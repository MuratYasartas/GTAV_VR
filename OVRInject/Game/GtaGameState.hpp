#pragma once

#include "../VR/SharedSettings.hpp"
#include <cstdint>
#include <Windows.h>

namespace OVRInject {
namespace Game {

/**
 * Known camera type hashes from GTA V
 * Used to detect game state (aiming, vehicle, cutscene, etc.)
 *
 * Hash values extracted from game analysis and modding resources.
 * Format: joaat("camera_metadata_name") or explicit game hashes.
 */
enum class CameraHash : uint32_t {
    Unknown = 0,

    // Standard gameplay cameras
    FollowPed = 3759477553,              // eCamFollowPedCameraMetadata - 3rd person ped
    FirstPersonShooter = 3837693093,     // eCamFirstPersonShooterCameraMetadata - 1st person aim
    CinematicMounted = 2185301869,       // eCamCinematicMountedCameraMetadata - 1st person vehicle
    FollowVehicle = 420909885,           // eCamFollowVehicleCameraMetadata - 3rd person vehicle
    ThirdPersonAim = 1732613077,         // eCamThirdPersonPedAimCameraMetadata - 3rd person aim

    // First person cameras
    FirstPersonPed = 477769724,          // eCamFirstPersonPedCameraMetadata
    // Note: FirstPersonAiming uses same hash as FirstPersonShooter (3837693093)
    FirstPersonDriving = 1386484893,     // eCamFirstPersonInCarCameraMetadata
    FirstPersonBoat = 3774842856,        // eCamFirstPersonInBoatCameraMetadata
    FirstPersonPlane = 1224026983,       // eCamFirstPersonInPlaneCameraMetadata
    FirstPersonHeli = 2457114543,        // eCamFirstPersonInHeliCameraMetadata

    // Vehicle cameras
    FollowBoat = 3526638177,             // eCamFollowBoatCameraMetadata
    FollowPlane = 2419563936,            // eCamFollowPlaneCameraMetadata
    FollowHeli = 2064644634,             // eCamFollowHeliCameraMetadata (also used for CinematicVehicle)
    FollowSub = 4199959599,              // eCamFollowSubCameraMetadata
    FollowBicycle = 3614892551,          // eCamFollowBicycleCameraMetadata

    // Scripted/cutscene cameras (these indicate cutscene state)
    Scripted = 3316649216,               // eCamScriptedCameraMetadata
    ScriptedFly = 1535408780,            // eCamScriptedFlyCameraMetadata
    ScriptedShake = 2891687915,          // Scripted camera with shake
    Cinematic = 892987104,               // eCamCinematicCameraMetadata
    CinematicIntro = 1316803979,         // Intro cutscene camera
    // Note: CinematicVehicle uses same hash as FollowHeli (2064644634)
    Director = 3032073741,               // Director controlled camera

    // Menu/UI cameras
    Selfie = 3286855989,                 // eCamSelfieCameraMetadata
    Debug = 2349876130,                  // eCamDebugCameraMetadata
    Editor = 832816249,                  // Rockstar Editor camera

    // Special state cameras
    DeathFail = 3896756745,              // Death/wasted camera
    ArrestScene = 1565598557,            // Arrest sequence camera
    Busted = 4144748578,                 // Busted camera
    RespawnInPlace = 2948490197,         // Respawning camera
};

/**
 * GtaGameState - Detects current game state for VR behavior adaptation
 *
 * Uses pattern scanning and camera hash detection to determine:
 * - Is cutscene active?
 * - Is player aiming?
 * - Is player in vehicle?
 * - Current camera type
 */
class GtaGameState {
public:
    GtaGameState();
    ~GtaGameState();

    // Initialize pattern scanning (call once after game is loaded)
    bool Initialize();

    // Update game state (call each frame)
    void Update();

    // State queries
    bool IsCutsceneActive() const { return is_cutscene_; }
    bool IsAiming() const { return is_aiming_; }
    bool IsInVehicle() const { return is_in_vehicle_; }
    bool IsFirstPerson() const { return is_first_person_; }
    bool IsLoading() const { return is_loading_; }
    bool IsInMenu() const { return is_in_menu_; }

    VR::GameState GetCurrentState() const { return current_state_; }
    uint32_t GetCurrentCameraHash() const { return current_camera_hash_; }

    // Check if we should apply decoupling based on current state
    bool ShouldApplyDecoupling() const;

    // Check if we should show virtual screen (cutscene mode)
    bool ShouldShowVirtualScreen() const;

private:
    // Pattern scanning for game state detection
    bool FindCutsceneFlag();
    bool FindGameStateAddress();

    // Camera hash-based detection
    void UpdateFromCameraHash(uint32_t hash);

    // Memory reading helpers
    bool IsReadable(uintptr_t address, size_t size) const;
    template<typename T> T ReadMemory(uintptr_t address, T defaultValue = T{}) const;

    // Detected addresses
    uintptr_t cutscene_flag_addr_ = 0;
    uintptr_t game_state_addr_ = 0;

    // Current state
    VR::GameState current_state_ = VR::GameState::Unknown;
    uint32_t current_camera_hash_ = 0;

    bool is_cutscene_ = false;
    bool is_aiming_ = false;
    bool is_in_vehicle_ = false;
    bool is_first_person_ = false;
    bool is_loading_ = false;
    bool is_in_menu_ = false;

    bool initialized_ = false;

    // For detecting state changes
    uint32_t last_camera_hash_ = 0;
    VR::GameState last_state_ = VR::GameState::Unknown;
};

} // namespace Game
} // namespace OVRInject
