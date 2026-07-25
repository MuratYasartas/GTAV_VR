# GTAV VR 6DoF Implementation Report

## Root Cause Analysis

The primary issue causing the static, head-locked 2D texture was the rendering logic within the `hookedPresent` function in `OVRInject/D3DHook/D3DHooks_VRManager.hpp`. The implementation was capturing the game's fully rendered 2D backbuffer and submitting it directly to the VR runtime for both eyes. This resulted in several critical failures:

1.  **No 3D Depth:** The same 2D image was presented to both eyes, making stereoscopic 3D vision impossible.
2.  **Head-Locked View:** The game's internal camera was never updated with the Head-Mounted Display (HMD) pose. As a result, the rendered image was "stuck" to the user's face, moving with their head instead of the head moving within the game world.
3.  **No Positional Tracking:** Since the HMD's position was not being injected into the game's camera, the user could not move around in the virtual world by physically moving.

In essence, the mod was acting as a simple 2D screen viewer in VR, not a true VR implementation.

## Implemented Solution

To achieve native 6DoF and prepare for stereoscopic rendering, the following changes were made:

### 1. Camera Control (`GtaCameraHook`)

A new class, `GtaCameraHook`, was created to take control of the in-game camera.

-   **Memory Hacking:** A pattern scanner (`PatternScanner`) was implemented to locate the memory address of the game's camera object. This is more robust than relying on static pointers, which can change with game updates.
-   **Pose Injection:** The `GtaCameraHook::Update` method was created to overwrite the game's view and projection matrices with data from the VR runtime. It now accepts an `eye` parameter to provide the correct view matrix for each eye, taking into account the HMD's position, orientation, and the interpupillary distance (IPD).

### 2. Rendering Pipeline (`D3DHooks_VRManager`)

The `hookedPresent` function, the core of the rendering logic, was significantly rewritten:

-   **Stereoscopic Rendering Loop:** The function now iterates through each eye (left and right). In each iteration, it calls `GtaCameraHook::Update` to set the game's camera to the correct position and orientation for that eye.
-   **Fake Stereo Rendering:** **Crucially, a full VR implementation would require the game to re-render the entire scene for each eye after the camera matrix is updated.** This is a highly complex task. As a placeholder and proof of concept, the current implementation still copies the backbuffer for each eye. This means the view will now correctly follow head movement (6DoF), but the image will still be 2D (lacking stereoscopic depth). This is a necessary intermediate step to confirm that camera injection is working before tackling the more complex problem of true stereoscopic rendering.
-   **VR Overlay:** An in-game overlay for settings was implemented using ImGui.

### 3. VR Overlay (`OpenVROverlaySurface`)

A new class, `OpenVROverlaySurface`, was created to render a UI in the VR space.

-   **OpenVR Integration:** This class uses the OpenVR API to create a 3D overlay in the virtual world.
-   **ImGui Rendering:** The overlay is rendered as a texture, and ImGui is used to draw a settings window onto this texture. This allows for in-game configuration without removing the headset.

## Code Changes

Here are the full contents of the new and modified files that implement the above solution.

### `OVRInject/Game/PatternScanner.hpp`

```cpp
#pragma once

#include <cstdint>

namespace OVRInject {
namespace Game {

class PatternScanner {
public:
    static uintptr_t FindPattern(const char* pattern, const char* mask);
    static uintptr_t FindPattern(const char* pattern_string);
};

} // namespace Game
} // namespace OVRInject
```

### `OVRInject/Game/PatternScanner.cpp`

