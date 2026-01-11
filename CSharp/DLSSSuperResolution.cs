//------------------------------------------------------------------------------
// DLSSSuperResolution.cs - DLSS Super Resolution Sample Implementation
//------------------------------------------------------------------------------
// Example of how to integrate DLSS-SR into a Unity render pipeline.
// Based on UnityDenoiserPlugin/DLSSSuperSampling.cs pattern.
//------------------------------------------------------------------------------

using System;
using UnityEngine;
using UnityEngine.Rendering;
using static DLSS.DLSSSdk;

namespace DLSS
{
    /// <summary>
    /// DLSS Super Resolution render pass implementation.
    /// Manages DLSS-SR feature lifecycle and execution.
    /// </summary>
    public class DLSSSuperResolution : IDisposable
    {
        private int m_dlssHandle = DLSS_INVALID_FEATURE_HANDLE;
        private IntPtr m_dlssParameters = IntPtr.Zero;
        private bool m_initialized = false;
        private bool m_disposed = false;

        // Create params tracking for recreation
        private uint m_inputWidth;
        private uint m_inputHeight;
        private uint m_outputWidth;
        private uint m_outputHeight;
        private NVSDK_NGX_PerfQuality_Value m_qualityValue;
        private NVSDK_NGX_DLSS_Feature_Flags m_featureFlags;
        private bool m_createParamsChanged = false;

        /// <summary>
        /// Create a new DLSS-SR instance.
        /// </summary>
        /// <param name="featureFlags">Feature creation flags (HDR, MV format, depth format, etc.)</param>
        /// <param name="qualityValue">Quality/performance preset</param>
        public DLSSSuperResolution(
            NVSDK_NGX_DLSS_Feature_Flags featureFlags = NVSDK_NGX_DLSS_Feature_Flags.None,
            NVSDK_NGX_PerfQuality_Value qualityValue = NVSDK_NGX_PerfQuality_Value.NVSDK_NGX_PerfQuality_Value_Balanced)
        {
            DLSS_Init();

            m_featureFlags = featureFlags;
            m_qualityValue = qualityValue;
        }

        /// <summary>
        /// Check if DLSS-SR is supported on the current system.
        /// </summary>
        public bool IsSupported => DLSS_IsSuperSamplingAvailable();

        /// <summary>
        /// Set the quality/performance preset.
        /// </summary>
        public void SetQuality(NVSDK_NGX_PerfQuality_Value quality)
        {
            if (m_qualityValue != quality)
            {
                m_qualityValue = quality;
                m_createParamsChanged = true;
            }
        }

        /// <summary>
        /// Set the feature creation flags.
        /// </summary>
        public void SetFeatureFlags(NVSDK_NGX_DLSS_Feature_Flags flags)
        {
            if (m_featureFlags != flags)
            {
                m_featureFlags = flags;
                m_createParamsChanged = true;
            }
        }

