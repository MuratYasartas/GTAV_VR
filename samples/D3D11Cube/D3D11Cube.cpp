// D3D11Cube.cpp
//
// Minimal Win32 + D3D11 stereo-test target for the GTAVR vertical slice
// (Mission Phase 2). This app stands in for GTA V so the VR mod can be
// exercised end-to-end on a trivial engine we fully control.
//
// Scene: three solid-colour cubes at known depths (z = 1m red, 3m green,
// 10m blue) plus a grey ground grid. Default camera sits at the origin
// looking down -Z.
//
// Modes:
//   - Default: renders with the built-in camera. If a producer publishes
//     poses through the "GTAVR_SLICE_CAM" shared mapping (see SliceCam.hpp)
//     with the VR_CONTROL flag set, the app renders alternate-eye frames
//     (viewL on even frames, viewR on odd frames) instead.
//   - Golden/test mode (env GTAVR_SLICE_GOLDEN=N): renders N deterministic
//     frames with a fixed simulated IPD of 0.063m (left eye at x=-0.0315 on
//     even frames, right eye at x=+0.0315 on odd frames), dumps every
//     backbuffer as a 24-bit BMP, then exits 0. Determinism: animation is
//     driven purely by the frame index (both frames of an L/R pair share
//     the same rotation), never by a clock; the shared mapping is ignored.
//
// NOTE: the window class name "grcWindow" is a leftover from when the mod
// located the window via FindWindowA("grcWindow"). The mod now resolves the
// window from the swapchain desc, so the name is harmless. See samples/README.md.

#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "SliceCam.hpp"

using Microsoft::WRL::ComPtr;
using gtavr::SliceCamState;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr float kFovY = 70.0f * 3.14159265358979323846f / 180.0f;
constexpr float kNearZ = 0.05f;
constexpr float kFarZ = 100.0f;

// Golden mode: IPD 0.063m -> eyes at +/- 0.0315m.
constexpr float kGoldenHalfIpd = 0.0315f;
// Deterministic per-pair rotation step (radians). Both frames of an L/R
// pair share the same angle so a pair differs only by the eye offset.
constexpr float kAngleStep = 0.02f;

// ---------------------------------------------------------------------------
// Row-major 4x4 matrix helpers (column-vector convention: clip = P*V*W*pos).
// Uploaded verbatim into the cbuffer; the HLSL side uses mul(pos, m), which
// composes correctly with default column-major cbuffer storage.
// ---------------------------------------------------------------------------

struct Mat4 {
    float m[16];
};

Mat4 MatIdentity() {
    Mat4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 MatMul(const Mat4& a, const Mat4& b) {
    Mat4 r = {};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a.m[i * 4 + k] * b.m[k * 4 + j];
            }
            r.m[i * 4 + j] = s;
        }
    }
    return r;
}

Mat4 MatTranslation(float x, float y, float z) {
    Mat4 r = MatIdentity();
    r.m[3] = x;
    r.m[7] = y;
    r.m[11] = z;
    return r;
}

Mat4 MatRotationY(float rad) {
    Mat4 r = MatIdentity();
    const float c = cosf(rad);
    const float s = sinf(rad);
    r.m[0] = c;
    r.m[2] = s;
    r.m[8] = -s;
    r.m[10] = c;
    return r;
}

// Standard D3D left-handed perspective (view space looks down +Z).
Mat4 MatPerspectiveFovLH(float fovY, float aspect, float zn, float zf) {
    const float f = 1.0f / tanf(fovY * 0.5f);
    Mat4 r = {};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = zf / (zf - zn);
    r.m[11] = -zn * zf / (zf - zn);
    r.m[14] = 1.0f;
    return r;
}

// Camera at eye, looking down -Z: translate world by -eye, then flip Z so
// the LH projection above sees forward geometry at positive view Z.
Mat4 MatViewForEye(float ex, float ey, float ez) {
    Mat4 flip = MatIdentity();
    flip.m[10] = -1.0f;
    return MatMul(flip, MatTranslation(-ex, -ey, -ez));
}

// ---------------------------------------------------------------------------
// Shaders (compiled at startup; solid untextured colour per draw).
// ---------------------------------------------------------------------------