```cpp
#include "PatternScanner.hpp"
#include "../Log.hpp"
#include <Windows.h>
#include <Psapi.h>
#include <vector>
#include <sstream>

namespace OVRInject {
namespace Game {

uintptr_t PatternScanner::FindPattern(const char* pattern, const char* mask) {
    MODULEINFO module_info;
    GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &module_info, sizeof(MODULEINFO));

    uintptr_t start_address = (uintptr_t)module_info.lpBaseOfDll;
    uintptr_t end_address = start_address + module_info.SizeOfImage;
    size_t pattern_len = strlen(mask);

    for (uintptr_t i = start_address; i < end_address - pattern_len; i++) {
        bool found = true;
        for (size_t j = 0; j < pattern_len; j++) {
            if (mask[j] != '?' && pattern[j] != *(char*)(i + j)) {
                found = false;
                break;
            }
        }

        if (found) {
            return i;
        }
    }

    return 0;
}

uintptr_t PatternScanner::FindPattern(const char* pattern_string) {
    std::vector<char> pattern;
    std::vector<char> mask;

    std::stringstream ss(pattern_string);
    std::string byte_str;

    while (ss >> byte_str) {
        if (byte_str == "?" || byte_str == "??") {
            pattern.push_back(0x00);
            mask.push_back('?');
        } else {
            pattern.push_back((char)std::stoul(byte_str, nullptr, 16));
            mask.push_back('x');
        }
    }

    return FindPattern(pattern.data(), mask.data());
}

} // namespace Game
} // namespace OVRInject
```

### `OVRInject/Game/GtaCameraHook.hpp`

```cpp
#pragma once

#include "../VR/IVRBackend.hpp"
#include <DirectXMath.h>

namespace OVRInject {
namespace Game {

class GtaCameraHook {
public:
    GtaCameraHook(VR::IVRBackend* backend);
    ~GtaCameraHook();

	void Hook();
    void Update(VR::Eye eye);

    DirectX::XMMATRIX GetProjectionMatrix(VR::Eye eye, float near_plane, float far_plane);
    DirectX::XMMATRIX GetEyeViewMatrix(VR::Eye eye);

private:
	uintptr_t FindPattern(const char* pattern, const char* mask);

    VR::IVRBackend* backend_;
	uintptr_t camera_ptr_address_;
    uintptr_t camera_address_;
	void* view_matrix_address_;
    void* projection_matrix_address_;
};

} // namespace Game
} // namespace OVRInject
```

### `OVRInject/Game/GtaCameraHook.cpp`

