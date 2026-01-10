//------------------------------------------------------------------------------
// DLSSContext.cpp - Internal DLSS Context Management Implementation
//------------------------------------------------------------------------------

#include "DLSSContext.h"
#include "DLSSPlugin.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_dlssd.h>
#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_dlssd.h>

#include <cstdio>
#include <cstring>

namespace dlss
{

//------------------------------------------------------------------------------
// DLSSLogger Implementation
//------------------------------------------------------------------------------

DLSSLogger& DLSSLogger::Instance()
{
    static DLSSLogger instance;
    return instance;
}

void DLSSLogger::SetCallback(DLSSLogCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = callback;
}

void DLSSLogger::SetLogLevel(DLSSLogLevel level)
{
    m_logLevel.store(level);
}

DLSSLogLevel DLSSLogger::GetLogLevel() const
{
    return m_logLevel.load();
}

void DLSSLogger::Debug(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(DLSS_Log_Debug, format, args);
    va_end(args);
}

void DLSSLogger::Info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(DLSS_Log_Info, format, args);
    va_end(args);
}

void DLSSLogger::Warning(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(DLSS_Log_Warning, format, args);
    va_end(args);
}

void DLSSLogger::Error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(DLSS_Log_Error, format, args);
    va_end(args);
}

void DLSSLogger::Log(DLSSLogLevel level, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(level, format, args);
    va_end(args);
}

void DLSSLogger::LogV(DLSSLogLevel level, const char* format, va_list args)
{
    // Check log level
    if (level < m_logLevel.load())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Format message with [DLSS] prefix
    char prefixedBuffer[2048];
    snprintf(prefixedBuffer, sizeof(prefixedBuffer) - 1, "[DLSS] %s", format);
    prefixedBuffer[sizeof(prefixedBuffer) - 1] = '\0';

    // Format final message
    vsnprintf(m_buffer, sizeof(m_buffer) - 1, prefixedBuffer, args);
    m_buffer[sizeof(m_buffer) - 1] = '\0';

    // If callback is set, use it (allows C# to override Unity log)
    if (m_callback)
    {
        m_callback(level, m_buffer);
        return;
    }

    // Otherwise, use Unity's log interface
    LogToUnity(level, m_buffer);
}

void DLSSLogger::LogToUnity(DLSSLogLevel level, const char* message)
{
    if (!g_unityLog)
        return;

    UnityLogType unityLogType;
    switch (level)
    {
    case DLSS_Log_Debug:
    case DLSS_Log_Info:
        unityLogType = kUnityLogTypeLog;
        break;
    case DLSS_Log_Warning:
        unityLogType = kUnityLogTypeWarning;
        break;
    case DLSS_Log_Error:
        unityLogType = kUnityLogTypeError;
        break;
    default:
        unityLogType = kUnityLogTypeLog;
        break;
    }

    g_unityLog->Log(unityLogType, message, "DLSSPlugin", 0);
}

//------------------------------------------------------------------------------
// Helper function implementations
//------------------------------------------------------------------------------

int ToNGXPerfQuality(DLSSQuality quality)
{
    switch (quality)
    {
    case DLSS_Quality_MaxPerformance:
        return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case DLSS_Quality_Balanced:
        return NVSDK_NGX_PerfQuality_Value_Balanced;
    case DLSS_Quality_MaxQuality:
        return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case DLSS_Quality_UltraPerformance:
        return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    case DLSS_Quality_UltraQuality:
        return NVSDK_NGX_PerfQuality_Value_UltraQuality;
    case DLSS_Quality_DLAA:
        return NVSDK_NGX_PerfQuality_Value_DLAA;
    default:
        return NVSDK_NGX_PerfQuality_Value_Balanced;
    }
}

int ToNGXSRPreset(DLSSSRPreset preset)
{
    switch (preset)
    {
    case DLSS_SR_Preset_Default:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
    case DLSS_SR_Preset_F:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_F;
    case DLSS_SR_Preset_G:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_G;
    case DLSS_SR_Preset_J:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_J;
    case DLSS_SR_Preset_K:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_K;
    case DLSS_SR_Preset_L:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_L;
    case DLSS_SR_Preset_M:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_M;
    default:
        return NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
    }
}

int ToNGXRRPreset(DLSSRRPreset preset)
{
    switch (preset)
    {
    case DLSS_RR_Preset_Default:
        return NVSDK_NGX_RayReconstruction_Hint_Render_Preset_Default;
    case DLSS_RR_Preset_D:
        return NVSDK_NGX_RayReconstruction_Hint_Render_Preset_D;
    case DLSS_RR_Preset_E:
        return NVSDK_NGX_RayReconstruction_Hint_Render_Preset_E;
    default:
        return NVSDK_NGX_RayReconstruction_Hint_Render_Preset_Default;
    }
}

int ToNGXFeatureFlags(uint32_t flags)
{
    int ngxFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
    if (flags & DLSS_Flag_IsHDR)
        ngxFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if (flags & DLSS_Flag_MVLowRes)
        ngxFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if (flags & DLSS_Flag_MVJittered)
        ngxFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
    if (flags & DLSS_Flag_DepthInverted)
        ngxFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if (flags & DLSS_Flag_AutoExposure)
        ngxFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    if (flags & DLSS_Flag_AlphaUpscaling)
        ngxFlags |= NVSDK_NGX_DLSS_Feature_Flags_AlphaUpscaling;
    return ngxFlags;
}

