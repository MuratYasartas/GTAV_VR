#include "XRHMDSupport.hpp"
#include "../Log.hpp"
#include "../Stereo/ComfortRuntime.hpp"
#include "../VR/SharedSettings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <Windows.h>

namespace OVRInject {

namespace {

constexpr XrDuration kSwapchainWaitTimeoutNs = 1 * 1000 * 1000;

DXGI_FORMAT NormalizeSwapchainFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
        return format;
    }
}

bool ReadEnvFlag(const char* name, bool defaultValue) {
    char value[8] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    if (len == 0 || len >= sizeof(value)) {
        return defaultValue;
    }
    char c = static_cast<char>(tolower(static_cast<unsigned char>(value[0])));
    return c == '1' || c == 'y' || c == 't';
}

void LogRenderScaleClamp(const char* owner,
                         float requestedScale,
                         float effectiveScale,
                         uint32_t baseWidth,
                         uint32_t baseHeight) {
    if (std::fabs(requestedScale - effectiveScale) < 0.001f) {
        return;
    }

    LOGSTRF("%s: Requested render scale %.2f clamped to %.2f for %ux%u eye resolution\n",
            owner, requestedScale, effectiveScale, baseWidth, baseHeight);
}

} // namespace

//-----------------------------------------------------------------------------
// Singleton
//-----------------------------------------------------------------------------

XRHMDSupport* XRHMDSupport::singleton_ = nullptr;

XRHMDSupport* XRHMDSupport::Singleton() {
    if (!singleton_) {
        singleton_ = new XRHMDSupport();
    }
    return singleton_;
}

//-----------------------------------------------------------------------------
// Constructor / Destructor
//-----------------------------------------------------------------------------

XRHMDSupport::XRHMDSupport() {
    // Initialize projection layer structure
    projection_layer_ = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};

    // Initialize projection views
    for (auto& view : projection_views_) {
        view = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    }
}

XRHMDSupport::~XRHMDSupport() {
    Shutdown();

    if (singleton_ == this) {
        singleton_ = nullptr;
    }
}

//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------

