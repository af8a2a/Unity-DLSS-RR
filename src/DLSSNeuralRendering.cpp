//------------------------------------------------------------------------------
// DLSSNeuralRendering.cpp - optional DLSS 5 Neural Rendering runtime bridge
//------------------------------------------------------------------------------

#include "DLSSNeuralRendering.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#include <Windows.h>
#include <d3d12.h>

#include <nvsdk_ngx.h>

#include "DLSSPluginLite.h"
#include "IUnityLog.h"

extern IUnityLog* g_unityLog;

namespace
{
constexpr unsigned long long kApplicationId = 0x0876232Cull;
constexpr NVSDK_NGX_Version kSnippetSdkVersion =
    static_cast<NVSDK_NGX_Version>(0x15);
constexpr int32_t kRuntimeUnavailable =
    static_cast<int32_t>(NVSDK_NGX_Result_FAIL_FeatureNotSupported);

using InitFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    unsigned long long,
    const wchar_t*,
    ID3D12Device*,
    NVSDK_NGX_Version,
    const NVSDK_NGX_Parameter*);
using ShutdownFn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
using CreateFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*,
    NVSDK_NGX_Feature,
    const NVSDK_NGX_Parameter*,
    NVSDK_NGX_Handle**);
using EvaluateFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*,
    const NVSDK_NGX_Handle*,
    const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);
using ReleaseFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using GetModuleFileNameWFn = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);

struct RuntimeState
{
    std::mutex mutex;
    HMODULE module = nullptr;
    ID3D12Device* device = nullptr;
    InitFn init = nullptr;
    ShutdownFn shutdown = nullptr;
    CreateFn create = nullptr;
    EvaluateFn evaluate = nullptr;
    ReleaseFn release = nullptr;
    GetModuleFileNameWFn originalGetModuleFileNameW = nullptr;
    void** getModuleFileNameWIat = nullptr;
    int32_t initResult = kRuntimeUnavailable;
    bool initialized = false;
};

RuntimeState g_runtime;
GetModuleFileNameWFn g_originalGetModuleFileNameW = nullptr;
HMODULE g_snippetModule = nullptr;
std::atomic<int32_t> g_lastCreateResult{kRuntimeUnavailable};
std::atomic<int32_t> g_lastEvaluateResult{kRuntimeUnavailable};

void LogInfo(const std::string& message)
{
    if (g_unityLog)
        UNITY_LOG(g_unityLog, message.c_str());
}

void LogWarning(const std::string& message)
{
    if (g_unityLog)
        UNITY_LOG_WARNING(g_unityLog, message.c_str());
}

std::string ResultMessage(const char* operation, int32_t result)
{
    char buffer[192] = {};
    sprintf_s(
        buffer,
        "[DLSS-NR] %s failed with result 0x%08X",
        operation,
        static_cast<unsigned int>(result));
    return buffer;
}

std::wstring GetPluginDirectory()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&g_runtime),
            &module))
    {
        return {};
    }

    std::array<wchar_t, 32768> path = {};
    const DWORD length =
        GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};

    std::wstring result(path.data(), length);
    const size_t separator = result.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : result.substr(0, separator);
}

std::wstring GetApplicationDataDirectory()
{
    std::array<wchar_t, MAX_PATH> tempPath = {};
    const DWORD length =
        GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());
    if (length == 0 || length >= tempPath.size())
        return L".";

    std::wstring path(tempPath.data(), length);
    path += L"UnityDLSS-DLSSNR";
    CreateDirectoryW(path.c_str(), nullptr);
    return path;
}

bool WriteIatSlot(void** slot, void* value)
{
    if (!slot)
        return false;

    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection))
        return false;

    InterlockedExchangePointer(slot, value);

    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return true;
}

// The signed standalone snippet validates that its caller belongs to the NGX
// core module. Redirect only its imported GetModuleFileNameW call so it sees
// _nvngx.dll/nvngx.dll, while leaving the rest of the process untouched.
DWORD WINAPI HookedGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size)
{
    if (!g_originalGetModuleFileNameW)
        return 0;

    if (g_snippetModule && module == g_snippetModule)
        return g_originalGetModuleFileNameW(module, filename, size);

    HMODULE ngxModule = GetModuleHandleW(L"_nvngx.dll");
    if (!ngxModule)
        ngxModule = GetModuleHandleW(L"nvngx.dll");
    if (ngxModule)
        return g_originalGetModuleFileNameW(ngxModule, filename, size);

    if (!filename || size == 0)
        return 0;

    const wchar_t fallbackName[] = L"nvngx.dll";
    constexpr DWORD fallbackLength =
        static_cast<DWORD>(std::size(fallbackName) - 1);
    if (size <= fallbackLength)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }

    std::memcpy(filename, fallbackName, sizeof(fallbackName));
    return fallbackLength;
}

