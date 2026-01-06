//------------------------------------------------------------------------------
// DLSSPlugin.h - Unified DLSS-SR and DLSS-RR API for Unity Native Plugin
//------------------------------------------------------------------------------
// Public C API for P/Invoke from Unity C#.
// Plugin manages DLSS contexts internally, keyed by viewID.
//------------------------------------------------------------------------------

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// DLL export macros
#ifdef _WIN32
    #define DLSS_PLUGIN_API __declspec(dllexport)
    #define DLSS_PLUGIN_CALL __stdcall
#else
    #define DLSS_PLUGIN_API __attribute__((visibility("default")))
    #define DLSS_PLUGIN_CALL
#endif

//------------------------------------------------------------------------------
// SECTION 1: Enumerations
//------------------------------------------------------------------------------

/// Result codes returned by plugin functions.
typedef enum DLSSResult
{
    DLSS_Result_Success = 0,
    DLSS_Result_Fail_NotInitialized = -1,
    DLSS_Result_Fail_FeatureNotSupported = -2,
    DLSS_Result_Fail_InvalidParameter = -3,
    DLSS_Result_Fail_OutOfMemory = -4,
    DLSS_Result_Fail_ContextNotFound = -5,
    DLSS_Result_Fail_ContextAlreadyExists = -6,
    DLSS_Result_Fail_DriverOutOfDate = -7,
    DLSS_Result_Fail_PlatformError = -8,
    DLSS_Result_Fail_NGXError = -9
} DLSSResult;

/// DLSS operating mode - selects between Super Resolution and Ray Reconstruction.
typedef enum DLSSMode
{
    DLSS_Mode_Off = 0,
    DLSS_Mode_SuperResolution = 1,      // Standard DLSS-SR (upscaling + AA)
    DLSS_Mode_RayReconstruction = 2     // DLSS-RR (ray tracing denoiser + upscaler)
} DLSSMode;

/// Quality preset - affects resolution scaling factor.
/// Maps to NVSDK_NGX_PerfQuality_Value.
typedef enum DLSSQuality
{
    DLSS_Quality_MaxPerformance = 0,    // UltraPerformance equivalent
    DLSS_Quality_Balanced = 1,
    DLSS_Quality_MaxQuality = 2,
    DLSS_Quality_UltraPerformance = 3,
    DLSS_Quality_UltraQuality = 4,
    DLSS_Quality_DLAA = 5               // No upscaling, AA only (1:1)
} DLSSQuality;

/// Render presets for DLSS-SR.
/// Maps to NVSDK_NGX_DLSS_Hint_Render_Preset.
typedef enum DLSSSRPreset
{
    DLSS_SR_Preset_Default = 0,
    DLSS_SR_Preset_F = 6,       // Deprecated
    DLSS_SR_Preset_G = 7,       // Reverts to default
    DLSS_SR_Preset_J = 10,      // Less ghosting, more flickering
    DLSS_SR_Preset_K = 11,      // Best quality (transformer-based)
    DLSS_SR_Preset_L = 12,      // Default for Ultra Perf
    DLSS_SR_Preset_M = 13       // Default for Perf
} DLSSSRPreset;

/// Render presets for DLSS-RR (Ray Reconstruction).
/// Maps to NVSDK_NGX_RayReconstruction_Hint_Render_Preset.
typedef enum DLSSRRPreset
{
    DLSS_RR_Preset_Default = 0,
    DLSS_RR_Preset_D = 4,       // Default transformer model
    DLSS_RR_Preset_E = 5        // Latest transformer (required for DoF guide)
} DLSSRRPreset;

