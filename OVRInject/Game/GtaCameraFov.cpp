#include "GtaCameraFov.hpp"

#include "BuildManifest.hpp"
#include "OnlineGuard.hpp"
#include "../Log.hpp"

#include <Windows.h>
#include <cmath>
#include <string>
#include <vector>

namespace OVRInject {
namespace Game {

namespace {

// Standard gameplay cameras
constexpr uint32_t kHashFollowPed = 3759477553u;
constexpr uint32_t kHashFirstPerson = 3837693093u;
constexpr uint32_t kHashFirstPersonVehicle = 2185301869u;
constexpr uint32_t kHashFollowVehicle = 420909885u;
constexpr uint32_t kHashThirdPersonAim = 1732613077u;

// First person cameras
constexpr uint32_t kHashFirstPersonPed = 477769724u;
constexpr uint32_t kHashFirstPersonDriving = 1386484893u;
constexpr uint32_t kHashFirstPersonBoat = 3774842856u;
constexpr uint32_t kHashFirstPersonPlane = 1224026983u;
constexpr uint32_t kHashFirstPersonHeli = 2457114543u;

// Vehicle cameras
constexpr uint32_t kHashFollowBoat = 3526638177u;
constexpr uint32_t kHashFollowPlane = 2419563936u;
constexpr uint32_t kHashFollowHeli = 2064644634u;
constexpr uint32_t kHashFollowSub = 4199959599u;
constexpr uint32_t kHashFollowBicycle = 3614892551u;

// Scripted/cutscene cameras
constexpr uint32_t kHashScripted = 3316649216u;
constexpr uint32_t kHashScriptedFly = 1535408780u;
constexpr uint32_t kHashScriptedShake = 2891687915u;
constexpr uint32_t kHashCinematic = 892987104u;
constexpr uint32_t kHashCinematicIntro = 1316803979u;
constexpr uint32_t kHashDirector = 3032073741u;

// Special cameras
constexpr uint32_t kHashSelfie = 3286855989u;
constexpr uint32_t kHashDebug = 2349876130u;
constexpr uint32_t kHashEditor = 832816249u;
constexpr uint32_t kHashDeathFail = 3896756745u;
constexpr uint32_t kHashArrestScene = 1565598557u;
constexpr uint32_t kHashBusted = 4144748578u;

int LookupFovOffset(uint32_t hash) {
    switch (hash) {
    // First person cameras
    case kHashFirstPerson:
    case kHashFirstPersonPed:
        return 36; // camFirstPersonShooterCameraMetadata (fieldOfView @ 0x24)

    // First person vehicle cameras
    case kHashFirstPersonVehicle:
    case kHashFirstPersonDriving:
    case kHashFirstPersonBoat:
    case kHashFirstPersonPlane:
    case kHashFirstPersonHeli:
        return 84; // camCinematicMountedCameraMetadata (fieldOfView @ 0x54)

    // Third person cameras
    case kHashFollowPed:
    case kHashFollowVehicle:
    case kHashFollowBoat:
    case kHashFollowPlane:
    case kHashFollowHeli:
    case kHashFollowSub:
    case kHashFollowBicycle:
    case kHashThirdPersonAim:
        return 48; // camFollow* / camThirdPerson* (fieldOfView @ 0x30)

    // Scripted/cutscene cameras - try common FOV offsets
    case kHashScripted:
    case kHashScriptedFly:
    case kHashScriptedShake:
    case kHashCinematic:
    case kHashCinematicIntro:
    case kHashDirector:
        return 48; // Most scripted cameras use this offset

    // Special cameras
    case kHashSelfie:
    case kHashDebug:
    case kHashEditor:
    case kHashDeathFail:
    case kHashArrestScene:
    case kHashBusted:
        return 48; // Default offset

    default:
        // Universal FOV fix: try common offset for unknown cameras
        // This helps with new camera types or modded cameras
        return 48;
    }
}

// Check if this is a cutscene/scripted camera
bool IsCutsceneCamera(uint32_t hash) {
    switch (hash) {
    case kHashScripted:
    case kHashScriptedFly:
    case kHashScriptedShake:
    case kHashCinematic:
    case kHashCinematicIntro:
    case kHashDirector:
    case kHashDeathFail:
    case kHashArrestScene:
    case kHashBusted:
        return true;
    default:
        return false;
    }
}

float ClampFov(float fov) {
    if (fov < 1.0f) return 1.0f;
    if (fov > 130.0f) return 130.0f;
    return fov;
}

} // namespace

bool GtaCameraFov::Initialize() {
    if (init_attempted_) {
        return get_cam_director_ != nullptr;
    }
    init_attempted_ = true;
    return Resolve();
}

bool GtaCameraFov::Resolve() {
    // Patterns are version-pinned in the build manifest (manifests/gtav_legacy.ini)
    // instead of being hardcoded here. Pattern kinds (preserved from the old
    // builtin table):
    //   - callOffset >= 0      : E8 relative call at match + callOffset
    //   - globalPtrOffset >= 0 : RIP-relative global pointer (48 8B 0D/05)
    //   - both < 0             : reference-only entry, skipped below
    struct PatternInfo {
        const char* pattern;
        int callOffset;     // Offset to E8 byte (-1 if not a call pattern)
        int globalPtrOffset; // Offset to 48 8B 0D/05 (-1 if not a global ptr pattern)
    };

    BuildManifest& manifest = BuildManifest::Get();
    manifest.Initialize();
    std::vector<FovPatternEntry> entries;
    if (!manifest.IsBuildSupported() || !manifest.GetFovPatterns(entries)) {
        LOGSTR("GtaCameraFov: No FOV patterns for this build "
               "(see BuildManifest diagnostics - unsupported build)\n");
        return false;
    }

    std::vector<PatternInfo> patterns;
    patterns.reserve(entries.size());
    for (const FovPatternEntry& entry : entries) {
        patterns.push_back({entry.pattern.c_str(), entry.callOffset, entry.globalPtrOffset});
    }

    // Try call-based patterns first
    for (size_t i = 0; i < patterns.size(); i++) {
        if (patterns[i].callOffset < 0 && patterns[i].globalPtrOffset < 0) {
            continue; // Skip non-function patterns
        }

        uintptr_t matchAddr = PatternScanner::FindPattern(patterns[i].pattern);
        if (!matchAddr) {
            continue;
        }

        LOGSTRF("GtaCameraFov: Pattern %d matched at 0x%p\n", (int)(i + 1), (void*)matchAddr);

        uintptr_t target = 0;

        if (patterns[i].callOffset >= 0) {
            // Resolve relative call address (E8 xx xx xx xx)
            uintptr_t callAddr = matchAddr + patterns[i].callOffset;
            if (!IsReadable(callAddr + 1, sizeof(int32_t))) {
                LOGSTRF("GtaCameraFov: Pattern %d - call address not readable\n", (int)(i + 1));
                continue;
            }
            int32_t offset = *reinterpret_cast<int32_t*>(callAddr + 1);
            target = callAddr + 5 + offset;
        }
        else if (patterns[i].globalPtrOffset >= 0) {
            // Resolve RIP-relative global pointer (48 8B 0D/05 xx xx xx xx)
            uintptr_t ptrAddr = matchAddr + patterns[i].globalPtrOffset + 3; // Skip opcode
            if (!IsReadable(ptrAddr, sizeof(int32_t))) {
                LOGSTRF("GtaCameraFov: Pattern %d - global ptr not readable\n", (int)(i + 1));
                continue;
            }
            int32_t disp = *reinterpret_cast<int32_t*>(ptrAddr);
            uintptr_t globalAddr = ptrAddr + 4 + disp;

            // Read the global pointer value
            if (!IsReadable(globalAddr, sizeof(uintptr_t))) {
                LOGSTRF("GtaCameraFov: Pattern %d - global address not readable\n", (int)(i + 1));
                continue;
            }
            uintptr_t globalValue = *reinterpret_cast<uintptr_t*>(globalAddr);

            if (globalValue && IsReadable(globalValue, sizeof(camBaseDirector))) {
                // This is a direct pointer to the camera director, create a stub
                static uintptr_t s_directorPtr = 0;
                s_directorPtr = globalValue;
                get_cam_director_ = []() -> camBaseDirector* {
                    return reinterpret_cast<camBaseDirector*>(s_directorPtr);
                };
                LOGSTRF("GtaCameraFov: Found camera director at 0x%p via global (pattern %d)\n",
                        (void*)globalValue, (int)(i + 1));
                return true;
            }
            continue;
        }

        if (!target) {
            LOGSTRF("GtaCameraFov: Pattern %d - failed to resolve target\n", (int)(i + 1));
            continue;
        }

        // Validate the target looks like a function
        if (!IsReadable(target, 8)) {
            LOGSTRF("GtaCameraFov: Pattern %d - target not readable\n", (int)(i + 1));
            continue;
        }

        // Try calling it and see if we get a valid result. The SEH-guarded
        // trial call lives in a helper: this function now has std::vector
        // locals, and __try cannot coexist with C++ object unwinding (C2712).
        auto testFunc = reinterpret_cast<GetCamDirectorFromPool>(target);
        if (TryAdoptDirectorFunction(testFunc, target, (int)(i + 1))) {
            return true;
        }
    }

    LOGSTR("GtaCameraFov: Failed to locate getCamDirectorFromPool with any pattern\n");
    return false;
}

bool GtaCameraFov::TryAdoptDirectorFunction(GetCamDirectorFromPool fn,
                                            uintptr_t target,
                                            int patternIndex) {
    // No C++ locals with destructors in here: __try requires it (C2712).
    __try {
        camBaseDirector* dir = fn();
        if (dir && IsReadable(reinterpret_cast<uintptr_t>(dir), sizeof(camBaseDirector))) {
            get_cam_director_ = fn;
            LOGSTRF("GtaCameraFov: getCamDirectorFromPool at 0x%p (pattern %d)\n",
                    reinterpret_cast<void*>(target), patternIndex);
            return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOGSTRF("GtaCameraFov: Pattern %d - exception calling function\n", patternIndex);
    }
    return false;
}

GtaCameraFov::camBaseCamera* GtaCameraFov::GetActiveCamera() const {
    if (!get_cam_director_) {
        return nullptr;
    }

    camBaseDirector* director = get_cam_director_();
    if (!director) {
        return nullptr;
    }

    camBaseCamera* camera = director->activeCamera;
    if (!camera) {
        return nullptr;
    }

    if (!IsReadable(reinterpret_cast<uintptr_t>(camera), sizeof(camBaseCamera))) {
        return nullptr;
    }

    return camera;
}

uintptr_t GtaCameraFov::GetActiveCameraAddress() const {
    camBaseCamera* camera = GetActiveCamera();
    return reinterpret_cast<uintptr_t>(camera);
}

uintptr_t GtaCameraFov::GetDirectorAddress() const {
    if (!get_cam_director_) {
        return 0;
    }
    camBaseDirector* director = get_cam_director_();
    return reinterpret_cast<uintptr_t>(director);
}

GtaCameraFov::camBaseCameraMetadata* GtaCameraFov::GetActiveMetadata() const {
    camBaseCamera* camera = GetActiveCamera();
    if (!camera) {
        return nullptr;
    }

    camBaseCameraMetadata* meta = camera->metadata;
    if (!meta) {
        return nullptr;
    }

    if (!IsReadable(reinterpret_cast<uintptr_t>(meta), sizeof(camBaseCameraMetadata))) {
        return nullptr;
    }

    return meta;
}

bool GtaCameraFov::IsReadable(uintptr_t address, size_t size) const {
    if (!address || size == 0) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT) {
        return false;
    }

    if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) {
        return false;
    }

