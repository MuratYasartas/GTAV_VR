#pragma once

#include "XRCore.hpp"
#include "XRSession.hpp"
#include "XRGraphicsBinding.hpp"
#include "XRSwapchain.hpp"
#include "../Overlay/OverlaySurface.hpp"
#include <memory>

namespace OVRInject {
namespace XR {

/**
 * Overlay placement mode
 */
enum class OverlayPlacement {
    HeadLocked,    // Fixed relative to head (HUD-style)
    WorldLocked,   // Fixed in world space
    HandAttached   // Attached to a controller
};

/**
 * Overlay configuration
 */
struct OverlayConfig {
    // Overlay dimensions (in meters when world-locked)
    float width = 1.0f;
    float height = 0.75f;

    // Texture resolution
    uint32_t texture_width = 1024;
    uint32_t texture_height = 768;

    // Placement
    OverlayPlacement placement = OverlayPlacement::HeadLocked;

    // Position offset from reference point
    // For head-locked: offset from head center
    // For world-locked: position in world space
    // For hand-attached: offset from controller
    XMFLOAT3 position_offset = {0.0f, 0.0f, -1.5f};

    // Rotation (euler angles in degrees)
    XMFLOAT3 rotation = {0.0f, 0.0f, 0.0f};

    // Transparency
    float opacity = 1.0f;

    // Sorting order (higher = rendered on top)
    int32_t sort_order = 0;

    // Visibility
    bool visible = true;
};

/**
 * XROverlay - Manages an overlay quad layer in OpenXR
 *
 * Overlays are rendered as XrCompositionLayerQuad and can be positioned:
 * - Head-locked: Fixed relative to the user's head (for HUD, menus)
 * - World-locked: Fixed in world space (for in-game UI panels)
 * - Hand-attached: Attached to a controller (for wrist displays)
 *
 * Each overlay has its own swapchain for rendering content.
 */
class XROverlay : public OVRInject::IOverlaySurface {
public:
    XROverlay(XRSession* session, XRGraphicsBinding* graphics, const OverlayConfig& config);
    ~XROverlay();

    // Disable copy
    XROverlay(const XROverlay&) = delete;
    XROverlay& operator=(const XROverlay&) = delete;

    /**
     * Initialize the overlay (creates swapchain)
     * @return true on success
     */
    bool Initialize();

    /**
     * Shutdown and cleanup
     */
    void Shutdown();

    /**
     * Check if initialized
     */
    bool IsInitialized() const override { return swapchain_ != nullptr && swapchain_->IsInitialized(); }

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------

    /**
     * Get current configuration
     */
    const OverlayConfig& GetConfig() const { return config_; }

    /**
     * Update configuration
     */
    void SetConfig(const OverlayConfig& config) { config_ = config; }

    /**
     * Set visibility
     */
    void SetVisible(bool visible) { config_.visible = visible; }
    bool IsVisible() const { return config_.visible; }

    /**
     * Set opacity
     */
    void SetOpacity(float opacity) { config_.opacity = opacity; }

    /**
     * Set position offset
     */
    void SetPositionOffset(const XMFLOAT3& offset) { config_.position_offset = offset; }

    /**
     * Set rotation
     */
    void SetRotation(const XMFLOAT3& rotation) { config_.rotation = rotation; }

    /**
     * Set placement mode
     */
    void SetPlacement(OverlayPlacement placement) { config_.placement = placement; }

    //-------------------------------------------------------------------------
    // Rendering
    //-------------------------------------------------------------------------

    /**
     * Begin rendering to the overlay
     * @return Render target view to render to, or nullptr on failure
     */
    ID3D11RenderTargetView* BeginRender() override;

    /**
     * End rendering to the overlay
     */
    void EndRender() override;

    /**
     * Get the texture for external rendering
     * Call AcquireImage first, render, then call ReleaseImage
     */
    ID3D11Texture2D* GetTexture();

    /**
     * Acquire next swapchain image
     */
    bool AcquireImage();

    /**
     * Release current swapchain image
     */
    bool ReleaseImage();

