// //------------------------------------------------------------------------------
// // DLSSContext.h - Internal DLSS Context Management
// //------------------------------------------------------------------------------
// // C++ implementation details for managing NGX DLSS contexts.
// // Not exposed to C# - internal use only.
// //------------------------------------------------------------------------------
//
// #pragma once
//
//
// #include <d3d12.h>
// #include <wrl/client.h>
// #include <unordered_map>
// #include <mutex>
// #include <memory>
// #include <atomic>
// #include <string>
// #include <cstdarg>
//
// #include "DLSSPlugin.h"
// #include "IUnityLog.h"
// // Forward declarations for NGX types
// struct NVSDK_NGX_Handle;
// struct NVSDK_NGX_Parameter;
// // NVSDK_NGX_Result is defined in nvsdk_ngx_defs.h - use int in forward decls
//
// // External Unity log interface (defined in Plugin.cpp)
// extern IUnityLog* g_unityLog;
//
// namespace dlss
// {
//
// // Forward declaration
// class DLSSContextManager;
//
// //------------------------------------------------------------------------------
// // DLSSLogger - Logging system with Unity Log and callback support
// //------------------------------------------------------------------------------
// class DLSSLogger
// {
// public:
//     static DLSSLogger& Instance();
//
//     // Set callback for log messages (optional, overrides Unity log if set)
//     void SetCallback(DLSSLogCallback callback);
//
//     // Set minimum log level
//     void SetLogLevel(DLSSLogLevel level);
//     DLSSLogLevel GetLogLevel() const;
//
//     // Logging methods
//     void Debug(const char* format, ...);
//     void Info(const char* format, ...);
//     void Warning(const char* format, ...);
//     void Error(const char* format, ...);
//
//     // Generic log with level
//     void Log(DLSSLogLevel level, const char* format, ...);
//     void LogV(DLSSLogLevel level, const char* format, va_list args);
//
// private:
//     DLSSLogger() = default;
//
//     // Output to Unity log interface
//     void LogToUnity(DLSSLogLevel level, const char* message);
//
//     DLSSLogCallback m_callback = nullptr;
//     std::atomic<DLSSLogLevel> m_logLevel{DLSS_Log_Info};
//     std::mutex m_mutex;
//     char m_buffer[2048];
// };
//
// // Convenience macros for logging
// #define DLSS_LOG_DEBUG(fmt, ...) dlss::DLSSLogger::Instance().Debug(fmt, ##__VA_ARGS__)
// #define DLSS_LOG_INFO(fmt, ...)  dlss::DLSSLogger::Instance().Info(fmt, ##__VA_ARGS__)
// #define DLSS_LOG_WARN(fmt, ...)  dlss::DLSSLogger::Instance().Warning(fmt, ##__VA_ARGS__)
// #define DLSS_LOG_ERROR(fmt, ...) dlss::DLSSLogger::Instance().Error(fmt, ##__VA_ARGS__)
//
// //------------------------------------------------------------------------------
// // DLSSContext - Wrapper for a single NGX DLSS feature handle
// //------------------------------------------------------------------------------
// class DLSSContext
// {
// public:
//     DLSSContext() = default;
//     ~DLSSContext();
//
//     // Non-copyable, movable
//     DLSSContext(const DLSSContext&) = delete;
//     DLSSContext& operator=(const DLSSContext&) = delete;
//     DLSSContext(DLSSContext&& other) noexcept;
//     DLSSContext& operator=(DLSSContext&& other) noexcept;
//
//     /// Create the NGX feature with given parameters
//     DLSSResult Create(
//         ID3D12Device* device,
//         ID3D12GraphicsCommandList* cmdList,
//         const DLSSContextCreateParams& params);
//
//     /// Destroy the NGX feature and release resources
//     void Destroy();
//
//     /// Execute DLSS with the given parameters
//     DLSSResult Execute(
//         ID3D12GraphicsCommandList* cmdList,
//         const DLSSExecuteParams& params);
//
//     /// Check if context is valid and created
//     bool IsValid() const { return m_handle != nullptr; }
//
//     /// Get the creation parameters
//     const DLSSContextCreateParams& GetParams() const { return m_params; }
//
//     /// Check if context needs recreation (params changed significantly)
//     bool NeedsRecreation(const DLSSContextCreateParams& newParams) const;
//
// private:
//     // Setup and execute DLSS-SR (returns NGX result code)
//     int SetupAndExecuteSR(
//         ID3D12GraphicsCommandList* cmdList,
//         NVSDK_NGX_Parameter* ngxParams,
//         const DLSSExecuteParams& params);
//
//     // Setup and execute DLSS-RR (returns NGX result code)
//     int SetupAndExecuteRR(
//         ID3D12GraphicsCommandList* cmdList,
//         NVSDK_NGX_Parameter* ngxParams,
//         const DLSSExecuteParams& params);
//
//     NVSDK_NGX_Handle* m_handle = nullptr;
//     NVSDK_NGX_Parameter* m_params_ngx = nullptr;  // Store NGX params reference
//     DLSSContextCreateParams m_params = {};
// };
//
// //------------------------------------------------------------------------------
// // DLSSContextManager - Manages all DLSS contexts by view ID
// //------------------------------------------------------------------------------
// class DLSSContextManager
// {
// public:
//     static DLSSContextManager& Instance();
//
//     // Non-copyable
//     DLSSContextManager(const DLSSContextManager&) = delete;
//     DLSSContextManager& operator=(const DLSSContextManager&) = delete;
//
//     /// Initialize NGX and the context manager
//     DLSSResult Initialize(
//         ID3D12Device* device,
//         uint64_t appId,
//         const char* projectId,
//         const char* engineVersion,
//         const wchar_t* logPath);
//
//     /// Shutdown and release all resources
//     void Shutdown();
//
//     /// Check if initialized
//     bool IsInitialized() const { return m_initialized.load(); }
//
//     /// Get D3D12 device
//     ID3D12Device* GetDevice() const { return m_device.Get(); }
//
//     /// Get NGX parameters for queries
//     NVSDK_NGX_Parameter* GetNGXParams() const { return m_ngxParams; }
//
//     /// Query DLSS capabilities
//     DLSSResult GetCapabilities(DLSSCapabilityInfo* outInfo);
//
//     /// Query optimal settings
//     DLSSResult GetOptimalSettings(
//         DLSSMode mode,
//         DLSSQuality quality,
//         uint32_t outputWidth,
//         uint32_t outputHeight,
//         DLSSOptimalSettings* outSettings);
//
//     /// Query stats
//     DLSSResult GetStats(DLSSMode mode, DLSSStats* outStats);
//
//     /// Create a context for a view
//     DLSSResult CreateContext(uint32_t viewId, const DLSSContextCreateParams& params);
//
//     /// Destroy a context
//     DLSSResult DestroyContext(uint32_t viewId);
//
//     /// Destroy all contexts
//     void DestroyAllContexts();
//
//     /// Check if context exists
//     bool HasContext(uint32_t viewId) const;
//
//     /// Update a context (may recreate)
//     DLSSResult UpdateContext(uint32_t viewId, const DLSSContextCreateParams& params);
//
//     /// Execute DLSS for a view
//     DLSSResult Execute(uint32_t viewId, ID3D12GraphicsCommandList* cmdList, const DLSSExecuteParams& params);
//
//     /// Get/set current view for render event
//     void SetCurrentView(uint32_t viewId) { m_currentViewId = viewId; }
//     uint32_t GetCurrentView() const { return m_currentViewId; }
//
//     /// Get/set execute params for render event
//     void SetExecuteParams(const DLSSExecuteParams& params);
//     const DLSSExecuteParams& GetExecuteParams() const { return m_executeParams; }
//
//     /// Get last NGX error
//     int32_t GetLastNGXError() const { return m_lastNGXError; }
//     void SetLastNGXError(int32_t error) { m_lastNGXError = error; }
//
//     /// Convert NGX result to DLSSResult (static for use without instance)
//     static DLSSResult TranslateNGXResult(int ngxResult);
//
// private:
//     DLSSContextManager() = default;
//     ~DLSSContextManager();
//
//     // Initialize NGX SDK
//     DLSSResult InitializeNGX(
//         uint64_t appId,
//         const char* projectId,
//         const char* engineVersion,
//         const wchar_t* logPath);
//
//     Microsoft::WRL::ComPtr<ID3D12Device> m_device;
//     NVSDK_NGX_Parameter* m_ngxParams = nullptr;
//
//     std::unordered_map<uint32_t, std::unique_ptr<DLSSContext>> m_contexts;
//     mutable std::mutex m_contextMutex;
//
//     std::atomic<bool> m_initialized{false};
//     std::atomic<uint32_t> m_currentViewId{0};
//     DLSSExecuteParams m_executeParams = {};
//     std::mutex m_executeParamsMutex;
//
//     std::atomic<int32_t> m_lastNGXError{0};
//
//     // Cached capabilities
//     bool m_dlssSRAvailable = false;
//     bool m_dlssRRAvailable = false;
// };
//
// //------------------------------------------------------------------------------
// // Helper functions
// //------------------------------------------------------------------------------
//
// /// Convert DLSSQuality to NGX PerfQuality value
// int ToNGXPerfQuality(DLSSQuality quality);
//
// /// Convert DLSSSRPreset to NGX DLSS render preset
// int ToNGXSRPreset(DLSSSRPreset preset);
//
// /// Convert DLSSRRPreset to NGX RR render preset
// int ToNGXRRPreset(DLSSRRPreset preset);
//
// /// Convert DLSSFeatureFlags to NGX feature flags
// int ToNGXFeatureFlags(uint32_t flags);
//
// /// Get result string
// const char* GetResultString(DLSSResult result);
//
// } // namespace dlss
