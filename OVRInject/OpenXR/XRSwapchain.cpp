#include "XRSwapchain.hpp"

namespace OVRInject {
namespace XR {

//=============================================================================
// XRSwapchain Implementation
//=============================================================================

XRSwapchain::XRSwapchain(XRSession* session, XRGraphicsBinding* graphics)
    : session_(session)
    , graphics_(graphics) {
}

XRSwapchain::~XRSwapchain() {
    Destroy();
}

bool XRSwapchain::Create(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (swapchain_ != XR_NULL_HANDLE) {
        LOGSTRF("Swapchain already created\n");
        return true;
    }

    if (!session_ || !session_->IsCreated()) {
        LOGSTRF("Cannot create swapchain: session not created\n");
        return false;
    }

    LOGSTRF("Creating OpenXR swapchain (%ux%u)...\n", width, height);

    width_ = width;
    height_ = height;
    format_ = format;

    // Query supported swapchain formats
    uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(session_->GetHandle(), 0, &formatCount, nullptr);

    std::vector<int64_t> formats(formatCount);
    xrEnumerateSwapchainFormats(session_->GetHandle(), formatCount, &formatCount, formats.data());

    LOGSTRF("Supported swapchain formats:\n");
    for (int64_t fmt : formats) {
        LOGSTRF("  DXGI_FORMAT_%lld\n", fmt);
    }

    // Select a compatible format.
    auto supportsFormat = [&](DXGI_FORMAT fmt) {
        for (int64_t available : formats) {
            if (available == static_cast<int64_t>(fmt)) {
                return true;
            }
        }
        return false;
    };

    if (!formats.empty()) {
        DXGI_FORMAT preferredFormats[] = {
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R10G10B10A2_UNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT
        };

        bool picked = false;
        for (DXGI_FORMAT preferred : preferredFormats) {
            if (supportsFormat(preferred)) {
                format_ = preferred;
                picked = true;
                break;
            }
        }

        if (!picked && supportsFormat(format)) {
            format_ = format;
            picked = true;
        }

        if (!picked) {
            LOGSTRF("Requested format %d not supported, using first available\n", format);
            format_ = static_cast<DXGI_FORMAT>(formats[0]);
        }
    }

    LOGSTRF("Using swapchain format %d\n", format_);

    // Create swapchain
    XrSwapchainCreateInfo createInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                            XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                            XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    createInfo.format = static_cast<int64_t>(format_);
    createInfo.sampleCount = 1;
    createInfo.width = width_;
    createInfo.height = height_;
    createInfo.faceCount = 1;
    createInfo.arraySize = 1;
    createInfo.mipCount = 1;

    XrResult result = xrCreateSwapchain(session_->GetHandle(), &createInfo, &swapchain_);
    if (XR_FAILED(result)) {
        LOGSTRF("xrCreateSwapchain failed: %s\n", GetResultString(result));
        return false;
    }

    // Enumerate images and create RTVs
    if (!EnumerateImages()) {
        Destroy();
        return false;
    }

    if (!CreateRenderTargetViews()) {
        Destroy();
        return false;
    }

    LOGSTRF("OpenXR swapchain created with %u images\n",
            static_cast<uint32_t>(images_.size()));
    return true;
}

void XRSwapchain::Destroy() {
    // Release render target views
    for (auto& image : images_) {
        if (image.rtv != nullptr) {
            image.rtv->Release();
            image.rtv = nullptr;
        }
        // Note: We don't release texture - it's owned by OpenXR
        image.texture = nullptr;
    }
    images_.clear();

    if (swapchain_ != XR_NULL_HANDLE) {
        xrDestroySwapchain(swapchain_);
        swapchain_ = XR_NULL_HANDLE;
    }

    width_ = 0;
    height_ = 0;
    format_ = DXGI_FORMAT_UNKNOWN;
    current_image_index_ = 0;
    image_acquired_ = false;
}

bool XRSwapchain::EnumerateImages() {
    uint32_t imageCount = 0;
    XrResult result = xrEnumerateSwapchainImages(swapchain_, 0, &imageCount, nullptr);
    if (XR_FAILED(result)) {
        LOGSTRF("xrEnumerateSwapchainImages count failed: %s\n", GetResultString(result));
        return false;
    }

    std::vector<XrSwapchainImageD3D11KHR> d3dImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});

