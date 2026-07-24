#pragma once

#include <d3d11.h>
#include <cstdint>

namespace OVRInject {

/**
 * IOverlaySurface - Generic render target interface for overlay UI
 *
 * Provides a minimal API for rendering UI into a texture that can be
 * presented by different VR runtimes (OpenXR/OpenVR).
 */
class IOverlaySurface {
public:
    virtual ~IOverlaySurface() = default;

    virtual bool IsInitialized() const = 0;

    virtual ID3D11RenderTargetView* BeginRender() = 0;
    virtual void EndRender() = 0;

    virtual uint32_t GetTextureWidth() const = 0;
    virtual uint32_t GetTextureHeight() const = 0;
};

} // namespace OVRInject
