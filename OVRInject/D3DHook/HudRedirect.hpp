#pragma once

/**
 * HudRedirect - HUD shader-hash identification + offscreen render redirect
 * (Phase 6 HUD infrastructure).
 *
 * Identification-first design (see docs/hud-postfx.md):
 *   1. A registry of HUD shader bytecode hashes lives in the build manifest
 *      ([hud] section, ADR-0004: hashes are version-pinned data, never code).
 *   2. MinHook detours on ID3D11Device::CreateVertexShader (vtable 12),
 *      CreateGeometryShader (vtable 13) and CreatePixelShader (vtable 15)
 *      hash every shader bytecode the game compiles and record the
 *      hash -> shader-object mapping for registered HUD hashes.
 *      (Vtable indices per d3d11.h: VS=12, GS=13, PS=15. NOTE: an earlier
 *      note said 12/15/16 - that was off; 15 is PS, 16 is CreateHullShader.)
 *   3. Lightweight vtable hooks on ID3D11DeviceContext::VSSetShader (11),
 *      PSSetShader (9) and GSSetShader (23) flag a "HUD pass armed" state per
 *      context when a registered HUD shader object is bound.
 *   4. An OMSetRenderTargets (33) hook substitutes an offscreen HUD render
 *      target whenever an armed context binds the swapchain backbuffer.
 *   5. At Present time the HUD target is alpha-composited onto the produced
 *      eye textures (simple in-place blit; world-locked quad compositing is a
 *      documented follow-up), then cleared for the next frame.
 *
 * Everything here is config-gated ([hud] enabled=1); with the gate off no
 * hooks are installed and the frame path is untouched. The hook targets are
 * the shared d3d11.dll implementations (one function body covers every
 * device/context), so installation happens once, on the first device seen.
 *
 * Hash: FNV-1a-64 over the raw bytecode buffer exactly as passed to
 * Create*Shader (the 3Dmigoto-style "<hex>-<stage>" names in the registry
 * assume the same input; parity with 3Dmigoto hashes is UNVERIFIED until
 * checked against a live dump - see docs/known-issues.md).
 */

#include <Windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <d3dcompiler.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "MinHook.h"
#include "../Log.hpp"

namespace OVRInject {
namespace Hud {

// Shader stage bits used in the registry and the per-context armed mask.
enum ShaderStageBits : uint32_t {
    kStageVS = 1u,
    kStageGS = 2u,
    kStagePS = 4u,
};

struct Registry {
    bool enabled = false;
    std::unordered_map<uint64_t, uint32_t> hashes;  // FNV-1a-64 -> stage bits
    uint32_t entryCount = 0;                        // parsed hash.N entries
};

struct State;

// Forward declarations: definitions live further down, the registry/shader
// helpers below already need them.
inline Registry& GetRegistry();
inline State& GetState();
inline std::mutex& GetStateMutex();

// ---------------------------------------------------------------------------
// Hash + registry parsing
// ---------------------------------------------------------------------------

// FNV-1a 64-bit over the raw shader bytecode.
inline uint64_t ComputeHash(const void* data, size_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 14695981039346656037ull;  // 0xcbf29ce484222325
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;  // 0x100000001b3
    }
    return hash;
}

inline uint32_t StageBitsFromSuffix(const std::string& suffix) {
    if (suffix == "vs") return kStageVS;
    if (suffix == "gs") return kStageGS;
    if (suffix == "ps") return kStagePS;
    return 0;
}

// Parses "<16-hex-chars>-<stage>" (e.g. "9bc8bfebaadd06d7-vs").
inline bool ParseHashEntry(const std::string& text, uint64_t& outHash, uint32_t& outStage) {
    size_t dash = text.find('-');
    std::string hexPart = (dash == std::string::npos) ? text : text.substr(0, dash);
    std::string suffix = (dash == std::string::npos) ? std::string() : text.substr(dash + 1);
    if (hexPart.empty() || hexPart.length() > 16) {
        return false;
    }
    uint64_t hash = 0;
    for (char c : hexPart) {
        unsigned nibble = 0;
        if (c >= '0' && c <= '9') nibble = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = static_cast<unsigned>(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F') nibble = static_cast<unsigned>(c - 'A') + 10;
        else return false;
        hash = (hash << 4) | nibble;
    }
    outHash = hash;
    outStage = StageBitsFromSuffix(suffix);
    return true;
}

inline std::string TrimIni(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.pop_back();
    }
    size_t first = text.find_first_not_of(" \t");
    return (first == std::string::npos) ? std::string() : text.substr(first);
}

// Loads the [hud] section of a manifest INI: enabled, hash.N, source.N.
// Parsed manually (NOT via GetPrivateProfileStringA): the repo manifests are
// LF-only and the Win32 INI API silently finds nothing in them. Idempotent:
// replaces the current registry contents. Safe with an empty/missing path
// (registry ends up disabled).
inline bool LoadRegistry(const std::string& manifestPath) {
    Registry fresh;
    std::unordered_map<std::string, std::string> keys;

    if (!manifestPath.empty()) {
        std::ifstream file(manifestPath);
        if (file.is_open()) {
            bool inHud = false;
            std::string line;
            while (std::getline(file, line)) {
                std::string trimmed = TrimIni(line);
                if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
                    continue;
                }
                if (trimmed[0] == '[') {
                    inHud = (trimmed == "[hud]");
                    continue;
                }
                if (!inHud) {
                    continue;
                }
                size_t eq = trimmed.find('=');
                if (eq == std::string::npos) {
                    continue;
                }
                keys[TrimIni(trimmed.substr(0, eq))] = TrimIni(trimmed.substr(eq + 1));
            }
        }
    }