const char* GetResultString(DLSSResult result)
{
    switch (result)
    {
    case DLSS_Result_Success:
        return "Success";
    case DLSS_Result_Fail_NotInitialized:
        return "Not initialized";
    case DLSS_Result_Fail_FeatureNotSupported:
        return "Feature not supported";
    case DLSS_Result_Fail_InvalidParameter:
        return "Invalid parameter";
    case DLSS_Result_Fail_OutOfMemory:
        return "Out of memory";
    case DLSS_Result_Fail_ContextNotFound:
        return "Context not found";
    case DLSS_Result_Fail_ContextAlreadyExists:
        return "Context already exists";
    case DLSS_Result_Fail_DriverOutOfDate:
        return "Driver out of date";
    case DLSS_Result_Fail_PlatformError:
        return "Platform error";
    case DLSS_Result_Fail_NGXError:
        return "NGX error";
    default:
        return "Unknown error";
    }
}

//------------------------------------------------------------------------------
// DLSSContext implementation
//------------------------------------------------------------------------------

DLSSContext::~DLSSContext()
{
    Destroy();
}

DLSSContext::DLSSContext(DLSSContext&& other) noexcept
    : m_handle(other.m_handle)
    , m_params(other.m_params)
{
    other.m_handle = nullptr;
}

DLSSContext& DLSSContext::operator=(DLSSContext&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        m_handle = other.m_handle;
        m_params = other.m_params;
        other.m_handle = nullptr;
    }
    return *this;
}

DLSSResult DLSSContext::Create(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const DLSSContextCreateParams& params)
{
    (void)device; // Device obtained from DLSSContextManager

    if (m_handle)
    {
        DLSS_LOG_DEBUG("DLSSContext::Create - destroying existing handle before recreating");
        Destroy();
    }

    auto& mgr = DLSSContextManager::Instance();
    NVSDK_NGX_Parameter* ngxParams = mgr.GetNGXParams();
    if (!ngxParams)
    {
        DLSS_LOG_ERROR("DLSSContext::Create - NGX parameters not available");
        return DLSS_Result_Fail_NotInitialized;
    }

    NVSDK_NGX_Result ngxResult;

    if (params.mode == DLSS_Mode_RayReconstruction)
    {
        DLSS_LOG_DEBUG("Creating DLSS-RR feature handle...");

        // Create DLSS-RR (Ray Reconstruction) feature
        NVSDK_NGX_DLSSD_Create_Params createParams = {};
        createParams.InWidth = params.inputResolution.width;
        createParams.InHeight = params.inputResolution.height;
        createParams.InTargetWidth = params.outputResolution.width;
        createParams.InTargetHeight = params.outputResolution.height;
        createParams.InPerfQualityValue = static_cast<NVSDK_NGX_PerfQuality_Value>(ToNGXPerfQuality(params.quality));
        createParams.InFeatureCreateFlags = ToNGXFeatureFlags(params.featureFlags);
        createParams.InEnableOutputSubrects = params.enableOutputSubrects != 0;
        createParams.InDenoiseMode = params.denoiseMode == DLSS_Denoise_DLUnified
            ? NVSDK_NGX_DLSS_Denoise_Mode_DLUnified
            : NVSDK_NGX_DLSS_Denoise_Mode_Off;
        createParams.InRoughnessMode = params.roughnessMode == DLSS_Roughness_PackedInNormalsW
            ? NVSDK_NGX_DLSS_Roughness_Mode_Packed
            : NVSDK_NGX_DLSS_Roughness_Mode_Unpacked;
        createParams.InUseHWDepth = params.depthType == DLSS_Depth_Hardware
            ? NVSDK_NGX_DLSS_Depth_Type_HW
            : NVSDK_NGX_DLSS_Depth_Type_Linear;

        // Set RR presets
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA,
            ToNGXRRPreset(params.presetRR_DLAA));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
            ToNGXRRPreset(params.presetRR_Quality));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,
            ToNGXRRPreset(params.presetRR_Balanced));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
            ToNGXRRPreset(params.presetRR_Performance));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance,
            ToNGXRRPreset(params.presetRR_UltraPerformance));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality,
            ToNGXRRPreset(params.presetRR_UltraQuality));

        ngxResult = NGX_D3D12_CREATE_DLSSD_EXT(
            cmdList,
            1, // CreationNodeMask
            1, // VisibilityNodeMask
            &m_handle,
            ngxParams,
            &createParams);
    }
    else
    {
        // Create DLSS-SR (Super Resolution) feature
        NVSDK_NGX_DLSS_Create_Params createParams = {};
        createParams.Feature.InWidth = params.inputResolution.width;
        createParams.Feature.InHeight = params.inputResolution.height;
        createParams.Feature.InTargetWidth = params.outputResolution.width;
        createParams.Feature.InTargetHeight = params.outputResolution.height;
        createParams.Feature.InPerfQualityValue = static_cast<NVSDK_NGX_PerfQuality_Value>(ToNGXPerfQuality(params.quality));
        createParams.InFeatureCreateFlags = ToNGXFeatureFlags(params.featureFlags);
        createParams.InEnableOutputSubrects = params.enableOutputSubrects != 0;

        // Set SR presets
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
            ToNGXSRPreset(params.presetDLAA));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
            ToNGXSRPreset(params.presetQuality));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
            ToNGXSRPreset(params.presetBalanced));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
            ToNGXSRPreset(params.presetPerformance));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
            ToNGXSRPreset(params.presetUltraPerformance));
        NVSDK_NGX_Parameter_SetI(ngxParams, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality,
            ToNGXSRPreset(params.presetUltraQuality));

        ngxResult = NGX_D3D12_CREATE_DLSS_EXT(
            cmdList,
            1, // CreationNodeMask
            1, // VisibilityNodeMask
            &m_handle,
            ngxParams,
            &createParams);
    }

    mgr.SetLastNGXError(ngxResult);

    if (NVSDK_NGX_FAILED(ngxResult))
    {
        m_handle = nullptr;
        return mgr.TranslateNGXResult(ngxResult);
    }

    m_params = params;
    return DLSS_Result_Success;
}

void DLSSContext::Destroy()
{
    if (m_handle)
    {
        NVSDK_NGX_D3D12_ReleaseFeature(m_handle);
        m_handle = nullptr;
    }
    m_params = {};
}

