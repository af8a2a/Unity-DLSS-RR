#include "Plugin.h"
#include "DLSSPlugin.h"
#pragma comment(lib, "dxgi")
#pragma comment(lib, "d3d12")

#include <atomic>
#include <d3d12.h>

#include <dxgi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "IUnityInterface.h"
#include "IUnityGraphics.h"
#include "IUnityGraphicsD3D12.h"



//-------------------------------------------------------
//Global Unity interfaces
static IUnityInterfaces *g_unityInterfaces = nullptr;
static IUnityGraphics *g_unityGraphics = nullptr;
static std::atomic<UnityGfxRenderer> g_renderer{kUnityGfxRendererNull};
IUnityGraphicsD3D12v8 *g_unityGraphics_D3D12 = nullptr;  // Non-static for DLSS access


extern "C" {
// Forward declarations
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

static void HandleDeviceEvent(UnityGfxDeviceEventType eventType) {
    switch (eventType) {
        case kUnityGfxDeviceEventInitialize:
            if (g_unityGraphics) {
                g_renderer = g_unityGraphics->GetRenderer();
            }
            // Note: DLSS_Initialize() is called explicitly from C# after device init
            // to allow passing app-specific parameters (appId, projectId, etc.)
            break;
        case kUnityGfxDeviceEventShutdown:
            // Shutdown DLSS before device is destroyed
            DLSS_Shutdown();
            g_renderer = kUnityGfxRendererNull;
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
    g_unityGraphics = g_unityInterfaces->Get<IUnityGraphics>();
    g_unityGraphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);
    g_unityGraphics_D3D12 = g_unityInterfaces->Get<IUnityGraphicsD3D12v8>();


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
    g_unityGraphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}
}
