# ADR-0003: Online detection and kill-switch design

Status: accepted (2026-07-24)

## Context

Hard Constraint 1: the framework must detect GTA Online sessions and
hard-disable itself — modding online is cheating, gets users banned, and is
permanently out of scope. The salvaged codebase has *zero* online detection
while performing per-frame memory writes — an active ban risk today.

Signals available:

- **BattlEye posture.** Since Sept 2024 GTA Online requires BattlEye; Rockstar
  provides an official launcher toggle that disables it for Story Mode (Online
  then refuses to start). If BattlEye components are active in/around the game
  process, the environment is not the sanctioned story-mode posture.
- **Network traffic.** LukeRoss's universal mod hooks `send/recv/WSASend/
  WSARecv` (decompiled strings, in-repo) — proven signal location. Story mode
  still talks to Social Club, so endpoints must be classified conservatively.
- **Script state.** ScriptHookV natives (`NETWORK_SESSION_*`) are the cleanest
  signal but add a hard dependency; kept as a later extension.

## Decision

Layered, **fail-closed** `OnlineGuard` (cross-cutting, below plugins):

1. BattlEye detector (module/service/process presence) → disable.
2. Network detector (winsock hooks, conservative endpoint classification) →
   disable only on strong session evidence; marked UNVERIFIED until live-tested.
3. Extension point for a script-state detector (interface only).

Verdict is sticky per process run, polled cheaply (≥1 s cached) from the
Present hook; on disable: pass-through Present, zero camera writes, XR
submission stops, reason logged.

**Non-negotiables:**
- No config key, env var, or build flag weakens or bypasses the guard. None.
- The guard is *detection only* — it never interferes with, patches, or hides
  from BattlEye or any protection system (Hard Constraint 2).
- False-positive philosophy: when uncertain, disable. A story-mode false trip
  costs a restart; a false negative costs a user's account.

## Consequences

- Story mode with BattlEye enabled is blocked even though injection there is
  arguably harmless — accepted: it is the unambiguous signal that the user is
  not in the sanctioned modding posture.
- The network detector's false-positive rate is unknown until measured;
  ship-default conservative, tuned by telemetry in `known-issues.md`.

## Alternatives considered

- ScriptHookV-native-only detection: rejected as sole mechanism — adds a
  dependency that itself breaks on game updates; kept as an extension.
- No network hook (BattlEye-only): rejected — BattlEye-off is necessary for
  story mods but a user could still enter Online through a stale toggle state;
  defense in depth is warranted for a P0 constraint.
