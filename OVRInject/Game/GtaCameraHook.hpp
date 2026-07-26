#pragma once

#include "../VR/IVRBackend.hpp"
#include "../VR/SharedSettings.hpp"
#include "GtaCameraFov.hpp"  // complete type needed for LifetimeToken member
#include <Windows.h>
#include <DirectXMath.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace OVRInject {
namespace Game {

class GtaGameState;  // Forward declaration

/**
 * GtaCameraHook - Injects VR head pose into GTA V's camera system
 *
 * Uses pattern scanning with config file support (gtavr_camera.ini)
 * to find and modify the game's camera matrix.
 *
 * Supports decoupling: VR head rotation is COMPOSED with game camera rotation
 * instead of replacing it, allowing independent head movement.
 */
class GtaCameraHook {
public:
    GtaCameraHook(VR::IVRBackend* backend, GtaCameraFov* cameraFov = nullptr);
    ~GtaCameraHook();

    // Starts camera resolution on the background worker thread and returns
    // immediately. Does NOT scan on the calling (render) thread; the camera
    // becomes ready asynchronously (poll IsReady). Returns false (never
    // ready synchronously) - kept for source compatibility with callers
    // that ignore the result.
    bool Hook();

    // Update the camera with the current VR head pose
    // Uses decoupling based on game state
    // O(1) render-thread cost: consumes worker results via lock-free handoff.
    void Update(VR::Eye eye);

    // Update with explicit game state reference
    void Update(VR::Eye eye, GtaGameState* gameState);

    // Stand down the resolution worker (and skip render-thread consumption)
    // when the scripted-camera path owns the camera. Without this the worker
    // spins at 100% of a core: its published candidates are never consumed
    // (Update() is not called), handoff_pending_ stays set forever, and the
    // give-up path never engages (observed live: ~1500 passes/sec, 9.8 GB of
    // log spam in one session).
    void SetStandby(bool on) { standby_.store(on, std::memory_order_release); }
    bool IsStandby() const { return standby_.load(std::memory_order_acquire); }

    // Returns true if we successfully found the camera
    bool IsReady() const;

    // Reset the reference pose (recenter)
    void RecenterPose();

    // Enable/disable decoupling at runtime
    void SetDecouplingEnabled(bool enabled) { decoupling_enabled_ = enabled; }
    bool IsDecouplingEnabled() const { return decoupling_enabled_; }
    void SetCameraFov(GtaCameraFov* cameraFov) { camera_fov_ = cameraFov; }

private:
    // Configuration loaded from gtavr_camera.ini
    struct CameraConfig {
        std::wstring module = L"GTA5.exe";
        std::string pattern;
        int64_t patternOffset = 0;
        std::vector<int64_t> relativeOffsets;
        int64_t ripOffset = 0;
        bool ripOffsetSet = false;
        std::vector<int64_t> pointerOffsets;
        int64_t matrixOffset = 0;
        bool negateRight = false;
        bool negateForward = false;
        bool negateUp = false;
        // [camera] resolveTimeoutSec - max wall-clock seconds for camera
        // resolution before giving up to mono mode (default 30).
        int resolveTimeoutSec = 30;
        // [camera] backgroundRetrySec - cheap background retry cadence after
        // give-up (default 60; 0 = only manual recenter retries).
        int backgroundRetrySec = 60;
    };

    // GTA camera matrix structure
    struct GtaCameraMatrix {
        float right[4];
        float forward[4];
        float up[4];
        float position[4];
    };

    bool LoadConfig(CameraConfig& outConfig);
    bool ResolveMatrixAddress(const CameraConfig& config, uintptr_t& outAddress);
    std::wstring ResolveConfigPath() const;
    bool IsReadable(uintptr_t address, size_t size) const;
    bool IsWritable(uintptr_t address, size_t size) const;
    bool TryResolveFromActiveCamera(uintptr_t& outAddress, uintptr_t& outCameraBase);
    bool TryResolveFromMetadataObjects(uintptr_t& outAddress, uintptr_t& outCameraBase,
                                       uint64_t deadlineTick) const;
    bool FindMatrixInCamera(uintptr_t cameraBase, uintptr_t& outAddress, size_t scanSize = 0x1000) const;
    bool FindMatrixViaPointerScan(uintptr_t base, uintptr_t& outAddress, uintptr_t& outCameraBase) const;
    bool FindCameraByMetadataScan(uintptr_t base, uintptr_t& outCameraBase) const;
    bool FindMetadataInCamera(uintptr_t cameraBase, uintptr_t& outMetadata) const;
    bool TryReadCameraHashes(uintptr_t cameraBase, uint32_t& outHashKey, uint32_t& outHashName) const;
    bool SafeRead(uintptr_t address, void* outData, size_t size) const;
    bool SafeReadPtr(uintptr_t address, uintptr_t& outValue) const;
    bool SafeWrite(uintptr_t address, const void* data, size_t size) const;
    bool ScoreMatrix(const GtaCameraMatrix& matrix, float& outScore) const;
    bool TryDirectMatrixScan(const CameraConfig& config, uintptr_t& outAddress);