const char kShaderSrc[] = R"(
cbuffer PerDraw : register(b0)
{
    float4x4 mvp;
    float4   color;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

VSOut VSMain(float3 pos : POSITION)
{
    VSOut o;
    o.pos = mul(float4(pos, 1.0f), mvp);
    o.col = color;
    return o;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.col;
}
)";

struct CbPerDraw {
    float mvp[16];
    float color[4];
};

struct Mesh {
    ComPtr<ID3D11Buffer> vb;
    ComPtr<ID3D11Buffer> ib;
    UINT indexCount = 0;
    UINT vertexCount = 0;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

struct CubeDef {
    float pos[3];
    float color[4];
};

// Known scene layout: unique pure colours at known depths. samples/check_golden.py
// relies on these exact colours/depths for the disparity measurement.
const CubeDef kCubes[] = {
    {{0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},    // near,  z =  1m, red
    {{-2.2f, 0.2f, -3.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},   // mid,   z =  3m, green
    {{4.5f, -0.2f, -10.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},  // far,   z = 10m, blue
};

constexpr float kCubeHalf = 0.25f;
constexpr float kGridColor[4] = {0.5f, 0.5f, 0.5f, 1.0f};
constexpr float kClearColor[4] = {0.10f, 0.10f, 0.12f, 1.0f};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

bool CompileShaders(ID3D11Device* dev, ComPtr<ID3D11VertexShader>& vs,
                    ComPtr<ID3D11PixelShader>& ps, ComPtr<ID3D11InputLayout>& layout) {
    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif
    HRESULT hr = D3DCompile(kShaderSrc, sizeof(kShaderSrc) - 1, "slice.hlsl", nullptr,
                            nullptr, "VSMain", "vs_5_0", flags, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        fprintf(stderr, "VS compile failed: %s\n",
                errBlob ? (const char*)errBlob->GetBufferPointer() : "(no details)");
        return false;
    }
    hr = D3DCompile(kShaderSrc, sizeof(kShaderSrc) - 1, "slice.hlsl", nullptr,
                    nullptr, "PSMain", "ps_5_0", flags, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        fprintf(stderr, "PS compile failed: %s\n",
                errBlob ? (const char*)errBlob->GetBufferPointer() : "(no details)");
        return false;
    }
    if (FAILED(dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                       nullptr, &vs)) ||
        FAILED(dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                      nullptr, &ps))) {
        fprintf(stderr, "Shader creation failed.\n");
        return false;
    }
    D3D11_INPUT_ELEMENT_DESC elem = {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                                     D3D11_INPUT_PER_VERTEX_DATA, 0};
    if (FAILED(dev->CreateInputLayout(&elem, 1, vsBlob->GetBufferPointer(),
                                      vsBlob->GetBufferSize(), &layout))) {
        fprintf(stderr, "Input layout creation failed.\n");
        return false;
    }
    return true;
}

Mesh CreateCubeMesh(ID3D11Device* dev) {
    const float s = kCubeHalf;
    // 4 vertices per face, 6 faces; culling is disabled so winding is irrelevant.
    const float verts[][3] = {
        {-s, -s, s}, {s, -s, s}, {s, s, s}, {-s, s, s},        // +z
        {s, -s, -s}, {-s, -s, -s}, {-s, s, -s}, {s, s, -s},    // -z
        {s, -s, s}, {s, -s, -s}, {s, s, -s}, {s, s, s},        // +x
        {-s, -s, -s}, {-s, -s, s}, {-s, s, s}, {-s, s, -s},    // -x
        {-s, s, s}, {s, s, s}, {s, s, -s}, {-s, s, -s},        // +y
        {-s, -s, -s}, {s, -s, -s}, {s, -s, s}, {-s, -s, s},    // -y
    };
    std::uint16_t indices[36];
    for (int f = 0; f < 6; ++f) {
        const std::uint16_t b = static_cast<std::uint16_t>(f * 4);
        indices[f * 6 + 0] = b + 0;
        indices[f * 6 + 1] = b + 1;
        indices[f * 6 + 2] = b + 2;
        indices[f * 6 + 3] = b + 0;
        indices[f * 6 + 4] = b + 2;
        indices[f * 6 + 5] = b + 3;
    }

    Mesh mesh;
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = sizeof(verts);
    D3D11_SUBRESOURCE_DATA init = {verts, 0, 0};
    dev->CreateBuffer(&bd, &init, &mesh.vb);
    bd.ByteWidth = sizeof(indices);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    init.pSysMem = indices;
    dev->CreateBuffer(&bd, &init, &mesh.ib);
    mesh.indexCount = 36;
    mesh.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    return mesh;
}

Mesh CreateGridMesh(ID3D11Device* dev) {
    // Ground plane at y = -1m, x in [-12, 12], z in [-21, -1], 1m spacing.
    std::vector<float> verts;
    for (int i = 0; i <= 20; ++i) {
        const float z = -1.0f - static_cast<float>(i);
        verts.insert(verts.end(), {-12.0f, -1.0f, z, 12.0f, -1.0f, z});
    }
    for (int i = -12; i <= 12; ++i) {
        const float x = static_cast<float>(i);
        verts.insert(verts.end(), {x, -1.0f, -1.0f, x, -1.0f, -21.0f});
    }

    Mesh mesh;
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = static_cast<UINT>(verts.size() * sizeof(float));
    D3D11_SUBRESOURCE_DATA init = {verts.data(), 0, 0};
    dev->CreateBuffer(&bd, &init, &mesh.vb);
    mesh.vertexCount = static_cast<UINT>(verts.size() / 3);
    mesh.topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    return mesh;
}

void PutLE16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

void PutLE32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// Writes a 24-bit bottom-up BMP from top-down BGRA8 pixels.
bool WriteBmp24(const std::string& path, int width, int height, const std::uint8_t* bgra,
                int pitch) {
    const std::uint32_t rowBytes = static_cast<std::uint32_t>(width) * 3;
    const std::uint32_t padded = (rowBytes + 3) & ~3u;
    const std::uint32_t dataSize = padded * static_cast<std::uint32_t>(height);
    const std::uint32_t fileSize = 54 + dataSize;

    std::uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    PutLE32(header + 2, fileSize);
    PutLE32(header + 10, 54);
    PutLE32(header + 14, 40);
    PutLE32(header + 18, static_cast<std::uint32_t>(width));
    PutLE32(header + 22, static_cast<std::uint32_t>(height));  // positive: bottom-up
    PutLE16(header + 26, 1);
    PutLE16(header + 28, 24);
    PutLE32(header + 34, dataSize);

    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) {
        fprintf(stderr, "Cannot open %s for writing.\n", path.c_str());
        return false;
    }
    fwrite(header, 1, sizeof(header), f);
    std::vector<std::uint8_t> row(padded, 0);
    for (int y = height - 1; y >= 0; --y) {
        const std::uint8_t* src = bgra + static_cast<size_t>(y) * pitch;
        for (int x = 0; x < width; ++x) {
            row[x * 3 + 0] = src[x * 4 + 0];  // B
            row[x * 3 + 1] = src[x * 4 + 1];  // G
            row[x * 3 + 2] = src[x * 4 + 2];  // R
        }
        fwrite(row.data(), 1, padded, f);
    }
    fclose(f);
    return true;
}

// Copies the swapchain backbuffer into a CPU-readable staging texture.
bool CaptureBackbuffer(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* swap,
                       ComPtr<ID3D11Texture2D>& staging, std::vector<std::uint8_t>& pixels,
                       int& pitch) {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
        return false;
    }
    if (!staging) {
        D3D11_TEXTURE2D_DESC desc = {};
        back->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(dev->CreateTexture2D(&desc, nullptr, &staging))) {
            return false;
        }
    }
    ctx->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }
    pitch = static_cast<int>(mapped.RowPitch);
    pixels.resize(static_cast<size_t>(pitch) * kHeight);
    memcpy(pixels.data(), mapped.pData, pixels.size());
    ctx->Unmap(staging.Get(), 0);
    return true;
}