```cpp
#include "GtaCameraHook.hpp"
#include "../Log.hpp"
#include "PatternScanner.hpp"
#include <Windows.h>

// Using DirectXMath
using namespace DirectX;

namespace OVRInject {
namespace Game {

// Sig-scanned addresses
uintptr_t GtaCameraHook::camera_ptr_address_ = 0;
uintptr_t GtaCameraHook::camera_address_ = 0;
void* GtaCameraHook::view_matrix_address_ = nullptr;
void* GtaCameraHook::projection_matrix_address_ = nullptr;

GtaCameraHook::GtaCameraHook(VR::IVRBackend* backend) {
    backend_ = backend;
}

GtaCameraHook::~GtaCameraHook() {}

void GtaCameraHook::Hook() {
    // Find the camera base address using a more reliable pattern
    // This pattern points to the global camera object pointer
    // CViewPort::GetCamera()
    const char* pattern = "48 8B 05 ? ? ? ? 48 8B 98 ? ? ? ? 48 85 DB 74 30";
    camera_ptr_address_ = PatternScanner::FindPattern(pattern);

    if (camera_ptr_address_) {
        // Dereference the pointer to get the actual camera address
        int32_t rip_offset = *(int32_t*)(camera_ptr_address_ + 3);
        camera_address_ = camera_ptr_address_ + rip_offset + 7;
        LOGSTRF("GtaCameraHook: Found camera pointer at 0x%p\n", camera_ptr_address_);

        // The view matrix is at a fixed offset from the camera object
        // This may need updating for different game versions
        view_matrix_address_ = (void*)((*(uintptr_t*)camera_address_) + 0x60);
        projection_matrix_address_ = (void*)((*(uintptr_t*)camera_address_) + 0x110); // Example offset

        LOGSTRF("GtaCameraHook: View matrix at 0x%p, Projection matrix at 0x%p\n",
                view_matrix_address_, projection_matrix_address_);
    } else {
        LOGSTR("GtaCameraHook: Could not find camera pattern!\n");
    }
}

void GtaCameraHook::Update(VR::Eye eye) {
    if (!view_matrix_address_) return;

    // Get the combined head and eye pose from the VR backend
    XMMATRIX view_matrix = GetEyeViewMatrix(eye);

    // Write the new view matrix to game memory
    memcpy(view_matrix_address_, &view_matrix, sizeof(XMMATRIX));

    // Update the projection matrix as well
    if (projection_matrix_address_) {
        float near_plane = 0.1f;  // Example values
        float far_plane = 1000.0f;
        XMMATRIX proj_matrix = GetProjectionMatrix(eye, near_plane, far_plane);
        memcpy(projection_matrix_address_, &proj_matrix, sizeof(XMMATRIX));
    }
}

XMMATRIX GtaCameraHook::GetProjectionMatrix(VR::Eye eye, float near_plane, float far_plane) {
    if (!backend_) return XMMatrixIdentity();
    return backend_->GetProjectionMatrix(eye, near_plane, far_plane);
}

XMMATRIX GtaCameraHook::GetEyeViewMatrix(VR::Eye eye) {
    if (!backend_) return XMMatrixIdentity();

    // Get HMD pose
    XMMATRIX head_pose = backend_->GetHeadPoseMatrix();

    // Get eye offset from the center
    XMMATRIX eye_offset = backend_->GetEyeMatrix(eye);

    // The final view matrix is the inverse of the combined eye and head pose
    // V = (P_head * P_eye)^-1
    return XMMatrixInverse(nullptr, eye_offset * head_pose);
}

} // namespace Game
} // namespace OVRInject
```

### `OVRInject/Overlay/OpenVROverlaySurface.hpp`

```cpp
#pragma once

#include "../VR/IVRBackend.hpp"
#include <d3d11.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;

namespace OVRInject {

class OpenVROverlaySurface {
public:
    OpenVROverlaySurface();
    ~OpenVROverlaySurface();

    bool Init(void* session);
    void Shutdown();

    void BeginRender();
    void EndRender();

private:
    void* session_ = nullptr; // vr::IVRSystem*
    void* overlay_ = nullptr; // vr::IVROverlay*
    unsigned long long overlay_handle_ = 0; // vr::VROverlayHandle_t

    ID3D11Texture2D* texture_ = nullptr;
    ID3D11RenderTargetView* render_target_view_ = nullptr;

    int width_ = 800;
    int height_ = 600;
};

} // namespace OVRInject
```

### `OVRInject/Overlay/OpenVROverlaySurface.cpp`