bool XRHMDSupport::Initialize(IDXGISwapChain* swap_chain, ID3D11Device* device) {
    LOGSTR("XRHMDSupport: Initializing OpenXR...\n");

    if (!device) {
        LOGSTR("XRHMDSupport: D3D11 device is null\n");
        return false;
    }

    swap_chain_ = swap_chain;
    device_ = device;
    device_->GetImmediateContext(&device_context_);

    // Create XRInstance
    instance_ = std::make_unique<XR::XRInstance>();
    if (!instance_->Initialize("GTA VR")) {
        LOGSTR("XRHMDSupport: Failed to initialize OpenXR instance\n");
        return false;
    }

    // OpenXR requires querying graphics requirements before session creation.
    D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    LUID requiredLuid = {};
    if (!instance_->GetD3D11GraphicsRequirements(minFeatureLevel, requiredLuid)) {
        LOGSTR("XRHMDSupport: Failed to query D3D11 graphics requirements\n");
        return false;
    }

    D3D_FEATURE_LEVEL deviceFeature = device_->GetFeatureLevel();
    if (deviceFeature < minFeatureLevel) {
        LOGSTRF("XRHMDSupport: Device feature level 0x%x < required 0x%x\n",
                deviceFeature, minFeatureLevel);
    }

    bool hasDeviceLuid = false;
    LUID deviceLuid = {};
    IDXGIDevice* dxgiDevice = nullptr;
    if (SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter) {
            DXGI_ADAPTER_DESC desc = {};
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                deviceLuid = desc.AdapterLuid;
                hasDeviceLuid = true;
                LOGSTRF("XRHMDSupport: Using adapter %ls\n", desc.Description);
            }
            adapter->Release();
        }
        dxgiDevice->Release();
    }

    if (hasDeviceLuid) {
        if (deviceLuid.HighPart != requiredLuid.HighPart ||
            deviceLuid.LowPart != requiredLuid.LowPart) {
            LOGSTRF("XRHMDSupport: Adapter LUID mismatch (device %08lx-%08lx vs required %08lx-%08lx)\n",
                    deviceLuid.HighPart, deviceLuid.LowPart,
                    requiredLuid.HighPart, requiredLuid.LowPart);
        }
    }

    // Create graphics binding
    graphics_ = std::make_unique<XR::XRGraphicsBinding>();
    if (!graphics_->Initialize(device_)) {
        LOGSTR("XRHMDSupport: Failed to initialize graphics binding\n");
        return false;
    }

    // Create session
    session_ = std::make_unique<XR::XRSession>(instance_.get(), graphics_.get());
    session_->SetStateChangeCallback([this](XR::SessionState oldState, XR::SessionState newState) {
        OnSessionStateChanged(oldState, newState);
    });
    session_->SetPreBeginCallback([this]() {
        // OpenXR requires xrAttachSessionActionSets BEFORE xrBeginSession.
        if (action_manager_) {
            LOGSTR("XRHMDSupport: Attaching action sets to session (pre-begin)\n");
            if (!action_manager_->AttachToSession()) {
                LOGSTR("XRHMDSupport: Failed to attach action sets; controllers will not work\n");
            }
        }
    });

    if (!session_->Create()) {
        LOGSTR("XRHMDSupport: Failed to create session\n");
        return false;
    }

    // Create view manager
    view_manager_ = std::make_unique<XR::XRViewManager>(instance_.get(), session_.get());
    if (!view_manager_->Initialize()) {
        LOGSTR("XRHMDSupport: Failed to initialize view manager\n");
        return false;
    }

    // Create swapchains with recommended resolution, clamped the same way as
    // the runtime recreate path (ComputeSafeRenderSize): the raw recommended
    // size (e.g. 5424x5356 on a Crystal Super) exceeds the eye-texture clamp,
    // which would force the box-copy path AND leave a stale strip of every
    // swapchain image unwritten.
    uint32_t baseWidth = view_manager_->GetRecommendedWidth();
    uint32_t baseHeight = view_manager_->GetRecommendedHeight();
    uint32_t width = 0, height = 0;
    VR::ComputeSafeRenderSize(baseWidth, baseHeight, 1.0f, render_scale_, width, height);
    if (width != baseWidth || height != baseHeight) {
        LOGSTRF("XRHMDSupport: Initial swapchain size clamped %ux%u -> %ux%u (scale %.2f)\n",
                baseWidth, baseHeight, width, height, render_scale_);
    }

    swapchains_ = std::make_unique<XR::XRStereoSwapchains>(session_.get(), graphics_.get());
    bool created = false;
    if (swapchain_format_ != DXGI_FORMAT_UNKNOWN) {
        created = swapchains_->Create(width, height, swapchain_format_);
    } else {
        created = swapchains_->Create(width, height);
    }
    if (!created) {
        LOGSTR("XRHMDSupport: Failed to create swapchains\n");
        return false;
    }
    swapchain_width_ = width;
    swapchain_height_ = height;
    render_scale_ = 1.0f;

    // Create frame manager
    frame_manager_ = std::make_unique<XR::XRFrameManager>(session_.get());

    // Create action manager (before session becomes ready)
    action_manager_ = std::make_unique<XR::XRActionManager>(instance_.get(), session_.get());
    if (!action_manager_->Initialize()) {
        LOGSTR("XRHMDSupport: Failed to initialize action manager\n");
        return false;
    }

    // Create overlay manager
    overlay_manager_ = std::make_unique<XR::XROverlayManager>(session_.get(), graphics_.get());

    // Create settings overlay + UI
    if (overlay_manager_) {
        XR::OverlayConfig config;
        config.width = 0.84f;
        config.height = 0.56f;
        config.texture_width = 1536;
        config.texture_height = 1024;
        config.placement = XR::OverlayPlacement::HeadLocked;
        config.position_offset = {0.0f, 0.0f, -1.1f};
        config.opacity = 1.0f;
        config.sort_order = 100;

        settings_overlay_ = overlay_manager_->CreateOverlay("settings", config);
        overlay_ui_ = std::make_unique<XR::XROverlayUI>();

        if (settings_overlay_ && overlay_ui_->Initialize(device_, settings_overlay_)) {
            overlay_ui_->LoadSettings();
            ApplyOverlaySettings(overlay_ui_->GetSettings());
            auto envFlag = [](const char* name) {
                char value[8] = {};
                DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
                if (len == 0 || len >= sizeof(value)) {
                    return false;
                }
                char c = static_cast<char>(tolower(value[0]));
                return c == '1' || c == 'y' || c == 't';
            };

            bool overlayVisible = ReadEnvFlag("GTAVR_OVERLAY", false) ||
                                  ReadEnvFlag("GTAVR_OVERLAY_VISIBLE", false);
            if (envFlag("GTAVR_OVERLAY_HIDDEN")) {
                overlayVisible = false;
            }
            overlay_ui_->SetVisible(overlayVisible);
            LOGSTRF("XRHMDSupport: Overlay initially %s - toggle with Delete/Insert/F10 or controller menu button\n",
                    overlayVisible ? "visible" : "hidden");
            overlay_ui_->SetOnSettingsChanged([this](const XR::VRSettings& settings) {
                ApplyOverlaySettings(settings);
            });

            if (overlay_manager_) {
                overlay_manager_->SetAllVisible(overlayVisible);
            }
        }
    }

    // Create controllers
    left_controller_ = std::make_unique<XRTrackedController>(XR::Hand::Left);
    right_controller_ = std::make_unique<XRTrackedController>(XR::Hand::Right);

    LOGSTRF("XRHMDSupport: Initialized successfully\n");
    LOGSTRF("  Resolution: %ux%u per eye\n", width, height);
    LOGSTRF("  System: %s\n", instance_->GetSystemName());

    return true;
}

void XRHMDSupport::Shutdown() {
    LOGSTR("XRHMDSupport: Shutting down...\n");

    // Cleanup in reverse order
    overlay_ui_.reset();
    settings_overlay_ = nullptr;
    overlay_manager_.reset();
    action_manager_.reset();
    frame_manager_.reset();
    swapchains_.reset();
    view_manager_.reset();
    session_.reset();
    graphics_.reset();
    instance_.reset();

    left_controller_.reset();
    right_controller_.reset();

    if (device_context_) {
        device_context_->Release();
        device_context_ = nullptr;
    }

    device_ = nullptr;
    swap_chain_ = nullptr;

    LOGSTR("XRHMDSupport: Shutdown complete\n");
}

bool XRHMDSupport::IsInitialized() const {
    return instance_ && instance_->IsInitialized() &&
           session_ && session_->IsCreated();
}

//-----------------------------------------------------------------------------
// Session State
//-----------------------------------------------------------------------------