    size_t regionBase = reinterpret_cast<size_t>(mbi.BaseAddress);
    size_t regionSize = static_cast<size_t>(mbi.RegionSize);
    size_t end = address + size;
    return end <= (regionBase + regionSize);
}

bool GtaCameraFov::WriteFloat(uintptr_t address, float value) const {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(address),
                        sizeof(float),
                        PAGE_EXECUTE_READWRITE,
                        &oldProtect)) {
        return false;
    }

    *reinterpret_cast<float*>(address) = value;

    DWORD unused = 0;
    VirtualProtect(reinterpret_cast<void*>(address),
                   sizeof(float),
                   oldProtect,
                   &unused);
    return true;
}

int GtaCameraFov::GetFovOffset(uint32_t hashKey, uint32_t hashName) const {
    // Try hashKey first
    int offset = LookupFovOffset(hashKey);
    // LookupFovOffset now returns 48 (universal) for unknown cameras,
    // but try hashName if we want to be more specific
    if (hashKey == 0 && hashName != 0) {
        offset = LookupFovOffset(hashName);
    }
    return offset;
}

float GtaCameraFov::GetDesiredFov(const VR::FovSettings& settings,
                                  uint32_t hashKey,
                                  uint32_t hashName) const {
    float global = settings.globalFov.load();
    if (!settings.perType.load()) {
        return global;
    }

    auto pick = [&](uint32_t hash) -> float {
        switch (hash) {
        // First person on foot
        case kHashFirstPerson:
        case kHashFirstPersonPed:
            return settings.fpPedFov.load();

        // Third person on foot
        case kHashFollowPed:
            return settings.tpPedFov.load();

        // Third person aiming
        case kHashThirdPersonAim:
            return settings.tpAimFov.load();

        // First person in vehicle
        case kHashFirstPersonVehicle:
        case kHashFirstPersonDriving:
        case kHashFirstPersonBoat:
        case kHashFirstPersonPlane:
        case kHashFirstPersonHeli:
            return settings.fpVehicleFov.load();

        // Third person in vehicle
        case kHashFollowVehicle:
        case kHashFollowBoat:
        case kHashFollowPlane:
        case kHashFollowHeli:
        case kHashFollowSub:
        case kHashFollowBicycle:
            return settings.tpVehicleFov.load();

        // Cutscene/scripted cameras - use global FOV
        // This ensures consistent VR FOV during cutscenes
        case kHashScripted:
        case kHashScriptedFly:
        case kHashScriptedShake:
        case kHashCinematic:
        case kHashCinematicIntro:
        case kHashDirector:
        case kHashDeathFail:
        case kHashArrestScene:
        case kHashBusted:
            return global;

        // Special cameras - use global
        case kHashSelfie:
        case kHashDebug:
        case kHashEditor:
            return global;

        default:
            return global;
        }
    };

    float value = pick(hashKey);
    // If we got global from hashKey, try hashName as fallback
    if (std::fabs(value - global) < 0.001f && hashName != hashKey && hashName != 0) {
        value = pick(hashName);
    }
    return value;
}

