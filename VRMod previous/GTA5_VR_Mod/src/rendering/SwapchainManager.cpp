#include "SwapchainManager.h"
#include "../core/Logger.h"
#include <openxr/openxr_platform.h>

namespace GTA5VR {

// Safe release helper
template<typename T>
void SafeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

SwapchainManager::SwapchainManager() {
    LOG_DEBUG("SwapchainManager", "Constructor called");
}

SwapchainManager::~SwapchainManager() {
    Shutdown();
}

bool SwapchainManager::Initialize(ID3D11Device* device, XrSession session,
                                   XrViewConfigurationView* viewConfigs) {
    if (m_initialized) {
        LOG_WARN("SwapchainManager", "Already initialized");
        return true;
    }

    if (!device || session == XR_NULL_HANDLE || !viewConfigs) {
        LOG_ERROR("SwapchainManager", "Invalid parameters for initialization");
        return false;
    }

    m_device = device;
    m_device->GetImmediateContext(&m_context);
    m_session = session;

    LOG_INFO("SwapchainManager", "Initializing swapchain manager");

    // Create swapchains for each eye
    for (uint32_t i = 0; i < static_cast<uint32_t>(Eye::Count); i++) {
        Eye eye = static_cast<Eye>(i);

        uint32_t width = static_cast<uint32_t>(viewConfigs[i].recommendedImageRectWidth * m_resolutionScale);
        uint32_t height = static_cast<uint32_t>(viewConfigs[i].recommendedImageRectHeight * m_resolutionScale);

        // Ensure dimensions are valid
        width = std::max(width, viewConfigs[i].maxImageRectWidth);
        width = std::min(width, viewConfigs[i].maxImageRectWidth);
        height = std::max(height, viewConfigs[i].maxImageRectHeight);
        height = std::min(height, viewConfigs[i].maxImageRectHeight);

        LOG_INFO("SwapchainManager", "Eye %u: Creating swapchain %ux%u (recommended: %ux%u)",
                 i, width, height,
                 viewConfigs[i].recommendedImageRectWidth,
                 viewConfigs[i].recommendedImageRectHeight);

        if (!CreateColorSwapchain(eye, width, height)) {
            LOG_ERROR("SwapchainManager", "Failed to create color swapchain for eye %u", i);
            Shutdown();
            return false;
        }

        if (m_useDepth && !CreateDepthSwapchain(eye, width, height)) {
            LOG_WARN("SwapchainManager", "Failed to create depth swapchain for eye %u, continuing without depth", i);
            m_useDepth = false;
        }
    }

    m_initialized = true;
    LOG_INFO("SwapchainManager", "Swapchain manager initialized successfully");
    return true;
}

void SwapchainManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("SwapchainManager", "Shutting down swapchain manager");

    for (uint32_t i = 0; i < static_cast<uint32_t>(Eye::Count); i++) {
        DestroySwapchain(static_cast<Eye>(i));
        DestroyDepthSwapchain(static_cast<Eye>(i));
    }

    SafeRelease(m_context);
    m_device = nullptr;
    m_session = XR_NULL_HANDLE;
    m_initialized = false;
}

