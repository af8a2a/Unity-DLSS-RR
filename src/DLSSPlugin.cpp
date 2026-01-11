// //------------------------------------------------------------------------------
// // DLSSPlugin.cpp - Exported C Functions for Unity P/Invoke
// //------------------------------------------------------------------------------
//
// #include <d3d12.h>
// #include <dxgi.h>
// #include <dxgi1_4.h>
// #include <unordered_map>
// #include <mutex>
//
// #include "DLSSPlugin.h"
// #include "DLSSContext.h"
// #include "IUnityGraphicsD3D12.h"
// #include "IUnityRenderingExtensions.h"
//
// #include <nvsdk_ngx.h>
// #include <nvsdk_ngx_params.h>
//
// //------------------------------------------------------------------------------
// // External Unity interface (defined in Plugin.cpp)
// //------------------------------------------------------------------------------
// extern IUnityGraphicsD3D12v8* g_unityGraphics_D3D12;
//
// //------------------------------------------------------------------------------
// // Low-Level Feature Handle Management
// //------------------------------------------------------------------------------
// static std::mutex g_featureHandleMutex;
// static uint32_t g_featureHandleCounter = 0;
// static std::unordered_map<int32_t, NVSDK_NGX_Handle*> g_featureHandles;
//
// //------------------------------------------------------------------------------
// // Unity Render Event Callbacks
// //------------------------------------------------------------------------------
// static void UNITY_INTERFACE_API OnDLSSRenderEvent(int eventID);
// static void UNITY_INTERFACE_API OnDLSSRenderEventWithData(int eventId, void* data);
//
// //------------------------------------------------------------------------------
// // Initialization/Shutdown
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Initialize(
//     uint64_t appId,
//     const char* projectId,
//     const char* engineVersion,
//     const wchar_t* logPath)
// {
//     if (!g_unityGraphics_D3D12)
//     {
//         return DLSS_Result_Fail_PlatformError;
//     }
//
//     ID3D12Device* device = g_unityGraphics_D3D12->GetDevice();
//     if (!device)
//     {
//         return DLSS_Result_Fail_PlatformError;
//     }
//
//     return dlss::DLSSContextManager::Instance().Initialize(
//         device, appId, projectId, engineVersion, logPath);
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Shutdown(void)
// {
//     dlss::DLSSContextManager::Instance().Shutdown();
// }
//
// DLSS_PLUGIN_API uint8_t DLSS_PLUGIN_CALL DLSS_IsInitialized(void)
// {
//     return dlss::DLSSContextManager::Instance().IsInitialized() ? 1 : 0;
// }
//
// //------------------------------------------------------------------------------
// // Capability Queries
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetCapabilities(DLSSCapabilityInfo* outInfo)
// {
//     if (!outInfo)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//     return dlss::DLSSContextManager::Instance().GetCapabilities(outInfo);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetOptimalSettings(
//     DLSSMode mode,
//     DLSSQuality quality,
//     uint32_t outputWidth,
//     uint32_t outputHeight,
//     DLSSOptimalSettings* outSettings)
// {
//     if (!outSettings)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//     return dlss::DLSSContextManager::Instance().GetOptimalSettings(
//         mode, quality, outputWidth, outputHeight, outSettings);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetStats(DLSSMode mode, DLSSStats* outStats)
// {
//     if (!outStats)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//     return dlss::DLSSContextManager::Instance().GetStats(mode, outStats);
// }
//
// //------------------------------------------------------------------------------
// // Context Management
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_CreateContext(
//     uint32_t viewId,
//     const DLSSContextCreateParams* params)
// {
//     if (!params)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//     return dlss::DLSSContextManager::Instance().CreateContext(viewId, *params);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_DestroyContext(uint32_t viewId)
// {
//     return dlss::DLSSContextManager::Instance().DestroyContext(viewId);
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_DestroyAllContexts(void)
// {
//     dlss::DLSSContextManager::Instance().DestroyAllContexts();
// }
//
// DLSS_PLUGIN_API uint8_t DLSS_PLUGIN_CALL DLSS_HasContext(uint32_t viewId)
// {
//     return dlss::DLSSContextManager::Instance().HasContext(viewId) ? 1 : 0;
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_UpdateContext(
//     uint32_t viewId,
//     const DLSSContextCreateParams* params)
// {
//     if (!params)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//     return dlss::DLSSContextManager::Instance().UpdateContext(viewId, *params);
// }
//
// //------------------------------------------------------------------------------
// // Execution
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Execute(
//     uint32_t viewId,
//     const DLSSExecuteParams* params)
// {
//     if (!params)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     auto& mgr = dlss::DLSSContextManager::Instance();
//     if (!mgr.IsInitialized())
//     {
//         return DLSS_Result_Fail_NotInitialized;
//     }
//
//     // Get command list from Unity via CommandRecordingState
//     if (!g_unityGraphics_D3D12)
//     {
//         return DLSS_Result_Fail_PlatformError;
//     }
//
//     UnityGraphicsD3D12RecordingState recordingState = {};
//     if (!g_unityGraphics_D3D12->CommandRecordingState(&recordingState) || !recordingState.commandList)
//     {
//         return DLSS_Result_Fail_PlatformError;
//     }
//
//     return mgr.Execute(viewId, recordingState.commandList, *params);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_ExecuteOnCommandList(
//     uint32_t viewId,
//     void* commandList,
//     const DLSSExecuteParams* params)
// {
//     if (!commandList || !params)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     auto& mgr = dlss::DLSSContextManager::Instance();
//     if (!mgr.IsInitialized())
//     {
//         return DLSS_Result_Fail_NotInitialized;
//     }
//
//     ID3D12GraphicsCommandList* cmdList = static_cast<ID3D12GraphicsCommandList*>(commandList);
//     return mgr.Execute(viewId, cmdList, *params);
// }
//
// //------------------------------------------------------------------------------
// // Unity Render Event Callback
// //------------------------------------------------------------------------------
//
// // Event ID for DLSS: 'DLSS' = 0x444C5353
// #define DLSS_RENDER_EVENT_ID 0x444C5353
//
// static void UNITY_INTERFACE_API OnDLSSRenderEvent(int eventID)
// {
//     if (eventID != DLSS_RENDER_EVENT_ID)
//     {
//         return;
//     }
//
//     auto& mgr = dlss::DLSSContextManager::Instance();
//     if (!mgr.IsInitialized())
//     {
//         return;
//     }
//
//     if (!g_unityGraphics_D3D12)
//     {
//         return;
//     }
//
//     // Get command list from Unity via CommandRecordingState
//     UnityGraphicsD3D12RecordingState recordingState = {};
//     if (!g_unityGraphics_D3D12->CommandRecordingState(&recordingState) || !recordingState.commandList)
//     {
//         return;
//     }
//
//     uint32_t viewId = mgr.GetCurrentView();
//     const DLSSExecuteParams& params = mgr.GetExecuteParams();
//
//     mgr.Execute(viewId, recordingState.commandList, params);
// }
//
// DLSS_PLUGIN_API void* DLSS_PLUGIN_CALL DLSS_GetRenderEventFunc(void)
// {
//     return reinterpret_cast<void*>(OnDLSSRenderEvent);
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_SetCurrentView(uint32_t viewId)
// {
//     dlss::DLSSContextManager::Instance().SetCurrentView(viewId);
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_SetExecuteParams(const DLSSExecuteParams* params)
// {
//     if (params)
//     {
//         dlss::DLSSContextManager::Instance().SetExecuteParams(*params);
//     }
// }
//
// //------------------------------------------------------------------------------
// // Debug/Utility
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API int32_t DLSS_PLUGIN_CALL DLSS_GetLastNGXError(void)
// {
//     return dlss::DLSSContextManager::Instance().GetLastNGXError();
// }
//
// DLSS_PLUGIN_API const char* DLSS_PLUGIN_CALL DLSS_GetResultString(DLSSResult result)
// {
//     return dlss::GetResultString(result);
// }
//
// //------------------------------------------------------------------------------
// // Logging
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_SetLogCallback(DLSSLogCallback callback)
// {
//     dlss::DLSSLogger::Instance().SetCallback(callback);
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_SetLogLevel(DLSSLogLevel level)
// {
//     dlss::DLSSLogger::Instance().SetLogLevel(level);
// }
//
// DLSS_PLUGIN_API DLSSLogLevel DLSS_PLUGIN_CALL DLSS_GetLogLevel(void)
// {
//     return dlss::DLSSLogger::Instance().GetLogLevel();
// }
//
// //------------------------------------------------------------------------------
// // Low-Level NGX Parameter API
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_AllocateParameters(void** outParams)
// {
//     if (!outParams)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     NVSDK_NGX_Parameter* params = nullptr;
//     NVSDK_NGX_Result result = NVSDK_NGX_D3D12_AllocateParameters(&params);
//
//     if (NVSDK_NGX_SUCCEED(result))
//     {
//         *outParams = params;
//         return DLSS_Result_Success;
//     }
//
//     *outParams = nullptr;
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetCapabilityParameters(void** outParams)
// {
//     if (!outParams)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     NVSDK_NGX_Parameter* params = nullptr;
//     NVSDK_NGX_Result result = NVSDK_NGX_D3D12_GetCapabilityParameters(&params);
//
//     if (NVSDK_NGX_SUCCEED(result))
//     {
//         *outParams = params;
//         return DLSS_Result_Success;
//     }
//
//     *outParams = nullptr;
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_DestroyParameters(void* params)
// {
//     if (!params)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     NVSDK_NGX_Result result = NVSDK_NGX_D3D12_DestroyParameters(
//         static_cast<NVSDK_NGX_Parameter*>(params));
//
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// //------------------------------------------------------------------------------
// // Low-Level Parameter Setters
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Parameter_SetI(void* params, const char* name, int32_t value)
// {
//     if (params && name)
//     {
//         NVSDK_NGX_Parameter_SetI(static_cast<NVSDK_NGX_Parameter*>(params), name, value);
//     }
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Parameter_SetUI(void* params, const char* name, uint32_t value)
// {
//     if (params && name)
//     {
//         NVSDK_NGX_Parameter_SetUI(static_cast<NVSDK_NGX_Parameter*>(params), name, value);
//     }
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Parameter_SetF(void* params, const char* name, float value)
// {
//     if (params && name)
//     {
//         NVSDK_NGX_Parameter_SetF(static_cast<NVSDK_NGX_Parameter*>(params), name, value);
//     }
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Parameter_SetD(void* params, const char* name, double value)
// {
//     if (params && name)
//     {
//         NVSDK_NGX_Parameter_SetD(static_cast<NVSDK_NGX_Parameter*>(params), name, value);
//     }
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Parameter_SetULL(void* params, const char* name, uint64_t value)
// {
//     if (params && name)
//     {
//         NVSDK_NGX_Parameter_SetULL(static_cast<NVSDK_NGX_Parameter*>(params), name, value);
//     }
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Parameter_SetD3D12Resource(void* params, const char* name, void* resource)
// {
//     if (params && name)
//     {
//         NVSDK_NGX_Parameter_SetD3d12Resource(
//             static_cast<NVSDK_NGX_Parameter*>(params),
//             name,
//             static_cast<ID3D12Resource*>(resource));
//     }
// }
//
// DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Parameter_SetVoidPointer(void* params, const char* name, void* ptr)
// {
//     if (params && name)
//     {
//         NVSDK_NGX_Parameter_SetVoidPointer(static_cast<NVSDK_NGX_Parameter*>(params), name, ptr);
//     }
// }
//
// //------------------------------------------------------------------------------
// // Low-Level Parameter Getters
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Parameter_GetI(void* params, const char* name, int32_t* outValue)
// {
//     if (!params || !name || !outValue)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetI(
//         static_cast<NVSDK_NGX_Parameter*>(params), name, outValue);
//
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Parameter_GetUI(void* params, const char* name, uint32_t* outValue)
// {
//     if (!params || !name || !outValue)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetUI(
//         static_cast<NVSDK_NGX_Parameter*>(params), name, outValue);
//
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Parameter_GetF(void* params, const char* name, float* outValue)
// {
//     if (!params || !name || !outValue)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetF(
//         static_cast<NVSDK_NGX_Parameter*>(params), name, outValue);
//
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Parameter_GetD(void* params, const char* name, double* outValue)
// {
//     if (!params || !name || !outValue)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetD(
//         static_cast<NVSDK_NGX_Parameter*>(params), name, outValue);
//
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Parameter_GetULL(void* params, const char* name, uint64_t* outValue)
// {
//     if (!params || !name || !outValue)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetULL(
//         static_cast<NVSDK_NGX_Parameter*>(params), name, outValue);
//
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Parameter_GetD3D12Resource(void* params, const char* name, void** outResource)
// {
//     if (!params || !name || !outResource)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     ID3D12Resource* resource = nullptr;
//     NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetD3d12Resource(
//         static_cast<NVSDK_NGX_Parameter*>(params), name, &resource);
//
//     *outResource = resource;
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Parameter_GetVoidPointer(void* params, const char* name, void** outPtr)
// {
//     if (!params || !name || !outPtr)
//     {
//         return DLSS_Result_Fail_InvalidParameter;
//     }
//
//     NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetVoidPointer(
//         static_cast<NVSDK_NGX_Parameter*>(params), name, outPtr);
//
//     return dlss::DLSSContextManager::TranslateNGXResult(result);
// }
//
// //------------------------------------------------------------------------------
// // Low-Level Feature Handle Management
// //------------------------------------------------------------------------------
//
// DLSS_PLUGIN_API int32_t DLSS_PLUGIN_CALL DLSS_AllocateFeatureHandle(void)
// {
//     std::lock_guard<std::mutex> lock(g_featureHandleMutex);
//
//     // Find next available handle (wrap around at 1024)
//     int32_t handle = static_cast<int32_t>(g_featureHandleCounter % 1024);
//
//     // Check if handle is already in use
//     if (g_featureHandles.find(handle) != g_featureHandles.end())
//     {
//         DLSS_LOG_ERROR("DLSS_AllocateFeatureHandle: Handle %d already exists", handle);
//         return DLSS_INVALID_FEATURE_HANDLE;
//     }
//
//     g_featureHandles[handle] = nullptr;
//     g_featureHandleCounter++;
//
//     DLSS_LOG_DEBUG("DLSS_AllocateFeatureHandle: Allocated handle %d", handle);
//     return handle;
// }
//
// DLSS_PLUGIN_API int32_t DLSS_PLUGIN_CALL DLSS_FreeFeatureHandle(int32_t handle)
// {
//     std::lock_guard<std::mutex> lock(g_featureHandleMutex);
//
//     auto it = g_featureHandles.find(handle);
//     if (it == g_featureHandles.end())
//     {
//         DLSS_LOG_ERROR("DLSS_FreeFeatureHandle: Handle %d does not exist", handle);
//         return -1;
//     }
//
//     g_featureHandles.erase(it);
//     DLSS_LOG_DEBUG("DLSS_FreeFeatureHandle: Freed handle %d", handle);
//     return 0;
// }
//
// //------------------------------------------------------------------------------
// // Render Event with Data (IssuePluginEventAndData)
// //------------------------------------------------------------------------------
//
// static void UNITY_INTERFACE_API OnDLSSRenderEventWithData(int eventId, void* data)
// {
//     if (!data)
//     {
//         DLSS_LOG_ERROR("OnDLSSRenderEventWithData: data is null for eventId %d", eventId);
//         return;
//     }
//
//     if (!g_unityGraphics_D3D12)
//     {
//         DLSS_LOG_ERROR("OnDLSSRenderEventWithData: g_unityGraphics_D3D12 is null");
//         return;
//     }
//
//     // Get command list from Unity
//     UnityGraphicsD3D12RecordingState recordingState = {};
//     if (!g_unityGraphics_D3D12->CommandRecordingState(&recordingState) || !recordingState.commandList)
//     {
//         DLSS_LOG_ERROR("OnDLSSRenderEventWithData: Failed to get command list from Unity");
//         return;
//     }
//
//     ID3D12GraphicsCommandList* cmdList = recordingState.commandList;
//
//     switch (eventId)
//     {
//     case DLSS_Event_CreateFeature:
//     {
//         DLSSCreateFeatureEventData* createData = static_cast<DLSSCreateFeatureEventData*>(data);
//         NVSDK_NGX_Parameter* params = static_cast<NVSDK_NGX_Parameter*>(createData->parameters);
//
//         NVSDK_NGX_Handle* ngxHandle = nullptr;
//         NVSDK_NGX_Feature feature = static_cast<NVSDK_NGX_Feature>(createData->feature);
//
//         NVSDK_NGX_Result result = NVSDK_NGX_D3D12_CreateFeature(
//             cmdList, feature, params, &ngxHandle);
//
//         if (NVSDK_NGX_SUCCEED(result))
//         {
//             std::lock_guard<std::mutex> lock(g_featureHandleMutex);
//             g_featureHandles[createData->handle] = ngxHandle;
//             DLSS_LOG_INFO("OnDLSSRenderEventWithData: Created feature handle %d (NGX feature %d)",
//                          createData->handle, static_cast<int>(feature));
//         }
//         else
//         {
//             DLSS_LOG_ERROR("OnDLSSRenderEventWithData: CreateFeature failed with 0x%08X", result);
//         }
//         break;
//     }
//
//     case DLSS_Event_EvaluateFeature:
//     {
//         DLSSEvaluateFeatureEventData* evalData = static_cast<DLSSEvaluateFeatureEventData*>(data);
//         NVSDK_NGX_Parameter* params = static_cast<NVSDK_NGX_Parameter*>(evalData->parameters);
//
//         NVSDK_NGX_Handle* ngxHandle = nullptr;
//         {
//             std::lock_guard<std::mutex> lock(g_featureHandleMutex);
//             auto it = g_featureHandles.find(evalData->handle);
//             if (it != g_featureHandles.end())
//             {
//                 ngxHandle = it->second;
//             }
//         }
//
//         if (ngxHandle)
//         {
//             NVSDK_NGX_Result result = NVSDK_NGX_D3D12_EvaluateFeature(cmdList, ngxHandle, params, nullptr);
//
//             if (!NVSDK_NGX_SUCCEED(result))
//             {
//                 DLSS_LOG_ERROR("OnDLSSRenderEventWithData: EvaluateFeature failed with 0x%08X for handle %d",
//                               result, evalData->handle);
//             }
//         }
//         else
//         {
//             DLSS_LOG_ERROR("OnDLSSRenderEventWithData: EvaluateFeature - handle %d not found", evalData->handle);
//         }
//         break;
//     }
//
//     case DLSS_Event_DestroyFeature:
//     {
//         DLSSDestroyFeatureEventData* destroyData = static_cast<DLSSDestroyFeatureEventData*>(data);
//
//         NVSDK_NGX_Handle* ngxHandle = nullptr;
//         {
//             std::lock_guard<std::mutex> lock(g_featureHandleMutex);
//             auto it = g_featureHandles.find(destroyData->handle);
//             if (it != g_featureHandles.end())
//             {
//                 ngxHandle = it->second;
//                 g_featureHandles.erase(it);
//             }
//         }
//
//         if (ngxHandle)
//         {
//             NVSDK_NGX_Result result = NVSDK_NGX_D3D12_ReleaseFeature(ngxHandle);
//
//             if (NVSDK_NGX_SUCCEED(result))
//             {
//                 DLSS_LOG_INFO("OnDLSSRenderEventWithData: Destroyed feature handle %d", destroyData->handle);
//             }
//             else
//             {
//                 DLSS_LOG_ERROR("OnDLSSRenderEventWithData: ReleaseFeature failed with 0x%08X for handle %d",
//                               result, destroyData->handle);
//             }
//         }
//         else
//         {
//             DLSS_LOG_ERROR("OnDLSSRenderEventWithData: DestroyFeature - handle %d not found", destroyData->handle);
//         }
//         break;
//     }
//
//     default:
//         DLSS_LOG_WARN("OnDLSSRenderEventWithData: Unknown eventId %d", eventId);
//         break;
//     }
// }
//
// DLSS_PLUGIN_API void* DLSS_PLUGIN_CALL DLSS_GetRenderEventAndDataFunc(void)
// {
//     return reinterpret_cast<void*>(OnDLSSRenderEventWithData);
// }
