#include "XROverlay.hpp"
#include <algorithm>
#include <cmath>

namespace OVRInject {
namespace XR {

namespace {

constexpr XrDuration kOverlayWaitTimeoutNs = 1 * 1000 * 1000;

}

//=============================================================================
// XROverlay Implementation
//=============================================================================

XROverlay::XROverlay(XRSession* session, XRGraphicsBinding* graphics, const OverlayConfig& config)
    : session_(session)
    , graphics_(graphics)
    , config_(config)
{
}

XROverlay::~XROverlay() {
    Shutdown();
}

bool XROverlay::Initialize() {
    if (!session_ || !session_->IsCreated()) {
        LOGSTR("XROverlay: Session not created\n");
        return false;
    }

    if (!graphics_) {
        LOGSTR("XROverlay: Graphics binding not provided\n");
        return false;
    }

    // Create swapchain for overlay rendering
    swapchain_ = std::make_unique<XRSwapchain>(session_, graphics_);

    if (!swapchain_->Create(config_.texture_width, config_.texture_height,
                            DXGI_FORMAT_R8G8B8A8_UNORM)) {
        LOGSTR("XROverlay: Failed to create swapchain\n");
        swapchain_.reset();
        return false;
    }

    // Initialize the layer structure
    layer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    layer_.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                        XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
    layer_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;

    LOGSTRF("XROverlay: Initialized (%ux%u)\n",
            config_.texture_width, config_.texture_height);
    return true;
}

void XROverlay::Shutdown() {
    if (swapchain_) {
        swapchain_->Destroy();
        swapchain_.reset();
    }
    LOGSTR("XROverlay: Shutdown\n");
}

//-----------------------------------------------------------------------------
// Rendering
//-----------------------------------------------------------------------------

ID3D11RenderTargetView* XROverlay::BeginRender() {
    if (!IsInitialized()) return nullptr;

    if (!AcquireImage()) return nullptr;

    ID3D11RenderTargetView* rtv = swapchain_->GetCurrentRTV();
    if (!rtv) {
        ReleaseImage();
        return nullptr;
    }
    return rtv;
}

void XROverlay::EndRender() {
    ReleaseImage();
}

ID3D11Texture2D* XROverlay::GetTexture() {
    if (!IsInitialized()) return nullptr;
    return swapchain_->GetCurrentTexture();
}

bool XROverlay::AcquireImage() {
    if (!IsInitialized()) return false;
    if (image_acquired_) return true;

    if (!swapchain_->AcquireImage(current_image_index_)) {
        return false;
    }

    if (!swapchain_->WaitImage(kOverlayWaitTimeoutNs)) {
        static bool loggedWaitTimeout = false;
        if (!loggedWaitTimeout) {
            LOGSTR("XROverlay: Swapchain wait timed out, skipping overlay render this frame\n");
            loggedWaitTimeout = true;
        }
        swapchain_->ReleaseImage();
        return false;
    }

    image_acquired_ = true;
    return true;
}

bool XROverlay::ReleaseImage() {
    if (!IsInitialized()) return false;
    if (!image_acquired_) return true;

    if (!swapchain_->ReleaseImage()) {
        return false;
    }

    image_acquired_ = false;
    return true;
}

//-----------------------------------------------------------------------------
// Composition Layer
//-----------------------------------------------------------------------------

XrCompositionLayerQuad* XROverlay::BuildLayer(XrSpace reference_space,
                                               const XrPosef& head_pose,
                                               const XrPosef* hand_pose,
                                               XrSpace view_space) {
    if (!IsInitialized()) return nullptr;
    if (!config_.visible) return nullptr;

    XrSpace layerSpace = reference_space;
    XrPosef pose;

    if (config_.placement == OverlayPlacement::HeadLocked && view_space != XR_NULL_HANDLE) {
        // Submit head-locked overlays in VIEW space: the compositor locks
        // the quad to the actual head pose at display time, eliminating
        // the one-frame swim caused by using the predicted head pose.
        layerSpace = view_space;
        pose = IdentityPose();
        pose.position = Float3ToXrVector3(config_.position_offset);
        pose.orientation = EulerToQuaternion(
            XMConvertToRadians(config_.rotation.x),
            XMConvertToRadians(config_.rotation.y),
            XMConvertToRadians(config_.rotation.z));
    } else {
        // Calculate pose based on placement mode
        pose = CalculatePose(head_pose, hand_pose);
    }

    // Build the layer
    layer_.space = layerSpace;
    layer_.subImage.swapchain = swapchain_->GetHandle();
    layer_.subImage.imageRect.offset = {0, 0};
    layer_.subImage.imageRect.extent.width = static_cast<int32_t>(config_.texture_width);
    layer_.subImage.imageRect.extent.height = static_cast<int32_t>(config_.texture_height);
    layer_.subImage.imageArrayIndex = 0;
    layer_.pose = pose;
    layer_.size.width = config_.width;
    layer_.size.height = config_.height;

    return &layer_;
}

XrPosef XROverlay::CalculatePose(const XrPosef& head_pose, const XrPosef* hand_pose) const {
    XrPosef result = IdentityPose();

    // Apply rotation from config
    result.orientation = EulerToQuaternion(
        XMConvertToRadians(config_.rotation.x),
        XMConvertToRadians(config_.rotation.y),
        XMConvertToRadians(config_.rotation.z)
    );

    switch (config_.placement) {
        case OverlayPlacement::HeadLocked: {
            // Position relative to head
            // Transform the offset by head orientation
            XMVECTOR headQuat = XMVectorSet(
                head_pose.orientation.x,
                head_pose.orientation.y,
                head_pose.orientation.z,
                head_pose.orientation.w
            );

            XMVECTOR offset = XMVectorSet(
                config_.position_offset.x,
                config_.position_offset.y,
                config_.position_offset.z,
                0.0f
            );

            // Rotate offset by head orientation
            XMVECTOR rotatedOffset = XMVector3Rotate(offset, headQuat);

            XMFLOAT3 rotOffset;
            XMStoreFloat3(&rotOffset, rotatedOffset);

            result.position.x = head_pose.position.x + rotOffset.x;
            result.position.y = head_pose.position.y + rotOffset.y;
            result.position.z = head_pose.position.z + rotOffset.z;

            // Combine head rotation with config rotation
            XMVECTOR configQuat = XMVectorSet(
                result.orientation.x,
                result.orientation.y,
                result.orientation.z,
                result.orientation.w
            );
            XMVECTOR combinedQuat = XMQuaternionMultiply(configQuat, headQuat);
            combinedQuat = XMQuaternionNormalize(combinedQuat);

            XMFLOAT4 quat;
            XMStoreFloat4(&quat, combinedQuat);
            result.orientation = {quat.x, quat.y, quat.z, quat.w};
            break;
        }

        case OverlayPlacement::WorldLocked: {
            // Fixed position in world space
            result.position.x = config_.position_offset.x;
            result.position.y = config_.position_offset.y;
            result.position.z = config_.position_offset.z;
            break;
        }

        case OverlayPlacement::HandAttached: {
            if (hand_pose) {
                // Position relative to hand
                XMVECTOR handQuat = XMVectorSet(
                    hand_pose->orientation.x,
                    hand_pose->orientation.y,
                    hand_pose->orientation.z,
                    hand_pose->orientation.w
                );

                XMVECTOR offset = XMVectorSet(
                    config_.position_offset.x,
                    config_.position_offset.y,
                    config_.position_offset.z,
                    0.0f
                );

                XMVECTOR rotatedOffset = XMVector3Rotate(offset, handQuat);

                XMFLOAT3 rotOffset;
                XMStoreFloat3(&rotOffset, rotatedOffset);

                result.position.x = hand_pose->position.x + rotOffset.x;
                result.position.y = hand_pose->position.y + rotOffset.y;
                result.position.z = hand_pose->position.z + rotOffset.z;

                // Combine hand rotation with config rotation
                XMVECTOR configQuat = XMVectorSet(
                    result.orientation.x,
                    result.orientation.y,
                    result.orientation.z,
                    result.orientation.w
                );
                XMVECTOR combinedQuat = XMQuaternionMultiply(configQuat, handQuat);
                combinedQuat = XMQuaternionNormalize(combinedQuat);

                XMFLOAT4 quat;
                XMStoreFloat4(&quat, combinedQuat);
                result.orientation = {quat.x, quat.y, quat.z, quat.w};
            } else {
                // Fallback to world position if no hand pose
                result.position.x = config_.position_offset.x;
                result.position.y = config_.position_offset.y;
                result.position.z = config_.position_offset.z;
            }
            break;
        }
    }

    return result;
}

XrQuaternionf XROverlay::EulerToQuaternion(float pitch, float yaw, float roll) const {
    // Convert euler angles (in radians) to quaternion
    float cy = cosf(yaw * 0.5f);
    float sy = sinf(yaw * 0.5f);
    float cp = cosf(pitch * 0.5f);
    float sp = sinf(pitch * 0.5f);
    float cr = cosf(roll * 0.5f);
    float sr = sinf(roll * 0.5f);

    XrQuaternionf q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;

    return q;
}

//=============================================================================
// XROverlayManager Implementation
//=============================================================================

XROverlayManager::XROverlayManager(XRSession* session, XRGraphicsBinding* graphics)
    : session_(session)
    , graphics_(graphics)
{
}

XROverlayManager::~XROverlayManager() {
    overlays_.clear();

    // Session still alive here (manager is destroyed before the session)
    if (view_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(view_space_);
        view_space_ = XR_NULL_HANDLE;
    }
}

XROverlay* XROverlayManager::CreateOverlay(const std::string& name, const OverlayConfig& config) {
    // Check if overlay with this name already exists
    for (const auto& pair : overlays_) {
        if (pair.first == name) {
            LOGSTRF("XROverlayManager: Overlay '%s' already exists\n", name.c_str());
            return pair.second.get();
        }
    }

    // Create new overlay
    auto overlay = std::make_unique<XROverlay>(session_, graphics_, config);
    if (!overlay->Initialize()) {
        LOGSTRF("XROverlayManager: Failed to create overlay '%s'\n", name.c_str());
        return nullptr;
    }

    XROverlay* ptr = overlay.get();
    overlays_.emplace_back(name, std::move(overlay));

    // Sort by sort order
    std::sort(overlays_.begin(), overlays_.end(),
        [](const auto& a, const auto& b) {
            return a.second->GetConfig().sort_order < b.second->GetConfig().sort_order;
        });

    LOGSTRF("XROverlayManager: Created overlay '%s'\n", name.c_str());
    return ptr;
}

XrSpace XROverlayManager::GetOrCreateViewSpace() {
    if (view_space_ != XR_NULL_HANDLE) {
        return view_space_;
    }

    if (!session_ || !session_->IsCreated()) {
        return XR_NULL_HANDLE;
    }

    XrReferenceSpaceCreateInfo createInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    createInfo.poseInReferenceSpace = IdentityPose();

    XrResult result = xrCreateReferenceSpace(session_->GetHandle(), &createInfo, &view_space_);
    if (XR_FAILED(result)) {
        LOGSTRF("XROverlayManager: Failed to create VIEW reference space (result: %d)\n", result);
        view_space_ = XR_NULL_HANDLE;
    }

    return view_space_;
}

XROverlay* XROverlayManager::GetOverlay(const std::string& name) {
    for (auto& pair : overlays_) {
        if (pair.first == name) {
            return pair.second.get();
        }
    }
    return nullptr;
}

void XROverlayManager::RemoveOverlay(const std::string& name) {
    overlays_.erase(
        std::remove_if(overlays_.begin(), overlays_.end(),
            [&name](const auto& pair) { return pair.first == name; }),
        overlays_.end()
    );
}

std::vector<XrCompositionLayerBaseHeader*> XROverlayManager::GetOverlayLayers(
    XrSpace reference_space,
    const XrPosef& head_pose,
    const XrPosef* hand_poses) {

    std::vector<XrCompositionLayerBaseHeader*> layers;

    if (!global_visible_) {
        return layers;
    }

    for (auto& pair : overlays_) {
        XROverlay* overlay = pair.second.get();
        if (!overlay || !overlay->IsVisible()) continue;

        // Determine which hand pose to use for hand-attached overlays
        const XrPosef* hand_pose = nullptr;
        if (hand_poses && overlay->GetConfig().placement == OverlayPlacement::HandAttached) {
            // Use left hand by default for hand-attached overlays
            hand_pose = &hand_poses[0];
        }

        // Head-locked overlays are submitted in VIEW space (swim-free)
        XrSpace viewSpace = XR_NULL_HANDLE;
        if (overlay->GetConfig().placement == OverlayPlacement::HeadLocked) {
            viewSpace = GetOrCreateViewSpace();
        }

        XrCompositionLayerQuad* layer = overlay->BuildLayer(reference_space, head_pose, hand_pose, viewSpace);
        if (layer) {
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(layer));
        }
    }

    return layers;
}

void XROverlayManager::SetAllVisible(bool visible) {
    global_visible_ = visible;
}

void XROverlayManager::ToggleAllVisible() {
    global_visible_ = !global_visible_;
}

} // namespace XR
} // namespace OVRInject
