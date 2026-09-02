//------------------------------------------------------------------------------
// DLSSPluginLite.cpp - Minimal DLSS Plugin Implementation (Thin NGX Wrapper)
//------------------------------------------------------------------------------
// This is a lightweight implementation that exposes NGX SDK directly to C#.
// All context management and parameter setup is done on the C# side.
// Based on UnityDenoiserPlugin pattern.
//------------------------------------------------------------------------------



#include <cstdlib>
#include <d3d12.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <sstream>
#include <vector>

// NGX SDK headers
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_dlssd.h>
#include <nvsdk_ngx_params.h>
#include "DLSSNeuralRendering.h"
#include "DLSSPluginLite.h"
#include "IUnityGraphicsD3D12.h"
#include "IUnityLog.h"
//------------------------------------------------------------------------------
// External Unity interfaces (defined in Plugin.cpp)
//------------------------------------------------------------------------------
extern IUnityGraphicsD3D12v8* g_unityGraphics_D3D12;
extern IUnityLog* g_unityLog;

//------------------------------------------------------------------------------
// Logging Helpers
//------------------------------------------------------------------------------

static void LogMessage(const char* msg)
{
    if (g_unityLog)
    {
        UNITY_LOG(g_unityLog, msg);
    }
}

static void LogWarning(const char* msg)
{
    if (g_unityLog)
    {
        UNITY_LOG_WARNING(g_unityLog, msg);
    }
}

static void LogError(const char* msg)
{
    if (g_unityLog)
    {
        UNITY_LOG_ERROR(g_unityLog, msg);
    }
}

static void LogDlssResult(NVSDK_NGX_Result result, const char* functionName)
{
    if (!NVSDK_NGX_SUCCEED(result))
    {
        std::ostringstream oss;
        oss << "[DLSS] " << functionName << " failed with error code: 0x"
            << std::hex << result << std::dec;

        switch (result)
        {
        case NVSDK_NGX_Result_FAIL_FeatureNotSupported:
            oss << " - Feature not supported on current hardware";
            break;
        case NVSDK_NGX_Result_FAIL_PlatformError:
            oss << " - Platform error, check D3D12 debug layer for more info";
            break;
        case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists:
            oss << " - Feature with given parameters already exists";
            break;
        case NVSDK_NGX_Result_FAIL_FeatureNotFound:
            oss << " - Feature with provided handle does not exist";
            break;
        case NVSDK_NGX_Result_FAIL_InvalidParameter:
            oss << " - Invalid parameter was provided";
            break;
        case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall:
            oss << " - Provided buffer is too small";
            break;
        case NVSDK_NGX_Result_FAIL_NotInitialized:
            oss << " - SDK was not initialized properly";
            break;
        case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat:
            oss << " - Unsupported format used for input/output buffers";
            break;
        case NVSDK_NGX_Result_FAIL_RWFlagMissing:
            oss << " - Feature input/output needs RW access (UAV)";
            break;
        case NVSDK_NGX_Result_FAIL_MissingInput:
            oss << " - Feature was created with specific input but none is provided at evaluation";
            break;
        case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature:
            oss << " - Feature is not available on the system";
            break;
        case NVSDK_NGX_Result_FAIL_OutOfDate:
            oss << " - NGX system libraries are old and need an update";
            break;
        case NVSDK_NGX_Result_FAIL_OutOfGPUMemory:
            oss << " - Feature requires more GPU memory than is available";
            break;
        case NVSDK_NGX_Result_FAIL_UnsupportedFormat:
            oss << " - Format used in input buffer(s) is not supported by feature";
            break;
        case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath:
            oss << " - Path provided in InApplicationDataPath cannot be written to";
            break;
        case NVSDK_NGX_Result_FAIL_UnsupportedParameter:
            oss << " - Unsupported parameter was provided";
            break;
        case NVSDK_NGX_Result_FAIL_Denied:
            oss << " - The feature or application was denied";
            break;
        case NVSDK_NGX_Result_FAIL_NotImplemented:
            oss << " - The feature or functionality is not implemented";
            break;
        default:
            oss << " - Unknown error";
            break;
        }

        LogError(oss.str().c_str());
    }
}

// Helper function to convert NVSDK_NGX_Feature to string
static const char* GetFeatureString(NVSDK_NGX_Feature feature)
{
    switch (feature)
    {
        case NVSDK_NGX_Feature_SuperSampling:
            return "DLSS-SR";
        case NVSDK_NGX_Feature_RayReconstruction:
            return "DLSS-RR";
        case NVSDK_NGX_Feature_FrameGeneration:
            return "FrameGeneration";
        case static_cast<NVSDK_NGX_Feature>(DLSS_NGX_Feature_NeuralRendering):
            return "DLSS 5 Neural Rendering";
        default:
            return "Unknown";
    }
}

//------------------------------------------------------------------------------
// NGX Log Callback
//------------------------------------------------------------------------------

static void NVSDK_CONV NGXLogCallback(const char* message, NVSDK_NGX_Logging_Level loggingLevel, NVSDK_NGX_Feature sourceComponent)
{
    std::ostringstream oss;
    oss << "[NGX][" << GetFeatureString(sourceComponent) << "]: " << message;

    switch (loggingLevel)
    {
    case NVSDK_NGX_LOGGING_LEVEL_VERBOSE:
    case NVSDK_NGX_LOGGING_LEVEL_ON:
        LogMessage(oss.str().c_str());
        break;
    case NVSDK_NGX_LOGGING_LEVEL_OFF:
        break;
    default:
        LogWarning(oss.str().c_str());
        break;
    }
}

