#pragma once

#include "../VR/SharedSettings.hpp"
#include "PatternScanner.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace OVRInject {
namespace Game {

class GtaCameraFov {
public:
    GtaCameraFov() = default;
    // Stops and joins the background resolve thread (if any) and flips the
    // lifetime token so cross-thread users (GtaCameraHook's worker) stop
    // touching this object before its memory is freed.
    ~GtaCameraFov();

    // Non-blocking: kicks off the background resolve on first call and returns
    // immediately. Safe to call from ANY thread (incl. the render thread) -
    // it never pattern-scans or calls into game code on the calling thread.
    // Returns IsReady(): true only once the director function is resolved.
    bool Initialize();

    // Synchronous resolve (pattern scans + SEH-wrapped trial call).
    // BACKGROUND THREADS ONLY - never call this from the render thread.
    bool ResolveBlocking();

    void Update(const VR::FovSettings& settings);
    bool IsReady() const { return get_cam_director_.load(std::memory_order_acquire) != nullptr; }

    // Read-only accessors backed by the cached director pointer (no calls
    // into game code, safe on the render thread).
    uintptr_t GetActiveCameraAddress() const;
    uintptr_t GetDirectorAddress() const;

    // Lifetime gate for cross-thread users. GtaCameraHook's background worker
    // caches this token once (while the object is known-alive) and locks it
    // around every use of this instance; the destructor takes the same lock
    // and clears `alive`, so the worker can never race the object being freed.
    struct LifetimeToken {
        std::mutex mutex;
        std::atomic<bool> alive{true};
    };
    std::shared_ptr<LifetimeToken> GetLifetimeToken() const { return lifetime_token_; }

private:
    struct camBaseObjectMetadata {
        void** vftable;
        uint32_t hashKey;
    };

    struct camBaseCameraMetadata : camBaseObjectMetadata {
        uint32_t hashName;
    };

    struct camBaseCamera {
        char pad[0x540];
        camBaseCameraMetadata* metadata;
    };

    struct camBaseDirector {
        char pad[0x2C0];
        camBaseCamera* activeCamera;
    };

    using GetCamDirectorFromPool = camBaseDirector* (*)();

    bool Resolve();
    // SEH-guarded trial call of a candidate getCamDirectorFromPool address
    // (kept out of Resolve, whose std::vector locals forbid __try - C2712).
    // A trial call is unavoidable for E8 call-target patterns: the returned
    // director pointer is runtime pool state that cannot be validated by
    // static reads. It only ever runs on the background resolve thread.
    bool TryAdoptDirectorFunction(GetCamDirectorFromPool fn, uintptr_t target, int patternIndex);
    camBaseCamera* GetActiveCamera() const;
    camBaseCameraMetadata* GetActiveMetadata() const;
    bool IsReadable(uintptr_t address, size_t size) const;
    bool WriteFloat(uintptr_t address, float value) const;
    int GetFovOffset(uint32_t hashKey, uint32_t hashName) const;
    float GetDesiredFov(const VR::FovSettings& settings,
                        uint32_t hashKey,
                        uint32_t hashName) const;

    // Background resolve/refresh thread.
    void ResolveThreadMain();
    void StopResolveThread();
    // SEH-wrapped call of the resolved director function, refreshing
    // cached_director_. Resolve thread only - keeps game-function calls off
    // the render thread (the per-frame path reads the cache instead).
    bool RefreshCachedDirector();
    void InterruptibleSleep(uint32_t ms) const;

    std::atomic<GetCamDirectorFromPool> get_cam_director_{nullptr};
    std::atomic<uintptr_t> cached_director_{0};
    std::atomic<bool> init_attempted_{false};
    std::atomic<bool> resolve_started_{false};
    std::atomic<bool> resolve_stop_{false};
    std::thread resolve_thread_;
    std::shared_ptr<LifetimeToken> lifetime_token_ = std::make_shared<LifetimeToken>();
};

} // namespace Game
} // namespace OVRInject