void XRHMDSupport::OnSessionStateChanged(XR::SessionState oldState, XR::SessionState newState) {
    LOGSTRF("XRHMDSupport: Session state changed: %s -> %s\n",
            XR::SessionStateToString(oldState),
            XR::SessionStateToString(newState));

    switch (newState) {
        case XR::SessionState::Ready:
            // Action sets are attached via the session's pre-begin callback
            // (OpenXR requires attach BEFORE xrBeginSession); XRSession then
            // begins the session itself. Nothing to do here.
            break;

        case XR::SessionState::Stopping:
            // End the session
            session_->EndSession();
            break;

        case XR::SessionState::Exiting:
        case XR::SessionState::LossPending:
            // Handle session loss
            break;

        default:
            break;
    }
}

//-----------------------------------------------------------------------------
// Frame Submission
//-----------------------------------------------------------------------------

bool XRHMDSupport::BeginFrame() {
    if (!IsInitialized()) return false;

    if (pending_swapchain_resize_ && view_manager_) {
        float requestedScale = pending_render_scale_;
        pending_swapchain_resize_ = false;

        uint32_t baseWidth = view_manager_->GetRecommendedWidth();
        uint32_t baseHeight = view_manager_->GetRecommendedHeight();
        uint32_t width = 0;
        uint32_t height = 0;
        VR::ComputeSafeRenderSize(baseWidth, baseHeight, requestedScale, render_scale_, width, height);
        LogRenderScaleClamp("XRHMDSupport", requestedScale, render_scale_, baseWidth, baseHeight);

        if (!RecreateSwapchains(width, height)) {
            LOGSTR("XRHMDSupport: Failed to recreate swapchains for render scale\n");
        } else {
            LOGSTRF("XRHMDSupport: Render scale %.2f -> swapchains %ux%u\n",
                render_scale_, width, height);
        }
    }

    // Poll events
    session_->PollEvents();

    // Keyboard overlay toggle: polled unconditionally, even when the
    // session is not renderable (parity with the OpenVR path's
    // Delete/Insert), so the menu can always be opened with dead or
    // unmapped controllers.
    PollOverlayKeyboardToggle();
    // Frame-loop gate: xrWaitFrame is only legal after xrBeginSession, and
    // from then on it must run EVERY frame - the runtime's session state
    // machine (READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED) advances FROM
    // frame-loop activity. Gating on visibility here starves the runtime and
    // wedges the session at READY forever (observed live). When the session
    // is not renderable, frameState.shouldRender=false drives the existing
    // EndFrameEmpty path below instead - the loop keeps pacing.
    if (!session_->IsBegun()) {
        static int notBegunFrames = 0;
        if ((notBegunFrames++ % 600) == 0) {
            LOGDBGF("XRHMDSupport: session not begun yet (state=%d) - pass-through frame\n",
                    static_cast<int>(session_->GetCurrentState()));
        }
        return false;
    }

    // Idle-headset throttle: with the HMD off, the session sits at
    // SYNCHRONIZED/VISIBLE (never FOCUSED) and some runtimes (Pimax, observed
    // live 2026-07-26) then throttle xrWaitFrame hard - and since our frame
    // loop runs on the GAME's render thread, the game itself gets strangled
    // (story-mode load crawled from ~2 min to >25 min with the headset
    // asleep). Pump the loop at a reduced cadence in those states so the
    // session state machine keeps advancing without throttling the game.
    // IDLE/READY and FOCUSED run at full rate: early transitions need the
    // pump, and FOCUSED is the normal play path.
    {
        const XR::SessionState st = session_->GetCurrentState();
        if (st == XR::SessionState::Synchronized || st == XR::SessionState::Visible) {
            static uint64_t lastPumpMs = 0;
            const uint64_t nowMs = GetTickCount64();
            if (nowMs - lastPumpMs < 100) {
                return false;  // game pass-through frame
            }
            lastPumpMs = nowMs;
        }
    }

    // Wait for frame
    if (!frame_manager_->WaitFrame()) {
        return false;
    }

    // Begin frame
    if (!frame_manager_->BeginFrame()) {
        return false;
    }

    should_render_ = frame_manager_->ShouldRender();
    frame_in_progress_ = true;
    views_valid_ = false;

    if (!should_render_) {
        frame_manager_->EndFrameEmpty();
        frame_in_progress_ = false;
        return false;
    }

    // Update view poses
    if (should_render_) {
        XrViewState viewState;
        XrTime displayTime = frame_manager_->GetPredictedDisplayTime();

        if (!view_manager_->LocateViews(displayTime, viewState)) {
            frame_manager_->EndFrameEmpty();
            frame_in_progress_ = false;
            should_render_ = false;
            return false;
        }

        views_valid_ = true;

        // Update head matrix
        XMMATRIX headPose = view_manager_->GetHeadPoseMatrix();
        head_matrix_ = Matrix4(headPose);

        // Update controller poses
        if (action_manager_) {
            action_manager_->SyncActions();
            action_manager_->UpdateControllerStates(displayTime, view_manager_->GetReferenceSpace());

            UpdateOverlayUI(action_manager_->GetLeftState(), action_manager_->GetRightState());
        }
    }

    return should_render_;
}

