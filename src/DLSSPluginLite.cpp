//------------------------------------------------------------------------------
// DLSSPluginLite.cpp - Minimal DLSS Plugin Implementation (Thin NGX Wrapper)
//------------------------------------------------------------------------------
// This is a lightweight implementation that exposes NGX SDK directly to C#.
// All context management and parameter setup is done on the C# side.
// Based on UnityDenoiserPlugin pattern.
//------------------------------------------------------------------------------



#include <d3d12.h>
#include <unordered_map>
#include <sstream>

// NGX SDK headers
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>
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
static std::unordered_map<int, NVSDK_NGX_Handle*> g_featureHandles;

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

    // Release all feature handles
    for (auto& pair : g_featureHandles)
    {
        if (pair.second != nullptr)
        {
            NVSDK_NGX_D3D12_ReleaseFeature(pair.second);
        }
    }
    g_featureHandles.clear();
    g_featureHandleCounter = 0;

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

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_AllocateFeatureHandle(void)
{
    // Find next available handle (wrap around at 1024)
    int handle = static_cast<int>(g_featureHandleCounter % 1024);

    if (g_featureHandles.find(handle) != g_featureHandles.end())
    {
        LogError("DLSS_AllocateFeatureHandle: handle already exists");
        return DLSS_INVALID_FEATURE_HANDLE;
    }

    g_featureHandles[handle] = nullptr;
    g_featureHandleCounter++;
    return handle;
}

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_FreeFeatureHandle(int handle)
{
    auto it = g_featureHandles.find(handle);
    if (it == g_featureHandles.end())
    {
        LogError("DLSS_FreeFeatureHandle: handle does not exist");
        return -1;
    }

    g_featureHandles.erase(it);
    return 0;
}

//------------------------------------------------------------------------------
// Render Event Handler
//------------------------------------------------------------------------------

static void UNITY_INTERFACE_API OnDLSSRenderEvent(int eventId, void* data)
{
    if (!data)
    {
        LogError("OnDLSSRenderEvent: data is null");
        return;
    }

    if (!g_unityGraphics_D3D12)
    {
        LogError("OnDLSSRenderEvent: Unity D3D12 interface not available");
        return;
    }

    // Get command list from Unity
    UnityGraphicsD3D12RecordingState recordingState = {};
    if (!g_unityGraphics_D3D12->CommandRecordingState(&recordingState) || !recordingState.commandList)
    {
        LogError("OnDLSSRenderEvent: Failed to get command list from Unity");
        return;
    }

    ID3D12GraphicsCommandList* cmdList = recordingState.commandList;

    switch (eventId)
    {
    case DLSS_Event_CreateFeature:
    {
        DLSSCreateFeatureParams* params = static_cast<DLSSCreateFeatureParams*>(data);
        NVSDK_NGX_Parameter* ngxParams = static_cast<NVSDK_NGX_Parameter*>(params->parameters);

        NVSDK_NGX_Handle* ngxHandle = nullptr;
        NVSDK_NGX_Feature feature = static_cast<NVSDK_NGX_Feature>(params->feature);

        NVSDK_NGX_Result result = NVSDK_NGX_D3D12_CreateFeature(
            cmdList, feature, ngxParams, &ngxHandle);

        LogDlssResult(result, "NVSDK_NGX_D3D12_CreateFeature");

        if (NVSDK_NGX_SUCCEED(result))
        {
            g_featureHandles[params->handle] = ngxHandle;

            std::ostringstream oss;
            oss << "[DLSS] Created " << GetFeatureString(feature) << " feature, handle=" << params->handle;
            LogMessage(oss.str().c_str());
        }
        break;
    }

    case DLSS_Event_EvaluateFeature:
    {
        DLSSEvaluateFeatureParams* params = static_cast<DLSSEvaluateFeatureParams*>(data);
        NVSDK_NGX_Parameter* ngxParams = static_cast<NVSDK_NGX_Parameter*>(params->parameters);

        auto it = g_featureHandles.find(params->handle);
        if (it == g_featureHandles.end() || it->second == nullptr)
        {
            std::ostringstream oss;
            oss << "OnDLSSRenderEvent: EvaluateFeature - handle " << params->handle << " not found";
            LogError(oss.str().c_str());
            return;
        }

        NVSDK_NGX_Handle* ngxHandle = it->second;
        NVSDK_NGX_Result result = NVSDK_NGX_D3D12_EvaluateFeature(cmdList, ngxHandle, ngxParams, nullptr);

        if (!NVSDK_NGX_SUCCEED(result))
        {
            LogDlssResult(result, "NVSDK_NGX_D3D12_EvaluateFeature");
        }
        break;
    }

    case DLSS_Event_DestroyFeature:
    {
        DLSSDestroyFeatureParams* params = static_cast<DLSSDestroyFeatureParams*>(data);

        auto it = g_featureHandles.find(params->handle);
        if (it == g_featureHandles.end())
        {
            std::ostringstream oss;
            oss << "OnDLSSRenderEvent: DestroyFeature - handle " << params->handle << " not found";
            LogError(oss.str().c_str());
            return;
        }

        NVSDK_NGX_Handle* ngxHandle = it->second;
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

        g_featureHandles.erase(it);
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
