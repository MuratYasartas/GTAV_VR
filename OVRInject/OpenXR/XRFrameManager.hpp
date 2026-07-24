#pragma once

#include "XRCore.hpp"
#include "XRSession.hpp"
#include <vector>

namespace OVRInject {
namespace XR {

/**
 * XRFrameManager - Manages the OpenXR frame timing loop
 *
 * Handles the frame lifecycle:
 * - WaitFrame: Synchronize with compositor, get predicted display time
 * - BeginFrame: Signal start of rendering
 * - EndFrame: Submit composition layers to the compositor
 *
 * OpenXR requires strict frame lifecycle:
 * WaitFrame -> BeginFrame -> [render] -> EndFrame
 */
class XRFrameManager {
public:
    XRFrameManager(XRSession* session);
    ~XRFrameManager();

    // Disable copy
    XRFrameManager(const XRFrameManager&) = delete;
    XRFrameManager& operator=(const XRFrameManager&) = delete;

    //-------------------------------------------------------------------------
    // Frame Lifecycle
    //-------------------------------------------------------------------------

    /**
     * Wait for the next frame
     * Blocks until the compositor is ready for a new frame
     *
     * @return true on success
     */
    bool WaitFrame();

    /**
     * Begin the frame
     * Call after WaitFrame, before rendering
     *
     * @return true on success
     */
    bool BeginFrame();

    /**
     * End the frame and submit composition layers
     *
     * @param layers Array of composition layer pointers to submit
     * @return true on success
     */
    bool EndFrame(const std::vector<XrCompositionLayerBaseHeader*>& layers);

    /**
     * End the frame with no layers (skip rendering)
     * Use when shouldRender is false
     *
     * @return true on success
     */
    bool EndFrameEmpty();

    //-------------------------------------------------------------------------
    // Frame State
    //-------------------------------------------------------------------------

    /**
     * Get the predicted display time for this frame
     * Use this for xrLocateViews and pose prediction
     */
    XrTime GetPredictedDisplayTime() const { return frame_state_.predictedDisplayTime; }

    /**
     * Get the predicted display period (frame interval)
     */
    XrDuration GetPredictedDisplayPeriod() const { return frame_state_.predictedDisplayPeriod; }

    /**
     * Check if we should render this frame
     * May be false if the HMD is sleeping or not visible
     */
    bool ShouldRender() const { return frame_state_.shouldRender == XR_TRUE; }

    /**
     * Check if a frame has been waited for but not yet ended
     */
    bool IsFrameInProgress() const { return frame_in_progress_; }

    /**
     * Check if frame has been begun (between BeginFrame and EndFrame)
     */
    bool IsFrameBegun() const { return frame_begun_; }

    //-------------------------------------------------------------------------
    // Statistics
    //-------------------------------------------------------------------------

    /**
     * Get total frames processed
     */
    uint64_t GetFrameCount() const { return frame_count_; }

    /**
     * Get number of frames skipped (shouldRender was false)
     */
    uint64_t GetSkippedFrameCount() const { return skipped_frame_count_; }

private:
    XRSession* session_;

    // Frame state from WaitFrame
    XrFrameState frame_state_ = {XR_TYPE_FRAME_STATE};

    // Frame lifecycle tracking
    bool frame_in_progress_ = false;  // Between WaitFrame and EndFrame
    bool frame_begun_ = false;         // Between BeginFrame and EndFrame

    // Statistics
    uint64_t frame_count_ = 0;
    uint64_t skipped_frame_count_ = 0;
};

} // namespace XR
} // namespace OVRInject