        /// <summary>
        /// Execute DLSS-SR.
        /// </summary>
        /// <param name="cmd">Command buffer to record commands into</param>
        /// <param name="colorInput">Input color texture (render resolution)</param>
        /// <param name="colorOutput">Output color texture (display resolution)</param>
        /// <param name="depth">Depth buffer</param>
        /// <param name="motionVectors">Motion vectors</param>
        /// <param name="jitterX">Jitter offset X in render pixels</param>
        /// <param name="jitterY">Jitter offset Y in render pixels</param>
        /// <param name="mvScaleX">Motion vector scale X (typically render width)</param>
        /// <param name="mvScaleY">Motion vector scale Y (typically render height)</param>
        /// <param name="reset">Reset temporal history (e.g., on scene change)</param>
        /// <param name="sharpness">Sharpening amount (deprecated, typically 0)</param>
        /// <param name="exposureTexture">Optional 1x1 exposure texture</param>
        /// <returns>True if execution was successful</returns>
        public bool Render(
            CommandBuffer cmd,
            RenderTexture colorInput,
            RenderTexture colorOutput,
            RenderTexture depth,
            RenderTexture motionVectors,
            float jitterX,
            float jitterY,
            float mvScaleX,
            float mvScaleY,
            bool reset = false,
            float sharpness = 0.0f,
            RenderTexture exposureTexture = null)
        {
            if (!IsSupported)
            {
                Debug.LogError("[DLSSSuperResolution] DLSS-SR is not supported");
                return false;
            }

            if (colorInput == null || colorOutput == null || depth == null || motionVectors == null)
            {
                Debug.LogError("[DLSSSuperResolution] Required textures are null");
                return false;
            }

            // Check if we need to recreate the feature
            uint inputW = (uint)colorInput.width;
            uint inputH = (uint)colorInput.height;
            uint outputW = (uint)colorOutput.width;
            uint outputH = (uint)colorOutput.height;

            if (m_inputWidth != inputW || m_inputHeight != inputH ||
                m_outputWidth != outputW || m_outputHeight != outputH)
            {
                m_inputWidth = inputW;
                m_inputHeight = inputH;
                m_outputWidth = outputW;
                m_outputHeight = outputH;
                m_createParamsChanged = true;
            }

            // Recreate feature if params changed
            if (m_createParamsChanged)
            {
                DisposeResources(cmd);
                m_createParamsChanged = false;
            }

            // Initialize if needed
            if (!m_initialized)
            {
                if (!Initialize(cmd))
                {
                    return false;
                }
            }

            // Set evaluation parameters
            SetupEvalParams(
                colorInput, colorOutput, depth, motionVectors,
                jitterX, jitterY, mvScaleX, mvScaleY,
                reset, sharpness, exposureTexture);

            // Execute
            DLSS_EvaluateFeature(cmd, m_dlssHandle, m_dlssParameters);
            return true;
        }

        private bool Initialize(CommandBuffer cmd)
        {
            if (m_initialized)
                return true;

            // Allocate parameters
            var result = DLSS_AllocateParameters_D3D12(out m_dlssParameters);
            if (NVSDK_NGX_FAILED(result))
            {
                Debug.LogError($"[DLSSSuperResolution] Failed to allocate parameters: {result}");
                return false;
            }

            // Set creation parameters
            DLSS_Parameter_SetUI(m_dlssParameters, NVSDK_NGX_Parameter_CreationNodeMask, 1);
            DLSS_Parameter_SetUI(m_dlssParameters, NVSDK_NGX_Parameter_VisibilityNodeMask, 1);
            DLSS_Parameter_SetUI(m_dlssParameters, NVSDK_NGX_Parameter_Width, m_inputWidth);
            DLSS_Parameter_SetUI(m_dlssParameters, NVSDK_NGX_Parameter_Height, m_inputHeight);
            DLSS_Parameter_SetUI(m_dlssParameters, NVSDK_NGX_Parameter_OutWidth, m_outputWidth);
            DLSS_Parameter_SetUI(m_dlssParameters, NVSDK_NGX_Parameter_OutHeight, m_outputHeight);
            DLSS_Parameter_SetI(m_dlssParameters, NVSDK_NGX_Parameter_PerfQualityValue, (int)m_qualityValue);
            DLSS_Parameter_SetI(m_dlssParameters, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, (int)m_featureFlags);
            DLSS_Parameter_SetI(m_dlssParameters, NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0);

            // Create feature
            m_dlssHandle = DLSS_CreateFeature(cmd, NVSDK_NGX_Feature.NVSDK_NGX_Feature_SuperSampling, m_dlssParameters);
            if (m_dlssHandle == DLSS_INVALID_FEATURE_HANDLE)
            {
                Debug.LogError("[DLSSSuperResolution] Failed to create DLSS-SR feature");
                DLSS_DestroyParameters_D3D12(m_dlssParameters);
                m_dlssParameters = IntPtr.Zero;
                return false;
            }

            m_initialized = true;
            Debug.Log($"[DLSSSuperResolution] Initialized: {m_inputWidth}x{m_inputHeight} -> {m_outputWidth}x{m_outputHeight}, Quality={m_qualityValue}");
            return true;
        }

