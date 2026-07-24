// OnlineGuard.cpp - see OnlineGuard.hpp and docs/online-guard.md for design.
//
// NOTE: winsock2.h MUST be included before any Windows.h header. This file is
// compiled via unity include at the very top of GtaCameraHook.cpp (it is not
// yet registered in OVRInject.vcxproj, which is owned by another workstream),
// which keeps this ordering intact for the whole translation unit.

#include "OnlineGuard.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>

#include <MinHook.h>

#include "../Log.hpp"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <unordered_map>
#include <vector>

namespace OVRInject {
namespace Game {

namespace {

// ---------------------------------------------------------------------------
// Network detector state (detector b.ii) - process-global because the
// MinHook detours are process-global. UNVERIFIED / needs-live-testing: the
// endpoint heuristic below has NOT been validated against a live GTA Online
// session. Thresholds are deliberately conservative so Social Club / story
// telemetry cannot trip them:
//   - only UDP sockets are considered (Social Club & telemetry are TCP/HTTPS),
//   - only connected sockets (getpeername succeeds), localhost excluded,
//   - a verdict additionally requires either the BattlEye detector to have
//     fired as well, or a "strong" endpoint pattern (many distinct peers).
// ---------------------------------------------------------------------------

constexpr uint64_t kEndpointWindowMs = 60000;    // sliding window for distinct endpoints
constexpr uint64_t kEndpointMinSpanMs = 30000;   // traffic must be sustained this long
constexpr size_t kMinDistinctEndpoints = 3;      // weak suspect threshold (UNVERIFIED)
constexpr size_t kStrongDistinctEndpoints = 10;  // stands alone without BE (UNVERIFIED)
constexpr uint64_t kSocketCacheMs = 10000;       // per-socket reclassification interval

struct EndpointRecord {
    uint64_t key = 0;
    uint64_t firstSeenTick = 0;
    uint64_t lastSeenTick = 0;
    uint32_t packets = 0;
};

struct NetworkDetectorState {
    std::mutex mutex;
    bool hookInstallAttempted = false;
    bool hooksActive = false;
    uint64_t totalUdpPackets = 0;
    std::vector<EndpointRecord> endpoints;
    // SOCKET -> (endpoint key, classify tick); key 0 = not a tracked UDP endpoint
    std::unordered_map<SOCKET, std::pair<uint64_t, uint64_t>> socketCache;
};

NetworkDetectorState g_net;

// Dynamically resolved ws2_32 entry points. ws2_32.lib is not in OVRInject's
// linker dependencies (and the project file is owned by another workstream),
// so every Winsock function used here is resolved with GetProcAddress.
using SendFn = int (WSAAPI*)(SOCKET, const char*, int, int);
using RecvFn = int (WSAAPI*)(SOCKET, char*, int, int);
using WSASendFn = int (WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD,
                               LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
using WSARecvFn = int (WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD,
                               LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
using GetPeerNameFn = int (WSAAPI*)(SOCKET, sockaddr*, int*);
using GetSockOptFn = int (WSAAPI*)(SOCKET, int, int, char*, int*);

SendFn g_origSend = nullptr;
RecvFn g_origRecv = nullptr;
WSASendFn g_origWSASend = nullptr;
WSARecvFn g_origWSARecv = nullptr;
GetPeerNameFn g_getpeername = nullptr;
GetSockOptFn g_getsockopt = nullptr;

uint64_t HashEndpoint(const sockaddr_storage& addr) {
    // FNV-1a over family + address (+ port) bytes. Collisions only merge two
    // endpoints into one, which keeps the heuristic conservative.
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&addr);
    size_t len = (addr.ss_family == AF_INET6) ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

bool IsLocalhostEndpoint(const sockaddr_storage& addr) {
    if (addr.ss_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(&addr);
        // 127.0.0.0/8
        return in->sin_addr.S_un.S_un_b.s_b1 == 127;
    }
    if (addr.ss_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        const uint8_t* b = in6->sin6_addr.u.Byte;
        for (int i = 0; i < 15; ++i) {
            if (b[i] != 0) return false;
        }
        return b[15] == 1; // ::1
    }
    return true; // unknown family: do not track (conservative)
}

// Returns a stable endpoint key for connected, non-localhost UDP sockets;
// 0 for anything we deliberately do not track. Cached per socket because this
// runs on the game's network hot path.
uint64_t ClassifySocket(SOCKET s) {
    if (!g_getpeername || !g_getsockopt) {
        return 0;
    }

    uint64_t now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_net.mutex);
        auto it = g_net.socketCache.find(s);
        if (it != g_net.socketCache.end() && now - it->second.second < kSocketCacheMs) {
            return it->second.first;
        }
    }

    uint64_t key = 0;
    int type = 0;
    int optLen = sizeof(type);
    if (g_getsockopt(s, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &optLen) == 0 &&
        type == SOCK_DGRAM) {
        sockaddr_storage addr = {};
        int addrLen = sizeof(addr);
        // Unconnected UDP (sendto/recvfrom style) fails getpeername and is
        // NOT tracked - a documented, conservative blind spot of this
        // UNVERIFIED detector (we prefer missing traffic over misclassifying).
        if (g_getpeername(s, reinterpret_cast<sockaddr*>(&addr), &addrLen) == 0 &&
            !IsLocalhostEndpoint(addr)) {
            key = HashEndpoint(addr);
        }
    }

    std::lock_guard<std::mutex> lock(g_net.mutex);
    g_net.socketCache[s] = {key, now};
    return key;
}

void RecordSocketTraffic(SOCKET s, int bytes) {
    if (bytes <= 0) {
        return;
    }
    uint64_t key = ClassifySocket(s);
    if (key == 0) {
        return;
    }

    uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_net.mutex);
    g_net.totalUdpPackets++;
    for (EndpointRecord& record : g_net.endpoints) {
        if (record.key == key) {
            record.lastSeenTick = now;
            record.packets++;
            return;
        }
    }
    EndpointRecord record;
    record.key = key;
    record.firstSeenTick = now;
    record.lastSeenTick = now;
    record.packets = 1;
    g_net.endpoints.push_back(record);
}

int WSAAPI Detour_send(SOCKET s, const char* buf, int len, int flags) {
    int ret = g_origSend(s, buf, len, flags);
    RecordSocketTraffic(s, ret);
    return ret;
}

int WSAAPI Detour_recv(SOCKET s, char* buf, int len, int flags) {
    int ret = g_origRecv(s, buf, len, flags);
    RecordSocketTraffic(s, ret);
    return ret;
}

int WSAAPI Detour_WSASend(SOCKET s, LPWSABUF buffers, DWORD bufferCount, LPDWORD bytesSent,
                          DWORD flags, LPWSAOVERLAPPED overlapped,
                          LPWSAOVERLAPPED_COMPLETION_ROUTINE completionRoutine) {
    int ret = g_origWSASend(s, buffers, bufferCount, bytesSent, flags, overlapped, completionRoutine);
    RecordSocketTraffic(s, ret == 0 ? 1 : 0);
    return ret;
}

int WSAAPI Detour_WSARecv(SOCKET s, LPWSABUF buffers, DWORD bufferCount, LPDWORD bytesReceived,
                          LPDWORD flags, LPWSAOVERLAPPED overlapped,
                          LPWSAOVERLAPPED_COMPLETION_ROUTINE completionRoutine) {
    int ret = g_origWSARecv(s, buffers, bufferCount, bytesReceived, flags, overlapped, completionRoutine);
    RecordSocketTraffic(s, ret == 0 ? 1 : 0);
    return ret;
}

void InstallNetworkHooks() {
    std::lock_guard<std::mutex> lock(g_net.mutex);
    if (g_net.hookInstallAttempted) {
        return;
    }
    g_net.hookInstallAttempted = true;

    HMODULE ws2 = GetModuleHandleW(L"ws2_32.dll");
    if (!ws2) {
        ws2 = LoadLibraryW(L"ws2_32.dll"); // system DLL the game uses anyway
    }
    if (!ws2) {
        LOGSTR("OnlineGuard: ws2_32.dll unavailable - network signal detector disabled\n");
        return;
    }

    g_getpeername = reinterpret_cast<GetPeerNameFn>(GetProcAddress(ws2, "getpeername"));
    g_getsockopt = reinterpret_cast<GetSockOptFn>(GetProcAddress(ws2, "getsockopt"));

    MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        LOGSTRF("OnlineGuard: MH_Initialize failed (%d) - network signal detector disabled\n", init);
        return;
    }

