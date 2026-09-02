//------------------------------------------------------------------------------
// DLSSNeuralRendering.cs - standalone DLSS 5 Neural Rendering wrapper
//------------------------------------------------------------------------------

using System;
using UnityEngine;
using UnityEngine.Rendering;

namespace UnityEngine.Rendering.Universal
{
    /// <summary>Feature-18 render preset exposed by nvngx_dlssnr.dll.</summary>
    public enum DLSSNeuralRenderingPreset : int
    {
        Default = 0,
        Preset1 = 1,
        Preset2 = 2,
        Preset3 = 3
    }

    /// <summary>Image style exposed by nvngx_dlssnr.dll.</summary>
    public enum DLSSNeuralRenderingStyle : int
    {
        Default = 0,
        Natural = 1,
        Cinematic = 2
    }

    /// <summary>
    /// Mutable per-frame controls for standalone DLSS 5 Neural Rendering.
    /// The input color, depth, and motion-vector textures are expected to be
    /// rasterized image-space inputs; this feature does not consume the
    /// GBuffer/ray inputs required by DLSS Ray Reconstruction.
    /// </summary>
    public sealed class DLSSNeuralRenderingSettings
    {
        public DLSSNeuralRenderingPreset Preset = DLSSNeuralRenderingPreset.Default;
        public DLSSNeuralRenderingStyle Style = DLSSNeuralRenderingStyle.Default;
        public float Intensity = 1.0f;
        public float LocalToneStrength = 1.0f;
        public float LocalStructureStrength = 1.0f;
        public float SkinStructureStrength = -1.0f;
        public bool DepthInverted = SystemInfo.usesReversedZBuffer;
        public bool UseAutoMask = false;
        public bool UICorrection = false;

        /// <summary>
        /// Enables the runtime's fixed 2x path. When false, input and output
        /// dimensions must match; when true, output must be exactly 2x input.
        /// Changing this value recreates the native feature.
        /// </summary>
        public bool Upscaling = false;

        /// <summary>
        /// Additional multiplier applied after DLSSMotionVectorEncoding has
        /// converted motion to current-to-previous input pixels.
        /// </summary>
        public Vector2 MotionVectorScale = Vector2.one;

        internal bool TryValidate(out string error)
        {
            if (!Enum.IsDefined(typeof(DLSSNeuralRenderingPreset), Preset))
            {
                error = $"Unknown preset value: {(int)Preset}.";
                return false;
            }

            if (!Enum.IsDefined(typeof(DLSSNeuralRenderingStyle), Style))
            {
                error = $"Unknown style value: {(int)Style}.";
                return false;
            }

            if (!IsFinite(Intensity) || Intensity < 0.0f || Intensity > 2.0f ||
                !IsFinite(LocalToneStrength) || LocalToneStrength < 0.0f || LocalToneStrength > 2.0f ||
                !IsFinite(LocalStructureStrength) || LocalStructureStrength < 0.0f || LocalStructureStrength > 2.0f ||
                !IsFinite(SkinStructureStrength) || SkinStructureStrength < -1.0f || SkinStructureStrength > 2.0f)
            {
                error = "Image controls are outside their supported ranges.";
                return false;
            }

            if (!IsFinite(MotionVectorScale.x) || !IsFinite(MotionVectorScale.y))
            {
                error = "MotionVectorScale must contain finite values.";
                return false;
            }

            error = null;
            return true;
        }

        private static bool IsFinite(float value)
        {
            return !float.IsNaN(value) && !float.IsInfinity(value);
        }
    }

    /// <summary>
    /// Owns one standalone DLSS 5 Neural Rendering feature (NGX feature 18).
    /// Availability means the signed runtime initialized; native creation may
    /// still fail when the current GPU or driver does not support the feature.
    /// </summary>
    public sealed class DLSSNeuralRendering : IDisposable
    {
#if DLSS_PLUGIN_INTEGRATE
        private int m_handle = DLSSExtension.DLSS_INVALID_FEATURE_HANDLE;
        private bool m_initialized;
        private bool m_createFailed;
        private bool m_disposed;
        private uint m_inputWidth;
        private uint m_inputHeight;
        private uint m_outputWidth;
        private uint m_outputHeight;
        private DLSSNeuralRenderingPreset m_preset;
        private bool m_upscaling;
        private DLSSExtension m_extension;

        private DLSSExtension Extension
        {
            get
            {
                if (m_extension == null)
                    m_extension = DLSSExtension.Instance;
                return m_extension;
            }
        }

        public bool IsSupported => Extension?.IsNRSupported ?? false;

