# PowerShell script to fetch NVIDIA DLSS headers and DLLs from GitHub
# This script avoids including the full DLSS repository in the project

param(
    [string]$DLSSVersion = "main",
    [string]$OutputDir = "External\NVIDIA-DLSS"
)

$ErrorActionPreference = "Stop"

Write-Host "Fetching NVIDIA DLSS SDK..." -ForegroundColor Green

# Create output directory structure
$IncludeDir = Join-Path $OutputDir "include"
$LibDir = Join-Path $OutputDir "lib"
$LibDirDev = Join-Path $LibDir "Dev"
$LibDirRel = Join-Path $LibDir "Rel"
$TempDir = Join-Path $env:TEMP "dlss_fetch_$(Get-Random)"

try {
    # Create directories
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    New-Item -ItemType Directory -Force -Path $IncludeDir | Out-Null
    New-Item -ItemType Directory -Force -Path $LibDir | Out-Null
    New-Item -ItemType Directory -Force -Path $LibDirDev | Out-Null
    New-Item -ItemType Directory -Force -Path $LibDirRel | Out-Null
    New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

    Write-Host "Created output directories" -ForegroundColor Cyan

    # Check if git is available
    $gitAvailable = $false
    try {
        $null = git --version
        $gitAvailable = $true
    } catch {
        Write-Host "Git not found, will use direct download method" -ForegroundColor Yellow
    }

    if ($gitAvailable) {
        Write-Host "Using git sparse checkout method..." -ForegroundColor Cyan
        
        Push-Location $TempDir
        
        # Initialize git repo and configure sparse checkout
        git init | Out-Null
        git remote add origin https://github.com/NVIDIA/DLSS.git
        git config core.sparseCheckout true
        
        # Configure sparse checkout paths
        $sparseConfig = @(
            "include/*",
            "lib/Windows_x86_64/rel/*",
            "lib/Windows_x86_64/dev/*",
            "lib/Windows_x86_64/x64/*"
        )
        $sparseConfig | Out-File -FilePath ".git/info/sparse-checkout" -Encoding ASCII
        
        # Fetch and checkout only the needed files
        Write-Host "Fetching from GitHub..." -ForegroundColor Cyan
        git fetch --depth 1 origin $DLSSVersion 2>&1 | Out-Null
        git checkout $DLSSVersion 2>&1 | Out-Null
        
        Pop-Location
        
        # Copy include files
        $SourceInclude = Join-Path $TempDir "include"
        if (Test-Path $SourceInclude) {
            Write-Host "Copying header files..." -ForegroundColor Cyan
            Copy-Item -Path "$SourceInclude\*" -Destination $IncludeDir -Recurse -Force
            Write-Host "Copied headers to $IncludeDir" -ForegroundColor Green
        } else {
            throw "Include directory not found in repository"
        }
        
        # Copy DLL files from release directory (rel) to Rel folder
        $SourceLibRel = Join-Path $TempDir "lib\Windows_x86_64\rel"
        if (Test-Path $SourceLibRel) {
            Write-Host "Copying release DLL files from rel directory..." -ForegroundColor Cyan
            Copy-Item -Path "$SourceLibRel\*.dll" -Destination $LibDirRel -Force -ErrorAction SilentlyContinue
            $relDlls = Get-ChildItem -Path $SourceLibRel -Filter "*.dll" -ErrorAction SilentlyContinue
            foreach ($dll in $relDlls) {
                Write-Host "  Copied release DLL: $($dll.Name)" -ForegroundColor Green
            }
        } else {
            Write-Host "Warning: Release DLL directory not found." -ForegroundColor Yellow
        }
        
        # Copy DLL files from development directory (dev) to Dev folder
        $SourceLibDev = Join-Path $TempDir "lib\Windows_x86_64\dev"
        if (Test-Path $SourceLibDev) {
            Write-Host "Copying debug DLL files from dev directory..." -ForegroundColor Cyan
            Copy-Item -Path "$SourceLibDev\*.dll" -Destination $LibDirDev -Force -ErrorAction SilentlyContinue
            $devDlls = Get-ChildItem -Path $SourceLibDev -Filter "*.dll" -ErrorAction SilentlyContinue
            foreach ($dll in $devDlls) {
                Write-Host "  Copied debug DLL: $($dll.Name)" -ForegroundColor Green
            }
        } else {
            Write-Host "Warning: Development DLL directory not found." -ForegroundColor Yellow
        }
        
        # Copy library files (.lib) from x64 directory to main lib folder
        # This includes nvsdk_ngx_d.lib for dynamic linking
        $SourceLibX64 = Join-Path $TempDir "lib\Windows_x86_64\x64"
        if (Test-Path $SourceLibX64) {
            Write-Host "Copying library files (.lib) from x64 directory..." -ForegroundColor Cyan
            $LibFiles = Get-ChildItem -Path $SourceLibX64 -Filter "*.lib" -ErrorAction SilentlyContinue
            foreach ($lib in $LibFiles) {
                Copy-Item -Path $lib.FullName -Destination $LibDir -Force
                Write-Host "  Copied library: $($lib.Name)" -ForegroundColor Green
            }
            if ($LibFiles.Count -eq 0) {
                Write-Host "  Warning: No .lib files found in x64 directory" -ForegroundColor Yellow
            }
        } else {
            Write-Host "Warning: x64 library directory not found. Library files may not be available." -ForegroundColor Yellow
        }
        
    } else {
        # Fallback: Download repository as zip
        Write-Host "Using zip download method..." -ForegroundColor Cyan
        $zipUrl = "https://github.com/NVIDIA/DLSS/archive/refs/heads/$DLSSVersion.zip"
        $zipFile = Join-Path $TempDir "dlss.zip"
        
        Write-Host "Downloading repository zip..." -ForegroundColor Cyan
        Invoke-WebRequest -Uri $zipUrl -OutFile $zipFile -UseBasicParsing
        
        Write-Host "Extracting zip file..." -ForegroundColor Cyan
        Expand-Archive -Path $zipFile -DestinationPath $TempDir -Force
        
        $ExtractedDir = Join-Path $TempDir "DLSS-$DLSSVersion"
        
        # Copy include files
        $SourceInclude = Join-Path $ExtractedDir "include"
        if (Test-Path $SourceInclude) {
            Write-Host "Copying header files..." -ForegroundColor Cyan
            Copy-Item -Path "$SourceInclude\*" -Destination $IncludeDir -Recurse -Force
            Write-Host "Copied headers to $IncludeDir" -ForegroundColor Green
        } else {
            throw "Include directory not found in repository"
        }
        
        # Copy DLL files from release directory (rel) to Rel folder
        $SourceLibRel = Join-Path $ExtractedDir "lib\Windows_x86_64\rel"
        if (Test-Path $SourceLibRel) {
            Write-Host "Copying release DLL files from rel directory..." -ForegroundColor Cyan
            Copy-Item -Path "$SourceLibRel\*.dll" -Destination $LibDirRel -Force -ErrorAction SilentlyContinue
            $relDlls = Get-ChildItem -Path $SourceLibRel -Filter "*.dll" -ErrorAction SilentlyContinue
            foreach ($dll in $relDlls) {
                Write-Host "  Copied release DLL: $($dll.Name)" -ForegroundColor Green
            }
        } else {
            Write-Host "Warning: Release DLL directory not found." -ForegroundColor Yellow
        }
        
        # Copy DLL files from development directory (dev) to Dev folder
        $SourceLibDev = Join-Path $ExtractedDir "lib\Windows_x86_64\dev"
        if (Test-Path $SourceLibDev) {
            Write-Host "Copying debug DLL files from dev directory..." -ForegroundColor Cyan
            Copy-Item -Path "$SourceLibDev\*.dll" -Destination $LibDirDev -Force -ErrorAction SilentlyContinue
            $devDlls = Get-ChildItem -Path $SourceLibDev -Filter "*.dll" -ErrorAction SilentlyContinue
            foreach ($dll in $devDlls) {
                Write-Host "  Copied debug DLL: $($dll.Name)" -ForegroundColor Green
            }
        } else {
            Write-Host "Warning: Development DLL directory not found." -ForegroundColor Yellow
        }
        
        # Copy library files (.lib) from x64 directory to main lib folder
        # This includes nvsdk_ngx_d.lib for dynamic linking
        $SourceLibX64 = Join-Path $ExtractedDir "lib\Windows_x86_64\x64"
        if (Test-Path $SourceLibX64) {
            Write-Host "Copying library files (.lib) from x64 directory..." -ForegroundColor Cyan
            $LibFiles = Get-ChildItem -Path $SourceLibX64 -Filter "*.lib" -ErrorAction SilentlyContinue
            foreach ($lib in $LibFiles) {
                Copy-Item -Path $lib.FullName -Destination $LibDir -Force
                Write-Host "  Copied library: $($lib.Name)" -ForegroundColor Green
            }
            if ($LibFiles.Count -eq 0) {
                Write-Host "  Warning: No .lib files found in x64 directory" -ForegroundColor Yellow
            }
        } else {
            Write-Host "Warning: x64 library directory not found. Library files may not be available." -ForegroundColor Yellow
        }
    }
    
    # Verify files were copied
    $headerCount = (Get-ChildItem -Path $IncludeDir -Recurse -File | Measure-Object).Count
    $relDllCount = (Get-ChildItem -Path $LibDirRel -File -Filter "*.dll" -ErrorAction SilentlyContinue | Measure-Object).Count
    $devDllCount = (Get-ChildItem -Path $LibDirDev -File -Filter "*.dll" -ErrorAction SilentlyContinue | Measure-Object).Count
    $libCount = (Get-ChildItem -Path $LibDir -File -Filter "*.lib" -ErrorAction SilentlyContinue | Measure-Object).Count
    
    Write-Host "`nSummary:" -ForegroundColor Green
    Write-Host "  Headers copied: $headerCount files" -ForegroundColor Cyan
    Write-Host "  Release DLLs copied: $relDllCount files (to $LibDirRel)" -ForegroundColor Cyan
    Write-Host "  Debug DLLs copied: $devDllCount files (to $LibDirDev)" -ForegroundColor Cyan
    Write-Host "  Libraries copied: $libCount files" -ForegroundColor Cyan
    Write-Host "`nDLSS SDK files are now available in: $OutputDir" -ForegroundColor Green
    
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    exit 1
} finally {
    # Cleanup temp directory
    if (Test-Path $TempDir) {
        Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "`nDone!" -ForegroundColor Green

