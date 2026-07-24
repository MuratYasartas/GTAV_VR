#include "OpenXRBackend.hpp"
#include "SharedSettings.hpp"

namespace {

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

DXGI_FORMAT ExtractSwapchainFormat(IDXGISwapChain* swap_chain) {
    if (!swap_chain) {
        return DXGI_FORMAT_UNKNOWN;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)) && backBuffer) {
        D3D11_TEXTURE2D_DESC desc = {};
        backBuffer->GetDesc(&desc);
        backBuffer->Release();
        return NormalizeSwapchainFormat(desc.Format);
    }

    DXGI_SWAP_CHAIN_DESC desc = {};
    if (SUCCEEDED(swap_chain->GetDesc(&desc))) {
        return NormalizeSwapchainFormat(desc.BufferDesc.Format);
    }

    return DXGI_FORMAT_UNKNOWN;
}

} // namespace

namespace OVRInject {
namespace VR {

//-----------------------------------------------------------------------------
// Constructor / Destructor
//-----------------------------------------------------------------------------

OpenXRBackend::OpenXRBackend() {
}

OpenXRBackend::~OpenXRBackend() {
    Shutdown();
}

//-----------------------------------------------------------------------------
// Lifecycle
//-----------------------------------------------------------------------------

bool OpenXRBackend::Initialize(ID3D11Device* device) {
    hmd_support_ = std::make_unique<XRHMDSupport>();

    if (!hmd_support_->Initialize(nullptr, device)) {
        hmd_support_.reset();
        return false;
    }

    if (preferred_swapchain_format_ != DXGI_FORMAT_UNKNOWN) {
        hmd_support_->SetSwapchainFormat(preferred_swapchain_format_);
    }

    return true;
}

void OpenXRBackend::SetSwapChain(IDXGISwapChain* swap_chain) {
    DXGI_FORMAT format = ExtractSwapchainFormat(swap_chain);
    if (format == DXGI_FORMAT_UNKNOWN) {
        return;
    }

    preferred_swapchain_format_ = format;

    if (hmd_support_) {
        hmd_support_->SetSwapchainFormat(format);
    }
}

void OpenXRBackend::Shutdown() {
    if (hmd_support_) {
        hmd_support_->Shutdown();
        hmd_support_.reset();
    }
}

bool OpenXRBackend::IsInitialized() const {
    return hmd_support_ && hmd_support_->IsInitialized();
}

//-----------------------------------------------------------------------------
// Frame Lifecycle
//-----------------------------------------------------------------------------

bool OpenXRBackend::BeginFrame() {
    if (!hmd_support_) return false;
    return hmd_support_->BeginFrame();
}

void OpenXRBackend::EndFrame() {
    if (!hmd_support_) return;
    hmd_support_->EndFrame();
}

void OpenXRBackend::SubmitEyeTexture(Eye eye, ID3D11Texture2D* texture) {
    if (!hmd_support_) return;
    hmd_support_->SubmitFrameTexture(static_cast<int>(eye), texture, 0);
}

bool OpenXRBackend::IsOverlayVisible() const {
    return hmd_support_ && hmd_support_->IsOverlayVisible();
}

bool OpenXRBackend::WantsOverlayInputCapture() const {
    return hmd_support_ && hmd_support_->WantsOverlayInputCapture();
}

//-----------------------------------------------------------------------------
// Head Tracking
//-----------------------------------------------------------------------------

XMMATRIX OpenXRBackend::GetHeadPoseMatrix() const {
    if (!hmd_support_) return XMMatrixIdentity();
    return hmd_support_->GetHeadMatrix().getDXMatrix();
}

XMFLOAT3 OpenXRBackend::GetHeadPosition() const {
    if (!hmd_support_) return XMFLOAT3(0, 0, 0);
    return hmd_support_->GetHeadPosition();
}

XMFLOAT3 OpenXRBackend::GetHeadForward() const {
    if (!hmd_support_) return XMFLOAT3(0, 0, -1);
    return hmd_support_->GetHeadForwardVector();
}

XMFLOAT3 OpenXRBackend::GetHeadUp() const {
    if (!hmd_support_) return XMFLOAT3(0, 1, 0);
    return hmd_support_->GetHeadUpVector();
}

XMFLOAT3 OpenXRBackend::GetHeadRotation() const {
    if (!hmd_support_) return XMFLOAT3(0, 0, 0);
    return hmd_support_->GetHeadRotation();
}

//-----------------------------------------------------------------------------
// Controller Tracking
//-----------------------------------------------------------------------------

void OpenXRBackend::UpdateControllers() {
    if (!hmd_support_) return;

    hmd_support_->UpdateControllers();

    // Update our local controller states
    UpdateControllerStateFromXR(Hand::Left);
    UpdateControllerStateFromXR(Hand::Right);
}

void OpenXRBackend::UpdateControllerStateFromXR(Hand hand) {
    auto& state = controller_states_[static_cast<size_t>(hand)];

    XRTrackedController* controller = (hand == Hand::Left)
        ? hmd_support_->GetLeftHand()
        : hmd_support_->GetRightHand();

    if (!controller) {
        state.isTracked = false;
        state.buttons.valid = false;
        return;
    }

    // Get pose
    Matrix4 mat = controller->GetMatrix();
    state.poseMatrix = mat.getDXMatrix();
    state.position = controller->GetPosition();

    auto buttonState = controller->GetButtonState();
    state.isTracked = buttonState.valid_;
    state.buttons.valid = buttonState.valid_;

    // Copy button states
    state.buttons.thumbstickX = buttonState.touchX;
    state.buttons.thumbstickY = buttonState.touchY;
    state.buttons.thumbstickTouched = buttonState.touchContact;
    state.buttons.thumbstickPressed = buttonState.padPressed;
    state.buttons.thumbstickJustPressed = buttonState.padJustPressed;
    state.buttons.thumbstickJustReleased = buttonState.padJustReleased;

    state.buttons.gripPressed = buttonState.gripPressed;
    state.buttons.gripJustPressed = buttonState.gripJustPressed;
    state.buttons.gripJustReleased = buttonState.gripJustReleased;

    state.buttons.triggerValue = buttonState.triggerMargin;
    state.buttons.triggerPressed = buttonState.triggerPressed;
    state.buttons.triggerJustPressed = buttonState.triggerJustPressed;
    state.buttons.triggerJustReleased = buttonState.triggerJustReleased;

    state.buttons.menuPressed = buttonState.menuPressed;
    state.buttons.menuJustPressed = buttonState.menuJustPressed;
    state.buttons.menuJustReleased = buttonState.menuJustReleased;
}

const ControllerState& OpenXRBackend::GetControllerState(Hand hand) const {
    size_t index = static_cast<size_t>(hand);
    if (VR::GetInputSettings().swapHands.load()) {
        index = (index == 0) ? 1 : 0;
    }
    return controller_states_[index];
}

bool OpenXRBackend::IsControllerTracked(Hand hand) const {
    return GetControllerState(hand).isTracked;
}

//-----------------------------------------------------------------------------
// Haptics
//-----------------------------------------------------------------------------

void OpenXRBackend::TriggerHaptic(Hand hand, float duration, float frequency, float amplitude) {
    if (!hmd_support_) return;

    auto* actionManager = hmd_support_->GetActionManager();
    if (!actionManager) return;

    // Convert duration from seconds to nanoseconds
    int64_t durationNs = static_cast<int64_t>(duration * 1e9);

    actionManager->TriggerHaptic(ToXRHand(hand), durationNs, frequency, amplitude);
}

//-----------------------------------------------------------------------------
// View Configuration
//-----------------------------------------------------------------------------

uint32_t OpenXRBackend::GetRecommendedWidth() const {
    if (!hmd_support_) return 1920;
    return hmd_support_->GetRecommendedWidth();
}

uint32_t OpenXRBackend::GetRecommendedHeight() const {
    if (!hmd_support_) return 1080;
    return hmd_support_->GetRecommendedHeight();
}

XMMATRIX OpenXRBackend::GetProjectionMatrix(Eye eye, float nearZ, float farZ) const {
    if (!hmd_support_) return XMMatrixIdentity();

    auto* viewManager = hmd_support_->GetViewManager();
    if (!viewManager) return XMMatrixIdentity();

    if (!viewManager->ViewsValid()) {
        // Views not located yet; fall back to the cached (identity) matrix.
        return hmd_support_->GetProjectionMatrix(ToXREye(eye));
    }

    // Compute the projection directly from the latest located FOV using the
    // caller's clip planes. IVRBackend declares this method const and
    // IVRBackend.hpp is not ours to change, so we must not mutate the view
    // manager here (this used to be done via const_cast).
    return XR::XrFovToProjectionMatrixD3D(
        viewManager->GetView(ToXREye(eye)).view.fov, nearZ, farZ);
}

XMMATRIX OpenXRBackend::GetViewMatrix(Eye eye) const {
    if (!hmd_support_) return XMMatrixIdentity();
    return hmd_support_->GetViewMatrix(ToXREye(eye));
}

//-----------------------------------------------------------------------------
// Overlay
//-----------------------------------------------------------------------------

void OpenXRBackend::ShowOverlay() {
    if (!hmd_support_) return;
    hmd_support_->SetOverlayVisible(true);
}

void OpenXRBackend::HideOverlay() {
    if (!hmd_support_) return;
    hmd_support_->SetOverlayVisible(false);
}

void OpenXRBackend::ToggleOverlay() {
    if (!hmd_support_) return;
    hmd_support_->ToggleOverlay();
}

void OpenXRBackend::RenderOverlay() {
    // Overlay rendering is handled as part of EndFrame via overlay layers
    // This method could be used for additional overlay rendering logic
}

//-----------------------------------------------------------------------------
// Info
//-----------------------------------------------------------------------------

const char* OpenXRBackend::GetRuntimeName() const {
    return "OpenXR";
}

const char* OpenXRBackend::GetSystemName() const {
    if (!hmd_support_) return "Unknown";

    auto* instance = hmd_support_->GetInstance();
    if (!instance) return "Unknown";

    return instance->GetSystemName();
}

void* OpenXRBackend::GetSession() const {
    if (!hmd_support_) return nullptr;
    auto* session = hmd_support_->GetSession();
    if (!session) return nullptr;
    return reinterpret_cast<void*>(session->GetHandle());
}

ID3D11Device* OpenXRBackend::GetDevice() const {
    if (!hmd_support_) return nullptr;
    return hmd_support_->GetDevice();
}

XMMATRIX OpenXRBackend::GetEyeMatrix(Eye eye) const {
    if (!hmd_support_) return XMMatrixIdentity();
    return hmd_support_->GetEyeMatrix(ToXREye(eye));
}

DXGI_FORMAT OpenXRBackend::GetPreferredSwapchainFormat() const {
    if (!hmd_support_) return DXGI_FORMAT_UNKNOWN;
    return hmd_support_->GetSwapchainFormat();
}

void OpenXRBackend::Recenter() {
    if (!hmd_support_) return;
    auto* viewManager = hmd_support_->GetViewManager();
    if (!viewManager) return;
    viewManager->Recenter();
}

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

XR::Eye OpenXRBackend::ToXREye(Eye eye) {
    return static_cast<XR::Eye>(eye);
}

XR::Hand OpenXRBackend::ToXRHand(Hand hand) {
    return static_cast<XR::Hand>(hand);
}

} // namespace VR
} // namespace OVRInject