    // Background worker: all expensive camera resolution (full-module pattern
    // scans, whole-process metadata sweeps) runs on this thread. The render
    // thread only consumes validated candidates through the lock-free SPSC
    // handoff below, keeping Update() O(1).
    void StartWorker();
    void StopWorker();
    void WorkerMain();
    DWORD WaitForWorkerEvent(uint32_t timeoutMs) const;
    bool ShouldAbortScan(uint64_t deadlineTick) const;
    bool RunResolutionPass(bool allowFullSweep, uint64_t deadlineTick, uint32_t passIndex);
    void RunReadyRefresh();
    // Worker-side, pure-read candidate validation (no writes, no calls into
    // game code): metadata check + writability + two ScoreMatrix samples
    // ~40ms apart, then publishes via the handoff.
    bool ValidateAndPublishCandidate(uintptr_t candidate, uintptr_t cameraBase, const char* source);
    // Render-thread side of the handoff: O(1) adopt of a validated candidate.
    void ConsumeWorkerCandidate();

    // Core camera update logic
    void UpdateWithDecoupling(VR::Eye eye, bool isAiming);
    void UpdateWithoutDecoupling(VR::Eye eye);
    void WriteCameraMatrix(const DirectX::XMMATRIX& rotation, const DirectX::XMFLOAT4& position);
    DirectX::XMFLOAT4 ComputeCameraPosition(const GtaCameraMatrix& gameMatrix,
                                            const DirectX::XMMATRIX& finalRotation,
                                            const DirectX::XMMATRIX& trackingToWorld,
                                            const DirectX::XMMATRIX& headPose,
                                            VR::Eye eye,
                                            bool applyEyeOffset);

    // Decoupling helpers
    DirectX::XMMATRIX ExtractRotationMatrix(const GtaCameraMatrix& matrix) const;
    DirectX::XMMATRIX ClampRotationToCone(const DirectX::XMMATRIX& rotation, float maxAngleDeg) const;
    DirectX::XMMATRIX ComposeRotations(const DirectX::XMMATRIX& base, const DirectX::XMMATRIX& delta) const;

    // Read current game camera
    bool ReadGameCamera(GtaCameraMatrix& outMatrix) const;

    VR::IVRBackend* backend_;
    CameraConfig config_;
    // Render-thread-owned camera state. The worker NEVER writes these two;
    // it publishes candidates via the handoff and Update() adopts them.
    // Atomics because the worker reads them (ready check / refresh compare).
    std::atomic<uint64_t> matrix_address_{0};
    std::atomic<bool> hook_ready_{false};
    bool config_loaded_ = false;

    // Decoupling state
    bool decoupling_enabled_ = true;
    DirectX::XMMATRIX reference_vr_rotation_ = DirectX::XMMatrixIdentity();  // VR rotation at recenter
    DirectX::XMMATRIX last_game_rotation_ = DirectX::XMMatrixIdentity();     // Last known game rotation
    bool has_reference_pose_ = false;
    DirectX::XMVECTOR reference_vr_position_ = DirectX::XMVectorZero();
    bool has_reference_position_ = false;

    // World rotation tracking for smooth cutscene transitions
    DirectX::XMMATRIX world_rotation_offset_ = DirectX::XMMatrixIdentity();  // Accumulated world rotation
    DirectX::XMMATRIX pre_cutscene_rotation_ = DirectX::XMMatrixIdentity();  // Rotation before cutscene
    bool was_in_cutscene_ = false;                                            // Previous frame cutscene state
    float cutscene_blend_alpha_ = 0.0f;                                       // Blend factor for transitions
    static constexpr float kCutsceneBlendSpeed = 5.0f;                        // Blend speed per second

    // For debugging
    uint32_t update_count_ = 0;
    uint32_t write_success_count_ = 0;

    // Active camera tracking (written by the worker during scans and by the
    // render thread when adopting a candidate - hence atomic).
    std::atomic<uint64_t> active_camera_base_{0};
    mutable uintptr_t last_logged_camera_base_ = 0;
    // Throttle for the whole-process metadata sweep (see metadataSweepIntervalSec
    // in manifests/gtav_legacy.ini; default 5s). Worker thread only.
    mutable uint64_t last_metadata_sweep_tick_ = 0;
    std::atomic<GtaCameraFov*> camera_fov_{nullptr};

    // --- Background worker state --------------------------------------------
    std::thread worker_thread_;
    std::atomic<bool> worker_stop_{false};
    std::atomic<bool> worker_started_{false};
    HANDLE worker_wake_event_ = nullptr;  // auto-reset; signaled on stop/retry
    // Fov scanner snapshot taken in StartWorker (object known-alive there);
    // used by the worker only, always under worker_fov_token_'s lock.
    GtaCameraFov* worker_fov_ = nullptr;
    std::shared_ptr<GtaCameraFov::LifetimeToken> worker_fov_token_;

    // Lock-free SPSC handoff (single producer = worker, single consumer =
    // render Update()). Protocol: worker fills address/base/hashes with
    // relaxed stores, then sets pending with release; the render thread
    // acquires pending, reads the fields, clears pending. The worker never
    // overwrites an unconsumed candidate.
    std::atomic<uint64_t> handoff_address_{0};
    std::atomic<uint64_t> handoff_base_{0};
    std::atomic<uint64_t> handoff_hash_key_{0};
    std::atomic<uint64_t> handoff_hash_name_{0};
    std::atomic<bool> handoff_pending_{false};

    // Standby mode (scripted camera owns the view): the worker idles long
    // and Update() returns immediately.
    std::atomic<bool> standby_{false};

    // Set by RecenterPose (manual retry) - the worker runs one full pass.
    std::atomic<bool> retry_requested_{false};
};

} // namespace Game
} // namespace OVRInject
