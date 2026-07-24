# OnlineGuard — single-player-only enforcement

Status: implemented (this document describes the design as built).
Module: `OVRInject/Game/OnlineGuard.hpp` / `OnlineGuard.cpp`.

Evidence legend (same convention as `docs/00-feasibility.md`):
- **[repo]** — verified against files in this repository.
- **UNVERIFIED** — not confirmed against a live game build / live GTA Online session.

---

## 1. Hard constraint (read first)

This mod is **single-player-only**. Modding GTA Online gets users banned by
Rockstar. `OnlineGuard` exists to hard-disable the mod the moment anything
suggests an online environment:

- The disable is **sticky** — once fired it stays fired for the rest of the
  process lifetime (`std::atomic<bool>`; see `RequestDisable`).
- The disable is **fail-closed** — ANY "online" verdict from ANY detector
  layer disables the mod. Detectors vote "online" or "no opinion"; there is
  no "definitely story mode" override.
- There is **NO config option, env var, registry value, or API to bypass or
  re-enable**. This is deliberate. Do not add one. A user who wants the mod
  back must fix the underlying condition (e.g. leave the online session) and
  restart the game.
- The guard **never touches BattlEye itself** — no unloading, no patching, no
  interference. It only observes and disables our own mod.

## 2. API

```cpp
namespace OVRInject::Game {
class OnlineGuard {
public:
    static OnlineGuard& Get();                 // process-wide singleton
    void Initialize();                         // idempotent, any thread
    bool IsOnlineSession();                    // poll-based, cached >= 1s
    bool ShouldDisableMod();                   // final gate for the render path
    void RequestDisable(const char* reason);   // sticky, thread-safe, logged
    bool IsDisabled();                         // sticky flag only
    std::string GetDisableReason() const;
    // extension point (c), see §3.4
    class IScriptStateDetector { public: virtual bool IsOnlineSession() = 0; ... };
    void SetScriptStateDetector(IScriptStateDetector*);
};
}
```

`IsOnlineSession()` / `ShouldDisableMod()` are cheap enough to call every
frame: expensive checks run at most once per second
(`kPollIntervalMs = 1000`), the cached atomic verdict is returned in between.

## 3. Detector layers

### 3.1 (a) BattlEye anti-cheat presence

Rockstar only sanctions story-mode modding with BattlEye toggled **off** in
the Rockstar Games Launcher. If BattlEye is active we treat the session as
online-risk and disable. Three observations, any one fires:

1. **Loaded modules** — `EnumProcessModules` + `GetModuleFileNameExW`;
   the module file name is matched (case-insensitive) against `BEClient`,
   `BEService`, `BEDA` (covers `BEClient_x64.dll`, `BEDaisy`/`BEDA*`).
2. **Windows service** — `OpenSCManager`/`OpenServiceW(L"BEService")` +
   `QueryServiceStatus`; fires on `SERVICE_RUNNING` or `SERVICE_START_PENDING`.
3. **Process** — Toolhelp32 snapshot; any process whose exe name contains
   `beservice` (covers `BEService.exe`, `BEService_fn.exe`).

Re-checked on every 1 s poll (BE state can change while the game runs).
Inability to perform a check (e.g. SCM access denied) yields "no opinion",
never a false positive.

### 3.2 (b)(i) Online-only command-line parameters

`GetCommandLineW` is scanned once (the command line is immutable) for
online-only launch parameters:

- `StraightIntoFreemode` (boots directly into GTA Online freemode),
- `scOnlineOnly`,
- an exact `-online` token.

The match list is deliberately conservative: known story/offline switches
(e.g. `-scOfflineOnly`) do not collide with any of these strings.

### 3.3 (b)(ii) Network endpoint heuristic — **UNVERIFIED, needs live testing**

**This detector has NOT been validated against a live GTA Online session.**
It is engineered to prefer silence over false positives.

