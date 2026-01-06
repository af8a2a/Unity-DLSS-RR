# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Unity Native Plugin (C++ DLL) providing NVIDIA DLSS integration for Unity. Supports DLSS Super Resolution, Frame Generation, and Dynamic Super Resolution via the NGX SDK.

## Build Commands

```powershell
# First-time setup: fetch DLSS SDK (required before building)
.\fetch_dlss.ps1

# Build
mkdir build
cd build
cmake ..
cmake --build .
```

Output: `build/bin/UnityPlugin.dll` with DLSS DLLs copied alongside.

## Build Configuration

- **CMake 3.20+** with **C++20** and **MSVC** required
- Debug builds use `External/NVIDIA-DLSS/lib/Dev/` DLLs
- Release builds use `External/NVIDIA-DLSS/lib/Rel/` DLLs
- Links against `nvsdk_ngx_d.lib` (or `nvsdk_ngx_d_dbg.lib` for debug)

## Architecture

### Plugin Entry Points
- `UnityPluginLoad()` - Unity calls this to initialize; registers graphics device callbacks
- `UnityPluginUnload()` - Cleanup; unregisters callbacks
- `OnGraphicsDeviceEvent()` - Handles graphics device lifecycle (Initialize, Shutdown, Reset)

### Key Globals (src/Plugin.cpp)
- `g_unityInterfaces` - Registry for Unity interfaces
- `g_unityGraphics` - Graphics API abstraction
- `g_unityGraphics_D3D12` - D3D12-specific interface (v8)
- `g_renderer` - Atomic renderer type for thread-safe state

### Interface Pattern
Unity interfaces use `IUnityInterfaces::Get<T>()` template with GUID-based lookup. Key interfaces in `PluginAPI/`:
- `IUnityGraphics.h` - Graphics device abstraction
- `IUnityGraphicsD3D12.h` - D3D12 command queue/resource access

### DLSS SDK
Headers in `External/NVIDIA-DLSS/include/`:
- `nvsdk_ngx.h` - Main NGX API
- `nvsdk_ngx_helpers.h` - Helper functions
- `nvsdk_ngx_defs_dlssd.h` / `nvsdk_ngx_defs_dlssg.h` - DLSS-D and Frame Gen definitions

## Notes

- DLSS SDK is fetched at build time, not stored in repo
- Plugin currently targets D3D12; Vulkan support is stubbed
- Uses WRL COM smart pointers for D3D12 resources
