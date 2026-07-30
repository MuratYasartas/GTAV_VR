// GTAVRBridge.asi - ScriptHookV bridge plugin for the GTAVR VR mod.
//
// Loaded by the ASI loader (dinput8.dll) from the game dir, which also loads
// ScriptHookV.dll through the import chain - so ScriptHookV initializes
// properly (direct LoadLibrary injection never completes init). This plugin
// owns a script thread on the game's main thread and exposes a named
// shared-memory channel ("GTAVR_SHV_BRIDGE") that OVRInject.dll uses to:
//   - read a continuously updated camera snapshot (coord/rot/fov/relH/relP),
//   - queue native control ops (yaw/pitch, scripted camera control).
//
// Keep it dependency-free (CRT + Windows only) - the 2017 NativeTrainer.asi
// killed game boots on this build, so this plugin stays minimal and careful.

#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <atomic>

#include "main.h"
#include "../OVRInject/Game/ShvBridgeShared.hpp"

#define GTAVR_BRIDGE_MAPPING "GTAVR_SHV_BRIDGE"

using OVRInject::Game::ShvBridge::BridgeOp;
using OVRInject::Game::ShvBridge::BridgeState;
using namespace OVRInject::Game::ShvBridge;

static std::atomic<bool> g_stop{false};
static FILE* g_log = nullptr;

static void Log(const char* fmt, ...) {
    if (!g_log) {
        char tmp[MAX_PATH];
        DWORD len = GetTempPathA(MAX_PATH, tmp);
        if (len > 0 && len < MAX_PATH) {
            strncat_s(tmp, "gtavrBridgeLog.txt", _TRUNCATE);
            fopen_s(&g_log, tmp, "a");
        }
        if (!g_log) return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fflush(g_log);
}

// Native hashes (ThirdParty/scripthook/inc/natives.h, Alexander Blade SDK)
static constexpr uint64_t H_GET_GAMEPLAY_CAM_COORD = 0x14D6F5678D8F1B37;
static constexpr uint64_t H_GET_GAMEPLAY_CAM_ROT = 0x837765A25378F0BB;
static constexpr uint64_t H_GET_GAMEPLAY_CAM_FOV = 0x65019750A0324133;
static constexpr uint64_t H_GET_GAMEPLAY_CAM_REL_HEADING = 0x743607648ADD4587;
static constexpr uint64_t H_GET_GAMEPLAY_CAM_REL_PITCH = 0x3A6867B4845BEDA2;
static constexpr uint64_t H_SET_GAMEPLAY_CAM_REL_HEADING = 0xB4EC2312F4E5B1F1;
static constexpr uint64_t H_SET_GAMEPLAY_CAM_REL_PITCH = 0x6D0858B8EDFD2B7D;
static constexpr uint64_t H_SET_GAMEPLAY_CAM_RAW_YAW = 0x103991D4A307D472;
static constexpr uint64_t H_SET_GAMEPLAY_CAM_RAW_PITCH = 0x759E13EBC1C15C5A;
static constexpr uint64_t H_CREATE_CAM = 0xC3981DCE61D9E13F;
static constexpr uint64_t H_SET_CAM_ACTIVE = 0x026FB97D0A425F84;
static constexpr uint64_t H_SET_CAM_COORD = 0x4D41783FB745E42E;
static constexpr uint64_t H_SET_CAM_ROT = 0x85973643155D0B07;
static constexpr uint64_t H_SET_CAM_FOV = 0xB13C14F66A00D047;
static constexpr uint64_t H_RENDER_SCRIPT_CAMS = 0x07E5B515DB0636FC;
static constexpr uint64_t H_DESTROY_CAM = 0x865908C81A2C22E9;
static constexpr uint64_t H_PLAYER_PED_ID = 0xD80958FC74E988A6;
static constexpr uint64_t H_GET_PED_BONE_COORDS = 0x17C07FC640E86B4E;

static inline void PushFloat(float v) {
    uint64_t bits = 0;
    *reinterpret_cast<float*>(&bits) = v;
    nativePush64(bits);
}

static inline float CallFloat(uint64_t hash) {
    nativeInit(hash);
    uint64_t* r = nativeCall();
    return r ? *reinterpret_cast<float*>(r) : 0.0f;
}

static void UpdateSnapshot(BridgeState* s) {
    // Cross-process seqlock: odd while the payload is being written, even
    // when stable.  The injected client retries instead of consuming a mix
    // of two game ticks (a source of visible camera jitter).
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&s->stateSeq));
    MemoryBarrier();
    // RAGE native Vector3 returns use an 8-byte stride per component
    // (x@0, y@8, z@16) - confirmed live: a contiguous float3 read yielded
    // y==0.0 and z==realY (GTAVR session 2026-07-26).
    nativeInit(H_GET_GAMEPLAY_CAM_COORD);
    uint64_t* rc = nativeCall();
    if (rc) {
        float* f = reinterpret_cast<float*>(rc);
        s->coord[0] = f[0];
        s->coord[1] = f[2];
        s->coord[2] = f[4];
    }
    // NOTE: GTA's euler convention is rotation order 2 (ZXY) - the same
    // order SET_CAM_ROT uses below. Reading with any other order
    // mis-decomposes the base whenever pitch and yaw are both non-zero
    // (the "image sits sideways" bug found in the 2026-07-27 audit).
    nativeInit(H_GET_GAMEPLAY_CAM_ROT);
    nativePush64(2);
    uint64_t* rr = nativeCall();
    if (rr) {
        float* f = reinterpret_cast<float*>(rr);
        s->rot[0] = f[0];
        s->rot[1] = f[2];
        s->rot[2] = f[4];
    }
    s->fov = CallFloat(H_GET_GAMEPLAY_CAM_FOV);
    s->relHeading = CallFloat(H_GET_GAMEPLAY_CAM_REL_HEADING);
    s->relPitch = CallFloat(H_GET_GAMEPLAY_CAM_REL_PITCH);

    // Player head-bone world position (camera anchor for VR): keeps the
    // character centered and pivots at the right point when orbiting,
    // instead of the gameplay cam's offset anchor (the "character off-center
    // / view from the side" report).
    {
        s->pedHead[0] = s->pedHead[1] = s->pedHead[2] = 0.0f;
        nativeInit(H_PLAYER_PED_ID);
        uint64_t* rp = nativeCall();
        if (rp) {
            const uint64_t ped = *rp;
            nativeInit(H_GET_PED_BONE_COORDS);
            nativePush64(ped);
            nativePush64(0x796E);  // SKEL_Head
            PushFloat(0.0f); PushFloat(0.0f); PushFloat(0.0f);
            uint64_t* rh = nativeCall();
            if (rh) {
                float* f = reinterpret_cast<float*>(rh);
                s->pedHead[0] = f[0];  // Vector3: x@0, y@8, z@16 (SDK types.h)
                s->pedHead[1] = f[2];
                s->pedHead[2] = f[4];
            }
        }
    }
    MemoryBarrier();
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&s->stateSeq));
}

