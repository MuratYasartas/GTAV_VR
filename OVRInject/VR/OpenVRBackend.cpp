#include "OpenVRBackend.hpp"
#include "../Log.hpp"
#include "SharedSettings.hpp"

#include <algorithm>

namespace {

XMMATRIX ConvertPose(const vr::HmdMatrix34_t& mat) {
    return XMMATRIX(
        mat.m[0][0], mat.m[1][0], mat.m[2][0], 0.0f,
        mat.m[0][1], mat.m[1][1], mat.m[2][1], 0.0f,
        mat.m[0][2], mat.m[1][2], mat.m[2][2], 0.0f,
        mat.m[0][3], mat.m[1][3], mat.m[2][3], 1.0f
    );
}

bool IsButtonPressed(uint64_t mask, vr::EVRButtonId button) {
    return (mask & (1ULL << static_cast<int>(button))) != 0;
}

} // namespace

namespace OVRInject {
namespace VR {

//-----------------------------------------------------------------------------
// Constructor / Destructor
//-----------------------------------------------------------------------------

OpenVRBackend::OpenVRBackend() {
}

OpenVRBackend::~OpenVRBackend() {
    Shutdown();
}

//-----------------------------------------------------------------------------
// Lifecycle
//-----------------------------------------------------------------------------

bool OpenVRBackend::Initialize(ID3D11Device* device) {
    if (initialized_) {
        return true;
    }

    device_ = device;

	vr::EVRInitError error = vr::VRInitError_None;
	hmd_ = vr::VR_Init(&error, vr::VRApplication_Scene);

	if (error != vr::VRInitError_None) {
		hmd_ = nullptr;
		LOGSTRF("OpenVRBackend Initialize error: %s", vr::VR_GetVRInitErrorAsEnglishDescription(error));
		return false;
	}

    initialized_ = true;
    LOGSTR("OpenVRBackend: Initialized\n");

    return true;
}

void OpenVRBackend::Shutdown() {
	if (hmd_)
	{
        if (async_disable_original_valid_) {
            vr::EVRSettingsError err = vr::VRSettingsError_None;
            vr::VRSettings()->SetBool(vr::k_pch_SteamVR_Section,
                                      vr::k_pch_SteamVR_DisableAsyncReprojection_Bool,
                                      async_disable_original_,
                                      &err);
            if (err != vr::VRSettingsError_None) {
                LOGSTRF("OpenVRBackend: Failed to restore async reprojection setting (%d)\n",
                        static_cast<int>(err));
            }
        }
		vr::VR_Shutdown();
		hmd_ = nullptr;
	}

    initialized_ = false;
    device_ = nullptr;

    LOGSTR("OpenVRBackend: Shutdown\n");
}

bool OpenVRBackend::IsInitialized() const {
    return initialized_ && hmd_ != nullptr;
}

//-----------------------------------------------------------------------------
// Frame Lifecycle
//-----------------------------------------------------------------------------

bool OpenVRBackend::BeginFrame() {
    if (!IsInitialized()) return false;

    // New frame: advance the counter so the pose cache refreshes once.
    ++frame_counter_;

    UpdateAsyncReprojectionSetting();

    // OpenVR frame timing is handled internally by HMDRenderer
    // SyncOnPoses is called separately
    UpdateControllers();

    return true;
}

void OpenVRBackend::EndFrame() {
    // OpenVR frame submission is handled by SubmitEyeTexture
    // via HMDRenderer's async queue
}

void OpenVRBackend::SubmitEyeTexture(Eye eye, ID3D11Texture2D* texture) {
    if (!IsInitialized() || !texture) return;

	vr::Texture_t vr_texture = { (void*)texture, vr::TextureType_DirectX, vr::ColorSpace_Gamma };
	vr::EVRCompositorError err = vr::VRCompositor()->Submit((vr::EVREye)eye, &vr_texture);
	if (err != vr::VRCompositorError_None) {
		// Rate-limited: a rejected submission otherwise fails as a silent black HMD.
		static int submitErrorCount = 0;
		if (++submitErrorCount <= 10 || (submitErrorCount % 600) == 0) {
			LOGWNDF("OpenVRBackend: Submit(eye=%d) rejected with error %d (count=%d)\n",
			        static_cast<int>(eye), static_cast<int>(err), submitErrorCount);
		}
	}
}

void OpenVRBackend::UpdateAsyncReprojectionSetting() {
    auto& perf = VR::GetPerformanceSettings();
    bool desired = perf.asyncReprojection.load();
    if (async_reprojection_last_valid_ && desired == async_reprojection_last_) {
        return;
    }

    if (!vr::VRSettings()) {
        return;
    }

    if (!async_disable_original_valid_) {
        vr::EVRSettingsError err = vr::VRSettingsError_None;
        bool current = vr::VRSettings()->GetBool(vr::k_pch_SteamVR_Section,
                                                 vr::k_pch_SteamVR_DisableAsyncReprojection_Bool,
                                                 &err);
        if (err == vr::VRSettingsError_None) {
            async_disable_original_ = current;
            async_disable_original_valid_ = true;
        } else {
            LOGSTRF("OpenVRBackend: Failed to read async reprojection setting (%d)\n",
                    static_cast<int>(err));
        }
    }

    bool disable_async = !desired;
    vr::EVRSettingsError err = vr::VRSettingsError_None;
    vr::VRSettings()->SetBool(vr::k_pch_SteamVR_Section,
                              vr::k_pch_SteamVR_DisableAsyncReprojection_Bool,
                              disable_async,
                              &err);
    if (err != vr::VRSettingsError_None) {
        LOGSTRF("OpenVRBackend: Failed to update async reprojection setting (%d)\n",
                static_cast<int>(err));
        return;
    }

    async_reprojection_last_ = desired;
    async_reprojection_last_valid_ = true;
}

//-----------------------------------------------------------------------------
// Head Tracking
//-----------------------------------------------------------------------------

void OpenVRBackend::UpdatePoseCache() const {
    if (!IsInitialized()) return;

    // One WaitGetPoses per frame: it blocks on compositor timing, and all
    // pose getters share this snapshot.
    if (pose_cache_valid_ && pose_cache_frame_ == frame_counter_) return;

    vr::VRCompositor()->WaitGetPoses(cached_poses_, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
    pose_cache_frame_ = frame_counter_;
    pose_cache_valid_ = true;
}

XMMATRIX OpenVRBackend::GetHeadPoseMatrix() const {
    if (!IsInitialized()) return XMMatrixIdentity();

	UpdatePoseCache();

	if (cached_poses_[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
	{
		const vr::HmdMatrix34_t& mat = cached_poses_[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
		return XMMATRIX(
			mat.m[0][0], mat.m[1][0], mat.m[2][0], 0.0f,
			mat.m[0][1], mat.m[1][1], mat.m[2][1], 0.0f,
			mat.m[0][2], mat.m[1][2], mat.m[2][2], 0.0f,
			mat.m[0][3], mat.m[1][3], mat.m[2][3], 1.0f
		);
	}

    return XMMatrixIdentity();
}

XMFLOAT3 OpenVRBackend::GetHeadPosition() const {
    if (!IsInitialized()) return XMFLOAT3(0, 0, 0);
	
	XMMATRIX head_pose = GetHeadPoseMatrix();
	return XMFLOAT3(head_pose.r[3].m128_f32[0], head_pose.r[3].m128_f32[1], head_pose.r[3].m128_f32[2]);
}

XMFLOAT3 OpenVRBackend::GetHeadForward() const {
    if (!IsInitialized()) return XMFLOAT3(0, 0, -1);

	XMMATRIX head_pose = GetHeadPoseMatrix();
	return XMFLOAT3(head_pose.r[2].m128_f32[0], head_pose.r[2].m128_f32[1], head_pose.r[2].m128_f32[2]);
}

XMFLOAT3 OpenVRBackend::GetHeadUp() const {
    if (!IsInitialized()) return XMFLOAT3(0, 1, 0);

	XMMATRIX head_pose = GetHeadPoseMatrix();
	return XMFLOAT3(head_pose.r[1].m128_f32[0], head_pose.r[1].m128_f32[1], head_pose.r[1].m128_f32[2]);
}

XMFLOAT3 OpenVRBackend::GetHeadRotation() const {
    if (!IsInitialized()) return XMFLOAT3(0, 0, 0);
	
	XMMATRIX head_pose = GetHeadPoseMatrix();
	
	XMFLOAT3 rotation;
	rotation.x = asinf(head_pose.r[1].m128_f32[2]);
	rotation.y = atan2f(head_pose.r[0].m128_f32[2], head_pose.r[2].m128_f32[2]);
	rotation.z = atan2f(head_pose.r[1].m128_f32[0], head_pose.r[1].m128_f32[1]);

	// IVRBackend documents euler angles in degrees; the formulas above yield radians
	rotation.x = XMConvertToDegrees(rotation.x);
	rotation.y = XMConvertToDegrees(rotation.y);
	rotation.z = XMConvertToDegrees(rotation.z);

	return rotation;
}

//-----------------------------------------------------------------------------
// Controller Tracking
//-----------------------------------------------------------------------------

void OpenVRBackend::UpdateControllers() {
    if (!IsInitialized()) return;

    auto& input = VR::GetInputSettings();

    UpdatePoseCache();

    controller_indices_[static_cast<size_t>(Hand::Left)] = vr::k_unTrackedDeviceIndexInvalid;
    controller_indices_[static_cast<size_t>(Hand::Right)] = vr::k_unTrackedDeviceIndexInvalid;

    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (hmd_->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller) {
            continue;
        }

        vr::ETrackedControllerRole role = hmd_->GetControllerRoleForTrackedDeviceIndex(i);
        if (role == vr::TrackedControllerRole_LeftHand) {
            controller_indices_[static_cast<size_t>(Hand::Left)] = i;
        } else if (role == vr::TrackedControllerRole_RightHand) {
            controller_indices_[static_cast<size_t>(Hand::Right)] = i;
        }
    }

    auto updateHand = [&](Hand hand) {
        size_t index = static_cast<size_t>(hand);
        auto& state = controller_states_[index];
        auto& prev = previous_buttons_[index];

        vr::TrackedDeviceIndex_t deviceIndex = controller_indices_[index];
        if (deviceIndex == vr::k_unTrackedDeviceIndexInvalid) {
            state = ControllerState{};
            state.isTracked = false;
            state.buttons.valid = false;
            prev = ControllerButtonState{};
            return;
        }

        const vr::TrackedDevicePose_t& pose = cached_poses_[deviceIndex];
        state.isTracked = pose.bPoseIsValid;
        state.buttons.valid = pose.bPoseIsValid;
        if (pose.bPoseIsValid) {
            state.poseMatrix = ConvertPose(pose.mDeviceToAbsoluteTracking);
            state.position = XMFLOAT3(state.poseMatrix.r[3].m128_f32[0],
                                      state.poseMatrix.r[3].m128_f32[1],
                                      state.poseMatrix.r[3].m128_f32[2]);
        }

        vr::VRControllerState_t controllerState = {};
        if (!hmd_->GetControllerState(deviceIndex, &controllerState, sizeof(controllerState))) {
            state.buttons.valid = false;
            prev = ControllerButtonState{};
            return;
        }

        ControllerButtonState buttons = {};
        buttons.valid = true;
        buttons.thumbstickX = controllerState.rAxis[0].x;
        buttons.thumbstickY = controllerState.rAxis[0].y;

        buttons.thumbstickTouched = IsButtonPressed(controllerState.ulButtonTouched, vr::k_EButton_SteamVR_Touchpad);
        buttons.thumbstickPressed = IsButtonPressed(controllerState.ulButtonPressed, vr::k_EButton_SteamVR_Touchpad);
        buttons.thumbstickJustPressed = buttons.thumbstickPressed && !prev.thumbstickPressed;
        buttons.thumbstickJustReleased = !buttons.thumbstickPressed && prev.thumbstickPressed;

        buttons.gripPressed = IsButtonPressed(controllerState.ulButtonPressed, vr::k_EButton_Grip);
        buttons.gripValue = buttons.gripPressed ? 1.0f : 0.0f;
        float gripPress = input.gripThreshold.load();
        if (gripPress < 0.05f) gripPress = 0.05f;
        if (gripPress > 0.95f) gripPress = 0.95f;
        float gripRelease = (std::max)(0.05f, gripPress - 0.2f);
        bool gripPressed = prev.gripPressed ? (buttons.gripValue >= gripRelease)
                                            : (buttons.gripValue >= gripPress);
        buttons.gripJustPressed = gripPressed && !prev.gripPressed;
        buttons.gripJustReleased = !gripPressed && prev.gripPressed;
        buttons.gripPressed = gripPressed;

        buttons.triggerValue = controllerState.rAxis[1].x;
        float triggerPress = input.triggerThreshold.load();
        if (triggerPress < 0.05f) triggerPress = 0.05f;
        if (triggerPress > 0.95f) triggerPress = 0.95f;
        float triggerRelease = (std::max)(0.05f, triggerPress - 0.2f);
        bool triggerPressed = prev.triggerPressed ? (buttons.triggerValue >= triggerRelease)
                                                  : (buttons.triggerValue >= triggerPress);
        buttons.triggerJustPressed = triggerPressed && !prev.triggerPressed;
        buttons.triggerJustReleased = !triggerPressed && prev.triggerPressed;
        buttons.triggerPressed = triggerPressed;

        buttons.primaryPressed = IsButtonPressed(controllerState.ulButtonPressed, vr::k_EButton_A);
        buttons.primaryJustPressed = buttons.primaryPressed && !prev.primaryPressed;
        buttons.primaryJustReleased = !buttons.primaryPressed && prev.primaryPressed;

        buttons.secondaryPressed = false;
        buttons.secondaryJustPressed = false;
        buttons.secondaryJustReleased = false;

        buttons.menuPressed = IsButtonPressed(controllerState.ulButtonPressed, vr::k_EButton_ApplicationMenu);
        buttons.menuJustPressed = buttons.menuPressed && !prev.menuPressed;
        buttons.menuJustReleased = !buttons.menuPressed && prev.menuPressed;

        state.buttons = buttons;
        prev = buttons;
    };

    updateHand(Hand::Left);
    updateHand(Hand::Right);
}

const ControllerState& OpenVRBackend::GetControllerState(Hand hand) const {
    size_t index = static_cast<size_t>(hand);
    if (VR::GetInputSettings().swapHands.load()) {
        index = (index == 0) ? 1 : 0;
    }
    return controller_states_[index];
}

bool OpenVRBackend::IsControllerTracked(Hand hand) const {
    return GetControllerState(hand).isTracked;
}

//-----------------------------------------------------------------------------
// Haptics
//-----------------------------------------------------------------------------

void OpenVRBackend::TriggerHaptic(Hand hand, float duration, float frequency, float amplitude) {
    if (!IsInitialized()) return;

    (void)frequency;

    size_t index = static_cast<size_t>(hand);
    vr::TrackedDeviceIndex_t deviceIndex = controller_indices_[index];
    if (deviceIndex == vr::k_unTrackedDeviceIndexInvalid) {
        return;
    }

    float clamped = (std::max)(0.0f, (std::min)(amplitude, 1.0f));
    uint32_t durationMicro = static_cast<uint32_t>((std::max)(1.0f, (std::min)(duration * 1000000.0f * clamped, 3999.0f)));
    hmd_->TriggerHapticPulse(deviceIndex, 0, static_cast<unsigned short>(durationMicro));
}

//-----------------------------------------------------------------------------
// View Configuration
//-----------------------------------------------------------------------------

uint32_t OpenVRBackend::GetRecommendedWidth() const {
    if (!IsInitialized()) return 1920;

    uint32_t width, height;
    hmd_->GetRecommendedRenderTargetSize(&width, &height);
    return width;
}

uint32_t OpenVRBackend::GetRecommendedHeight() const {
    if (!IsInitialized()) return 1080;

	uint32_t width, height;
	hmd_->GetRecommendedRenderTargetSize(&width, &height);
	return height;
}

XMMATRIX OpenVRBackend::GetProjectionMatrix(Eye eye, float nearZ, float farZ) const {
    if (!IsInitialized()) return XMMatrixIdentity();

    vr::HmdMatrix44_t mat = hmd_->GetProjectionMatrix((vr::EVREye)eye, nearZ, farZ);

    // Convert OpenVR matrix to DirectX
    return XMMATRIX(
        mat.m[0][0], mat.m[1][0], mat.m[2][0], mat.m[3][0],
        mat.m[0][1], mat.m[1][1], mat.m[2][1], mat.m[3][1],
        mat.m[0][2], mat.m[1][2], mat.m[2][2], mat.m[3][2],
        mat.m[0][3], mat.m[1][3], mat.m[2][3], mat.m[3][3]
    );
}

XMMATRIX OpenVRBackend::GetViewMatrix(Eye eye) const {
    if (!IsInitialized()) return XMMatrixIdentity();

    vr::HmdMatrix34_t eyeToHead = hmd_->GetEyeToHeadTransform((vr::EVREye)eye);

    // Convert to 4x4 matrix
    XMMATRIX eyeMatrix = XMMATRIX(
        eyeToHead.m[0][0], eyeToHead.m[1][0], eyeToHead.m[2][0], 0.0f,
        eyeToHead.m[0][1], eyeToHead.m[1][1], eyeToHead.m[2][1], 0.0f,
        eyeToHead.m[0][2], eyeToHead.m[1][2], eyeToHead.m[2][2], 0.0f,
        eyeToHead.m[0][3], eyeToHead.m[1][3], eyeToHead.m[2][3], 1.0f
    );

    // Combine with head matrix and invert for view
    XMMATRIX headMatrix = GetHeadPoseMatrix();
    XMMATRIX poseMatrix = eyeMatrix * headMatrix;
    return XMMatrixInverse(nullptr, poseMatrix);
}

XMMATRIX OpenVRBackend::GetEyeMatrix(Eye eye) const {
	if (!IsInitialized()) return XMMatrixIdentity();

	vr::HmdMatrix34_t eyeToHead = hmd_->GetEyeToHeadTransform((vr::EVREye)eye);

	// Convert to 4x4 matrix
	return XMMATRIX(
		eyeToHead.m[0][0], eyeToHead.m[1][0], eyeToHead.m[2][0], 0.0f,
		eyeToHead.m[0][1], eyeToHead.m[1][1], eyeToHead.m[2][1], 0.0f,
		eyeToHead.m[0][2], eyeToHead.m[1][2], eyeToHead.m[2][2], 0.0f,
		eyeToHead.m[0][3], eyeToHead.m[1][3], eyeToHead.m[2][3], 1.0f
	);
}

//-----------------------------------------------------------------------------
// Overlay
//-----------------------------------------------------------------------------

void OpenVRBackend::ShowOverlay() {
}

void OpenVRBackend::HideOverlay() {
}

void OpenVRBackend::ToggleOverlay() {
}

void OpenVRBackend::RenderOverlay() {
}

//-----------------------------------------------------------------------------
// Info
//-----------------------------------------------------------------------------

const char* OpenVRBackend::GetRuntimeName() const {
    return "OpenVR";
}

const char* OpenVRBackend::GetSystemName() const {
    if (!IsInitialized()) return "Unknown";

    if (system_name_.empty()) {
		char buffer[256] = {};
		vr::ETrackedPropertyError error;
		hmd_->GetStringTrackedDeviceProperty(
			vr::k_unTrackedDeviceIndex_Hmd,
			vr::Prop_ModelNumber_String,
			buffer,
			sizeof(buffer),
			&error
		);
		if (error == vr::TrackedProp_Success) {
			system_name_ = buffer;
		}
		else {
			system_name_ = "SteamVR HMD";
		}
    }

    return system_name_.c_str();
}

void OpenVRBackend::Recenter() {
	if (!IsInitialized()) return;
	vr::IVRChaperone* chaperone = vr::VRChaperone();
	if (chaperone) {
		chaperone->ResetZeroPose(vr::TrackingUniverseSeated);
	}
}

} // namespace VR
} // namespace OVRInject