DLSSResult DLSSContext::Execute(
    ID3D12GraphicsCommandList* cmdList,
    const DLSSExecuteParams& params)
{
    if (!m_handle)
    {
        return DLSS_Result_Fail_ContextNotFound;
    }

    auto& mgr = DLSSContextManager::Instance();
    NVSDK_NGX_Parameter* ngxParams = mgr.GetNGXParams();
    if (!ngxParams)
    {
        return DLSS_Result_Fail_NotInitialized;
    }

    int ngxResult;

    if (params.mode == DLSS_Mode_RayReconstruction)
    {
        ngxResult = SetupAndExecuteRR(cmdList, ngxParams, params);
    }
    else
    {
        ngxResult = SetupAndExecuteSR(cmdList, ngxParams, params);
    }

    mgr.SetLastNGXError(ngxResult);

    if (NVSDK_NGX_FAILED(ngxResult))
    {
        return mgr.TranslateNGXResult(ngxResult);
    }

    return DLSS_Result_Success;
}

bool DLSSContext::NeedsRecreation(const DLSSContextCreateParams& newParams) const
{
    // Check if key parameters changed that require recreation
    if (m_params.mode != newParams.mode)
        return true;
    if (m_params.outputResolution.width != newParams.outputResolution.width ||
        m_params.outputResolution.height != newParams.outputResolution.height)
        return true;
    if (m_params.inputResolution.width < newParams.inputResolution.width ||
        m_params.inputResolution.height < newParams.inputResolution.height)
        return true;
    if (m_params.quality != newParams.quality)
        return true;
    if (m_params.featureFlags != newParams.featureFlags)
        return true;

    // For RR mode, check RR-specific params
    if (newParams.mode == DLSS_Mode_RayReconstruction)
    {
        if (m_params.denoiseMode != newParams.denoiseMode)
            return true;
        if (m_params.depthType != newParams.depthType)
            return true;
        if (m_params.roughnessMode != newParams.roughnessMode)
            return true;
    }

    return false;
}

int DLSSContext::SetupAndExecuteSR(
    ID3D12GraphicsCommandList* cmdList,
    NVSDK_NGX_Parameter* ngxParams,
    const DLSSExecuteParams& params)
{
    NVSDK_NGX_D3D12_DLSS_Eval_Params evalParams = {};

    // Common textures
    evalParams.Feature.pInColor = static_cast<ID3D12Resource*>(params.textures.colorInput);
    evalParams.Feature.pInOutput = static_cast<ID3D12Resource*>(params.textures.colorOutput);
    evalParams.pInDepth = static_cast<ID3D12Resource*>(params.textures.depth);
    evalParams.pInMotionVectors = static_cast<ID3D12Resource*>(params.textures.motionVectors);
    evalParams.pInExposureTexture = static_cast<ID3D12Resource*>(params.textures.exposureTexture);
    evalParams.pInBiasCurrentColorMask = static_cast<ID3D12Resource*>(params.textures.biasColorMask);
    evalParams.pInTransparencyMask = static_cast<ID3D12Resource*>(params.textures.transparencyMask);

    // Common params
    evalParams.InJitterOffsetX = params.common.jitterOffsetX;
    evalParams.InJitterOffsetY = params.common.jitterOffsetY;
    evalParams.InMVScaleX = params.common.mvScaleX != 0.0f ? params.common.mvScaleX : 1.0f;
    evalParams.InMVScaleY = params.common.mvScaleY != 0.0f ? params.common.mvScaleY : 1.0f;
    evalParams.InRenderSubrectDimensions.Width = params.common.renderSubrectDimensions.width;
    evalParams.InRenderSubrectDimensions.Height = params.common.renderSubrectDimensions.height;
    evalParams.InReset = params.common.reset ? 1 : 0;
    evalParams.InPreExposure = params.common.preExposure != 0.0f ? params.common.preExposure : 1.0f;
    evalParams.InExposureScale = params.common.exposureScale != 0.0f ? params.common.exposureScale : 1.0f;
    evalParams.InIndicatorInvertXAxis = params.common.invertXAxis;
    evalParams.InIndicatorInvertYAxis = params.common.invertYAxis;

    // Subrect bases
    evalParams.InColorSubrectBase.X = params.common.colorSubrectBase.x;
    evalParams.InColorSubrectBase.Y = params.common.colorSubrectBase.y;
    evalParams.InDepthSubrectBase.X = params.common.depthSubrectBase.x;
    evalParams.InDepthSubrectBase.Y = params.common.depthSubrectBase.y;
    evalParams.InMVSubrectBase.X = params.common.mvSubrectBase.x;
    evalParams.InMVSubrectBase.Y = params.common.mvSubrectBase.y;
    evalParams.InOutputSubrectBase.X = params.common.outputSubrectBase.x;
    evalParams.InOutputSubrectBase.Y = params.common.outputSubrectBase.y;
    evalParams.InBiasCurrentColorSubrectBase.X = params.common.biasColorSubrectBase.x;
    evalParams.InBiasCurrentColorSubrectBase.Y = params.common.biasColorSubrectBase.y;

    return NGX_D3D12_EVALUATE_DLSS_EXT(cmdList, m_handle, ngxParams, &evalParams);
}