    struct HookSpec {
        const char* name;
        LPVOID detour;
        LPVOID* original;
        LPVOID target;
    };
    HookSpec specs[] = {
        {"send", reinterpret_cast<LPVOID>(&Detour_send), reinterpret_cast<LPVOID*>(&g_origSend), nullptr},
        {"recv", reinterpret_cast<LPVOID>(&Detour_recv), reinterpret_cast<LPVOID*>(&g_origRecv), nullptr},
        {"WSASend", reinterpret_cast<LPVOID>(&Detour_WSASend), reinterpret_cast<LPVOID*>(&g_origWSASend), nullptr},
        {"WSARecv", reinterpret_cast<LPVOID>(&Detour_WSARecv), reinterpret_cast<LPVOID*>(&g_origWSARecv), nullptr},
    };

    int installed = 0;
    for (HookSpec& spec : specs) {
        spec.target = GetProcAddress(ws2, spec.name);
        if (!spec.target) {
            continue;
        }
        if (MH_CreateHook(spec.target, spec.detour, spec.original) != MH_OK) {
            LOGSTRF("OnlineGuard: MH_CreateHook(%s) failed\n", spec.name);
            continue;
        }
        if (MH_EnableHook(spec.target) != MH_OK) {
            LOGSTRF("OnlineGuard: MH_EnableHook(%s) failed\n", spec.name);
            MH_RemoveHook(spec.target);
            continue;
        }
        installed++;
    }

