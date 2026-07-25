#include "GtaCameraFov.hpp"

#include "BuildManifest.hpp"
#include "OnlineGuard.hpp"
#include "../Log.hpp"

#include <Windows.h>
#include <cmath>
#include <cstring>
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

// SEH-guarded raw reads. VirtualQuery checks prove a region was readable at
// query time; the game may free/re-protect it before the deref runs, and
// without these guards that race takes the whole game down. No C++ locals
// with destructors in these helpers (C2712).
bool SehReadPtr(uintptr_t at, uintptr_t& outValue) {
    __try {
        outValue = *reinterpret_cast<const uintptr_t*>(at);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

bool SehReadBytes(uintptr_t at, void* outData, size_t size) {
    __try {
        std::memcpy(outData, reinterpret_cast<const void*>(at), size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

} // namespace

GtaCameraFov::~GtaCameraFov() {
    // Flip the lifetime token first so GtaCameraHook's worker stops using
    // this instance, then stop the resolve thread before members die.
    {
        std::lock_guard<std::mutex> lock(lifetime_token_->mutex);
        lifetime_token_->alive.store(false, std::memory_order_release);
    }
    StopResolveThread();
}

void GtaCameraFov::InterruptibleSleep(uint32_t ms) const {
    // 10ms slices keep thread join latency bounded on shutdown.
    for (uint32_t slept = 0; slept < ms && !resolve_stop_.load(std::memory_order_acquire); slept += 10) {
        Sleep(10);
    }
}

void GtaCameraFov::StopResolveThread() {
    resolve_stop_.store(true, std::memory_order_release);
    if (resolve_thread_.joinable()) {
        resolve_thread_.join();
    }
}

bool GtaCameraFov::Initialize() {
    if (init_attempted_.load(std::memory_order_acquire)) {
        return IsReady();
    }
    init_attempted_.store(true, std::memory_order_release);

    // Kick the heavy work (full-module pattern scans + SEH-wrapped trial
    // calls) onto a background thread so the calling thread - which may be
    // the render thread - never blocks on them.
    bool expected = false;
    if (resolve_started_.compare_exchange_strong(expected, true)) {
        resolve_stop_.store(false, std::memory_order_release);
        try {
            resolve_thread_ = std::thread(&GtaCameraFov::ResolveThreadMain, this);
            LOGDBGF("GtaCameraFov: background resolve thread spawned\n");
        } catch (...) {
            LOGWNDF("GtaCameraFov: failed to spawn resolve thread; director resolution disabled\n");
            resolve_started_.store(false, std::memory_order_release);
        }
    }
    return IsReady();
}

bool GtaCameraFov::ResolveBlocking() {
    // BACKGROUND THREADS ONLY (see header). Used by GtaCameraHook's worker
    // for its local fallback instance.
    init_attempted_.store(true, std::memory_order_release);
    if (get_cam_director_.load(std::memory_order_acquire)) {
        return true;
    }
    if (Resolve()) {
        RefreshCachedDirector();
    }
    return get_cam_director_.load(std::memory_order_acquire) != nullptr;
}

void GtaCameraFov::ResolveThreadMain() {
    LOGDBGF("GtaCameraFov: resolve thread started (tid=%lu)\n", GetCurrentThreadId());
    while (!resolve_stop_.load(std::memory_order_acquire)) {
        if (get_cam_director_.load(std::memory_order_acquire) == nullptr) {
            Resolve();  // abort-aware via resolve_stop_
            if (get_cam_director_.load(std::memory_order_acquire) == nullptr) {
                LOGDBGF("GtaCameraFov: director not resolved yet; retrying in 15s\n");
                InterruptibleSleep(15000);
                continue;
            }
        }

        // Keep the cached director pointer fresh so the render thread only
        // ever performs pure memory reads (no game-function calls).
        RefreshCachedDirector();
        InterruptibleSleep(2000);
    }
    LOGDBGF("GtaCameraFov: resolve thread exiting\n");
}

bool GtaCameraFov::RefreshCachedDirector() {
    GetCamDirectorFromPool fn = get_cam_director_.load(std::memory_order_acquire);
    if (!fn) {
        return false;
    }

    camBaseDirector* dir = nullptr;
    bool callOk = true;
    __try {
        dir = fn();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        callOk = false;
    }
    (void)callOk;  // a faulting target simply fails the refresh below

    if (dir && IsReadable(reinterpret_cast<uintptr_t>(dir), sizeof(camBaseDirector))) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(dir);
        if (cached_director_.load(std::memory_order_acquire) != addr) {
            LOGDBGF("GtaCameraFov: cached director = 0x%p\n", reinterpret_cast<void*>(addr));
        }
        cached_director_.store(addr, std::memory_order_release);
        return true;
    }

    static uint32_t s_refreshFailCount = 0;
    if ((++s_refreshFailCount % 30) == 1) {
        LOGDBGF("GtaCameraFov: director refresh failed (transient or stale target, count=%u)\n",
                s_refreshFailCount);
    }
    return false;
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
        if (resolve_stop_.load(std::memory_order_acquire)) {
            return false;
        }
        if (patterns[i].callOffset < 0 && patterns[i].globalPtrOffset < 0) {
            continue; // Skip non-function patterns
        }

        uintptr_t matchAddr = PatternScanner::FindPattern(patterns[i].pattern, nullptr, &resolve_stop_);
        if (!matchAddr) {
            continue;
        }

        LOGSTRF("GtaCameraFov: Pattern %d matched at 0x%p\n", (int)(i + 1), (void*)matchAddr);

        uintptr_t target = 0;

        if (patterns[i].callOffset >= 0) {
            // Resolve relative call address (E8 xx xx xx xx)
            uintptr_t callAddr = matchAddr + patterns[i].callOffset;
            int32_t offset = 0;
            if (!SehReadBytes(callAddr + 1, &offset, sizeof(offset))) {
                LOGSTRF("GtaCameraFov: Pattern %d - call address not readable\n", (int)(i + 1));
                continue;
            }
            target = callAddr + 5 + offset;
        }
        else if (patterns[i].globalPtrOffset >= 0) {
            // Resolve RIP-relative global pointer (48 8B 0D/05 xx xx xx xx)
            uintptr_t ptrAddr = matchAddr + patterns[i].globalPtrOffset + 3; // Skip opcode
            int32_t disp = 0;
            if (!SehReadBytes(ptrAddr, &disp, sizeof(disp))) {
                LOGSTRF("GtaCameraFov: Pattern %d - global ptr not readable\n", (int)(i + 1));
                continue;
            }
            uintptr_t globalAddr = ptrAddr + 4 + disp;

            // Read the global pointer value
            uintptr_t globalValue = 0;
            if (!SehReadBytes(globalAddr, &globalValue, sizeof(globalValue))) {
                LOGSTRF("GtaCameraFov: Pattern %d - global address not readable\n", (int)(i + 1));
                continue;
            }

            if (globalValue && IsReadable(globalValue, sizeof(camBaseDirector))) {
                // This is a direct pointer to the camera director, create a stub
                static uintptr_t s_directorPtr = 0;
                s_directorPtr = globalValue;
                get_cam_director_.store([]() -> camBaseDirector* {
                    return reinterpret_cast<camBaseDirector*>(s_directorPtr);
                }, std::memory_order_release);
                cached_director_.store(globalValue, std::memory_order_release);
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
    // BACKGROUND THREAD ONLY: this test-CALLS a candidate game function.
    // A read-only alternative does not exist for E8 call targets - the
    // returned director pointer is runtime pool state, not statically
    // readable - so the call is kept, SEH-wrapped, and quarantined to the
    // resolve thread (see ResolveThreadMain / ResolveBlocking).
    __try {
        camBaseDirector* dir = fn();
        if (dir && IsReadable(reinterpret_cast<uintptr_t>(dir), sizeof(camBaseDirector))) {
            get_cam_director_.store(fn, std::memory_order_release);
            cached_director_.store(reinterpret_cast<uintptr_t>(dir), std::memory_order_release);
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
    // Read-only: consume the director pointer cached by the resolve thread.
    // No game-function calls on this (possibly render) thread.
    uintptr_t director = cached_director_.load(std::memory_order_acquire);
    if (!director || !IsReadable(director + offsetof(camBaseDirector, activeCamera), sizeof(uintptr_t))) {
        return nullptr;
    }

    uintptr_t camera = 0;
    if (!SehReadPtr(director + offsetof(camBaseDirector, activeCamera), camera) || !camera) {
        return nullptr;
    }

    if (!IsReadable(camera, sizeof(camBaseCamera))) {
        return nullptr;
    }

    return reinterpret_cast<camBaseCamera*>(camera);
}

uintptr_t GtaCameraFov::GetActiveCameraAddress() const {
    camBaseCamera* camera = GetActiveCamera();
    return reinterpret_cast<uintptr_t>(camera);
}

uintptr_t GtaCameraFov::GetDirectorAddress() const {
    // Read-only accessor: no call into game code (see GetActiveCamera).
    return cached_director_.load(std::memory_order_acquire);
}

GtaCameraFov::camBaseCameraMetadata* GtaCameraFov::GetActiveMetadata() const {
    camBaseCamera* camera = GetActiveCamera();
    if (!camera) {
        return nullptr;
    }

    uintptr_t meta = 0;
    if (!SehReadPtr(reinterpret_cast<uintptr_t>(camera) + offsetof(camBaseCamera, metadata), meta) || !meta) {
        return nullptr;
    }

    if (!IsReadable(meta, sizeof(camBaseCameraMetadata))) {
        return nullptr;
    }

    return reinterpret_cast<camBaseCameraMetadata*>(meta);
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

    if (get_cam_director_.load(std::memory_order_acquire) == nullptr) {
        Initialize();  // async kick-off only; never blocks the render thread
        return;
    }
    if (cached_director_.load(std::memory_order_acquire) == 0) {
        return;  // resolved, but no readable director cached yet
    }

    camBaseCameraMetadata* meta = GetActiveMetadata();
    if (!meta) {
        return;
    }

    // SEH-guarded read of the metadata header (game may free it any frame).
    struct MetadataHeader {
        uintptr_t vftable;
        uint32_t hashKey;
        uint32_t hashName;
    };
    MetadataHeader header = {};
    if (!SehReadBytes(reinterpret_cast<uintptr_t>(meta), &header, sizeof(header))) {
        return;
    }
    uint32_t hashKey = header.hashKey;
    uint32_t hashName = header.hashName;

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
            // SEH-guarded: IsReadable is only a point-in-time check.
            if (!SehReadBytes(fovAddr, &current, sizeof(current))) {
                current = 0.0f;
            }
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
