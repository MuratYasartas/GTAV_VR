#include "DX11Hook.h"
#include "../core/Logger.h"
#include "../core/MemoryManager.h"
#include <MinHook.h>

namespace GTA5VR {

// Static member initialization
DX11Hook::PresentFn DX11Hook::s_originalPresent = nullptr;
DX11Hook::DrawFn DX11Hook::s_originalDraw = nullptr;
DX11Hook::DrawIndexedFn DX11Hook::s_originalDrawIndexed = nullptr;
DX11Hook::VSSetShaderFn DX11Hook::s_originalVSSetShader = nullptr;
DX11Hook::PSSetShaderFn DX11Hook::s_originalPSSetShader = nullptr;
DX11Hook::OMSetRenderTargetsFn DX11Hook::s_originalOMSetRenderTargets = nullptr;

DX11Hook& DX11Hook::GetInstance() {
    static DX11Hook instance;
    return instance;
}

bool DX11Hook::Initialize() {
    if (m_initialized) return true;

    LOG_INFO("Initializing DX11 hooks...");

    if (!FindD3D11Device()) {
        LOG_ERROR("Failed to find D3D11 device");
        return false;
    }

    if (!GetVTableAddresses()) {
        LOG_ERROR("Failed to get VTable addresses");
        return false;
    }

    m_initialized = true;
    LOG_INFO("DX11 hooks initialized");
    return true;
}

void DX11Hook::Shutdown() {
    RemoveAllHooks();
    m_initialized = false;
    LOG_INFO("DX11 hooks shutdown");
}

bool DX11Hook::InstallPresentHook() {
    if (!m_initialized || !m_swapChain) return false;

    void** vtable = *reinterpret_cast<void***>(m_swapChain);
    void* presentAddr = vtable[8]; // Present is index 8 in IDXGISwapChain vtable

    if (MH_CreateHook(presentAddr, &HookedPresent, reinterpret_cast<void**>(&s_originalPresent)) != MH_OK) {
        LOG_ERROR("Failed to create Present hook");
        return false;
    }

    if (MH_EnableHook(presentAddr) != MH_OK) {
        LOG_ERROR("Failed to enable Present hook");
        return false;
    }

    LOG_INFO("Present hook installed");
    return true;
}

bool DX11Hook::InstallDrawHooks() {
    if (!m_initialized || !m_context) return false;

    void** vtable = *reinterpret_cast<void***>(m_context);

    // Draw is index 13, DrawIndexed is index 12
    void* drawAddr = vtable[13];
    void* drawIndexedAddr = vtable[12];

    if (MH_CreateHook(drawAddr, &HookedDraw, reinterpret_cast<void**>(&s_originalDraw)) != MH_OK) {
        LOG_WARNING("Failed to create Draw hook");
    } else {
        MH_EnableHook(drawAddr);
    }

    if (MH_CreateHook(drawIndexedAddr, &HookedDrawIndexed, reinterpret_cast<void**>(&s_originalDrawIndexed)) != MH_OK) {
        LOG_WARNING("Failed to create DrawIndexed hook");
    } else {
        MH_EnableHook(drawIndexedAddr);
    }

    LOG_INFO("Draw hooks installed");
    return true;
}

bool DX11Hook::InstallShaderHooks() {
    if (!m_initialized || !m_context) return false;

    void** vtable = *reinterpret_cast<void***>(m_context);

    // VSSetShader is index 11, PSSetShader is index 9
    void* vsSetShaderAddr = vtable[11];
    void* psSetShaderAddr = vtable[9];

    if (MH_CreateHook(vsSetShaderAddr, &HookedVSSetShader, reinterpret_cast<void**>(&s_originalVSSetShader)) != MH_OK) {
        LOG_WARNING("Failed to create VSSetShader hook");
    } else {
        MH_EnableHook(vsSetShaderAddr);
    }

    if (MH_CreateHook(psSetShaderAddr, &HookedPSSetShader, reinterpret_cast<void**>(&s_originalPSSetShader)) != MH_OK) {
        LOG_WARNING("Failed to create PSSetShader hook");
    } else {
        MH_EnableHook(psSetShaderAddr);
    }

    LOG_INFO("Shader hooks installed");
    return true;
}

bool DX11Hook::InstallRenderTargetHooks() {
    if (!m_initialized || !m_context) return false;

    void** vtable = *reinterpret_cast<void***>(m_context);

    // OMSetRenderTargets is index 33
    void* omSetRTAddr = vtable[33];

    if (MH_CreateHook(omSetRTAddr, &HookedOMSetRenderTargets, reinterpret_cast<void**>(&s_originalOMSetRenderTargets)) != MH_OK) {
        LOG_WARNING("Failed to create OMSetRenderTargets hook");
    } else {
        MH_EnableHook(omSetRTAddr);
    }

    LOG_INFO("RenderTarget hooks installed");
    return true;
}

void DX11Hook::RemoveAllHooks() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
}