        /// <summary>
        /// Record Neural Rendering into a Unity command buffer.
        /// </summary>
        /// <param name="colorInput">Rasterized color at input resolution.</param>
        /// <param name="colorOutput">Distinct UAV-capable destination.</param>
        /// <param name="depth">Raw device-depth texture at input resolution.</param>
        /// <param name="motionVectors">Motion texture at input resolution.</param>
        /// <param name="motionVectorEncoding">Units and direction stored in the motion texture.</param>
        /// <param name="settings">Per-frame controls and feature preset.</param>
        /// <param name="reset">Reset temporal history, for example after a camera cut.</param>
        /// <returns>True only when an evaluation event was queued.</returns>
        public bool Render(
            CommandBuffer cmd,
            RenderTexture colorInput,
            RenderTexture colorOutput,
            RenderTexture depth,
            RenderTexture motionVectors,
            DLSSMotionVectorEncoding motionVectorEncoding,
            DLSSNeuralRenderingSettings settings,
            bool reset = false)
        {
            if (m_disposed)
                throw new ObjectDisposedException(nameof(DLSSNeuralRendering));

            if (!IsSupported || Extension == null)
            {
                Debug.LogError("[DLSSNeuralRendering] DLSS 5 Neural Rendering is not supported");
                RecordFallback(cmd, colorInput, colorOutput);
                return false;
            }

            if (!ValidateInputs(
                    colorInput,
                    colorOutput,
                    depth,
                    motionVectors,
                    settings,
                    out string validationError))
            {
                Debug.LogError($"[DLSSNeuralRendering] {validationError}");
                RecordFallback(cmd, colorInput, colorOutput);
                return false;
            }

            uint inputWidth = (uint)colorInput.width;
            uint inputHeight = (uint)colorInput.height;
            uint outputWidth = (uint)colorOutput.width;
            uint outputHeight = (uint)colorOutput.height;
            bool createParametersChanged =
                m_inputWidth != inputWidth ||
                m_inputHeight != inputHeight ||
                m_outputWidth != outputWidth ||
                m_outputHeight != outputHeight ||
                m_preset != settings.Preset ||
                m_upscaling != settings.Upscaling;

            if (createParametersChanged)
            {
                if (!DisposeResources(cmd))
                {
                    RecordFallback(cmd, colorInput, colorOutput);
                    return false;
                }

                m_inputWidth = inputWidth;
                m_inputHeight = inputHeight;
                m_outputWidth = outputWidth;
                m_outputHeight = outputHeight;
                m_preset = settings.Preset;
                m_upscaling = settings.Upscaling;
                m_createFailed = false;
            }

            if (!EnsureInitialized(cmd))
            {
                RecordFallback(cmd, colorInput, colorOutput);
                return false;
            }

            Vector2 motionVectorScale = motionVectorEncoding.GetNGXPixelScale(
                colorInput.width,
                colorInput.height);
            motionVectorScale = Vector2.Scale(
                motionVectorScale,
                settings.MotionVectorScale);

            // Seed a valid display result before the asynchronous native event.
            // A successful evaluation overwrites it; a failed one never exposes
            // an uncleared UAV to the rest of the frame.
            RecordFallback(cmd, colorInput, colorOutput);
            if (Extension.EvaluateNeuralRenderingFeature(
                    cmd,
                    m_handle,
                    colorInput,
                    colorOutput,
                    depth,
                    motionVectors,
                    motionVectorScale.x,
                    motionVectorScale.y,
                    settings,
                    reset))
            {
                return true;
            }

            return false;
        }

        private bool EnsureInitialized(CommandBuffer cmd)
        {
            if (m_initialized)
                return true;
            if (m_createFailed)
                return false;

            DLSSExtension extension = Extension;
            if (extension == null)
                return false;

            if (m_handle != DLSSExtension.DLSS_INVALID_FEATURE_HANDLE)
            {
                DLSSFeatureStatus status =
                    extension.GetFeatureStatus(m_handle, out NVSDK_NGX_Result createResult);
                switch (status)
                {
                    case DLSSFeatureStatus.Pending:
                        return false;
                    case DLSSFeatureStatus.Ready:
                        m_initialized = true;
#if DEBUG || UNITY_EDITOR
                        Debug.Log(
                            $"[DLSSNeuralRendering] Initialized: " +
                            $"{m_inputWidth}x{m_inputHeight} -> " +
                            $"{m_outputWidth}x{m_outputHeight}, Preset={m_preset}");
#endif
                        return true;
                    case DLSSFeatureStatus.Failed:
                        Debug.LogError(
                            $"[DLSSNeuralRendering] Native feature creation failed: {createResult}");
                        extension.ReleaseFeatureHandle(m_handle);
                        m_handle = DLSSExtension.DLSS_INVALID_FEATURE_HANDLE;
                        m_createFailed = true;
                        return false;
                    default:
                        Debug.LogError(
                            "[DLSSNeuralRendering] Native feature handle became invalid");
                        m_handle = DLSSExtension.DLSS_INVALID_FEATURE_HANDLE;
                        m_createFailed = true;
                        return false;
                }
            }

            m_handle = extension.CreateNeuralRenderingFeature(
                cmd,
                m_inputWidth,
                m_inputHeight,
                m_outputWidth,
                m_outputHeight,
                m_preset,
                m_upscaling);
            if (m_handle == DLSSExtension.DLSS_INVALID_FEATURE_HANDLE)
            {
                m_createFailed = true;
                return false;
            }

            // Native creation runs later on the render thread.
            return false;
        }