Mechanism: MinHook detours on `ws2_32!send/recv/WSASend/WSARecv` (resolved via
`GetProcAddress`; `ws2_32.lib` is not linked). Per call the socket is
classified (`getsockopt(SO_TYPE)` + `getpeername`, cached per socket for 10 s)
and only **connected, non-localhost UDP** endpoints are counted — Social Club
and story telemetry are TCP/HTTPS and never enter the table. Unconnected UDP
(`sendto`/`recvfrom` style) fails `getpeername` and is *not* tracked; that is
a documented, conservative blind spot.

Verdict (evaluated on the 1 s poll, 60 s sliding window):

- **weak suspect**: ≥ 3 distinct endpoints, sustained for ≥ 30 s;
- **verdict requires**: weak suspect **AND** (the BattlEye detector **also
  fired** OR a **strong pattern**: ≥ 10 distinct endpoints — session play
  fans out to many peers, telemetry does not).

Thresholds (`kMinDistinctEndpoints`, `kStrongDistinctEndpoints`,
`kEndpointMinSpanMs`, `kEndpointWindowMs`) are `constexpr` in
`OnlineGuard.cpp` and marked UNVERIFIED. If hook installation fails the
detector simply contributes nothing (logged).

### 3.4 (c) Script-state detector — extension point (interface only)

A future ScriptHookV-based plugin can implement
`OnlineGuard::IScriptStateDetector` and register it via
`SetScriptStateDetector()`. The implementation would query
`NETWORK::NETWORK_SESSION_IS_ACTIVE` / session-state natives from a script
fiber and return `true` for online sessions. OnlineGuard has **no**
ScriptHookV dependency; the pointer is not owned (pass `nullptr` to
unregister). Polled last in `RunDetectors()`.

## 4. Integration — Present hook (owner: D3DHook workstream)

The guard is **not** wired into the Present hook by this change (that file is
owned by another workstream). The integration is exactly three lines at the
top of the detoured `Present`:

```cpp
if (OVRInject::Game::OnlineGuard::Get().ShouldDisableMod()) {
    return originalPresent(pSwapChain, SyncInterval, PresentFlags); // pass through, skip ALL camera writes
}
```

- `originalPresent` = whatever the D3D hook already calls for unmodified
  frames. Returning it directly leaves the game 100 % vanilla for the frame.
- Place the check before any camera/FOV work; `ShouldDisableMod()` is
  nanoseconds on a cache hit, so calling it every frame is fine.
- Call `OnlineGuard::Get().Initialize()` once during DLL startup (e.g. where
  the other hooks are installed). It is idempotent, so a defensive second
  call is harmless. (`GtaCameraHook::Hook()` already calls it as well.)
- Defense-in-depth already in place inside `OVRInject/Game/`:
  `GtaCameraHook::Update` and `GtaCameraFov::Update` early-out on
  `ShouldDisableMod()`, so game-memory writes stop even before this Present
  wiring lands. The Present pass-through is still required so the render
  path itself goes fully vanilla.

## 5. Version-pinned manifest (`manifests/gtav_legacy.ini`)

All AOB patterns and offsets now live in the manifest, not in source
(`BuildManifest.cpp` loads it; search order: `GTAVR_SETTINGS_DIR`, game exe
dir, this DLL's dir). Build detection: `GTA5.exe` FileVersion via
`GetFileVersionInfo` → `[bNNNN]` section match (exact `version=` pin or
`matchModule=` also supported). Unknown build → one clear log line naming the
detected version/image size and stating the build is unsupported; every
getter returns false and the camera/FOV resolvers fail cleanly — there is no
fallback to hardcoded patterns. `gtavr_camera.ini` user overrides still take
precedence over the manifest for the camera matrix (unchanged legacy
behavior).

The full-process camera-metadata sweep in `GtaCameraHook` is throttled to at
most once per `[detect] metadataSweepIntervalSec` seconds (default 5), and
its raw memory derefs are SEH-guarded against the query→read free race.
