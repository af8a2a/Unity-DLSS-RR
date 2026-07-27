This directory contains the C# wrappers used by VividRP.

## Jitter and motion-vector contract

The public SR/RR wrappers take explicit coordinate descriptions instead of raw
NGX scale values:

```csharp
var jitter = DLSSJitterOffset.FromProjectionNdc(
    cameraData.jitter,
    renderSize);

superResolution.Render(
    cmd,
    colorInput,
    colorOutput,
    depth,
    motionVectors,
    jitter,
    DLSSMotionVectorEncoding.VividNormalizedUV,
    reset);
```

`VividNormalizedUV` matches VividRP's raster motion buffer
(`currentUV - previousUV`). It resolves to NGX scale
`(-renderWidth, -renderHeight)`.

The ray-tracing GBuffer already writes
`(previousUV - currentUV) * renderSize`, so Ray Reconstruction must use:

```csharp
rayReconstruction.Render(
    cmd,
    colorInput,
    colorOutput,
    depth,
    rayTracingMotionVectors,
    gbuffer,
    rayInputs,
    worldToView,
    viewToClip,
    jitter,
    DLSSMotionVectorEncoding.VividRayTracingPixels,
    reset,
    frameTimeDeltaMs);
```

`VividRayTracingPixels` resolves to NGX scale `(1, 1)`. Do not multiply that
texture by the render dimensions again.
