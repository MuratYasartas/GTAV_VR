#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace OVRInject {
namespace Game {

/**
 * OnlineGuard - HARD-DISABLES the mod when a GTA Online environment is detected.
 *
 * Single-player-only is a non-negotiable constraint: modding GTA Online gets
 * users banned. This guard is deliberately fail-closed - ANY "online" verdict
 * from ANY detector layer makes the disable sticky for the rest of the
 * process lifetime. There is NO config option, env var, or API to re-enable
 * the mod afterwards; that is a hard constraint (see docs/online-guard.md).
 *
 * Detector layers (each documented in docs/online-guard.md):
 *   a) BattlEye anti-cheat presence (loaded BE modules + BEService service/process).
 *      Story-mode modding is only sanctioned by Rockstar with BattlEye toggled
 *      OFF in the Rockstar Games Launcher; if BE is active we treat the
 *      session as online-risk and disable. We never touch BE itself.
 *   b) GTA Online session signals:
 *      (i)  command line of GTA5.exe for online-only parameters.
 *      (ii) UNVERIFIED/needs-live-testing: MinHook hooks on ws2_32
 *           send/recv/WSASend/WSARecv counting distinct non-localhost UDP
 *           endpoints; only flags when the BattlEye detector ALSO fired or
 *           the endpoint pattern strongly suggests session play.
 *   c) Extension point for a future ScriptHookV script-state detector
 *      (NETWORK_SESSION_* natives) - interface only, no ScriptHookV dependency.
 *
 * Usage is poll-based and cheap enough to call every frame: expensive checks
 * are cached on a >= 1 second timer.
 */
class OnlineGuard {
public:
    OnlineGuard() = default;

    // Process-wide singleton (the intended way to reach the guard).
    static OnlineGuard& Get();

    // Idempotent. Installs the network-signal hooks and evaluates the
    // one-shot detectors. Safe to call from any thread.
    void Initialize();

    // Poll-based online verdict. Runs detectors at most once per second and
    // returns the cached verdict in between. Any online verdict also makes
    // the disable sticky via RequestDisable.
    bool IsOnlineSession();

    // Final gate for the render path: true when the mod must pass through
    // untouched (sticky disable OR fresh online verdict).
    bool ShouldDisableMod();

    // Sticky, thread-safe. Logs the reason the first time it fires.
    void RequestDisable(const char* reason);

    // True once RequestDisable (or any detector) has fired.
    bool IsDisabled();

    // Reason recorded by the first RequestDisable call ("" while enabled).
    std::string GetDisableReason() const;

    /**
     * Extension point (c) - future ScriptHookV script-state detector.
     *
     * Implement this interface from a ScriptHookV plugin thread and register
     * it with SetScriptStateDetector. The implementation should query
     * NETWORK::NETWORK_SESSION_IS_ACTIVE / NETWORK::NETWORK_IS_IN_SESSION
     * style natives and return true for online sessions. OnlineGuard itself
     * has NO dependency on ScriptHookV; the pointer is not owned and must
     * outlive the registration (pass nullptr to unregister).
     */
    class IScriptStateDetector {
    public:
        virtual ~IScriptStateDetector() = default;
        virtual bool IsOnlineSession() = 0;
    };
    void SetScriptStateDetector(IScriptStateDetector* detector);

private:
    bool RunDetectors();
    bool CheckBattlEyeActive(std::string& outDetail);
    bool CheckCommandLineOnline(std::string& outDetail);
    bool CheckNetworkSuspect(bool battlEyeActive, std::string& outDetail);

    static constexpr uint64_t kPollIntervalMs = 1000; // >= 1s cache per spec

    std::atomic<bool> disabled_{false};
    std::atomic<bool> cached_online_{false};
    std::atomic<uint64_t> last_poll_tick_{0};
    std::atomic<bool> init_attempted_{false};

    mutable std::mutex mutex_;          // guards disable_reason_ + script_detector_
    std::string disable_reason_;
    IScriptStateDetector* script_detector_ = nullptr;
};

} // namespace Game
} // namespace OVRInject