    const std::string& enabledValue = keys["enabled"];
    fresh.enabled = (enabledValue == "1" || enabledValue == "true");

    for (uint32_t i = 0; i < 64; ++i) {
        char key[32] = {};
        snprintf(key, sizeof(key), "hash.%u", i);
        auto it = keys.find(key);
        if (it == keys.end()) {
            continue;
        }
        uint64_t hash = 0;
        uint32_t stage = 0;
        if (!ParseHashEntry(it->second, hash, stage)) {
            LOGWNDF("HudRedirect: ignoring malformed [hud] entry %s=%s\n", key, it->second.c_str());
            continue;
        }
        fresh.hashes[hash] = stage;
        ++fresh.entryCount;

        snprintf(key, sizeof(key), "source.%u", i);
        auto src = keys.find(key);
        LOGSTRF("HudRedirect: registered HUD hash %016llx (stage 0x%x) - %s\n",
                static_cast<unsigned long long>(hash), stage,
                src != keys.end() ? src->second.c_str() : "(no source)");
    }

    GetRegistry() = fresh;
    LOGSTRF("HudRedirect: registry loaded (%u hashes, %s)\n",
            fresh.entryCount, fresh.enabled ? "ENABLED" : "disabled");
    return fresh.enabled;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct State {
    bool hooksInstalled = false;
    std::atomic<bool> anyHudShaders{false};

    // Shader object -> registry stage bits (only registered HUD shaders are
    // stored). Guarded by GetStateMutex().
    std::unordered_map<void*, uint32_t> hudShaders;
    // Per-context armed stage mask (a HUD shader currently bound). Guarded.
    std::unordered_map<ID3D11DeviceContext*, uint32_t> armed;

    // Current swapchain backbuffer (raw pointer, refreshed every Present; the
    // swapchain owns the reference).
    ID3D11Texture2D* backbuffer = nullptr;
    uint32_t bbWidth = 0;
    uint32_t bbHeight = 0;
    DXGI_FORMAT bbFormat = DXGI_FORMAT_UNKNOWN;
    uint32_t bbSamples = 1;
    bool bbMsaaLogged = false;

    // Offscreen HUD target (created lazily, backbuffer-sized).
    ID3D11Texture2D* hudTex = nullptr;
    ID3D11RenderTargetView* hudRtv = nullptr;
    ID3D11ShaderResourceView* hudSrv = nullptr;
    bool hudTargetFailed = false;

    // Composite resources.
    ID3D11VertexShader* compositeVs = nullptr;
    ID3D11PixelShader* compositePs = nullptr;
    ID3D11BlendState* compositeBlend = nullptr;
    ID3D11DepthStencilState* compositeDepth = nullptr;
    ID3D11RasterizerState* compositeRaster = nullptr;
    ID3D11SamplerState* compositeSampler = nullptr;

    // Set by the OMSetRenderTargets hook when a substitution actually happened
    // this frame; cleared by FinishFrame after the composite + clear.
    bool hudUsedThisFrame = false;

    // Hook targets/originals.
    void* createVsTarget = nullptr;
    void* createGsTarget = nullptr;
    void* createPsTarget = nullptr;
    void* vsSetShaderTarget = nullptr;
    void* psSetShaderTarget = nullptr;
    void* gsSetShaderTarget = nullptr;
    void* omSetRenderTargetsTarget = nullptr;
};

inline State& GetState() {
    static State state;
    return state;
}

inline Registry& GetRegistry() {
    static Registry registry;
    return registry;
}

inline std::mutex& GetStateMutex() {
    static std::mutex mutex;
    return mutex;
}

// ---------------------------------------------------------------------------
// Hooked functions
// ---------------------------------------------------------------------------

typedef HRESULT(__stdcall* CreateVertexShaderFn)(ID3D11Device*, const void*, SIZE_T,
                                                 ID3D11ClassLinkage*, ID3D11VertexShader**);
typedef HRESULT(__stdcall* CreateGeometryShaderFn)(ID3D11Device*, const void*, SIZE_T,
                                                   ID3D11ClassLinkage*, ID3D11GeometryShader**);
typedef HRESULT(__stdcall* CreatePixelShaderFn)(ID3D11Device*, const void*, SIZE_T,
                                                ID3D11ClassLinkage*, ID3D11PixelShader**);
typedef void(__stdcall* VSSetShaderFn)(ID3D11DeviceContext*, ID3D11VertexShader*,
                                       ID3D11ClassInstance* const*, UINT);
typedef void(__stdcall* PSSetShaderFn)(ID3D11DeviceContext*, ID3D11PixelShader*,
                                       ID3D11ClassInstance* const*, UINT);
typedef void(__stdcall* GSSetShaderFn)(ID3D11DeviceContext*, ID3D11GeometryShader*,
                                       ID3D11ClassInstance* const*, UINT);
typedef void(__stdcall* OMSetRenderTargetsFn)(ID3D11DeviceContext*, UINT,
                                              ID3D11RenderTargetView* const*,
                                              ID3D11DepthStencilView*);

static CreateVertexShaderFn Original_CreateVertexShader = nullptr;
static CreateGeometryShaderFn Original_CreateGeometryShader = nullptr;
static CreatePixelShaderFn Original_CreatePixelShader = nullptr;
static VSSetShaderFn Original_VSSetShader = nullptr;
static PSSetShaderFn Original_PSSetShader = nullptr;
static GSSetShaderFn Original_GSSetShader = nullptr;
static OMSetRenderTargetsFn Original_OMSetRenderTargets = nullptr;

// Records shaderObject if its bytecode hash is in the registry.
inline void RecordShaderIfHud(void* shaderObject, const void* bytecode, SIZE_T bytecodeLength,
                              uint32_t stageBit) {
    if (!shaderObject || !bytecode || bytecodeLength == 0) {
        return;
    }
    const Registry& registry = GetRegistry();
    if (registry.hashes.empty()) {
        return;
    }
    uint64_t hash = ComputeHash(bytecode, bytecodeLength);
    auto it = registry.hashes.find(hash);
    if (it == registry.hashes.end()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(GetStateMutex());
        GetState().hudShaders[shaderObject] = it->second;
    }
    GetState().anyHudShaders.store(true);
    LOGSTRF("HudRedirect: HUD shader identified (hash %016llx, created as stage 0x%x, registry stage 0x%x)\n",
            static_cast<unsigned long long>(hash), stageBit, it->second);
}

// Fast existence check used by the SetShader hooks.
inline bool IsHudShader(void* shaderObject) {
    if (!GetState().anyHudShaders.load()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(GetStateMutex());
    return GetState().hudShaders.find(shaderObject) != GetState().hudShaders.end();
}

// Updates the per-context armed mask for one stage.
inline void UpdateArmed(ID3D11DeviceContext* context, void* shaderObject, uint32_t stageBit) {
    if (!context || !GetState().anyHudShaders.load()) {
        return;
    }
    bool isHud = IsHudShader(shaderObject);
    std::lock_guard<std::mutex> lock(GetStateMutex());
    uint32_t& mask = GetState().armed[context];
    if (isHud) {
        if ((mask & stageBit) == 0) {
            mask |= stageBit;
            LOGSTRF("HudRedirect: HUD pass armed on context %p (stage 0x%x)\n",
                    context, stageBit);
        }
    } else {
        mask &= ~stageBit;
    }
}

// Replaces the swapchain-sized HUD target (called on resize / format change).
inline void ReleaseHudTarget() {
    State& state = GetState();
    if (state.hudSrv) { state.hudSrv->Release(); state.hudSrv = nullptr; }
    if (state.hudRtv) { state.hudRtv->Release(); state.hudRtv = nullptr; }
    if (state.hudTex) { state.hudTex->Release(); state.hudTex = nullptr; }
    state.hudTargetFailed = false;
}

inline DXGI_FORMAT ResolveTypedFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return format;
    }
}