bool HookSnippetCallerCheck()
{
    if (!g_runtime.module)
        return false;

    auto* base = reinterpret_cast<BYTE*>(g_runtime.module);
    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew <= 0)
        return false;

    auto* ntHeaders =
        reinterpret_cast<IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const IMAGE_DATA_DIRECTORY& importDirectory =
        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!importDirectory.VirtualAddress)
        return false;

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    const FARPROC expectedAddress =
        kernel32 ? GetProcAddress(kernel32, "GetModuleFileNameW") : nullptr;

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        base + importDirectory.VirtualAddress);
    for (; descriptor->Name; ++descriptor)
    {
        const char* libraryName =
            reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(libraryName, "KERNEL32.dll") != 0 &&
            _stricmp(libraryName, "KERNELBASE.dll") != 0)
        {
            continue;
        }

        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + descriptor->FirstThunk);
        auto* names = descriptor->OriginalFirstThunk
            ? reinterpret_cast<IMAGE_THUNK_DATA*>(
                  base + descriptor->OriginalFirstThunk)
            : nullptr;

        for (size_t index = 0; slots[index].u1.Function; ++index)
        {
            bool matches = false;
            if (names && names[index].u1.AddressOfData &&
                !IMAGE_SNAP_BY_ORDINAL(names[index].u1.Ordinal))
            {
                auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                    base + names[index].u1.AddressOfData);
                matches = std::strcmp(
                              reinterpret_cast<const char*>(import->Name),
                              "GetModuleFileNameW") == 0;
            }
            else if (expectedAddress &&
                     slots[index].u1.Function ==
                         reinterpret_cast<ULONG_PTR>(expectedAddress))
            {
                matches = true;
            }

            if (!matches)
                continue;

            void** slot = reinterpret_cast<void**>(&slots[index].u1.Function);
            g_runtime.originalGetModuleFileNameW =
                reinterpret_cast<GetModuleFileNameWFn>(slots[index].u1.Function);
            g_originalGetModuleFileNameW = g_runtime.originalGetModuleFileNameW;
            g_snippetModule = g_runtime.module;
            if (!WriteIatSlot(
                    slot,
                    reinterpret_cast<void*>(&HookedGetModuleFileNameW)))
            {
                g_runtime.originalGetModuleFileNameW = nullptr;
                g_originalGetModuleFileNameW = nullptr;
                g_snippetModule = nullptr;
                return false;
            }

            g_runtime.getModuleFileNameWIat = slot;
            return true;
        }
    }

    return false;
}

void UnhookSnippetCallerCheck()
{
    if (g_runtime.getModuleFileNameWIat &&
        g_runtime.originalGetModuleFileNameW)
    {
        WriteIatSlot(
            g_runtime.getModuleFileNameWIat,
            reinterpret_cast<void*>(g_runtime.originalGetModuleFileNameW));
    }

    g_runtime.getModuleFileNameWIat = nullptr;
    g_runtime.originalGetModuleFileNameW = nullptr;
    g_originalGetModuleFileNameW = nullptr;
    g_snippetModule = nullptr;
}

template <typename T>
bool ResolveExport(T& target, const char* name)
{
    target = reinterpret_cast<T>(GetProcAddress(g_runtime.module, name));
    if (target)
        return true;

    LogWarning(std::string("[DLSS-NR] Required runtime export is missing: ") + name);
    return false;
}

void ClearExports()
{
    g_runtime.init = nullptr;
    g_runtime.shutdown = nullptr;
    g_runtime.create = nullptr;
    g_runtime.evaluate = nullptr;
    g_runtime.release = nullptr;
}

void UnloadRuntime()
{
    UnhookSnippetCallerCheck();
    ClearExports();
    if (g_runtime.module)
        FreeLibrary(g_runtime.module);
    g_runtime.module = nullptr;
}

void** ParameterVtable(NVSDK_NGX_Parameter* parameters)
{
    return parameters ? *reinterpret_cast<void***>(parameters) : nullptr;
}