bool SwapchainManager::CreateColorSwapchain(Eye eye, uint32_t width, uint32_t height) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    EyeSwapchain& swapchain = m_swapchains[eyeIndex];

    // Enumerate supported swapchain formats
    uint32_t formatCount = 0;
    XrResult result = xrEnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr);
    if (XR_FAILED(result) || formatCount == 0) {
        LOG_ERROR("SwapchainManager", "Failed to enumerate swapchain formats: %d", result);
        return false;
    }

    std::vector<int64_t> formats(formatCount);
    result = xrEnumerateSwapchainFormats(m_session, formatCount, &formatCount, formats.data());
    if (XR_FAILED(result)) {
        LOG_ERROR("SwapchainManager", "Failed to get swapchain formats: %d", result);
        return false;
    }

    // Select format
    m_colorFormat = SelectColorFormat(formats);
    LOG_DEBUG("SwapchainManager", "Selected color format: %d", m_colorFormat);

    // Create swapchain
    XrSwapchainCreateInfo createInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                            XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    createInfo.format = static_cast<int64_t>(m_colorFormat);
    createInfo.sampleCount = 1;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.faceCount = 1;
    createInfo.arraySize = 1;
    createInfo.mipCount = 1;

    result = xrCreateSwapchain(m_session, &createInfo, &swapchain.handle);
    if (XR_FAILED(result)) {
        LOG_ERROR("SwapchainManager", "Failed to create color swapchain: %d", result);
        return false;
    }

    swapchain.width = width;
    swapchain.height = height;
    swapchain.format = m_colorFormat;

    // Enumerate swapchain images
    uint32_t imageCount = 0;
    result = xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr);
    if (XR_FAILED(result) || imageCount == 0) {
        LOG_ERROR("SwapchainManager", "Failed to enumerate swapchain images: %d", result);
        return false;
    }

    LOG_DEBUG("SwapchainManager", "Swapchain has %u images", imageCount);

    std::vector<XrSwapchainImageD3D11KHR> xrImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    result = xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount,
                                        reinterpret_cast<XrSwapchainImageBaseHeader*>(xrImages.data()));
    if (XR_FAILED(result)) {
        LOG_ERROR("SwapchainManager", "Failed to get swapchain images: %d", result);
        return false;
    }

    // Store images and create views
    swapchain.images.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        swapchain.images[i].texture = xrImages[i].texture;
        swapchain.images[i].width = width;
        swapchain.images[i].height = height;
    }

    if (!CreateImageViews(eye)) {
        LOG_ERROR("SwapchainManager", "Failed to create image views");
        return false;
    }

    swapchain.isValid = true;
    LOG_INFO("SwapchainManager", "Created color swapchain for eye %u: %ux%u, %u images",
             eyeIndex, width, height, imageCount);

    return true;
}

bool SwapchainManager::CreateDepthSwapchain(Eye eye, uint32_t width, uint32_t height) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    DepthSwapchain& swapchain = m_depthSwapchains[eyeIndex];

    // Create depth swapchain
    XrSwapchainCreateInfo createInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    createInfo.format = static_cast<int64_t>(m_depthFormat);
    createInfo.sampleCount = 1;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.faceCount = 1;
    createInfo.arraySize = 1;
    createInfo.mipCount = 1;

    XrResult result = xrCreateSwapchain(m_session, &createInfo, &swapchain.handle);
    if (XR_FAILED(result)) {
        LOG_WARN("SwapchainManager", "Failed to create depth swapchain: %d", result);
        return false;
    }

    swapchain.width = width;
    swapchain.height = height;
    swapchain.format = m_depthFormat;

    // Enumerate depth images
    uint32_t imageCount = 0;
    result = xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr);
    if (XR_FAILED(result) || imageCount == 0) {
        LOG_ERROR("SwapchainManager", "Failed to enumerate depth swapchain images");
        return false;
    }

    std::vector<XrSwapchainImageD3D11KHR> xrImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    result = xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount,
                                        reinterpret_cast<XrSwapchainImageBaseHeader*>(xrImages.data()));
    if (XR_FAILED(result)) {
        LOG_ERROR("SwapchainManager", "Failed to get depth swapchain images");
        return false;
    }

    swapchain.textures.resize(imageCount);
    swapchain.dsvs.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; i++) {
        swapchain.textures[i] = xrImages[i].texture;

        // Create DSV
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = m_depthFormat;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        HRESULT hr = m_device->CreateDepthStencilView(swapchain.textures[i], &dsvDesc,
                                                       &swapchain.dsvs[i]);
        if (FAILED(hr)) {
            LOG_ERROR("SwapchainManager", "Failed to create depth DSV: 0x%08X", hr);
            return false;
        }
    }

    swapchain.isValid = true;
    LOG_INFO("SwapchainManager", "Created depth swapchain for eye %u: %ux%u",
             eyeIndex, width, height);

    return true;
}