// Creates (or reuses) the offscreen HUD target matching the backbuffer.
// v1 limitation: non-MSAA backbuffers only; an MSAA backbuffer disables
// substitution (identification still works).
inline bool EnsureHudTarget(ID3D11Device* device) {
    State& state = GetState();
    if (state.hudRtv && state.hudSrv) {
        return true;
    }
    if (state.hudTargetFailed || !device || state.bbWidth == 0 || state.bbHeight == 0) {
        return false;
    }
    if (state.bbSamples > 1) {
        if (!state.bbMsaaLogged) {
            LOGWNDF("HudRedirect: MSAA backbuffer (%u samples) - HUD substitution disabled (v1 is non-MSAA only)\n",
                    state.bbSamples);
            state.bbMsaaLogged = true;
        }
        state.hudTargetFailed = true;
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = state.bbWidth;
    desc.Height = state.bbHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = ResolveTypedFormat(state.bbFormat);
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&desc, nullptr, &state.hudTex)) || !state.hudTex) {
        LOGWNDF("HudRedirect: failed to create HUD texture %ux%u fmt=%u\n",
                state.bbWidth, state.bbHeight, static_cast<unsigned>(desc.Format));
        state.hudTargetFailed = true;
        return false;
    }
    if (FAILED(device->CreateRenderTargetView(state.hudTex, nullptr, &state.hudRtv)) || !state.hudRtv) {
        LOGWNDF("HudRedirect: failed to create HUD RTV\n");
        ReleaseHudTarget();
        state.hudTargetFailed = true;
        return false;
    }
    if (FAILED(device->CreateShaderResourceView(state.hudTex, nullptr, &state.hudSrv)) || !state.hudSrv) {
        LOGWNDF("HudRedirect: failed to create HUD SRV\n");
        ReleaseHudTarget();
        state.hudTargetFailed = true;
        return false;
    }

    // Start transparent: nothing was drawn into it yet.
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    if (context) {
        const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(state.hudRtv, zero);
        context->Release();
    }

    LOGSTRF("HudRedirect: HUD target %ux%u fmt=%u created\n",
            state.bbWidth, state.bbHeight, static_cast<unsigned>(desc.Format));
    return true;
}

