#pragma once

/**
 * PerfStats - frametime + engine-section instrumentation (Phase 8).
 *
 * Ring buffer of the last 4096 Present-to-Present frametimes, recorded on the
 * render thread from hookedPresent. Percentiles (p50/p99/p99.9), mean and a
 * dropped-frame estimate (frametime > 1.5x p50) are computed from the ring.
 * Separate accumulators track engine-internal section timings (camera write,
 * blit, submit) fed by StereoEngine with QueryPerformanceCounter.
 *
 * Once per elapsed second a row {timestamp, frames, p50, p99, p99.9, drops}
 * is finalized; rows are exported to gtavr_perf.csv on ShutdownVR/unload and
 * on demand via the F11 hotkey (polled on the render thread - the mod has no
 * WndProc hook to hang the hotkey on).
 *
 * GetSnapshot() is the tiny read API for the overlay (fields only; the overlay
 * display itself is next wave). p50/p99/p99.9 + fps are additionally mirrored
 * into VR::RuntimeStats atomics for cross-thread reads.
 */

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace OVRInject {
namespace Perf {

class PerfStats {
public:
    static PerfStats& Get();

    // Record one Present-to-Present frametime. Call once per hooked Present,
    // on the render thread, including pass-through frames.
    void Record();

    // Engine-section accumulators (StereoEngine, QPC-timed).
    void AddCameraWriteMs(double ms);
    void AddBlitMs(double ms);
    void AddSubmitMs(double ms);

    // Field-only read API for the overlay (computed over the ring window).
    struct Snapshot {
        uint64_t totalFrames = 0;
        float fps = 0.0f;               // frames in the last completed second
        float meanMs = 0.0f;
        float p50Ms = 0.0f;
        float p99Ms = 0.0f;
        float p999Ms = 0.0f;
        uint32_t droppedEstimate = 0;   // ring frames > 1.5x p50
        float cameraWriteAvgMs = 0.0f;
        float blitAvgMs = 0.0f;
        float submitAvgMs = 0.0f;
    };
    Snapshot GetSnapshot() const;

    // Write all finalized per-second rows to gtavr_perf.csv. Safe to call
    // repeatedly (F11 hotkey + unload both land here).
    void ExportCsv();

    // Idempotent. Flushes the partial current second as a final row, exports
    // the CSV, and ignores further Record() calls. Called from ShutdownVR.
    void Shutdown();

private:
    PerfStats();

    struct SecondRow {
        double tSec = 0.0;      // seconds since first recorded frame
        uint32_t frames = 0;
        float p50Ms = 0.0f;
        float p99Ms = 0.0f;
        float p999Ms = 0.0f;
        uint32_t drops = 0;     // frametimes > 1.5x this second's p50
    };

    void FinalizeSecondLocked(double nowSec);
    std::string ResolveCsvPath() const;

    static constexpr size_t kRingCapacity = 4096;
    static constexpr size_t kMaxRows = 14400;  // 4 h of 1 s rows; oldest dropped

    mutable std::mutex mutex_;
    bool shutdown_ = false;

    double freq_ = 0.0;         // QPC counts per second
    double lastSec_ = 0.0;      // timestamp of previous Present (seconds)
    bool haveLast_ = false;
    double startSec_ = 0.0;     // timestamp of first recorded frame
    double secStartSec_ = 0.0;  // start of the current 1 s bucket

    std::vector<float> ring_;   // frametimes, ms; writeIndex_ % kRingCapacity
    uint64_t writeIndex_ = 0;
    uint64_t totalFrames_ = 0;

    std::vector<float> secSamples_;
    std::deque<SecondRow> rows_;
    float lastSecondFps_ = 0.0f;

    double cameraWriteSumMs_ = 0.0;
    double blitSumMs_ = 0.0;
    double submitSumMs_ = 0.0;
    uint64_t cameraWriteCount_ = 0;
    uint64_t blitCount_ = 0;
    uint64_t submitCount_ = 0;
};

} // namespace Perf
} // namespace OVRInject
