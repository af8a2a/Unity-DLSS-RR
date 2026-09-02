//------------------------------------------------------------------------------
// DLSSNeuralRendering.h - optional DLSS 5 Neural Rendering runtime bridge
//------------------------------------------------------------------------------

#pragma once

#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct NVSDK_NGX_Handle;
struct NVSDK_NGX_Parameter;
struct DLSSNeuralRenderingCreateParams;
struct DLSSNeuralRenderingEvaluateParams;

/// Load and initialize the separately distributed nvngx_dlssnr.dll runtime.
/// Failure is non-fatal: DLSS-SR and DLSS-RR continue to use the regular NGX
/// runtime even when Neural Rendering is unavailable.
bool DLSSNR_InitializeRuntime(ID3D12Device* device);
void DLSSNR_ShutdownRuntime();
bool DLSSNR_IsRuntimeAvailable();
int32_t DLSSNR_GetRuntimeInitResult();
int32_t DLSSNR_GetLastCreateResult();
int32_t DLSSNR_GetLastEvaluateResult();

int32_t DLSSNR_CreateFeature(
    ID3D12GraphicsCommandList* commandList,
    NVSDK_NGX_Parameter* parameters,
    const DLSSNeuralRenderingCreateParams& createParams,
    NVSDK_NGX_Handle** handle);

int32_t DLSSNR_EvaluateFeature(
    ID3D12GraphicsCommandList* commandList,
    const NVSDK_NGX_Handle* handle,
    NVSDK_NGX_Parameter* parameters,
    const DLSSNeuralRenderingEvaluateParams& evaluateParams);

int32_t DLSSNR_ReleaseFeature(NVSDK_NGX_Handle* handle);
