#include "GtaGameState.hpp"
#include "PatternScanner.hpp"
#include "../Log.hpp"
#include "../VR/SharedSettings.hpp"

namespace OVRInject {
namespace Game {

GtaGameState::GtaGameState() {
    LOGSTR("GtaGameState: Created\n");
}

GtaGameState::~GtaGameState() {
    LOGSTR("GtaGameState: Destroyed\n");
}

bool GtaGameState::Initialize() {
    if (initialized_) {
        return true;
    }

    LOGSTR("GtaGameState: Initializing game state detection...\n");

    // Try to find cutscene flag in memory
    bool foundCutscene = FindCutsceneFlag();
    bool foundGameState = FindGameStateAddress();

    if (foundCutscene) {
        LOGSTRF("GtaGameState: Cutscene flag found at 0x%p\n", (void*)cutscene_flag_addr_);
    } else {
        LOGSTR("GtaGameState: Cutscene flag not found, using camera hash fallback\n");
    }

    if (foundGameState) {
        LOGSTRF("GtaGameState: Game state found at 0x%p\n", (void*)game_state_addr_);
    }

    // Even without pattern matches, we can still use camera hash detection
    initialized_ = true;
    LOGSTR("GtaGameState: Initialization complete\n");
    return true;
}

void GtaGameState::Update() {
    if (!initialized_) {
        return;
    }

    // Get current camera hash from RuntimeStats (set by GtaCameraFov)
    auto& stats = VR::GetRuntimeStats();
    current_camera_hash_ = stats.activeCameraHash.load();

    // Update state from camera hash
    // IMPORTANT: Camera hash-based detection is the primary method
    UpdateFromCameraHash(current_camera_hash_);

    // NOTE: Pattern-based cutscene detection DISABLED
    // The patterns are too generic and cause false positives, matching unrelated memory
    // that contains non-zero data, causing is_cutscene_ to always be true.
    // Camera hash-based detection (above) is more reliable.
    //
    // if (cutscene_flag_addr_ != 0) {
    //     uint8_t flag = ReadMemory<uint8_t>(cutscene_flag_addr_, 0);
    //     is_cutscene_ = (flag != 0);
    // }

    // NOTE: Pattern-based game state detection also disabled for same reason
    // The menu/loading states should be detected via other means
    //
    // if (game_state_addr_ != 0) {
    //     int32_t state = ReadMemory<int32_t>(game_state_addr_, 0);
    //     // eGameState: 0=Playing, 5=MainMenu, 6=Loading
    //     is_loading_ = (state == 6);
    //     is_in_menu_ = (state == 5);
    // }

    // Determine overall state
    if (is_loading_) {
        current_state_ = VR::GameState::Loading;
    } else if (is_in_menu_) {
        current_state_ = VR::GameState::Menu;
    } else if (is_cutscene_) {
        current_state_ = VR::GameState::Cutscene;
    } else if (is_aiming_) {
        current_state_ = VR::GameState::Aiming;
    } else if (is_in_vehicle_) {
        current_state_ = VR::GameState::InVehicle;
    } else {
        current_state_ = VR::GameState::Playing;
    }

    // Update shared game state info
    auto& gameState = VR::GetGameStateInfo();
    gameState.currentState.store(static_cast<int>(current_state_));
    gameState.isCutsceneActive.store(is_cutscene_);
    gameState.isAiming.store(is_aiming_);
    gameState.isInVehicle.store(is_in_vehicle_);
    gameState.isFirstPerson.store(is_first_person_);
    gameState.currentCameraHash.store(current_camera_hash_);

    // Log state changes
    if (current_state_ != last_state_) {
        const char* stateName = "Unknown";
        switch (current_state_) {
            case VR::GameState::Playing: stateName = "Playing"; break;
            case VR::GameState::Cutscene: stateName = "Cutscene"; break;
            case VR::GameState::Aiming: stateName = "Aiming"; break;
            case VR::GameState::InVehicle: stateName = "InVehicle"; break;
            case VR::GameState::Loading: stateName = "Loading"; break;
            case VR::GameState::Menu: stateName = "Menu"; break;
            default: break;
        }
        LOGSTRF("GtaGameState: State changed to %s (camera hash: 0x%X)\n",
                stateName, current_camera_hash_);
        last_state_ = current_state_;
    }

    last_camera_hash_ = current_camera_hash_;
}

void GtaGameState::UpdateFromCameraHash(uint32_t hash) {
    // Detect state from camera type hash
    CameraHash camType = static_cast<CameraHash>(hash);

    // If camera hash is 0 (not yet set by GtaCameraFov), don't make assumptions
    // Just keep current state - assume NOT cutscene to allow camera updates
    if (hash == 0) {
        // Camera system not ready yet - assume normal gameplay
        is_cutscene_ = false;
        return;
    }

    // First, detect if this is a cutscene/scripted camera
    bool is_cutscene_camera = false;
    switch (camType) {
        case CameraHash::Scripted:
        case CameraHash::ScriptedFly:
        case CameraHash::ScriptedShake:
        case CameraHash::Cinematic:
        case CameraHash::CinematicIntro:
        case CameraHash::Director:
        case CameraHash::DeathFail:
        case CameraHash::ArrestScene:
        case CameraHash::Busted:
        case CameraHash::RespawnInPlace:
            is_cutscene_camera = true;
            break;
        default:
            break;
    }

    // Always use camera hash-based detection for cutscenes
    // This is more reliable than pattern scanning which can match unrelated memory
    is_cutscene_ = is_cutscene_camera;

    // Handle specific camera types
    switch (camType) {
        // First person aiming (FirstPersonAiming is same hash as FirstPersonShooter)
        case CameraHash::FirstPersonShooter:
            is_aiming_ = true;
            is_first_person_ = true;
            is_in_vehicle_ = false;
            break;

        // Third person aiming
        case CameraHash::ThirdPersonAim:
            is_aiming_ = true;
            is_first_person_ = false;
            is_in_vehicle_ = false;
            break;

        // Third person walking
        case CameraHash::FollowPed:
            is_aiming_ = false;
            is_first_person_ = false;
            is_in_vehicle_ = false;
            break;

        // First person walking
        case CameraHash::FirstPersonPed:
            is_aiming_ = false;
            is_first_person_ = true;
            is_in_vehicle_ = false;
            break;

        // Third person vehicle
        case CameraHash::FollowVehicle:
        case CameraHash::FollowBoat:
        case CameraHash::FollowPlane:
        case CameraHash::FollowHeli:
        case CameraHash::FollowSub:
        case CameraHash::FollowBicycle:
            is_aiming_ = false;
            is_first_person_ = false;
            is_in_vehicle_ = true;
            break;

        // First person vehicle
        case CameraHash::CinematicMounted:
        case CameraHash::FirstPersonDriving:
        case CameraHash::FirstPersonBoat:
        case CameraHash::FirstPersonPlane:
        case CameraHash::FirstPersonHeli:
            is_aiming_ = false;
            is_first_person_ = true;
            is_in_vehicle_ = true;
            break;

        // Cutscene/scripted cameras (CinematicVehicle uses same hash as FollowHeli)
        case CameraHash::Scripted:
        case CameraHash::ScriptedFly:
        case CameraHash::ScriptedShake:
        case CameraHash::Cinematic:
        case CameraHash::CinematicIntro:
        case CameraHash::Director:
            is_aiming_ = false;
            // Keep current first_person and in_vehicle states during cutscene
            break;

        // Menu/special cameras
        case CameraHash::Selfie:
        case CameraHash::Debug:
        case CameraHash::Editor:
            is_aiming_ = false;
            is_first_person_ = false;
            is_in_vehicle_ = false;
            break;

        // Death/arrest cameras
        case CameraHash::DeathFail:
        case CameraHash::ArrestScene:
        case CameraHash::Busted:
        case CameraHash::RespawnInPlace:
            is_aiming_ = false;
            // These are like cutscenes, game camera controls everything
            break;

        default:
            // Unknown camera hash - keep current state
            // Log if it's a new hash we haven't seen
            if (hash != 0 && hash != last_camera_hash_) {
                LOGSTRF("GtaGameState: Unknown camera hash detected: 0x%X\n", hash);
            }
            break;
    }
}

bool GtaGameState::ShouldApplyDecoupling() const {
    auto& settings = VR::GetDecouplingSettings();

    if (!settings.enabled.load()) {
        return false;
    }

    // Don't decouple during cutscenes (handled separately)
    if (is_cutscene_) {
        return false;
    }

    // Don't decouple in menus or loading
    if (is_loading_ || is_in_menu_) {
        return false;
    }

    VR::DecouplingMode mode = static_cast<VR::DecouplingMode>(settings.mode.load());

    switch (mode) {
        case VR::DecouplingMode::Always:
            return true;

        case VR::DecouplingMode::OnlyAiming:
            return is_aiming_;

        case VR::DecouplingMode::Never:
            return false;
    }

    return true;
}

bool GtaGameState::ShouldShowVirtualScreen() const {
    auto& settings = VR::GetCutsceneSettings();
    VR::CutsceneMode mode = static_cast<VR::CutsceneMode>(settings.mode.load());

    if (!is_cutscene_) {
        return false;
    }

    return mode == VR::CutsceneMode::VirtualScreen;
}

bool GtaGameState::FindCutsceneFlag() {
    // Pattern for IS_CUTSCENE_ACTIVE memory location
    // This pattern may need updating for different GTA V versions
    // Common pattern: checking a global flag for cutscene state

    // Pattern from various GTA V modding sources
    const char* patterns[] = {
        // Pattern 1: Common cutscene check
        "48 8B 05 ? ? ? ? 48 85 C0 74 ? 80 B8 ? ? 00 00 00",
        // Pattern 2: Alternative cutscene state
        "40 38 35 ? ? ? ? 75 ? 48 8B 0D",
        // Pattern 3: Cutscene manager check
        "48 85 C9 74 ? 48 8B 89 ? ? 00 00 48 85 C9",
        nullptr
    };

    HMODULE module = GetModuleHandle(nullptr);

    for (int i = 0; patterns[i] != nullptr; i++) {
        uintptr_t addr = PatternScanner::FindPattern(patterns[i], module);
        if (addr != 0) {
            // Resolve RIP-relative address
            // The pattern points to a MOV instruction, we need to extract the address
            int32_t offset = *reinterpret_cast<int32_t*>(addr + 3);
            cutscene_flag_addr_ = addr + 7 + offset;

            if (IsReadable(cutscene_flag_addr_, 1)) {
                LOGSTRF("GtaGameState: Found cutscene flag with pattern %d\n", i + 1);
                return true;
            }
        }
    }

    return false;
}

bool GtaGameState::FindGameStateAddress() {
    // Pattern for game state enum
    // eGameState: 0=Playing, 1=Intro, 3=Startup, 5=MainMenu, 6=Loading

    const char* patterns[] = {
        // Pattern from ExtendedCameraSettings
        "0F 29 74 24 ? 85 DB",
        "0F 29 74 24 ? 85 C0",
        nullptr
    };

    HMODULE module = GetModuleHandle(nullptr);

    for (int i = 0; patterns[i] != nullptr; i++) {
        uintptr_t addr = PatternScanner::FindPattern(patterns[i], module);
        if (addr != 0) {
            // Resolve the address (RIP-relative)
            int32_t offset = *reinterpret_cast<int32_t*>(addr - 4);
            game_state_addr_ = addr + offset;

            if (IsReadable(game_state_addr_, sizeof(int32_t))) {
                LOGSTRF("GtaGameState: Found game state with pattern %d\n", i + 1);
                return true;
            }
        }
    }

    return false;
}

bool GtaGameState::IsReadable(uintptr_t address, size_t size) const {
    if (!address || size == 0) return false;

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT) return false;
    if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) return false;

    return true;
}

template<typename T>
T GtaGameState::ReadMemory(uintptr_t address, T defaultValue) const {
    if (!IsReadable(address, sizeof(T))) {
        return defaultValue;
    }

    __try {
        return *reinterpret_cast<T*>(address);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return defaultValue;
    }
}

// Explicit template instantiations
template uint8_t GtaGameState::ReadMemory<uint8_t>(uintptr_t, uint8_t) const;
template int32_t GtaGameState::ReadMemory<int32_t>(uintptr_t, int32_t) const;

} // namespace Game
} // namespace OVRInject
