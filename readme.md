# Unity-DLSS

Unity-DLSS is a Unity native plugin with NVIDIA DLSS Super Resolution, Ray
Reconstruction, and optional DLSS 5 Neural Rendering support.

## DLSS Integration

This project integrates NVIDIA DLSS SDK without including the full repository in the source code to avoid open-source licensing risks.

### Setup DLSS SDK

Before building, you need to fetch the DLSS headers and DLLs:

```powershell
.\fetch_dlss.ps1
```

This script will:
- Download the DLSS SDK from GitHub (https://github.com/NVIDIA/DLSS)
- Extract header files from `include/` to `External/NVIDIA-DLSS/include`
- Extract DLL files from `lib/Windows_x86_64/rel/` to `External/NVIDIA-DLSS/lib`

### Build

After fetching DLSS, configure and build with CMake:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

DLSS 5 Neural Rendering is delivered as a separate NVIDIA-signed
`nvngx_dlssnr.dll` and is not downloaded by `fetch_dlss.ps1`. Configure its
path when it is available:

```powershell
cmake -S . -B build -DDLSSNR_DLL:FILEPATH="D:/path/to/nvngx_dlssnr.dll"
cmake --build build --config Release
```

The build copies the file next to `UnityDLSS.dll`. Without this option the
plugin still builds, and DLSS-SR/RR remain available; the managed
`IsNRSupported` property reports `false`.

The CMake configuration will automatically:
- Include DLSS header files in the build
- Copy DLSS DLLs to the output directory alongside your plugin DLL

### Project Structure

```
├── External/
│   └── NVIDIA-DLSS/          # DLSS SDK (fetched by script)
│       ├── include/          # DLSS header files
│       └── lib/              # DLSS DLL files
├── PluginAPI/                # Unity plugin API headers
├── src/                      # Plugin source code
├── CMakeLists.txt            # CMake configuration
└── fetch_dlss.ps1           # Script to fetch DLSS SDK
```

### Notes

- The DLSS SDK is fetched at build time, not stored in the repository
- Only the necessary files (headers and DLLs) are extracted
- The script supports both git sparse checkout and zip download methods
- `nvngx_dlssnr.dll` is not committed or redistributed by this repository
- DLSS 5 Neural Rendering uses D3D12 NGX feature 18 and is independent of
  DLSS Ray Reconstruction (feature 13)