    g_net.hooksActive = (installed > 0);
    LOGSTRF("OnlineGuard: network signal detector %s (%d/4 ws2_32 hooks) "
            "[UNVERIFIED heuristic - needs live testing]\n",
            g_net.hooksActive ? "active" : "inactive", installed);
}

bool ModuleNameContains(const wchar_t* path, const wchar_t* needleLower) {
    std::wstring name(path ? path : L"");
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        name = name.substr(slash + 1);
    }
    for (wchar_t& c : name) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return name.find(needleLower) != std::wstring::npos;
}

} // namespace

OnlineGuard& OnlineGuard::Get() {
    static OnlineGuard s_instance;
    return s_instance;
}

void OnlineGuard::Initialize() {
    bool expected = false;
    if (!init_attempted_.compare_exchange_strong(expected, true)) {
        return; // already initialized (or being initialized on another thread)
    }

    LOGSTR("OnlineGuard: initializing (single-player-only enforcement, no bypass)\n");

    InstallNetworkHooks();

    // One-shot detector: command line never changes during process lifetime.
    std::string detail;
    if (CheckCommandLineOnline(detail)) {
        RequestDisable(detail.c_str());
    }

    // Prime the BattlEye check so an already-active BE trips immediately
    // instead of on the first polled frame.
    if (CheckBattlEyeActive(detail)) {
        RequestDisable(detail.c_str());
    }
}