void SetUll(NVSDK_NGX_Parameter* parameters, const char* name, unsigned long long value)
{
    using Fn = void(NVSDK_CONV*)(
        NVSDK_NGX_Parameter*, const char*, unsigned long long);
    reinterpret_cast<Fn>(ParameterVtable(parameters)[0])(parameters, name, value);
}

void SetResource(
    NVSDK_NGX_Parameter* parameters,
    const char* name,
    ID3D12Resource* value)
{
    using Fn = void(NVSDK_CONV*)(
        NVSDK_NGX_Parameter*, const char*, ID3D12Resource*);
    reinterpret_cast<Fn>(ParameterVtable(parameters)[1])(parameters, name, value);
}

void SetUi(NVSDK_NGX_Parameter* parameters, const char* name, unsigned int value)
{
    using Fn = void(NVSDK_CONV*)(
        NVSDK_NGX_Parameter*, const char*, unsigned int);
    reinterpret_cast<Fn>(ParameterVtable(parameters)[3])(parameters, name, value);
}

void SetI(NVSDK_NGX_Parameter* parameters, const char* name, int value)
{
    using Fn = void(NVSDK_CONV*)(NVSDK_NGX_Parameter*, const char*, int);
    reinterpret_cast<Fn>(ParameterVtable(parameters)[4])(parameters, name, value);
}

void SetF(NVSDK_NGX_Parameter* parameters, const char* name, float value)
{
    using Fn = void(NVSDK_CONV*)(NVSDK_NGX_Parameter*, const char*, float);
    reinterpret_cast<Fn>(ParameterVtable(parameters)[6])(parameters, name, value);
}

NVSDK_NGX_Result GetUi(
    NVSDK_NGX_Parameter* parameters,
    const char* name,
    unsigned int* value)
{
    using Fn = NVSDK_NGX_Result(NVSDK_CONV*)(
        NVSDK_NGX_Parameter*, const char*, unsigned int*);
    return reinterpret_cast<Fn>(ParameterVtable(parameters)[11])(
        parameters, name, value);
}

NVSDK_NGX_Result NVSDK_CONV ComputeScalingRatio(
    NVSDK_NGX_Parameter* parameters)
{
    if (!parameters)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    unsigned int upscaling = 0;
    GetUi(parameters, "DLSSNR.Upscaling", &upscaling);
    SetF(parameters, "DLSSNR.ScalingRatio", upscaling ? 0.5f : 1.0f);
    return NVSDK_NGX_Result_Success;
}

void SetCreateParameters(
    NVSDK_NGX_Parameter* parameters,
    const DLSSNeuralRenderingCreateParams& createParams)
{
    const int inputWidth = static_cast<int>(createParams.inputWidth);
    const int inputHeight = static_cast<int>(createParams.inputHeight);
    const int outputWidth = static_cast<int>(createParams.outputWidth);
    const int outputHeight = static_cast<int>(createParams.outputHeight);

    SetI(parameters, "Width", inputWidth);
    SetI(parameters, "Height", inputHeight);
    SetI(parameters, "OutWidth", outputWidth);
    SetI(parameters, "OutHeight", outputHeight);
    SetI(parameters, "DLSSNR.Width", outputWidth);
    SetI(parameters, "DLSSNR.Height", outputHeight);
    SetI(parameters, "DLSSNR.InputWidth", inputWidth);
    SetI(parameters, "DLSSNR.InputHeight", inputHeight);
    SetI(parameters, "DLSSNR.OutputWidth", outputWidth);
    SetI(parameters, "DLSSNR.OutputHeight", outputHeight);
    SetI(parameters, "DLSSNR.Output.Width", outputWidth);
    SetI(parameters, "DLSSNR.Output.Height", outputHeight);
    SetI(parameters, "DLSSNR.Hint.Render.Preset", createParams.preset);
    SetI(parameters, "CreationNodeMask", 1);
    SetI(parameters, "VisibilityNodeMask", 1);
    SetUi(parameters, "DLSS.Output.Subrect.Base.X", 0);
    SetUi(parameters, "DLSS.Output.Subrect.Base.Y", 0);
    SetUi(parameters, "DLSSNR.Upscaling", createParams.upscaling != 0);
    SetF(parameters, "DLSSNR.Scale", createParams.upscaling ? 0.5f : 1.0f);
    SetF(
        parameters,
        "DLSSNR.ScalingRatio",
        createParams.upscaling ? 0.5f : 1.0f);
    SetUll(
        parameters,
        "DLSSNRComputeScalingRatioCallback",
        reinterpret_cast<unsigned long long>(&ComputeScalingRatio));
}