/// Feature flags for context creation.
/// Maps to NVSDK_NGX_DLSS_Feature_Flags.
typedef enum DLSSFeatureFlags
{
    DLSS_Flag_None = 0,
    DLSS_Flag_IsHDR = (1 << 0),             // Input is HDR (pre-tonemapped)
    DLSS_Flag_MVLowRes = (1 << 1),          // Motion vectors are low-res
    DLSS_Flag_MVJittered = (1 << 2),        // Motion vectors include jitter
    DLSS_Flag_DepthInverted = (1 << 3),     // Reversed-Z depth buffer
    DLSS_Flag_AutoExposure = (1 << 6),      // Use auto-exposure
    DLSS_Flag_AlphaUpscaling = (1 << 7)     // Upscale alpha channel
} DLSSFeatureFlags;

/// Depth type for Ray Reconstruction.
/// Maps to NVSDK_NGX_DLSS_Depth_Type.
typedef enum DLSSDepthType
{
    DLSS_Depth_Linear = 0,      // Linear depth buffer
    DLSS_Depth_Hardware = 1     // Hardware Z-buffer
} DLSSDepthType;

/// Roughness packing mode for Ray Reconstruction.
/// Maps to NVSDK_NGX_DLSS_Roughness_Mode.
typedef enum DLSSRoughnessMode
{
    DLSS_Roughness_Unpacked = 0,        // Roughness in separate texture
    DLSS_Roughness_PackedInNormalsW = 1 // Roughness in normals.w channel
} DLSSRoughnessMode;

/// Denoise mode for Ray Reconstruction.
/// Maps to NVSDK_NGX_DLSS_Denoise_Mode.
typedef enum DLSSDenoiseMode
{
    DLSS_Denoise_Off = 0,
    DLSS_Denoise_DLUnified = 1  // DL-based unified upscaler (required for RR)
} DLSSDenoiseMode;

//------------------------------------------------------------------------------
// SECTION 2: Parameter Structures
//------------------------------------------------------------------------------

/// Common resolution/dimension parameters.
typedef struct DLSSDimensions
{
    uint32_t width;
    uint32_t height;
} DLSSDimensions;

/// Coordinates for subrect base (atlas support).
typedef struct DLSSCoordinates
{
    uint32_t x;
    uint32_t y;
} DLSSCoordinates;

/// 4x4 matrix (column-major, matches Unity/D3D convention).
typedef struct DLSSMatrix4x4
{
    float m[16];  // Column-major: m[0-3]=col0, m[4-7]=col1, etc.
} DLSSMatrix4x4;

//------------------------------------------------------------------------------
// Context Creation Parameters
//------------------------------------------------------------------------------

/// Parameters for creating a DLSS context.
typedef struct DLSSContextCreateParams
{
    // === Required for both SR and RR ===
    DLSSMode mode;                      // SR or RR
    DLSSQuality quality;                // Quality preset
    DLSSDimensions inputResolution;     // Render resolution (low-res input)
    DLSSDimensions outputResolution;    // Target resolution (upscaled output)
    uint32_t featureFlags;              // DLSSFeatureFlags bitmask

    // === SR-specific presets (one per quality level) ===
    DLSSSRPreset presetDLAA;
    DLSSSRPreset presetQuality;
    DLSSSRPreset presetBalanced;
    DLSSSRPreset presetPerformance;
    DLSSSRPreset presetUltraPerformance;
    DLSSSRPreset presetUltraQuality;

    // === RR-specific parameters ===
    DLSSDenoiseMode denoiseMode;        // Off or DLUnified
    DLSSDepthType depthType;            // Linear vs HW depth
    DLSSRoughnessMode roughnessMode;    // Roughness texture packing
    DLSSRRPreset presetRR_DLAA;
    DLSSRRPreset presetRR_Quality;
    DLSSRRPreset presetRR_Balanced;
    DLSSRRPreset presetRR_Performance;
    DLSSRRPreset presetRR_UltraPerformance;
    DLSSRRPreset presetRR_UltraQuality;

    // === Optional ===
    uint8_t enableOutputSubrects;       // Enable subrect output (for atlases)
} DLSSContextCreateParams;

//------------------------------------------------------------------------------
// Execution Parameters - Common (SR and RR)
//------------------------------------------------------------------------------

