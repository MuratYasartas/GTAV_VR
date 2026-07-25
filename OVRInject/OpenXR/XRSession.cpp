#include "XRSession.hpp"

namespace OVRInject {
namespace XR {

XRSession::XRSession(XRInstance* instance, XRGraphicsBinding* graphics)
    : instance_(instance)
    , graphics_(graphics) {
}

XRSession::~XRSession() {
    Destroy();
}

bool XRSession::Create() {
    if (session_ != XR_NULL_HANDLE) {
        LOGSTRF("Session already created\n");
        return true;
    }

    if (!instance_ || !instance_->IsInitialized()) {
        LOGSTRF("Cannot create session: instance not initialized\n");
        return false;
    }

    if (!graphics_ || !graphics_->IsInitialized()) {
        LOGSTRF("Cannot create session: graphics binding not initialized\n");
        return false;
    }

    LOGSTRF("Creating OpenXR session...\n");

    XrSessionCreateInfo createInfo = {XR_TYPE_SESSION_CREATE_INFO};
    createInfo.next = &graphics_->GetBindingInfo();
    createInfo.systemId = instance_->GetSystemId();

    XrResult result = xrCreateSession(instance_->GetHandle(), &createInfo, &session_);
    if (XR_FAILED(result)) {
        LOGSTRF("xrCreateSession failed: %s\n", GetResultString(result));
        return false;
    }

    current_state_ = SessionState::Idle;
    LOGSTRF("OpenXR session created successfully\n");
    return true;
}

void XRSession::Destroy() {
    if (session_ != XR_NULL_HANDLE) {
        LOGSTRF("Destroying OpenXR session...\n");

        if (session_running_) {
            xrEndSession(session_);
            session_running_ = false;
        }

        xrDestroySession(session_);
        session_ = XR_NULL_HANDLE;
    }

    current_state_ = SessionState::Unknown;
}

bool XRSession::BeginSession() {
    if (session_ == XR_NULL_HANDLE) {
        LOGSTRF("Cannot begin session: session not created\n");
        return false;
    }

    if (session_running_) {
        LOGSTRF("Session already running\n");
        return true;
    }

    LOGSTRF("Beginning OpenXR session...\n");

    XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
    beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    XrResult result = xrBeginSession(session_, &beginInfo);
    if (XR_FAILED(result)) {
        LOGSTRF("xrBeginSession failed: %s\n", GetResultString(result));
        return false;
    }

    session_running_ = true;
    LOGSTRF("OpenXR session begun\n");
    return true;
}

bool XRSession::EndSession() {
    if (session_ == XR_NULL_HANDLE) {
        return false;
    }

    if (!session_running_) {
        return true;
    }

    LOGSTRF("Ending OpenXR session...\n");

    XrResult result = xrEndSession(session_);
    if (XR_FAILED(result)) {
        LOGSTRF("xrEndSession failed: %s\n", GetResultString(result));
        return false;
    }

    session_running_ = false;
    LOGSTRF("OpenXR session ended\n");
    return true;
}

void XRSession::RequestExit() {
    if (session_ != XR_NULL_HANDLE) {
        xrRequestExitSession(session_);
    }
}

bool XRSession::IsRunning() const {
    SessionState state = current_state_;
    return state == SessionState::Synchronized ||
           state == SessionState::Visible ||
           state == SessionState::Focused;
}

bool XRSession::IsFocused() const {
    return current_state_ == SessionState::Focused;
}

bool XRSession::ShouldRender() const {
    SessionState state = current_state_;
    return state == SessionState::Visible || state == SessionState::Focused;
}

void XRSession::PollEvents() {
    if (instance_ == nullptr || !instance_->IsInitialized()) {
        return;
    }

    // Heartbeat while stuck pre-SYNCHRONIZED: the log must prove event
    // pumping is alive, so "runtime never sends READY" (HMD idle, another
    // app holds focus, runtime quirk) is distinguishable from "our frame
    // loop never polls". ~every 5s at 60 Hz.
    if (current_state_ != SessionState::Synchronized &&
        current_state_ != SessionState::Visible &&
        current_state_ != SessionState::Focused) {
        static int heartbeatFrames = 0;
        if ((heartbeatFrames++ % 300) == 0) {
            LOGSTRF("OpenXR: waiting for session READY (state=%s, poll #%d)\n",
                    SessionStateToString(current_state_), heartbeatFrames);
        }
    }

    XrEventDataBuffer eventData = {XR_TYPE_EVENT_DATA_BUFFER};

    while (true) {
        eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
        XrResult result = xrPollEvent(instance_->GetHandle(), &eventData);

        if (result == XR_EVENT_UNAVAILABLE) {
            // No more events
            break;
        }

        if (XR_FAILED(result)) {
            LOGSTRF("xrPollEvent failed: %s\n", GetResultString(result));
            break;
        }

        switch (eventData.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
                HandleSessionStateChange(stateEvent->state);
                break;
            }

            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                HandleInstanceLossPending();
                break;
            }

            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
                LOGSTRF("OpenXR: Interaction profile changed\n");
                break;
            }

            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
                LOGSTRF("OpenXR: Reference space change pending\n");
                break;
            }

            default:
                LOGSTRF("OpenXR: Unhandled event type %d\n", eventData.type);
                break;
        }
    }
}

void XRSession::HandleSessionStateChange(XrSessionState newState) {
    SessionState oldState = current_state_;
    SessionState newSessionState = FromXrSessionState(newState);

    LOGSTRF("OpenXR session state: %s -> %s\n",
            SessionStateToString(oldState),
            SessionStateToString(newSessionState));

    current_state_ = newSessionState;

    // Automatic state handling
    switch (newSessionState) {
        case SessionState::Ready:
            // Automatically begin session when ready.
            // Per the OpenXR spec, xrAttachSessionActionSets must be called
            // BEFORE xrBeginSession, so the pre-begin callback (which attaches
            // action sets) runs first; only then do we begin the session.
            if (!session_running_) {
                if (pre_begin_callback_) {
                    LOGSTR("OpenXR session ready: running pre-begin step (action set attach)\n");
                    pre_begin_callback_();
                }
                BeginSession();
            }
            break;

        case SessionState::Stopping:
            // Automatically end session when stopping
            if (session_running_) {
                EndSession();
            }
            break;

        case SessionState::Exiting:
            // Session is exiting
            LOGSTRF("OpenXR session exiting\n");
            break;

        case SessionState::LossPending:
            // Runtime loss pending
            LOGSTRF("OpenXR runtime loss pending\n");
            break;

        default:
            break;
    }

    // Notify callback
    if (state_callback_) {
        state_callback_(oldState, newSessionState);
    }
}

void XRSession::HandleInstanceLossPending() {
    LOGSTRF("OpenXR instance loss pending - runtime is shutting down\n");

    // The runtime is about to be lost
    // Give the application time to clean up
    current_state_ = SessionState::LossPending;

    if (state_callback_) {
        state_callback_(current_state_, SessionState::LossPending);
    }
}

} // namespace XR
} // namespace OVRInject
