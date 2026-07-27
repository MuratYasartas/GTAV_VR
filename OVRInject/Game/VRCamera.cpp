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

// Frame conventions (derived + hand-verified 2026-07-26):
//
// GTA V world: x=east, y=north, z=up. Camera basis B uses the GAME matrix
// layout: rows (right, forward, up) in GTA-world coords. Order-2 euler
// (degrees): rx=pitch about cam-right (positive=look up), ry=roll about
// cam-forward, rz=yaw about world-up (0=north, positive=counterclockwise).
// Row-vector build B = Rx*Ry*Rz yields exactly rows (right, forward, up).
//
// XR head pose (XrPoseToMatrix): rows are the head's local axes in tracking
// coords with x=right, y=up, z=BACK (OpenXR forward is -Z).
//
// Delta chain:
//   D_local = H * R^-1   (head delta in the reference head-local frame)
//   D_cam   = Jt * D_local * J   (axis remap XR -> GTA-cam-local)
//   B'      = D_cam * B          (delta applied in camera-LOCAL space)
// with J rows [(1,0,0),(0,0,1),(0,-1,0)]: xr-x -> cam-right, xr-y -> cam-up,
// xr-z(back) -> -cam-forward.
// Hand-checked: pure yaw-left -> D_cam=Rz(+a) -> cam yaw +a (CCW=left);
// pure pitch-up -> Rx(+a) -> pitch +a (up); roll-right -> Ry(+a).

// XR-head-local -> GTA-cam-local axis remap.
const DirectX::XMMATRIX kXrToCam =
    DirectX::XMMATRIX(1, 0, 0, 0,
                      0, 0, 1, 0,
                      0, -1, 0, 0,
                      0, 0, 0, 1);

DirectX::XMMATRIX GtaEulerToBasis(float rxDeg, float ryDeg, float rzDeg) {
    using namespace DirectX;
    return XMMatrixRotationX(rxDeg * kDegToRad) *
           XMMatrixRotationY(ryDeg * kDegToRad) *
           XMMatrixRotationZ(rzDeg * kDegToRad);
    // rows: r0=right, r1=forward, r2=up (GTA world coords)
}