/// Common texture inputs shared by SR and RR.
/// All textures are ID3D12Resource* (passed as void* for C interop).
typedef struct DLSSCommonTextures
{
    void* colorInput;           // Required: Noisy/low-res color input
    void* colorOutput;          // Required: Upscaled output destination
    void* depth;                // Required: Depth buffer
    void* motionVectors;        // Required: Motion vectors (2D screen-space)
    void* exposureTexture;      // Optional: 1x1 exposure scale texture
    void* biasColorMask;        // Optional: Bias current color mask
    void* transparencyMask;     // Optional: Reserved for future use
} DLSSCommonTextures;

/// Common per-frame parameters for both SR and RR.
typedef struct DLSSCommonParams
{
    // === Jitter (required) ===
    float jitterOffsetX;        // Jitter X in render pixel space
    float jitterOffsetY;        // Jitter Y in render pixel space

    // === Motion vector scaling (required for correct MV interpretation) ===
    float mvScaleX;             // MV scale X (typically render width)
    float mvScaleY;             // MV scale Y (typically render height)

    // === Rendering state ===
    DLSSDimensions renderSubrectDimensions; // Actual rendered area dimensions
    uint8_t reset;              // Reset temporal history (scene change)

    // === Exposure (optional) ===
    float preExposure;          // Pre-exposure multiplier (default 1.0)
    float exposureScale;        // Exposure scale (default 1.0)

    // === Indicator/debug (optional) ===
    uint8_t invertYAxis;        // Invert Y axis for indicator
    uint8_t invertXAxis;        // Invert X axis for indicator

    // === Subrect offsets (for atlas rendering) ===
    DLSSCoordinates colorSubrectBase;
    DLSSCoordinates depthSubrectBase;
    DLSSCoordinates mvSubrectBase;
    DLSSCoordinates outputSubrectBase;
    DLSSCoordinates biasColorSubrectBase;
} DLSSCommonParams;

//------------------------------------------------------------------------------
// Execution Parameters - Ray Reconstruction Specific
//------------------------------------------------------------------------------

/// GBuffer textures for Ray Reconstruction.
typedef struct DLSSRRGBufferTextures
{
    void* diffuseAlbedo;        // Required: Diffuse albedo
    void* specularAlbedo;       // Required: Specular albedo
    void* normals;              // Required: World-space normals (optionally with roughness in .w)
    void* roughness;            // Optional if packed in normals.w
    void* emissive;             // Optional: Emissive channel
} DLSSRRGBufferTextures;

/// Ray direction and hit distance textures for DLSS-RR.
typedef struct DLSSRRRayTextures
{
    // === Separate ray direction and hit distance (recommended) ===
    void* diffuseRayDirection;      // RGB: normalized ray direction
    void* diffuseHitDistance;       // R: hit distance
    void* specularRayDirection;     // RGB: normalized ray direction
    void* specularHitDistance;      // R: hit distance

    // === Combined direction+distance (alternative) ===
    void* diffuseRayDirectionHitDistance;  // RGBA: direction.xyz + distance.w
    void* specularRayDirectionHitDistance; // RGBA: direction.xyz + distance.w
} DLSSRRRayTextures;

/// Optional textures for advanced RR features.
typedef struct DLSSRROptionalTextures
{
    // Reflected albedo
    void* reflectedAlbedo;

    // Particle handling
    void* colorBeforeParticles;
    void* colorAfterParticles;

    // Transparency handling
    void* colorBeforeTransparency;
    void* colorAfterTransparency;

    // Fog handling
    void* colorBeforeFog;
    void* colorAfterFog;

    // Depth of Field (requires Preset E)
    void* depthOfFieldGuide;
    void* colorBeforeDepthOfField;
    void* colorAfterDepthOfField;

    // Subsurface scattering
    void* screenSpaceSubsurfaceScatteringGuide;
    void* colorBeforeScreenSpaceSubsurfaceScattering;
    void* colorAfterScreenSpaceSubsurfaceScattering;

    // Refraction
    void* screenSpaceRefractionGuide;
    void* colorBeforeScreenSpaceRefraction;
    void* colorAfterScreenSpaceRefraction;

    // Additional inputs
    void* motionVectorsReflections;     // MVs for specular reflections
    void* transparencyLayer;            // Input-res particle layer
    void* transparencyLayerOpacity;
    void* transparencyLayerMvecs;
    void* disocclusionMask;

    // Alpha
    void* alpha;                        // Alpha channel input
    void* outputAlpha;                  // Alpha channel output
} DLSSRROptionalTextures;

