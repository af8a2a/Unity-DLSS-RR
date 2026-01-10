# DLSS Unity Integration Guide

This guide explains how to integrate the DLSS native plugin into your Unity project, supporting both **DLSS-SR** (Super Resolution) and **DLSS-RR** (Ray Reconstruction).

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Installation](#installation)
3. [Quick Start](#quick-start)
4. [DLSS-SR Integration](#dlss-sr-integration)
5. [DLSS-RR Integration](#dlss-rr-integration)
6. [Render Pipeline Integration](#render-pipeline-integration)
7. [Advanced Topics](#advanced-topics)
8. [Troubleshooting](#troubleshooting)
9. [API Reference](#api-reference)

---

## Prerequisites

- **Unity 2021.3+** with D3D12 graphics API
- **NVIDIA RTX GPU** (GeForce RTX 20xx or newer)
- **Driver Version**: 531.0+ for SR, 545.0+ for RR
- **Windows 10/11** (64-bit)

---

## Installation

### 1. Copy Plugin Files

Copy the following files to your Unity project's `Assets/Plugins/x86_64/` folder:

```
UnityPlugin.dll      # Main plugin
nvngx_dlss.dll       # DLSS-SR runtime
nvngx_dlssd.dll      # DLSS-RR runtime
nvngx_dlssg.dll      # DLSS Frame Generation (optional)
```

### 2. Copy C# Wrapper

Copy `CSharp/DLSSPlugin.cs` to your Unity project's `Assets/Scripts/DLSS/` folder.

### 3. Configure Player Settings

In **Edit > Project Settings > Player**:

- Set **Graphics API** to **Direct3D12**
- Disable **Auto Graphics API** and ensure D3D12 is first in the list

---

## Quick Start

### Minimal SR Example

```csharp
using UnityEngine;
using UnityEngine.Rendering;
using DLSS;

public class DLSSQuickStart : MonoBehaviour
{
    private uint _viewId;
    private RenderTexture _colorInput;
    private RenderTexture _colorOutput;
    private RenderTexture _depth;
    private RenderTexture _motionVectors;

    void Start()
    {
        // Initialize DLSS
        if (!DLSSManager.Initialize())
        {
            Debug.LogError("DLSS initialization failed!");
            return;
        }

        // Check capabilities
        if (DLSSManager.TryGetCapabilities(out var caps))
        {
            Debug.Log($"DLSS-SR: {caps.IsSRAvailable}, DLSS-RR: {caps.IsRRAvailable}");
        }

        // Get optimal render resolution
        uint outputWidth = (uint)Screen.width;
        uint outputHeight = (uint)Screen.height;

        if (DLSSManager.TryGetOptimalSettings(
            DLSSMode.SuperResolution,
            DLSSQuality.Balanced,
            outputWidth, outputHeight,
            out var settings))
        {
            // Create render textures at optimal resolution
            CreateRenderTextures(settings.optimalRenderWidth, settings.optimalRenderHeight,
                                 outputWidth, outputHeight);
        }

        // Create DLSS context
        _viewId = (uint)GetInstanceID();
        DLSSManager.CreateSRContext(
            _viewId,
            DLSSQuality.Balanced,
            settings.optimalRenderWidth, settings.optimalRenderHeight,
            outputWidth, outputHeight,
            DLSSFeatureFlags.DepthInverted | DLSSFeatureFlags.MVLowRes);
    }

    void OnDestroy()
    {
        DLSSManager.DestroyContext(_viewId);
        DLSSManager.Shutdown();
    }
}
```

---

## DLSS-SR Integration

### Step 1: Initialize DLSS

Initialize once at application startup (e.g., in a singleton or ScriptableRenderPipeline):

```csharp
public class DLSSInitializer
{
    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    static void Initialize()
    {
        // Optional: Provide NVIDIA app ID for profile matching
        ulong appId = 0;  // 0 = generic
        string projectId = "my-unity-project";
        string engineVersion = Application.unityVersion;
        string logPath = Application.persistentDataPath + "/DLSS";

        var result = DLSSNative.DLSS_Initialize(appId, projectId, engineVersion, logPath);

        if (result != DLSSResult.Success)
        {
            Debug.LogError($"DLSS init failed: {DLSSNative.DLSS_GetResultString(result)}");
        }
    }
}
```

### Step 2: Query Optimal Settings

Before creating a context, query the optimal render resolution:

```csharp
public static Vector2Int GetOptimalRenderSize(DLSSQuality quality, int outputWidth, int outputHeight)
{
    if (DLSSNative.DLSS_GetOptimalSettings(
        DLSSMode.SuperResolution,
        quality,
        (uint)outputWidth,
        (uint)outputHeight,
        out var settings) == DLSSResult.Success)
    {
        return new Vector2Int((int)settings.optimalRenderWidth, (int)settings.optimalRenderHeight);
    }

    // Fallback: use output resolution
    return new Vector2Int(outputWidth, outputHeight);
}
```

### Step 3: Create Context (Per Camera)

Create a DLSS context for each camera that needs upscaling:

```csharp
public void SetupDLSSContext(Camera camera, DLSSQuality quality)
{
    uint viewId = (uint)camera.GetInstanceID();

    // Destroy existing context if any
    if (DLSSNative.DLSS_HasContext(viewId) != 0)
    {
        DLSSNative.DLSS_DestroyContext(viewId);
    }

    // Get optimal settings
    var outputSize = new Vector2Int(camera.pixelWidth, camera.pixelHeight);
    var renderSize = GetOptimalRenderSize(quality, outputSize.x, outputSize.y);

    // Configure feature flags based on your pipeline
    var flags = DLSSFeatureFlags.DepthInverted  // Unity uses reversed-Z
              | DLSSFeatureFlags.MVLowRes;       // Motion vectors at render resolution

    var createParams = new DLSSContextCreateParams
    {
        mode = DLSSMode.SuperResolution,
        quality = quality,
        inputResolution = new DLSSDimensions((uint)renderSize.x, (uint)renderSize.y),
        outputResolution = new DLSSDimensions((uint)outputSize.x, (uint)outputSize.y),
        featureFlags = (uint)flags,

        // SR presets (use Default or K for best quality)
        presetDLAA = DLSSSRPreset.Default,
        presetQuality = DLSSSRPreset.K,
        presetBalanced = DLSSSRPreset.Default,
        presetPerformance = DLSSSRPreset.Default,
        presetUltraPerformance = DLSSSRPreset.L,
        presetUltraQuality = DLSSSRPreset.K
    };

    var result = DLSSNative.DLSS_CreateContext(viewId, ref createParams);

    if (result != DLSSResult.Success)
    {
        Debug.LogError($"Failed to create DLSS context: {DLSSNative.DLSS_GetResultString(result)}");
    }
}
```

### Step 4: Execute DLSS (Per Frame)

Execute DLSS after rendering at low resolution, before post-processing:

```csharp
public void ExecuteDLSS(
    Camera camera,
    RenderTexture colorInput,
    RenderTexture colorOutput,
    RenderTexture depth,
    RenderTexture motionVectors,
    Vector2 jitterOffset,
    bool reset = false)
{
    uint viewId = (uint)camera.GetInstanceID();

    var executeParams = new DLSSExecuteParams
    {
        mode = DLSSMode.SuperResolution,

        textures = new DLSSCommonTextures
        {
            colorInput = colorInput.GetNativeTexturePtr(),
            colorOutput = colorOutput.GetNativeTexturePtr(),
            depth = depth.GetNativeTexturePtr(),
            motionVectors = motionVectors.GetNativeTexturePtr(),
            exposureTexture = IntPtr.Zero,  // Optional
            biasColorMask = IntPtr.Zero     // Optional
        },

        common = new DLSSCommonParams
        {
            // Jitter in pixel space (render resolution)
            jitterOffsetX = jitterOffset.x,
            jitterOffsetY = jitterOffset.y,

            // Motion vector scale (typically render resolution)
            mvScaleX = colorInput.width,
            mvScaleY = colorInput.height,

            // Actual rendered dimensions
            renderSubrectDimensions = new DLSSDimensions(
                (uint)colorInput.width,
                (uint)colorInput.height),

            // Reset temporal history on scene change
            reset = reset ? (byte)1 : (byte)0,

            // Exposure (1.0 if not using auto-exposure)
            preExposure = 1.0f,
            exposureScale = 1.0f
        }
    };

    var result = DLSSNative.DLSS_Execute(viewId, ref executeParams);

    if (result != DLSSResult.Success)
    {
        Debug.LogError($"DLSS execute failed: {DLSSNative.DLSS_GetResultString(result)}");
    }
}
```

### Step 5: Cleanup

Destroy contexts when cameras are disabled or destroyed:

```csharp
void OnDisable()
{
    DLSSNative.DLSS_DestroyContext((uint)camera.GetInstanceID());
}

// On application quit
void OnApplicationQuit()
{
    DLSSNative.DLSS_DestroyAllContexts();
    DLSSNative.DLSS_Shutdown();
}
```

---

## DLSS-RR Integration

DLSS-RR (Ray Reconstruction) replaces traditional denoisers for ray-traced effects. It requires additional GBuffer and ray data inputs.

### Required Textures for RR

| Texture | Format | Description |
|---------|--------|-------------|
| `colorInput` | RGBA16F/RGBA32F | Noisy ray-traced color (diffuse + specular combined) |
| `colorOutput` | RGBA16F/RGBA32F | Upscaled denoised output |
| `depth` | D32F/D24S8 | Depth buffer |
| `motionVectors` | RG16F | Screen-space motion vectors |
| `diffuseAlbedo` | RGBA8/RGBA16F | Diffuse albedo from GBuffer |
| `specularAlbedo` | RGBA8/RGBA16F | Specular albedo from GBuffer |
| `normals` | RGBA16F | World-space normals (optionally with roughness in .w) |
| `diffuseRayDirection` | RGB16F | Normalized diffuse ray direction |
| `diffuseHitDistance` | R16F | Diffuse ray hit distance |
| `specularRayDirection` | RGB16F | Normalized specular ray direction |
| `specularHitDistance` | R16F | Specular ray hit distance |

### Create RR Context

```csharp
public void SetupDLSSRRContext(Camera camera, DLSSQuality quality)
{
    uint viewId = (uint)camera.GetInstanceID();

    var outputSize = new Vector2Int(camera.pixelWidth, camera.pixelHeight);

    // Query RR optimal settings
    DLSSNative.DLSS_GetOptimalSettings(
        DLSSMode.RayReconstruction,
        quality,
        (uint)outputSize.x,
        (uint)outputSize.y,
        out var settings);

    var renderSize = new Vector2Int(
        (int)settings.optimalRenderWidth,
        (int)settings.optimalRenderHeight);

    var flags = DLSSFeatureFlags.DepthInverted
              | DLSSFeatureFlags.MVLowRes
              | DLSSFeatureFlags.IsHDR;  // RR typically uses HDR

    var createParams = new DLSSContextCreateParams
    {
        mode = DLSSMode.RayReconstruction,
        quality = quality,
        inputResolution = new DLSSDimensions((uint)renderSize.x, (uint)renderSize.y),
        outputResolution = new DLSSDimensions((uint)outputSize.x, (uint)outputSize.y),
        featureFlags = (uint)flags,

        // RR-specific settings
        denoiseMode = DLSSDenoiseMode.DLUnified,
        depthType = DLSSDepthType.Hardware,
        roughnessMode = DLSSRoughnessMode.Unpacked,  // Or PackedInNormalsW

        // RR presets (E is latest with DoF support)
        presetRR_DLAA = DLSSRRPreset.E,
        presetRR_Quality = DLSSRRPreset.E,
        presetRR_Balanced = DLSSRRPreset.E,
        presetRR_Performance = DLSSRRPreset.E,
        presetRR_UltraPerformance = DLSSRRPreset.E,
        presetRR_UltraQuality = DLSSRRPreset.E
    };

    var result = DLSSNative.DLSS_CreateContext(viewId, ref createParams);

    if (result != DLSSResult.Success)
    {
        Debug.LogError($"Failed to create DLSS-RR context: {DLSSNative.DLSS_GetResultString(result)}");
    }
}
```

### Execute DLSS-RR

```csharp
public void ExecuteDLSSRR(
    Camera camera,
    // Common textures
    RenderTexture colorInput,
    RenderTexture colorOutput,
    RenderTexture depth,
    RenderTexture motionVectors,
    // GBuffer
    RenderTexture diffuseAlbedo,
    RenderTexture specularAlbedo,
    RenderTexture normals,
    RenderTexture roughness,  // null if packed in normals.w
    // Ray data
    RenderTexture diffuseRayDirection,
    RenderTexture diffuseHitDistance,
    RenderTexture specularRayDirection,
    RenderTexture specularHitDistance,
    // Per-frame data
    Vector2 jitterOffset,
    Matrix4x4 worldToView,
    Matrix4x4 viewToClip,
    float deltaTimeMs,
    bool reset = false)
{
    uint viewId = (uint)camera.GetInstanceID();

    var executeParams = new DLSSExecuteParams
    {
        mode = DLSSMode.RayReconstruction,

        textures = new DLSSCommonTextures
        {
            colorInput = colorInput.GetNativeTexturePtr(),
            colorOutput = colorOutput.GetNativeTexturePtr(),
            depth = depth.GetNativeTexturePtr(),
            motionVectors = motionVectors.GetNativeTexturePtr()
        },

        common = new DLSSCommonParams
        {
            jitterOffsetX = jitterOffset.x,
            jitterOffsetY = jitterOffset.y,
            mvScaleX = colorInput.width,
            mvScaleY = colorInput.height,
            renderSubrectDimensions = new DLSSDimensions(
                (uint)colorInput.width,
                (uint)colorInput.height),
            reset = reset ? (byte)1 : (byte)0,
            preExposure = 1.0f,
            exposureScale = 1.0f
        },

        rrParams = new DLSSRRParams
        {
            gbuffer = new DLSSRRGBufferTextures
            {
                diffuseAlbedo = diffuseAlbedo.GetNativeTexturePtr(),
                specularAlbedo = specularAlbedo.GetNativeTexturePtr(),
                normals = normals.GetNativeTexturePtr(),
                roughness = roughness != null ? roughness.GetNativeTexturePtr() : IntPtr.Zero
            },

            rays = new DLSSRRRayTextures
            {
                diffuseRayDirection = diffuseRayDirection.GetNativeTexturePtr(),
                diffuseHitDistance = diffuseHitDistance.GetNativeTexturePtr(),
                specularRayDirection = specularRayDirection.GetNativeTexturePtr(),
                specularHitDistance = specularHitDistance.GetNativeTexturePtr()
            },

            // Matrices (required for RR temporal stability)
            worldToViewMatrix = worldToView,
            viewToClipMatrix = viewToClip,

            frameTimeDeltaMs = deltaTimeMs
        }
    };

    var result = DLSSNative.DLSS_Execute(viewId, ref executeParams);

    if (result != DLSSResult.Success)
    {
        Debug.LogError($"DLSS-RR execute failed: {DLSSNative.DLSS_GetResultString(result)}");
    }
}
```

---

## Render Pipeline Integration

### SRP (URP/HDRP) Integration Pattern

```csharp
public class DLSSRenderPass : ScriptableRenderPass
{
    private DLSSSettings _settings;
    private uint _viewId;
    private bool _contextCreated;

    public override void Execute(ScriptableRenderContext context, ref RenderingData renderingData)
    {
        var camera = renderingData.cameraData.camera;
        _viewId = (uint)camera.GetInstanceID();

        // Ensure context exists
        if (!_contextCreated)
        {
            CreateContext(camera);
            _contextCreated = true;
        }

        var cmd = CommandBufferPool.Get("DLSS");

        // Get textures from render pipeline
        var colorInput = /* your low-res color */;
        var colorOutput = /* your output target */;
        var depth = /* depth texture */;
        var motionVectors = /* motion vector texture */;

        // Execute DLSS
        ExecuteDLSS(cmd, camera, colorInput, colorOutput, depth, motionVectors);

        context.ExecuteCommandBuffer(cmd);
        CommandBufferPool.Release(cmd);
    }

    private void ExecuteDLSS(CommandBuffer cmd, Camera camera,
        RTHandle colorInput, RTHandle colorOutput, RTHandle depth, RTHandle motionVectors)
    {
        // Option 1: Direct execution (immediate)
        var executeParams = CreateExecuteParams(colorInput, colorOutput, depth, motionVectors);
        DLSSNative.DLSS_Execute(_viewId, ref executeParams);

        // Option 2: Render event callback (deferred)
        // DLSSNative.DLSS_SetCurrentView(_viewId);
        // DLSSNative.DLSS_SetExecuteParams(ref executeParams);
        // cmd.IssuePluginEvent(DLSSNative.DLSS_GetRenderEventFunc(), DLSSNative.DLSS_RENDER_EVENT_ID);
    }
}
```

### Jitter Pattern (Halton Sequence)

DLSS requires sub-pixel jitter for temporal accumulation:

```csharp
public static class DLSSJitter
{
    private static int _frameIndex = 0;
    private const int JITTER_PHASE_COUNT = 8;

    public static Vector2 GetJitter(int renderWidth, int renderHeight)
    {
        _frameIndex = (_frameIndex + 1) % JITTER_PHASE_COUNT;

        float x = HaltonSequence(2, _frameIndex + 1) - 0.5f;
        float y = HaltonSequence(3, _frameIndex + 1) - 0.5f;

        return new Vector2(x, y);
    }

    public static Matrix4x4 GetJitteredProjectionMatrix(Camera camera, Vector2 jitter)
    {
        var proj = camera.projectionMatrix;

        // Apply jitter in clip space
        proj.m02 += jitter.x * 2.0f / camera.pixelWidth;
        proj.m12 += jitter.y * 2.0f / camera.pixelHeight;

        return proj;
    }

    private static float HaltonSequence(int baseValue, int index)
    {
        float result = 0;
        float fraction = 1.0f / baseValue;

        while (index > 0)
        {
            result += (index % baseValue) * fraction;
            index /= baseValue;
            fraction /= baseValue;
        }

        return result;
    }
}
```

---

## Advanced Topics

### Quality Mode Selection

```csharp
public static DLSSQuality RecommendQuality(int outputWidth, int outputHeight)
{
    int pixels = outputWidth * outputHeight;

    if (pixels >= 3840 * 2160)      // 4K
        return DLSSQuality.Balanced;
    else if (pixels >= 2560 * 1440) // 1440p
        return DLSSQuality.MaxQuality;
    else if (pixels >= 1920 * 1080) // 1080p
        return DLSSQuality.DLAA;     // No upscaling needed
    else
        return DLSSQuality.MaxQuality;
}
```

### Dynamic Resolution

When resolution changes, update the DLSS context:

```csharp
public void OnResolutionChanged(int newRenderWidth, int newRenderHeight, int outputWidth, int outputHeight)
{
    var updateParams = new DLSSContextCreateParams
    {
        mode = _currentMode,
        quality = _currentQuality,
        inputResolution = new DLSSDimensions((uint)newRenderWidth, (uint)newRenderHeight),
        outputResolution = new DLSSDimensions((uint)outputWidth, (uint)outputHeight),
        featureFlags = _currentFlags
        // ... other params
    };

    // UpdateContext will recreate if needed
    var result = DLSSNative.DLSS_UpdateContext(_viewId, ref updateParams);

    if (result != DLSSResult.Success)
    {
        Debug.LogWarning($"DLSS context update failed: {DLSSNative.DLSS_GetResultString(result)}");
    }
}
```

### Multi-Camera Support

Each camera needs its own DLSS context with a unique view ID:

```csharp
public class DLSSCameraManager : MonoBehaviour
{
    private Dictionary<Camera, uint> _cameraContexts = new Dictionary<Camera, uint>();
    private uint _nextViewId = 1;

    public uint GetOrCreateContext(Camera camera, DLSSQuality quality)
    {
        if (!_cameraContexts.TryGetValue(camera, out uint viewId))
        {
            viewId = _nextViewId++;
            _cameraContexts[camera] = viewId;

            // Create context for this camera
            SetupContext(viewId, camera, quality);
        }

        return viewId;
    }

    public void ReleaseContext(Camera camera)
    {
        if (_cameraContexts.TryGetValue(camera, out uint viewId))
        {
            DLSSNative.DLSS_DestroyContext(viewId);
            _cameraContexts.Remove(camera);
        }
    }
}
```

### Reset Temporal History

Reset temporal history when:
- Scene changes / level loads
- Camera teleports (large discontinuity)
- Camera cuts in cinematics

```csharp
// In your execute call
common.reset = (byte)(shouldReset ? 1 : 0);

// Example detection
bool shouldReset = Vector3.Distance(camera.transform.position, _lastPosition) > 5.0f
                || Time.frameCount == 1
                || SceneManager.GetActiveScene() != _lastScene;
```

---

## Logging

The DLSS plugin provides comprehensive logging via Unity's native `IUnityLog` interface. **Logging is automatic** - no setup required!

### Automatic Unity Console Logging

By default, all DLSS log messages at `Info` level and above are automatically output to Unity Console:

```
[DLSS] Initializing DLSS plugin (appId=0, projectId=my-project, engineVersion=2023.2)
[DLSS] DLSS initialized successfully - SR: available, RR: available
[DLSS] Creating DLSS context (viewId=12345, mode=SR, quality=Balanced, input=1920x1080, output=3840x2160)
[DLSS] DLSS context created successfully for viewId 12345
```

### Log Levels

| Level | Description | Default |
|-------|-------------|---------|
| `Debug` | Verbose trace information | Not shown |
| `Info` | Important operations (init, context creation) | **Shown** |
| `Warning` | Non-fatal issues | **Shown** |
| `Error` | Fatal errors | **Shown** |

### Change Log Level

```csharp
// Show all logs including Debug
DLSSManager.SetLogLevel(DLSSLogLevel.Debug);

// Only show warnings and errors
DLSSManager.SetLogLevel(DLSSLogLevel.Warning);

// Get current level
var level = DLSSManager.GetLogLevel();
```

### Custom Log Callback (Advanced)

If you need custom log handling (e.g., file logging, analytics), you can override the default Unity logging:

```csharp
// Override default Unity Console logging
DLSSManager.SetCustomLogCallback((level, message) =>
{
    // Send to your custom logging system
    MyLogger.Write($"{level}: {message}");

    // Or filter specific messages
    if (level == DLSSLogLevel.Error)
    {
        Analytics.LogError(message);
    }
});

// Restore default Unity Console logging
DLSSManager.ResetLoggingToDefault();
```

### Example Log Output

With `DLSSLogLevel.Info` (default):

```
[DLSS] Initializing DLSS plugin (appId=0, projectId=my-project, engineVersion=2023.2)
[DLSS] Initializing NGX SDK...
[DLSS] NGX SDK initialized, querying capabilities...
[DLSS] NGX feature availability queried - SR: 1, RR: 1
[DLSS] DLSS initialized successfully - SR: available, RR: available
[DLSS] Creating DLSS context (viewId=12345, mode=SR, quality=Balanced, input=1920x1080, output=3840x2160)
[DLSS] DLSS context created successfully for viewId 12345
```

With `DLSSLogLevel.Debug`:

```
[DLSS] Executing DLSS for viewId 12345 (mode=SR, reset=0)
[DLSS] DLSSContext::Create - Creating DLSS-SR feature handle...
```

---

## Troubleshooting

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| `Fail_NotInitialized` | DLSS_Initialize not called | Call Initialize before any other DLSS function |
| `Fail_FeatureNotSupported` | Non-RTX GPU or old driver | Check GPU compatibility, update drivers |
| `Fail_InvalidParameter` | Null texture or invalid dimensions | Verify all required textures are set |
| `Fail_ContextNotFound` | Context not created for viewId | Call CreateContext before Execute |
| Ghosting/smearing | Missing or incorrect motion vectors | Verify MV format and scale |
| Flickering | Missing jitter or incorrect jitter scale | Ensure jitter is applied to projection matrix |

### Debug Logging

```csharp
void LogDLSSState()
{
    Debug.Log($"DLSS Initialized: {DLSSNative.DLSS_IsInitialized()}");

    if (DLSSNative.DLSS_GetCapabilities(out var caps) == DLSSResult.Success)
    {
        Debug.Log($"SR Available: {caps.IsSRAvailable}");
        Debug.Log($"RR Available: {caps.IsRRAvailable}");
        Debug.Log($"Needs Driver Update: {caps.NeedsDriverUpdate}");
        Debug.Log($"Min Driver: {caps.minDriverVersionMajor}.{caps.minDriverVersionMinor}");
    }

    if (DLSSNative.DLSS_GetStats(DLSSMode.SuperResolution, out var stats) == DLSSResult.Success)
    {
        Debug.Log($"VRAM Used: {stats.VRAMAllocatedMB:F2} MB");
    }

    int ngxError = DLSSNative.DLSS_GetLastNGXError();
    if (ngxError != 0)
    {
        Debug.LogWarning($"Last NGX Error: 0x{ngxError:X8}");
    }
}
```

---

## API Reference

### Initialization

| Function | Description |
|----------|-------------|
| `DLSS_Initialize(appId, projectId, engineVersion, logPath)` | Initialize DLSS subsystem |
| `DLSS_Shutdown()` | Shutdown and release all resources |
| `DLSS_IsInitialized()` | Check if initialized (returns byte) |

### Capability Queries

| Function | Description |
|----------|-------------|
| `DLSS_GetCapabilities(out DLSSCapabilityInfo)` | Query feature availability |
| `DLSS_GetOptimalSettings(mode, quality, w, h, out DLSSOptimalSettings)` | Get optimal render resolution |
| `DLSS_GetStats(mode, out DLSSStats)` | Get VRAM usage statistics |

### Context Management

| Function | Description |
|----------|-------------|
| `DLSS_CreateContext(viewId, ref DLSSContextCreateParams)` | Create context for a view |
| `DLSS_DestroyContext(viewId)` | Destroy a specific context |
| `DLSS_DestroyAllContexts()` | Destroy all contexts |
| `DLSS_HasContext(viewId)` | Check if context exists (returns byte) |
| `DLSS_UpdateContext(viewId, ref DLSSContextCreateParams)` | Update/recreate context |

### Execution

| Function | Description |
|----------|-------------|
| `DLSS_Execute(viewId, ref DLSSExecuteParams)` | Execute DLSS immediately |
| `DLSS_ExecuteOnCommandList(viewId, cmdList, ref DLSSExecuteParams)` | Execute on specific command list |
| `DLSS_GetRenderEventFunc()` | Get callback for CommandBuffer.IssuePluginEvent |
| `DLSS_SetCurrentView(viewId)` | Set view for render event callback |
| `DLSS_SetExecuteParams(ref DLSSExecuteParams)` | Set params for render event callback |

### Debug

| Function | Description |
|----------|-------------|
| `DLSS_GetLastNGXError()` | Get last NGX error code |
| `DLSS_GetResultString(result)` | Get human-readable error string |

---

## Version History

| Version | Changes |
|---------|---------|
| 1.0.0 | Initial release with DLSS-SR and DLSS-RR support |

---

## License

This plugin uses NVIDIA NGX SDK. See NVIDIA's license terms for redistribution requirements.