int DLSSContext::SetupAndExecuteRR(
    ID3D12GraphicsCommandList* cmdList,
    NVSDK_NGX_Parameter* ngxParams,
    const DLSSExecuteParams& params)
{
    NVSDK_NGX_D3D12_DLSSD_Eval_Params evalParams = {};

    // Common textures
    evalParams.pInColor = static_cast<ID3D12Resource*>(params.textures.colorInput);
    evalParams.pInOutput = static_cast<ID3D12Resource*>(params.textures.colorOutput);
    evalParams.pInDepth = static_cast<ID3D12Resource*>(params.textures.depth);
    evalParams.pInMotionVectors = static_cast<ID3D12Resource*>(params.textures.motionVectors);
    evalParams.pInExposureTexture = static_cast<ID3D12Resource*>(params.textures.exposureTexture);
    evalParams.pInBiasCurrentColorMask = static_cast<ID3D12Resource*>(params.textures.biasColorMask);
    evalParams.pInTransparencyMask = static_cast<ID3D12Resource*>(params.textures.transparencyMask);

    // Common params
    evalParams.InJitterOffsetX = params.common.jitterOffsetX;
    evalParams.InJitterOffsetY = params.common.jitterOffsetY;
    evalParams.InMVScaleX = params.common.mvScaleX != 0.0f ? params.common.mvScaleX : 1.0f;
    evalParams.InMVScaleY = params.common.mvScaleY != 0.0f ? params.common.mvScaleY : 1.0f;
    evalParams.InRenderSubrectDimensions.Width = params.common.renderSubrectDimensions.width;
    evalParams.InRenderSubrectDimensions.Height = params.common.renderSubrectDimensions.height;
    evalParams.InReset = params.common.reset ? 1 : 0;
    evalParams.InPreExposure = params.common.preExposure != 0.0f ? params.common.preExposure : 1.0f;
    evalParams.InExposureScale = params.common.exposureScale != 0.0f ? params.common.exposureScale : 1.0f;
    evalParams.InIndicatorInvertXAxis = params.common.invertXAxis;
    evalParams.InIndicatorInvertYAxis = params.common.invertYAxis;

    // Subrect bases
    evalParams.InColorSubrectBase.X = params.common.colorSubrectBase.x;
    evalParams.InColorSubrectBase.Y = params.common.colorSubrectBase.y;
    evalParams.InDepthSubrectBase.X = params.common.depthSubrectBase.x;
    evalParams.InDepthSubrectBase.Y = params.common.depthSubrectBase.y;
    evalParams.InMVSubrectBase.X = params.common.mvSubrectBase.x;
    evalParams.InMVSubrectBase.Y = params.common.mvSubrectBase.y;
    evalParams.InOutputSubrectBase.X = params.common.outputSubrectBase.x;
    evalParams.InOutputSubrectBase.Y = params.common.outputSubrectBase.y;
    evalParams.InBiasCurrentColorSubrectBase.X = params.common.biasColorSubrectBase.x;
    evalParams.InBiasCurrentColorSubrectBase.Y = params.common.biasColorSubrectBase.y;

    // RR-specific: GBuffer textures
    evalParams.pInDiffuseAlbedo = static_cast<ID3D12Resource*>(params.rrParams.gbuffer.diffuseAlbedo);
    evalParams.pInSpecularAlbedo = static_cast<ID3D12Resource*>(params.rrParams.gbuffer.specularAlbedo);
    evalParams.pInNormals = static_cast<ID3D12Resource*>(params.rrParams.gbuffer.normals);
    evalParams.pInRoughness = static_cast<ID3D12Resource*>(params.rrParams.gbuffer.roughness);
    evalParams.GBufferSurface.pInAttrib[NVSDK_NGX_GBUFFER_EMISSIVE] =
        static_cast<ID3D12Resource*>(params.rrParams.gbuffer.emissive);

    // RR-specific: Ray textures
    evalParams.pInDiffuseRayDirection = static_cast<ID3D12Resource*>(params.rrParams.rays.diffuseRayDirection);
    evalParams.pInDiffuseHitDistance = static_cast<ID3D12Resource*>(params.rrParams.rays.diffuseHitDistance);
    evalParams.pInSpecularRayDirection = static_cast<ID3D12Resource*>(params.rrParams.rays.specularRayDirection);
    evalParams.pInSpecularHitDistance = static_cast<ID3D12Resource*>(params.rrParams.rays.specularHitDistance);
    evalParams.pInDiffuseRayDirectionHitDistance =
        static_cast<ID3D12Resource*>(params.rrParams.rays.diffuseRayDirectionHitDistance);
    evalParams.pInSpecularRayDirectionHitDistance =
        static_cast<ID3D12Resource*>(params.rrParams.rays.specularRayDirectionHitDistance);

    // RR-specific: Optional textures
    evalParams.pInReflectedAlbedo = static_cast<ID3D12Resource*>(params.rrParams.optional.reflectedAlbedo);
    evalParams.pInColorBeforeParticles = static_cast<ID3D12Resource*>(params.rrParams.optional.colorBeforeParticles);
    evalParams.pInColorAfterParticles = static_cast<ID3D12Resource*>(params.rrParams.optional.colorAfterParticles);
    evalParams.pInColorBeforeTransparency = static_cast<ID3D12Resource*>(params.rrParams.optional.colorBeforeTransparency);
    evalParams.pInColorAfterTransparency = static_cast<ID3D12Resource*>(params.rrParams.optional.colorAfterTransparency);
    evalParams.pInColorBeforeFog = static_cast<ID3D12Resource*>(params.rrParams.optional.colorBeforeFog);
    evalParams.pInColorAfterFog = static_cast<ID3D12Resource*>(params.rrParams.optional.colorAfterFog);
    evalParams.pInDepthOfFieldGuide = static_cast<ID3D12Resource*>(params.rrParams.optional.depthOfFieldGuide);
    evalParams.pInColorBeforeDepthOfField = static_cast<ID3D12Resource*>(params.rrParams.optional.colorBeforeDepthOfField);
    evalParams.pInColorAfterDepthOfField = static_cast<ID3D12Resource*>(params.rrParams.optional.colorAfterDepthOfField);
    evalParams.pInScreenSpaceSubsurfaceScatteringGuide =
        static_cast<ID3D12Resource*>(params.rrParams.optional.screenSpaceSubsurfaceScatteringGuide);
    evalParams.pInColorBeforeScreenSpaceSubsurfaceScattering =
        static_cast<ID3D12Resource*>(params.rrParams.optional.colorBeforeScreenSpaceSubsurfaceScattering);
    evalParams.pInColorAfterScreenSpaceSubsurfaceScattering =
        static_cast<ID3D12Resource*>(params.rrParams.optional.colorAfterScreenSpaceSubsurfaceScattering);
    evalParams.pInScreenSpaceRefractionGuide =
        static_cast<ID3D12Resource*>(params.rrParams.optional.screenSpaceRefractionGuide);
    evalParams.pInColorBeforeScreenSpaceRefraction =
        static_cast<ID3D12Resource*>(params.rrParams.optional.colorBeforeScreenSpaceRefraction);
    evalParams.pInColorAfterScreenSpaceRefraction =
        static_cast<ID3D12Resource*>(params.rrParams.optional.colorAfterScreenSpaceRefraction);
    evalParams.pInMotionVectorsReflections =
        static_cast<ID3D12Resource*>(params.rrParams.optional.motionVectorsReflections);
    evalParams.pInTransparencyLayer = static_cast<ID3D12Resource*>(params.rrParams.optional.transparencyLayer);
    evalParams.pInTransparencyLayerOpacity =
        static_cast<ID3D12Resource*>(params.rrParams.optional.transparencyLayerOpacity);
    evalParams.pInTransparencyLayerMvecs =
        static_cast<ID3D12Resource*>(params.rrParams.optional.transparencyLayerMvecs);
    evalParams.pInDisocclusionMask = static_cast<ID3D12Resource*>(params.rrParams.optional.disocclusionMask);
    evalParams.pInAlpha = static_cast<ID3D12Resource*>(params.rrParams.optional.alpha);
    evalParams.pInOutputAlpha = static_cast<ID3D12Resource*>(params.rrParams.optional.outputAlpha);

    // RR-specific: Matrices (cast to float* for NGX)
    evalParams.pInWorldToViewMatrix = const_cast<float*>(params.rrParams.worldToViewMatrix.m);
    evalParams.pInViewToClipMatrix = const_cast<float*>(params.rrParams.viewToClipMatrix.m);

    // RR-specific: Frame time
    evalParams.InFrameTimeDeltaInMsec = params.rrParams.frameTimeDeltaMs;

    return NGX_D3D12_EVALUATE_DLSSD_EXT(cmdList, m_handle, ngxParams, &evalParams);
}