bool OnlineGuard::IsOnlineSession() {
    if (disabled_.load()) {
        return true;
    }

    uint64_t now = GetTickCount64();
    uint64_t last = last_poll_tick_.load();
    if (last != 0 && now - last < kPollIntervalMs) {
        return cached_online_.load();
    }
    last_poll_tick_.store(now); // benign race: a duplicate poll within 1s is harmless

    bool online = RunDetectors();
    cached_online_.store(online);
    return online || disabled_.load();
}

bool OnlineGuard::ShouldDisableMod() {
    return IsDisabled() || IsOnlineSession();
}

void OnlineGuard::RequestDisable(const char* reason) {
    bool was = disabled_.exchange(true);
    if (was) {
        return; // already disabled; keep the first reason
    }
    cached_online_.store(true);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        disable_reason_ = reason ? reason : "unspecified";
    }
    LOGSTRF("OnlineGuard: *** MOD HARD-DISABLED *** reason: %s\n",
            reason ? reason : "unspecified");
    LOGSTR("OnlineGuard: this cannot be re-enabled at runtime (single-player-only hard constraint)\n");
}

bool OnlineGuard::IsDisabled() {
    return disabled_.load();
}

std::string OnlineGuard::GetDisableReason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return disable_reason_;
}

void OnlineGuard::SetScriptStateDetector(IScriptStateDetector* detector) {
    std::lock_guard<std::mutex> lock(mutex_);
    script_detector_ = detector;
    LOGSTRF("OnlineGuard: script-state detector %s\n", detector ? "registered" : "unregistered");
}

bool OnlineGuard::RunDetectors() {
    std::string detail;

    // (a) BattlEye presence -> online-risk, disable immediately.
    bool battlEyeActive = CheckBattlEyeActive(detail);
    if (battlEyeActive) {
        RequestDisable(detail.c_str());
        return true;
    }

    // (b)(i) command line - one-shot, but cheap enough to re-check.
    if (CheckCommandLineOnline(detail)) {
        RequestDisable(detail.c_str());
        return true;
    }

    // (b)(ii) UNVERIFIED network endpoint heuristic.
    if (CheckNetworkSuspect(battlEyeActive, detail)) {
        RequestDisable(detail.c_str());
        return true;
    }

    // (c) optional ScriptHookV script-state detector (extension point).
    IScriptStateDetector* detector = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        detector = script_detector_;
    }
    if (detector && detector->IsOnlineSession()) {
        RequestDisable("script-state detector reported a GTA Online session");
        return true;
    }

    return false;
}

bool OnlineGuard::CheckBattlEyeActive(std::string& outDetail) {
    // (a) part 1: BattlEye client modules loaded in this process.
    // GTA V loads BEClient_*.dll / BEDA*.dll when BattlEye is enabled.
    HMODULE modules[1024] = {};
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        DWORD count = (std::min)(static_cast<DWORD>(needed / sizeof(HMODULE)),
                                 static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));
        for (DWORD i = 0; i < count; ++i) {
            wchar_t path[MAX_PATH] = {};
            if (!GetModuleFileNameExW(GetCurrentProcess(), modules[i], path, MAX_PATH)) {
                continue;
            }
            if (ModuleNameContains(path, L"beclient") ||
                ModuleNameContains(path, L"beservice") ||
                ModuleNameContains(path, L"beda")) {
                outDetail = "BattlEye module loaded in process: " +
                    std::string("BE component (beclient/beservice/beda)");
                return true;
            }
        }
    }

    // (a) part 2: the BEService Windows service.
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE service = OpenServiceW(scm, L"BEService", SERVICE_QUERY_STATUS);
        if (service) {
            SERVICE_STATUS status = {};
            if (QueryServiceStatus(service, &status) &&
                (status.dwCurrentState == SERVICE_RUNNING ||
                 status.dwCurrentState == SERVICE_START_PENDING)) {
                CloseServiceHandle(service);
                CloseServiceHandle(scm);
                outDetail = "BattlEye BEService Windows service is running";
                return true;
            }
            CloseServiceHandle(service);
        }
        CloseServiceHandle(scm);
    }

    // (a) part 3: BEService process variants (BEService.exe, BEService_fn.exe, ...).
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (ModuleNameContains(entry.szExeFile, L"beservice")) {
                    CloseHandle(snapshot);
                    outDetail = "BattlEye service process is running";
                    return true;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    return false;
}