std::string GetEnvString(const char* name) {
    char buf[512];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        return std::string();
    }
    return std::string(buf, n);
}

}  // namespace

int main() {
    // Golden/test mode: GTAVR_SLICE_GOLDEN=N renders N deterministic frames.
    const std::string goldenEnv = GetEnvString("GTAVR_SLICE_GOLDEN");
    const int goldenFrames = goldenEnv.empty() ? 0 : atoi(goldenEnv.c_str());
    const bool goldenMode = goldenFrames > 0;
    std::string goldenDir = GetEnvString("GTAVR_SLICE_GOLDEN_DIR");
    if (goldenDir.empty()) {
        goldenDir = "golden_out";
    }
    if (goldenMode) {
        CreateDirectoryA(goldenDir.c_str(), nullptr);  // fine if it already exists
    }

    // Window class name is historical (see note at file top); any name works.
    HINSTANCE inst = GetModuleHandle(nullptr);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "grcWindow";
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        fprintf(stderr, "RegisterClassA failed (%lu).\n", GetLastError());
        return 1;
    }
    RECT rc = {0, 0, kWidth, kHeight};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowExA(0, "grcWindow", "GTAVR Slice",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                                rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        fprintf(stderr, "CreateWindowExA failed (%lu).\n", GetLastError());
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Device + swapchain.
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = kWidth;
    sd.BufferDesc.Height = kHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain> swap;
    const D3D_DRIVER_TYPE driverTypes[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
    HRESULT hr = E_FAIL;
    for (D3D_DRIVER_TYPE dt : driverTypes) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, dt, nullptr, 0, nullptr, 0,
                                           D3D11_SDK_VERSION, &sd, &swap, &dev, nullptr,
                                           &ctx);
        if (SUCCEEDED(hr)) {
            break;
        }
    }
    if (FAILED(hr)) {
        fprintf(stderr, "D3D11CreateDeviceAndSwapChain failed (0x%08lX).\n",
                static_cast<unsigned long>(hr));
        return 1;
    }

    ComPtr<ID3D11Texture2D> back;
    ComPtr<ID3D11RenderTargetView> rtv;
    swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(dev->CreateRenderTargetView(back.Get(), nullptr, &rtv))) {
        fprintf(stderr, "CreateRenderTargetView failed.\n");
        return 1;
    }

    ComPtr<ID3D11Texture2D> depthTex;
    ComPtr<ID3D11DepthStencilView> dsv;
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = kWidth;
    dd.Height = kHeight;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(dev->CreateTexture2D(&dd, nullptr, &depthTex)) ||
        FAILED(dev->CreateDepthStencilView(depthTex.Get(), nullptr, &dsv))) {
        fprintf(stderr, "Depth buffer creation failed.\n");
        return 1;
    }

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    if (!CompileShaders(dev.Get(), vs, ps, layout)) {
        return 1;
    }

    ComPtr<ID3D11Buffer> cb;
    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.ByteWidth = sizeof(CbPerDraw);
    if (FAILED(dev->CreateBuffer(&cbd, nullptr, &cb))) {
        fprintf(stderr, "Constant buffer creation failed.\n");
        return 1;
    }

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    // AntialiasedLineEnable and MultisampleEnable stay FALSE so golden frames
    // contain only exact, flat colours (required by the BMP checker).
    ComPtr<ID3D11RasterizerState> rs;
    dev->CreateRasterizerState(&rd, &rs);

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ComPtr<ID3D11DepthStencilState> dss;
    dev->CreateDepthStencilState(&dsd, &dss);

    Mesh cube = CreateCubeMesh(dev.Get());
    Mesh grid = CreateGridMesh(dev.Get());
    if (!cube.vb || !cube.ib || !grid.vb) {
        fprintf(stderr, "Mesh creation failed.\n");
        return 1;
    }

    D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(kWidth),
                         static_cast<float>(kHeight), 0.0f, 1.0f};

    // Cooperative-engine camera harness: named shared mapping, read-only here.
    // Zero-filled when we create it, so magic == 0 until a producer writes it.
    HANDLE camMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                           0, sizeof(SliceCamState),
                                           gtavr::kSliceCamMapName);
    SliceCamState* camShared =
        camMapping ? static_cast<SliceCamState*>(MapViewOfFile(camMapping, FILE_MAP_READ,
                                                               0, 0, sizeof(SliceCamState)))
                   : nullptr;
    if (!camShared) {
        fprintf(stderr, "Warning: GTAVR_SLICE_CAM mapping unavailable; VR-control mode disabled.\n");
    }

    ComPtr<ID3D11Texture2D> staging;
    bool running = true;
    std::uint32_t frame = 0;
    int goldenWritten = 0;

    printf("GTAVR D3D11Cube slice%s\n",
           goldenMode ? " (golden mode)" : " — waiting for VR control or running default camera");

    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) {
            break;
        }
        if (goldenMode && frame >= static_cast<std::uint32_t>(goldenFrames)) {
            break;
        }

        // Pick the camera for this frame.
        Mat4 proj = MatPerspectiveFovLH(kFovY, static_cast<float>(kWidth) / kHeight,
                                        kNearZ, kFarZ);
        Mat4 view = MatViewForEye(0.0f, 0.0f, 0.0f);
        bool externalCam = false;
        if (!goldenMode && camShared) {
            SliceCamState snap;
            memcpy(&snap, camShared, sizeof(snap));
            if (snap.magic == gtavr::kSliceCamMagic &&
                (snap.flags & gtavr::kSliceCamFlagVrControl)) {
                memcpy(view.m, (frame & 1) ? snap.viewR : snap.viewL, sizeof(view.m));
                memcpy(proj.m, snap.proj, sizeof(proj.m));
                externalCam = true;
            }
        }
        if (goldenMode) {
            // Alternate-eye simulation with a fixed IPD of 0.063m.
            const float eyeX = (frame & 1) ? kGoldenHalfIpd : -kGoldenHalfIpd;
            view = MatViewForEye(eyeX, 0.0f, 0.0f);
        }
        (void)externalCam;

        // Deterministic animation: driven by the frame pair index only.
        const float angle = static_cast<float>(frame >> 1) * kAngleStep;

        ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(rs.Get());
        ctx->OMSetDepthStencilState(dss.Get(), 0);
        ctx->IASetInputLayout(layout.Get());
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, cb.GetAddressOf());

        ctx->ClearRenderTargetView(rtv.Get(), kClearColor);
        ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

        auto drawMesh = [&](const Mesh& mesh, const Mat4& mvp, const float color[4]) {
            CbPerDraw cbData;
            memcpy(cbData.mvp, mvp.m, sizeof(cbData.mvp));
            memcpy(cbData.color, color, sizeof(cbData.color));
            ctx->UpdateSubresource(cb.Get(), 0, nullptr, &cbData, 0, 0);
            UINT stride = 3 * sizeof(float);
            UINT offset = 0;
            ctx->IASetVertexBuffers(0, 1, mesh.vb.GetAddressOf(), &stride, &offset);
            ctx->IASetPrimitiveTopology(mesh.topology);
            if (mesh.ib) {
                ctx->IASetIndexBuffer(mesh.ib.Get(), DXGI_FORMAT_R16_UINT, 0);
                ctx->DrawIndexed(mesh.indexCount, 0, 0);
            } else {
                ctx->Draw(mesh.vertexCount, 0);
            }
        };

        drawMesh(grid, MatMul(proj, view), kGridColor);
        for (const CubeDef& def : kCubes) {
            const Mat4 world =
                MatMul(MatTranslation(def.pos[0], def.pos[1], def.pos[2]),
                       MatRotationY(angle));
            drawMesh(cube, MatMul(proj, MatMul(view, world)), def.color);
        }

        if (goldenMode) {
            std::vector<std::uint8_t> pixels;
            int pitch = 0;
            if (!CaptureBackbuffer(dev.Get(), ctx.Get(), swap.Get(), staging, pixels,
                                   pitch)) {
                fprintf(stderr, "Backbuffer capture failed on frame %u.\n", frame);
                return 1;
            }
            char name[64];
            snprintf(name, sizeof(name), "frame_%04u_%c.bmp", frame,
                     (frame & 1) ? 'R' : 'L');
            const std::string path = goldenDir + "\\" + name;
            if (!WriteBmp24(path, kWidth, kHeight, pixels.data(), pitch)) {
                return 1;
            }
            ++goldenWritten;
        }

        swap->Present(goldenMode ? 0 : 1, 0);
        ++frame;
    }

    if (goldenMode) {
        printf("Golden mode: wrote %d/%d frames to %s\n", goldenWritten, goldenFrames,
               goldenDir.c_str());
        return goldenWritten == goldenFrames ? 0 : 1;
    }
    return 0;
}