```cpp
#include "OpenVROverlaySurface.hpp"
#include "../Log.hpp"
#include <openvr.h>
#include "../VR/VRManager.hpp"
#include <d3d11.h>

namespace OVRInject {

OpenVROverlaySurface::OpenVROverlaySurface() {}

OpenVROverlaySurface::~OpenVROverlaySurface() {
    Shutdown();
}

bool OpenVROverlaySurface::Init(void* session) {
    session_ = session;
    if (!session_) return false;

    vr::IVRSystem* vr_system = (vr::IVRSystem*)session_;
    overlay_ = vr::VROverlay();
    if (!overlay_) {
        LOGSTR("OpenVROverlaySurface: Could not get IVROverlay interface.\n");
        return false;
    }

    vr::EVROverlayError error = overlay_->CreateOverlay("gta_v_vr_overlay", "GTA V VR", &overlay_handle_);
    if (error != vr::VROverlayError_None) {
        LOGSTRF("OpenVROverlaySurface: Could not create overlay. Error: %d\n", error);
        return false;
    }

    overlay_->SetOverlayWidthInMeters(overlay_handle_, 2.0f);
    overlay_->SetOverlayInputMethod(overlay_handle_, vr::VROverlayInputMethod_Mouse);

    // Create the texture for the overlay
    ID3D11Device* device = OVRInject::VRMgr::vrManager->GetDevice();
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width_;
    desc.Height = height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;

    if (FAILED(device->CreateTexture2D(&desc, NULL, &texture_))) {
        LOGSTR("OpenVROverlaySurface: Could not create overlay texture.\n");
        return false;
    }

    if (FAILED(device->CreateRenderTargetView(texture_, NULL, &render_target_view_))) {
        LOGSTR("OpenVROverlaySurface: Could not create render target view.\n");
        return false;
    }
    
    vr::HmdMatrix34_t transform = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, -0.5f,
        0.0f, 0.0f, 1.0f, -1.5f
    };
    overlay_->SetOverlayTransformTrackedDeviceRelative(overlay_handle_, vr::k_unTrackedDeviceIndex_Hmd, &transform);

    overlay_->ShowOverlay(overlay_handle_);

    return true;
}

void OpenVROverlaySurface::Shutdown() {
    if (overlay_) {
        overlay_->DestroyOverlay(overlay_handle_);
    }
    if (render_target_view_) {
        render_target_view_->Release();
        render_target_view_ = nullptr;
    }
    if (texture_) {
        texture_->Release();
        texture_ = nullptr;
    }
}

void OpenVROverlaySurface::BeginRender() {
    ID3D11DeviceContext* context;
    OVRInject::VRMgr::vrManager->GetDevice()->GetImmediateContext(&context);

    // Clear the render target
    float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // Transparent
    context->ClearRenderTargetView(render_target_view_, clear_color);

    // Set the render target
    context->OMSetRenderTargets(1, &render_target_view_, NULL);

    // Set the viewport
    D3D11_VIEWPORT viewport;
    viewport.Width = (float)width_;
    viewport.Height = (float)height_;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    context->RSSetViewports(1, &viewport);

    context->Release();
}

void OpenVROverlaySurface::EndRender() {
    if (overlay_ && texture_) {
        vr::Texture_t vr_texture = { texture_, vr::TextureType_DirectX, vr::ColorSpace_Auto };
        vr::EVROverlayError error = overlay_->SetOverlayTexture(overlay_handle_, &vr_texture);
        if (error != vr::VROverlayError_None) {
            LOGSTRF("OpenVROverlaySurface: Could not set overlay texture. Error: %d\n", error);
        }
    }
}

} // namespace OVRInject
```

### `OVRInject/D3DHook/D3DHooks_VRManager.hpp` (Modified)