void SetSubrect(
    NVSDK_NGX_Parameter* parameters,
    const char* resourceName,
    uint32_t width,
    uint32_t height)
{
    const std::string prefix = std::string("DLSSNR.") + resourceName + "Subrect";
    SetI(parameters, (prefix + "BaseX").c_str(), 0);
    SetI(parameters, (prefix + "BaseY").c_str(), 0);
    SetI(parameters, (prefix + "Width").c_str(), static_cast<int>(width));
    SetI(parameters, (prefix + "Height").c_str(), static_cast<int>(height));
}
} // namespace

bool DLSSNR_InitializeRuntime(ID3D12Device* device)
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.initialized)
        return g_runtime.device == device;
    if (!device)
        return false;

    const std::wstring pluginDirectory = GetPluginDirectory();
    const std::wstring dllPath = pluginDirectory.empty()
        ? L"nvngx_dlssnr.dll"
        : pluginDirectory + L"\\nvngx_dlssnr.dll";
    g_runtime.module = LoadLibraryExW(
        dllPath.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!g_runtime.module)
    {
        g_runtime.initResult = kRuntimeUnavailable;
        const DWORD error = GetLastError();
        char buffer[256] = {};
        sprintf_s(
            buffer,
            "[DLSS-NR] Optional nvngx_dlssnr.dll was not loaded (Win32=%lu).",
            error);
        LogWarning(buffer);
        return false;
    }

    const bool exportsAvailable =
        ResolveExport(g_runtime.init, "NVSDK_NGX_D3D12_Init_Ext") &
        ResolveExport(g_runtime.shutdown, "NVSDK_NGX_D3D12_Shutdown1") &
        ResolveExport(g_runtime.create, "NVSDK_NGX_D3D12_CreateFeature") &
        ResolveExport(g_runtime.evaluate, "NVSDK_NGX_D3D12_EvaluateFeature") &
        ResolveExport(g_runtime.release, "NVSDK_NGX_D3D12_ReleaseFeature");
    if (!exportsAvailable)
    {
        UnloadRuntime();
        g_runtime.initResult = kRuntimeUnavailable;
        return false;
    }

    if (!HookSnippetCallerCheck())
    {
        LogWarning("[DLSS-NR] Failed to install the signed snippet caller-path bridge.");
        UnloadRuntime();
        g_runtime.initResult = kRuntimeUnavailable;
        return false;
    }

    const std::wstring applicationDataPath = GetApplicationDataDirectory();
    const NVSDK_NGX_Result result = g_runtime.init(
        kApplicationId,
        applicationDataPath.c_str(),
        device,
        kSnippetSdkVersion,
        nullptr);
    g_runtime.initResult = static_cast<int32_t>(result);
    if (NVSDK_NGX_FAILED(result))
    {
        LogWarning(ResultMessage("runtime initialization", g_runtime.initResult));
        UnloadRuntime();
        return false;
    }

    g_runtime.device = device;
    g_runtime.initialized = true;
    LogInfo("[DLSS-NR] DLSS 5 Neural Rendering runtime initialized (feature 18).");
    return true;
}

void DLSSNR_ShutdownRuntime()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.initialized && g_runtime.shutdown)
    {
        const NVSDK_NGX_Result result = g_runtime.shutdown(g_runtime.device);
        if (NVSDK_NGX_FAILED(result))
        {
            LogWarning(ResultMessage(
                "runtime shutdown", static_cast<int32_t>(result)));
        }
    }

    g_runtime.device = nullptr;
    g_runtime.initialized = false;
    UnloadRuntime();
}

bool DLSSNR_IsRuntimeAvailable()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    return g_runtime.initialized;
}

int32_t DLSSNR_GetRuntimeInitResult()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    return g_runtime.initResult;
}

int32_t DLSSNR_GetLastCreateResult()
{
    return g_lastCreateResult.load(std::memory_order_relaxed);
}

int32_t DLSSNR_GetLastEvaluateResult()
{
    return g_lastEvaluateResult.load(std::memory_order_relaxed);
}