//------------------------------------------------------------------------------
// DLSSContextManager implementation
//------------------------------------------------------------------------------

DLSSContextManager& DLSSContextManager::Instance()
{
    static DLSSContextManager instance;
    return instance;
}

DLSSContextManager::~DLSSContextManager()
{
    Shutdown();
}

DLSSResult DLSSContextManager::Initialize(
    ID3D12Device* device,
    uint64_t appId,
    const char* projectId,
    const char* engineVersion,
    const wchar_t* logPath)
{
    if (m_initialized.load())
    {
        DLSS_LOG_DEBUG("DLSS already initialized, skipping");
        return DLSS_Result_Success;
    }

    if (!device)
    {
        DLSS_LOG_ERROR("DLSS Initialize failed: device is null");
        return DLSS_Result_Fail_InvalidParameter;
    }

    DLSS_LOG_INFO("Initializing DLSS plugin (appId=%llu, projectId=%s, engineVersion=%s)",
        appId,
        projectId ? projectId : "(null)",
        engineVersion ? engineVersion : "(null)");

    m_device = device;

    DLSSResult result = InitializeNGX(appId, projectId, engineVersion, logPath);
    if (result != DLSS_Result_Success)
    {
        DLSS_LOG_ERROR("DLSS NGX initialization failed: %s (NGX error: 0x%08X)",
            GetResultString(result), m_lastNGXError.load());
        m_device.Reset();
        return result;
    }

    m_initialized.store(true);

    DLSS_LOG_INFO("DLSS initialized successfully - SR: %s, RR: %s",
        m_dlssSRAvailable ? "available" : "unavailable",
        m_dlssRRAvailable ? "available" : "unavailable");

    return DLSS_Result_Success;
}

DLSSResult DLSSContextManager::InitializeNGX(
    uint64_t appId,
    const char* projectId,
    const char* engineVersion,
    const wchar_t* logPath)
{
    DLSS_LOG_DEBUG("Initializing NGX SDK...");

    // Setup application identifier
    NVSDK_NGX_Application_Identifier appIdentifier = {};

    if (projectId && projectId[0] != '\0')
    {
        appIdentifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
        appIdentifier.v.ProjectDesc.ProjectId = projectId;
        appIdentifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_UNITY;
        appIdentifier.v.ProjectDesc.EngineVersion = engineVersion ? engineVersion : "1.0";
    }
    else
    {
        appIdentifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
        appIdentifier.v.ApplicationId = appId;
    }

    // Initialize NGX
    NVSDK_NGX_Result ngxResult = NVSDK_NGX_D3D12_Init_with_ProjectID(
        projectId ? projectId : "",
        NVSDK_NGX_ENGINE_TYPE_UNITY,
        engineVersion ? engineVersion : "1.0",
        logPath ? logPath : L".",
        m_device.Get(),
        nullptr,  // NVSDK_NGX_FeatureCommonInfo - not needed
        NVSDK_NGX_Version_API);

    m_lastNGXError = ngxResult;

    if (NVSDK_NGX_FAILED(ngxResult))
    {
        DLSS_LOG_ERROR("NGX D3D12 Init failed with error 0x%08X", ngxResult);
        return TranslateNGXResult(ngxResult);
    }

    DLSS_LOG_DEBUG("NGX SDK initialized, querying capabilities...");

    // Get capability parameters
    ngxResult = NVSDK_NGX_D3D12_GetCapabilityParameters(&m_ngxParams);
    if (NVSDK_NGX_FAILED(ngxResult))
    {
        DLSS_LOG_ERROR("Failed to get NGX capability parameters: 0x%08X", ngxResult);
        NVSDK_NGX_D3D12_Shutdown1(m_device.Get());
        m_lastNGXError = ngxResult;
        return TranslateNGXResult(ngxResult);
    }

    // Check feature availability
    int dlssSRAvailable = 0;
    int dlssRRAvailable = 0;
    NVSDK_NGX_Parameter_GetI(m_ngxParams, NVSDK_NGX_Parameter_SuperSampling_Available, &dlssSRAvailable);
    NVSDK_NGX_Parameter_GetI(m_ngxParams, NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &dlssRRAvailable);

    m_dlssSRAvailable = (dlssSRAvailable != 0);
    m_dlssRRAvailable = (dlssRRAvailable != 0);

    DLSS_LOG_DEBUG("NGX feature availability queried - SR: %d, RR: %d", dlssSRAvailable, dlssRRAvailable);

    return DLSS_Result_Success;
}