//------------------------------------------------------------------------------
// Feature Handle Management
//------------------------------------------------------------------------------

static uint32_t g_featureHandleCounter = 0;

struct DLSSFeatureHandleRecord
{
    NVSDK_NGX_Handle* ngxHandle = nullptr;
    NVSDK_NGX_Parameter* parameters = nullptr;
    DLSSNGXFeature feature = DLSS_NGX_Feature_SuperSampling;
    DLSSFeatureStatus status = DLSS_FeatureStatus_Pending;
    int createResult = 0;
    int lastEvaluateResult = static_cast<int>(NVSDK_NGX_Result_Success);
    uint64_t lastUseFenceValue = 0;
    unsigned int nrInputWidth = 0;
    unsigned int nrInputHeight = 0;
    unsigned int nrOutputWidth = 0;
    unsigned int nrOutputHeight = 0;
    int nrPreset = 0;
    int nrUpscaling = 0;
};

struct DLSSPendingNeuralRenderingRelease
{
    NVSDK_NGX_Handle* ngxHandle = nullptr;
    NVSDK_NGX_Parameter* parameters = nullptr;
    uint64_t fenceValue = 0;
};

static std::mutex g_featureHandlesMutex;
static std::unordered_map<int, DLSSFeatureHandleRecord> g_featureHandles;
static std::vector<DLSSPendingNeuralRenderingRelease> g_pendingNeuralRenderingReleases;

static bool IsNeuralRenderingFeature(const DLSSFeatureHandleRecord& record)
{
    return record.feature == DLSS_NGX_Feature_NeuralRendering;
}

static uint64_t GetCompletedFrameFenceValue()
{
    if (!g_unityGraphics_D3D12)
        return 0;

    ID3D12Fence* fence = g_unityGraphics_D3D12->GetFrameFence();
    return fence ? fence->GetCompletedValue() : 0;
}

static void DestroyParameterMap(NVSDK_NGX_Parameter* parameters)
{
    if (!parameters)
        return;

    const NVSDK_NGX_Result result =
        NVSDK_NGX_D3D12_DestroyParameters(parameters);
    LogDlssResult(result, "NVSDK_NGX_D3D12_DestroyParameters");
}

static void ReleaseNeuralRenderingObjects(
    NVSDK_NGX_Handle* ngxHandle,
    NVSDK_NGX_Parameter* parameters)
{
    if (ngxHandle)
    {
        const int result = DLSSNR_ReleaseFeature(ngxHandle);
        LogDlssResult(
            static_cast<NVSDK_NGX_Result>(result),
            "NVSDK_NGX_D3D12_ReleaseFeature (DLSS-NR)");
    }

    DestroyParameterMap(parameters);
}

static void FlushPendingNeuralRenderingReleases(bool force)
{
    if (g_pendingNeuralRenderingReleases.empty())
        return;

    const uint64_t completedFence = force ? 0 : GetCompletedFrameFenceValue();
    size_t writeIndex = 0;
    for (size_t readIndex = 0;
         readIndex < g_pendingNeuralRenderingReleases.size();
         ++readIndex)
    {
        const DLSSPendingNeuralRenderingRelease pending =
            g_pendingNeuralRenderingReleases[readIndex];
        if (force || pending.fenceValue == 0 || completedFence >= pending.fenceValue)
        {
            ReleaseNeuralRenderingObjects(pending.ngxHandle, pending.parameters);
            continue;
        }

        g_pendingNeuralRenderingReleases[writeIndex++] = pending;
    }

    g_pendingNeuralRenderingReleases.resize(writeIndex);
}

static void ReleaseNeuralRenderingRecord(
    DLSSFeatureHandleRecord& record,
    bool force)
{
    if (!record.ngxHandle)
    {
        DestroyParameterMap(record.parameters);
        record.parameters = nullptr;
        return;
    }

    if (!force && record.lastUseFenceValue != 0 &&
        GetCompletedFrameFenceValue() < record.lastUseFenceValue)
    {
        g_pendingNeuralRenderingReleases.push_back(
            {record.ngxHandle, record.parameters, record.lastUseFenceValue});
    }
    else
    {
        ReleaseNeuralRenderingObjects(record.ngxHandle, record.parameters);
    }

    record.ngxHandle = nullptr;
    record.parameters = nullptr;
}

static int AllocateFeatureHandleLocked(
    NVSDK_NGX_Parameter* parameters,
    DLSSNGXFeature feature)
{
    for (uint32_t attempt = 0; attempt < 1024; ++attempt)
    {
        const int handle = static_cast<int>(g_featureHandleCounter++ % 1024);
        if (g_featureHandles.find(handle) != g_featureHandles.end())
            continue;

        DLSSFeatureHandleRecord record = {};
        record.parameters = parameters;
        record.feature = feature;
        g_featureHandles.emplace(handle, record);
        return handle;
    }

    return DLSS_INVALID_FEATURE_HANDLE;
}