void GtaCameraFov::Update(const VR::FovSettings& settings) {
    // OnlineGuard hard-disable: skip ALL FOV writes (single-player-only hard
    // constraint, docs/online-guard.md). Defense-in-depth alongside the
    // Present-hook pass-through.
    if (OnlineGuard::Get().ShouldDisableMod()) {
        return;
    }

    if (!get_cam_director_) {
        Initialize();
        if (!get_cam_director_) {
            return;
        }
    }

    camBaseCameraMetadata* meta = GetActiveMetadata();
    if (!meta) {
        return;
    }

    uint32_t hashKey = meta->hashKey;
    uint32_t hashName = meta->hashName;

    auto& stats = VR::GetRuntimeStats();
    stats.activeCameraHash.store(hashKey);
    stats.activeCameraHashName.store(hashName);
    int fovOffset = settings.overrideOffset.load()
        ? settings.manualOffset.load()
        : GetFovOffset(hashKey, hashName);
    stats.activeFovOffset.store(fovOffset);

    float current = 0.0f;
    if (fovOffset > 0) {
        uintptr_t fovAddr = reinterpret_cast<uintptr_t>(meta) + static_cast<uintptr_t>(fovOffset);
        if (IsReadable(fovAddr, sizeof(float))) {
            current = *reinterpret_cast<float*>(fovAddr);
        }
    }
    stats.activeFov.store(current);

    if (!settings.enabled.load()) {
        return;
    }

    if (fovOffset <= 0) {
        return;
    }

    float desired = ClampFov(GetDesiredFov(settings, hashKey, hashName));
    if (std::fabs(current - desired) < 0.001f) {
        return;
    }

    uintptr_t fovAddr = reinterpret_cast<uintptr_t>(meta) + static_cast<uintptr_t>(fovOffset);
    if (!IsReadable(fovAddr, sizeof(float))) {
        return;
    }

    if (!WriteFloat(fovAddr, desired)) {
        return;
    }

    stats.activeFov.store(desired);
}

} // namespace Game
} // namespace OVRInject
