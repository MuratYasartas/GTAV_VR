#include "PerfStats.hpp"

#include "../Log.hpp"
#include "../VR/SharedSettings.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace OVRInject {
namespace Perf {

namespace {

// Nearest-rank percentile over an ascending-sorted sample set.
float PercentileMs(const std::vector<float>& sorted, float p) {
    if (sorted.empty()) {
        return 0.0f;
    }
    float rank = (p / 100.0f) * static_cast<float>(sorted.size());
    size_t idx = static_cast<size_t>(rank);
    if (rank > static_cast<float>(idx)) {
        ++idx;  // round up to the nearest rank
    }
    if (idx == 0) idx = 1;
    if (idx > sorted.size()) idx = sorted.size();
    return sorted[idx - 1];
}

uint32_t CountDrops(const std::vector<float>& samples, float p50Ms) {
    if (p50Ms <= 0.0f) {
        return 0;
    }
    const float threshold = 1.5f * p50Ms;
    uint32_t drops = 0;
    for (float ms : samples) {
        if (ms > threshold) {
            ++drops;
        }
    }
    return drops;
}

} // namespace

PerfStats& PerfStats::Get() {
    static PerfStats stats;
    return stats;
}

PerfStats::PerfStats() {
    LARGE_INTEGER freq = {};
    QueryPerformanceFrequency(&freq);
    freq_ = freq.QuadPart > 0 ? static_cast<double>(freq.QuadPart) : 1.0;
    ring_.resize(kRingCapacity, 0.0f);
    secSamples_.reserve(512);
}

void PerfStats::Record() {
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    const double nowSec = static_cast<double>(now.QuadPart) / freq_;

    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) {
        return;
    }

    if (!haveLast_) {
        haveLast_ = true;
        startSec_ = nowSec;
        secStartSec_ = nowSec;
    } else {
        const float ms = static_cast<float>((nowSec - lastSec_) * 1000.0);
        ring_[writeIndex_ % kRingCapacity] = ms;
        ++writeIndex_;
        ++totalFrames_;
        secSamples_.push_back(ms);

        if (nowSec - secStartSec_ >= 1.0) {
            FinalizeSecondLocked(nowSec);
        }
    }
    lastSec_ = nowSec;
}

void PerfStats::FinalizeSecondLocked(double nowSec) {
    if (secSamples_.empty()) {
        secStartSec_ = nowSec;
        return;
    }

    std::vector<float> sorted = secSamples_;
    std::sort(sorted.begin(), sorted.end());

    SecondRow row;
    row.tSec = secStartSec_ - startSec_;
    row.frames = static_cast<uint32_t>(secSamples_.size());
    row.p50Ms = PercentileMs(sorted, 50.0f);
    row.p99Ms = PercentileMs(sorted, 99.0f);
    row.p999Ms = PercentileMs(sorted, 99.9f);
    row.drops = CountDrops(secSamples_, row.p50Ms);

    rows_.push_back(row);
    if (rows_.size() > kMaxRows) {
        rows_.pop_front();
    }
    lastSecondFps_ = static_cast<float>(row.frames);

    // Cross-thread mirrors for the overlay (and any other reader).
    auto& stats = VR::GetRuntimeStats();
    stats.fps.store(lastSecondFps_);
    stats.frametimeP50Ms.store(row.p50Ms);
    stats.frametimeP99Ms.store(row.p99Ms);
    stats.frametimeP999Ms.store(row.p999Ms);

    secSamples_.clear();
    secStartSec_ = nowSec;
}

void PerfStats::AddCameraWriteMs(double ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    cameraWriteSumMs_ += ms;
    ++cameraWriteCount_;
}

void PerfStats::AddBlitMs(double ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    blitSumMs_ += ms;
    ++blitCount_;
}

void PerfStats::AddSubmitMs(double ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    submitSumMs_ += ms;
    ++submitCount_;
}

PerfStats::Snapshot PerfStats::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    Snapshot snap;
    snap.totalFrames = totalFrames_;
    snap.fps = lastSecondFps_;

    const uint64_t used = (std::min)(writeIndex_, static_cast<uint64_t>(kRingCapacity));
    if (used > 0) {
        std::vector<float> samples;
        samples.reserve(static_cast<size_t>(used));
        if (writeIndex_ <= kRingCapacity) {
            samples.insert(samples.end(), ring_.begin(), ring_.begin() + static_cast<size_t>(used));
        } else {
            samples = ring_;
        }

        double sum = 0.0;
        for (float ms : samples) {
            sum += ms;
        }
        snap.meanMs = static_cast<float>(sum / static_cast<double>(samples.size()));

        std::sort(samples.begin(), samples.end());
        snap.p50Ms = PercentileMs(samples, 50.0f);
        snap.p99Ms = PercentileMs(samples, 99.0f);
        snap.p999Ms = PercentileMs(samples, 99.9f);
        snap.droppedEstimate = CountDrops(samples, snap.p50Ms);
    }

    snap.cameraWriteAvgMs = cameraWriteCount_ > 0
        ? static_cast<float>(cameraWriteSumMs_ / static_cast<double>(cameraWriteCount_)) : 0.0f;
    snap.blitAvgMs = blitCount_ > 0
        ? static_cast<float>(blitSumMs_ / static_cast<double>(blitCount_)) : 0.0f;
    snap.submitAvgMs = submitCount_ > 0
        ? static_cast<float>(submitSumMs_ / static_cast<double>(submitCount_)) : 0.0f;
    return snap;
}

std::string PerfStats::ResolveCsvPath() const {
    // GTAVR_PERF_CSV (full file path) wins, then GTAVR_LOG_DIR\gtavr_perf.csv,
    // then gtavr_perf.csv in the host process working directory (game dir).
    char path[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("GTAVR_PERF_CSV", path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return path;
    }
    char dir[MAX_PATH] = {};
    len = GetEnvironmentVariableA("GTAVR_LOG_DIR", dir, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::string resolved = dir;
        resolved += "\\gtavr_perf.csv";
        return resolved;
    }
    return "gtavr_perf.csv";
}

void PerfStats::ExportCsv() {
    std::deque<SecondRow> rowsCopy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rowsCopy = rows_;
    }

    const std::string path = ResolveCsvPath();
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "w") != 0 || !file) {
        LOGWNDF("PerfStats: Failed to open %s for CSV export\n", path.c_str());
        return;
    }

    std::fprintf(file, "timestamp_s,frames,p50_ms,p99_ms,p999_ms,drops\n");
    for (const SecondRow& row : rowsCopy) {
        std::fprintf(file, "%.3f,%u,%.3f,%.3f,%.3f,%u\n",
                     row.tSec,
                     static_cast<unsigned>(row.frames),
                     static_cast<double>(row.p50Ms),
                     static_cast<double>(row.p99Ms),
                     static_cast<double>(row.p999Ms),
                     static_cast<unsigned>(row.drops));
    }
    std::fclose(file);

    LOGSTRF("PerfStats: Exported %u per-second rows to %s\n",
            static_cast<unsigned>(rowsCopy.size()), path.c_str());
}

void PerfStats::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return;
        }
        shutdown_ = true;
        // Flush the partial current second so the tail of the session is not
        // lost from the CSV.
        if (!secSamples_.empty()) {
            FinalizeSecondLocked(lastSec_);
        }
    }
    ExportCsv();
}

} // namespace Perf
} // namespace OVRInject
