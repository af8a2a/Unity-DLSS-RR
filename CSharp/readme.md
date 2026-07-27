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
    DLSSExposure.Identity,
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
    DLSSExposure.Identity,
    reset,
    frameTimeDeltaMs);
```

`VividRayTracingPixels` resolves to NGX scale `(1, 1)`. Do not multiply that
texture by the render dimensions again.

## Exposure contract

SR and RR use the same required `DLSSExposure` value:

```csharp
var exposure = new DLSSExposure(
    preExposure: framePreExposure,
    exposureScale: 1.0f,
    exposureTexture: finalExposureTexture);
```

- `PreExposure` is the exact multiplier already applied to the input color.
- `ExposureScale` is forwarded to NGX without being replaced by `1.0`.
- `ExposureTexture` is optional, but when supplied it must be a created 1x1,
  single-slice 2D `RenderTexture` containing the final exposure scale.
- `DLSSExposure.Identity` is the explicit choice for non-pre-exposed input.
- `DLSSFeatureFlags.AutoExposure` is a feature-creation option. It does not
  replace `PreExposure`; the latter must always match the color input.

For VividRP, the value in
`VividExposureData.preExposureBuffer[0].x` is the multiplier used by
`VividApplyPreExposure`, so it is the value that must reach `PreExposure`.
VividRP's HDRP exposure path already owns a 1x1 exposure history texture. Its
Unreal path currently owns only a `GraphicsBuffer`; that buffer cannot be
passed as `ExposureTexture` and needs a 1x1 texture bridge in the pipeline.

Do not synchronously read the GPU exposure buffer every frame just to fill the
CPU scalar. The pipeline should retain or publish the scalar used to build the
current frame, and schedule any exposure-texture conversion before the DLSS
evaluation pass.
