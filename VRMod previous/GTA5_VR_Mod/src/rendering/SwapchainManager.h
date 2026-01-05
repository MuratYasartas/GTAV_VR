#pragma once

#include <d3d11.h>
#include <vector>
#include <array>
#include <memory>
#include <openxr/openxr.h>

namespace GTA5VR {

// Maximum number of swapchain images (typically 3 for triple buffering)
constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 4;

// Eye indices
enum class Eye : uint32_t {
    Left = 0,
    Right = 1,
    Count = 2
};

// Swapchain image wrapper
struct SwapchainImage {
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    bool acquired = false;
};

// Per-eye swapchain data
struct EyeSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<SwapchainImage> images;
    uint32_t currentIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    bool isValid = false;
};

// Depth swapchain data
struct DepthSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> textures;
    std::vector<ID3D11DepthStencilView*> dsvs;
    uint32_t currentIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT;
    bool isValid = false;
};

class SwapchainManager {
public:
    SwapchainManager();
    ~SwapchainManager();

    // Prevent copying
    SwapchainManager(const SwapchainManager&) = delete;
    SwapchainManager& operator=(const SwapchainManager&) = delete;

    // Initialization
    bool Initialize(ID3D11Device* device, XrSession session, XrViewConfigurationView* viewConfigs);
    void Shutdown();

    // Swapchain operations
    bool AcquireSwapchainImage(Eye eye);
    bool WaitSwapchainImage(Eye eye, XrDuration timeout = XR_INFINITE_DURATION);
    bool ReleaseSwapchainImage(Eye eye);

    // Depth swapchain operations
    bool AcquireDepthImage(Eye eye);
    bool ReleaseDepthImage(Eye eye);

    // Getters
    SwapchainImage* GetCurrentImage(Eye eye);
    ID3D11RenderTargetView* GetCurrentRTV(Eye eye);
    ID3D11ShaderResourceView* GetCurrentSRV(Eye eye);
    ID3D11DepthStencilView* GetCurrentDSV(Eye eye);
    ID3D11DepthStencilView* GetDepthDSV(Eye eye);

    XrSwapchain GetSwapchainHandle(Eye eye) const;
    XrSwapchain GetDepthSwapchainHandle(Eye eye) const;

    uint32_t GetWidth(Eye eye) const;
    uint32_t GetHeight(Eye eye) const;
    uint32_t GetCurrentImageIndex(Eye eye) const;

    DXGI_FORMAT GetFormat() const { return m_colorFormat; }
    DXGI_FORMAT GetDepthFormat() const { return m_depthFormat; }

    // Resolution scaling
    void SetResolutionScale(float scale);
    float GetResolutionScale() const { return m_resolutionScale; }

    // Utility
    bool IsInitialized() const { return m_initialized; }
    bool HasDepthSwapchain() const { return m_depthSwapchains[0].isValid; }

private:
    bool CreateColorSwapchain(Eye eye, uint32_t width, uint32_t height);
    bool CreateDepthSwapchain(Eye eye, uint32_t width, uint32_t height);
    bool CreateImageViews(Eye eye);
    void DestroySwapchain(Eye eye);
    void DestroyDepthSwapchain(Eye eye);

    DXGI_FORMAT SelectColorFormat(const std::vector<int64_t>& supportedFormats);
    DXGI_FORMAT SelectDepthFormat(const std::vector<int64_t>& supportedFormats);

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    XrSession m_session = XR_NULL_HANDLE;

    std::array<EyeSwapchain, static_cast<size_t>(Eye::Count)> m_swapchains;
    std::array<DepthSwapchain, static_cast<size_t>(Eye::Count)> m_depthSwapchains;

    DXGI_FORMAT m_colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    DXGI_FORMAT m_depthFormat = DXGI_FORMAT_D32_FLOAT;

    float m_resolutionScale = 1.0f;
    bool m_initialized = false;
    bool m_useDepth = true;
};

} // namespace GTA5VR
