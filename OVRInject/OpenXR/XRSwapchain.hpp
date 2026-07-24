#pragma once

#include "XRCore.hpp"
#include "XRSession.hpp"
#include "XRGraphicsBinding.hpp"
#include <vector>
#include <memory>
#include <array>

namespace OVRInject {
namespace XR {

/**
 * SwapchainImage - Contains D3D11 resources for a single swapchain image
 */
struct SwapchainImage {
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
};

/**
 * XRSwapchain - Manages a single OpenXR swapchain
 *
 * Handles:
 * - Swapchain creation and destruction
 * - Image enumeration and RTV creation
 * - Acquire/wait/release cycle for frame rendering
 */
class XRSwapchain {
public:
    XRSwapchain(XRSession* session, XRGraphicsBinding* graphics);
    ~XRSwapchain();

    // Disable copy
    XRSwapchain(const XRSwapchain&) = delete;
    XRSwapchain& operator=(const XRSwapchain&) = delete;

    /**
     * Create the swapchain
     * @param width Swapchain image width
     * @param height Swapchain image height
     * @param format Image format (default SRGB)
     * @return true on success
     */
    bool Create(uint32_t width, uint32_t height,
                DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

    /**
     * Destroy the swapchain and release resources
     */
    void Destroy();

    /**
     * Check if created
     */
    bool IsCreated() const { return swapchain_ != XR_NULL_HANDLE; }
    bool IsInitialized() const { return IsCreated(); }

    //-------------------------------------------------------------------------
    // Frame Cycle
    //-------------------------------------------------------------------------

    /**
     * Acquire the next swapchain image
     * @param imageIndex Output: index of acquired image
     * @return true on success
     */
    bool AcquireImage(uint32_t& imageIndex);

    /**
     * Wait for the swapchain image to be ready
     * @param timeout Wait timeout in nanoseconds (default infinite)
     * @return true on success
     */
    bool WaitImage(XrDuration timeout = XR_INFINITE_DURATION);

    /**
     * Release the swapchain image
     * @return true on success
     */
    bool ReleaseImage();

    //-------------------------------------------------------------------------
    // Accessors
    //-------------------------------------------------------------------------

    XrSwapchain GetHandle() const { return swapchain_; }
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    DXGI_FORMAT GetFormat() const { return format_; }
    uint32_t GetImageCount() const { return static_cast<uint32_t>(images_.size()); }

    /**
     * Get image at specific index
     */
    const SwapchainImage& GetImage(uint32_t index) const;

    /**
     * Get currently acquired image texture
     */
    ID3D11Texture2D* GetCurrentTexture() const;

    /**
     * Get currently acquired image render target view
     */
    ID3D11RenderTargetView* GetCurrentRTV() const;

    /**
     * Get current image index
     */
    uint32_t GetCurrentImageIndex() const { return current_image_index_; }

    /**
     * Check if an image is currently acquired
     */
    bool IsImageAcquired() const { return image_acquired_; }

private:
    /**
     * Enumerate swapchain images and create D3D11 resources
     */
    bool EnumerateImages();

    /**
     * Create render target views for all images
     */
    bool CreateRenderTargetViews();

    //-------------------------------------------------------------------------
    // Members
    //-------------------------------------------------------------------------

    XRSession* session_;
    XRGraphicsBinding* graphics_;

    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;

    std::vector<SwapchainImage> images_;
    uint32_t current_image_index_ = 0;
    bool image_acquired_ = false;
};

/**
 * XRStereoSwapchains - Manages both eye swapchains for stereo rendering
 */
class XRStereoSwapchains {
public:
    XRStereoSwapchains(XRSession* session, XRGraphicsBinding* graphics);
    ~XRStereoSwapchains();

    /**
     * Create swapchains for both eyes
     * @param width Swapchain image width
     * @param height Swapchain image height
     * @param format Image format
     * @return true on success
     */
    bool Create(uint32_t width, uint32_t height,
                DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    /**
     * Destroy both swapchains
     */
    void Destroy();

    /**
     * Check if created
     */
    bool IsCreated() const;

    //-------------------------------------------------------------------------
    // Accessors
    //-------------------------------------------------------------------------

    /**
     * Get swapchain for specific eye
     */
    XRSwapchain* GetSwapchain(Eye eye);
    const XRSwapchain* GetSwapchain(Eye eye) const;

    XRSwapchain* GetLeftSwapchain() { return GetSwapchain(Eye::Left); }
    XRSwapchain* GetRightSwapchain() { return GetSwapchain(Eye::Right); }

    //-------------------------------------------------------------------------
    // Convenience Methods
    //-------------------------------------------------------------------------

    /**
     * Acquire images from both swapchains
     */
    bool AcquireAll();

    /**
     * Wait for both swapchain images
     */
    bool WaitAll(XrDuration timeout = XR_INFINITE_DURATION);

    /**
     * Release both swapchain images
     */
    bool ReleaseAll();

private:
    XRSession* session_;
    XRGraphicsBinding* graphics_;

    std::array<std::unique_ptr<XRSwapchain>, static_cast<size_t>(Eye::Count)> swapchains_;
};

} // namespace XR
} // namespace OVRInject
