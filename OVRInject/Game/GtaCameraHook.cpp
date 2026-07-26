#include "GtaCameraHook.hpp"
#include "GtaGameState.hpp"
#include "GtaCameraFov.hpp"
#include "BuildManifest.hpp"
#include "OnlineGuard.hpp"
#include "../Log.hpp"
#include "../VR/SharedSettings.hpp"
#include "PatternScanner.hpp"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace DirectX;

namespace OVRInject {
namespace Game {

namespace {

std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string ToLower(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

int64_t ParseInt(const std::string& value, int64_t fallback = 0) {
    if (value.empty()) return fallback;
    char* end = nullptr;
    long long result = std::strtoll(value.c_str(), &end, 0);
    if (end == value.c_str()) {
        return fallback;
    }
    return static_cast<int64_t>(result);
}

bool ParseBool(const std::string& value) {
    std::string lower = ToLower(value);
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

std::vector<int64_t> ParseOffsetList(const std::string& value) {
    std::vector<int64_t> result;
    std::stringstream ss(value);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = Trim(token);
        if (token.empty()) continue;
        result.push_back(ParseInt(token, 0));
    }
    return result;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &out[0], needed, nullptr, nullptr);
    return out;
}

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

bool GetModuleImageRange(HMODULE module, uintptr_t& outBase, uintptr_t& outEnd) {
    outBase = 0;
    outEnd = 0;

    if (!module) {
        return false;
    }

    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    outBase = reinterpret_cast<uintptr_t>(module);
    outEnd = outBase + nt->OptionalHeader.SizeOfImage;
    return true;
}

bool GetModuleSectionRange(HMODULE module,
                           const char* sectionName,
                           uintptr_t& outBase,
                           uintptr_t& outEnd) {
    outBase = 0;
    outEnd = 0;

    if (!module || !sectionName) {
        return false;
    }

    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    auto* section = IMAGE_FIRST_SECTION(nt);
    for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char name[9] = {};
        std::memcpy(name, section->Name, 8);
        if (std::strncmp(name, sectionName, 8) != 0) {
            continue;
        }

        outBase = reinterpret_cast<uintptr_t>(module) + section->VirtualAddress;
        outEnd = outBase + (std::max)(section->Misc.VirtualSize, section->SizeOfRawData);
        return true;
    }