inline HRESULT __stdcall hookedCreateVertexShader(ID3D11Device* self, const void* pShaderBytecode,
                                           SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage,
                                           ID3D11VertexShader** ppVertexShader) {
    HRESULT hr = Original_CreateVertexShader(self, pShaderBytecode, BytecodeLength,
                                             pClassLinkage, ppVertexShader);
    if (SUCCEEDED(hr) && ppVertexShader && *ppVertexShader) {
        RecordShaderIfHud(*ppVertexShader, pShaderBytecode, BytecodeLength, kStageVS);
    }
    return hr;
}

inline HRESULT __stdcall hookedCreateGeometryShader(ID3D11Device* self, const void* pShaderBytecode,
                                             SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage,
                                             ID3D11GeometryShader** ppGeometryShader) {
    HRESULT hr = Original_CreateGeometryShader(self, pShaderBytecode, BytecodeLength,
                                               pClassLinkage, ppGeometryShader);
    if (SUCCEEDED(hr) && ppGeometryShader && *ppGeometryShader) {
        RecordShaderIfHud(*ppGeometryShader, pShaderBytecode, BytecodeLength, kStageGS);
    }
    return hr;
}

inline HRESULT __stdcall hookedCreatePixelShader(ID3D11Device* self, const void* pShaderBytecode,
                                          SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage,
                                          ID3D11PixelShader** ppPixelShader) {
    HRESULT hr = Original_CreatePixelShader(self, pShaderBytecode, BytecodeLength,
                                            pClassLinkage, ppPixelShader);
    if (SUCCEEDED(hr) && ppPixelShader && *ppPixelShader) {
        RecordShaderIfHud(*ppPixelShader, pShaderBytecode, BytecodeLength, kStagePS);
    }
    return hr;
}

inline void __stdcall hookedVSSetShader(ID3D11DeviceContext* self, ID3D11VertexShader* pVertexShader,
                                 ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) {
    UpdateArmed(self, pVertexShader, kStageVS);
    Original_VSSetShader(self, pVertexShader, ppClassInstances, NumClassInstances);
}

inline void __stdcall hookedPSSetShader(ID3D11DeviceContext* self, ID3D11PixelShader* pPixelShader,
                                 ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) {
    UpdateArmed(self, pPixelShader, kStagePS);
    Original_PSSetShader(self, pPixelShader, ppClassInstances, NumClassInstances);
}

