#pragma comment(lib, "dxgi")
#pragma comment(lib, "d3d12")

#include <atomic>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "Plugin.h"
#include "DLSSPluginLite.h"
#include "IUnityInterface.h"
#include "IUnityGraphics.h"
#include "IUnityGraphicsD3D12.h"
#include "IUnityLog.h"



//-------------------------------------------------------
//Global Unity interfaces
static IUnityInterfaces *g_unityInterfaces = nullptr;
static IUnityGraphics *g_unityGraphics = nullptr;
static std::atomic<UnityGfxRenderer> g_renderer{kUnityGfxRendererNull};
IUnityGraphicsD3D12v8 *g_unityGraphics_D3D12 = nullptr;  // Non-static for DLSS access
IUnityLog *g_unityLog = nullptr;  // Non-static for DLSS logging access



extern "C" {
// Forward declarations
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

static void ConfigureD3D12PluginEvent(int eventId) {
    if (!g_unityGraphics_D3D12) {
        return;
    }

    UnityD3D12PluginEventConfig config = {};
    config.graphicsQueueAccess = kUnityD3D12GraphicsQueueAccess_DontCare;
    config.flags =
        kUnityD3D12EventConfigFlag_FlushCommandBuffers |
        kUnityD3D12EventConfigFlag_ModifiesCommandBuffersState;
    g_unityGraphics_D3D12->ConfigureEvent(eventId, &config);
}

static void ConfigureD3D12PluginEvents() {
    ConfigureD3D12PluginEvent(DLSS_Event_CreateFeature);
    ConfigureD3D12PluginEvent(DLSS_Event_EvaluateSuperResolution);
    ConfigureD3D12PluginEvent(DLSS_Event_DestroyFeature);
    ConfigureD3D12PluginEvent(DLSS_Event_EvaluateRayReconstruction);
    ConfigureD3D12PluginEvent(DLSS_Event_CreateNeuralRendering);
    ConfigureD3D12PluginEvent(DLSS_Event_EvaluateNeuralRendering);
}

static void HandleDeviceEvent(UnityGfxDeviceEventType eventType) {
    switch (eventType) {
        case kUnityGfxDeviceEventInitialize:
            if (g_unityGraphics) {
                g_renderer = g_unityGraphics->GetRenderer();
            }
            if (g_renderer == kUnityGfxRendererD3D12) {
                if (!g_unityGraphics_D3D12 && g_unityInterfaces) {
                    g_unityGraphics_D3D12 =
                        g_unityInterfaces->Get<IUnityGraphicsD3D12v8>();
                }
                ConfigureD3D12PluginEvents();
            }
            // Note: DLSS_Init_with_ProjectID_D3D12() is called explicitly from C# after device init
            // to allow passing app-specific parameters (projectId, engineVersion, etc.)
            break;
        case kUnityGfxDeviceEventShutdown:
            // Note: DLSS_Shutdown_D3D12() is called explicitly from C# before device shutdown
            // The lite plugin is managed entirely from C# side
            g_renderer = kUnityGfxRendererNull;
            g_unityLog = nullptr;
            break;
        case kUnityGfxDeviceEventBeforeReset:
        case kUnityGfxDeviceEventAfterReset:
        default:
            break;
    }
}


//-------------------------------------------------------
//Unity interfaces

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType) {
    HandleDeviceEvent(eventType);
}

// Called by Unity to load the plugin and provide the interfaces pointer
UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces *unityInterfaces) {
    g_unityInterfaces = unityInterfaces;
    if (!g_unityInterfaces) {
        return;
    }

    g_unityGraphics = g_unityInterfaces->Get<IUnityGraphics>();
    g_unityLog = g_unityInterfaces->Get<IUnityLog>();
    if (!g_unityGraphics) {
        return;
    }

    g_unityGraphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

#if SUPPORT_VULKAN
    if (s_Graphics->GetRenderer() == kUnityGfxRendererNull) {
        extern void RenderAPI_Vulkan_OnPluginLoad(IUnityInterfaces *);
        RenderAPI_Vulkan_OnPluginLoad(unityInterfaces);
    }
#endif // SUPPORT_VULKAN

    // Initialize now (in case the graphics device is already initialized)
    OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

// Called by Unity when the plugin is unloaded
UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginUnload() {
    if (g_unityGraphics) {
        g_unityGraphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
    }

    g_unityGraphics_D3D12 = nullptr;
    g_unityGraphics = nullptr;
    g_unityLog = nullptr;
    g_unityInterfaces = nullptr;
    g_renderer = kUnityGfxRendererNull;
}
}
