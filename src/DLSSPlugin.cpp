//------------------------------------------------------------------------------
// DLSSPlugin.cpp - Exported C Functions for Unity P/Invoke
//------------------------------------------------------------------------------

#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include "DLSSPlugin.h"
#include "DLSSContext.h"
#include "IUnityGraphicsD3D12.h"

//------------------------------------------------------------------------------
// External Unity interface (defined in Plugin.cpp)
//------------------------------------------------------------------------------
extern IUnityGraphicsD3D12v8* g_unityGraphics_D3D12;

//------------------------------------------------------------------------------
// Unity Render Event Callback
//------------------------------------------------------------------------------
static void UNITY_INTERFACE_API OnDLSSRenderEvent(int eventID);

//------------------------------------------------------------------------------
// Initialization/Shutdown
//------------------------------------------------------------------------------

DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Initialize(
    uint64_t appId,
    const char* projectId,
    const char* engineVersion,
    const wchar_t* logPath)
{
    if (!g_unityGraphics_D3D12)
    {
        return DLSS_Result_Fail_PlatformError;
    }

    ID3D12Device* device = g_unityGraphics_D3D12->GetDevice();
    if (!device)
    {
        return DLSS_Result_Fail_PlatformError;
    }

    return dlss::DLSSContextManager::Instance().Initialize(
        device, appId, projectId, engineVersion, logPath);
}

DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Shutdown(void)
{
    dlss::DLSSContextManager::Instance().Shutdown();
}

DLSS_PLUGIN_API uint8_t DLSS_PLUGIN_CALL DLSS_IsInitialized(void)
{
    return dlss::DLSSContextManager::Instance().IsInitialized() ? 1 : 0;
}

//------------------------------------------------------------------------------
// Capability Queries
//------------------------------------------------------------------------------

DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetCapabilities(DLSSCapabilityInfo* outInfo)
{
    if (!outInfo)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }
    return dlss::DLSSContextManager::Instance().GetCapabilities(outInfo);
}

DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetOptimalSettings(
    DLSSMode mode,
    DLSSQuality quality,
    uint32_t outputWidth,
    uint32_t outputHeight,
    DLSSOptimalSettings* outSettings)
{
    if (!outSettings)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }
    return dlss::DLSSContextManager::Instance().GetOptimalSettings(
        mode, quality, outputWidth, outputHeight, outSettings);
}

DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetStats(DLSSMode mode, DLSSStats* outStats)
{
    if (!outStats)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }
    return dlss::DLSSContextManager::Instance().GetStats(mode, outStats);
}

//------------------------------------------------------------------------------
// Context Management
//------------------------------------------------------------------------------

DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_CreateContext(
    uint32_t viewId,
    const DLSSContextCreateParams* params)
{
    if (!params)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }
    return dlss::DLSSContextManager::Instance().CreateContext(viewId, *params);
}

DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_DestroyContext(uint32_t viewId)
{
    return dlss::DLSSContextManager::Instance().DestroyContext(viewId);
}

DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_DestroyAllContexts(void)
{
    dlss::DLSSContextManager::Instance().DestroyAllContexts();
}

DLSS_PLUGIN_API uint8_t DLSS_PLUGIN_CALL DLSS_HasContext(uint32_t viewId)
{
    return dlss::DLSSContextManager::Instance().HasContext(viewId) ? 1 : 0;
}

DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_UpdateContext(
    uint32_t viewId,
    const DLSSContextCreateParams* params)
{
    if (!params)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }
    return dlss::DLSSContextManager::Instance().UpdateContext(viewId, *params);
}

//------------------------------------------------------------------------------
// Execution
//------------------------------------------------------------------------------

DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Execute(
    uint32_t viewId,
    const DLSSExecuteParams* params)
{
    if (!params)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }

    auto& mgr = dlss::DLSSContextManager::Instance();
    if (!mgr.IsInitialized())
    {
        return DLSS_Result_Fail_NotInitialized;
    }

    // Get command list from Unity via CommandRecordingState
    if (!g_unityGraphics_D3D12)
    {
        return DLSS_Result_Fail_PlatformError;
    }

    UnityGraphicsD3D12RecordingState recordingState = {};
    if (!g_unityGraphics_D3D12->CommandRecordingState(&recordingState) || !recordingState.commandList)
    {
        return DLSS_Result_Fail_PlatformError;
    }

    return mgr.Execute(viewId, recordingState.commandList, *params);
}

DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_ExecuteOnCommandList(
    uint32_t viewId,
    void* commandList,
    const DLSSExecuteParams* params)
{
    if (!commandList || !params)
    {
        return DLSS_Result_Fail_InvalidParameter;
    }

    auto& mgr = dlss::DLSSContextManager::Instance();
    if (!mgr.IsInitialized())
    {
        return DLSS_Result_Fail_NotInitialized;
    }

    ID3D12GraphicsCommandList* cmdList = static_cast<ID3D12GraphicsCommandList*>(commandList);
    return mgr.Execute(viewId, cmdList, *params);
}

//------------------------------------------------------------------------------
// Unity Render Event Callback
//------------------------------------------------------------------------------

// Event ID for DLSS: 'DLSS' = 0x444C5353
#define DLSS_RENDER_EVENT_ID 0x444C5353

static void UNITY_INTERFACE_API OnDLSSRenderEvent(int eventID)
{
    if (eventID != DLSS_RENDER_EVENT_ID)
    {
        return;
    }

    auto& mgr = dlss::DLSSContextManager::Instance();
    if (!mgr.IsInitialized())
    {
        return;
    }

    if (!g_unityGraphics_D3D12)
    {
        return;
    }

    // Get command list from Unity via CommandRecordingState
    UnityGraphicsD3D12RecordingState recordingState = {};
    if (!g_unityGraphics_D3D12->CommandRecordingState(&recordingState) || !recordingState.commandList)
    {
        return;
    }

    uint32_t viewId = mgr.GetCurrentView();
    const DLSSExecuteParams& params = mgr.GetExecuteParams();

    mgr.Execute(viewId, recordingState.commandList, params);
}

DLSS_PLUGIN_API void* DLSS_PLUGIN_CALL DLSS_GetRenderEventFunc(void)
{
    return reinterpret_cast<void*>(OnDLSSRenderEvent);
}

DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_SetCurrentView(uint32_t viewId)
{
    dlss::DLSSContextManager::Instance().SetCurrentView(viewId);
}

DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_SetExecuteParams(const DLSSExecuteParams* params)
{
    if (params)
    {
        dlss::DLSSContextManager::Instance().SetExecuteParams(*params);
    }
}

//------------------------------------------------------------------------------
// Debug/Utility
//------------------------------------------------------------------------------

DLSS_PLUGIN_API int32_t DLSS_PLUGIN_CALL DLSS_GetLastNGXError(void)
{
    return dlss::DLSSContextManager::Instance().GetLastNGXError();
}

DLSS_PLUGIN_API const char* DLSS_PLUGIN_CALL DLSS_GetResultString(DLSSResult result)
{
    return dlss::GetResultString(result);
}