        private void SetupEvalParams(
            RenderTexture colorInput,
            RenderTexture colorOutput,
            RenderTexture depth,
            RenderTexture motionVectors,
            float jitterX,
            float jitterY,
            float mvScaleX,
            float mvScaleY,
            bool reset,
            float sharpness,
            RenderTexture exposureTexture)
        {
            // Input textures
            DLSS_Parameter_SetD3d12RenderTexture(m_dlssParameters, NVSDK_NGX_Parameter_Color, colorInput);
            DLSS_Parameter_SetD3d12RenderTexture(m_dlssParameters, NVSDK_NGX_Parameter_Output, colorOutput);
            DLSS_Parameter_SetD3d12RenderTexture(m_dlssParameters, NVSDK_NGX_Parameter_Depth, depth);
            DLSS_Parameter_SetD3d12RenderTexture(m_dlssParameters, NVSDK_NGX_Parameter_MotionVectors, motionVectors);

            // Optional exposure texture
            if (exposureTexture != null)
            {
                DLSS_Parameter_SetD3d12RenderTexture(m_dlssParameters, NVSDK_NGX_Parameter_ExposureTexture, exposureTexture);
            }

            // Jitter
            DLSS_Parameter_SetF(m_dlssParameters, NVSDK_NGX_Parameter_Jitter_Offset_X, jitterX);
            DLSS_Parameter_SetF(m_dlssParameters, NVSDK_NGX_Parameter_Jitter_Offset_Y, jitterY);

            // Motion vector scale (default to 1.0 if 0)
            DLSS_Parameter_SetF(m_dlssParameters, NVSDK_NGX_Parameter_MV_Scale_X, mvScaleX == 0 ? 1.0f : mvScaleX);
            DLSS_Parameter_SetF(m_dlssParameters, NVSDK_NGX_Parameter_MV_Scale_Y, mvScaleY == 0 ? 1.0f : mvScaleY);

            // Reset flag
            DLSS_Parameter_SetI(m_dlssParameters, NVSDK_NGX_Parameter_Reset, reset ? 1 : 0);

            // Sharpness (deprecated but kept for compatibility)
            DLSS_Parameter_SetF(m_dlssParameters, NVSDK_NGX_Parameter_Sharpness, sharpness);

            // Render subrect dimensions
            DLSS_Parameter_SetUI(m_dlssParameters, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, m_inputWidth);
            DLSS_Parameter_SetUI(m_dlssParameters, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, m_inputHeight);

            // Exposure defaults
            DLSS_Parameter_SetF(m_dlssParameters, NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
            DLSS_Parameter_SetF(m_dlssParameters, NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);
        }

        private void DisposeResources(CommandBuffer cmd)
        {
            if (m_initialized)
            {
                if (m_dlssHandle != DLSS_INVALID_FEATURE_HANDLE)
                {
                    DLSS_DestroyFeature(cmd, m_dlssHandle);
                    m_dlssHandle = DLSS_INVALID_FEATURE_HANDLE;
                }

                if (m_dlssParameters != IntPtr.Zero)
                {
                    DLSS_DestroyParameters_D3D12(m_dlssParameters);
                    m_dlssParameters = IntPtr.Zero;
                }

                m_initialized = false;
            }
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (m_disposed)
                return;

            if (disposing)
            {
                // Create a temporary command buffer for cleanup
                using (var cmd = new CommandBuffer())
                {
                    cmd.name = "DLSS-SR Cleanup";
                    DisposeResources(cmd);
                    Graphics.ExecuteCommandBuffer(cmd);
                }
            }

            DLSS_Shutdown();
            m_disposed = true;
        }

        ~DLSSSuperResolution()
        {
            Dispose(false);
        }
    }
}