void XRHMDSupport::EndFrame() {
    if (!frame_in_progress_) return;

    std::vector<XrCompositionLayerBaseHeader*> layers;

    if (should_render_ && swapchains_) {
        RenderOverlayUI();

        // Build projection layer
        projection_layer_.space = view_manager_->GetRenderSpace();
        projection_layer_.viewCount = 2;
        projection_layer_.views = projection_views_.data();

        // Set up projection views for each eye
        for (size_t i = 0; i < 2; ++i) {
            XR::Eye eye = static_cast<XR::Eye>(i);
            const auto& viewInfo = view_manager_->GetView(eye);
            auto* swapchain = swapchains_->GetSwapchain(eye);

            projection_views_[i].pose = viewInfo.view.pose;
            projection_views_[i].fov = viewInfo.view.fov;
            projection_views_[i].subImage.swapchain = swapchain->GetHandle();
            projection_views_[i].subImage.imageRect.offset = {0, 0};
            projection_views_[i].subImage.imageRect.extent.width =
                static_cast<int32_t>(swapchain->GetWidth());
            projection_views_[i].subImage.imageRect.extent.height =
                static_cast<int32_t>(swapchain->GetHeight());
            projection_views_[i].subImage.imageArrayIndex = 0;
        }

        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection_layer_));

        // Add overlay layers
        if (overlay_manager_) {
            XrPosef headPose = view_manager_ ? view_manager_->GetHeadPose() : XR::IdentityPose();

            auto overlayLayers = overlay_manager_->GetOverlayLayers(
                view_manager_->GetReferenceSpace(), headPose, nullptr);

            for (auto* layer : overlayLayers) {
                layers.push_back(layer);
            }
        }
    }

    // End frame
    frame_manager_->EndFrame(layers);

    frame_in_progress_ = false;
    should_render_ = false;
}

void XRHMDSupport::SubmitFrameTexture(int eye_index, ID3D11Texture2D* texture, const unsigned int& time) {
    if (!IsInitialized() || !swapchains_ || !should_render_ || !views_valid_) return;

    XR::Eye eye = static_cast<XR::Eye>(eye_index);
    CopyTextureToSwapchain(texture, eye);
}

void XRHMDSupport::CopyTextureToSwapchain(ID3D11Texture2D* source, XR::Eye eye) {
    if (!source || !swapchains_) return;

    auto* swapchain = swapchains_->GetSwapchain(eye);
    if (!swapchain) return;

    static bool loggedFormats = false;

    // Acquire swapchain image
    uint32_t imageIndex;
    if (!swapchain->AcquireImage(imageIndex)) return;
    if (!swapchain->WaitImage(kSwapchainWaitTimeoutNs)) {
        static bool loggedWaitTimeout = false;
        if (!loggedWaitTimeout) {
            LOGSTR("XRHMDSupport: Swapchain wait timed out, skipping eye submit to keep Present responsive\n");
            loggedWaitTimeout = true;
        }
        swapchain->ReleaseImage();
        return;
    }

    // Get the swapchain texture
    ID3D11Texture2D* dest = swapchain->GetCurrentTexture();
    if (!dest) {
        swapchain->ReleaseImage();
        return;
    }

    // Get source and dest descriptions
    D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
    source->GetDesc(&srcDesc);
    dest->GetDesc(&dstDesc);

    if (!loggedFormats) {
        LOGSTRF("CopyTextureToSwapchain: src fmt=%u (%ux%u) dst fmt=%u (%ux%u)\n",
            srcDesc.Format, srcDesc.Width, srcDesc.Height,
            dstDesc.Format, dstDesc.Width, dstDesc.Height);
        loggedFormats = true;
    }

    // Copy the texture
    if (srcDesc.Width == dstDesc.Width && srcDesc.Height == dstDesc.Height) {
        // Direct copy if sizes match
        device_context_->CopyResource(dest, source);
    } else {
        // Use a box copy for size mismatch
        D3D11_BOX srcBox = {};
        srcBox.left = 0;
        srcBox.top = 0;
        srcBox.front = 0;
        srcBox.right = min(srcDesc.Width, dstDesc.Width);
        srcBox.bottom = min(srcDesc.Height, dstDesc.Height);
        srcBox.back = 1;

        device_context_->CopySubresourceRegion(dest, 0, 0, 0, 0, source, 0, &srcBox);
    }

    // Release swapchain image
    swapchain->ReleaseImage();
}

//-----------------------------------------------------------------------------
// Tracking
//-----------------------------------------------------------------------------

void XRHMDSupport::SyncOnPoses() {
    // This is called externally to sync poses
    // In OpenXR, pose updates happen in BeginFrame via LocateViews
    // This method exists for API compatibility
}

XMFLOAT3 XRHMDSupport::GetHeadPosition(float yaw) {
    if (!view_manager_) return XMFLOAT3(0, 0, 0);

    XMFLOAT3 pos = view_manager_->GetHeadPosition();

    if (yaw != 0.0f) {
        // Apply yaw rotation
        XMMATRIX yawMat = XMMatrixRotationY(XMConvertToRadians(yaw));
        XMVECTOR posVec = XMLoadFloat3(&pos);
        posVec = XMVector3Transform(posVec, yawMat);
        XMStoreFloat3(&pos, posVec);
    }

    return pos;
}

//-----------------------------------------------------------------------------
// Controllers
//-----------------------------------------------------------------------------

XRTrackedController* XRHMDSupport::GetLeftHand() {
    return left_controller_.get();
}

XRTrackedController* XRHMDSupport::GetRightHand() {
    return right_controller_.get();
}

