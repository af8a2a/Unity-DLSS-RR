//------------------------------------------------------------------------------
// DLSSPluginLite.h - Minimal DLSS Plugin API (Thin NGX Wrapper)
//------------------------------------------------------------------------------
// This is a lightweight API that exposes NGX SDK directly to C#.
// All context management and parameter setup is done on the C# side.
// Based on UnityDenoiserPlugin pattern.
//------------------------------------------------------------------------------

#pragma once
#include <dxgi.h>
#include <dxgi1_4.h>
#include "IUnityGraphics.h"
#include "IUnityRenderingExtensions.h"

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------------------------------------------------
// Init Parameters
//------------------------------------------------------------------------------

/// Engine type enumeration (matches NVSDK_NGX_EngineType)
typedef enum DLSSEngineType
{
    DLSS_ENGINE_TYPE_CUSTOM = 0,
    DLSS_ENGINE_TYPE_UNREAL = 1,
    DLSS_ENGINE_TYPE_UNITY = 2,
    DLSS_ENGINE_TYPE_OMNIVERSE = 3
} DLSSEngineType;

/// Logging level (matches NVSDK_NGX_Logging_Level)
typedef enum DLSSLoggingLevel
{
    DLSS_LOGGING_LEVEL_OFF = 0,
    DLSS_LOGGING_LEVEL_ON = 1,
    DLSS_LOGGING_LEVEL_VERBOSE = 2
} DLSSLoggingLevel;

/// Initialization parameters
typedef struct DLSSInitParams
{
    const char* projectId;              // Application project ID (can be NULL)
    DLSSEngineType engineType;          // Engine type
    const char* engineVersion;          // Engine version string
    const wchar_t* applicationDataPath; // Path for NGX logs (can be NULL)
    DLSSLoggingLevel loggingLevel;      // NGX logging verbosity
} DLSSInitParams;

//------------------------------------------------------------------------------
// NGX Feature Types
//------------------------------------------------------------------------------

/// NGX Feature types (subset relevant to DLSS)
typedef enum DLSSNGXFeature
{
    DLSS_NGX_Feature_SuperSampling = 1,         // DLSS-SR
    DLSS_NGX_Feature_RayReconstruction = 13     // DLSS-RR
} DLSSNGXFeature;

//------------------------------------------------------------------------------
// Render Event Structures (for IssuePluginEventAndData)
//------------------------------------------------------------------------------

/// Render event IDs
typedef enum DLSSRenderEventId
{
    DLSS_Event_CreateFeature = 0,
    DLSS_Event_EvaluateFeature = 1,
    DLSS_Event_DestroyFeature = 2
} DLSSRenderEventId;

/// Parameters for create feature render event
typedef struct DLSSCreateFeatureParams
{
    int handle;
    DLSSNGXFeature feature;
    void* parameters;   // NVSDK_NGX_Parameter*
} DLSSCreateFeatureParams;

/// Parameters for evaluate feature render event
typedef struct DLSSEvaluateFeatureParams
{
    int handle;
    void* parameters;   // NVSDK_NGX_Parameter*
} DLSSEvaluateFeatureParams;

/// Parameters for destroy feature render event
typedef struct DLSSDestroyFeatureParams
{
    int handle;
} DLSSDestroyFeatureParams;

//------------------------------------------------------------------------------
// Exported Functions
//------------------------------------------------------------------------------

/// Invalid feature handle constant
#define DLSS_INVALID_FEATURE_HANDLE (-1)

//--- Initialization/Shutdown ---

/// Initialize DLSS with project ID for D3D12.
/// @param params Initialization parameters.
/// @return NGX result code (0 = success).
int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Init_with_ProjectID_D3D12(
    const DLSSInitParams* params);

/// Shutdown DLSS for D3D12.
/// @return NGX result code.
int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Shutdown_D3D12(void);

//--- Parameter Management ---

/// Allocate NGX parameters structure.
/// @param ppOutParameters Receives pointer to allocated parameters.
/// @return NGX result code.
int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_AllocateParameters_D3D12(
    void** ppOutParameters);

/// Get capability parameters from NGX (for querying feature support).
/// @param ppOutParameters Receives pointer to capability parameters.
/// @return NGX result code.
int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_GetCapabilityParameters_D3D12(
    void** ppOutParameters);

/// Destroy NGX parameters structure.
/// @param pInParameters Parameters to destroy.
/// @return NGX result code.
int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_DestroyParameters_D3D12(
    void* pInParameters);

//--- Parameter Setters (direct pass-through to NGX) ---

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetULL(
    void* pParameters, const char* paramName, unsigned long long value);

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetF(
    void* pParameters, const char* paramName, float value);

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetD(
    void* pParameters, const char* paramName, double value);

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetUI(
    void* pParameters, const char* paramName, unsigned int value);

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetI(
    void* pParameters, const char* paramName, int value);

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetD3d12Resource(
    void* pParameters, const char* paramName, void* value);

void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_SetVoidPointer(
    void* pParameters, const char* paramName, void* value);

//--- Parameter Getters (direct pass-through to NGX) ---

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetULL(
    void* pParameters, const char* paramName, unsigned long long* pValue);

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetF(
    void* pParameters, const char* paramName, float* pValue);

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetD(
    void* pParameters, const char* paramName, double* pValue);

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetUI(
    void* pParameters, const char* paramName, unsigned int* pValue);

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetI(
    void* pParameters, const char* paramName, int* pValue);

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetD3d12Resource(
    void* pParameters, const char* paramName, void** ppValue);

int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_Parameter_GetVoidPointer(
    void* pParameters, const char* paramName, void** ppValue);

//--- Feature Handle Management ---

/// Allocate a feature handle for use with render events.
/// @return Handle ID, or DLSS_INVALID_FEATURE_HANDLE on failure.
int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_AllocateFeatureHandle(void);

/// Free a feature handle.
/// @param handle Handle to free.
/// @return 0 on success, -1 on failure.
int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_FreeFeatureHandle(int handle);

//--- Render Event ---

/// Get the render event callback function for use with IssuePluginEventAndData.
/// Use with DLSSRenderEventId values as eventId.
/// Data should be pointer to DLSSCreateFeatureParams/DLSSEvaluateFeatureParams/DLSSDestroyFeatureParams.
/// @return UnityRenderingEventAndData function pointer.
UnityRenderingEventAndData UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API DLSS_UnityRenderEventFunc(void);

#ifdef __cplusplus
} // extern "C"
#endif