/// Ray Reconstruction specific parameters.
typedef struct DLSSRRParams
{
    DLSSRRGBufferTextures gbuffer;
    DLSSRRRayTextures rays;
    DLSSRROptionalTextures optional;

    // === Matrices (required for RR) ===
    DLSSMatrix4x4 worldToViewMatrix;
    DLSSMatrix4x4 viewToClipMatrix;

    // === Timing (optional) ===
    float frameTimeDeltaMs;
} DLSSRRParams;

//------------------------------------------------------------------------------
// Combined Execute Parameters
//------------------------------------------------------------------------------

/// Unified execution parameters structure.
typedef struct DLSSExecuteParams
{
    DLSSMode mode;              // Must match context mode
    DLSSCommonTextures textures;
    DLSSCommonParams common;
    DLSSRRParams rrParams;      // Only used when mode == DLSS_Mode_RayReconstruction
} DLSSExecuteParams;

//------------------------------------------------------------------------------
// SECTION 3: Capability/Query Structures
//------------------------------------------------------------------------------

/// Information about DLSS feature availability.
typedef struct DLSSCapabilityInfo
{
    uint8_t dlssSRAvailable;        // DLSS-SR supported
    uint8_t dlssRRAvailable;        // DLSS-RR (Ray Reconstruction) supported
    uint8_t needsDriverUpdate;      // Driver update recommended
    uint32_t minDriverVersionMajor; // Minimum driver version
    uint32_t minDriverVersionMinor;
} DLSSCapabilityInfo;

/// Optimal settings for a given quality mode and output resolution.
typedef struct DLSSOptimalSettings
{
    uint32_t optimalRenderWidth;
    uint32_t optimalRenderHeight;
    uint32_t minRenderWidth;
    uint32_t minRenderHeight;
    uint32_t maxRenderWidth;
    uint32_t maxRenderHeight;
    float sharpness;                // Deprecated but kept for compatibility
} DLSSOptimalSettings;

/// Memory statistics for DLSS.
typedef struct DLSSStats
{
    uint64_t vramAllocatedBytes;
    uint32_t optLevel;
    uint8_t isDevBranch;
} DLSSStats;

//------------------------------------------------------------------------------
// SECTION 4: Exported C Functions
//------------------------------------------------------------------------------

//--- Initialization/Shutdown ---

/// Initialize the DLSS subsystem.
/// Must be called after Unity graphics device is initialized.
/// @param appId Application ID for NVIDIA profile matching (0 for generic).
/// @param projectId Project ID string (can be NULL).
/// @param engineVersion Engine version string (can be NULL).
/// @param logPath Path for DLSS log files (can be NULL for default).
/// @return DLSS_Result_Success on success.
DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Initialize(
    uint64_t appId,
    const char* projectId,
    const char* engineVersion,
    const wchar_t* logPath);

/// Shutdown the DLSS subsystem and release all contexts.
DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_Shutdown(void);

/// Check if DLSS is initialized and ready.
/// @return 1 if initialized, 0 otherwise.
DLSS_PLUGIN_API uint8_t DLSS_PLUGIN_CALL DLSS_IsInitialized(void);

//--- Capability Queries ---

/// Query DLSS feature capabilities.
/// @param outInfo Pointer to receive capability information.
/// @return DLSS_Result_Success on success.
DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetCapabilities(DLSSCapabilityInfo* outInfo);