void XRHMDSupport::UpdateControllers() {
    if (!action_manager_) return;

    // Update left controller
    if (left_controller_) {
        const auto& state = action_manager_->GetLeftState();
        left_controller_->UpdateFromState(state);

        if (state.poseValid) {
            Matrix4 mat(XR::XrPoseToMatrix(state.pose));
            left_controller_->SetMatrix(mat);
        }
    }

    // Update right controller
    if (right_controller_) {
        const auto& state = action_manager_->GetRightState();
        right_controller_->UpdateFromState(state);

        if (state.poseValid) {
            Matrix4 mat(XR::XrPoseToMatrix(state.pose));
            right_controller_->SetMatrix(mat);
        }
    }
}

//-----------------------------------------------------------------------------
// View Information
//-----------------------------------------------------------------------------

uint32_t XRHMDSupport::GetRawRecommendedWidth() const {
    if (!view_manager_) return 1920;
    return view_manager_->GetRecommendedWidth();
}

uint32_t XRHMDSupport::GetRawRecommendedHeight() const {
    if (!view_manager_) return 1080;
    return view_manager_->GetRecommendedHeight();
}

uint32_t XRHMDSupport::GetRecommendedWidth() const {
    if (swapchain_width_ > 0) {
        return swapchain_width_;
    }
    if (!view_manager_) return 1920;
    return view_manager_->GetRecommendedWidth();
}

uint32_t XRHMDSupport::GetRecommendedHeight() const {
    if (swapchain_height_ > 0) {
        return swapchain_height_;
    }
    if (!view_manager_) return 1080;
    return view_manager_->GetRecommendedHeight();
}

DXGI_FORMAT XRHMDSupport::GetSwapchainFormat() const {
    if (swapchains_) {
        auto* swapchain = swapchains_->GetSwapchain(XR::Eye::Left);
        if (swapchain && swapchain->IsCreated()) {
            return swapchain->GetFormat();
        }
    }
    return swapchain_format_;
}

XMMATRIX XRHMDSupport::GetProjectionMatrix(XR::Eye eye) const {
    if (!view_manager_) return XMMatrixIdentity();
    return view_manager_->GetView(eye).projection_matrix;
}

XMMATRIX XRHMDSupport::GetViewMatrix(XR::Eye eye) const {
    if (!view_manager_) return XMMatrixIdentity();
    return view_manager_->GetView(eye).view_matrix;
}

XMMATRIX XRHMDSupport::GetEyeMatrix(XR::Eye eye) const {
    if (!view_manager_) return XMMatrixIdentity();

    // Get the head pose matrix and the eye view matrix
    XMMATRIX headMatrix = view_manager_->GetHeadPoseMatrix();
    XMMATRIX eyeViewMatrix = view_manager_->GetView(eye).view_matrix;

    // The eye matrix is the transformation from head to eye
    // eyeView = inverse(eyePose) = inverse(headPose * eyeOffset)
    // So: eyeOffset = inverse(headPose) * eyePose = inverse(headPose) * inverse(eyeView)
    XMMATRIX invHeadMatrix = XMMatrixInverse(nullptr, headMatrix);
    XMMATRIX eyePoseMatrix = XMMatrixInverse(nullptr, eyeViewMatrix);

    return invHeadMatrix * eyePoseMatrix;
}

//-----------------------------------------------------------------------------
// Overlay
//-----------------------------------------------------------------------------

void XRHMDSupport::ToggleOverlay() {
    if (overlay_ui_ && overlay_ui_->IsInitialized()) {
        overlay_ui_->Toggle();
        if (overlay_manager_) {
            overlay_manager_->SetAllVisible(overlay_ui_->IsVisible());
        }
        return;
    }

    if (overlay_manager_) {
        overlay_manager_->ToggleAllVisible();
    }
}

void XRHMDSupport::SetOverlayVisible(bool visible) {
    if (overlay_manager_) {
        overlay_manager_->SetAllVisible(visible);
    }
    if (overlay_ui_) {
        overlay_ui_->SetVisible(visible);
    }
}

void XRHMDSupport::PollOverlayKeyboardToggle() {
    if (!overlay_ui_ || !overlay_ui_->IsInitialized()) {
        return;
    }

    // Delete/Insert match the OpenVR path (D3DHooks_VRManager.hpp);
    // F10 is kept for existing OpenXR users.
    static bool prevDelDown = false;
    static bool prevInsDown = false;
    static bool prevF10Down = false;
    bool delDown = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
    bool insDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    bool f10Down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    bool pressed = (delDown && !prevDelDown) ||
                   (insDown && !prevInsDown) ||
                   (f10Down && !prevF10Down);
    prevDelDown = delDown;
    prevInsDown = insDown;
    prevF10Down = f10Down;

    if (pressed) {
        overlay_ui_->Toggle();
        if (overlay_manager_) {
            overlay_manager_->SetAllVisible(overlay_ui_->IsVisible());
        }
        LOGSTRF("XRHMDSupport: Overlay toggled via keyboard -> %s\n",
                overlay_ui_->IsVisible() ? "visible" : "hidden");
    }
}