bool OnlineGuard::CheckCommandLineOnline(std::string& outDetail) {
    // (b)(i): online-only launch parameters. The list itself is conservative:
    // known story/offline switches (-scOfflineOnly etc.) deliberately do not
    // match any of these substrings/tokens.
    static int s_cachedVerdict = -1; // command line is immutable; evaluate once
    if (s_cachedVerdict >= 0) {
        return s_cachedVerdict == 1;
    }

    int verdict = 0;
    std::wstring cmd = GetCommandLineW();
    for (wchar_t& c : cmd) {
        c = static_cast<wchar_t>(towlower(c));
    }

    auto hasToken = [&](const wchar_t* token) {
        size_t tokenLen = wcslen(token);
        size_t pos = 0;
        while ((pos = cmd.find(token, pos)) != std::wstring::npos) {
            bool startOk = (pos == 0) || cmd[pos - 1] == L' ' || cmd[pos - 1] == L'\t';
            size_t end = pos + tokenLen;
            bool endOk = (end >= cmd.size()) || cmd[end] == L' ' || cmd[end] == L'\t';
            if (startOk && endOk) {
                return true;
            }
            pos = end;
        }
        return false;
    };

    if (cmd.find(L"straightintofreemode") != std::wstring::npos ||
        cmd.find(L"sconlineonly") != std::wstring::npos ||
        hasToken(L"-online")) {
        verdict = 1;
    }

    s_cachedVerdict = verdict;
    if (verdict == 1) {
        outDetail = "GTA5.exe command line contains an online-only parameter";
    }
    return verdict == 1;
}

bool OnlineGuard::CheckNetworkSuspect(bool battlEyeActive, std::string& outDetail) {
    // (b)(ii) UNVERIFIED / needs-live-testing. See the design notes at the top
    // of this file. Never fires from traffic alone unless the endpoint pattern
    // is strong; otherwise it only confirms a BattlEye verdict.
    if (!g_net.hooksActive) {
        return false;
    }

    uint64_t now = GetTickCount64();
    size_t distinct = 0;
    uint64_t oldestFirstSeen = 0;
    {
        std::lock_guard<std::mutex> lock(g_net.mutex);
        // Prune endpoints outside the sliding window.
        auto& endpoints = g_net.endpoints;
        endpoints.erase(
            std::remove_if(endpoints.begin(), endpoints.end(),
                           [&](const EndpointRecord& r) {
                               return now - r.lastSeenTick > kEndpointWindowMs;
                           }),
            endpoints.end());
        distinct = endpoints.size();
        for (const EndpointRecord& r : endpoints) {
            if (oldestFirstSeen == 0 || r.firstSeenTick < oldestFirstSeen) {
                oldestFirstSeen = r.firstSeenTick;
            }
        }
    }

    bool strongPattern = distinct >= kStrongDistinctEndpoints;
    bool sustained = distinct >= kMinDistinctEndpoints &&
                     oldestFirstSeen != 0 &&
                     now - oldestFirstSeen >= kEndpointMinSpanMs;

    // Conservative gate: prefer flagging only when BattlEye ALSO fired, or the
    // pattern of endpoints strongly suggests session play.
    if (sustained && (strongPattern || battlEyeActive)) {
        char text[160] = {};
        sprintf_s(text,
                  "network endpoint heuristic suspects an online session "
                  "(%zu distinct non-localhost UDP endpoints, battleye=%d) [UNVERIFIED]",
                  distinct, battlEyeActive ? 1 : 0);
        outDetail = text;
        return true;
    }

    return false;
}

} // namespace Game
} // namespace OVRInject