    //-------------------------------------------------------------------------
    // Composition Layer
    //-------------------------------------------------------------------------

    /**
     * Build the composition layer for this overlay
     * Call this when constructing layers for xrEndFrame
     *
     * @param reference_space Reference space for positioning
     * @param head_pose Current head pose (for head-locked overlays)
     * @param hand_pose Hand pose (for hand-attached overlays)
     * @param view_space VIEW reference space; head-locked overlays are
     *        submitted here so the compositor locks them to the real
     *        head pose at display time (no prediction swim)
     * @return Pointer to the layer, or nullptr if not visible
     */
    XrCompositionLayerQuad* BuildLayer(XrSpace reference_space,
                                       const XrPosef& head_pose,
                                       const XrPosef* hand_pose = nullptr,
                                       XrSpace view_space = XR_NULL_HANDLE);

    /**
     * Get texture dimensions
     */
    uint32_t GetTextureWidth() const override { return config_.texture_width; }
    uint32_t GetTextureHeight() const override { return config_.texture_height; }

private:
    /**
     * Calculate the pose for the overlay based on placement mode
     */
    XrPosef CalculatePose(const XrPosef& head_pose, const XrPosef* hand_pose) const;

    /**
     * Convert euler angles to quaternion
     */
    XrQuaternionf EulerToQuaternion(float pitch, float yaw, float roll) const;

    XRSession* session_;
    XRGraphicsBinding* graphics_;
    OverlayConfig config_;

    // Swapchain for overlay rendering
    std::unique_ptr<XRSwapchain> swapchain_;

    // Composition layer (reused each frame)
    XrCompositionLayerQuad layer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};

    // Current swapchain image index
    uint32_t current_image_index_ = 0;
    bool image_acquired_ = false;
};

/**
 * XROverlayManager - Manages multiple overlays
 *
 * Handles overlay creation, rendering order, and layer composition.
 */
class XROverlayManager {
public:
    XROverlayManager(XRSession* session, XRGraphicsBinding* graphics);
    ~XROverlayManager();

    // Disable copy
    XROverlayManager(const XROverlayManager&) = delete;
    XROverlayManager& operator=(const XROverlayManager&) = delete;

    /**
     * Create a new overlay
     * @param name Unique name for the overlay
     * @param config Overlay configuration
     * @return Pointer to created overlay, or nullptr on failure
     */
    XROverlay* CreateOverlay(const std::string& name, const OverlayConfig& config);

    /**
     * Get overlay by name
     */
    XROverlay* GetOverlay(const std::string& name);

    /**
     * Remove overlay
     */
    void RemoveOverlay(const std::string& name);

    /**
     * Get all visible overlay layers
     * @param reference_space Reference space for positioning
     * @param head_pose Current head pose
     * @param hand_poses Array of hand poses (left, right), can be nullptr
     * @return Vector of layer pointers for xrEndFrame
     */
    std::vector<XrCompositionLayerBaseHeader*> GetOverlayLayers(
        XrSpace reference_space,
        const XrPosef& head_pose,
        const XrPosef* hand_poses = nullptr);

    /**
     * Get number of overlays
     */
    size_t GetOverlayCount() const { return overlays_.size(); }

    /**
     * Set global overlay visibility
     */
    void SetAllVisible(bool visible);

    /**
     * Toggle global overlay visibility
     */
    void ToggleAllVisible();

    /**
     * Check if overlays are globally visible
     */
    bool AreOverlaysVisible() const { return global_visible_; }

private:
    /**
     * Get (lazily creating) the VIEW reference space for head-locked overlays
     */
    XrSpace GetOrCreateViewSpace();

    XRSession* session_;
    XRGraphicsBinding* graphics_;

    std::vector<std::pair<std::string, std::unique_ptr<XROverlay>>> overlays_;
    bool global_visible_ = true;

    // VIEW reference space for head-locked overlays (created on demand)
    XrSpace view_space_ = XR_NULL_HANDLE;
};

} // namespace XR
} // namespace OVRInject