//------------------------------------------------------------------------------
// Initialization/Shutdown
//------------------------------------------------------------------------------

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Init_with_ProjectID_D3D12(
    const DLSSInitParams* params)
{
    if (!params)
    {
        LogError("DLSS_Init_with_ProjectID_D3D12: params is null");
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }

    if (!g_unityGraphics_D3D12)
    {
        LogError("DLSS_Init_with_ProjectID_D3D12: Unity D3D12 interface not available");
        return static_cast<int>(NVSDK_NGX_Result_FAIL_PlatformError);
    }

    ID3D12Device* device = g_unityGraphics_D3D12->GetDevice();
    if (!device)
    {
        LogError("DLSS_Init_with_ProjectID_D3D12: D3D12 device not available");
        return static_cast<int>(NVSDK_NGX_Result_FAIL_PlatformError);
    }

    // Create feature common info for logging
    NVSDK_NGX_FeatureCommonInfo featureInfo = {};
    featureInfo.LoggingInfo.LoggingCallback = NGXLogCallback;
    featureInfo.LoggingInfo.MinimumLoggingLevel = static_cast<NVSDK_NGX_Logging_Level>(params->loggingLevel);
    featureInfo.LoggingInfo.DisableOtherLoggingSinks = true;

    // Initialize NGX
    NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init_with_ProjectID(
        params->projectId,
        static_cast<NVSDK_NGX_EngineType>(params->engineType),
        params->engineVersion,
        params->applicationDataPath,
        device,
        &featureInfo,
        NVSDK_NGX_Version_API);

    LogDlssResult(result, "NVSDK_NGX_D3D12_Init_with_ProjectID");

    if (NVSDK_NGX_SUCCEED(result))
    {
        LogMessage("[DLSS] Initialized successfully");
        // Neural Rendering uses a separately distributed, dynamically loaded
        // feature-18 runtime. Its absence or initialization failure must not
        // change the result of regular NGX initialization.
        DLSSNR_InitializeRuntime(device);
    }

    return static_cast<int>(result);
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Shutdown_D3D12(void)
{
    if (!g_unityGraphics_D3D12)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_PlatformError);
    }

    ID3D12Device* device = g_unityGraphics_D3D12->GetDevice();

    {
        std::lock_guard<std::mutex> lock(g_featureHandlesMutex);

        // Release all feature handles
        for (auto& pair : g_featureHandles)
        {
            if (IsNeuralRenderingFeature(pair.second))
            {
                ReleaseNeuralRenderingRecord(pair.second, true);
                continue;
            }

            if (pair.second.ngxHandle != nullptr)
            {
                NVSDK_NGX_D3D12_ReleaseFeature(pair.second.ngxHandle);
            }
            if (pair.second.parameters != nullptr)
            {
                NVSDK_NGX_D3D12_DestroyParameters(pair.second.parameters);
            }
        }
        g_featureHandles.clear();
        FlushPendingNeuralRenderingReleases(true);
        g_featureHandleCounter = 0;
    }

    // Release feature-18 objects before shutting down its runtime, and shut
    // that runtime down before the process NGX core that owns parameter maps.
    DLSSNR_ShutdownRuntime();

    NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Shutdown1(device);
    LogDlssResult(result, "NVSDK_NGX_D3D12_Shutdown1");

    LogMessage("[DLSS] Shutdown complete");
    return static_cast<int>(result);
}

//------------------------------------------------------------------------------
// Parameter Management
//------------------------------------------------------------------------------

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_AllocateParameters_D3D12(void** ppOutParameters)
{
    if (!ppOutParameters)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }

    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Result result = NVSDK_NGX_D3D12_AllocateParameters(&params);
    *ppOutParameters = params;

    LogDlssResult(result, "NVSDK_NGX_D3D12_AllocateParameters");
    return static_cast<int>(result);
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_GetCapabilityParameters_D3D12(void** ppOutParameters)
{
    if (!ppOutParameters)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }

    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Result result = NVSDK_NGX_D3D12_GetCapabilityParameters(&params);
    *ppOutParameters = params;

    LogDlssResult(result, "NVSDK_NGX_D3D12_GetCapabilityParameters");
    return static_cast<int>(result);
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_DestroyParameters_D3D12(void* pInParameters)
{
    if (!pInParameters)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }

    NVSDK_NGX_Result result = NVSDK_NGX_D3D12_DestroyParameters(
        static_cast<NVSDK_NGX_Parameter*>(pInParameters));

    LogDlssResult(result, "NVSDK_NGX_D3D12_DestroyParameters");
    return static_cast<int>(result);
}