void XRHMDSupport::UpdateOverlayUI(const XR::ControllerState& leftState,
                                   const XR::ControllerState& rightState) {
    if (!overlay_ui_ || !overlay_ui_->IsInitialized()) {
        return;
    }

    if (overlay_manager_) {
        bool uiVisible = overlay_ui_->IsVisible();
        if (overlay_manager_->AreOverlaysVisible() != uiVisible) {
            overlay_manager_->SetAllVisible(uiVisible);
        }
    }

    auto& inputSettings = VR::GetInputSettings();
    const auto& leftSrc = inputSettings.swapHands.load() ? rightState : leftState;
    const auto& rightSrc = inputSettings.swapHands.load() ? leftState : rightState;

    XR::OverlayInputState leftInput = {};
    leftInput.thumbstickX = leftSrc.thumbstickX;
    leftInput.thumbstickY = leftSrc.thumbstickY;
    leftInput.thumbstickTouched = leftSrc.thumbstickTouched;
    leftInput.thumbstickPressed = leftSrc.thumbstickPressed;
    leftInput.thumbstickJustPressed = leftSrc.thumbstickJustPressed;
    leftInput.thumbstickJustReleased = leftSrc.thumbstickJustPressReleased;
    leftInput.triggerPressed = leftSrc.triggerPressed;
    leftInput.gripPressed = leftSrc.gripPressed;
    leftInput.menuPressed = leftSrc.menuPressed;
    leftInput.menuJustPressed = leftSrc.menuJustPressed;
    leftInput.primaryPressed = leftSrc.primaryPressed;
    leftInput.secondaryPressed = leftSrc.secondaryPressed;

    XR::OverlayInputState rightInput = {};
    rightInput.thumbstickX = rightSrc.thumbstickX;
    rightInput.thumbstickY = rightSrc.thumbstickY;
    rightInput.thumbstickTouched = rightSrc.thumbstickTouched;
    rightInput.thumbstickPressed = rightSrc.thumbstickPressed;
    rightInput.thumbstickJustPressed = rightSrc.thumbstickJustPressed;
    rightInput.thumbstickJustReleased = rightSrc.thumbstickJustPressReleased;
    rightInput.triggerPressed = rightSrc.triggerPressed;
    rightInput.gripPressed = rightSrc.gripPressed;
    rightInput.menuPressed = rightSrc.menuPressed;
    rightInput.menuJustPressed = rightSrc.menuJustPressed;
    rightInput.primaryPressed = rightSrc.primaryPressed;
    rightInput.secondaryPressed = rightSrc.secondaryPressed;

    bool toggleRequested = leftSrc.menuJustPressed || rightSrc.menuJustPressed;
    // NOTE: the keyboard toggle (Delete/Insert/F10) lives in
    // PollOverlayKeyboardToggle(), called from BeginFrame regardless of
    // session visibility - polling it here too would double-toggle.
    if (!toggleRequested) {
        bool bothGrips = leftSrc.gripJustPressed && rightSrc.gripJustPressed;
        bool bothPrimary = leftSrc.primaryJustPressed && rightSrc.primaryJustPressed;
        bool rightFaceCombo = rightSrc.primaryJustPressed && rightSrc.secondaryJustPressed;
        toggleRequested = bothGrips || bothPrimary || rightFaceCombo;
    }

    if (toggleRequested) {
        overlay_ui_->Toggle();
        if (overlay_manager_) {
            overlay_manager_->SetAllVisible(overlay_ui_->IsVisible());
        }
    }

    if (overlay_ui_->IsVisible()) {
        overlay_ui_->HandleInput(leftInput, rightInput);
    }
}

void XRHMDSupport::RenderOverlayUI() {
    if (overlay_ui_ && overlay_ui_->IsInitialized() && overlay_ui_->IsVisible()) {
        overlay_ui_->Render();
        if (overlay_ui_->ConsumeRecenterRequest()) {
            if (view_manager_) {
                view_manager_->Recenter();
            }
            VR::GetStereoSettings().recenterRequested.store(true);
            VR::GetHeadLookSettings().recenterRequested.store(true);
            VR::GetViewSettings().snapYawOffsetDeg.store(0.0f);
        }
    }
}