int32_t DLSSNR_CreateFeature(
    ID3D12GraphicsCommandList* commandList,
    NVSDK_NGX_Parameter* parameters,
    const DLSSNeuralRenderingCreateParams& createParams,
    NVSDK_NGX_Handle** handle)
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.initialized || !g_runtime.create || !commandList ||
        !parameters || !handle)
    {
        return kRuntimeUnavailable;
    }

    SetCreateParameters(parameters, createParams);
    const NVSDK_NGX_Result result = g_runtime.create(
        commandList,
        static_cast<NVSDK_NGX_Feature>(DLSS_NGX_Feature_NeuralRendering),
        parameters,
        handle);
    const int32_t nativeResult = static_cast<int32_t>(result);
    g_lastCreateResult.store(nativeResult, std::memory_order_relaxed);
    return nativeResult;
}

int32_t DLSSNR_EvaluateFeature(
    ID3D12GraphicsCommandList* commandList,
    const NVSDK_NGX_Handle* handle,
    NVSDK_NGX_Parameter* parameters,
    const DLSSNeuralRenderingEvaluateParams& evaluateParams)
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.initialized || !g_runtime.evaluate || !commandList ||
        !handle || !parameters)
    {
        return kRuntimeUnavailable;
    }

    SetResource(
        parameters,
        "DLSSNR.Color",
        static_cast<ID3D12Resource*>(evaluateParams.color));
    SetResource(
        parameters,
        "DLSSNR.Output",
        static_cast<ID3D12Resource*>(evaluateParams.output));
    SetResource(
        parameters,
        "DLSSNR.MVec",
        static_cast<ID3D12Resource*>(evaluateParams.motionVectors));
    SetResource(
        parameters,
        "DLSSNR.Depth",
        static_cast<ID3D12Resource*>(evaluateParams.depth));

    SetI(parameters, "Width", static_cast<int>(evaluateParams.inputWidth));
    SetI(parameters, "Height", static_cast<int>(evaluateParams.inputHeight));
    SetI(parameters, "OutWidth", static_cast<int>(evaluateParams.outputWidth));
    SetI(parameters, "OutHeight", static_cast<int>(evaluateParams.outputHeight));
    SetSubrect(
        parameters,
        "Color",
        evaluateParams.inputWidth,
        evaluateParams.inputHeight);
    SetSubrect(
        parameters,
        "Depth",
        evaluateParams.inputWidth,
        evaluateParams.inputHeight);
    SetSubrect(
        parameters,
        "MVec",
        evaluateParams.inputWidth,
        evaluateParams.inputHeight);
    SetI(parameters, "DLSSNR.OutputSubrectBaseX", 0);
    SetI(parameters, "DLSSNR.OutputSubrectBaseY", 0);
    SetI(
        parameters,
        "DLSSNR.OutputSubrectWidth",
        static_cast<int>(evaluateParams.outputWidth));
    SetI(
        parameters,
        "DLSSNR.OutputSubrectHeight",
        static_cast<int>(evaluateParams.outputHeight));
    SetF(parameters, "DLSSNR.MVecScaleX", evaluateParams.motionVectorScaleX);
    SetF(parameters, "DLSSNR.MVecScaleY", evaluateParams.motionVectorScaleY);
    SetUi(parameters, "DLSSNR.DepthInverted", evaluateParams.depthInverted != 0);
    SetUi(parameters, "DLSSNR.Enabled", 1);
    SetUi(parameters, "DLSSNR.Reset", evaluateParams.reset != 0);
    SetF(parameters, "DLSSNR.Intensity", evaluateParams.intensity);
    SetF(parameters, "DLSSNR.LocalToneStrength", evaluateParams.localToneStrength);
    SetF(
        parameters,
        "DLSSNR.LocalStructureStrength",
        evaluateParams.localStructureStrength);
    SetF(
        parameters,
        "DLSSNR.SkinStructureStrength",
        evaluateParams.skinStructureStrength);
    SetUi(parameters, "DLSSNR.UseAutoMask", evaluateParams.useAutoMask != 0);
    SetI(parameters, "DLSSNR.Style", evaluateParams.style);
    SetUi(parameters, "DLSSNR.UICorrection", evaluateParams.uiCorrection != 0);

    const NVSDK_NGX_Result result = g_runtime.evaluate(
        commandList,
        handle,
        parameters,
        nullptr);
    const int32_t nativeResult = static_cast<int32_t>(result);
    g_lastEvaluateResult.store(nativeResult, std::memory_order_relaxed);
    return nativeResult;
}

int32_t DLSSNR_ReleaseFeature(NVSDK_NGX_Handle* handle)
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.initialized || !g_runtime.release || !handle)
        return kRuntimeUnavailable;

    return static_cast<int32_t>(g_runtime.release(handle));
}