void DLSSContextManager::Shutdown()
{
    if (!m_initialized.exchange(false))
    {
        return;
    }

    DLSS_LOG_INFO("Shutting down DLSS plugin");

    size_t contextCount = m_contexts.size();
    DestroyAllContexts();

    if (contextCount > 0)
    {
        DLSS_LOG_INFO("Destroyed %zu DLSS context(s) during shutdown", contextCount);
    }

    if (m_ngxParams)
    {
        NVSDK_NGX_D3D12_DestroyParameters(m_ngxParams);
        m_ngxParams = nullptr;
    }

    if (m_device)
    {
        NVSDK_NGX_D3D12_Shutdown1(m_device.Get());
        m_device.Reset();
    }

    m_dlssSRAvailable = false;
    m_dlssRRAvailable = false;

    DLSS_LOG_INFO("DLSS plugin shutdown complete");
}

DLSSResult DLSSContextManager::GetCapabilities(DLSSCapabilityInfo* outInfo)
{
    if (!m_initialized.load())
    {
        return DLSS_Result_Fail_NotInitialized;
    }

    if (!outInfo)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }

    outInfo->dlssSRAvailable = m_dlssSRAvailable ? 1 : 0;
    outInfo->dlssRRAvailable = m_dlssRRAvailable ? 1 : 0;

    // Query driver version info
    int needsUpdate = 0;
    unsigned int minMajor = 0, minMinor = 0;
    NVSDK_NGX_Parameter_GetI(m_ngxParams, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsUpdate);
    NVSDK_NGX_Parameter_GetUI(m_ngxParams, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minMajor);
    NVSDK_NGX_Parameter_GetUI(m_ngxParams, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minMinor);

    outInfo->needsDriverUpdate = (needsUpdate != 0) ? 1 : 0;
    outInfo->minDriverVersionMajor = minMajor;
    outInfo->minDriverVersionMinor = minMinor;

    return DLSS_Result_Success;
}

DLSSResult DLSSContextManager::GetOptimalSettings(
    DLSSMode mode,
    DLSSQuality quality,
    uint32_t outputWidth,
    uint32_t outputHeight,
    DLSSOptimalSettings* outSettings)
{
    if (!m_initialized.load())
    {
        return DLSS_Result_Fail_NotInitialized;
    }

    if (!outSettings)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }

    unsigned int optWidth = 0, optHeight = 0;
    unsigned int maxWidth = 0, maxHeight = 0;
    unsigned int minWidth = 0, minHeight = 0;
    float sharpness = 0.0f;

    NVSDK_NGX_Result ngxResult;

    if (mode == DLSS_Mode_RayReconstruction)
    {
        ngxResult = NGX_DLSSD_GET_OPTIMAL_SETTINGS(
            m_ngxParams,
            outputWidth,
            outputHeight,
            static_cast<NVSDK_NGX_PerfQuality_Value>(ToNGXPerfQuality(quality)),
            &optWidth, &optHeight,
            &maxWidth, &maxHeight,
            &minWidth, &minHeight,
            &sharpness);
    }
    else
    {
        ngxResult = NGX_DLSS_GET_OPTIMAL_SETTINGS(
            m_ngxParams,
            outputWidth,
            outputHeight,
            static_cast<NVSDK_NGX_PerfQuality_Value>(ToNGXPerfQuality(quality)),
            &optWidth, &optHeight,
            &maxWidth, &maxHeight,
            &minWidth, &minHeight,
            &sharpness);
    }

    m_lastNGXError = ngxResult;

    if (NVSDK_NGX_FAILED(ngxResult))
    {
        return TranslateNGXResult(ngxResult);
    }

    outSettings->optimalRenderWidth = optWidth;
    outSettings->optimalRenderHeight = optHeight;
    outSettings->maxRenderWidth = maxWidth;
    outSettings->maxRenderHeight = maxHeight;
    outSettings->minRenderWidth = minWidth;
    outSettings->minRenderHeight = minHeight;
    outSettings->sharpness = sharpness;

    return DLSS_Result_Success;
}

DLSSResult DLSSContextManager::GetStats(DLSSMode mode, DLSSStats* outStats)
{
    if (!m_initialized.load())
    {
        return DLSS_Result_Fail_NotInitialized;
    }

    if (!outStats)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }

    unsigned long long vramBytes = 0;
    unsigned int optLevel = 0;
    unsigned int isDevBranch = 0;

    NVSDK_NGX_Result ngxResult;

    if (mode == DLSS_Mode_RayReconstruction)
    {
        ngxResult = NGX_DLSSD_GET_STATS_2(m_ngxParams, &vramBytes, &optLevel, &isDevBranch);
    }
    else
    {
        ngxResult = NGX_DLSS_GET_STATS_2(m_ngxParams, &vramBytes, &optLevel, &isDevBranch);
    }

    m_lastNGXError = ngxResult;

    if (NVSDK_NGX_FAILED(ngxResult))
    {
        return TranslateNGXResult(ngxResult);
    }

    outStats->vramAllocatedBytes = vramBytes;
    outStats->optLevel = optLevel;
    outStats->isDevBranch = (isDevBranch != 0) ? 1 : 0;

    return DLSS_Result_Success;
}