inline void __stdcall hookedGSSetShader(ID3D11DeviceContext* self, ID3D11GeometryShader* pGeometryShader,
                                 ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) {
    UpdateArmed(self, pGeometryShader, kStageGS);
    Original_GSSetShader(self, pGeometryShader, ppClassInstances, NumClassInstances);
}

// Returns the sample count of the DSV's underlying texture (0 = no DSV).
inline uint32_t GetDsvSampleCount(ID3D11DepthStencilView* dsv) {
    if (!dsv) {
        return 0;
    }
    ID3D11Resource* resource = nullptr;
    dsv->GetResource(&resource);
    if (!resource) {
        return 0;
    }
    ID3D11Texture2D* texture = nullptr;
    uint32_t samples = 1;
    if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                           reinterpret_cast<void**>(&texture))) && texture) {
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        samples = desc.SampleDesc.Count;
        texture->Release();
    }
    resource->Release();
    return samples;
}

inline void __stdcall hookedOMSetRenderTargets(ID3D11DeviceContext* self, UINT NumViews,
                                        ID3D11RenderTargetView* const* ppRenderTargetViews,
                                        ID3D11DepthStencilView* pDepthStencilView) {
    State& state = GetState();

    // Scene-buffer probe (diagnostic, first few hits only): identify which
    // texture feeds the FINAL backbuffer pass - i.e. the game's internal
    // (possibly high-res, HDR) render target pre-downsample. This is the
    // candidate for LukeRoss-style internal-buffer capture for VR quality.
    if (state.backbuffer && ppRenderTargetViews && NumViews > 0) {
        static int probeLogged = 0;
        if (probeLogged < 6) {
            for (UINT i = 0; i < NumViews; ++i) {
                if (!ppRenderTargetViews[i]) continue;
                ID3D11Resource* res = nullptr;
                ppRenderTargetViews[i]->GetResource(&res);
                const bool isBackbuffer = (res != nullptr && res == state.backbuffer);
                if (res) res->Release();
                if (!isBackbuffer) continue;
                ID3D11ShaderResourceView* srv = nullptr;
                self->PSGetShaderResources(0, 1, &srv);
                if (srv) {
                    ID3D11Resource* srvRes = nullptr;
                    srv->GetResource(&srvRes);
                    ID3D11Texture2D* tex = nullptr;
                    if (srvRes && SUCCEEDED(srvRes->QueryInterface(__uuidof(ID3D11Texture2D),
                                                                   reinterpret_cast<void**>(&tex))) && tex) {
                        D3D11_TEXTURE2D_DESC d = {};
                        tex->GetDesc(&d);
                        LOGSTRF("SceneProbe: final-pass source = %ux%u fmt=%u samples=%u bind=0x%x\n",
                                d.Width, d.Height, d.Format, d.SampleDesc.Count, d.BindFlags);
                        probeLogged++;
                        tex->Release();
                    }
                    if (srvRes) srvRes->Release();
                    srv->Release();
                }
                break;
            }
        }
    }

    if (!state.anyHudShaders.load() || !state.backbuffer || !ppRenderTargetViews ||
        NumViews == 0 || NumViews > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) {
        Original_OMSetRenderTargets(self, NumViews, ppRenderTargetViews, pDepthStencilView);
        return;
    }

    uint32_t armedMask = 0;
    {
        std::lock_guard<std::mutex> lock(GetStateMutex());
        auto it = state.armed.find(self);
        armedMask = (it != state.armed.end()) ? it->second : 0;
    }
    if (armedMask == 0) {
        Original_OMSetRenderTargets(self, NumViews, ppRenderTargetViews, pDepthStencilView);
        return;
    }

    // Substitute only slots that actually target the swapchain backbuffer.
    ID3D11RenderTargetView* replaced[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    bool anyReplaced = false;
    for (UINT i = 0; i < NumViews; ++i) {
        replaced[i] = ppRenderTargetViews[i];
        if (!ppRenderTargetViews[i]) {
            continue;
        }
        ID3D11Resource* resource = nullptr;
        ppRenderTargetViews[i]->GetResource(&resource);
        bool isBackbuffer = (resource != nullptr && resource == state.backbuffer);
        if (resource) {
            resource->Release();
        }
        if (isBackbuffer) {
            ID3D11Device* device = nullptr;
            self->GetDevice(&device);
            bool ready = device && EnsureHudTarget(device);
            if (device) {
                device->Release();
            }
            if (ready) {
                replaced[i] = state.hudRtv;
                anyReplaced = true;
            }
        }
    }

    if (!anyReplaced) {
        Original_OMSetRenderTargets(self, NumViews, ppRenderTargetViews, pDepthStencilView);
        return;
    }

    // The HUD target is non-MSAA; a depth buffer with a different sample count
    // cannot stay bound alongside it (the runtime rejects the combination).
    ID3D11DepthStencilView* dsv = pDepthStencilView;
    uint32_t dsvSamples = GetDsvSampleCount(dsv);
    if (dsvSamples > 1) {
        static bool loggedDsvDrop = false;
        if (!loggedDsvDrop) {
            LOGSTR("HudRedirect: dropping MSAA depth-stencil from substituted HUD pass\n");
            loggedDsvDrop = true;
        }
        dsv = nullptr;
    }

    state.hudUsedThisFrame = true;
    Original_OMSetRenderTargets(self, NumViews, replaced, dsv);
}

// ---------------------------------------------------------------------------
// Composite + frame boundary
// ---------------------------------------------------------------------------

static const char* kCompositeVertexShader = R"(
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

VSOut main(uint id : SV_VertexID)
{
    float2 pos[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };
    float2 uv[3] = {
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f, 1.0f)
    };

    VSOut o;
    o.pos = float4(pos[id], 0.0f, 1.0f);
    o.uv = uv[id];
    return o;
}
)";

