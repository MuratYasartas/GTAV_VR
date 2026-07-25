#pragma once

#include "XRCore.hpp"
#include "XRInstance.hpp"
#include "XRGraphicsBinding.hpp"
#include <functional>
#include <atomic>

namespace OVRInject {
namespace XR {

/**
 * XRSession - Manages OpenXR session lifecycle and state machine
 *
 * OpenXR Session State Machine:
 *   IDLE -> READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED
 *                                              |
 *                                              v
 *                                          STOPPING -> IDLE
 *
 * Also handles:
 *   - Session creation with D3D11 graphics binding
 *   - Event polling and state transitions
 *   - Session begin/end lifecycle
 */
class XRSession {
public:
    using StateChangeCallback = std::function<void(SessionState oldState, SessionState newState)>;
    // Invoked when the session reaches READY, before xrBeginSession is called.
    using PreBeginCallback = std::function<void()>;

    XRSession(XRInstance* instance, XRGraphicsBinding* graphics);
    ~XRSession();

    // Disable copy
    XRSession(const XRSession&) = delete;
    XRSession& operator=(const XRSession&) = delete;

    /**
     * Create the OpenXR session
     * @return true on success
     */
    bool Create();

    /**
     * Destroy the session
     */
    void Destroy();

    /**
     * Begin the session (call when state becomes READY)
     * @return true on success
     */
    bool BeginSession();

    /**
     * End the session (call when state becomes STOPPING)
     * @return true on success
     */
    bool EndSession();

    /**
     * Request session exit
     */
    void RequestExit();

    //-------------------------------------------------------------------------
    // State Management
    //-------------------------------------------------------------------------

    /**
     * Get current session state
     */
    SessionState GetCurrentState() const { return current_state_; }

    /**
     * Check if session is in a running state (SYNCHRONIZED, VISIBLE, or FOCUSED)
     */
    bool IsRunning() const;

    /**
     * Check if session has input focus
     */
    bool IsFocused() const;

    /**
     * Check if session should render (VISIBLE or FOCUSED)
     */
    bool ShouldRender() const;

    /**
     * Check if session is created
     */
    bool IsCreated() const { return session_ != XR_NULL_HANDLE; }

    /**
     * Check if xrBeginSession has succeeded. xrWaitFrame/xrBeginFrame are only
     * legal after this point - and from then on they must run EVERY frame so
     * the runtime's session state machine can advance past READY.
     */
    bool IsBegun() const { return session_running_; }

    //-------------------------------------------------------------------------
    // Event Processing
    //-------------------------------------------------------------------------

    /**
     * Poll and process OpenXR events
     * Should be called each frame
     */
    void PollEvents();

    /**
     * Set callback for state changes
     */
    void SetStateChangeCallback(StateChangeCallback callback) {
        state_callback_ = callback;
    }

    /**
     * Set callback invoked on READY, before the session is begun.
     * The OpenXR spec requires xrAttachSessionActionSets to be called
     * BEFORE xrBeginSession, so action-set attachment happens here.
     */
    void SetPreBeginCallback(PreBeginCallback callback) {
        pre_begin_callback_ = callback;
    }

    //-------------------------------------------------------------------------
    // Accessors
    //-------------------------------------------------------------------------

    XrSession GetHandle() const { return session_; }
    XrInstance GetInstanceHandle() const { return instance_->GetHandle(); }

private:
    /**
     * Handle session state change event
     */
    void HandleSessionStateChange(XrSessionState newState);

    /**
     * Handle instance loss pending event
     */
    void HandleInstanceLossPending();

    //-------------------------------------------------------------------------
    // Members
    //-------------------------------------------------------------------------

    XRInstance* instance_;
    XRGraphicsBinding* graphics_;

    XrSession session_ = XR_NULL_HANDLE;
    std::atomic<SessionState> current_state_{SessionState::Unknown};
    bool session_running_ = false;

    StateChangeCallback state_callback_;
    PreBeginCallback pre_begin_callback_;
};

} // namespace XR
} // namespace OVRInject