DLSSResult DLSSContextManager::CreateContext(uint32_t viewId, const DLSSContextCreateParams& params)
{
    if (!m_initialized.load())
    {
        DLSS_LOG_ERROR("CreateContext failed: DLSS not initialized");
        return DLSS_Result_Fail_NotInitialized;
    }

    std::lock_guard<std::mutex> lock(m_contextMutex);

    if (m_contexts.find(viewId) != m_contexts.end())
    {
        DLSS_LOG_WARN("CreateContext failed: context already exists for viewId %u", viewId);
        return DLSS_Result_Fail_ContextAlreadyExists;
    }

    const char* modeStr = (params.mode == DLSS_Mode_RayReconstruction) ? "RR" : "SR";
    const char* qualityStr = "";
    switch (params.quality)
    {
    case DLSS_Quality_DLAA: qualityStr = "DLAA"; break;
    case DLSS_Quality_UltraQuality: qualityStr = "UltraQuality"; break;
    case DLSS_Quality_MaxQuality: qualityStr = "Quality"; break;
    case DLSS_Quality_Balanced: qualityStr = "Balanced"; break;
    case DLSS_Quality_MaxPerformance: qualityStr = "Performance"; break;
    case DLSS_Quality_UltraPerformance: qualityStr = "UltraPerformance"; break;
    default: qualityStr = "Unknown"; break;
    }

    DLSS_LOG_INFO("Creating DLSS context (viewId=%u, mode=%s, quality=%s, input=%ux%u, output=%ux%u)",
        viewId, modeStr, qualityStr,
        params.inputResolution.width, params.inputResolution.height,
        params.outputResolution.width, params.outputResolution.height);

    // We need a command list for creating the feature
    // For now, create a temporary one (in practice, caller should provide)
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;

    HRESULT hr = m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmdAllocator));
    if (FAILED(hr))
    {
        DLSS_LOG_ERROR("Failed to create D3D12 command allocator: HRESULT 0x%08X", hr);
        return DLSS_Result_Fail_PlatformError;
    }

    hr = m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        cmdAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&cmdList));
    if (FAILED(hr))
    {
        DLSS_LOG_ERROR("Failed to create D3D12 command list: HRESULT 0x%08X", hr);
        return DLSS_Result_Fail_PlatformError;
    }

    auto context = std::make_unique<DLSSContext>();
    DLSSResult result = context->Create(m_device.Get(), cmdList.Get(), params);

    cmdList->Close();

    if (result != DLSS_Result_Success)
    {
        DLSS_LOG_ERROR("Failed to create DLSS context for viewId %u: %s", viewId, GetResultString(result));
        return result;
    }

    m_contexts[viewId] = std::move(context);

    DLSS_LOG_INFO("DLSS context created successfully for viewId %u", viewId);

    return DLSS_Result_Success;
}

DLSSResult DLSSContextManager::DestroyContext(uint32_t viewId)
{
    std::lock_guard<std::mutex> lock(m_contextMutex);

    auto it = m_contexts.find(viewId);
    if (it == m_contexts.end())
    {
        DLSS_LOG_DEBUG("DestroyContext: no context found for viewId %u (already destroyed)", viewId);
        return DLSS_Result_Success; // Not an error to destroy non-existent context
    }

    DLSS_LOG_INFO("Destroying DLSS context for viewId %u", viewId);

    m_contexts.erase(it);
    return DLSS_Result_Success;
}

void DLSSContextManager::DestroyAllContexts()
{
    std::lock_guard<std::mutex> lock(m_contextMutex);
    m_contexts.clear();
}

bool DLSSContextManager::HasContext(uint32_t viewId) const
{
    std::lock_guard<std::mutex> lock(m_contextMutex);
    return m_contexts.find(viewId) != m_contexts.end();
}

DLSSResult DLSSContextManager::UpdateContext(uint32_t viewId, const DLSSContextCreateParams& params)
{
    std::lock_guard<std::mutex> lock(m_contextMutex);

    auto it = m_contexts.find(viewId);
    if (it == m_contexts.end())
    {
        DLSS_LOG_ERROR("UpdateContext failed: context not found for viewId %u", viewId);
        return DLSS_Result_Fail_ContextNotFound;
    }

    if (!it->second->NeedsRecreation(params))
    {
        DLSS_LOG_DEBUG("UpdateContext: no recreation needed for viewId %u", viewId);
        return DLSS_Result_Success;
    }

    DLSS_LOG_INFO("UpdateContext: recreating context for viewId %u due to parameter changes", viewId);

    // Need to recreate - remove old context first
    m_contexts.erase(it);

    // Unlock and call CreateContext
    m_contextMutex.unlock();
    DLSSResult result = CreateContext(viewId, params);
    m_contextMutex.lock();

    return result;
}

DLSSResult DLSSContextManager::Execute(
    uint32_t viewId,
    ID3D12GraphicsCommandList* cmdList,
    const DLSSExecuteParams& params)
{
    if (!m_initialized.load())
    {
        DLSS_LOG_ERROR("Execute failed: DLSS not initialized");
        return DLSS_Result_Fail_NotInitialized;
    }

    if (!cmdList)
    {
        DLSS_LOG_ERROR("Execute failed: command list is null");
        return DLSS_Result_Fail_InvalidParameter;
    }

    std::lock_guard<std::mutex> lock(m_contextMutex);

    auto it = m_contexts.find(viewId);
    if (it == m_contexts.end())
    {
        DLSS_LOG_ERROR("Execute failed: context not found for viewId %u", viewId);
        return DLSS_Result_Fail_ContextNotFound;
    }

    DLSS_LOG_DEBUG("Executing DLSS for viewId %u (mode=%s, reset=%d)",
        viewId,
        params.mode == DLSS_Mode_RayReconstruction ? "RR" : "SR",
        params.common.reset);

    DLSSResult result = it->second->Execute(cmdList, params);

    if (result != DLSS_Result_Success)
    {
        DLSS_LOG_ERROR("Execute failed for viewId %u: %s", viewId, GetResultString(result));
    }

    return result;
}