/// Query optimal render resolution for a given output size and quality mode.
/// @param mode DLSS mode (SR or RR).
/// @param quality Quality preset.
/// @param outputWidth Desired output width.
/// @param outputHeight Desired output height.
/// @param outSettings Pointer to receive optimal settings.
/// @return DLSS_Result_Success on success.
DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetOptimalSettings(
    DLSSMode mode,
    DLSSQuality quality,
    uint32_t outputWidth,
    uint32_t outputHeight,
    DLSSOptimalSettings* outSettings);

/// Query current DLSS memory statistics.
/// @param mode DLSS mode to query stats for.
/// @param outStats Pointer to receive statistics.
/// @return DLSS_Result_Success on success.
DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_GetStats(DLSSMode mode, DLSSStats* outStats);

//--- Context Management (Per-View) ---

/// Create a DLSS context for a specific view (camera).
/// @param viewId Unique identifier for this view (camera instance ID).
/// @param params Context creation parameters.
/// @return DLSS_Result_Success on success.
DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_CreateContext(
    uint32_t viewId,
    const DLSSContextCreateParams* params);

/// Destroy a DLSS context for a specific view.
/// @param viewId View identifier.
/// @return DLSS_Result_Success on success.
DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_DestroyContext(uint32_t viewId);

/// Destroy all DLSS contexts.
DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_DestroyAllContexts(void);

/// Check if a context exists for the given view.
/// @param viewId View identifier.
/// @return 1 if context exists, 0 otherwise.
DLSS_PLUGIN_API uint8_t DLSS_PLUGIN_CALL DLSS_HasContext(uint32_t viewId);

/// Update context parameters (may recreate context if resolution changes).
/// @param viewId View identifier.
/// @param params New parameters.
/// @return DLSS_Result_Success on success.
DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_UpdateContext(
    uint32_t viewId,
    const DLSSContextCreateParams* params);

//--- Execution ---

/// Execute DLSS for a specific view.
/// @param viewId View identifier (context must exist).
/// @param params Execution parameters including textures.
/// @return DLSS_Result_Success on success.
DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_Execute(
    uint32_t viewId,
    const DLSSExecuteParams* params);

/// Execute DLSS directly with a provided command list.
/// @param viewId View identifier.
/// @param commandList ID3D12GraphicsCommandList* (as void*).
/// @param params Execution parameters.
/// @return DLSS_Result_Success on success.
DLSS_PLUGIN_API DLSSResult DLSS_PLUGIN_CALL DLSS_ExecuteOnCommandList(
    uint32_t viewId,
    void* commandList,
    const DLSSExecuteParams* params);

//--- Unity Render Event Callback ---

/// Get the render event callback for Unity's CommandBuffer.
/// Event ID: 0x444C5353 ('DLSS')
/// @return Function pointer for UnityRenderingEvent callback.
DLSS_PLUGIN_API void* DLSS_PLUGIN_CALL DLSS_GetRenderEventFunc(void);

/// Set the current view ID for render event callback.
/// @param viewId View identifier.
DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_SetCurrentView(uint32_t viewId);

/// Set execution parameters for render event callback.
/// @param params Execution parameters (copied internally).
DLSS_PLUGIN_API void DLSS_PLUGIN_CALL DLSS_SetExecuteParams(const DLSSExecuteParams* params);

//--- Debug/Utility ---

/// Get the last NGX error code for debugging.
/// @return NGX result code from last operation.
DLSS_PLUGIN_API int32_t DLSS_PLUGIN_CALL DLSS_GetLastNGXError(void);

/// Get a human-readable error message for a result code.
/// @param result Result code.
/// @return Static string describing the error.
DLSS_PLUGIN_API const char* DLSS_PLUGIN_CALL DLSS_GetResultString(DLSSResult result);

#ifdef __cplusplus
}
#endif
