#pragma once

#include "XRCore.hpp"
#include <dxgi.h>
#include <dxgi1_2.h>

namespace OVRInject {
namespace XR {

/**
 * XRGraphicsBinding - Manages D3D11 graphics binding for OpenXR
 *
 * This class handles:
 * - Creating or using an existing D3D11 device
 * - Providing the graphics binding info for session creation
 * - Creating shared textures for frame passing between contexts
 */
class XRGraphicsBinding {
public:
    XRGraphicsBinding();
    ~XRGraphicsBinding();

    // Disable copy
    XRGraphicsBinding(const XRGraphicsBinding&) = delete;
    XRGraphicsBinding& operator=(const XRGraphicsBinding&) = delete;

    /**
     * Initialize with an existing D3D11 device (e.g., from game hook)
     * @param device The D3D11 device to use
     * @return true on success
     */
    bool Initialize(ID3D11Device* existingDevice);

    /**
     * Initialize by creating a new D3D11 device matching the required adapter
     * @param adapterLuid LUID of the adapter to use (from OpenXR requirements)
     * @return true on success
     */
    bool InitializeWithLuid(const LUID& adapterLuid);

    /**
     * Shutdown and cleanup
     */
    void Shutdown();

    /**
     * Check if initialized
     */
    bool IsInitialized() const { return device_ != nullptr; }

    //-------------------------------------------------------------------------
    // Accessors
    //-------------------------------------------------------------------------

    ID3D11Device* GetDevice() const { return device_; }
    ID3D11DeviceContext* GetContext() const { return context_; }

    /**
     * Get the graphics binding structure for OpenXR session creation
     */
    const XrGraphicsBindingD3D11KHR& GetBindingInfo() const {
        return binding_info_;
    }

    /**
     * Check if we own the device (created it ourselves)
     */
    bool OwnsDevice() const { return owns_device_; }

    //-------------------------------------------------------------------------
    // Texture Creation Helpers
    //-------------------------------------------------------------------------

    /**
     * Create a shared texture that can be used across D3D11 contexts
     * @param width Texture width
     * @param height Texture height
     * @param format Texture format
     * @return The created texture (caller must Release)
     */
    ID3D11Texture2D* CreateSharedTexture(
        uint32_t width,
        uint32_t height,
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    /**
     * Create a render target view for a texture
     */
    ID3D11RenderTargetView* CreateRenderTargetView(ID3D11Texture2D* texture);

    /**
     * Create a depth stencil texture and view
     */
    bool CreateDepthStencil(
        uint32_t width,
        uint32_t height,
        ID3D11Texture2D** outTexture,
        ID3D11DepthStencilView** outView,
        ID3D11DepthStencilState** outState);

private:
    /**
     * Find adapter by LUID
     */
    IDXGIAdapter1* FindAdapterByLuid(const LUID& luid);

    /**
     * Create D3D11 device on specific adapter
     */
    bool CreateDeviceOnAdapter(IDXGIAdapter1* adapter);

    //-------------------------------------------------------------------------
    // Members
    //-------------------------------------------------------------------------

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    bool owns_device_ = false;

    XrGraphicsBindingD3D11KHR binding_info_ = {
        XR_TYPE_GRAPHICS_BINDING_D3D11_KHR
    };
};

} // namespace XR
} // namespace OVRInject
