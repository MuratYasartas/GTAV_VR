#include "TestFramework.hpp"

#include "../OVRInject/Stereo/RenderPoseHistory.hpp"
#include "../OVRInject/OpenXR/XRCore.hpp"

using namespace DirectX;
using namespace OVRInject::Stereo;

namespace {

XMMATRIX PoseWithId(float id) {
    return XMMatrixTranslation(id, id * 2.0f, -id);
}

float PoseId(const XMMATRIX* pose) {
    return pose ? pose->r[3].m128_f32[0] : -1.0f;
}

} // namespace

TEST(RenderPoseHistory_PendingPoseBecomesTexturePoseOnNextRender) {
    RenderPoseHistory history;
    history.RecordCameraWrite(1, PoseWithId(42.0f));
    CHECK(history.HasPendingPose(1));
    CHECK(history.GetTexturePose(1) == nullptr);
    CHECK(history.CommitRenderedTexture(1));
    CHECK_NEAR(PoseId(history.GetTexturePose(1)), 42.0f, 1.0e-6f);
}

TEST(RenderPoseHistory_StaleEyeKeepsItsOwnOlderPose) {
    RenderPoseHistory history;
    history.RecordCameraWrite(0, PoseWithId(10.0f));
    history.CommitRenderedTexture(0);
    history.RecordCameraWrite(1, PoseWithId(20.0f));
    history.CommitRenderedTexture(1);
    history.RecordCameraWrite(0, PoseWithId(30.0f));
    history.CommitRenderedTexture(0);

    CHECK_NEAR(PoseId(history.GetTexturePose(0)), 30.0f, 1.0e-6f);
    CHECK_NEAR(PoseId(history.GetTexturePose(1)), 20.0f, 1.0e-6f);
}

TEST(RenderPoseHistory_MissedFrameDoesNotRelabelTexture) {
    RenderPoseHistory history;
    history.RecordCameraWrite(0, PoseWithId(5.0f));
    history.CommitRenderedTexture(0);
    history.RecordCameraWrite(0, PoseWithId(6.0f));
    CHECK_NEAR(PoseId(history.GetTexturePose(0)), 5.0f, 1.0e-6f);
}

TEST(RenderPoseHistory_ResetInvalidatesPendingAndTexturePoses) {
    RenderPoseHistory history;
    history.RecordCameraWrite(0, PoseWithId(1.0f));
    history.CommitRenderedTexture(0);
    history.Reset();
    CHECK(!history.HasPendingPose(0));
    CHECK(history.GetTexturePose(0) == nullptr);
}

TEST(RenderPoseHistory_OpenXrPoseConversionRoundTripsRigidMatrix) {
    const XMMATRIX original =
        XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(17.0f),
            XMConvertToRadians(-63.0f),
            XMConvertToRadians(8.0f)) *
        XMMatrixTranslation(1.2f, -0.4f, 2.7f);
    const XrPosef pose = OVRInject::XR::MatrixToXrPose(original);
    const XMMATRIX reconstructed = OVRInject::XR::XrPoseToMatrix(pose);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            CHECK_NEAR(
                reconstructed.r[row].m128_f32[column],
                original.r[row].m128_f32[column],
                1.0e-5f);
        }
    }
}