    result = xrEnumerateSwapchainImages(
        swapchain_,
        imageCount,
        &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(d3dImages.data()));

    if (XR_FAILED(result)) {
        LOGSTRF("xrEnumerateSwapchainImages failed: %s\n", GetResultString(result));
        return false;
    }

    images_.resize(imageCount);
    DXGI_FORMAT actualFormat = DXGI_FORMAT_UNKNOWN;
    for (uint32_t i = 0; i < imageCount; ++i) {
        images_[i].texture = d3dImages[i].texture;
        images_[i].rtv = nullptr;

        if (images_[i].texture) {
            D3D11_TEXTURE2D_DESC desc = {};
            images_[i].texture->GetDesc(&desc);
            if (actualFormat == DXGI_FORMAT_UNKNOWN) {
                actualFormat = desc.Format;
            } else if (actualFormat != desc.Format) {
                LOGSTRF("Swapchain image %u format differs (%u vs %u)\n",
                        i, static_cast<unsigned>(actualFormat),
                        static_cast<unsigned>(desc.Format));
            }
        }
    }

    if (actualFormat != DXGI_FORMAT_UNKNOWN && actualFormat != format_) {
        LOGSTRF("Swapchain image format %u differs from requested %u\n",
                static_cast<unsigned>(actualFormat),
                static_cast<unsigned>(format_));
        format_ = actualFormat;
    }

    return true;
}

bool XRSwapchain::CreateRenderTargetViews() {
    if (!graphics_ || !graphics_->IsInitialized()) {
        LOGSTRF("Cannot create RTVs: graphics not initialized\n");
        return false;
    }

    ID3D11Device* device = graphics_->GetDevice();

    auto normalizeRtvFormat = [&](DXGI_FORMAT fmt) {
        switch (fmt) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:
            return fmt;
        }
    };

    bool anyRtv = false;
    for (auto& image : images_) {
        if (image.texture == nullptr) {
            continue;
        }

        HRESULT hr = device->CreateRenderTargetView(image.texture, nullptr, &image.rtv);
        if (FAILED(hr)) {
            D3D11_TEXTURE2D_DESC desc = {};
            image.texture->GetDesc(&desc);
            DXGI_FORMAT rtvFormat = normalizeRtvFormat(format_);
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = rtvFormat;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            hr = device->CreateRenderTargetView(image.texture, &rtvDesc, &image.rtv);
            if (FAILED(hr)) {
                LOGSTRF("Failed to create RTV: 0x%08lx (fmt=%u bind=0x%08x misc=0x%08x samples=%u rtvFmt=%u)\n",
                    hr, desc.Format, desc.BindFlags, desc.MiscFlags, desc.SampleDesc.Count, rtvFormat);
                image.rtv = nullptr;
                continue;
            }
        }
        anyRtv = true;
    }

    if (!anyRtv) {
        LOGSTRF("No RTVs created for swapchain images; continuing without RTVs\n");
    }

    return true;
}

bool XRSwapchain::AcquireImage(uint32_t& imageIndex) {
    if (swapchain_ == XR_NULL_HANDLE) {
        return false;
    }

    if (image_acquired_) {
        LOGSTRF("Image already acquired\n");
        return false;
    }

    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};

    XrResult result = xrAcquireSwapchainImage(swapchain_, &acquireInfo, &current_image_index_);
    if (XR_FAILED(result)) {
        LOGSTRF("xrAcquireSwapchainImage failed: %s\n", GetResultString(result));
        return false;
    }

    image_acquired_ = true;
    imageIndex = current_image_index_;
    return true;
}

