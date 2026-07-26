#include "VRCamera.hpp"
#include "ShvNatives.hpp"
#include "GtaGameState.hpp"
#include "../Log.hpp"

#include <cmath>

namespace OVRInject {
namespace Game {

namespace {

constexpr float kDegToRad = 0.01745329251994329577f;
constexpr float kRadToDeg = 57.295779513082320876f;

// GTA euler (order 2, degrees) -> internal matrix with rows (right, up,
// forward) holding GTA-world axis values. GTA-native row-vector build:
// M = Rx(rx) * Ry(ry) * Rz(rz) gives rows (right, forward, up); we swap the
// last two rows into the hook's internal layout.
DirectX::XMMATRIX GtaEulerToInternal(float rxDeg, float ryDeg, float rzDeg) {
    using namespace DirectX;
    const XMMATRIX m = XMMatrixRotationX(rxDeg * kDegToRad) *
                       XMMatrixRotationY(ryDeg * kDegToRad) *
                       XMMatrixRotationZ(rzDeg * kDegToRad);
    XMMATRIX r;
    r.r[0] = m.r[0];              // right
    r.r[1] = m.r[2];              // up
    r.r[2] = m.r[1];              // forward
    r.r[3] = XMVectorSet(0, 0, 0, 1);
    return r;
}

// Inverse of the above. For rows (right, up, forward) with the GTA order-2
// factorization M = Rx*Ry*Rz (rows right/forward/up):
//   pitch = atan2(forward.z, up.z)
//   roll  = asin(-right.z)
//   yaw   = atan2(right.y, right.x)
void InternalToGtaEuler(const DirectX::XMMATRIX& r, float& rxDeg, float& ryDeg, float& rzDeg) {
    const float rightX = r.r[0].m128_f32[0];
    const float rightY = r.r[0].m128_f32[1];
    const float rightZ = r.r[0].m128_f32[2];
    const float upZ = r.r[1].m128_f32[2];
    const float fwdZ = r.r[2].m128_f32[2];

    float s = -rightZ;
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    ryDeg = asinf(s) * kRadToDeg;
    rxDeg = atan2f(fwdZ, upZ) * kRadToDeg;
    rzDeg = atan2f(rightY, rightX) * kRadToDeg;
}

} // namespace

VRCamera::VRCamera(VR::IVRBackend* backend) : backend_(backend) {}

VRCamera::~VRCamera() {
    Shutdown();
}

bool VRCamera::IsAvailable() const {
    return ShvNatives::Get().IsAvailable();
}

void VRCamera::Recenter() {
    hasRefRot_ = false;
    hasRefPos_ = false;
}

void VRCamera::Engage() {
    auto& shv = ShvNatives::Get();
    shv.CamSetActive(cam_, true);
    shv.CamRender(cam_, true);
    engaged_ = true;
    hasRefRot_ = false;  // re-reference on resume so the view does not snap
    hasRefPos_ = false;
    LOGSTRF("VRCamera: engaged (cam=%d)\n", cam_);
}

void VRCamera::Disengage() {
    if (!engaged_) return;
    auto& shv = ShvNatives::Get();
    shv.CamRender(cam_, false);
    shv.CamSetActive(cam_, false);
    engaged_ = false;
    LOGSTR("VRCamera: disengaged (cutscene/loading)\n");
}

void VRCamera::Shutdown() {
    if (cam_ == 0) return;
    auto& shv = ShvNatives::Get();
    if (engaged_) {
        shv.CamRender(cam_, false);
        shv.CamSetActive(cam_, false);
        engaged_ = false;
    }
    shv.CamDestroy(cam_);
    LOGSTRF("VRCamera: released (cam=%d)\n", cam_);
    cam_ = 0;
    createRequested_ = false;
}

void VRCamera::Update(VR::Eye eye, GtaGameState* gameState) {
    auto& shv = ShvNatives::Get();
    if (!shv.IsAvailable()) return;

    // Cutscenes / loading: give the camera back to the game.
    if (gameState && (gameState->IsCutsceneActive() || gameState->IsLoading())) {
        Disengage();
        return;
    }

    // Scripted cam lifecycle: request once, adopt the handle when the bridge
    // reports it (op executes on its next tick).
    if (cam_ == 0) {
        if (!createRequested_) {
            shv.CamCreate();
            createRequested_ = true;
            LOGSTR("VRCamera: scripted cam create requested\n");
        }
        int created = shv.LastCreatedCam();
        if (created != 0) {
            cam_ = created;
            LOGSTRF("VRCamera: scripted cam created (cam=%d)\n", cam_);
        }
        return;
    }

    if (!engaged_) {
        Engage();
    }

    const ShvNatives::CamSnapshot snap = shv.Snapshot();

    // Base pose from the gameplay camera (still engine-driven: follows the
    // player, vehicles, aiming - we only add the VR delta on top).
    DirectX::XMMATRIX gameRot = GtaEulerToInternal(snap.rotX, snap.rotY, snap.rotZ);

    // Head pose (late-latched by the stereo engine already).
    DirectX::XMMATRIX headPose = backend_ ? backend_->GetHeadPoseMatrix()
                                          : DirectX::XMMatrixIdentity();
    auto& view = VR::GetViewSettings();
    const float snapYaw = view.snapYawOffsetDeg.load();
    if (std::fabs(snapYaw) > 0.001f) {
        headPose = DirectX::XMMatrixRotationY(snapYaw * kDegToRad) * headPose;
    }
    DirectX::XMMATRIX headRot = headPose;
    headRot.r[3] = DirectX::XMVectorSet(0, 0, 0, 1);

    auto& stereoSettings = VR::GetStereoSettings();
    auto& cameraSettings = VR::GetCameraSettings();
    const bool headTracking = stereoSettings.headTracking.load();
    const bool positionTracking = stereoSettings.positionTracking.load();

    if ((headTracking || positionTracking) && !hasRefRot_) {
        refRot_ = headRot;
        hasRefRot_ = true;
    }
    const DirectX::XMMATRIX refInv = hasRefRot_
        ? DirectX::XMMatrixInverse(nullptr, refRot_)
        : DirectX::XMMatrixIdentity();

    const DirectX::XMMATRIX vrDelta = headTracking
        ? DirectX::XMMatrixMultiply(refInv, headRot)
        : DirectX::XMMatrixIdentity();

    // Same composition as the memory path: delta in camera-local space.
    const DirectX::XMMATRIX finalRot = DirectX::XMMatrixMultiply(gameRot, vrDelta);

    // --- Position -----------------------------------------------------------
    float worldScale = cameraSettings.worldScale.load();
    if (worldScale < 0.01f) worldScale = 0.01f;

    DirectX::XMVECTOR totalOffset = DirectX::XMVectorZero();

    if (positionTracking) {
        const DirectX::XMVECTOR headPos = DirectX::XMVectorSet(
            headPose.r[3].m128_f32[0], headPose.r[3].m128_f32[1],
            headPose.r[3].m128_f32[2], 0.0f);
        if (!hasRefPos_) {
            refPos_ = headPos;
            hasRefPos_ = true;
        }
        DirectX::XMVECTOR delta = DirectX::XMVectorSubtract(headPos, refPos_);
        delta = DirectX::XMVectorScale(delta, worldScale);
        const DirectX::XMMATRIX trackingToWorld =
            DirectX::XMMatrixMultiply(gameRot, refInv);
        totalOffset = DirectX::XMVectorAdd(
            totalOffset, DirectX::XMVector3Transform(delta, trackingToWorld));
    } else {
        hasRefPos_ = false;
    }

    // Per-eye stereo offset (AER): the eye's tracking-space offset rotated
    // into the world by the final orientation, scaled to the configured IPD.
    if (stereoSettings.mode.load() == static_cast<int>(VR::StereoMode::AlternateEye)) {
        const float desiredIpd = stereoSettings.stereoIPD.load();
        DirectX::XMVECTOR eyeLocal = DirectX::XMVectorZero();
        if (backend_) {
            const DirectX::XMMATRIX eyeMatrix = backend_->GetEyeMatrix(eye);
            eyeLocal = DirectX::XMVectorSet(eyeMatrix.r[3].m128_f32[0],
                                            eyeMatrix.r[3].m128_f32[1],
                                            eyeMatrix.r[3].m128_f32[2], 0.0f);
        }
        const float runtimeIpd = std::fabs(DirectX::XMVectorGetX(eyeLocal)) * 2.0f;
        if (runtimeIpd < 0.0001f) {
            const float sign = (eye == VR::Eye::Left) ? -0.5f : 0.5f;
            eyeLocal = DirectX::XMVectorSet(sign * desiredIpd, 0, 0, 0);
        } else {
            eyeLocal = DirectX::XMVectorScale(eyeLocal, desiredIpd / runtimeIpd);
        }
        eyeLocal = DirectX::XMVectorScale(eyeLocal, worldScale);
        totalOffset = DirectX::XMVectorAdd(
            totalOffset, DirectX::XMVector3Transform(eyeLocal, finalRot));
    }

    const DirectX::XMVECTOR finalPos = DirectX::XMVectorAdd(
        DirectX::XMVectorSet(snap.coordX, snap.coordY, snap.coordZ, 0.0f),
        totalOffset);

    float rx, ry, rz;
    InternalToGtaEuler(finalRot, rx, ry, rz);

    shv.CamSetCoord(cam_,
                    DirectX::XMVectorGetX(finalPos),
                    DirectX::XMVectorGetY(finalPos),
                    DirectX::XMVectorGetZ(finalPos));
    shv.CamSetRot(cam_, rx, ry, rz);

    // FOV pass-through (gameplay FOV, including aim zoom). XR-matched FOV is
    // a follow-up; keep one variable at a time.
    if (snap.fov > 1.0f && std::fabs(snap.fov - lastFov_) > 0.25f) {
        shv.CamSetFov(cam_, snap.fov);
        lastFov_ = snap.fov;
    }

    updateCount_++;
    if (!loggedFirstPose_) {
        loggedFirstPose_ = true;
        LOGSTRF("VRCamera: first pose - base rot=(%.1f, %.1f, %.1f) coord=(%.1f, %.1f, %.1f) fov=%.1f eye=%d\n",
                snap.rotX, snap.rotY, snap.rotZ,
                snap.coordX, snap.coordY, snap.coordZ, snap.fov,
                static_cast<int>(eye));
        LOGSTRF("VRCamera: first write - rot=(%.1f, %.1f, %.1f) pos=(%.1f, %.1f, %.1f)\n",
                rx, ry, rz,
                DirectX::XMVectorGetX(finalPos),
                DirectX::XMVectorGetY(finalPos),
                DirectX::XMVectorGetZ(finalPos));
    }
    if (updateCount_ % 300 == 0) {
        LOGSTRF("VRCamera: #%u base rot=(%.1f, %.1f, %.1f) -> write rot=(%.1f, %.1f, %.1f) snapSeq=%u\n",
                updateCount_, snap.rotX, snap.rotY, snap.rotZ, rx, ry, rz, snap.sequence);
    }
    lastSnapSeq_ = snap.sequence;
}

} // namespace Game
} // namespace OVRInject
