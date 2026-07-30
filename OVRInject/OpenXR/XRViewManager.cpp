#include "XRViewManager.hpp"
#include "XRRecenter.hpp"

#include <cctype>
#include <cstdlib>

namespace OVRInject {
namespace XR {

namespace {

static void NormalizePose(XrPosef& pose) {
    XMVECTOR q = XMVectorSet(pose.orientation.x, pose.orientation.y,
                             pose.orientation.z, pose.orientation.w);
    q = XMQuaternionNormalize(q);
    XMFLOAT4 out;
    XMStoreFloat4(&out, q);
    pose.orientation = {out.x, out.y, out.z, out.w};
}

} // namespace

//-----------------------------------------------------------------------------
// Constructor / Destructor
//-----------------------------------------------------------------------------

XRViewManager::XRViewManager(XRInstance* instance, XRSession* session)
    : instance_(instance)
    , session_(session)
{
    // Initialize views with default values
    for (auto& view : views_) {
        view.config = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
        view.view = {XR_TYPE_VIEW};
        view.projection_matrix = XMMatrixIdentity();
        view.view_matrix = XMMatrixIdentity();
    }
}

XRViewManager::~XRViewManager() {
    Shutdown();
}

//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------

bool XRViewManager::Initialize() {
    if (!instance_ || !instance_->IsInitialized()) {
        LOGSTR("XRViewManager: Instance not initialized\n");
        return false;
    }

    if (!session_ || !session_->IsCreated()) {
        LOGSTR("XRViewManager: Session not created\n");
        return false;
    }

    // Enumerate view configurations to get per-eye resolution info
    if (!EnumerateViewConfigs()) {
        LOGSTR("XRViewManager: Failed to enumerate view configs\n");
        return false;
    }

    // Create the reference space (default to STAGE for room-scale)
    XrReferenceSpaceType preferredSpace = XR_REFERENCE_SPACE_TYPE_STAGE;
    const char* spaceEnv = std::getenv("GTAVR_REFERENCE_SPACE");
    if (spaceEnv && spaceEnv[0]) {
        std::string value(spaceEnv);
        for (auto& c : value) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (value == "local") {
            preferredSpace = XR_REFERENCE_SPACE_TYPE_LOCAL;
        } else if (value == "view") {
            preferredSpace = XR_REFERENCE_SPACE_TYPE_VIEW;
        } else if (value == "stage") {
            preferredSpace = XR_REFERENCE_SPACE_TYPE_STAGE;
        }
    }

    if (!CreateReferenceSpace(preferredSpace)) {
        if (preferredSpace != XR_REFERENCE_SPACE_TYPE_STAGE) {
            LOGSTR("XRViewManager: Preferred reference space not supported, trying STAGE\n");
            if (CreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_STAGE)) {
                goto reference_space_created;
            }
        }
        LOGSTR("XRViewManager: STAGE reference space not supported, trying LOCAL\n");
        if (!CreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_LOCAL)) {
            LOGSTR("XRViewManager: Failed to create reference space\n");
            return false;
        }
    }

reference_space_created:

    if (render_space_ == XR_NULL_HANDLE) {
        render_space_ = reference_space_;
        render_space_type_ = reference_space_type_;
    }

    LOGSTRF("XRViewManager: Initialized successfully\n");
    LOGSTRF("  Recommended resolution: %ux%u per eye\n",
            GetRecommendedWidth(), GetRecommendedHeight());
    LOGSTRF("  Max resolution: %ux%u per eye\n",
            GetMaxWidth(), GetMaxHeight());
    const char* spaceName = "LOCAL";
    if (reference_space_type_ == XR_REFERENCE_SPACE_TYPE_STAGE) {
        spaceName = "STAGE";
    } else if (reference_space_type_ == XR_REFERENCE_SPACE_TYPE_VIEW) {
        spaceName = "VIEW";
    }
    LOGSTRF("  Reference space: %s\n", spaceName);

    return true;
}

void XRViewManager::Shutdown() {
    if (render_space_ != XR_NULL_HANDLE && render_space_ != reference_space_) {
        xrDestroySpace(render_space_);
        render_space_ = XR_NULL_HANDLE;
    }

    if (reference_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(reference_space_);
        reference_space_ = XR_NULL_HANDLE;
        LOGSTR("XRViewManager: Reference space destroyed\n");
    }

    reference_space_type_ = XR_REFERENCE_SPACE_TYPE_LOCAL;
    render_space_type_ = reference_space_type_;
    views_valid_ = false;
}

//-----------------------------------------------------------------------------
// View Configuration Enumeration
//-----------------------------------------------------------------------------