static const char* kCompositePixelShader = R"(
Texture2D g_hud : register(t0);
SamplerState g_sampler : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_Target
{
    return g_hud.Sample(g_sampler, uv);
}
)";

inline bool EnsureCompositeResources(ID3D11Device* device);

// Alpha-blits the HUD target onto one eye render target, in place. No-op when
// no HUD draw was substituted this frame. World-locked quad compositing is a
// documented follow-up; v1 is a straight screen-space blit.
inline void Composite(ID3D11Device* device, ID3D11DeviceContext* context,
                      ID3D11RenderTargetView* eyeRtv) {
    State& state = GetState();
    if (!state.hudUsedThisFrame || !state.hudSrv || !context || !eyeRtv) {
        return;
    }
    if (!EnsureCompositeResources(device)) {
        return;
    }

    // Size the viewport from the eye target.
    ID3D11Resource* resource = nullptr;
    eyeRtv->GetResource(&resource);
    if (!resource) {
        return;
    }
    ID3D11Texture2D* eyeTexture = nullptr;
    D3D11_TEXTURE2D_DESC eyeDesc = {};
    if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                           reinterpret_cast<void**>(&eyeTexture))) && eyeTexture) {
        eyeTexture->GetDesc(&eyeDesc);
        eyeTexture->Release();
    }
    resource->Release();
    if (eyeDesc.Width == 0 || eyeDesc.Height == 0) {
        return;
    }

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(eyeDesc.Width);
    viewport.Height = static_cast<float>(eyeDesc.Height);
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    context->OMSetRenderTargets(1, &eyeRtv, nullptr);
    const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(state.compositeBlend, blendFactor, 0xffffffff);
    context->OMSetDepthStencilState(state.compositeDepth, 0);
    context->RSSetState(state.compositeRaster);

    context->VSSetShader(state.compositeVs, nullptr, 0);
    context->PSSetShader(state.compositePs, nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->PSSetShaderResources(0, 1, &state.hudSrv);
    context->PSSetSamplers(0, 1, &state.compositeSampler);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrv[] = {nullptr};
    context->PSSetShaderResources(0, 1, nullSrv);
}