bool SwapchainManager::CreateImageViews(Eye eye) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    EyeSwapchain& swapchain = m_swapchains[eyeIndex];

    for (auto& image : swapchain.images) {
        // Create RTV
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = swapchain.format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;

        HRESULT hr = m_device->CreateRenderTargetView(image.texture, &rtvDesc, &image.rtv);
        if (FAILED(hr)) {
            LOG_ERROR("SwapchainManager", "Failed to create RTV: 0x%08X", hr);
            return false;
        }

        // Create SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = swapchain.format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        hr = m_device->CreateShaderResourceView(image.texture, &srvDesc, &image.srv);
        if (FAILED(hr)) {
            LOG_ERROR("SwapchainManager", "Failed to create SRV: 0x%08X", hr);
            return false;
        }

        // Create DSV for per-image depth (using a staging texture)
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = image.width;
        depthDesc.Height = image.height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        ID3D11Texture2D* depthTexture = nullptr;
        hr = m_device->CreateTexture2D(&depthDesc, nullptr, &depthTexture);
        if (SUCCEEDED(hr)) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = depthDesc.Format;
            dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            dsvDesc.Texture2D.MipSlice = 0;

            hr = m_device->CreateDepthStencilView(depthTexture, &dsvDesc, &image.dsv);
            depthTexture->Release();

            if (FAILED(hr)) {
                LOG_WARN("SwapchainManager", "Failed to create per-image DSV: 0x%08X", hr);
            }
        }
    }

    return true;
}

void SwapchainManager::DestroySwapchain(Eye eye) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    EyeSwapchain& swapchain = m_swapchains[eyeIndex];

    for (auto& image : swapchain.images) {
        SafeRelease(image.dsv);
        SafeRelease(image.srv);
        SafeRelease(image.rtv);
        // Don't release texture - owned by OpenXR
        image.texture = nullptr;
    }
    swapchain.images.clear();

    if (swapchain.handle != XR_NULL_HANDLE) {
        xrDestroySwapchain(swapchain.handle);
        swapchain.handle = XR_NULL_HANDLE;
    }

    swapchain.isValid = false;
}

void SwapchainManager::DestroyDepthSwapchain(Eye eye) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    DepthSwapchain& swapchain = m_depthSwapchains[eyeIndex];

    for (auto& dsv : swapchain.dsvs) {
        SafeRelease(dsv);
    }
    swapchain.dsvs.clear();
    swapchain.textures.clear();

    if (swapchain.handle != XR_NULL_HANDLE) {
        xrDestroySwapchain(swapchain.handle);
        swapchain.handle = XR_NULL_HANDLE;
    }

    swapchain.isValid = false;
}

bool SwapchainManager::AcquireSwapchainImage(Eye eye) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    EyeSwapchain& swapchain = m_swapchains[eyeIndex];

    if (!swapchain.isValid) {
        LOG_ERROR("SwapchainManager", "Cannot acquire from invalid swapchain");
        return false;
    }

    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &swapchain.currentIndex);
    if (XR_FAILED(result)) {
        LOG_ERROR("SwapchainManager", "Failed to acquire swapchain image: %d", result);
        return false;
    }

    swapchain.images[swapchain.currentIndex].acquired = true;
    LOG_DEBUG("SwapchainManager", "Acquired swapchain image %u for eye %u",
              swapchain.currentIndex, eyeIndex);

    return true;
}

bool SwapchainManager::WaitSwapchainImage(Eye eye, XrDuration timeout) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    EyeSwapchain& swapchain = m_swapchains[eyeIndex];

    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = timeout;

    XrResult result = xrWaitSwapchainImage(swapchain.handle, &waitInfo);
    if (XR_FAILED(result)) {
        LOG_ERROR("SwapchainManager", "Failed to wait for swapchain image: %d", result);
        return false;
    }

    return true;
}

bool SwapchainManager::ReleaseSwapchainImage(Eye eye) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    EyeSwapchain& swapchain = m_swapchains[eyeIndex];

    if (!swapchain.isValid || !swapchain.images[swapchain.currentIndex].acquired) {
        LOG_WARN("SwapchainManager", "No acquired image to release");
        return false;
    }

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XrResult result = xrReleaseSwapchainImage(swapchain.handle, &releaseInfo);
    if (XR_FAILED(result)) {
        LOG_ERROR("SwapchainManager", "Failed to release swapchain image: %d", result);
        return false;
    }

    swapchain.images[swapchain.currentIndex].acquired = false;
    LOG_DEBUG("SwapchainManager", "Released swapchain image for eye %u", eyeIndex);

    return true;
}