bool XRViewManager::EnumerateViewConfigs() {
    XrInstance instance = instance_->GetHandle();
    XrSystemId systemId = instance_->GetSystemId();

    // First, enumerate supported view configuration types
    uint32_t viewConfigTypeCount = 0;
    XrResult result = xrEnumerateViewConfigurations(
        instance, systemId, 0, &viewConfigTypeCount, nullptr);

    if (XR_FAILED(result) || viewConfigTypeCount == 0) {
        LOGSTR("XRViewManager: Failed to enumerate view configuration types\n");
        return false;
    }

    std::vector<XrViewConfigurationType> viewConfigTypes(viewConfigTypeCount);
    result = xrEnumerateViewConfigurations(
        instance, systemId, viewConfigTypeCount, &viewConfigTypeCount, viewConfigTypes.data());

    if (XR_FAILED(result)) {
        LOGSTR("XRViewManager: Failed to get view configuration types\n");
        return false;
    }

    // Check if stereo view is supported
    bool stereoSupported = false;
    for (const auto& type : viewConfigTypes) {
        if (type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
            stereoSupported = true;
            break;
        }
    }

    if (!stereoSupported) {
        LOGSTR("XRViewManager: Stereo view configuration not supported\n");
        return false;
    }

    view_config_type_ = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    // Get view configuration views (per-eye resolution info)
    uint32_t viewCount = 0;
    result = xrEnumerateViewConfigurationViews(
        instance, systemId, view_config_type_, 0, &viewCount, nullptr);

    if (XR_FAILED(result) || viewCount == 0) {
        LOGSTR("XRViewManager: Failed to enumerate view configuration views\n");
        return false;
    }

    if (viewCount != static_cast<uint32_t>(Eye::Count)) {
        LOGSTRF("XRViewManager: Unexpected view count: %u (expected 2)\n", viewCount);
        return false;
    }

    std::vector<XrViewConfigurationView> viewConfigs(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    result = xrEnumerateViewConfigurationViews(
        instance, systemId, view_config_type_, viewCount, &viewCount, viewConfigs.data());

    if (XR_FAILED(result)) {
        LOGSTR("XRViewManager: Failed to get view configuration views\n");
        return false;
    }

    // Store the configuration for each eye
    for (uint32_t i = 0; i < viewCount; ++i) {
        views_[i].config = viewConfigs[i];

        LOGSTRF("XRViewManager: View %u config:\n", i);
        LOGSTRF("  Recommended: %ux%u, samples: %u\n",
                viewConfigs[i].recommendedImageRectWidth,
                viewConfigs[i].recommendedImageRectHeight,
                viewConfigs[i].recommendedSwapchainSampleCount);
        LOGSTRF("  Max: %ux%u, samples: %u\n",
                viewConfigs[i].maxImageRectWidth,
                viewConfigs[i].maxImageRectHeight,
                viewConfigs[i].maxSwapchainSampleCount);
    }

    return true;
}

//-----------------------------------------------------------------------------
// Reference Space
//-----------------------------------------------------------------------------

bool XRViewManager::CreateSpace(XrReferenceSpaceType type, XrSpace& outSpace) {
    if (!session_ || !session_->IsCreated()) {
        LOGSTR("XRViewManager: Session not created, cannot create reference space\n");
        return false;
    }

    XrSession session = session_->GetHandle();

    uint32_t spaceCount = 0;
    XrResult result = xrEnumerateReferenceSpaces(session, 0, &spaceCount, nullptr);
    if (XR_FAILED(result)) {
        LOGSTR("XRViewManager: Failed to enumerate reference spaces\n");
        return false;
    }

    std::vector<XrReferenceSpaceType> supportedSpaces(spaceCount);
    result = xrEnumerateReferenceSpaces(session, spaceCount, &spaceCount, supportedSpaces.data());
    if (XR_FAILED(result)) {
        LOGSTR("XRViewManager: Failed to get reference spaces\n");
        return false;
    }

    bool typeSupported = false;
    for (const auto& space : supportedSpaces) {
        if (space == type) {
            typeSupported = true;
            break;
        }
    }

    if (!typeSupported) {
        LOGSTRF("XRViewManager: Reference space type %d not supported\n", type);
        return false;
    }

    XrReferenceSpaceCreateInfo createInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    createInfo.referenceSpaceType = type;
    createInfo.poseInReferenceSpace = IdentityPose();

    result = xrCreateReferenceSpace(session, &createInfo, &outSpace);
    if (XR_FAILED(result)) {
        LOGSTRF("XRViewManager: Failed to create reference space (result: %d)\n", result);
        return false;
    }

    return true;
}

bool XRViewManager::CreateReferenceSpace(XrReferenceSpaceType type) {
    if (!session_ || !session_->IsCreated()) {
        LOGSTR("XRViewManager: Session not created, cannot create reference space\n");
        return false;
    }

    // Destroy existing reference space if any
    if (reference_space_ != XR_NULL_HANDLE) {
        xrDestroySpace(reference_space_);
        reference_space_ = XR_NULL_HANDLE;
    }

    if (!CreateSpace(type, reference_space_)) {
        return false;
    }

    reference_space_type_ = type;
    if (render_space_ == XR_NULL_HANDLE || render_space_type_ == reference_space_type_) {
        render_space_ = reference_space_;
        render_space_type_ = reference_space_type_;
    }

    LOGSTRF("XRViewManager: Created reference space type %d\n", type);

    return true;
}

void XRViewManager::SetViewLockEnabled(bool enabled) {
    if (reference_space_ == XR_NULL_HANDLE) {
        return;
    }

    XrReferenceSpaceType desired = enabled ? XR_REFERENCE_SPACE_TYPE_VIEW : reference_space_type_;
    if (render_space_type_ == desired && render_space_ != XR_NULL_HANDLE) {
        return;
    }

    if (render_space_ != XR_NULL_HANDLE && render_space_ != reference_space_) {
        xrDestroySpace(render_space_);
        render_space_ = XR_NULL_HANDLE;
    }

    if (desired == reference_space_type_) {
        render_space_ = reference_space_;
        render_space_type_ = reference_space_type_;
        return;
    }

    XrSpace newSpace = XR_NULL_HANDLE;
    if (!CreateSpace(desired, newSpace)) {
        LOGSTR("XRViewManager: Failed to enable view lock; using reference space\n");
        render_space_ = reference_space_;
        render_space_type_ = reference_space_type_;
        return;
    }

    render_space_ = newSpace;
    render_space_type_ = desired;
}

//-----------------------------------------------------------------------------
// View Location
//-----------------------------------------------------------------------------

bool XRViewManager::LocateViews(XrTime display_time, XrViewState& view_state) {
    if (!session_ || !session_->IsCreated()) {
        return false;
    }

    if (reference_space_ == XR_NULL_HANDLE || render_space_ == XR_NULL_HANDLE) {
        return false;
    }

    XrSession session = session_->GetHandle();

    auto locate = [&](XrSpace space,
                      std::array<XrView, static_cast<size_t>(Eye::Count)>& outViews,
                      XrViewState& outState) -> bool {
        XrViewLocateInfo locateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = view_config_type_;
        locateInfo.displayTime = display_time;
        locateInfo.space = space;

        outState = {XR_TYPE_VIEW_STATE};
        for (auto& view : outViews) {
            view = {XR_TYPE_VIEW};
        }

        uint32_t viewCount = static_cast<uint32_t>(Eye::Count);
        XrResult result = xrLocateViews(
            session, &locateInfo, &outState, viewCount, &viewCount, outViews.data());

        if (XR_FAILED(result)) {
            return false;
        }

        bool positionValid = (outState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
        bool orientationValid = (outState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
        if (!orientationValid) {
            return false;
        }
        if (!positionValid) {
            for (auto& view : outViews) {
                view.pose.position = {0.0f, 0.0f, 0.0f};
            }
        }
        return true;
    };

    std::array<XrView, static_cast<size_t>(Eye::Count)> trackingViews;
    XrViewState trackingState = {XR_TYPE_VIEW_STATE};
    if (!locate(reference_space_, trackingViews, trackingState)) {
        views_valid_ = false;
        return false;
    }

    view_state = trackingState;

    for (auto& view : trackingViews) {
        if (recenter_active_) {
            view.pose = ComposePoses(recenter_pose_, view.pose);
        }
        NormalizePose(view.pose);
    }

    // Calculate combined head pose (average of both eyes)
    const auto& leftPose = trackingViews[static_cast<size_t>(Eye::Left)].pose;
    const auto& rightPose = trackingViews[static_cast<size_t>(Eye::Right)].pose;

    // Average position
    head_pose_.position.x = (leftPose.position.x + rightPose.position.x) * 0.5f;
    head_pose_.position.y = (leftPose.position.y + rightPose.position.y) * 0.5f;
    head_pose_.position.z = (leftPose.position.z + rightPose.position.z) * 0.5f;

    // Use left eye orientation (they should be nearly identical)
    head_pose_.orientation = leftPose.orientation;

    if (render_space_ == reference_space_) {
        for (size_t i = 0; i < static_cast<size_t>(Eye::Count); ++i) {
            views_[i].view = trackingViews[i];
        }
    } else {
        std::array<XrView, static_cast<size_t>(Eye::Count)> renderViews;
        XrViewState renderState = {XR_TYPE_VIEW_STATE};
        if (!locate(render_space_, renderViews, renderState)) {
            views_valid_ = false;
            return false;
        }
        for (auto& view : renderViews) {
            if (recenter_active_) {
                view.pose = ComposePoses(recenter_pose_, view.pose);
            }
            NormalizePose(view.pose);
        }
        for (size_t i = 0; i < static_cast<size_t>(Eye::Count); ++i) {
            views_[i].view = renderViews[i];
        }
    }

    views_valid_ = true;

    // Update matrices
    UpdateProjectionMatrices();
    UpdateViewMatrices();

    return true;
}

void XRViewManager::Recenter(bool yaw_only) {
    if (!views_valid_) {
        return;
    }

    if (yaw_only) {
        const XrPosef delta = ComputeYawOnlyRecenterPose(head_pose_);
        recenter_pose_ = recenter_active_
            ? ComposePoses(delta, recenter_pose_)
            : delta;
        recenter_active_ = true;
        return;
    }

    const XrPosef delta = InvertPose(head_pose_);
    recenter_pose_ = recenter_active_
        ? ComposePoses(delta, recenter_pose_)
        : delta;
    recenter_active_ = true;
}

//-----------------------------------------------------------------------------
// View Info Getters
//-----------------------------------------------------------------------------

uint32_t XRViewManager::GetRecommendedWidth() const {
    return views_[0].config.recommendedImageRectWidth;
}

uint32_t XRViewManager::GetRecommendedHeight() const {
    return views_[0].config.recommendedImageRectHeight;
}

uint32_t XRViewManager::GetMaxWidth() const {
    return views_[0].config.maxImageRectWidth;
}

uint32_t XRViewManager::GetMaxHeight() const {
    return views_[0].config.maxImageRectHeight;
}

const ViewInfo& XRViewManager::GetView(Eye eye) const {
    return views_[static_cast<size_t>(eye)];
}

//-----------------------------------------------------------------------------
// Head Pose Utilities
//-----------------------------------------------------------------------------

XMMATRIX XRViewManager::GetHeadPoseMatrix() const {
    if (!views_valid_) {
        return XMMatrixIdentity();
    }
    return XrPoseToMatrix(head_pose_);
}

XMFLOAT3 XRViewManager::GetHeadPosition() const {
    if (!views_valid_) {
        return XMFLOAT3(0.0f, 0.0f, 0.0f);
    }
    return GetPosePosition(head_pose_);
}

XMFLOAT3 XRViewManager::GetHeadForward() const {
    if (!views_valid_) {
        return XMFLOAT3(0.0f, 0.0f, -1.0f);
    }
    return GetPoseForward(head_pose_);
}

XMFLOAT3 XRViewManager::GetHeadUp() const {
    if (!views_valid_) {
        return XMFLOAT3(0.0f, 1.0f, 0.0f);
    }
    return GetPoseUp(head_pose_);
}

XMFLOAT3 XRViewManager::GetHeadRotation() const {
    if (!views_valid_) {
        return XMFLOAT3(0.0f, 0.0f, 0.0f);
    }
    return QuatToEuler(head_pose_.orientation);
}

XrPosef XRViewManager::GetHeadPose() const {
    if (!views_valid_) {
        return IdentityPose();
    }
    return head_pose_;
}

//-----------------------------------------------------------------------------
// Projection Settings
//-----------------------------------------------------------------------------

void XRViewManager::SetClipPlanes(float nearZ, float farZ) {
    near_z_ = nearZ;
    far_z_ = farZ;

    // Update projection matrices if views are valid
    if (views_valid_) {
        UpdateProjectionMatrices();
    }
}

//-----------------------------------------------------------------------------
// Matrix Updates
//-----------------------------------------------------------------------------

void XRViewManager::UpdateProjectionMatrices() {
    for (size_t i = 0; i < static_cast<size_t>(Eye::Count); ++i) {
        views_[i].projection_matrix = XrFovToProjectionMatrixD3D(
            views_[i].view.fov, near_z_, far_z_);
    }
}

void XRViewManager::UpdateViewMatrices() {
    for (size_t i = 0; i < static_cast<size_t>(Eye::Count); ++i) {
        views_[i].view_matrix = XrPoseToViewMatrix(views_[i].view.pose);
    }
}

} // namespace XR
} // namespace OVRInject