    return false;
}

bool HasReadableProtection(DWORD protect) {
    DWORD baseProtect = protect & 0xff;
    switch (baseProtect) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

bool IsKnownCameraHash(uint32_t hash) {
    switch (hash) {
    case 3759477553u:  // FollowPed
    case 3837693093u:  // FirstPersonShooter
    case 477769724u:   // FirstPersonPed
    case 2185301869u:  // CinematicMounted
    case 1386484893u:  // FirstPersonDriving
    case 3774842856u:  // FirstPersonBoat
    case 1224026983u:  // FirstPersonPlane
    case 2457114543u:  // FirstPersonHeli
    case 420909885u:   // FollowVehicle
    case 3526638177u:  // FollowBoat
    case 2419563936u:  // FollowPlane
    case 2064644634u:  // FollowHeli
    case 4199959599u:  // FollowSub
    case 3614892551u:  // FollowBicycle
    case 1732613077u:  // ThirdPersonAim
    case 3316649216u:  // Scripted
    case 1535408780u:  // ScriptedFly
    case 2891687915u:  // ScriptedShake
    case 892987104u:   // Cinematic
    case 1316803979u:  // CinematicIntro
    case 3032073741u:  // Director
    case 3286855989u:  // Selfie
    case 2349876130u:  // Debug
    case 832816249u:   // Editor
    case 3896756745u:  // DeathFail
    case 1565598557u:  // ArrestScene
    case 4144748578u:  // Busted
    case 2948490197u:  // RespawnInPlace
        return true;
    default:
        return false;
    }
}

// SEH-guarded raw reads for the whole-process VirtualQuery sweeps in
// TryResolveFromMetadataObjects. VirtualQuery only proves a region was
// readable at query time; the game may free or re-protect it before the deref
// runs, and without these guards that race takes the whole game down.
bool SehReadPtrAndHash(const uint8_t* at, uintptr_t& outPtr, uint32_t& outHash) {
    __try {
        outPtr = *reinterpret_cast<const uintptr_t*>(at);
        outHash = *reinterpret_cast<const uint32_t*>(at + sizeof(uintptr_t));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

bool SehReadWord(const uintptr_t* at, uintptr_t& outValue) {
    __try {
        outValue = *at;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

} // namespace

GtaCameraHook::GtaCameraHook(VR::IVRBackend* backend, GtaCameraFov* cameraFov)
    : backend_(backend)
    , camera_fov_(cameraFov) {
    LOGSTR("GtaCameraHook: Created\n");
}

GtaCameraHook::~GtaCameraHook() {
    // Stop the worker BEFORE anything it might use (camera_fov_, config_)
    // goes away. Join is bounded: all scan loops poll worker_stop_.
    StopWorker();
    LOGSTRF("GtaCameraHook: Destroyed. Updates: %u, Writes: %u\n",
            update_count_, write_success_count_);
}

void GtaCameraHook::StartWorker() {
    bool expected = false;
    if (!worker_started_.compare_exchange_strong(expected, true)) {
        return;  // already running
    }

    worker_wake_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset
    if (!worker_wake_event_) {
        LOGWNDF("GtaCameraHook: wake event creation failed (gle=%lu) - worker will poll\n",
                GetLastError());
    }

    worker_stop_.store(false, std::memory_order_release);

    // Snapshot the Fov scanner + its lifetime token BEFORE spawning the
    // thread: the instance is known-alive here (Hook runs during VRManager
    // init), while it may be freed just before ~GtaCameraHook during
    // shutdown. The worker only ever touches this snapshot under the token
    // lock, never a re-read pointer.
    worker_fov_ = camera_fov_.load(std::memory_order_acquire);
    worker_fov_token_ = worker_fov_ ? worker_fov_->GetLifetimeToken() : nullptr;

    try {
        worker_thread_ = std::thread(&GtaCameraHook::WorkerMain, this);
    } catch (...) {
        LOGWNDF("GtaCameraHook: failed to start worker thread - camera resolution disabled\n");
        worker_started_.store(false, std::memory_order_release);
        if (worker_wake_event_) {
            CloseHandle(worker_wake_event_);
            worker_wake_event_ = nullptr;
        }
        return;
    }
    LOGDBGF("GtaCameraHook: worker thread spawned (resolveTimeout=%ds, backgroundRetry=%ds)\n",
            config_.resolveTimeoutSec, config_.backgroundRetrySec);
}

void GtaCameraHook::StopWorker() {
    if (!worker_started_.load(std::memory_order_acquire)) {
        return;
    }
    worker_stop_.store(true, std::memory_order_release);
    if (worker_wake_event_) {
        SetEvent(worker_wake_event_);
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    if (worker_wake_event_) {
        CloseHandle(worker_wake_event_);
        worker_wake_event_ = nullptr;
    }
    worker_started_.store(false, std::memory_order_release);
    LOGDBGF("GtaCameraHook: worker stopped\n");
}

DWORD GtaCameraHook::WaitForWorkerEvent(uint32_t timeoutMs) const {
    if (!worker_wake_event_) {
        Sleep(timeoutMs);
        return WAIT_TIMEOUT;
    }
    return WaitForSingleObject(worker_wake_event_, timeoutMs);
}

bool GtaCameraHook::ShouldAbortScan(uint64_t deadlineTick) const {
    if (worker_stop_.load(std::memory_order_acquire)) {
        return true;
    }
    return deadlineTick != 0 && GetTickCount64() >= deadlineTick;
}

bool GtaCameraHook::Hook() {
    LOGSTR("GtaCameraHook: Attempting to hook camera...\n");

    // Arm the online guard as early as possible (idempotent). Single-player-only
    // is a hard constraint with no bypass - see docs/online-guard.md.
    OnlineGuard::Get().Initialize();

    auto& stats = VR::GetRuntimeStats();
    stats.cameraConfigLoaded.store(false);
    stats.cameraHookReady.store(false);
    stats.cameraMatrixAddress.store(0);
    stats.cameraMatrixWritable.store(false);

    // Load config (cheap file read - the only thing still done synchronously).
    config_loaded_ = LoadConfig(config_);
    stats.cameraConfigLoaded.store(config_loaded_);

    // ALL expensive resolution (full-module pattern scans, whole-process
    // metadata sweeps) runs on the background worker. Previous revisions did
    // this on the render thread: a single metadata sweep takes ~7.7s on a
    // live GTA5.exe and was repeated while no candidate validated, freezing
    // the game at ~0.13 fps. The render thread now only consumes validated
    // candidates via the lock-free handoff in Update().
    StartWorker();
    LOGSTR("GtaCameraHook: camera resolution running on background worker\n");

    stats.cameraHookReady.store(false);
    return false;  // not ready synchronously; worker publishes candidates
}

bool GtaCameraHook::IsReady() const {
    return hook_ready_.load(std::memory_order_acquire) &&
           matrix_address_.load(std::memory_order_acquire) != 0;
}

void GtaCameraHook::WorkerMain() {
    LOGDBGF("GtaCameraHook: worker started (tid=%lu)\n", GetCurrentThreadId());

    int timeoutSec = config_.resolveTimeoutSec;
    if (timeoutSec < 5) timeoutSec = 5;
    if (timeoutSec > 600) timeoutSec = 600;
    int retrySec = config_.backgroundRetrySec;
    if (retrySec < 0) retrySec = 0;
    if (retrySec > 3600) retrySec = 3600;
    const uint64_t budgetMs = static_cast<uint64_t>(timeoutSec) * 1000ull;
    const uint64_t retryMs = static_cast<uint64_t>(retrySec) * 1000ull;

    uint64_t windowStartTick = GetTickCount64();
    bool gaveUp = false;
    bool gaveUpLogged = false;
    bool wasReady = false;
    uint32_t passIndex = 0;

    while (!worker_stop_.load(std::memory_order_acquire)) {
        // Never resolve (or keep resolving) while the online guard is hot.
        if (OnlineGuard::Get().ShouldDisableMod()) {
            WaitForWorkerEvent(1000);
            continue;
        }

        bool ready = IsReady();
        if (wasReady && !ready) {
            // Camera lost mid-game (scene transition, write failure): grant a
            // fresh resolution window with the full method set.
            LOGDBGF("GtaCameraHook: camera lost - restarting resolution window\n");
            gaveUp = false;
            windowStartTick = GetTickCount64();
            retry_requested_.store(false, std::memory_order_release);
        }
        wasReady = ready;

        if (ready) {
            // Cheap periodic re-resolve so camera switches (on foot <->
            // vehicle etc.) are tracked. Bounded scans, worker thread.
            RunReadyRefresh();
            WaitForWorkerEvent(2000);
            continue;
        }

        if (!gaveUp) {
            retry_requested_.store(false, std::memory_order_release);
            uint64_t deadline = windowStartTick + budgetMs;
            RunResolutionPass(true, deadline, ++passIndex);
            if (!IsReady() && !handoff_pending_.load(std::memory_order_acquire) &&
                !worker_stop_.load(std::memory_order_acquire)) {
                if (GetTickCount64() >= deadline) {
                    gaveUp = true;
                    if (!gaveUpLogged) {
                        gaveUpLogged = true;
                        LOGSTR("GtaCameraHook: camera unresolved - staying in mono mode\n");
                    }
                    LOGDBGF("GtaCameraHook: give-up after %u passes (%llums budget exhausted); "
                            "retry via manual recenter or %llus background retry\n",
                            passIndex, budgetMs, retryMs / 1000ull);
                } else {
                    WaitForWorkerEvent(250);
                }
            }
            continue;
        }

        // Given up: stop sweeping. Retry only via manual recenter (full pass)
        // or the periodic background retry (cheap pass - no whole-process
        // metadata sweep).
        DWORD waitRes = WaitForWorkerEvent(retryMs ? static_cast<uint32_t>(retryMs) : INFINITE);
        if (worker_stop_.load(std::memory_order_acquire)) {
            break;
        }
        bool manual = retry_requested_.exchange(false, std::memory_order_acq_rel);
        if (manual) {
            LOGDBGF("GtaCameraHook: manual recenter retry - full resolution pass %u\n",
                    passIndex + 1);
            RunResolutionPass(true, GetTickCount64() + budgetMs, ++passIndex);
        } else if (waitRes == WAIT_TIMEOUT && retryMs) {
            LOGDBGF("GtaCameraHook: background retry - cheap resolution pass %u\n", passIndex + 1);
            RunResolutionPass(false, GetTickCount64() + budgetMs, ++passIndex);
        }
    }

    LOGDBGF("GtaCameraHook: worker exiting\n");
}

bool GtaCameraHook::RunResolutionPass(bool allowFullSweep, uint64_t deadlineTick, uint32_t passIndex) {
    LOGDBGF("GtaCameraHook: resolution pass %u start (fullSweep=%d)\n",
            passIndex, allowFullSweep ? 1 : 0);
    ULONGLONG passStart = GetTickCount64();

    uintptr_t addr = 0;
    uintptr_t base = 0;

    // 1) Config-based resolution (gtavr_camera.ini [camera]).
    if (config_loaded_ && !ShouldAbortScan(deadlineTick) && ResolveMatrixAddress(config_, addr)) {
        if (ValidateAndPublishCandidate(addr, 0, "config")) {
            return true;
        }
    }

    // 2) Build-manifest patterns (version-pinned, manifests/gtav_legacy.ini).
    // Tried BEFORE the active-camera step: the manifest direct scan is
    // milliseconds-cheap and proven to resolve on supported builds, while the
    // active-camera pointer/metadata scans have no time budget and have
    // wedged for minutes live (2026-07-26).
    BuildManifest& manifest = BuildManifest::Get();
    manifest.Initialize();
    std::vector<CameraPatternEntry> entries;
    if (manifest.IsBuildSupported() && manifest.GetCameraPatterns(entries)) {
        // INFO on purpose: with verbose off this is the ONLY visibility into
        // which resolution stage runs and where it fails.
        LOGSTRF("GtaCameraHook: Trying build manifest patterns (%zu entries, section [%s])...\n",
                entries.size(), manifest.GetBuildInfo().section.c_str());
        for (size_t i = 0; i < entries.size(); ++i) {
            if (ShouldAbortScan(deadlineTick)) {
                LOGSTRF("GtaCameraHook: pass %u aborted at manifest pattern %zu/%zu (time budget)\n",
                        passIndex, i + 1, entries.size());
                return false;
            }
            const CameraPatternEntry& entry = entries[i];
            CameraConfig fallbackConfig;
            fallbackConfig.pattern = entry.pattern;
            fallbackConfig.patternOffset = entry.patternOffset;
            fallbackConfig.relativeOffsets = entry.relativeOffsets;
            fallbackConfig.ripOffset = entry.ripOffset;
            fallbackConfig.ripOffsetSet = entry.ripOffsetSet;
            fallbackConfig.matrixOffset = entry.matrixOffset;
            fallbackConfig.pointerOffsets = entry.pointerOffsets;

            uintptr_t candidate = 0;
            LOGSTRF("GtaCameraHook: manifest pattern %zu/%zu: trying '%s' (patternOffset=%lld, matrixOffset=0x%llX, source: %s)\n",
                    i + 1, entries.size(), entry.pattern.c_str(),
                    static_cast<long long>(entry.patternOffset),
                    static_cast<unsigned long long>(entry.matrixOffset),
                    entry.source.c_str());
            if (TryDirectMatrixScan(fallbackConfig, candidate)) {
                LOGSTRF("GtaCameraHook: manifest pattern %zu/%zu: candidate 0x%p - validating\n",
                        i + 1, entries.size(), reinterpret_cast<void*>(candidate));
                if (ValidateAndPublishCandidate(candidate, 0, "manifest")) {
                    return true;
                }
                LOGSTRF("GtaCameraHook: manifest pattern %zu/%zu: candidate REJECTED by validation (reason above)\n",
                        i + 1, entries.size());
            } else {
                LOGSTRF("GtaCameraHook: manifest pattern %zu/%zu: no usable matrix (scan detail above)\n",
                        i + 1, entries.size());
            }
        }
    } else if (passIndex == 1) {
        LOGSTR("GtaCameraHook: no manifest camera patterns for this build (unsupported build or empty section)\n");
    }

    // 3) Active camera via the camera director (GtaCameraFov).
    // FULL PASSES ONLY, last resort after config+manifest: this step's
    // pointer/metadata scans (up to millions of VirtualQuery calls in the
    // worst case) have no time budget and wedged for minutes live
    // (2026-07-26). Cheap passes must stay cheap: config + manifest only.
    addr = 0;
    base = 0;
    if (allowFullSweep && !ShouldAbortScan(deadlineTick) && TryResolveFromActiveCamera(addr, base)) {
        if (ValidateAndPublishCandidate(addr, base, "active-camera")) {
            return true;
        }
    } else if (!allowFullSweep) {
        LOGDBGF("GtaCameraHook: cheap pass - skipping active-camera step\n");
    }

    // 4) Whole-process metadata sweep - expensive (~7.7s+ per sweep on a live
    //    GTA5.exe); full passes only, throttled by metadataSweepIntervalSec.
    addr = 0;
    base = 0;
    if (allowFullSweep && TryResolveFromMetadataObjects(addr, base, deadlineTick)) {
        if (ValidateAndPublishCandidate(addr, base, "metadata-sweep")) {
            return true;
        }
    }

    // 5) Direct matrix scan with the config pattern (last resort).
    addr = 0;
    if (allowFullSweep && config_loaded_ && !ShouldAbortScan(deadlineTick) &&
        TryDirectMatrixScan(config_, addr)) {
        if (ValidateAndPublishCandidate(addr, 0, "direct-scan")) {
            return true;
        }
    }

    LOGSTRF("GtaCameraHook: resolution pass %u end: no candidate (%llums)\n",
            passIndex, GetTickCount64() - passStart);
    return false;
}

void GtaCameraHook::RunReadyRefresh() {
    uintptr_t addr = 0;
    uintptr_t base = 0;
    bool found = false;

    // Preserve the historic preference: config resolution first, then the
    // active-camera path when config fails.
    if (config_loaded_ && ResolveMatrixAddress(config_, addr)) {
        found = true;
        base = 0;
    }
    if (!found && TryResolveFromActiveCamera(addr, base)) {
        found = true;
    }
    if (!found || worker_stop_.load(std::memory_order_acquire)) {
        return;  // transient failure (level transition) - keep current camera
    }
    if (addr == matrix_address_.load(std::memory_order_acquire)) {
        return;  // same camera, nothing to do
    }
    ValidateAndPublishCandidate(addr, base, "refresh");
}

bool GtaCameraHook::ValidateAndPublishCandidate(uintptr_t candidate, uintptr_t cameraBase,
                                                const char* source) {
    if (!candidate || worker_stop_.load(std::memory_order_acquire)) {
        return false;
    }

    // Pure-read validation (no writes to candidate memory, no calls into
    // game code): metadata presence, writability, then two ScoreMatrix
    // samples ~40ms apart for stability.
    // Rejection verdicts are INFO (not DBG): with verbose off they are the
    // only way to see WHY a resolved candidate was not adopted.
    if (cameraBase) {
        uintptr_t meta = 0;
        if (!FindMetadataInCamera(cameraBase, meta)) {
            LOGSTRF("GtaCameraHook: candidate 0x%p (%s) REJECTED: no camera metadata\n",
                    reinterpret_cast<void*>(candidate), source);
            return false;
        }
    }

    if (!IsWritable(candidate, sizeof(GtaCameraMatrix))) {
        LOGSTRF("GtaCameraHook: candidate 0x%p (%s) REJECTED: not writable\n",
                reinterpret_cast<void*>(candidate), source);
        return false;
    }

    GtaCameraMatrix first = {};
    if (!SafeRead(candidate, &first, sizeof(first))) {
        LOGSTRF("GtaCameraHook: candidate 0x%p (%s) REJECTED: read failed\n",
                reinterpret_cast<void*>(candidate), source);
        return false;
    }
    float score = 0.0f;
    if (!ScoreMatrix(first, score)) {
        LOGSTRF("GtaCameraHook: candidate 0x%p (%s) REJECTED: implausible matrix\n",
                reinterpret_cast<void*>(candidate), source);
        return false;
    }

    // Stability sample #2 ~40ms later (interruptible on shutdown).
    for (int i = 0; i < 8; ++i) {
        if (worker_stop_.load(std::memory_order_acquire)) {
            return false;
        }
        Sleep(5);
    }
    GtaCameraMatrix second = {};
    float score2 = 0.0f;
    if (!SafeRead(candidate, &second, sizeof(second)) || !ScoreMatrix(second, score2)) {
        LOGSTRF("GtaCameraHook: candidate 0x%p (%s) REJECTED: failed stability re-read\n",
                reinterpret_cast<void*>(candidate), source);
        return false;
    }

    // Publish. Never overwrite an unconsumed candidate.
    if (handoff_pending_.load(std::memory_order_acquire)) {
        LOGSTRF("GtaCameraHook: candidate 0x%p (%s) deferred: previous handoff unconsumed\n",
                reinterpret_cast<void*>(candidate), source);
        return false;
    }

    uint32_t hashKey = 0;
    uint32_t hashName = 0;
    if (cameraBase) {
        TryReadCameraHashes(cameraBase, hashKey, hashName);
    }

    handoff_address_.store(candidate, std::memory_order_relaxed);
    handoff_base_.store(cameraBase, std::memory_order_relaxed);
    handoff_hash_key_.store(hashKey, std::memory_order_relaxed);
    handoff_hash_name_.store(hashName, std::memory_order_relaxed);
    handoff_pending_.store(true, std::memory_order_release);
    LOGSTRF("GtaCameraHook: candidate 0x%p ACCEPTED (%s, score=%.3f) - published to render thread\n",
            reinterpret_cast<void*>(candidate), source, score);
    return true;
}

void GtaCameraHook::ConsumeWorkerCandidate() {
    if (!handoff_pending_.load(std::memory_order_acquire)) {
        return;  // O(1) common case: nothing published
    }
    uint64_t candidate = handoff_address_.load(std::memory_order_relaxed);
    uint64_t cameraBase = handoff_base_.load(std::memory_order_relaxed);
    uint64_t hashKey = handoff_hash_key_.load(std::memory_order_relaxed);
    uint64_t hashName = handoff_hash_name_.load(std::memory_order_relaxed);
    handoff_pending_.store(false, std::memory_order_release);

    if (!candidate) {
        return;
    }
    if (matrix_address_.load(std::memory_order_acquire) == candidate &&
        hook_ready_.load(std::memory_order_acquire)) {
        return;  // already adopted
    }

    // Cheap O(1) read-only re-validation on the render thread (the worker
    // already fully validated; this only guards against the memory racing
    // away between publish and adopt).
    if (!IsWritable(candidate, sizeof(GtaCameraMatrix))) {
        LOGDBGF("GtaCameraHook: handoff candidate 0x%p dropped: no longer writable\n",
                reinterpret_cast<void*>(candidate));
        return;
    }
    GtaCameraMatrix matrix = {};
    if (!SafeRead(candidate, &matrix, sizeof(matrix))) {
        LOGDBGF("GtaCameraHook: handoff candidate 0x%p dropped: read failed\n",
                reinterpret_cast<void*>(candidate));
        return;
    }
    float score = 0.0f;
    if (!ScoreMatrix(matrix, score)) {
        LOGDBGF("GtaCameraHook: handoff candidate 0x%p dropped: implausible\n",
                reinterpret_cast<void*>(candidate));
        return;
    }

    matrix_address_.store(candidate, std::memory_order_release);
    hook_ready_.store(true, std::memory_order_release);
    if (cameraBase) {
        active_camera_base_.store(cameraBase, std::memory_order_release);
    }

    auto& stats = VR::GetRuntimeStats();
    stats.cameraMatrixAddress.store(candidate);
    stats.cameraMatrixWritable.store(true);
    stats.cameraHookReady.store(true);
    if (hashKey || hashName) {
        stats.activeCameraHash.store(static_cast<uint32_t>(hashKey));
        stats.activeCameraHashName.store(static_cast<uint32_t>(hashName));
    }
    LOGSTRF("GtaCameraHook: Camera matrix validated at 0x%p (score=%.3f)\n",
            reinterpret_cast<void*>(candidate), score);
}

bool GtaCameraHook::ScoreMatrix(const GtaCameraMatrix& matrix, float& outScore) const {
    XMVECTOR right = XMVectorSet(matrix.right[0], matrix.right[1], matrix.right[2], 0.0f);
    XMVECTOR forward = XMVectorSet(matrix.forward[0], matrix.forward[1], matrix.forward[2], 0.0f);
    XMVECTOR up = XMVectorSet(matrix.up[0], matrix.up[1], matrix.up[2], 0.0f);

    float lenR = XMVectorGetX(XMVector3Length(right));
    float lenF = XMVectorGetX(XMVector3Length(forward));
    float lenU = XMVectorGetX(XMVector3Length(up));
    if (!std::isfinite(lenR) || !std::isfinite(lenF) || !std::isfinite(lenU)) {
        return false;
    }
    if (lenR < 0.1f || lenR > 3.0f ||
        lenF < 0.1f || lenF > 3.0f ||
        lenU < 0.1f || lenU > 3.0f) {
        return false;
    }

    float dotRF = std::fabs(XMVectorGetX(XMVector3Dot(right, forward)));
    float dotRU = std::fabs(XMVectorGetX(XMVector3Dot(right, up)));
    float dotFU = std::fabs(XMVectorGetX(XMVector3Dot(forward, up)));
    if (dotRF > 0.95f || dotRU > 0.95f || dotFU > 0.95f) {
        return false;
    }

    XMVECTOR cross = XMVector3Cross(right, forward);
    float crossLen = XMVectorGetX(XMVector3Length(cross));
    if (crossLen < 0.1f) {
        return false;
    }

    XMVECTOR crossNorm = XMVector3Normalize(cross);
    XMVECTOR upNorm = XMVector3Normalize(up);
    float crossDot = std::fabs(XMVectorGetX(XMVector3Dot(crossNorm, upNorm)));
    float crossErr = std::fabs(1.0f - crossDot);

    float wErr = std::fabs(matrix.right[3]) + std::fabs(matrix.forward[3]) + std::fabs(matrix.up[3]);
    float posW = matrix.position[3];
    if (!std::isfinite(wErr) || !std::isfinite(posW)) {
        return false;
    }

    float lenErr = std::fabs(lenR - 1.0f) + std::fabs(lenF - 1.0f) + std::fabs(lenU - 1.0f);
    float dotErr = dotRF + dotRU + dotFU;

    XMFLOAT4 pos = {};
    std::memcpy(&pos, matrix.position, sizeof(pos));
    float posAbs = (std::max)({std::fabs(pos.x), std::fabs(pos.y), std::fabs(pos.z)});
    if (posAbs > 10000000.0f) {
        return false;
    }
    float posErr = posAbs * 0.000001f;

    float posWErr = std::fabs(posW - 1.0f);
    outScore = lenErr * 1.0f + dotErr * 1.5f + crossErr * 0.5f + posErr + wErr * 0.5f + posWErr * 0.25f;
    return true;
}

bool GtaCameraHook::SafeRead(uintptr_t address, void* outData, size_t size) const {
    if (!IsReadable(address, size)) {
        return false;
    }
    __try {
        std::memcpy(outData, reinterpret_cast<void*>(address), size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

bool GtaCameraHook::SafeReadPtr(uintptr_t address, uintptr_t& outValue) const {
    uintptr_t value = 0;
    if (!SafeRead(address, &value, sizeof(value))) {
        return false;
    }
    outValue = value;
    return true;
}

bool GtaCameraHook::SafeWrite(uintptr_t address, const void* data, size_t size) const {
    if (!IsWritable(address, size)) {
        return false;
    }
    __try {
        std::memcpy(reinterpret_cast<void*>(address), data, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

bool GtaCameraHook::FindMatrixInCamera(uintptr_t cameraBase, uintptr_t& outAddress, size_t scanSize) const {
    if (!cameraBase) {
        return false;
    }

    if (scanSize < sizeof(GtaCameraMatrix)) {
        return false;
    }
    size_t scanLimit = (std::min)(scanSize, static_cast<size_t>(0x8000));
    float bestScore = 1000.0f;
    uintptr_t bestAddr = 0;

    for (size_t offset = 0; offset + sizeof(GtaCameraMatrix) <= scanLimit; offset += 0x10) {
        uintptr_t addr = cameraBase + offset;
        if (!IsWritable(addr, sizeof(GtaCameraMatrix))) {
            continue;
        }

        GtaCameraMatrix candidate = {};
        if (!SafeRead(addr, &candidate, sizeof(candidate))) {
            continue;
        }

        float score = 0.0f;
        if (!ScoreMatrix(candidate, score)) {
            continue;
        }

        if (score < bestScore) {
            bestScore = score;
            bestAddr = addr;
        }
    }

    if (!bestAddr) {
        return false;
    }

    outAddress = bestAddr;
    return true;
}

bool GtaCameraHook::FindMatrixViaPointerScan(uintptr_t base,
                                             uintptr_t& outAddress,
                                             uintptr_t& outCameraBase) const {
    if (!base) {
        return false;
    }

    constexpr size_t kPtrScanSize = 0x4000;
    constexpr size_t kChildScanSize = 0x8000;

    for (size_t offset = 0; offset + sizeof(uintptr_t) <= kPtrScanSize; offset += sizeof(uintptr_t)) {
        uintptr_t ptrAddr = base + offset;
        if (!IsReadable(ptrAddr, sizeof(uintptr_t))) {
            continue;
        }

        uintptr_t candidate = 0;
        if (!SafeReadPtr(ptrAddr, candidate)) {
            continue;
        }
        if (!candidate || candidate == base) {
            continue;
        }

        uintptr_t found = 0;
        if (FindMetadataInCamera(candidate, found)) {
            if (FindMatrixInCamera(candidate, found, kChildScanSize)) {
                if (candidate != last_logged_camera_base_) {
                    LOGSTRF("GtaCameraHook: Found camera via pointer scan at 0x%p (base 0x%p, offset 0x%zx)\n",
                            reinterpret_cast<void*>(candidate),
                            reinterpret_cast<void*>(base),
                            offset);
                    last_logged_camera_base_ = candidate;
                }
                outAddress = found;
                outCameraBase = candidate;
                return true;
            }
        }

        if (IsReadable(candidate, sizeof(uintptr_t))) {
            uintptr_t nested = 0;
            if (!SafeReadPtr(candidate, nested)) {
                continue;
            }
            if (nested && nested != candidate) {
                if (FindMetadataInCamera(nested, found) && FindMatrixInCamera(nested, found, kChildScanSize)) {
                    if (nested != last_logged_camera_base_) {
                        LOGSTRF("GtaCameraHook: Found camera via nested pointer scan at 0x%p (base 0x%p, offset 0x%zx)\n",
                                reinterpret_cast<void*>(nested),
                                reinterpret_cast<void*>(base),
                                offset);
                        last_logged_camera_base_ = nested;
                    }
                    outAddress = found;
                    outCameraBase = nested;
                    return true;
                }
            }
        }
    }

    return false;
}

bool GtaCameraHook::FindMetadataInCamera(uintptr_t cameraBase, uintptr_t& outMetadata) const {
    if (!cameraBase) {
        return false;
    }

    constexpr size_t kScanSize = 0x2000;
    for (size_t offset = 0; offset + sizeof(uintptr_t) <= kScanSize; offset += sizeof(uintptr_t)) {
        uintptr_t ptrAddr = cameraBase + offset;
        if (!IsReadable(ptrAddr, sizeof(uintptr_t))) {
            continue;
        }
        uintptr_t meta = 0;
        if (!SafeReadPtr(ptrAddr, meta)) {
            continue;
        }
        if (!meta || !IsReadable(meta, sizeof(uintptr_t) + sizeof(uint32_t))) {
            continue;
        }
        uint32_t hash = 0;
        if (!SafeRead(meta + sizeof(void*), &hash, sizeof(hash))) {
            continue;
        }
        if (IsKnownCameraHash(hash)) {
            outMetadata = meta;
            return true;
        }
    }

    return false;
}

bool GtaCameraHook::FindCameraByMetadataScan(uintptr_t base, uintptr_t& outCameraBase) const {
    if (!base) {
        return false;
    }

    constexpr size_t kScanSize = 0x4000;
    for (size_t offset = 0; offset + sizeof(uintptr_t) <= kScanSize; offset += sizeof(uintptr_t)) {
        uintptr_t ptrAddr = base + offset;
        if (!IsReadable(ptrAddr, sizeof(uintptr_t))) {
            continue;
        }
        uintptr_t candidate = 0;
        if (!SafeReadPtr(ptrAddr, candidate)) {
            continue;
        }
        if (!candidate || candidate == base) {
            continue;
        }
        uintptr_t meta = 0;
        if (FindMetadataInCamera(candidate, meta)) {
            LOGSTRF("GtaCameraHook: Found camera via metadata scan at 0x%p (base 0x%p, offset 0x%zx)\n",
                    reinterpret_cast<void*>(candidate),
                    reinterpret_cast<void*>(base),
                    offset);
            outCameraBase = candidate;
            return true;
        }
    }

    return false;
}

bool GtaCameraHook::TryDirectMatrixScan(const CameraConfig& config, uintptr_t& outAddress) {
    // Resolves one pattern entry (config or build-manifest) end to end: AOB
    // scan, RIP-relative chain, pointer walk, then a bounded plausibility
    // scan around base+matrixOffset. Failures are logged at INFO so a camera
    // that never resolves is diagnosable without verbose logging.

    HMODULE module = GetModuleHandleW(config.module.empty() ? L"GTA5.exe" : config.module.c_str());
    if (!module) {
        module = GetModuleHandle(nullptr);
    }

    uintptr_t patternAddr = PatternScanner::FindPattern(config.pattern.c_str(), module, &worker_stop_);
    if (!patternAddr) {
        LOGSTRF("GtaCameraHook: direct-scan: pattern NOT FOUND in module image: %s\n",
                config.pattern.c_str());
        return false;
    }
    LOGSTRF("GtaCameraHook: direct-scan: pattern matched at 0x%p\n",
            reinterpret_cast<void*>(patternAddr));

    uintptr_t addr = patternAddr + config.patternOffset;

    auto resolveRip = [](uintptr_t at, int64_t offset) -> uintptr_t {
        int32_t disp = 0;
        __try {
            disp = *reinterpret_cast<int32_t*>(at + offset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return static_cast<uintptr_t>(0);
        }
        return at + offset + sizeof(int32_t) + disp;
    };

    if (!config.relativeOffsets.empty()) {
        for (int64_t offset : config.relativeOffsets) {
            if (!IsReadable(addr + offset, sizeof(int32_t))) {
                LOGSTRF("GtaCameraHook: direct-scan: RIP chain broken - disp32 at 0x%p (offset %lld) not readable\n",
                        reinterpret_cast<void*>(addr + offset), static_cast<long long>(offset));
                return false;
            }
            addr = resolveRip(addr, offset);
            if (!addr) {
                LOGSTRF("GtaCameraHook: direct-scan: RIP chain broken - read fault at offset %lld\n",
                        static_cast<long long>(offset));
                return false;
            }
        }
    } else if (config.ripOffsetSet && config.ripOffset >= 0) {
        if (!IsReadable(addr + config.ripOffset, sizeof(int32_t))) {
            LOGSTRF("GtaCameraHook: direct-scan: RIP disp32 at 0x%p not readable\n",
                    reinterpret_cast<void*>(addr + config.ripOffset));
            return false;
        }
        addr = resolveRip(addr, config.ripOffset);
        if (!addr) {
            LOGSTR("GtaCameraHook: direct-scan: RIP resolve read fault\n");
            return false;
        }
    }

    bool pointerChainUnresolved = false;
    if (!config.pointerOffsets.empty()) {
        uintptr_t current = addr;
        bool pointerResolved = true;
        for (int64_t offset : config.pointerOffsets) {
            if (!IsReadable(current, sizeof(uintptr_t))) {
                pointerResolved = false;
                break;
            }

            uintptr_t ptr = 0;
            if (!SafeReadPtr(current, ptr) || !ptr) {
                pointerResolved = false;
                break;
            }

            current = ptr + offset;
        }

        if (pointerResolved) {
            addr = current;
        } else {
            pointerChainUnresolved = true;
        }
    }
    if (pointerChainUnresolved) {
        // The slot is null/unreadable (camera object not allocated yet). The
        // scan below then searches around the unresolved address and normally
        // finds nothing - logged at INFO: a prime live-failure suspect.
        LOGSTRF("GtaCameraHook: direct-scan: pointer chain unresolved (slot 0x%p null/unreadable) - falling back to area scan\n",
                reinterpret_cast<void*>(addr));
    }
    LOGDBGF("GtaCameraHook: direct-scan: resolved base 0x%p, scanning for matrix\n",
            reinterpret_cast<void*>(addr));

    // Now scan the resolved address area for a valid camera matrix
    // Try the matrixOffset first, then scan nearby
    constexpr size_t kScanRange = 0x1000;  // 4KB scan range
    constexpr size_t kScanStep = 0x10;     // 16-byte alignment

    // Lower score is better for ScoreMatrix; keep the best plausible candidate
    float bestScore = 1000.0f;
    uintptr_t bestCandidate = 0;
    size_t bestOffset = 0;

    auto considerCandidate = [&](uintptr_t candidate, size_t offset) {
        if (!IsWritable(candidate, sizeof(GtaCameraMatrix))) {
            return;
        }

        GtaCameraMatrix matrix = {};
        if (!SafeRead(candidate, &matrix, sizeof(matrix))) {
            return;
        }

        float score = 0.0f;
        if (!ScoreMatrix(matrix, score)) {
            return;
        }

        if (score < bestScore) {
            bestScore = score;
            bestCandidate = candidate;
            bestOffset = offset;
        }
    };

    // First try the specified offset
    if (config.matrixOffset != 0) {
        considerCandidate(addr + config.matrixOffset, 0);
    }

    // Scan the area around the resolved address
    for (size_t offset = 0; offset < kScanRange; offset += kScanStep) {
        considerCandidate(addr + offset, offset);
    }

    if (!bestCandidate) {
        LOGSTRF("GtaCameraHook: direct-scan: no plausible writable matrix near 0x%p\n",
                reinterpret_cast<void*>(addr));
        return false;
    }

    LOGSTRF("GtaCameraHook: Direct scan best matrix at 0x%p offset 0x%zx (score=%.3f)\n",
            (void*)bestCandidate, bestOffset, bestScore);
    outAddress = bestCandidate;
    return true;
}

bool GtaCameraHook::TryResolveFromActiveCamera(uintptr_t& outAddress, uintptr_t& outCameraBase) {
    outAddress = 0;
    outCameraBase = 0;

    // WORKER THREAD ONLY. Access to the shared GtaCameraFov goes through the
    // lifetime token cached at worker start (object known-alive then); it is
    // locked around every use, so the worker can never race the VRManager
    // shutdown order (cameraFov is destroyed before cameraHook).
    GtaCameraFov* fov = worker_fov_;
    GtaCameraFov localFov;
    std::unique_lock<std::mutex> gateLock;

    if (!fov) {
        // No shared scanner: resolve synchronously on this (background)
        // thread with a local instance.
        if (!localFov.ResolveBlocking()) {
            return false;
        }
        fov = &localFov;
    } else {
        if (!worker_fov_token_) {
            return false;
        }
        gateLock = std::unique_lock<std::mutex>(worker_fov_token_->mutex);
        if (!worker_fov_token_->alive.load(std::memory_order_acquire)) {
            return false;  // GtaCameraFov is being destroyed
        }
        if (!fov->IsReady()) {
            fov->Initialize();  // async kick-off, cheap
            return false;
        }
    }

    uintptr_t cameraBase = fov->GetActiveCameraAddress();
    uintptr_t directorBase = fov->GetDirectorAddress();

    uintptr_t meta = 0;
    if (cameraBase && !FindMetadataInCamera(cameraBase, meta)) {
        static uintptr_t last_unverified_camera_base = 0;
        if (cameraBase != last_unverified_camera_base) {
            LOGSTRF("GtaCameraHook: Active camera base 0x%p has no metadata match, scanning anyway\n",
                    reinterpret_cast<void*>(cameraBase));
            last_unverified_camera_base = cameraBase;
        }
    }

    if (!cameraBase) {
        if (!directorBase || !IsReadable(directorBase, sizeof(uintptr_t))) {
            return false;
        }

        uintptr_t metaCamera = 0;
        if (FindCameraByMetadataScan(directorBase, metaCamera)) {
            cameraBase = metaCamera;
        } else {
            uintptr_t foundBase = 0;
            if (FindMatrixViaPointerScan(directorBase, outAddress, foundBase)) {
                outCameraBase = foundBase;
                return true;
            }
            return false;
        }
    }

    if (cameraBase != active_camera_base_.load(std::memory_order_acquire)) {
        LOGSTRF("GtaCameraHook: Active camera base = 0x%p\n", reinterpret_cast<void*>(cameraBase));
        active_camera_base_.store(cameraBase, std::memory_order_release);
    }
    outCameraBase = cameraBase;

    if (config_.matrixOffset != 0) {
        uintptr_t candidate = cameraBase + static_cast<intptr_t>(config_.matrixOffset);
        if (IsWritable(candidate, sizeof(GtaCameraMatrix))) {
            GtaCameraMatrix matrix = {};
            if (SafeRead(candidate, &matrix, sizeof(matrix))) {
                float score = 0.0f;
                if (ScoreMatrix(matrix, score)) {
                    LOGSTRF("GtaCameraHook: Using camera matrix at 0x%p (score=%.3f)\n",
                            reinterpret_cast<void*>(candidate), score);
                    outAddress = candidate;
                    return true;
                }
            }
        }
    }

    if (FindMatrixInCamera(cameraBase, outAddress, 0x4000)) {
        LOGSTRF("GtaCameraHook: Scanned camera matrix at 0x%p\n",
                reinterpret_cast<void*>(outAddress));
        return true;
    }

    if (directorBase) {
        uintptr_t metaCamera = 0;
        if (FindCameraByMetadataScan(directorBase, metaCamera) && metaCamera != cameraBase) {
            uintptr_t found = 0;
            if (FindMatrixInCamera(metaCamera, found, 0x4000)) {
                LOGSTRF("GtaCameraHook: Switched camera base to 0x%p\n",
                        reinterpret_cast<void*>(metaCamera));
                active_camera_base_.store(metaCamera, std::memory_order_release);
                outCameraBase = metaCamera;
                outAddress = found;
                return true;
            }
        }
    }

    uintptr_t foundBase = 0;
    if (FindMatrixViaPointerScan(cameraBase, outAddress, foundBase)) {
        if (foundBase != active_camera_base_.load(std::memory_order_acquire)) {
            LOGSTRF("GtaCameraHook: Active camera base updated via pointer scan = 0x%p\n",
                    reinterpret_cast<void*>(foundBase));
            active_camera_base_.store(foundBase, std::memory_order_release);
        }
        outCameraBase = foundBase;
        return true;
    }

    return false;
}

bool GtaCameraHook::TryResolveFromMetadataObjects(uintptr_t& outAddress,
                                                  uintptr_t& outCameraBase,
                                                  uint64_t deadlineTick) const {
    outAddress = 0;
    outCameraBase = 0;

    // WORKER THREAD ONLY. Both sweeps below walk the whole process address
    // space (~7.7s+ each on a live GTA5.exe); they poll ShouldAbortScan so
    // they stay interruptible (shutdown + resolution-budget deadline).
    //
    // Throttle: run at most once per N seconds;
    // N is a manifest value ([detect] metadataSweepIntervalSec, default 5).
    int sweepIntervalSec = 5;
    BuildManifest::Get().GetMetadataSweepIntervalSeconds(sweepIntervalSec);
    if (sweepIntervalSec < 1) {
        sweepIntervalSec = 1;
    }
    ULONGLONG nowTick = GetTickCount64();
    if (last_metadata_sweep_tick_ != 0 &&
        nowTick - last_metadata_sweep_tick_ < static_cast<ULONGLONG>(sweepIntervalSec) * 1000ull) {
        LOGDBGF("GtaCameraHook: metadata sweep skipped (throttled, interval=%ds)\n", sweepIntervalSec);
        return false;
    }
    last_metadata_sweep_tick_ = nowTick;

    HMODULE module = GetModuleHandleW(config_.module.empty() ? L"GTA5.exe" : config_.module.c_str());
    if (!module) {
        module = GetModuleHandle(nullptr);
    }
    if (!module) {
        return false;
    }

    uintptr_t imageBase = 0;
    uintptr_t imageEnd = 0;
    if (!GetModuleImageRange(module, imageBase, imageEnd)) {
        return false;
    }

    uintptr_t textBase = 0;
    uintptr_t textEnd = 0;
    uintptr_t rdataBase = 0;
    uintptr_t rdataEnd = 0;
    GetModuleSectionRange(module, ".text", textBase, textEnd);
    GetModuleSectionRange(module, ".rdata", rdataBase, rdataEnd);

    auto isModulePointer = [&](uintptr_t value) -> bool {
        if (textBase && value >= textBase && value < textEnd) {
            return true;
        }
        if (rdataBase && value >= rdataBase && value < rdataEnd) {
            return true;
        }
        return value >= imageBase && value < imageEnd;
    };

    std::vector<uintptr_t> metadataCandidates;
    metadataCandidates.reserve(64);

    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    uintptr_t address = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    uintptr_t maxAddress = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    LOGDBGF("GtaCameraHook: metadata sweep 1/2 start (candidate objects)\n");
    ULONGLONG sweepStart = GetTickCount64();
    bool aborted = false;

    while (address < maxAddress) {
        if (ShouldAbortScan(deadlineTick)) {
            aborted = true;
            break;
        }
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) {
            break;
        }

        uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t regionEnd = regionBase + static_cast<uintptr_t>(mbi.RegionSize);
        address = regionEnd;

        if (mbi.State != MEM_COMMIT ||
            (mbi.Protect & PAGE_GUARD) ||
            (mbi.Protect & PAGE_NOACCESS) ||
            !HasReadableProtection(mbi.Protect) ||
            (mbi.Type != MEM_IMAGE && mbi.Type != MEM_PRIVATE && mbi.Type != MEM_MAPPED) ||
            mbi.RegionSize < 0x20) {
            continue;
        }

        auto* bytes = reinterpret_cast<const uint8_t*>(regionBase);
        size_t limit = static_cast<size_t>(mbi.RegionSize - 0x10);
        for (size_t offset = 0; offset <= limit; offset += sizeof(uintptr_t)) {
            // Cheap periodic abort check (bitmask, ~every 512KB scanned).
            if ((offset & 0x7FFFF) == 0 && ShouldAbortScan(deadlineTick)) {
                aborted = true;
                break;
            }
            uintptr_t vftable = 0;
            uint32_t hash = 0;
            // SEH-guarded: the region may be freed between VirtualQuery and now.
            // A fault means the whole region went away, so stop scanning it.
            if (!SehReadPtrAndHash(bytes + offset, vftable, hash)) {
                break;
            }

            if (!IsKnownCameraHash(hash) || !isModulePointer(vftable)) {
                continue;
            }

            metadataCandidates.push_back(regionBase + offset);
        }
        if (aborted) {
            break;
        }
    }

    if (aborted) {
        LOGDBGF("GtaCameraHook: metadata sweep 1/2 ABORTED after %llums\n",
                GetTickCount64() - sweepStart);
        return false;
    }
    LOGDBGF("GtaCameraHook: metadata sweep 1/2 end: %zu candidates, %llums\n",
            metadataCandidates.size(), GetTickCount64() - sweepStart);

    if (metadataCandidates.empty()) {
        return false;
    }

    std::sort(metadataCandidates.begin(), metadataCandidates.end());
    metadataCandidates.erase(std::unique(metadataCandidates.begin(), metadataCandidates.end()),
                             metadataCandidates.end());

    LOGSTRF("GtaCameraHook: Metadata object scan found %zu camera metadata candidates\n",
            metadataCandidates.size());

    LOGDBGF("GtaCameraHook: metadata sweep 2/2 start (back-references)\n");
    sweepStart = GetTickCount64();

    constexpr size_t kCameraOffsets[] = { 0x540, 0x230, 0x10 };
    address = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    while (address < maxAddress) {
        if (ShouldAbortScan(deadlineTick)) {
            aborted = true;
            break;
        }
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) {
            break;
        }

        uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t regionEnd = regionBase + static_cast<uintptr_t>(mbi.RegionSize);
        address = regionEnd;

        if (mbi.State != MEM_COMMIT ||
            (mbi.Protect & PAGE_GUARD) ||
            (mbi.Protect & PAGE_NOACCESS) ||
            !HasReadableProtection(mbi.Protect) ||
            (mbi.Type != MEM_PRIVATE && mbi.Type != MEM_MAPPED && mbi.Type != MEM_IMAGE) ||
            mbi.RegionSize < sizeof(uintptr_t)) {
            continue;
        }

        auto* words = reinterpret_cast<const uintptr_t*>(regionBase);
        size_t wordCount = static_cast<size_t>(mbi.RegionSize / sizeof(uintptr_t));
        for (size_t i = 0; i < wordCount; ++i) {
            if ((i & 0xFFFF) == 0 && ShouldAbortScan(deadlineTick)) {
                aborted = true;
                break;
            }
            uintptr_t value = 0;
            // SEH-guarded: the region may be freed between VirtualQuery and now.
            if (!SehReadWord(words + i, value)) {
                break;
            }
            if (!std::binary_search(metadataCandidates.begin(), metadataCandidates.end(), value)) {
                continue;
            }

            uintptr_t ptrAddress = regionBase + (i * sizeof(uintptr_t));
            for (size_t metadataOffset : kCameraOffsets) {
                if (ptrAddress < metadataOffset) {
                    continue;
                }

                uintptr_t cameraBase = ptrAddress - metadataOffset;
                if ((cameraBase & 0xF) != 0 ||
                    !IsReadable(cameraBase, metadataOffset + sizeof(uintptr_t))) {
                    continue;
                }

                uintptr_t matrixAddress = 0;
                if (!FindMatrixInCamera(cameraBase, matrixAddress, 0x4000)) {
                    continue;
                }

                LOGSTRF("GtaCameraHook: Resolved active camera via metadata object 0x%p -> camera 0x%p\n",
                        reinterpret_cast<void*>(value),
                        reinterpret_cast<void*>(cameraBase));
                LOGDBGF("GtaCameraHook: metadata sweep 2/2 end: match, %llums\n",
                        GetTickCount64() - sweepStart);
                outAddress = matrixAddress;
                outCameraBase = cameraBase;
                return true;
            }
        }
        if (aborted) {
            break;
        }
    }

    LOGDBGF("GtaCameraHook: metadata sweep 2/2 end: no match, %llums%s\n",
            GetTickCount64() - sweepStart, aborted ? " (ABORTED)" : "");
    return false;
}

bool GtaCameraHook::TryReadCameraHashes(uintptr_t cameraBase,
                                        uint32_t& outHashKey,
                                        uint32_t& outHashName) const {
    outHashKey = 0;
    outHashName = 0;

    uintptr_t metadata = 0;
    if (!FindMetadataInCamera(cameraBase, metadata)) {
        return false;
    }

    struct CameraMetadataHeader {
        uintptr_t vftable;
        uint32_t hashKey;
        uint32_t hashName;
    };

    CameraMetadataHeader header = {};
    if (!SafeRead(metadata, &header, sizeof(header))) {
        return false;
    }

    if (!IsKnownCameraHash(header.hashKey) && !IsKnownCameraHash(header.hashName)) {
        return false;
    }

    outHashKey = header.hashKey;
    outHashName = header.hashName;
    return true;
}

void GtaCameraHook::Update(VR::Eye eye) {
    // Call without game state - use settings to determine behavior
    Update(eye, nullptr);
}

void GtaCameraHook::Update(VR::Eye eye, GtaGameState* gameState) {
    update_count_++;

    // OnlineGuard hard-disable: skip ALL camera writes (single-player-only
    // hard constraint, docs/online-guard.md). The Present-hook owner adds the
    // primary pass-through; this guards the write path owned by this file.
    if (OnlineGuard::Get().ShouldDisableMod()) {
        return;
    }

    // O(1) handoff: adopt a worker-validated camera candidate, if any.
    // ALL scanning/resolution lives on the background worker (WorkerMain);
    // the render thread never pattern-scans, sweeps process memory, or calls
    // into game code from here.
    ConsumeWorkerCandidate();

    // Skip camera WRITES during cutscenes, menus, loading
    bool skipCutscene = false;
    if (gameState && gameState->IsCutsceneActive()) {
        auto& cutscene = VR::GetCutsceneSettings();
        auto mode = static_cast<VR::CutsceneMode>(cutscene.mode.load());
        skipCutscene = (mode == VR::CutsceneMode::VirtualScreen);
    }

    if (gameState && (gameState->IsLoading() || gameState->IsInMenu() || skipCutscene)) {
        if (update_count_ % 300 == 1) {
            LOGSTR("GtaCameraHook: Skipping camera writes (menu/loading/cutscene virtual screen)\n");
        }
        return;
    }

    if (!IsReady()) {
        if (update_count_ % 300 == 1) {
            LOGDBGF("GtaCameraHook: Not ready (addr=%p, handoffPending=%d) - worker resolving\n",
                    reinterpret_cast<void*>(matrix_address_.load(std::memory_order_acquire)),
                    handoff_pending_.load(std::memory_order_acquire) ? 1 : 0);
        }
        return;
    }

    if (!IsWritable(matrix_address_.load(std::memory_order_acquire), sizeof(GtaCameraMatrix))) {
        static bool logged = false;
        if (!logged) {
            LOGSTRF("GtaCameraHook: Matrix address 0x%p is not writable, disabling hook\n",
                    reinterpret_cast<void*>(matrix_address_.load(std::memory_order_acquire)));
            logged = true;
        }
        hook_ready_.store(false, std::memory_order_release);
        auto& stats = VR::GetRuntimeStats();
        stats.cameraHookReady.store(false);
        return;
    }

    if (!backend_) {
        return;
    }

    // Check if we should apply decoupling
    bool shouldDecouple = decoupling_enabled_;
    bool isAiming = false;
    bool isInCutscene = false;
    bool showVirtualScreen = false;

    if (gameState) {
        shouldDecouple = gameState->ShouldApplyDecoupling();
        isAiming = gameState->IsAiming();
        isInCutscene = gameState->IsCutsceneActive();
        showVirtualScreen = gameState->ShouldShowVirtualScreen();
    } else {
        // No game state - use settings
        auto& settings = VR::GetDecouplingSettings();
        shouldDecouple = settings.enabled.load();
        auto& gameInfo = VR::GetGameStateInfo();
        isAiming = gameInfo.isAiming.load();
        isInCutscene = gameInfo.isCutsceneActive.load();
    }

    // Handle cutscene transitions for smooth rotation blending
    // Detect transition INTO cutscene
    if (isInCutscene && !was_in_cutscene_) {
        // Entering cutscene - store current rotation
        pre_cutscene_rotation_ = last_game_rotation_;
        cutscene_blend_alpha_ = 0.0f;
        LOGSTR("GtaCameraHook: Entering cutscene - stored pre-cutscene rotation\n");
    }
    // Detect transition OUT OF cutscene
    else if (!isInCutscene && was_in_cutscene_) {
        // Exiting cutscene - compute world rotation offset to apply
        // This helps prevent jarring rotation when cutscene ends
        if (backend_) {
            GtaCameraMatrix currentMatrix;
            if (ReadGameCamera(currentMatrix)) {
                XMMATRIX currentGameRot = ExtractRotationMatrix(currentMatrix);
                // Calculate offset = pre_cutscene * inverse(current)
                // This offset will be smoothly blended out
                XMMATRIX currentInverse = XMMatrixInverse(nullptr, currentGameRot);
                world_rotation_offset_ = XMMatrixMultiply(pre_cutscene_rotation_, currentInverse);
            }
        }
        cutscene_blend_alpha_ = 1.0f;  // Start blending out the offset
        LOGSTR("GtaCameraHook: Exiting cutscene - calculated world rotation offset\n");
    }
    was_in_cutscene_ = isInCutscene;

    // Update blend alpha (blend out the offset over time)
    if (cutscene_blend_alpha_ > 0.0f && !isInCutscene) {
        // Assume ~60fps, blend over ~0.5 seconds
        cutscene_blend_alpha_ -= kCutsceneBlendSpeed / 60.0f;
        if (cutscene_blend_alpha_ < 0.0f) {
            cutscene_blend_alpha_ = 0.0f;
            world_rotation_offset_ = XMMatrixIdentity();
        }
    }

    // Don't modify camera during cutscenes if virtual screen mode
    if (showVirtualScreen) {
        return;
    }

    // Debug logging
    if (update_count_ % 300 == 1) {
        XMFLOAT3 headPos = backend_->GetHeadPosition();
        XMFLOAT3 headRot = backend_->GetHeadRotation();
        LOGSTRF("GtaCameraHook: Head rot=(%.1f, %.1f, %.1f) decouple=%d aim=%d writes=%u\n",
                headRot.x, headRot.y, headRot.z,
                shouldDecouple ? 1 : 0, isAiming ? 1 : 0,
                write_success_count_);
    }

    if (shouldDecouple) {
        UpdateWithDecoupling(eye, isAiming);
    } else {
        UpdateWithoutDecoupling(eye);
    }
}

void GtaCameraHook::RecenterPose() {
    if (!backend_) return;

    // Manual retry trigger: if the worker gave up on camera resolution
    // ("camera unresolved - staying in mono mode"), a recenter runs one full
    // resolution pass. Cheap on this thread (flag + event only).
    retry_requested_.store(true, std::memory_order_release);
    if (worker_wake_event_) {
        SetEvent(worker_wake_event_);
    }

    // Store current VR rotation as reference
    XMMATRIX headPose = backend_->GetHeadPoseMatrix();
    auto& view = VR::GetViewSettings();
    float snapYaw = view.snapYawOffsetDeg.load();
    if (std::fabs(snapYaw) > 0.001f) {
        XMMATRIX snapRot = XMMatrixRotationY(XMConvertToRadians(snapYaw));
        headPose = snapRot * headPose;
    }

    XMMATRIX headRotation = headPose;
    headRotation.r[3] = XMVectorSet(0, 0, 0, 1);
    reference_vr_rotation_ = headRotation;
    has_reference_pose_ = true;

    reference_vr_position_ = XMVectorSet(headPose.r[3].m128_f32[0],
                                         headPose.r[3].m128_f32[1],
                                         headPose.r[3].m128_f32[2],
                                         0.0f);
    has_reference_position_ = true;

    LOGSTR("GtaCameraHook: Pose recentered\n");
}

void GtaCameraHook::UpdateWithDecoupling(VR::Eye eye, bool isAiming) {
    // Read current game camera
    GtaCameraMatrix gameMatrix;
    if (!ReadGameCamera(gameMatrix)) {
        return;
    }

    // Extract game's rotation matrix
    XMMATRIX gameRotation = ExtractRotationMatrix(gameMatrix);

    // Get current VR head pose
    XMMATRIX headPose = backend_->GetHeadPoseMatrix();
    auto& view = VR::GetViewSettings();
    float snapYaw = view.snapYawOffsetDeg.load();
    if (std::fabs(snapYaw) > 0.001f) {
        XMMATRIX snapRot = XMMatrixRotationY(XMConvertToRadians(snapYaw));
        headPose = snapRot * headPose;
    }

    XMMATRIX headRotation = headPose;
    headRotation.r[3] = XMVectorSet(0, 0, 0, 1);

    auto& stereoSettings = VR::GetStereoSettings();
    bool headTracking = stereoSettings.headTracking.load();
    bool positionTracking = stereoSettings.positionTracking.load();

    if ((headTracking || positionTracking) && !has_reference_pose_) {
        reference_vr_rotation_ = headRotation;
        has_reference_pose_ = true;
    }

    XMMATRIX referenceInverse = has_reference_pose_
        ? XMMatrixInverse(nullptr, reference_vr_rotation_)
        : XMMatrixIdentity();

    XMMATRIX vrDeltaRotation = headTracking
        ? XMMatrixMultiply(referenceInverse, headRotation)
        : XMMatrixIdentity();

    // If aiming, clamp the VR rotation to a cone
    if (isAiming) {
        auto& settings = VR::GetDecouplingSettings();
        float coneDeg = settings.aimConeDeg.load();
        vrDeltaRotation = ClampRotationToCone(vrDeltaRotation, coneDeg);
    }

    // Compose: game rotation * VR delta rotation = final rotation
    XMMATRIX finalRotation = ComposeRotations(gameRotation, vrDeltaRotation);

    // Apply world rotation offset if transitioning out of cutscene
    // This smoothly blends the rotation to prevent jarring camera snaps
    if (cutscene_blend_alpha_ > 0.001f) {
        // Convert matrices to quaternions for smooth interpolation
        XMVECTOR offsetQuat = XMQuaternionRotationMatrix(world_rotation_offset_);
        XMVECTOR identityQuat = XMQuaternionIdentity();

        // SLERP between offset and identity based on blend alpha
        // As alpha decreases, we blend toward identity (no offset)
        XMVECTOR blendedQuat = XMQuaternionSlerp(identityQuat, offsetQuat, cutscene_blend_alpha_);

        // Apply the blended offset to the final rotation
        XMMATRIX blendedOffset = XMMatrixRotationQuaternion(blendedQuat);
        finalRotation = XMMatrixMultiply(finalRotation, blendedOffset);
    }

    XMMATRIX trackingToWorld = gameRotation;
    if (headTracking && has_reference_pose_) {
        trackingToWorld = XMMatrixMultiply(gameRotation, referenceInverse);
    }

    bool applyEyeOffset = stereoSettings.mode.load() == static_cast<int>(VR::StereoMode::AlternateEye);
    XMFLOAT4 position = ComputeCameraPosition(gameMatrix,
                                              finalRotation,
                                              trackingToWorld,
                                              headPose,
                                              eye,
                                              applyEyeOffset);

    // Write the composed rotation with computed position
    WriteCameraMatrix(finalRotation, position);

    // Store last game rotation for smooth transitions
    last_game_rotation_ = gameRotation;
}

void GtaCameraHook::UpdateWithoutDecoupling(VR::Eye eye) {
    // Simple mode: just apply VR rotation directly (original behavior)
    // Read current game camera for position
    GtaCameraMatrix gameMatrix;
    if (!ReadGameCamera(gameMatrix)) {
        return;
    }

    XMMATRIX gameRotation = ExtractRotationMatrix(gameMatrix);

    // Get VR head pose rotation
    XMMATRIX headPose = backend_->GetHeadPoseMatrix();
    auto& view = VR::GetViewSettings();
    float snapYaw = view.snapYawOffsetDeg.load();
    if (std::fabs(snapYaw) > 0.001f) {
        XMMATRIX snapRot = XMMatrixRotationY(XMConvertToRadians(snapYaw));
        headPose = snapRot * headPose;
    }

    XMMATRIX headRotation = headPose;
    headRotation.r[3] = XMVectorSet(0, 0, 0, 1);

    auto& stereoSettings = VR::GetStereoSettings();
    bool headTracking = stereoSettings.headTracking.load();
    bool positionTracking = stereoSettings.positionTracking.load();

    if ((headTracking || positionTracking) && !has_reference_pose_) {
        reference_vr_rotation_ = headRotation;
        has_reference_pose_ = true;
    }

    XMMATRIX finalRotation = headTracking ? headRotation : gameRotation;

    XMMATRIX trackingToWorld = XMMatrixIdentity();
    if (headTracking && has_reference_pose_) {
        trackingToWorld = XMMatrixInverse(nullptr, reference_vr_rotation_);
    } else if (!headTracking) {
        trackingToWorld = gameRotation;
    }

    bool applyEyeOffset = stereoSettings.mode.load() == static_cast<int>(VR::StereoMode::AlternateEye);
    XMFLOAT4 position = ComputeCameraPosition(gameMatrix,
                                              finalRotation,
                                              trackingToWorld,
                                              headPose,
                                              eye,
                                              applyEyeOffset);

    // Write VR rotation with computed position
    WriteCameraMatrix(finalRotation, position);
}

XMFLOAT4 GtaCameraHook::ComputeCameraPosition(const GtaCameraMatrix& gameMatrix,
                                              const XMMATRIX& finalRotation,
                                              const XMMATRIX& trackingToWorld,
                                              const XMMATRIX& headPose,
                                              VR::Eye eye,
                                              bool applyEyeOffset) {
    auto& stereoSettings = VR::GetStereoSettings();
    auto& cameraSettings = VR::GetCameraSettings();

    float worldScale = cameraSettings.worldScale.load();
    if (worldScale < 0.01f) worldScale = 0.01f;

    XMVECTOR basePos = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(gameMatrix.position));
    XMVECTOR totalOffset = XMVectorZero();

    if (stereoSettings.positionTracking.load()) {
        XMVECTOR headPos = XMVectorSet(headPose.r[3].m128_f32[0],
                                       headPose.r[3].m128_f32[1],
                                       headPose.r[3].m128_f32[2],
                                       0.0f);
        if (!has_reference_position_) {
            reference_vr_position_ = headPos;
            has_reference_position_ = true;
        }

        XMVECTOR delta = XMVectorSubtract(headPos, reference_vr_position_);
        delta = XMVectorScale(delta, worldScale);
        XMVECTOR worldDelta = XMVector3Transform(delta, trackingToWorld);
        totalOffset = XMVectorAdd(totalOffset, worldDelta);
    } else {
        has_reference_position_ = false;
    }

    float heightOffset = cameraSettings.playerHeight.load() - 1.7f;
    XMVECTOR localOffset = XMVectorSet(cameraSettings.cameraOffsetX.load(),
                                       cameraSettings.cameraOffsetY.load() + heightOffset,
                                       cameraSettings.cameraOffsetZ.load(),
                                       0.0f);
    localOffset = XMVectorScale(localOffset, worldScale);
    XMVECTOR worldOffset = XMVector3Transform(localOffset, finalRotation);
    totalOffset = XMVectorAdd(totalOffset, worldOffset);

    if (applyEyeOffset) {
        float desiredIpd = stereoSettings.stereoIPD.load();
        XMVECTOR eyeLocal = XMVectorZero();
        if (backend_) {
            XMMATRIX eyeMatrix = backend_->GetEyeMatrix(eye);
            eyeLocal = XMVectorSet(eyeMatrix.r[3].m128_f32[0],
                                   eyeMatrix.r[3].m128_f32[1],
                                   eyeMatrix.r[3].m128_f32[2],
                                   0.0f);
        }

        float runtimeIpd = std::fabs(XMVectorGetX(eyeLocal)) * 2.0f;
        if (runtimeIpd < 0.0001f) {
            float sign = (eye == VR::Eye::Left) ? -0.5f : 0.5f;
            eyeLocal = XMVectorSet(sign * desiredIpd, 0.0f, 0.0f, 0.0f);
        } else {
            float scale = desiredIpd / runtimeIpd;
            eyeLocal = XMVectorScale(eyeLocal, scale);
        }

        eyeLocal = XMVectorScale(eyeLocal, worldScale);
        XMVECTOR eyeWorld = XMVector3Transform(eyeLocal, finalRotation);
        totalOffset = XMVectorAdd(totalOffset, eyeWorld);
    }

    XMVECTOR finalPos = XMVectorAdd(basePos, totalOffset);
    XMFLOAT4 position;
    XMStoreFloat4(&position, finalPos);
    position.w = gameMatrix.position[3];
    return position;
}

bool GtaCameraHook::ReadGameCamera(GtaCameraMatrix& outMatrix) const {
    uintptr_t address = matrix_address_.load(std::memory_order_acquire);

    if (!IsReadable(address, sizeof(GtaCameraMatrix))) {
        return false;
    }

    return SafeRead(address, &outMatrix, sizeof(GtaCameraMatrix));
}

XMMATRIX GtaCameraHook::ExtractRotationMatrix(const GtaCameraMatrix& matrix) const {
    // GTA format: right, forward, up, position
    // Build a rotation matrix from the direction vectors
    XMVECTOR right = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(matrix.right));
    XMVECTOR forward = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(matrix.forward));
    XMVECTOR up = XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(matrix.up));

    // Normalize vectors (they should already be normalized, but just in case)
    right = XMVector3Normalize(right);
    forward = XMVector3Normalize(forward);
    up = XMVector3Normalize(up);

    // Build rotation matrix
    // In DirectX, rows are: right, up, forward (but GTA uses right, forward, up)
    XMMATRIX rotation;
    rotation.r[0] = right;
    rotation.r[1] = up;
    rotation.r[2] = forward;
    rotation.r[3] = XMVectorSet(0, 0, 0, 1);

    return rotation;
}

XMMATRIX GtaCameraHook::ClampRotationToCone(const XMMATRIX& rotation, float maxAngleDeg) const {
    if (maxAngleDeg >= 180.0f) {
        return rotation;  // No clamping needed
    }

    // Convert rotation matrix to quaternion for proper angle clamping
    // This preserves roll and handles all axes correctly
    XMVECTOR quat = XMQuaternionRotationMatrix(rotation);

    // Calculate the total rotation angle from identity
    // Quaternion angle is: angle = 2 * acos(w), where w is the scalar part
    XMFLOAT4 q;
    XMStoreFloat4(&q, quat);

    // Normalize the quaternion (should already be normalized, but ensure)
    float len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 0.0001f) {
        q.x /= len; q.y /= len; q.z /= len; q.w /= len;
    }

    // Clamp w to valid range for acos
    float w = (std::max)(-1.0f, (std::min)(1.0f, q.w));
    float totalAngleRad = 2.0f * acosf(fabsf(w));
    float maxAngleRad = maxAngleDeg * kDegToRad;

    if (totalAngleRad <= maxAngleRad) {
        return rotation;  // Within cone, no clamping needed
    }

    // Scale the quaternion to clamp the angle
    // For a quaternion (w, x, y, z) with angle θ, scaling to angle θ' requires:
    // sin(θ'/2) / sin(θ/2) factor for the vector part
    float currentHalfAngle = totalAngleRad * 0.5f;
    float targetHalfAngle = maxAngleRad * 0.5f;

    float sinCurrent = sinf(currentHalfAngle);
    if (fabsf(sinCurrent) < 0.0001f) {
        return rotation;  // Very small rotation, keep as-is
    }

    float sinTarget = sinf(targetHalfAngle);
    float scale = sinTarget / sinCurrent;

    // Build clamped quaternion
    XMFLOAT4 clampedQ;
    clampedQ.w = cosf(targetHalfAngle);
    if (q.w < 0) clampedQ.w = -clampedQ.w;  // Preserve quaternion hemisphere
    clampedQ.x = q.x * scale;
    clampedQ.y = q.y * scale;
    clampedQ.z = q.z * scale;

    XMVECTOR clampedQuat = XMLoadFloat4(&clampedQ);
    clampedQuat = XMQuaternionNormalize(clampedQuat);

    return XMMatrixRotationQuaternion(clampedQuat);
}

XMMATRIX GtaCameraHook::ComposeRotations(const XMMATRIX& base, const XMMATRIX& delta) const {
    // Multiply rotations: base * delta
    // This applies delta rotation in the local space of base
    return XMMatrixMultiply(base, delta);
}

void GtaCameraHook::WriteCameraMatrix(const XMMATRIX& rotation, const XMFLOAT4& position) {
    uintptr_t address = matrix_address_.load(std::memory_order_acquire);

    if (!IsWritable(address, sizeof(GtaCameraMatrix))) {
        return;
    }

    // Extract rotation vectors
    XMVECTOR right = rotation.r[0];
    XMVECTOR up = rotation.r[1];
    XMVECTOR forward = rotation.r[2];

    // Apply axis negation if configured
    if (config_.negateRight) right = XMVectorNegate(right);
    if (config_.negateUp) up = XMVectorNegate(up);
    if (config_.negateForward) forward = XMVectorNegate(forward);

    // Build GTA matrix: right, forward, up, position
    GtaCameraMatrix newMatrix = {};

    XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(newMatrix.right), right);
    XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(newMatrix.forward), forward);
    XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(newMatrix.up), up);
    newMatrix.position[0] = position.x;
    newMatrix.position[1] = position.y;
    newMatrix.position[2] = position.z;
    newMatrix.position[3] = position.w;

    if (!SafeWrite(address, &newMatrix, sizeof(newMatrix))) {
        hook_ready_.store(false, std::memory_order_release);
        auto& stats = VR::GetRuntimeStats();
        stats.cameraHookReady.store(false);
        return;
    }

    write_success_count_++;

    // DIAG (temporary, decisive): does our write survive until the next one?
    // Every ~300 writes, read back what the address contains BEFORE we write
    // it again. If the game rewrote its own values over ours, the write race
    // is lost and the ViewInverse-style patch is required; if ours persists,
    // the renderer must be reading a DIFFERENT camera object.
    {
        static GtaCameraMatrix lastWritten = {};
        static bool haveLastWritten = false;
        if (haveLastWritten && (write_success_count_ % 300 == 0)) {
            GtaCameraMatrix current = {};
            if (SafeRead(address, &current, sizeof(current))) {
                const float* a = reinterpret_cast<const float*>(&current);
                const float* b = reinterpret_cast<const float*>(&lastWritten);
                float diff = 0.0f;
                for (int i = 0; i < 16; ++i) diff += fabsf(a[i] - b[i]);
                LOGSTRF("GtaCameraHook: DIAG readback diff=%.3f -> %s\n", diff,
                        diff > 0.5f ? "GAME OVERWRITES our matrix (race lost)" : "our matrix persists (renderer reads elsewhere)");
            }
        }
        if (write_success_count_ % 2000 == 0 && camera_fov_.load()) {
            uintptr_t activeCam = camera_fov_.load()->GetActiveCameraAddress();
            uintptr_t ourBase = address - static_cast<uintptr_t>(config_.matrixOffset);
            LOGSTRF("GtaCameraHook: DIAG director activeCam=0x%p, our camBase=0x%p (matrix 0x%p) -> %s\n",
                    reinterpret_cast<void*>(activeCam), reinterpret_cast<void*>(ourBase),
                    reinterpret_cast<void*>(address),
                    (activeCam && activeCam == ourBase) ? "MATCH" : "MISMATCH");
        }
        lastWritten = newMatrix;
        haveLastWritten = true;
    }

    // Debug logging every ~5 seconds
    if (write_success_count_ % 300 == 1) {
        LOGSTRF("GtaCameraHook: Write #%u - pos=(%.1f, %.1f, %.1f)\n",
                write_success_count_,
                position.x, position.y, position.z);
    }
}

bool GtaCameraHook::LoadConfig(CameraConfig& outConfig) {
    std::wstring configPath = ResolveConfigPath();
    std::string pathUtf8 = WideToUtf8(configPath);

    LOGSTRF("GtaCameraHook: Loading config from: %s\n", pathUtf8.c_str());

    std::ifstream file(configPath);
    if (!file.is_open()) {
        LOGSTR("GtaCameraHook: Config file not found\n");
        return false;
    }

    bool inCamera = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }

        line = Trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            std::string section = ToLower(Trim(line.substr(1, line.size() - 2)));
            inCamera = (section == "camera");
            continue;
        }

        if (!inCamera) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = ToLower(Trim(line.substr(0, eq)));
        std::string value = Trim(line.substr(eq + 1));

        if (key == "module") {
            outConfig.module.assign(value.begin(), value.end());
        } else if (key == "pattern") {
            outConfig.pattern = value;
        } else if (key == "patternoffset") {
            outConfig.patternOffset = ParseInt(value, 0);
        } else if (key == "relativeoffsets") {
            outConfig.relativeOffsets = ParseOffsetList(value);
        } else if (key == "ripoffset") {
            outConfig.ripOffset = ParseInt(value, 0);
            // Treat ripOffset=0 as disabled to match config templates and avoid
            // resolving a displacement from the opcode byte.
            outConfig.ripOffsetSet = (outConfig.ripOffset != 0);
        } else if (key == "pointeroffsets") {
            outConfig.pointerOffsets = ParseOffsetList(value);
        } else if (key == "matrixoffset") {
            outConfig.matrixOffset = ParseInt(value, 0);
        } else if (key == "negateright") {
            outConfig.negateRight = ParseBool(value);
        } else if (key == "negateforward") {
            outConfig.negateForward = ParseBool(value);
        } else if (key == "negateup") {
            outConfig.negateUp = ParseBool(value);
        } else if (key == "resolvetimeoutsec") {
            outConfig.resolveTimeoutSec = static_cast<int>(ParseInt(value, 30));
        } else if (key == "backgroundretrysec") {
            outConfig.backgroundRetrySec = static_cast<int>(ParseInt(value, 60));
        }
    }