void DLSSContextManager::SetExecuteParams(const DLSSExecuteParams& params)
{
    std::lock_guard<std::mutex> lock(m_executeParamsMutex);
    m_executeParams = params;
}

DLSSResult DLSSContextManager::TranslateNGXResult(int ngxResult)
{
    // Success case - no logging needed
    if (ngxResult == NVSDK_NGX_Result_Success)
    {
        return DLSS_Result_Success;
    }

    // Map NGX error codes to DLSSResult with detailed logging
    DLSSResult result;
    const char* errorDesc = nullptr;
    const char* suggestion = nullptr;

    switch (ngxResult)
    {
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported:
        result = DLSS_Result_Fail_FeatureNotSupported;
        errorDesc = "Feature not supported";
        suggestion = "Check GPU compatibility (requires NVIDIA RTX) and driver version";
        break;

    case NVSDK_NGX_Result_FAIL_PlatformError:
        result = DLSS_Result_Fail_PlatformError;
        errorDesc = "Platform error";
        suggestion = "Ensure D3D12 device is valid and properly initialized";
        break;

    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists:
        result = DLSS_Result_Fail_ContextAlreadyExists;
        errorDesc = "Feature already exists";
        suggestion = "Destroy existing context before creating a new one with same ID";
        break;

    case NVSDK_NGX_Result_FAIL_FeatureNotFound:
        result = DLSS_Result_Fail_ContextNotFound;
        errorDesc = "Feature not found";
        suggestion = "Ensure context was created before executing";
        break;

    case NVSDK_NGX_Result_FAIL_InvalidParameter:
        result = DLSS_Result_Fail_InvalidParameter;
        errorDesc = "Invalid parameter";
        suggestion = "Check input textures, resolutions, and parameter values";
        break;

    case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall:
        result = DLSS_Result_Fail_InvalidParameter;
        errorDesc = "Scratch buffer too small";
        suggestion = "Internal buffer allocation issue - try recreating context";
        break;

    case NVSDK_NGX_Result_FAIL_NotInitialized:
        result = DLSS_Result_Fail_NotInitialized;
        errorDesc = "NGX not initialized";
        suggestion = "Call DLSS_Initialize() before using DLSS features";
        break;

    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat:
        result = DLSS_Result_Fail_InvalidParameter;
        errorDesc = "Unsupported input format";
        suggestion = "Check texture formats - DLSS requires specific formats (e.g., RGBA16F for color)";
        break;

    case NVSDK_NGX_Result_FAIL_RWFlagMissing:
        result = DLSS_Result_Fail_InvalidParameter;
        errorDesc = "Read/Write flag missing on resource";
        suggestion = "Ensure output texture has UAV (unordered access) flag enabled";
        break;

    case NVSDK_NGX_Result_FAIL_MissingInput:
        result = DLSS_Result_Fail_InvalidParameter;
        errorDesc = "Required input missing";
        suggestion = "Provide all required textures (color, depth, motion vectors, output)";
        break;

    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature:
        result = DLSS_Result_Fail_NGXError;
        errorDesc = "Unable to initialize feature";
        suggestion = "DLSS model files may be missing or corrupted - reinstall DLSS DLLs";
        break;

    case NVSDK_NGX_Result_FAIL_OutOfDate:
        result = DLSS_Result_Fail_DriverOutOfDate;
        errorDesc = "Driver or SDK out of date";
        suggestion = "Update NVIDIA driver to latest version (minimum 531.0 for SR, 545.0 for RR)";
        break;

    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory:
        result = DLSS_Result_Fail_OutOfMemory;
        errorDesc = "Out of GPU memory";
        suggestion = "Reduce resolution, quality preset, or free GPU memory";
        break;

    case NVSDK_NGX_Result_FAIL_UnsupportedFormat:
        result = DLSS_Result_Fail_InvalidParameter;
        errorDesc = "Unsupported texture format";
        suggestion = "Use compatible formats: RGBA16F/RGBA32F for color, R32F/D32F for depth";
        break;

    case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath:
        result = DLSS_Result_Fail_PlatformError;
        errorDesc = "Unable to write to app data path";
        suggestion = "Check write permissions for DLSS log/cache directory";
        break;

    case NVSDK_NGX_Result_FAIL_UnsupportedParameter:
        result = DLSS_Result_Fail_InvalidParameter;
        errorDesc = "Unsupported parameter value";
        suggestion = "Check quality preset, feature flags, and mode settings";
        break;

    case NVSDK_NGX_Result_FAIL_Denied:
        result = DLSS_Result_Fail_FeatureNotSupported;
        errorDesc = "Feature access denied";
        suggestion = "DLSS may be disabled by driver settings or application profile";
        break;

    case NVSDK_NGX_Result_FAIL_NotImplemented:
        result = DLSS_Result_Fail_FeatureNotSupported;
        errorDesc = "Feature not implemented";
        suggestion = "This feature may not be available in current SDK/driver version";
        break;

    default:
        result = DLSS_Result_Fail_NGXError;
        errorDesc = "Unknown NGX error";
        suggestion = "Check NGX error code for details";
        break;
    }

    // Log the detailed error information
    DLSS_LOG_ERROR("NGX Error 0x%08X: %s", ngxResult, errorDesc);
    if (suggestion)
    {
        DLSS_LOG_ERROR("  Suggestion: %s", suggestion);
    }

    return result;
}

} // namespace dlss