bool SwapchainManager::AcquireDepthImage(Eye eye) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    DepthSwapchain& swapchain = m_depthSwapchains[eyeIndex];

    if (!swapchain.isValid) {
        return false;
    }

    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &swapchain.currentIndex);
    if (XR_FAILED(result)) {
        LOG_ERROR("SwapchainManager", "Failed to acquire depth swapchain image: %d", result);
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = xrWaitSwapchainImage(swapchain.handle, &waitInfo);

    return XR_SUCCEEDED(result);
}

bool SwapchainManager::ReleaseDepthImage(Eye eye) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    DepthSwapchain& swapchain = m_depthSwapchains[eyeIndex];

    if (!swapchain.isValid) {
        return false;
    }

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XrResult result = xrReleaseSwapchainImage(swapchain.handle, &releaseInfo);
    return XR_SUCCEEDED(result);
}

SwapchainImage* SwapchainManager::GetCurrentImage(Eye eye) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    EyeSwapchain& swapchain = m_swapchains[eyeIndex];

    if (!swapchain.isValid || swapchain.currentIndex >= swapchain.images.size()) {
        return nullptr;
    }

    return &swapchain.images[swapchain.currentIndex];
}

ID3D11RenderTargetView* SwapchainManager::GetCurrentRTV(Eye eye) {
    auto* image = GetCurrentImage(eye);
    return image ? image->rtv : nullptr;
}

ID3D11ShaderResourceView* SwapchainManager::GetCurrentSRV(Eye eye) {
    auto* image = GetCurrentImage(eye);
    return image ? image->srv : nullptr;
}

ID3D11DepthStencilView* SwapchainManager::GetCurrentDSV(Eye eye) {
    auto* image = GetCurrentImage(eye);
    return image ? image->dsv : nullptr;
}

ID3D11DepthStencilView* SwapchainManager::GetDepthDSV(Eye eye) {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    DepthSwapchain& swapchain = m_depthSwapchains[eyeIndex];

    if (!swapchain.isValid || swapchain.currentIndex >= swapchain.dsvs.size()) {
        return nullptr;
    }

    return swapchain.dsvs[swapchain.currentIndex];
}

XrSwapchain SwapchainManager::GetSwapchainHandle(Eye eye) const {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    return m_swapchains[eyeIndex].handle;
}

XrSwapchain SwapchainManager::GetDepthSwapchainHandle(Eye eye) const {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    return m_depthSwapchains[eyeIndex].handle;
}

uint32_t SwapchainManager::GetWidth(Eye eye) const {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    return m_swapchains[eyeIndex].width;
}

uint32_t SwapchainManager::GetHeight(Eye eye) const {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    return m_swapchains[eyeIndex].height;
}

uint32_t SwapchainManager::GetCurrentImageIndex(Eye eye) const {
    uint32_t eyeIndex = static_cast<uint32_t>(eye);
    return m_swapchains[eyeIndex].currentIndex;
}

void SwapchainManager::SetResolutionScale(float scale) {
    m_resolutionScale = std::clamp(scale, 0.5f, 2.0f);
    LOG_INFO("SwapchainManager", "Resolution scale set to %.2f", m_resolutionScale);
    // Note: Actual resize requires recreating swapchains
}

DXGI_FORMAT SwapchainManager::SelectColorFormat(const std::vector<int64_t>& supportedFormats) {
    // Preferred formats in order
    const DXGI_FORMAT preferredFormats[] = {
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM,
    };

    for (auto preferred : preferredFormats) {
        for (auto supported : supportedFormats) {
            if (static_cast<int64_t>(preferred) == supported) {
                return preferred;
            }
        }
    }

    // Fallback to first supported format
    return static_cast<DXGI_FORMAT>(supportedFormats[0]);
}

DXGI_FORMAT SwapchainManager::SelectDepthFormat(const std::vector<int64_t>& supportedFormats) {
    const DXGI_FORMAT preferredFormats[] = {
        DXGI_FORMAT_D32_FLOAT,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        DXGI_FORMAT_D16_UNORM,
    };

    for (auto preferred : preferredFormats) {
        for (auto supported : supportedFormats) {
            if (static_cast<int64_t>(preferred) == supported) {
                return preferred;
            }
        }
    }

    return DXGI_FORMAT_D32_FLOAT;
}

} // namespace GTA5VR