    if (outConfig.pattern.empty()) {
        LOGSTR("GtaCameraHook: No pattern found in config\n");
        return false;
    }

    LOGSTRF("GtaCameraHook: Loaded config - pattern: %s, matrixOffset: 0x%llX\n",
            outConfig.pattern.c_str(), outConfig.matrixOffset);
    return true;
}

bool GtaCameraHook::ResolveMatrixAddress(const CameraConfig& config, uintptr_t& outAddress) {
    HMODULE module = GetModuleHandleW(config.module.empty() ? L"GTA5.exe" : config.module.c_str());
    if (!module) {
        module = GetModuleHandle(nullptr);
        LOGSTR("GtaCameraHook: Using main module\n");
    }

    uintptr_t patternAddr = PatternScanner::FindPattern(config.pattern.c_str(), module, &worker_stop_);
    if (!patternAddr) {
        LOGDBGF("GtaCameraHook: Pattern not found (or aborted): %s\n", config.pattern.c_str());
        return false;
    }

    LOGSTRF("GtaCameraHook: Pattern found at 0x%p\n", (void*)patternAddr);

    uintptr_t addr = patternAddr + config.patternOffset;

    auto resolveRip = [](uintptr_t at, int64_t offset) -> uintptr_t {
        int32_t disp = 0;
        __try {
            disp = *reinterpret_cast<int32_t*>(at + offset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return static_cast<uintptr_t>(0);
        }
        return at + offset + sizeof(int32_t) + disp;
    };

    if (!config.relativeOffsets.empty()) {
        for (int64_t offset : config.relativeOffsets) {
            addr = resolveRip(addr, offset);
            LOGSTRF("GtaCameraHook: After relative offset %lld: 0x%p\n", offset, (void*)addr);
        }
    } else if (config.ripOffsetSet) {
        addr = resolveRip(addr, config.ripOffset);
        LOGSTRF("GtaCameraHook: After RIP offset %lld: 0x%p\n", config.ripOffset, (void*)addr);
    }

    // Track the base address before pointer dereferences for fallback
    uintptr_t baseBeforePointers = addr;

    auto tryDirectResolvedMatrix = [&](uintptr_t base, const char* reason) -> bool {
        uintptr_t candidate = base + config.matrixOffset;
        if (!IsReadable(candidate, sizeof(GtaCameraMatrix))) {
            return false;
        }

        GtaCameraMatrix matrix = {};
        if (!SafeRead(candidate, &matrix, sizeof(matrix))) {
            return false;
        }

        float score = 0.0f;
        if (!ScoreMatrix(matrix, score)) {
            return false;
        }

        LOGSTRF("GtaCameraHook: %s yielded direct matrix at 0x%p (score=%.3f)\n",
                reason, reinterpret_cast<void*>(candidate), score);
        outAddress = candidate;
        return true;
    };

    auto tryCameraBaseScan = [&](uintptr_t cameraBase, const char* reason) -> bool {
        if (!cameraBase) {
            return false;
        }

        uintptr_t candidate = 0;
        if (FindMatrixInCamera(cameraBase, candidate, 0x4000)) {
            LOGSTRF("GtaCameraHook: %s found scanned matrix 0x%p from base 0x%p\n",
                    reason,
                    reinterpret_cast<void*>(candidate),
                    reinterpret_cast<void*>(cameraBase));
            outAddress = candidate;
            return true;
        }

        uintptr_t foundBase = 0;
        if (FindMatrixViaPointerScan(cameraBase, candidate, foundBase)) {
            LOGSTRF("GtaCameraHook: %s found nested matrix 0x%p from base 0x%p\n",
                    reason,
                    reinterpret_cast<void*>(candidate),
                    reinterpret_cast<void*>(foundBase ? foundBase : cameraBase));
            outAddress = candidate;
            return true;
        }

        return false;
    };

    auto tryImplicitPointerFallback = [&](uintptr_t pointerSlot, const char* reason) -> bool {
        uintptr_t ptr = 0;
        if (!IsReadable(pointerSlot, sizeof(uintptr_t)) || !SafeReadPtr(pointerSlot, ptr) || !ptr) {
            return false;
        }

        if (tryDirectResolvedMatrix(ptr, reason)) {
            return true;
        }

        if (tryCameraBaseScan(ptr, reason)) {
            return true;
        }

        return false;
    };

    for (int64_t offset : config.pointerOffsets) {
        if (!IsReadable(addr, sizeof(uintptr_t))) {
            LOGSTRF("GtaCameraHook: Cannot read pointer at 0x%p\n", (void*)addr);
            return false;
        }
        uintptr_t ptr = 0;
        if (!SafeReadPtr(addr, ptr)) {
            LOGSTRF("GtaCameraHook: Failed to read pointer at 0x%p\n", (void*)addr);
            return false;
        }
        if (!ptr) {
            // Pointer is null - camera object not yet allocated
            // This is normal during loading. Log less frequently.
            if (config.pointerOffsets.size() == 1 && offset == 0 &&
                (tryDirectResolvedMatrix(baseBeforePointers, "Null pointer fallback") ||
                 tryCameraBaseScan(baseBeforePointers, "Null pointer fallback"))) {
                return true;
            }
            static uint32_t nullPtrLogCount = 0;
            if (nullPtrLogCount++ % 100 == 0) {
                LOGSTRF("GtaCameraHook: Null pointer in chain at 0x%p (camera not loaded yet, attempt %u)\n",
                        (void*)addr, nullPtrLogCount);
            }
            return false;
        }
        addr = ptr + offset;
        LOGSTRF("GtaCameraHook: After pointer offset %lld: 0x%p\n", offset, (void*)addr);
    }

    if (config.pointerOffsets.empty() &&
        tryImplicitPointerFallback(addr, "Implicit pointer fallback")) {
        return true;
    }

    uintptr_t resolvedBase = addr;
    addr += config.matrixOffset;

    if (!IsReadable(addr, sizeof(float) * 16)) {
        LOGSTRF("GtaCameraHook: Final address 0x%p is not readable\n", (void*)addr);
        return tryCameraBaseScan(resolvedBase, "Resolved-base scan fallback");
    }

    GtaCameraMatrix matrix = {};
    if (!SafeRead(addr, &matrix, sizeof(matrix))) {
        LOGSTRF("GtaCameraHook: Failed to read matrix at 0x%p\n", (void*)addr);
        return tryCameraBaseScan(resolvedBase, "Resolved-base scan fallback");
    }
    float score = 0.0f;
    if (!ScoreMatrix(matrix, score)) {
        LOGSTRF("GtaCameraHook: Resolved matrix at 0x%p is not plausible\n", (void*)addr);
        if (tryCameraBaseScan(resolvedBase, "Resolved-base scan fallback")) {
            return true;
        }
        if (tryImplicitPointerFallback(resolvedBase, "Resolved-base pointer fallback")) {
            return true;
        }
        return false;
    }

    outAddress = addr;
    return true;
}

std::wstring GtaCameraHook::ResolveConfigPath() const {
    wchar_t path[MAX_PATH] = {};

    DWORD len = GetEnvironmentVariableW(L"GTAVR_SETTINGS_DIR", path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::wstring full(path);
        if (!full.empty() && full.back() != L'\\' && full.back() != L'/') {
            full.push_back(L'\\');
        }
        full += L"gtavr_camera.ini";
        DWORD attrs = GetFileAttributesW(full.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return full;
        }
    }

    len = GetEnvironmentVariableW(L"GTAVR_CAMERA_PATH", path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::wstring(path);
    }

    HMODULE hModule = nullptr;
    static int s_moduleMarker = 0; // Use static variable address to find our module
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&s_moduleMarker), &hModule);
    if (hModule) {
        wchar_t dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(hModule, dllPath, MAX_PATH)) {
            std::wstring dir(dllPath);
            size_t lastSlash = dir.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos) {
                dir = dir.substr(0, lastSlash + 1);
            }
            return dir + L"gtavr_camera.ini";
        }
    }

    return L"gtavr_camera.ini";
}

bool GtaCameraHook::IsReadable(uintptr_t address, size_t size) const {
    if (!address || size == 0) return false;

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) {
        return false;
    }

    if (mbi.State != MEM_COMMIT) return false;
    if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) return false;

    size_t regionBase = reinterpret_cast<size_t>(mbi.BaseAddress);
    size_t regionSize = static_cast<size_t>(mbi.RegionSize);
    size_t end = address + size;
    return end <= (regionBase + regionSize);
}

bool GtaCameraHook::IsWritable(uintptr_t address, size_t size) const {
    if (!IsReadable(address, size)) return false;

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) {
        return false;
    }

    DWORD protect = mbi.Protect & 0xff;
    return protect == PAGE_READWRITE ||
           protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_WRITECOPY ||
           protect == PAGE_EXECUTE_WRITECOPY;
}

} // namespace Game
} // namespace OVRInject