static void ExecuteOp(BridgeState* s, const BridgeOp& op) {
    switch (op.type) {
    case OpSetRawYawPitch:
        nativeInit(H_SET_GAMEPLAY_CAM_RAW_YAW);
        PushFloat(op.a);
        nativeCall();
        nativeInit(H_SET_GAMEPLAY_CAM_RAW_PITCH);
        PushFloat(op.b);
        nativeCall();
        break;
    case OpSetRelativeHeadingPitch:
        nativeInit(H_SET_GAMEPLAY_CAM_REL_HEADING);
        PushFloat(op.a);
        nativeCall();
        nativeInit(H_SET_GAMEPLAY_CAM_REL_PITCH);
        PushFloat(op.b);
        PushFloat(op.c);
        nativeCall();
        break;
    case OpCamCreate:
        nativeInit(H_CREATE_CAM);
        nativePush64(reinterpret_cast<uint64_t>("DEFAULT_SCRIPTED_CAMERA"));
        nativePush64(0);
        if (uint64_t* result = nativeCall()) {
            InterlockedExchange(
                reinterpret_cast<volatile LONG*>(&s->lastCreateResult),
                static_cast<LONG>(*result));
            Log("CREATE_CAM -> %d\n", s->lastCreateResult);
        } else {
            Log("CREATE_CAM failed: nativeCall returned null\n");
        }
        break;
    case OpCamSetCoord:
        nativeInit(H_SET_CAM_COORD);
        nativePush64(static_cast<uint64_t>(op.cam));
        PushFloat(op.a);
        PushFloat(op.b);
        PushFloat(op.c);
        nativeCall();
        break;
    case OpCamSetRot:
        nativeInit(H_SET_CAM_ROT);
        nativePush64(static_cast<uint64_t>(op.cam));
        PushFloat(op.a);
        PushFloat(op.b);
        PushFloat(op.c);
        nativePush64(2);
        nativeCall();
        break;
    case OpCamSetFov:
        nativeInit(H_SET_CAM_FOV);
        nativePush64(static_cast<uint64_t>(op.cam));
        PushFloat(op.a);
        nativeCall();
        break;
    case OpCamSetActive:
        nativeInit(H_SET_CAM_ACTIVE);
        nativePush64(static_cast<uint64_t>(op.cam));
        nativePush64(op.a != 0.0f ? 1 : 0);
        nativeCall();
        break;
    case OpCamRender:
        nativeInit(H_RENDER_SCRIPT_CAMS);
        nativePush64(op.a != 0.0f ? 1 : 0);
        nativePush64(0);
        nativePush64(0);
        nativePush64(1);
        nativePush64(1);
        nativeCall();
        break;
    case OpCamDestroy:
        nativeInit(H_DESTROY_CAM);
        nativePush64(static_cast<uint64_t>(op.cam));
        nativePush64(1);
        nativeCall();
        break;
    default:
        break;
    }
}

static void ScriptMain() {
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        0, sizeof(BridgeState), GTAVR_BRIDGE_MAPPING);
    if (!mapping) {
        Log("CreateFileMapping failed %lu\n", GetLastError());
        return;
    }
    BridgeState* s = static_cast<BridgeState*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BridgeState)));
    if (!s) {
        Log("MapViewOfFile failed %lu\n", GetLastError());
        CloseHandle(mapping);
        return;
    }
    ZeroMemory(s, sizeof(*s));
    s->magic = kMagic;
    StoreRelease(&s->bridgeAlive, 1);
    Log("GTAVRBridge: script thread up, channel ready\n");

    uint32_t logTick = 0;
    while (!g_stop.load()) {
        UpdateSnapshot(s);

        BridgeOp operation = {};
        while (TryPop(s, operation)) {
            ExecuteOp(s, operation);
        }

        if (++logTick % 240 == 1) {
            Log("cam coord=(%.1f, %.1f, %.1f) rot=(%.1f, %.1f, %.1f) fov=%.1f relH=%.2f relP=%.2f\n",
                s->coord[0], s->coord[1], s->coord[2], s->rot[0], s->rot[1], s->rot[2],
                s->fov, s->relHeading, s->relPitch);
        }
        WAIT(0);
    }

    StoreRelease(&s->bridgeAlive, 0);
    UnmapViewOfFile(s);
    CloseHandle(mapping);
    Log("GTAVRBridge: stopped\n");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        scriptRegister(hModule, &ScriptMain);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_stop.store(true);
        scriptUnregister(hModule);
    }
    return TRUE;
}