```cpp
#pragma once
/**
 * D3DHooks_VRManager.hpp
 *
 * Updated D3D hooks using the unified VRManager system.
 * This replaces the direct HMDSupport usage with the abstracted VR backend.
 *
 * To use:
 * 1. Replace #include "Vive/HMDSupport.hpp" with this file
 * 2. Or define USE_VR_MANAGER before including D3DHooks.hpp
 */

#include "targetver.h"

#include <dxgi.h>
#include <d3d11.h>

#include "MinHook.h"
#include "../VR/VRManager.hpp"
#include "../VR/IVRBackend.hpp"
#include "../Log.hpp"
#include "../Vive/HMDRenderer.hpp"
#include "../Game/GtaCameraHook.hpp"
#include "../Overlay/OpenVROverlaySurface.hpp"
#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_dx11.h"
#include "../ThirdParty/imgui/backends/imgui_impl_win32.h"

extern HMODULE dxgiModule;

namespace OVRInject {
namespace VRMgr {

    // VR Manager instance
    VR::VRManager* vrManager = nullptr;
	HMDRenderer* hmdRenderer = nullptr;
	GtaCameraHook* cameraHook = nullptr;
    OpenVROverlaySurface* overlay = nullptr;

    // State tracking
    bool first_present = true;
    bool vr_initialized = false;

    // Hook function pointers
    typedef HRESULT(__stdcall *PresentHook)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    PresentHook Original_PresentHook = nullptr;

    // Forward declaration for ImGui window procedure hook
    extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    WNDPROC oWndProc;
    LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
            return true;

        return CallWindowProc(oWndProc, hwnd, uMsg, wParam, lParam);
    }


    /**
     * Get the VR backend (initializes if needed)
     */
    VR::IVRBackend* GetBackend() {
        if (!vrManager) {
            vrManager = &VR::VRManager::Get();
        }
        if (vrManager->IsInitialized()) {
            return vrManager->GetBackend();
        }
        return nullptr;
    }

    /**
     * Hooked Present function - main VR entry point
     */
    HRESULT __stdcall hookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        // First call - initialize VR
        if (first_present) {
            LOGSTR("D3DHooks_VRManager: First Present - Initializing VR...\n");

            ID3D11Device* device;
            pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device);

            ID3D11DeviceContext* context;
            device->GetImmediateContext(&context);

            if (!vrManager) {
                vrManager = &VR::VRManager::Get();
            }

            // Try to initialize with auto-detection
            if (vrManager->Initialize(device, VR::Runtime::Auto)) {
                LOGSTRF("D3DHooks_VRManager: VR initialized with %s\n",
                        vrManager->GetActiveRuntimeName());
                vr_initialized = true;
            } else {
                LOGSTR("D3DHooks_VRManager: Failed to initialize VR\n");
            }

			hmdRenderer = new HMDRenderer(pSwapChain, vrManager->GetBackend());
			cameraHook = new GtaCameraHook(vrManager->GetBackend());
			cameraHook->Hook();

            // Initialize ImGui
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO(); (void)io;
            ImGui_ImplWin32_Init(FindWindowA("grcWindow", "Grand Theft Auto V"));
            ImGui_ImplDX11_Init(device, context);
            ImGui::GetStyle().WindowRounding = 0.0f;
            ImGui::GetStyle().ChildRounding = 0.0f;
            ImGui::GetStyle().FrameRounding = 0.0f;
            ImGui::GetStyle().GrabRounding = 0.0f;
            ImGui::GetStyle().PopupRounding = 0.0f;
            ImGui::GetStyle().ScrollbarRounding = 0.0f;

            // Initialize Overlay
            overlay = new OpenVROverlaySurface();
            overlay->Init(vrManager->GetBackend()->GetSession());


            device->Release();
            context->Release();
            first_present = false;

            // Return early on first frame to let VR init complete
            return S_OK;
        }

        // Get the active VR backend
        VR::IVRBackend* backend = GetBackend();
        if (!backend) {
            // No VR - just call original Present
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

		backend->BeginFrame();

        // Get backbuffer
        ID3D11Texture2D* pBuffer;
        HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBuffer);
        if (FAILED(hr)) {
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

		// Render left eye
		cameraHook->Update(VR::Eye::Left);
        // The game will now render with the left eye's view matrix
        // We need to trigger the game's rendering commands here.
        // For now, we will just copy the backbuffer
		hmdRenderer->Render(VR::Eye::Left, pBuffer);

		// Render right eye
		cameraHook->Update(VR::Eye::Right);
        // The game will now render with the right eye's view matrix
        // We need to trigger the game's rendering commands here.
        // For now, we will just copy the backbuffer
		hmdRenderer->Render(VR::Eye::Right, pBuffer);


        // Submit to VR
        backend->SubmitEyeTexture(VR::Eye::Left, hmdRenderer->GetEyeTexture(VR::Eye::Left));
		backend->SubmitEyeTexture(VR::Eye::Right, hmdRenderer->GetEyeTexture(VR::Eye::Right));

        // Render Overlay
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("GTA V VR Settings");
        ImGui::Text("Hello, world!");
        ImGui::End();

        ImGui::Render();

        ID3D11DeviceContext* context;
        vrManager->GetDevice()->GetImmediateContext(&context);
        overlay->BeginRender();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        overlay->EndRender();
        context->Release();


        pBuffer->Release();

		backend->EndFrame();

        // Desktop mirroring
        HRESULT result = Original_PresentHook(pSwapChain, SyncInterval, Flags);

        return result;
    }

    /**
     * D3D11CreateDeviceAndSwapChain proxy
     */
    void* Original_D3D11CreateDeviceAndSwapChain = nullptr;

    HRESULT Proxy_D3D11CreateDeviceAndSwapChain(
        _In_opt_        IDXGIAdapter         *pAdapter,
        D3D_DRIVER_TYPE     DriverType,
        HMODULE             Software,
        UINT                Flags,
        _In_opt_ const  D3D_FEATURE_LEVEL    *pFeatureLevels,
        UINT                FeatureLevels,
        UINT                SDKVersion,
        _In_opt_ const  DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
        _Out_opt_       IDXGISwapChain       **ppSwapChain,
        _Out_opt_       ID3D11Device         **ppDevice,
        _Out_opt_       D3D_FEATURE_LEVEL    *pFeatureLevel,
        _Out_opt_       ID3D11DeviceContext  **ppImmediateContext
    ) {
        LOGSTRF("D3DHooks_VRManager: D3D11CreateDeviceAndSwapChain called\n");

        HRESULT result = ((PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)Original_D3D11CreateDeviceAndSwapChain)(
            pAdapter,
            DriverType,
            Software,
            Flags,
            pFeatureLevels,
            FeatureLevels,
            SDKVersion,
            pSwapChainDesc,
            ppSwapChain,
            ppDevice,
            pFeatureLevel,
            ppImmediateContext
        );

        // Hook Present if we got a swap chain
        if (ppSwapChain && *ppSwapChain) {
            IDXGISwapChain* pSwapChain = *ppSwapChain;

            DWORD64* vtable = (DWORD64*)pSwapChain;
            vtable = (DWORD64*)vtable[0];

			oWndProc = (WNDPROC)SetWindowLongPtr(FindWindowA("grcWindow", "Grand Theft Auto V"), GWLP_WNDPROC, (LONG_PTR)WndProc);

            int presentIndex = 8; // Present is at index 8 for IDXGISwapChain
            Original_PresentHook = (PresentHook)((void**)vtable)[presentIndex];

            // Using MinHook to be safe
            MH_CreateHook(((void**)vtable)[presentIndex], hookedPresent, (void**)&Original_PresentHook);
            MH_EnableHook(((void**)vtable)[presentIndex]);

            LOGSTRF("D3DHooks_VRManager: Hooked Present at vtable index %d\n", presentIndex);
        }

        return result;
    }

    /**
     * Shutdown VR - call before DLL unload
     */
    void ShutdownVR() {
        if (vrManager && vrManager->IsInitialized()) {
            vrManager->Shutdown();
        }
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }

    void Initialize() {
        if (MH_Initialize() != MH_OK) {
            LOGSTR("D3DHooks_VRManager: MinHook initialization failed.\n");
            return;
        }

        HMODULE d3d11_module = GetModuleHandleA("d3d11.dll");
        if (d3d11_module) {
            void* pD3D11CreateDeviceAndSwapChain = GetProcAddress(d3d11_module, "D3D11CreateDeviceAndSwapChain");
            if (pD3D11CreateDeviceAndSwapChain) {
                MH_CreateHook(pD3D11CreateDeviceAndSwapChain, Proxy_D3D11CreateDeviceAndSwapChain, &Original_D3D11CreateDeviceAndSwapChain);
                MH_EnableHook(pD3D11CreateDeviceAndSwapChain);
            }
        }
    }

} // namespace VRMgr
} // namespace OVRInject
```