void XRHMDSupport::ApplyOverlaySettings(const XR::VRSettings& settings) {
    if (settings_overlay_) {
        XR::OverlayConfig config = settings_overlay_->GetConfig();
        config.opacity = settings.overlayOpacity;
        config.width = 1.2f * settings.overlayScale;
        config.height = 0.8f * settings.overlayScale;
        config.position_offset = {0.0f, 0.0f, -settings.overlayDistance};
        settings_overlay_->SetConfig(config);
    }

    auto& repro = VR::GetReprojectionSettings();
    repro.enabled.store(settings.reprojectionEnabled);
    repro.ipd.store(settings.reprojectionIPD);
    repro.depthScale.store(settings.reprojectionDepthScale);
    repro.depthBias.store(settings.reprojectionDepthBias);
    repro.invertDepth.store(settings.reprojectionInvertDepth);
    repro.renderScale.store(settings.renderScale);
    repro.screenOffsetX.store(settings.imageOffsetX);
    repro.screenOffsetY.store(settings.imageOffsetY);
    repro.imageScale.store(settings.imageScale);

    auto& comfort = VR::GetComfortSettings();
    comfort.snapTurning.store(settings.snapTurning);
    comfort.snapTurnAngle.store(settings.snapTurnAngle);
    comfort.vignetteEnabled.store(settings.vignetteEnabled);
    comfort.vignetteIntensity.store(settings.vignetteIntensity);

    // Phase 5/6 comfort keys have no slot in VR::SharedSettings; they live in
    // Stereo::ComfortRuntimeSettings. The OpenVR path already live-applies them
    // (ApplyOverlaySettingsOpenVR in D3DHooks_VRManager.hpp) - do the same here
    // so the overlay takes effect immediately on OpenXR too, instead of only at
    // next launch via LoadComfortRuntimeFromIni.
    auto& comfortRuntime = Stereo::GetComfortRuntime();
    comfortRuntime.vehicleHorizonLock.store(settings.vehicleHorizonLock);
    comfortRuntime.smoothTurnSpeedDeg.store(settings.smoothTurnSpeed);

    auto& stereo = VR::GetStereoSettings();
    stereo.mode.store(settings.stereoMode);
    stereo.stereoIPD.store(settings.stereoIPD);
    stereo.ipdAuto.store(settings.ipdAuto);
    bool cameraReady = VR::GetRuntimeStats().cameraHookReady.load();
    bool allowHeadLookViewLock = ReadEnvFlag("GTAVR_HEADLOOK_VIEWLOCK", false);
    bool headLookActive = settings.headLookEnabled && !cameraReady && allowHeadLookViewLock;
    stereo.headTracking.store(headLookActive ? true : settings.headTracking);
    stereo.positionTracking.store(headLookActive ? false : settings.positionTracking);

    auto& camera = VR::GetCameraSettings();
    camera.worldScale.store(settings.worldScale);
    camera.playerHeight.store(settings.playerHeight);
    camera.cameraOffsetX.store(settings.cameraOffsetX);
    camera.cameraOffsetY.store(settings.cameraOffsetY);
    camera.cameraOffsetZ.store(settings.cameraOffsetZ);

    auto& input = VR::GetInputSettings();
    input.swapHands.store(settings.swapHands);
    input.triggerThreshold.store(settings.triggerThreshold);
    input.gripThreshold.store(settings.gripThreshold);

    auto& headLook = VR::GetHeadLookSettings();
    headLook.enabled.store(settings.headLookEnabled);
    headLook.maxAngleDeg.store(settings.headLookMaxAngle);
    headLook.deadzoneDeg.store(settings.headLookDeadzone);
    headLook.sensitivity.store(settings.headLookSensitivity);
    headLook.invertY.store(settings.headLookInvertY);

    if (settings.headLookEnabled && !cameraReady && !allowHeadLookViewLock) {
        static bool loggedHeadLookFallback = false;
        if (!loggedHeadLookFallback) {
            LOGSTR("XRHMDSupport: Head Look fallback view-lock disabled. Set GTAVR_HEADLOOK_VIEWLOCK=1 to force it.\n");
            loggedHeadLookFallback = true;
        }
    }

    if (view_manager_) {
        view_manager_->SetViewLockEnabled(headLookActive);
    }

    if (settings.headLookEnabled && cameraReady) {
        static bool logged = false;
        if (!logged) {
            LOGSTR("XRHMDSupport: Head Look disabled because camera hook is active\n");
            logged = true;
        }
    }

    auto& fov = VR::GetFovSettings();
    fov.enabled.store(settings.fovOverride);
    fov.perType.store(settings.fovPerType);
    fov.globalFov.store(settings.fovGlobal);
    fov.fpPedFov.store(settings.fovFpPed);
    fov.tpPedFov.store(settings.fovTpPed);
    fov.tpAimFov.store(settings.fovTpAim);
    fov.fpVehicleFov.store(settings.fovFpVehicle);
    fov.tpVehicleFov.store(settings.fovTpVehicle);
    fov.overrideOffset.store(settings.fovOffsetOverride);
    fov.manualOffset.store(settings.fovManualOffset);

    auto& perf = VR::GetPerformanceSettings();
    perf.asyncReprojection.store(settings.asyncReprojection);
    perf.gameResolutionScale.store(settings.gameResolutionScale);

    auto& decoupling = VR::GetDecouplingSettings();
    decoupling.enabled.store(settings.decouplingEnabled);
    decoupling.mode.store(settings.decouplingMode);
    decoupling.maxPitchDeg.store(settings.decouplingMaxPitch);
    decoupling.maxYawDeg.store(settings.decouplingMaxYaw);
    decoupling.aimConeDeg.store(settings.decouplingAimCone);

    auto& cutscene = VR::GetCutsceneSettings();
    cutscene.mode.store(settings.cutsceneMode);
    cutscene.screenDistance.store(settings.cutsceneScreenDistance);
    cutscene.screenScale.store(settings.cutsceneScreenScale);
    cutscene.screenCurve.store(settings.cutsceneScreenCurve);

    auto& debug = VR::GetDebugSettings();
    debug.showDebugInfo.store(settings.showDebugInfo);
    debug.showControllerModels.store(settings.showControllerModels);

    float requestedScale = settings.renderScale;
    float effectiveScale = requestedScale;
    if (view_manager_) {
        effectiveScale = VR::ComputeSafeRenderScale(requestedScale,
            view_manager_->GetRecommendedWidth(),
            view_manager_->GetRecommendedHeight());
    } else {
        effectiveScale = VR::ClampRequestedRenderScale(requestedScale);
    }
    if (std::fabs(effectiveScale - render_scale_) > 0.001f) {
        RequestSwapchainResize(requestedScale);
    }

    // User-visible confirmation that overlay edits reached the live pipeline.
    // INFO level on purpose: verbose logging may be off in-game, and "settings
    // have no effect" reports need positive evidence the apply path ran.
    // Rate-limited because ImGui sliders fire continuously while dragged.
    static uint64_t lastApplyLogTick = 0;
    uint64_t nowTick = GetTickCount64();
    if (nowTick - lastApplyLogTick >= 1000) {
        lastApplyLogTick = nowTick;
        LOGSTRF("XRHMDSupport: Settings applied - mode=%d renderScale=%.2f world=%.2f ipd=%.4f snap=%d/%.0f vign=%d/%.2f horizonLock=%d fov=%d/%.0f\n",
                settings.stereoMode,
                static_cast<double>(settings.renderScale),
                static_cast<double>(settings.worldScale),
                static_cast<double>(settings.stereoIPD),
                settings.snapTurning ? 1 : 0,
                static_cast<double>(settings.snapTurnAngle),
                settings.vignetteEnabled ? 1 : 0,
                static_cast<double>(settings.vignetteIntensity),
                settings.vehicleHorizonLock ? 1 : 0,
                settings.fovOverride ? 1 : 0,
                static_cast<double>(settings.fovGlobal));
    }
}