// Called once per Present, after the eye composites: clears the HUD target for
// the next frame and re-caches the backbuffer the next frame's substitution
// compares against.
inline void FinishFrame(ID3D11DeviceContext* context, ID3D11Texture2D* presentedBackbuffer) {
    State& state = GetState();

    if (state.hudUsedThisFrame) {
        if (context && state.hudRtv) {
            const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            context->ClearRenderTargetView(state.hudRtv, zero);
        }
        state.hudUsedThisFrame = false;
    }

    if (presentedBackbuffer) {
        D3D11_TEXTURE2D_DESC desc = {};
        presentedBackbuffer->GetDesc(&desc);
        bool changed = (presentedBackbuffer != state.backbuffer) ||
                       (desc.Width != state.bbWidth) ||
                       (desc.Height != state.bbHeight) ||
                       (desc.Format != state.bbFormat) ||
                       (desc.SampleDesc.Count != state.bbSamples);
        if (changed) {
            ReleaseHudTarget();
            state.backbuffer = presentedBackbuffer;
            state.bbWidth = desc.Width;
            state.bbHeight = desc.Height;
            state.bbFormat = desc.Format;
            state.bbSamples = desc.SampleDesc.Count;
            state.bbMsaaLogged = false;
        }
    } else {
        state.backbuffer = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Composite resource creation (D3DCompile at runtime, like the other shaders)
// ---------------------------------------------------------------------------

inline bool CompileShader(ID3D11Device* device, const char* source, const char* target,
                          const char* label, ID3DBlob** outBlob);

inline bool EnsureCompositeResources(ID3D11Device* device) {
    State& state = GetState();
    if (state.compositeVs && state.compositePs && state.compositeBlend &&
        state.compositeDepth && state.compositeRaster && state.compositeSampler) {
        return true;
    }
    if (!device) {
        return false;
    }

    ID3DBlob* blob = nullptr;
    if (!CompileShader(device, kCompositeVertexShader, "vs_5_0", "HUD composite VS", &blob)) {
        return false;
    }
    HRESULT hr = device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                            nullptr, &state.compositeVs);
    blob->Release();
    if (FAILED(hr) || !state.compositeVs) {
        LOGWNDF("HudRedirect: failed to create composite vertex shader\n");
        return false;
    }

    if (!CompileShader(device, kCompositePixelShader, "ps_5_0", "HUD composite PS", &blob)) {
        return false;
    }
    hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                   nullptr, &state.compositePs);
    blob->Release();
    if (FAILED(hr) || !state.compositePs) {
        LOGWNDF("HudRedirect: failed to create composite pixel shader\n");
        return false;
    }

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blendDesc, &state.compositeBlend))) {
        LOGWNDF("HudRedirect: failed to create composite blend state\n");
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = FALSE;
    depthDesc.StencilEnable = FALSE;
    if (FAILED(device->CreateDepthStencilState(&depthDesc, &state.compositeDepth))) {
        LOGWNDF("HudRedirect: failed to create composite depth state\n");
        return false;
    }

    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.ScissorEnable = FALSE;
    if (FAILED(device->CreateRasterizerState(&rasterDesc, &state.compositeRaster))) {
        LOGWNDF("HudRedirect: failed to create composite raster state\n");
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state.compositeSampler))) {
        LOGWNDF("HudRedirect: failed to create composite sampler\n");
        return false;
    }

    return true;
}