// Inverse of GtaEulerToBasis (exact round-trip for |roll| < 90 deg).
void BasisToGtaEuler(const DirectX::XMMATRIX& b, float& rxDeg, float& ryDeg, float& rzDeg) {
    const float rightX = b.r[0].m128_f32[0];
    const float rightY = b.r[0].m128_f32[1];
    const float rightZ = b.r[0].m128_f32[2];
    const float fwdZ = b.r[1].m128_f32[2];
    const float upZ = b.r[2].m128_f32[2];

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
    if (!shv.IsAvailable()) {
        // The bridge script thread may start AFTER our injection (ScriptHookV
        // brings scripts up late in the game boot). Retry on a slow cadence;
        // cheap - OpenFileMapping on failure.
        const uint64_t nowMs = GetTickCount64();
        if (nowMs - lastBridgeRetryMs_ >= 5000) {
            lastBridgeRetryMs_ = nowMs;
            shv.Initialize();
        }
        return;
    }

    // Cutscenes / loading / menus: give the camera back to the game.
    if (gameState && (gameState->IsCutsceneActive() || gameState->IsLoading() ||
                      gameState->IsInMenu())) {
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
    const DirectX::XMMATRIX basis =
        GtaEulerToBasis(snap.rotX, snap.rotY, snap.rotZ);

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

    // Head-motion prediction (rotation): the camera write lands ~1-2 frames
    // before photon (bridge tick + render), so the live pose is stale when
    // the frame displays - the "blurry/laggy when turning" complaint. Extrap
    // olate ~20ms ahead from the last two poses, capped at 12 degrees to stay
    // stable on direction changes. GTAVR_HEAD_PREDICT_MS=0 disables.
    {
        static DirectX::XMMATRIX prevRot = DirectX::XMMatrixIdentity();
        static uint64_t prevTick = 0;
        static bool havePrev = false;
        static float predictMs = [] {
            char v[16] = {};
            DWORD n = GetEnvironmentVariableA("GTAVR_HEAD_PREDICT_MS", v, sizeof(v));
            return (n > 0) ? static_cast<float>(atof(v)) : 20.0f;
        }();
        const uint64_t nowTick = GetTickCount64();
        if (predictMs > 0.0f && havePrev && prevTick != 0) {
            const float dt = static_cast<float>(nowTick - prevTick) / 1000.0f;
            if (dt > 0.002f && dt < 0.25f) {
                // World-frame delta from prev to cur (row-vector: D = prev^-1 * cur),
                // scaled by predictMs/dt and capped at 12 degrees.
                const DirectX::XMMATRIX deltaMat = DirectX::XMMatrixMultiply(
                    DirectX::XMMatrixInverse(nullptr, prevRot), headRot);
                const DirectX::XMVECTOR delta = XMQuaternionRotationMatrix(deltaMat);
                float t = (predictMs / 1000.0f) / dt;
                DirectX::XMFLOAT4 dq;
                DirectX::XMStoreFloat4(&dq, delta);
                float angle = 2.0f * acosf((std::min)(1.0f, fabsf(dq.w)));
                const float maxAngle = 12.0f * (3.14159265f / 180.0f);
                if (angle * t > maxAngle && angle > 0.0001f) {
                    t = maxAngle / angle;
                }
                const DirectX::XMVECTOR predDelta = DirectX::XMQuaternionSlerp(
                    DirectX::XMQuaternionIdentity(), delta, t);
                headRot = DirectX::XMMatrixMultiply(
                    headRot, DirectX::XMMatrixRotationQuaternion(predDelta));
            }
        }
        if (!havePrev || nowTick != prevTick) {
            prevRot = headRot;
            prevTick = nowTick;
            havePrev = true;
        }
    }

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

    // Head delta in the reference head-local frame, remapped to cam-local.
    const DirectX::XMMATRIX dLocal = headTracking
        ? DirectX::XMMatrixMultiply(headRot, refInv)
        : DirectX::XMMatrixIdentity();
    const DirectX::XMMATRIX kJt = DirectX::XMMatrixTranspose(kXrToCam);
    const DirectX::XMMATRIX dCam = DirectX::XMMatrixMultiply(
        DirectX::XMMatrixMultiply(kJt, dLocal), kXrToCam);

    // Apply in camera-local space, then carry the world basis.
    const DirectX::XMMATRIX finalBasis = DirectX::XMMatrixMultiply(dCam, basis);

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
        // tracking coords -> ref-head-local -> cam-local -> GTA world
        const DirectX::XMMATRIX trackToWorld = DirectX::XMMatrixMultiply(
            DirectX::XMMatrixMultiply(refInv, kXrToCam), basis);
        totalOffset = DirectX::XMVectorAdd(
            totalOffset, DirectX::XMVector3Transform(delta, trackToWorld));
    } else {
        hasRefPos_ = false;
    }

    // User camera offsets (same semantics as the memory path): X right,
    // Y up, Z forward in camera space, plus the standing-height correction.
    {
        const float heightOffset = cameraSettings.playerHeight.load() - 1.7f;
        // cam-local component order for the basis rows (right, forward, up)
        const DirectX::XMVECTOR localCam = DirectX::XMVectorScale(
            DirectX::XMVectorSet(cameraSettings.cameraOffsetX.load(),
                                 cameraSettings.cameraOffsetZ.load(),
                                 cameraSettings.cameraOffsetY.load() + heightOffset,
                                 0.0f),
            worldScale);
        totalOffset = DirectX::XMVectorAdd(
            totalOffset, DirectX::XMVector3Transform(localCam, finalBasis));
    }

    // Per-eye stereo offset (AER): XR eye offset (head-local) -> cam-local ->
    // world. Auto mode (default) uses the runtime's own eye offset - the
    // physically correct IPD for the HMD; manual mode scales it to the
    // overlay's stereoIPD value (or falls back to it entirely).
    if (stereoSettings.mode.load() == static_cast<int>(VR::StereoMode::AlternateEye)) {
        const bool ipdAuto = stereoSettings.ipdAuto.load();
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
            // Runtime reports no eye offset: slider is the only source.
            const float sign = (eye == VR::Eye::Left) ? -0.5f : 0.5f;
            eyeLocal = DirectX::XMVectorSet(sign * desiredIpd, 0, 0, 0);
        } else if (!ipdAuto) {
            eyeLocal = DirectX::XMVectorScale(eyeLocal, desiredIpd / runtimeIpd);
        }
        eyeLocal = DirectX::XMVectorScale(eyeLocal, worldScale);
        totalOffset = DirectX::XMVectorAdd(
            totalOffset, DirectX::XMVector3Transform(
                             eyeLocal, DirectX::XMMatrixMultiply(kXrToCam, finalBasis)));
    }

    const DirectX::XMVECTOR finalPos = DirectX::XMVectorAdd(
        DirectX::XMVectorSet(snap.coordX, snap.coordY, snap.coordZ, 0.0f),
        totalOffset);

    float rx, ry, rz;
    BasisToGtaEuler(finalBasis, rx, ry, rz);

    shv.CamSetCoord(cam_,
                    DirectX::XMVectorGetX(finalPos),
                    DirectX::XMVectorGetY(finalPos),
                    DirectX::XMVectorGetZ(finalPos));
    shv.CamSetRot(cam_, rx, ry, rz);

    // FOV: default = the XR per-eye vertical FOV, so the game frustum covers
    // the HMD's (paired with the angular crop in the blit - fixes the
    // "zoomed-in, low-res" feel of the raw pass-through). The overlay's
    // Camera FOV Override takes over when enabled; gameplay aim-zoom is only
    // used as a fallback when the XR FOV is unavailable.
    float xrVfovDeg = 0.0f;
    if (backend_) {
        const DirectX::XMMATRIX proj = backend_->GetProjectionMatrix(eye, 0.1f, 100.0f);
        const float m11 = proj.r[1].m128_f32[1];
        if (m11 > 0.01f) {
            xrVfovDeg = 2.0f * atanf(1.0f / m11) * kRadToDeg;
        }
    }
    float desiredFov = (xrVfovDeg > 30.0f) ? xrVfovDeg : snap.fov;
    auto& fovSettings = VR::GetFovSettings();
    if (fovSettings.enabled.load()) {
        if (fovSettings.perType.load() && gameState) {
            if (gameState->IsInVehicle()) {
                desiredFov = gameState->IsFirstPerson()
                    ? fovSettings.fpVehicleFov.load()
                    : fovSettings.tpVehicleFov.load();
            } else if (gameState->IsAiming()) {
                desiredFov = fovSettings.tpAimFov.load();
            } else {
                desiredFov = gameState->IsFirstPerson()
                    ? fovSettings.fpPedFov.load()
                    : fovSettings.tpPedFov.load();
            }
        } else {
            desiredFov = fovSettings.globalFov.load();
        }
    }
    if (desiredFov > 1.0f) {
        // Published for the blit's angular crop (D3DHooks_VRManager).
        VR::GetRuntimeStats().activeFov.store(desiredFov);
        if (std::fabs(desiredFov - lastFov_) > 0.25f) {
            shv.CamSetFov(cam_, desiredFov);
            lastFov_ = desiredFov;
            LOGSTRF("VRCamera: cam FOV -> %.1f (%s)\n", desiredFov,
                    fovSettings.enabled.load() ? "override" : "XR-matched");
        }
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