void DX11Hook::SetPresentCallback(PresentCallback callback) {
    m_presentCallback = callback;
}

void DX11Hook::SetPreDrawCallback(DrawCallback callback) {
    m_preDrawCallback = callback;
}

void DX11Hook::SetPostDrawCallback(DrawCallback callback) {
    m_postDrawCallback = callback;
}

void DX11Hook::SetShaderCallback(ShaderCallback callback) {
    m_shaderCallback = callback;
}

bool DX11Hook::FindD3D11Device() {
    // Create a temporary window and D3D11 device to get vtable addresses
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0, 0,
                     GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                     L"GTA5VR_D3D11Hook", nullptr };
    RegisterClassEx(&wc);

    HWND tempWindow = CreateWindow(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                    0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 2;
    scd.BufferDesc.Height = 2;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = tempWindow;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* tempDevice = nullptr;
    ID3D11DeviceContext* tempContext = nullptr;
    IDXGISwapChain* tempSwapChain = nullptr;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                nullptr, 0, D3D11_SDK_VERSION, &scd,
                                                &tempSwapChain, &tempDevice, &featureLevel, &tempContext);

    if (FAILED(hr)) {
        DestroyWindow(tempWindow);
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return false;
    }

    // Store vtable pointers
    m_swapChain = tempSwapChain;
    m_device = tempDevice;
    m_context = tempContext;

    // In a real implementation, we'd get the game's actual device/swapchain
    // For now, we keep references to find vtable addresses

    // Note: In production, you'd hook into the game's existing D3D11 device
    // This is a simplified version for demonstration

    DestroyWindow(tempWindow);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return true;
}

bool DX11Hook::GetVTableAddresses() {
    // VTable addresses are retrieved from the temporary device
    // In production, you'd scan for the game's device
    return m_swapChain != nullptr && m_device != nullptr && m_context != nullptr;
}

// Hooked functions
HRESULT WINAPI DX11Hook::HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    auto& hook = GetInstance();

    // Call VR callback before present
    if (hook.m_presentCallback) {
        hook.m_presentCallback(swapChain, syncInterval, flags);
    }

    return s_originalPresent(swapChain, syncInterval, flags);
}

void WINAPI DX11Hook::HookedDraw(ID3D11DeviceContext* context, UINT vertexCount, UINT startVertex) {
    auto& hook = GetInstance();

    if (hook.m_preDrawCallback) {
        hook.m_preDrawCallback(context, vertexCount, startVertex);
    }

    s_originalDraw(context, vertexCount, startVertex);

    if (hook.m_postDrawCallback) {
        hook.m_postDrawCallback(context, vertexCount, startVertex);
    }
}

void WINAPI DX11Hook::HookedDrawIndexed(ID3D11DeviceContext* context, UINT indexCount, UINT startIndex, INT baseVertex) {
    auto& hook = GetInstance();

    if (hook.m_preDrawCallback) {
        hook.m_preDrawCallback(context, indexCount, startIndex);
    }

    s_originalDrawIndexed(context, indexCount, startIndex, baseVertex);

    if (hook.m_postDrawCallback) {
        hook.m_postDrawCallback(context, indexCount, startIndex);
    }
}

void WINAPI DX11Hook::HookedVSSetShader(ID3D11DeviceContext* context, ID3D11VertexShader* shader,
                                         ID3D11ClassInstance* const* instances, UINT numInstances) {
    auto& hook = GetInstance();

    if (hook.m_shaderCallback) {
        hook.m_shaderCallback(context, shader);
    }

    s_originalVSSetShader(context, shader, instances, numInstances);
}

void WINAPI DX11Hook::HookedPSSetShader(ID3D11DeviceContext* context, ID3D11PixelShader* shader,
                                         ID3D11ClassInstance* const* instances, UINT numInstances) {
    s_originalPSSetShader(context, shader, instances, numInstances);
}

void WINAPI DX11Hook::HookedOMSetRenderTargets(ID3D11DeviceContext* context, UINT numViews,
                                                ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv) {
    s_originalOMSetRenderTargets(context, numViews, rtvs, dsv);
}

} // namespace GTA5VR