bool XRSwapchain::WaitImage(XrDuration timeout) {
    if (swapchain_ == XR_NULL_HANDLE || !image_acquired_) {
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = timeout;

    XrResult result = xrWaitSwapchainImage(swapchain_, &waitInfo);
    if (XR_FAILED(result)) {
        if (result != XR_TIMEOUT_EXPIRED) {
            LOGSTRF("xrWaitSwapchainImage failed: %s\n", GetResultString(result));
        }
        return false;
    }

    return true;
}

bool XRSwapchain::ReleaseImage() {
    if (swapchain_ == XR_NULL_HANDLE || !image_acquired_) {
        return false;
    }

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

    XrResult result = xrReleaseSwapchainImage(swapchain_, &releaseInfo);
    if (XR_FAILED(result)) {
        LOGSTRF("xrReleaseSwapchainImage failed: %s\n", GetResultString(result));
        return false;
    }

    image_acquired_ = false;
    return true;
}

const SwapchainImage& XRSwapchain::GetImage(uint32_t index) const {
    static SwapchainImage empty;
    if (index >= images_.size()) {
        return empty;
    }
    return images_[index];
}

ID3D11Texture2D* XRSwapchain::GetCurrentTexture() const {
    if (!image_acquired_ || current_image_index_ >= images_.size()) {
        return nullptr;
    }
    return images_[current_image_index_].texture;
}

ID3D11RenderTargetView* XRSwapchain::GetCurrentRTV() const {
    if (!image_acquired_ || current_image_index_ >= images_.size()) {
        return nullptr;
    }
    return images_[current_image_index_].rtv;
}

//=============================================================================
// XRStereoSwapchains Implementation
//=============================================================================

XRStereoSwapchains::XRStereoSwapchains(XRSession* session, XRGraphicsBinding* graphics)
    : session_(session)
    , graphics_(graphics) {
}

XRStereoSwapchains::~XRStereoSwapchains() {
    Destroy();
}

bool XRStereoSwapchains::Create(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    LOGSTRF("Creating stereo swapchains (%ux%u)...\n", width, height);

    for (size_t i = 0; i < static_cast<size_t>(Eye::Count); ++i) {
        swapchains_[i] = std::make_unique<XRSwapchain>(session_, graphics_);

        if (!swapchains_[i]->Create(width, height, format)) {
            LOGSTRF("Failed to create swapchain for eye %zu\n", i);
            Destroy();
            return false;
        }
    }

    LOGSTRF("Stereo swapchains created successfully\n");
    return true;
}

void XRStereoSwapchains::Destroy() {
    for (auto& swapchain : swapchains_) {
        swapchain.reset();
    }
}

bool XRStereoSwapchains::IsCreated() const {
    for (const auto& swapchain : swapchains_) {
        if (!swapchain || !swapchain->IsCreated()) {
            return false;
        }
    }
    return true;
}

XRSwapchain* XRStereoSwapchains::GetSwapchain(Eye eye) {
    size_t index = static_cast<size_t>(eye);
    if (index >= swapchains_.size()) {
        return nullptr;
    }
    return swapchains_[index].get();
}

const XRSwapchain* XRStereoSwapchains::GetSwapchain(Eye eye) const {
    size_t index = static_cast<size_t>(eye);
    if (index >= swapchains_.size()) {
        return nullptr;
    }
    return swapchains_[index].get();
}

bool XRStereoSwapchains::AcquireAll() {
    for (auto& swapchain : swapchains_) {
        if (swapchain) {
            uint32_t index;
            if (!swapchain->AcquireImage(index)) {
                return false;
            }
        }
    }
    return true;
}

bool XRStereoSwapchains::WaitAll(XrDuration timeout) {
    for (auto& swapchain : swapchains_) {
        if (swapchain) {
            if (!swapchain->WaitImage(timeout)) {
                return false;
            }
        }
    }
    return true;
}

bool XRStereoSwapchains::ReleaseAll() {
    for (auto& swapchain : swapchains_) {
        if (swapchain) {
            if (!swapchain->ReleaseImage()) {
                return false;
            }
        }
    }
    return true;
}

} // namespace XR
} // namespace OVRInject