bool XRHMDSupport::IsOverlayVisible() const {
    return overlay_ui_ && overlay_ui_->IsVisible();
}

bool XRHMDSupport::WantsOverlayInputCapture() const {
    return overlay_ui_ && overlay_ui_->WantsCaptureInput();
}

bool XRHMDSupport::RecreateSwapchains(uint32_t width, uint32_t height) {
    if (!session_ || !graphics_) {
        return false;
    }

    if (!swapchains_) {
        swapchains_ = std::make_unique<XR::XRStereoSwapchains>(session_.get(), graphics_.get());
    } else {
        swapchains_->Destroy();
    }

    bool created = false;
    if (swapchain_format_ != DXGI_FORMAT_UNKNOWN) {
        created = swapchains_->Create(width, height, swapchain_format_);
    } else {
        created = swapchains_->Create(width, height);
    }
    if (!created) {
        return false;
    }

    swapchain_width_ = width;
    swapchain_height_ = height;
    return true;
}

void XRHMDSupport::SetSwapchainFormat(DXGI_FORMAT format) {
    DXGI_FORMAT normalized = NormalizeSwapchainFormat(format);
    if (normalized == DXGI_FORMAT_UNKNOWN || normalized == swapchain_format_) {
        return;
    }

    swapchain_format_ = normalized;

    if (swapchains_ && swapchain_width_ > 0 && swapchain_height_ > 0) {
        if (frame_in_progress_) {
            pending_swapchain_resize_ = true;
            pending_render_scale_ = render_scale_;
            return;
        }

        if (!RecreateSwapchains(swapchain_width_, swapchain_height_)) {
            LOGSTR("XRHMDSupport: Failed to recreate swapchains for format change\n");
        }
    }
}

void XRHMDSupport::RequestSwapchainResize(float scale) {
    pending_render_scale_ = VR::ClampRequestedRenderScale(scale);
    pending_swapchain_resize_ = true;
}

//-----------------------------------------------------------------------------
// Settings
//-----------------------------------------------------------------------------

void XRHMDSupport::SetDesktopMirroring(bool enable) {
    desktop_mirroring_ = enable;
}

void XRHMDSupport::SetFrameHueristic(int frameHuer) {
    // OpenXR handles frame timing internally
}

void XRHMDSupport::SetSpinlock(bool enabled) {
    // OpenXR handles synchronization internally
}

void XRHMDSupport::SetHS(float hs) {
    horizontal_scale_ = hs;
}

void XRHMDSupport::SetVS(float vs) {
    vertical_scale_ = vs;
}

void XRHMDSupport::SetZS(float zs) {
    z_scale_ = zs;
}

//=============================================================================
// XRTrackedController Implementation
//=============================================================================

XRTrackedController::XRTrackedController(XR::Hand hand)
    : hand_(hand)
    , button_state_(true)
{
}

void XRTrackedController::SetMatrix(const Matrix4& matrix) {
    matrix_ = matrix;
}

Matrix4 XRTrackedController::GetMatrix() const {
    return matrix_;
}

XMFLOAT3 XRTrackedController::GetPosition() const {
    return matrix_.GetPosition();
}

XMFLOAT3 XRTrackedController::GetRotation(float rx, float ry, float rz) const {
    return matrix_.GetAngles(rx, ry, rz);
}

void XRTrackedController::UpdateFromState(const XR::ControllerState& state) {
    button_state_.valid_ = state.valid;

    // Thumbstick/touchpad
    button_state_.touchX = state.thumbstickX;
    button_state_.touchY = state.thumbstickY;
    button_state_.touchContact = state.thumbstickTouched;
    button_state_.touchJustContacted = state.thumbstickJustTouched;
    button_state_.touchJustReleased = state.thumbstickJustReleased;
    button_state_.padPressed = state.thumbstickPressed;
    button_state_.padJustPressed = state.thumbstickJustPressed;
    button_state_.padJustReleased = state.thumbstickJustPressReleased;

    // Grip
    button_state_.gripPressed = state.gripPressed;
    button_state_.gripJustPressed = state.gripJustPressed;
    button_state_.gripJustReleased = state.gripJustReleased;

    // Trigger
    button_state_.triggerMargin = state.triggerValue;
    button_state_.triggerPressed = state.triggerPressed;
    button_state_.triggerJustPressed = state.triggerJustPressed;
    button_state_.triggerJustReleased = state.triggerJustReleased;

    // Menu
    button_state_.menuPressed = state.menuPressed;
    button_state_.menuJustPressed = state.menuJustPressed;
    button_state_.menuJustReleased = state.menuJustReleased;
}

} // namespace OVRInject