        private bool DisposeResources(CommandBuffer cmd)
        {
            DLSSExtension extension = Extension;
            if (m_handle == DLSSExtension.DLSS_INVALID_FEATURE_HANDLE)
            {
                m_initialized = false;
                return true;
            }

            if (extension == null || cmd == null)
                return false;

            DLSSFeatureStatus status = extension.GetFeatureStatus(m_handle, out _);
            if (status == DLSSFeatureStatus.Pending || status == DLSSFeatureStatus.Ready)
            {
                if (!extension.DestroyFeature(cmd, m_handle))
                    return false;
            }
            else if (status == DLSSFeatureStatus.Failed)
            {
                extension.ReleaseFeatureHandle(m_handle);
            }

            m_handle = DLSSExtension.DLSS_INVALID_FEATURE_HANDLE;
            m_initialized = false;
            return true;
        }

        private static bool ValidateInputs(
            RenderTexture colorInput,
            RenderTexture colorOutput,
            RenderTexture depth,
            RenderTexture motionVectors,
            DLSSNeuralRenderingSettings settings,
            out string error)
        {
            if (colorInput == null || colorOutput == null ||
                depth == null || motionVectors == null)
            {
                error = "Color, output, depth, and motion-vector textures are required.";
                return false;
            }

            if (ReferenceEquals(colorInput, colorOutput) ||
                colorInput.GetNativeTexturePtr() == colorOutput.GetNativeTexturePtr())
            {
                error = "Color input and output must be distinct textures.";
                return false;
            }

            if (!colorInput.IsCreated() || !colorOutput.IsCreated() ||
                !depth.IsCreated() || !motionVectors.IsCreated())
            {
                error = "All textures must be created before evaluation.";
                return false;
            }

            if (!colorOutput.enableRandomWrite)
            {
                error = "Color output must be created with enableRandomWrite=true.";
                return false;
            }

            if (depth.width != colorInput.width || depth.height != colorInput.height ||
                motionVectors.width != colorInput.width || motionVectors.height != colorInput.height)
            {
                error = "Depth and motion-vector dimensions must match the color input.";
                return false;
            }

            if (settings == null)
            {
                error = "Settings cannot be null.";
                return false;
            }

            if (!settings.TryValidate(out error))
                return false;

            long expectedWidth = settings.Upscaling
                ? (long)colorInput.width * 2L
                : colorInput.width;
            long expectedHeight = settings.Upscaling
                ? (long)colorInput.height * 2L
                : colorInput.height;
            if (colorOutput.width != expectedWidth || colorOutput.height != expectedHeight)
            {
                error = settings.Upscaling
                    ? "The upscaling path requires output dimensions exactly 2x the input."
                    : "The full-resolution path requires matching input/output dimensions.";
                return false;
            }

            error = null;
            return true;
        }

        private static void RecordFallback(
            CommandBuffer cmd,
            RenderTexture colorInput,
            RenderTexture colorOutput)
        {
            if (cmd != null && colorInput != null && colorOutput != null)
                cmd.Blit(colorInput, colorOutput);
        }

        public void Dispose()
        {
            if (m_disposed)
                return;

            using (var cmd = new CommandBuffer())
            {
                cmd.name = "DLSS Neural Rendering Cleanup";
                DisposeResources(cmd);
                Graphics.ExecuteCommandBuffer(cmd);
            }

            m_disposed = true;
            GC.SuppressFinalize(this);
        }

        ~DLSSNeuralRendering()
        {
            // Native feature records are also released by DLSSExtension.ShutDown.
            m_disposed = true;
        }
#else
        public bool IsSupported => false;

        public bool Render(
            CommandBuffer cmd,
            RenderTexture colorInput,
            RenderTexture colorOutput,
            RenderTexture depth,
            RenderTexture motionVectors,
            DLSSMotionVectorEncoding motionVectorEncoding,
            DLSSNeuralRenderingSettings settings,
            bool reset = false)
        {
            return false;
        }

        public void Dispose() { }
#endif
    }
}
