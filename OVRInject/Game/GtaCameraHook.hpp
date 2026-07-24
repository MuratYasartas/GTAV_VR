#pragma once

#include "../VR/IVRBackend.hpp"
#include "../VR/SharedSettings.hpp"
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace OVRInject {
namespace Game {

class GtaGameState;  // Forward declaration
class GtaCameraFov;

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

    // Try to find and hook the camera. Returns true on success.
    bool Hook();

    // Update the camera with the current VR head pose
    // Uses decoupling based on game state
    void Update(VR::Eye eye);

    // Update with explicit game state reference
    void Update(VR::Eye eye, GtaGameState* gameState);

    // Returns true if we successfully found the camera
    bool IsReady() const;

    // Get projection/view matrices for rendering
    DirectX::XMMATRIX GetProjectionMatrix(VR::Eye eye, float near_plane, float far_plane);
    DirectX::XMMATRIX GetEyeViewMatrix(VR::Eye eye);

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
    bool TryResolveFromMetadataObjects(uintptr_t& outAddress, uintptr_t& outCameraBase) const;
    bool FindMatrixInCamera(uintptr_t cameraBase, uintptr_t& outAddress, size_t scanSize = 0x1000) const;
    bool FindMatrixViaPointerScan(uintptr_t base, uintptr_t& outAddress, uintptr_t& outCameraBase) const;
    bool FindCameraByMetadataScan(uintptr_t base, uintptr_t& outCameraBase) const;
    bool FindMetadataInCamera(uintptr_t cameraBase, uintptr_t& outMetadata) const;
    bool TryReadCameraHashes(uintptr_t cameraBase, uint32_t& outHashKey, uint32_t& outHashName) const;
    bool AcceptMatrixCandidate(uintptr_t candidate, uintptr_t cameraBase);
    bool SafeRead(uintptr_t address, void* outData, size_t size) const;
    bool SafeReadPtr(uintptr_t address, uintptr_t& outValue) const;
    bool SafeWrite(uintptr_t address, const void* data, size_t size) const;
    bool ScoreMatrix(const GtaCameraMatrix& matrix, float& outScore) const;
    bool TryDirectMatrixScan(const CameraConfig& config, uintptr_t& outAddress);

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
    void* matrix_address_ = nullptr;
    bool config_loaded_ = false;
    bool hook_ready_ = false;

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

    // Active camera tracking (fallback scanning)
    uintptr_t active_camera_base_ = 0;
    uintptr_t pending_matrix_address_ = 0;
    uintptr_t pending_camera_base_ = 0;
    uint32_t pending_valid_frames_ = 0;
    mutable uintptr_t last_logged_camera_base_ = 0;
    uint32_t last_camera_scan_ = 0;
    // Throttle for the whole-process metadata sweep (see metadataSweepIntervalSec
    // in manifests/gtav_legacy.ini; default 5s).
    mutable uint64_t last_metadata_sweep_tick_ = 0;
    GtaCameraFov* camera_fov_ = nullptr;
};

} // namespace Game
} // namespace OVRInject