//------------------------------------------------------------------------------
// Parameter Setters
//------------------------------------------------------------------------------

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetULL(
    void* pParameters, const char* paramName, unsigned long long value)
{
    if (pParameters && paramName)
    {
        NVSDK_NGX_Parameter_SetULL(static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, value);
    }
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetF(
    void* pParameters, const char* paramName, float value)
{
    if (pParameters && paramName)
    {
        NVSDK_NGX_Parameter_SetF(static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, value);
    }
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetD(
    void* pParameters, const char* paramName, double value)
{
    if (pParameters && paramName)
    {
        NVSDK_NGX_Parameter_SetD(static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, value);
    }
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetUI(
    void* pParameters, const char* paramName, unsigned int value)
{
    if (pParameters && paramName)
    {
        NVSDK_NGX_Parameter_SetUI(static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, value);
    }
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetI(
    void* pParameters, const char* paramName, int value)
{
    if (pParameters && paramName)
    {
        NVSDK_NGX_Parameter_SetI(static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, value);
    }
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetD3d12Resource(
    void* pParameters, const char* paramName, void* value)
{
    if (pParameters && paramName)
    {
        NVSDK_NGX_Parameter_SetD3d12Resource(
            static_cast<NVSDK_NGX_Parameter*>(pParameters),
            paramName,
            static_cast<ID3D12Resource*>(value));
    }
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetVoidPointer(
    void* pParameters, const char* paramName, void* value)
{
    if (pParameters && paramName)
    {
        NVSDK_NGX_Parameter_SetVoidPointer(static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, value);
    }
}

//------------------------------------------------------------------------------
// Parameter Getters
//------------------------------------------------------------------------------

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetULL(
    void* pParameters, const char* paramName, unsigned long long* pValue)
{
    if (!pParameters || !paramName || !pValue)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }
    return static_cast<int>(NVSDK_NGX_Parameter_GetULL(
        static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, pValue));
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetF(
    void* pParameters, const char* paramName, float* pValue)
{
    if (!pParameters || !paramName || !pValue)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }
    return static_cast<int>(NVSDK_NGX_Parameter_GetF(
        static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, pValue));
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetD(
    void* pParameters, const char* paramName, double* pValue)
{
    if (!pParameters || !paramName || !pValue)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }
    return static_cast<int>(NVSDK_NGX_Parameter_GetD(
        static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, pValue));
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetUI(
    void* pParameters, const char* paramName, unsigned int* pValue)
{
    if (!pParameters || !paramName || !pValue)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }
    return static_cast<int>(NVSDK_NGX_Parameter_GetUI(
        static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, pValue));
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetI(
    void* pParameters, const char* paramName, int* pValue)
{
    if (!pParameters || !paramName || !pValue)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }
    return static_cast<int>(NVSDK_NGX_Parameter_GetI(
        static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, pValue));
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetD3d12Resource(
    void* pParameters, const char* paramName, void** ppValue)
{
    if (!pParameters || !paramName || !ppValue)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }
    ID3D12Resource* resource = nullptr;
    NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetD3d12Resource(
        static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, &resource);
    *ppValue = resource;
    return static_cast<int>(result);
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetVoidPointer(
    void* pParameters, const char* paramName, void** ppValue)
{
    if (!pParameters || !paramName || !ppValue)
    {
        return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
    }
    return static_cast<int>(NVSDK_NGX_Parameter_GetVoidPointer(
        static_cast<NVSDK_NGX_Parameter*>(pParameters), paramName, ppValue));
}

//------------------------------------------------------------------------------
// Feature Handle Management
//------------------------------------------------------------------------------

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_AllocateFeatureHandle(
    void* parameters)
{
    if (!parameters)
    {
        LogError("DLSS_AllocateFeatureHandle: parameters is null");
        return DLSS_INVALID_FEATURE_HANDLE;
    }

    std::lock_guard<std::mutex> lock(g_featureHandlesMutex);

    const int handle = AllocateFeatureHandleLocked(
        static_cast<NVSDK_NGX_Parameter*>(parameters),
        DLSS_NGX_Feature_SuperSampling);
    if (handle != DLSS_INVALID_FEATURE_HANDLE)
        return handle;

    LogError("DLSS_AllocateFeatureHandle: no handles available");
    return DLSS_INVALID_FEATURE_HANDLE;
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_AllocateNeuralRenderingHandle(void)
{
    if (!DLSSNR_IsRuntimeAvailable())
    {
        LogError("DLSS_AllocateNeuralRenderingHandle: runtime is unavailable");
        return DLSS_INVALID_FEATURE_HANDLE;
    }

    NVSDK_NGX_Parameter* parameters = nullptr;
    const NVSDK_NGX_Result result =
        NVSDK_NGX_D3D12_AllocateParameters(&parameters);
    if (NVSDK_NGX_FAILED(result) || !parameters)
    {
        LogDlssResult(result, "NVSDK_NGX_D3D12_AllocateParameters (DLSS-NR)");
        return DLSS_INVALID_FEATURE_HANDLE;
    }

    std::lock_guard<std::mutex> lock(g_featureHandlesMutex);
    const int handle = AllocateFeatureHandleLocked(
        parameters,
        DLSS_NGX_Feature_NeuralRendering);
    if (handle == DLSS_INVALID_FEATURE_HANDLE)
    {
        DestroyParameterMap(parameters);
        LogError("DLSS_AllocateNeuralRenderingHandle: no handles available");
    }
    return handle;
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_IsNeuralRenderingAvailable(void)
{
    return DLSSNR_IsRuntimeAvailable() ? 1 : 0;
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_GetNeuralRenderingInitResult(void)
{
    return DLSSNR_GetRuntimeInitResult();
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_GetNeuralRenderingLastCreateResult(void)
{
    return DLSSNR_GetLastCreateResult();
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_GetNeuralRenderingLastEvaluateResult(void)
{
    return DLSSNR_GetLastEvaluateResult();
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_FreeFeatureHandle(int handle)
{
    std::lock_guard<std::mutex> lock(g_featureHandlesMutex);

    auto it = g_featureHandles.find(handle);
    if (it == g_featureHandles.end())
    {
        LogError("DLSS_FreeFeatureHandle: handle does not exist");
        return -1;
    }

    if (it->second.ngxHandle != nullptr)
    {
        LogError("DLSS_FreeFeatureHandle: ready feature must be released through a destroy render event");
        return -1;
    }

    if (it->second.parameters != nullptr)
    {
        NVSDK_NGX_Result result = NVSDK_NGX_D3D12_DestroyParameters(
            it->second.parameters);
        LogDlssResult(result, "NVSDK_NGX_D3D12_DestroyParameters");
    }

    g_featureHandles.erase(it);
    return 0;
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_GetFeatureHandleStatus(
    int handle,
    int* pCreateResult)
{
    if (!pCreateResult)
    {
        return static_cast<int>(DLSS_FeatureStatus_Invalid);
    }

    std::lock_guard<std::mutex> lock(g_featureHandlesMutex);

    auto it = g_featureHandles.find(handle);
    if (it == g_featureHandles.end())
    {
        *pCreateResult = static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
        return static_cast<int>(DLSS_FeatureStatus_Invalid);
    }

    *pCreateResult = it->second.createResult;
    return static_cast<int>(it->second.status);
}

UNITY_INTERFACE_EXPORT void* UNITY_INTERFACE_API DLSS_AllocateEventData(
    unsigned int size)
{
    if (size == 0)
        return nullptr;

    return std::malloc(size);
}

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_FreeEventData(void* data)
{
    std::free(data);
}

unsigned int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_GetEventDataSize(
    int eventId)
{
    switch (eventId)
    {
    case DLSS_Event_CreateFeature:
        return static_cast<unsigned int>(sizeof(DLSSCreateFeatureParams));
    case DLSS_Event_EvaluateSuperResolution:
        return static_cast<unsigned int>(sizeof(DLSSSuperResolutionEvaluateParams));
    case DLSS_Event_DestroyFeature:
        return static_cast<unsigned int>(sizeof(DLSSDestroyFeatureParams));
    case DLSS_Event_EvaluateRayReconstruction:
        return static_cast<unsigned int>(sizeof(DLSSRayReconstructionEvaluateParams));
    case DLSS_Event_CreateNeuralRendering:
        return static_cast<unsigned int>(sizeof(DLSSNeuralRenderingCreateParams));
    case DLSS_Event_EvaluateNeuralRendering:
        return static_cast<unsigned int>(sizeof(DLSSNeuralRenderingEvaluateParams));
    default:
        return 0;
    }
}

//------------------------------------------------------------------------------
// Render Event Handler
//------------------------------------------------------------------------------

struct EventDataDeleter
{
    void operator()(void* data) const
    {
        std::free(data);
    }
};

static void SetD3D12ResourceParameter(
    NVSDK_NGX_Parameter* parameters,
    const char* name,
    void* resource)
{
    NVSDK_NGX_Parameter_SetD3d12Resource(
        parameters,
        name,
        static_cast<ID3D12Resource*>(resource));
}

static void SetCommonEvaluateParameters(
    NVSDK_NGX_Parameter* parameters,
    const DLSSCommonEvaluateParams& common)
{
    SetD3D12ResourceParameter(parameters, NVSDK_NGX_Parameter_Color, common.color);
    SetD3D12ResourceParameter(parameters, NVSDK_NGX_Parameter_Output, common.output);
    SetD3D12ResourceParameter(parameters, NVSDK_NGX_Parameter_Depth, common.depth);
    SetD3D12ResourceParameter(parameters, NVSDK_NGX_Parameter_MotionVectors, common.motionVectors);

    NVSDK_NGX_Parameter_SetF(parameters, NVSDK_NGX_Parameter_Jitter_Offset_X, common.jitterOffsetX);
    NVSDK_NGX_Parameter_SetF(parameters, NVSDK_NGX_Parameter_Jitter_Offset_Y, common.jitterOffsetY);
    NVSDK_NGX_Parameter_SetF(parameters, NVSDK_NGX_Parameter_MV_Scale_X, common.motionVectorScaleX);
    NVSDK_NGX_Parameter_SetF(parameters, NVSDK_NGX_Parameter_MV_Scale_Y, common.motionVectorScaleY);
    NVSDK_NGX_Parameter_SetI(parameters, NVSDK_NGX_Parameter_Reset, common.reset);
    NVSDK_NGX_Parameter_SetUI(
        parameters,
        NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
        common.renderSubrectWidth);
    NVSDK_NGX_Parameter_SetUI(
        parameters,
        NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,
        common.renderSubrectHeight);
    NVSDK_NGX_Parameter_SetF(parameters, NVSDK_NGX_Parameter_DLSS_Pre_Exposure, common.preExposure);
    NVSDK_NGX_Parameter_SetF(parameters, NVSDK_NGX_Parameter_DLSS_Exposure_Scale, common.exposureScale);
    NVSDK_NGX_Parameter_SetI(
        parameters,
        NVSDK_NGX_Parameter_DLSS_Indicator_Invert_X_Axis,
        common.invertXAxis);
    NVSDK_NGX_Parameter_SetI(
        parameters,
        NVSDK_NGX_Parameter_DLSS_Indicator_Invert_Y_Axis,
        common.invertYAxis);
}

static NVSDK_NGX_Result EvaluateFeatureLocked(
    ID3D12GraphicsCommandList* commandList,
    DLSSFeatureHandleRecord& record,
    const DLSSCommonEvaluateParams& common)
{
    ID3D12Resource* outputResource = static_cast<ID3D12Resource*>(common.output);
    if (outputResource)
    {
        g_unityGraphics_D3D12->RequestResourceState(
            outputResource,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    NVSDK_NGX_Result result = NVSDK_NGX_D3D12_EvaluateFeature(
        commandList,
        record.ngxHandle,
        record.parameters,
        nullptr);

    if (outputResource)
    {
        g_unityGraphics_D3D12->NotifyResourceState(
            outputResource,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            true);
    }

    return result;
}

static void MarkCreateFeatureFailed(void* data, NVSDK_NGX_Result result)
{
    if (!data)
        return;

    DLSSCreateFeatureParams* params = static_cast<DLSSCreateFeatureParams*>(data);
    std::lock_guard<std::mutex> lock(g_featureHandlesMutex);

    auto it = g_featureHandles.find(params->handle);
    if (it != g_featureHandles.end())
    {
        it->second.ngxHandle = nullptr;
        it->second.status = DLSS_FeatureStatus_Failed;
        it->second.createResult = static_cast<int>(result);
    }
}

static void UNITY_INTERFACE_API OnDLSSRenderEvent(int eventId, void* data)
{
    if (!data)
    {
        LogError("OnDLSSRenderEvent: data is null");
        return;
    }

    std::unique_ptr<void, EventDataDeleter> ownedData(data);

    {
        std::lock_guard<std::mutex> lock(g_featureHandlesMutex);
        FlushPendingNeuralRenderingReleases(false);
    }

    ID3D12GraphicsCommandList* cmdList = nullptr;
    if (eventId != DLSS_Event_DestroyFeature)
    {
        if (!g_unityGraphics_D3D12)
        {
            LogError("OnDLSSRenderEvent: Unity D3D12 interface not available");
            if (eventId == DLSS_Event_CreateFeature ||
                eventId == DLSS_Event_CreateNeuralRendering)
            {
                MarkCreateFeatureFailed(data, NVSDK_NGX_Result_FAIL_PlatformError);
            }
            return;
        }

        // Create and evaluate need Unity's active D3D12 command list. Destroy
        // only releases native objects and remains valid without recording state.
        UnityGraphicsD3D12RecordingState recordingState = {};
        if (!g_unityGraphics_D3D12->CommandRecordingState(&recordingState) ||
            !recordingState.commandList)
        {
            LogError("OnDLSSRenderEvent: Failed to get command list from Unity");
            if (eventId == DLSS_Event_CreateFeature ||
                eventId == DLSS_Event_CreateNeuralRendering)
            {
                MarkCreateFeatureFailed(data, NVSDK_NGX_Result_FAIL_PlatformError);
            }
            return;
        }

        cmdList = recordingState.commandList;
    }

    switch (eventId)
    {
    case DLSS_Event_CreateFeature:
    {
        DLSSCreateFeatureParams* params = static_cast<DLSSCreateFeatureParams*>(data);
        std::lock_guard<std::mutex> lock(g_featureHandlesMutex);
        auto it = g_featureHandles.find(params->handle);
        if (it == g_featureHandles.end() ||
            it->second.status != DLSS_FeatureStatus_Pending ||
            it->second.parameters != params->parameters ||
            params->feature == DLSS_NGX_Feature_NeuralRendering)
        {
            LogError("OnDLSSRenderEvent: CreateFeature - invalid handle or parameter ownership");
            return;
        }

        NVSDK_NGX_Handle* ngxHandle = nullptr;
        NVSDK_NGX_Feature feature = static_cast<NVSDK_NGX_Feature>(params->feature);
        it->second.feature = params->feature;

        NVSDK_NGX_Result result = NVSDK_NGX_D3D12_CreateFeature(
            cmdList,
            feature,
            it->second.parameters,
            &ngxHandle);

        if (NVSDK_NGX_SUCCEED(result) && ngxHandle == nullptr)
        {
            result = NVSDK_NGX_Result_FAIL_FeatureNotFound;
        }

        LogDlssResult(result, "NVSDK_NGX_D3D12_CreateFeature");

        it->second.createResult = static_cast<int>(result);
        if (NVSDK_NGX_SUCCEED(result) && ngxHandle != nullptr)
        {
            it->second.ngxHandle = ngxHandle;
            it->second.status = DLSS_FeatureStatus_Ready;
        }
        else
        {
            if (ngxHandle != nullptr)
            {
                NVSDK_NGX_D3D12_ReleaseFeature(ngxHandle);
            }
            it->second.ngxHandle = nullptr;
            it->second.status = DLSS_FeatureStatus_Failed;
        }

        if (NVSDK_NGX_SUCCEED(result) && ngxHandle != nullptr)
        {
            std::ostringstream oss;
            oss << "[DLSS] Created " << GetFeatureString(feature) << " feature, handle=" << params->handle;
            LogMessage(oss.str().c_str());
        }
        break;
    }

    case DLSS_Event_CreateNeuralRendering:
    {
        DLSSNeuralRenderingCreateParams* params =
            static_cast<DLSSNeuralRenderingCreateParams*>(data);
        std::lock_guard<std::mutex> lock(g_featureHandlesMutex);
        auto it = g_featureHandles.find(params->handle);
        if (it == g_featureHandles.end() ||
            it->second.status != DLSS_FeatureStatus_Pending ||
            !IsNeuralRenderingFeature(it->second) ||
            !it->second.parameters ||
            params->inputWidth == 0 || params->inputHeight == 0 ||
            params->outputWidth == 0 || params->outputHeight == 0 ||
            (params->upscaling != 0 &&
             (params->outputWidth != params->inputWidth * 2u ||
              params->outputHeight != params->inputHeight * 2u)) ||
            (params->upscaling == 0 &&
             (params->outputWidth != params->inputWidth ||
              params->outputHeight != params->inputHeight)))
        {
            LogError("OnDLSSRenderEvent: CreateNeuralRendering - invalid handle or dimensions");
            if (it != g_featureHandles.end())
            {
                it->second.status = DLSS_FeatureStatus_Failed;
                it->second.createResult =
                    static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
            }
            return;
        }

        NVSDK_NGX_Handle* ngxHandle = nullptr;
        const int nativeResult = DLSSNR_CreateFeature(
            cmdList,
            it->second.parameters,
            *params,
            &ngxHandle);
        NVSDK_NGX_Result result = static_cast<NVSDK_NGX_Result>(nativeResult);
        if (NVSDK_NGX_SUCCEED(result) && !ngxHandle)
            result = NVSDK_NGX_Result_FAIL_FeatureNotFound;

        it->second.createResult = static_cast<int>(result);
        it->second.nrInputWidth = params->inputWidth;
        it->second.nrInputHeight = params->inputHeight;
        it->second.nrOutputWidth = params->outputWidth;
        it->second.nrOutputHeight = params->outputHeight;
        it->second.nrPreset = params->preset;
        it->second.nrUpscaling = params->upscaling;

        if (NVSDK_NGX_SUCCEED(result) && ngxHandle)
        {
            it->second.ngxHandle = ngxHandle;
            it->second.status = DLSS_FeatureStatus_Ready;
            std::ostringstream oss;
            oss << "[DLSS-NR] Created feature 18, handle=" << params->handle
                << ", " << params->inputWidth << "x" << params->inputHeight
                << " -> " << params->outputWidth << "x" << params->outputHeight;
            LogMessage(oss.str().c_str());
        }
        else
        {
            if (ngxHandle)
                DLSSNR_ReleaseFeature(ngxHandle);
            it->second.ngxHandle = nullptr;
            it->second.status = DLSS_FeatureStatus_Failed;
            LogDlssResult(result, "NVSDK_NGX_D3D12_CreateFeature (DLSS-NR)");
        }
        break;
    }

    case DLSS_Event_EvaluateSuperResolution:
    {
        DLSSSuperResolutionEvaluateParams* params =
            static_cast<DLSSSuperResolutionEvaluateParams*>(data);
        std::lock_guard<std::mutex> lock(g_featureHandlesMutex);
        auto it = g_featureHandles.find(params->common.handle);
        if (it == g_featureHandles.end() ||
            it->second.status != DLSS_FeatureStatus_Ready ||
            it->second.ngxHandle == nullptr ||
            it->second.parameters == nullptr ||
            it->second.feature != DLSS_NGX_Feature_SuperSampling)
        {
            std::ostringstream oss;
            oss << "OnDLSSRenderEvent: EvaluateSuperResolution - handle "
                << params->common.handle << " is not ready";
            LogError(oss.str().c_str());
            return;
        }

        SetCommonEvaluateParameters(it->second.parameters, params->common);
        SetD3D12ResourceParameter(
            it->second.parameters,
            NVSDK_NGX_Parameter_ExposureTexture,
            params->exposureTexture);
        SetD3D12ResourceParameter(
            it->second.parameters,
            NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask,
            params->biasColorMask);

        NVSDK_NGX_Result result = EvaluateFeatureLocked(
            cmdList,
            it->second,
            params->common);

        if (!NVSDK_NGX_SUCCEED(result))
        {
            LogDlssResult(result, "NVSDK_NGX_D3D12_EvaluateFeature (DLSS-SR)");
        }
        break;
    }

    case DLSS_Event_EvaluateRayReconstruction:
    {
        DLSSRayReconstructionEvaluateParams* params =
            static_cast<DLSSRayReconstructionEvaluateParams*>(data);
        std::lock_guard<std::mutex> lock(g_featureHandlesMutex);
        auto it = g_featureHandles.find(params->common.handle);
        if (it == g_featureHandles.end() ||
            it->second.status != DLSS_FeatureStatus_Ready ||
            it->second.ngxHandle == nullptr ||
            it->second.parameters == nullptr ||
            it->second.feature != DLSS_NGX_Feature_RayReconstruction)
        {
            std::ostringstream oss;
            oss << "OnDLSSRenderEvent: EvaluateRayReconstruction - handle "
                << params->common.handle << " is not ready";
            LogError(oss.str().c_str());
            return;
        }

        NVSDK_NGX_Parameter* parameters = it->second.parameters;
        SetCommonEvaluateParameters(parameters, params->common);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_ExposureTexture,
            params->exposureTexture);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_DiffuseAlbedo,
            params->diffuseAlbedo);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_SpecularAlbedo,
            params->specularAlbedo);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_GBuffer_Normals,
            params->normals);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_GBuffer_Roughness,
            params->roughness);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_GBuffer_Emissive,
            params->emissive);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_DLSSD_DiffuseRayDirection,
            params->diffuseRayDirection);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_DLSSD_DiffuseHitDistance,
            params->diffuseHitDistance);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_DLSSD_DiffuseRayDirectionHitDistance,
            params->diffuseRayDirectionHitDistance);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_DLSSD_SpecularRayDirection,
            params->specularRayDirection);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance,
            params->specularHitDistance);
        SetD3D12ResourceParameter(
            parameters,
            NVSDK_NGX_Parameter_DLSSD_SpecularRayDirectionHitDistance,
            params->specularRayDirectionHitDistance);
        NVSDK_NGX_Parameter_SetVoidPointer(
            parameters,
            NVSDK_NGX_Parameter_DLSS_WORLD_TO_VIEW_MATRIX,
            params->worldToView.values);
        NVSDK_NGX_Parameter_SetVoidPointer(
            parameters,
            NVSDK_NGX_Parameter_DLSS_VIEW_TO_CLIP_MATRIX,
            params->viewToClip.values);
        NVSDK_NGX_Parameter_SetF(
            parameters,
            NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,
            params->frameTimeDeltaMs);

        NVSDK_NGX_Result result = EvaluateFeatureLocked(
            cmdList,
            it->second,
            params->common);

        if (!NVSDK_NGX_SUCCEED(result))
        {
            LogDlssResult(result, "NVSDK_NGX_D3D12_EvaluateFeature (DLSS-RR)");
        }
        break;
    }

    case DLSS_Event_EvaluateNeuralRendering:
    {
        DLSSNeuralRenderingEvaluateParams* params =
            static_cast<DLSSNeuralRenderingEvaluateParams*>(data);
        std::lock_guard<std::mutex> lock(g_featureHandlesMutex);
        auto it = g_featureHandles.find(params->handle);
        if (it == g_featureHandles.end() ||
            it->second.status != DLSS_FeatureStatus_Ready ||
            !IsNeuralRenderingFeature(it->second) ||
            !it->second.ngxHandle || !it->second.parameters ||
            !params->color || !params->output || !params->depth ||
            !params->motionVectors || params->color == params->output ||
            params->inputWidth != it->second.nrInputWidth ||
            params->inputHeight != it->second.nrInputHeight ||
            params->outputWidth != it->second.nrOutputWidth ||
            params->outputHeight != it->second.nrOutputHeight)
        {
            LogError("OnDLSSRenderEvent: EvaluateNeuralRendering - invalid handle, resources, or dimensions");
            return;
        }

        ID3D12Resource* color = static_cast<ID3D12Resource*>(params->color);
        ID3D12Resource* output = static_cast<ID3D12Resource*>(params->output);
        ID3D12Resource* depth = static_cast<ID3D12Resource*>(params->depth);
        ID3D12Resource* motionVectors =
            static_cast<ID3D12Resource*>(params->motionVectors);

        g_unityGraphics_D3D12->RequestResourceState(
            color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g_unityGraphics_D3D12->RequestResourceState(
            depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g_unityGraphics_D3D12->RequestResourceState(
            motionVectors, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g_unityGraphics_D3D12->RequestResourceState(
            output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        const int nativeResult = DLSSNR_EvaluateFeature(
            cmdList,
            it->second.ngxHandle,
            it->second.parameters,
            *params);
        const NVSDK_NGX_Result result =
            static_cast<NVSDK_NGX_Result>(nativeResult);
        it->second.lastEvaluateResult = nativeResult;
        it->second.lastUseFenceValue =
            g_unityGraphics_D3D12->GetNextFrameFenceValue();

        g_unityGraphics_D3D12->NotifyResourceState(
            color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
        g_unityGraphics_D3D12->NotifyResourceState(
            depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
        g_unityGraphics_D3D12->NotifyResourceState(
            motionVectors, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
        g_unityGraphics_D3D12->NotifyResourceState(
            output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

        if (NVSDK_NGX_FAILED(result))
            LogDlssResult(result, "NVSDK_NGX_D3D12_EvaluateFeature (DLSS-NR)");
        break;
    }

    case DLSS_Event_DestroyFeature:
    {
        DLSSDestroyFeatureParams* params = static_cast<DLSSDestroyFeatureParams*>(data);

        std::lock_guard<std::mutex> lock(g_featureHandlesMutex);
        auto it = g_featureHandles.find(params->handle);
        if (it == g_featureHandles.end())
        {
            std::ostringstream oss;
            oss << "OnDLSSRenderEvent: DestroyFeature - handle " << params->handle << " not found";
            LogError(oss.str().c_str());
        }
        else
        {
            if (IsNeuralRenderingFeature(it->second))
            {
                ReleaseNeuralRenderingRecord(it->second, false);
                g_featureHandles.erase(it);
                break;
            }

            NVSDK_NGX_Handle* ngxHandle = it->second.ngxHandle;
            if (ngxHandle != nullptr)
            {
                NVSDK_NGX_Result result = NVSDK_NGX_D3D12_ReleaseFeature(ngxHandle);
                LogDlssResult(result, "NVSDK_NGX_D3D12_ReleaseFeature");

                if (NVSDK_NGX_SUCCEED(result))
                {
                    std::ostringstream oss;
                    oss << "[DLSS] Destroyed feature, handle=" << params->handle;
                    LogMessage(oss.str().c_str());
                }
            }

            if (it->second.parameters != nullptr)
            {
                NVSDK_NGX_Result result = NVSDK_NGX_D3D12_DestroyParameters(
                    it->second.parameters);
                LogDlssResult(result, "NVSDK_NGX_D3D12_DestroyParameters");
            }

            g_featureHandles.erase(it);
        }
        break;
    }

    default:
        {
            std::ostringstream oss;
            oss << "OnDLSSRenderEvent: Unknown eventId " << eventId;
            LogWarning(oss.str().c_str());
        }
        break;
    }
}

UnityRenderingEventAndData UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_UnityRenderEventFunc(void)
{
    return OnDLSSRenderEvent;
}