// Defined here (after the declaration) so the header stays self-contained
// without forcing d3dcompiler.h on every includer... it does need it anyway.
inline bool CompileShader(ID3D11Device* device, const char* source, const char* target,
                          const char* label, ID3DBlob** outBlob) {
    (void)device;
    *outBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                            "main", target, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                            outBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            LOGWNDF("HudRedirect: %s compile failed: %s\n", label,
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            errorBlob->Release();
        } else {
            LOGWNDF("HudRedirect: %s compile failed (hr=0x%08lx)\n", label,
                    static_cast<unsigned long>(hr));
        }
        if (*outBlob) {
            (*outBlob)->Release();
            *outBlob = nullptr;
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Install / uninstall
// ---------------------------------------------------------------------------

// Idempotent. Installs the identification + substitution hooks on the shared
// d3d11 implementation, using the given device's (and its immediate context's)
// vtable entries. Call as early as possible - the device-creation proxy paths
// are the first opportunity, which predates the game's shader compilation.
inline bool EnsureHooksInstalled(ID3D11Device* device) {
    State& state = GetState();
    if (state.hooksInstalled) {
        return true;
    }
    if (!device || !GetRegistry().enabled) {
        return false;
    }

    void** deviceVtable = *reinterpret_cast<void***>(device);
    // d3d11.h ID3D11Device vtable: CreateVertexShader=12,
    // CreateGeometryShader=13, CreatePixelShader=15.
    state.createVsTarget = deviceVtable[12];
    state.createGsTarget = deviceVtable[13];
    state.createPsTarget = deviceVtable[15];

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    if (!context) {
        LOGWNDF("HudRedirect: no immediate context - HUD hooks not installed\n");
        return false;
    }
    void** contextVtable = *reinterpret_cast<void***>(context);
    // d3d11.h ID3D11DeviceContext vtable: PSSetShader=9, VSSetShader=11,
    // GSSetShader=23, OMSetRenderTargets=33.
    state.psSetShaderTarget = contextVtable[9];
    state.vsSetShaderTarget = contextVtable[11];
    state.gsSetShaderTarget = contextVtable[23];
    state.omSetRenderTargetsTarget = contextVtable[33];
    context->Release();

    struct HookSpec {
        void* target;
        void* detour;
        void** original;
        const char* name;
    };
    HookSpec specs[] = {
        {state.createVsTarget, reinterpret_cast<void*>(&hookedCreateVertexShader),
         reinterpret_cast<void**>(&Original_CreateVertexShader), "CreateVertexShader(12)"},
        {state.createGsTarget, reinterpret_cast<void*>(&hookedCreateGeometryShader),
         reinterpret_cast<void**>(&Original_CreateGeometryShader), "CreateGeometryShader(13)"},
        {state.createPsTarget, reinterpret_cast<void*>(&hookedCreatePixelShader),
         reinterpret_cast<void**>(&Original_CreatePixelShader), "CreatePixelShader(15)"},
        {state.vsSetShaderTarget, reinterpret_cast<void*>(&hookedVSSetShader),
         reinterpret_cast<void**>(&Original_VSSetShader), "VSSetShader(11)"},
        {state.psSetShaderTarget, reinterpret_cast<void*>(&hookedPSSetShader),
         reinterpret_cast<void**>(&Original_PSSetShader), "PSSetShader(9)"},
        {state.gsSetShaderTarget, reinterpret_cast<void*>(&hookedGSSetShader),
         reinterpret_cast<void**>(&Original_GSSetShader), "GSSetShader(23)"},
        {state.omSetRenderTargetsTarget, reinterpret_cast<void*>(&hookedOMSetRenderTargets),
         reinterpret_cast<void**>(&Original_OMSetRenderTargets), "OMSetRenderTargets(33)"},
    };

    uint32_t installed = 0;
    for (HookSpec& spec : specs) {
        if (MH_CreateHook(spec.target, spec.detour, spec.original) == MH_OK &&
            MH_EnableHook(spec.target) == MH_OK) {
            ++installed;
        } else {
            MH_RemoveHook(spec.target);
            LOGWNDF("HudRedirect: failed to hook %s\n", spec.name);
        }
    }

    if (installed == 0) {
        LOGWNDF("HudRedirect: no HUD hooks installed - HUD identification disabled\n");
        return false;
    }
    state.hooksInstalled = true;
    LOGSTRF("HudRedirect: installed %u/%u HUD hooks (identification %s)\n",
            installed, static_cast<unsigned>(sizeof(specs) / sizeof(specs[0])),
            GetRegistry().entryCount > 0 ? "active" : "idle (no registered hashes)");
    return true;
}

// Releases every D3D object owned by the HUD path (device-lost / shutdown).
// Never touches MinHook.
inline void ReleaseResources() {
    State& state = GetState();
    ReleaseHudTarget();
    if (state.compositeVs) { state.compositeVs->Release(); state.compositeVs = nullptr; }
    if (state.compositePs) { state.compositePs->Release(); state.compositePs = nullptr; }
    if (state.compositeBlend) { state.compositeBlend->Release(); state.compositeBlend = nullptr; }
    if (state.compositeDepth) { state.compositeDepth->Release(); state.compositeDepth = nullptr; }
    if (state.compositeRaster) { state.compositeRaster->Release(); state.compositeRaster = nullptr; }
    if (state.compositeSampler) { state.compositeSampler->Release(); state.compositeSampler = nullptr; }
    state.backbuffer = nullptr;
    state.bbWidth = 0;
    state.bbHeight = 0;
    state.bbFormat = DXGI_FORMAT_UNKNOWN;
    state.hudUsedThisFrame = false;
    std::lock_guard<std::mutex> lock(GetStateMutex());
    state.armed.clear();
}

// Ordered-unload counterpart of EnsureHooksInstalled.
inline void UninstallHooks() {
    State& state = GetState();
    void* targets[] = {
        state.createVsTarget, state.createGsTarget, state.createPsTarget,
        state.vsSetShaderTarget, state.psSetShaderTarget, state.gsSetShaderTarget,
        state.omSetRenderTargetsTarget,
    };
    for (void* target : targets) {
        if (target) {
            MH_RemoveHook(target);
        }
    }
    state.createVsTarget = nullptr;
    state.createGsTarget = nullptr;
    state.createPsTarget = nullptr;
    state.vsSetShaderTarget = nullptr;
    state.psSetShaderTarget = nullptr;
    state.gsSetShaderTarget = nullptr;
    state.omSetRenderTargetsTarget = nullptr;
    state.hooksInstalled = false;
}

} // namespace Hud
} // namespace OVRInject
