#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <functional>

namespace GTA5VR {

class DX11Hook {
public:
    static DX11Hook& GetInstance();

    // Lifecycle
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Install specific hooks
    bool InstallPresentHook();
    bool InstallDrawHooks();
    bool InstallShaderHooks();
    bool InstallRenderTargetHooks();

    // Remove hooks
    void RemoveAllHooks();

    // Callbacks for VR integration
    using PresentCallback = std::function<void(IDXGISwapChain*, UINT, UINT)>;
    using DrawCallback = std::function<void(ID3D11DeviceContext*, UINT, UINT)>;
    using ShaderCallback = std::function<void(ID3D11DeviceContext*, ID3D11VertexShader*)>;

    void SetPresentCallback(PresentCallback callback);
    void SetPreDrawCallback(DrawCallback callback);
    void SetPostDrawCallback(DrawCallback callback);
    void SetShaderCallback(ShaderCallback callback);

    // Get D3D resources
    ID3D11Device* GetDevice() const { return m_device; }
    ID3D11DeviceContext* GetContext() const { return m_context; }
    IDXGISwapChain* GetSwapChain() const { return m_swapChain; }
    HWND GetGameWindow() const { return m_gameWindow; }

private:
    DX11Hook() = default;
    ~DX11Hook() = default;
    DX11Hook(const DX11Hook&) = delete;
    DX11Hook& operator=(const DX11Hook&) = delete;

    bool FindD3D11Device();
    bool GetVTableAddresses();

    // Hooked function implementations
    static HRESULT WINAPI HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
    static void WINAPI HookedDraw(ID3D11DeviceContext* context, UINT vertexCount, UINT startVertex);
    static void WINAPI HookedDrawIndexed(ID3D11DeviceContext* context, UINT indexCount, UINT startIndex, INT baseVertex);
    static void WINAPI HookedVSSetShader(ID3D11DeviceContext* context, ID3D11VertexShader* shader, ID3D11ClassInstance* const* instances, UINT numInstances);
    static void WINAPI HookedPSSetShader(ID3D11DeviceContext* context, ID3D11PixelShader* shader, ID3D11ClassInstance* const* instances, UINT numInstances);
    static void WINAPI HookedOMSetRenderTargets(ID3D11DeviceContext* context, UINT numViews, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv);

    // Original function pointers
    using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
    using DrawFn = void(WINAPI*)(ID3D11DeviceContext*, UINT, UINT);
    using DrawIndexedFn = void(WINAPI*)(ID3D11DeviceContext*, UINT, UINT, INT);
    using VSSetShaderFn = void(WINAPI*)(ID3D11DeviceContext*, ID3D11VertexShader*, ID3D11ClassInstance* const*, UINT);
    using PSSetShaderFn = void(WINAPI*)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
    using OMSetRenderTargetsFn = void(WINAPI*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);

    static PresentFn s_originalPresent;
    static DrawFn s_originalDraw;
    static DrawIndexedFn s_originalDrawIndexed;
    static VSSetShaderFn s_originalVSSetShader;
    static PSSetShaderFn s_originalPSSetShader;
    static OMSetRenderTargetsFn s_originalOMSetRenderTargets;

    // D3D resources
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    HWND m_gameWindow = nullptr;

    // Callbacks
    PresentCallback m_presentCallback;
    DrawCallback m_preDrawCallback;
    DrawCallback m_postDrawCallback;
    ShaderCallback m_shaderCallback;

    bool m_initialized = false;
};

} // namespace GTA5VR
