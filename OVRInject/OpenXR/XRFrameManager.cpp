#include "XRFrameManager.hpp"

namespace OVRInject {
namespace XR {

//-----------------------------------------------------------------------------
// Constructor / Destructor
//-----------------------------------------------------------------------------

XRFrameManager::XRFrameManager(XRSession* session)
    : session_(session)
{
}

XRFrameManager::~XRFrameManager() {
    // If a frame is in progress, we should end it
    // However, at destruction time, the session may already be invalid
    // Just reset state
    frame_in_progress_ = false;
    frame_begun_ = false;
}

//-----------------------------------------------------------------------------
// Frame Lifecycle
//-----------------------------------------------------------------------------

bool XRFrameManager::WaitFrame() {
    if (!session_ || !session_->IsCreated()) {
        LOGSTR("XRFrameManager: Session not created\n");
        return false;
    }

    // Cannot wait for a new frame if one is already in progress
    if (frame_in_progress_) {
        LOGSTR("XRFrameManager: Frame already in progress, call EndFrame first\n");
        return false;
    }

    XrSession session = session_->GetHandle();

    XrFrameWaitInfo waitInfo = {XR_TYPE_FRAME_WAIT_INFO};
    frame_state_ = {XR_TYPE_FRAME_STATE};

    XrResult result = xrWaitFrame(session, &waitInfo, &frame_state_);
    if (XR_FAILED(result)) {
        LOGSTRF("XRFrameManager: xrWaitFrame failed with result %d\n", result);
        return false;
    }

    frame_in_progress_ = true;
    frame_count_++;

    return true;
}

bool XRFrameManager::BeginFrame() {
    if (!session_ || !session_->IsCreated()) {
        LOGSTR("XRFrameManager: Session not created\n");
        return false;
    }

    if (!frame_in_progress_) {
        LOGSTR("XRFrameManager: No frame in progress, call WaitFrame first\n");
        return false;
    }

    if (frame_begun_) {
        LOGSTR("XRFrameManager: Frame already begun\n");
        return false;
    }

    XrSession session = session_->GetHandle();

    XrFrameBeginInfo beginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
    XrResult result = xrBeginFrame(session, &beginInfo);

    // XR_FRAME_DISCARDED is not an error - it means the runtime wants us to skip
    if (result == XR_FRAME_DISCARDED) {
        // Frame was discarded, but we still need to call EndFrame
        frame_begun_ = true;
        return true;
    }

    if (XR_FAILED(result)) {
        LOGSTRF("XRFrameManager: xrBeginFrame failed with result %d - resetting frame state\n", result);
        // Never leave a half-open frame: WaitFrame marked this frame as in
        // progress, so clear the flag here. Otherwise the next WaitFrame
        // would refuse ("frame already in progress") and wedge the frame loop.
        frame_in_progress_ = false;
        return false;
    }

    frame_begun_ = true;
    return true;
}

bool XRFrameManager::EndFrame(const std::vector<XrCompositionLayerBaseHeader*>& layers) {
    if (!session_ || !session_->IsCreated()) {
        LOGSTR("XRFrameManager: Session not created\n");
        return false;
    }

    if (!frame_in_progress_) {
        LOGSTR("XRFrameManager: No frame in progress\n");
        return false;
    }

    // Must call BeginFrame before EndFrame
    if (!frame_begun_) {
        LOGSTR("XRFrameManager: Frame not begun, call BeginFrame first\n");
        return false;
    }

    XrSession session = session_->GetHandle();

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frame_state_.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = static_cast<uint32_t>(layers.size());
    endInfo.layers = layers.empty() ? nullptr : layers.data();

    XrResult result = xrEndFrame(session, &endInfo);
    if (XR_FAILED(result)) {
        LOGSTRF("XRFrameManager: xrEndFrame failed with result %d\n", result);
        // Reset state even on failure to allow recovery
        frame_in_progress_ = false;
        frame_begun_ = false;
        return false;
    }

    frame_in_progress_ = false;
    frame_begun_ = false;

    return true;
}

bool XRFrameManager::EndFrameEmpty() {
    if (!session_ || !session_->IsCreated()) {
        LOGSTR("XRFrameManager: Session not created\n");
        return false;
    }

    if (!frame_in_progress_) {
        LOGSTR("XRFrameManager: No frame in progress\n");
        return false;
    }

    // If BeginFrame wasn't called, we need to call it before EndFrame
    if (!frame_begun_) {
        if (!BeginFrame()) {
            // Reset state to allow recovery
            frame_in_progress_ = false;
            return false;
        }
    }

    skipped_frame_count_++;

    // Submit with no layers
    std::vector<XrCompositionLayerBaseHeader*> emptyLayers;
    return EndFrame(emptyLayers);
}

} // namespace XR
} // namespace OVRInject
