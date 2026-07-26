#include "XROverlayUI.hpp"
#include "../Log.hpp"
#include "../Perf/PerfStats.hpp"
#include "../VR/SharedSettings.hpp"
#include "../VR/VRManager.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <Windows.h>

// ImGui includes (conditional)
#ifdef HAS_IMGUI
#include "imgui.h"
#include "imgui_impl_dx11.h"
#endif

namespace OVRInject {
namespace XR {

#ifdef HAS_IMGUI
namespace {

static char ToLowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

static bool ReadEnvFlagDefaultTrue(const char* name) {
    char value[8] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    if (len == 0 || len >= sizeof(value)) {
        return true;
    }

    char c = ToLowerAscii(value[0]);
    if (c == '0' || c == 'n' || c == 'f') {
        return false;
    }
    return true;
}

static bool IsMouseCaptureEnabled() {
    static bool enabled = ReadEnvFlagDefaultTrue("GTAVR_MOUSE_CAPTURE");
    return enabled;
}

static bool IsModuleAvailable(const char* name) {
    HMODULE module = LoadLibraryA(name);
    if (!module) {
        return false;
    }
    FreeLibrary(module);
    return true;
}

static bool HasOpticalFlow() {
    static int cached = -1;
    if (cached < 0) {
        cached = IsModuleAvailable("nvofapi64.dll") ? 1 : 0;
    }
    return cached == 1;
}

static bool HasNgx() {
    static int cached = -1;
    if (cached < 0) {
        cached = (IsModuleAvailable("nvngx_dlss.dll") || IsModuleAvailable("nvngx.dll")) ? 1 : 0;
    }
    return cached == 1;
}

struct WindowSearch {
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
    WindowSearch* search = reinterpret_cast<WindowSearch*>(lparam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != search->pid) {
        return TRUE;
    }

    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    search->hwnd = hwnd;
    return FALSE;
}

static HWND FindMainWindow(DWORD pid) {
    WindowSearch search;
    search.pid = pid;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&search));
    return search.hwnd;
}

static HWND GetOverlayWindow() {
    static HWND cached = nullptr;
    static DWORD cachedPid = 0;

    if (cached && IsWindow(cached)) {
        return cached;
    }

    DWORD pid = GetCurrentProcessId();
    if (pid != cachedPid) {
        cached = nullptr;
    }

    HWND foreground = GetForegroundWindow();
    if (foreground) {
        DWORD fgPid = 0;
        GetWindowThreadProcessId(foreground, &fgPid);
        if (fgPid == pid) {
            cached = foreground;
            cachedPid = pid;
            return cached;
        }
    }

    cached = FindMainWindow(pid);
    cachedPid = pid;
    return cached;
}

static void UpdateMouseCapture(bool enabled) {
    static bool captured = false;
    static HWND capturedWindow = nullptr;

    if (!IsMouseCaptureEnabled() || !enabled) {
        if (captured) {
            ClipCursor(nullptr);
            ReleaseCapture();
            captured = false;
            capturedWindow = nullptr;
        }
        return;
    }

    HWND hwnd = GetOverlayWindow();
    if (!hwnd) {
        return;
    }

    if (captured && capturedWindow == hwnd) {
        return;
    }

    RECT rect = {};
    if (!GetClientRect(hwnd, &rect)) {
        return;
    }

    POINT ul = {rect.left, rect.top};
    POINT lr = {rect.right, rect.bottom};
    ClientToScreen(hwnd, &ul);
    ClientToScreen(hwnd, &lr);

    RECT clip = {ul.x, ul.y, lr.x, lr.y};
    ClipCursor(&clip);
    SetCapture(hwnd);

    captured = true;
    capturedWindow = hwnd;
}

static bool UpdateMouseInput(ImGuiIO& io,
                             float overlayWidth,
                             float overlayHeight,
                             float* outX,
                             float* outY) {
    if (!IsMouseCaptureEnabled()) {
        return false;
    }

    UpdateMouseCapture(true);

    HWND hwnd = GetOverlayWindow();
    if (!hwnd) {
        return false;
    }

    RECT client = {};
    if (!GetClientRect(hwnd, &client)) {
        return false;
    }

    int clientWidth = client.right - client.left;
    int clientHeight = client.bottom - client.top;
    if (clientWidth <= 0 || clientHeight <= 0) {
        return false;
    }

    POINT screen = {};
    if (!GetCursorPos(&screen)) {
        return false;
    }

    if (!ScreenToClient(hwnd, &screen)) {
        return false;
    }

    float x = (static_cast<float>(screen.x) / static_cast<float>(clientWidth)) * overlayWidth;
    float y = (static_cast<float>(screen.y) / static_cast<float>(clientHeight)) * overlayHeight;

    if (x < 0.0f) {
        x = 0.0f;
    } else if (x > overlayWidth) {
        x = overlayWidth;
    }

    if (y < 0.0f) {
        y = 0.0f;
    } else if (y > overlayHeight) {
        y = overlayHeight;
    }

    io.MousePos = ImVec2(x, y);
    io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    io.MouseDrawCursor = true;

    if (outX) {
        *outX = x;
    }
    if (outY) {
        *outY = y;
    }

    return true;
}

} // namespace
#endif

static std::string ResolveSettingsPath(const char* filename) {
    char path[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("GTAVR_SETTINGS_DIR", path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::string full(path);
        if (!full.empty() && full.back() != '\\' && full.back() != '/') {
            full.push_back('\\');
        }
        full += filename;
        DWORD attrs = GetFileAttributesA(full.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return full;
        }
    }

    len = GetEnvironmentVariableA("GTAVR_SETTINGS_PATH", path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::string(path);
    }

    return std::string(filename);
}

//-----------------------------------------------------------------------------
// Constructor / Destructor
//-----------------------------------------------------------------------------

XROverlayUI::XROverlayUI() {
}

XROverlayUI::~XROverlayUI() {
    Shutdown();
}

//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------

bool XROverlayUI::Initialize(ID3D11Device* device, IOverlaySurface* overlay) {
#ifdef HAS_IMGUI
    if (initialized_) return true;

    device_ = device;
    overlay_ = overlay;

    if (!device_) {
        LOGSTR("XROverlayUI: Device is null\n");
        return false;
    }

    device_->GetImmediateContext(&context_);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Set style for VR (larger elements, high contrast)
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;

    // Scale up for VR readability
    style.ScaleAllSizes(1.5f);
    io.FontGlobalScale = 1.5f;

    // Initialize D3D11 backend
    ImGui_ImplDX11_Init(device_, context_);

    initialized_ = true;
    LOGSTR("XROverlayUI: Initialized\n");
    return true;
#else
    LOGSTR("XROverlayUI: ImGui not available (HAS_IMGUI not defined)\n");
    return false;
#endif
}

void XROverlayUI::Shutdown() {
#ifdef HAS_IMGUI
    if (!initialized_) return;

    UpdateMouseCapture(false);

    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();

    if (context_) {
        context_->Release();
        context_ = nullptr;
    }

    initialized_ = false;
    LOGSTR("XROverlayUI: Shutdown\n");
#endif
}

void XROverlayUI::SetVisible(bool visible) {
#ifdef HAS_IMGUI
    if (visible_ == visible) {
        return;
    }
    visible_ = visible;
    VR::GetRuntimeStats().overlayVisible.store(visible);
    UpdateMouseCapture(visible_);
#else
    visible_ = visible;
    VR::GetRuntimeStats().overlayVisible.store(visible);
#endif
}

void XROverlayUI::Toggle() {
    SetVisible(!visible_);
}

//-----------------------------------------------------------------------------
// Rendering
//-----------------------------------------------------------------------------

void XROverlayUI::Render() {
#ifdef HAS_IMGUI
    if (!initialized_ || !visible_) return;

    // Begin ImGui frame
    ImGui_ImplDX11_NewFrame();

    // Get overlay render target
    ID3D11RenderTargetView* rtv = nullptr;
    if (overlay_) {
        rtv = overlay_->BeginRender();
    }

    if (!rtv) {
        return;
    }

    float overlayWidth = overlay_ ? static_cast<float>(overlay_->GetTextureWidth()) : 1024.0f;
    float overlayHeight = overlay_ ? static_cast<float>(overlay_->GetTextureHeight()) : 768.0f;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(overlayWidth, overlayHeight);

    static auto lastTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> delta = now - lastTime;
    io.DeltaTime = (delta.count() > 0.0f) ? delta.count() : (1.0f / 90.0f);
    lastTime = now;

    ImGui::NewFrame();

    // Clear overlay to transparent so it doesn't block the scene
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_->ClearRenderTargetView(rtv, clearColor);
    context_->OMSetRenderTargets(1, &rtv, nullptr);

    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = overlayWidth;
    viewport.Height = overlayHeight;
    viewport.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &viewport);

    bool wasVisible = visible_;

    // Main window
    float windowWidth = viewport.Width * 0.7f;
    float windowHeight = viewport.Height * 0.75f;
    ImGui::SetNextWindowPos(ImVec2((viewport.Width - windowWidth) * 0.5f,
                                   (viewport.Height - windowHeight) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);

    ImGuiStyle& style = ImGui::GetStyle();
    float prevAlpha = style.Alpha;
    style.Alpha = settings_.overlayOpacity;

    ImGui::SetNextWindowBgAlpha(0.2f * settings_.overlayOpacity);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("GTA VR Settings", &visible_, windowFlags)) {
        RenderMenuBar();

        ImGui::Separator();

        // Tab content
        switch (current_tab_) {
            case 0: RenderWorldSettings(); break;
            case 1: RenderComfortSettings(); break;
            case 2: RenderPerformanceSettings(); break;
            case 3: RenderControlsSettings(); break;
            case 4: RenderDebugInfo(); break;
        }

        ImGui::Separator();

        // Save/Load buttons
        if (ImGui::Button("Save Settings")) {
            SaveSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Settings")) {
            LoadSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Defaults")) {
            settings_ = VRSettings();
            NotifySettingsChanged();
        }
    }
    ImGui::End();

    if (wasVisible != visible_) {
        UpdateMouseCapture(visible_);
    }

    style.Alpha = prevAlpha;

    // Check if ImGui wants input
    wants_capture_ = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;

    // Render ImGui
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // End overlay render
    if (overlay_) {
        overlay_->EndRender();
    }
#endif
}

void XROverlayUI::HandleInput(const OverlayInputState& leftState, const OverlayInputState& rightState) {
#ifdef HAS_IMGUI
    if (!initialized_) return;

    ImGuiIO& io = ImGui::GetIO();

    float width = overlay_ ? static_cast<float>(overlay_->GetTextureWidth()) : 1024.0f;
    float height = overlay_ ? static_cast<float>(overlay_->GetTextureHeight()) : 768.0f;
    bool mouseActive = UpdateMouseInput(io, width, height, &cursor_x_, &cursor_y_);

    if (!mouseActive) {
        // Use right thumbstick for cursor movement
        cursor_x_ += rightState.thumbstickX * 10.0f;
        cursor_y_ -= rightState.thumbstickY * 10.0f;  // Invert Y

        // Clamp cursor
        cursor_x_ = max(0.0f, min(cursor_x_, width));
        cursor_y_ = max(0.0f, min(cursor_y_, height));

        io.MousePos = ImVec2(cursor_x_, cursor_y_);

        // Trigger = click
        io.MouseDown[0] = rightState.triggerPressed;

        // Grip = right click
        io.MouseDown[1] = rightState.gripPressed;

        io.MouseDown[2] = false;
        io.MouseDrawCursor = true;
    } else {
        io.MouseDown[0] = io.MouseDown[0] || rightState.triggerPressed;
        io.MouseDown[1] = io.MouseDown[1] || rightState.gripPressed;
    }

    // Left thumbstick for scrolling
    io.MouseWheel = 0.0f;
    io.MouseWheel += leftState.thumbstickY * 0.5f;

    // A button = confirm, B button = back (for gamepad navigation)
    io.NavInputs[ImGuiNavInput_Activate] = rightState.primaryPressed ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_Cancel] = rightState.secondaryPressed ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_Menu] = rightState.menuPressed ? 1.0f : 0.0f;

    // Keyboard fallback for dead/unmapped controllers: arrows navigate,
    // Left/Right adjust the focused slider, Enter activates, Esc cancels.
    // ImGui applies its own key-repeat to held NavInputs. Limit: these
    // keys also reach the game (no WndProc hook) - docs/user/comfort.md.
    auto keyDown = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    if (keyDown(VK_UP))     io.NavInputs[ImGuiNavInput_DpadUp] = 1.0f;
    if (keyDown(VK_DOWN))   io.NavInputs[ImGuiNavInput_DpadDown] = 1.0f;
    if (keyDown(VK_LEFT))   io.NavInputs[ImGuiNavInput_DpadLeft] = 1.0f;
    if (keyDown(VK_RIGHT))  io.NavInputs[ImGuiNavInput_DpadRight] = 1.0f;
    if (keyDown(VK_RETURN)) io.NavInputs[ImGuiNavInput_Activate] = 1.0f;
    if (keyDown(VK_ESCAPE)) io.NavInputs[ImGuiNavInput_Cancel] = 1.0f;
#endif
}

//-----------------------------------------------------------------------------
// UI Tabs
//-----------------------------------------------------------------------------

void XROverlayUI::RenderMenuBar() {
#ifdef HAS_IMGUI
    const char* tabs[] = {"World", "Comfort", "Performance", "Controls", "Debug"};

    for (int i = 0; i < 5; ++i) {
        if (i > 0) ImGui::SameLine();

        bool selected = (current_tab_ == i);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }

        if (ImGui::Button(tabs[i], ImVec2(120, 40))) {
            current_tab_ = i;
        }

        if (selected) {
            ImGui::PopStyleColor();
        }
    }
#endif
}

void XROverlayUI::RenderWorldSettings() {
#ifdef HAS_IMGUI
    ImGui::Text("World Settings");
    ImGui::Spacing();

    bool changed = false;

    changed |= ImGui::SliderFloat("World Scale", &settings_.worldScale, 0.5f, 2.0f, "%.2f");
    ImGui::SetItemTooltip("Adjust the scale of the game world");

    changed |= ImGui::SliderFloat("Player Height (m)", &settings_.playerHeight, 1.2f, 2.2f, "%.2f");
    ImGui::SetItemTooltip("Your real-world height for proper scaling");

    ImGui::Separator();
    ImGui::Text("Camera Offset");

    auto snapToStep = [](float value, float step) {
        if (step <= 0.0f) return value;
        return std::round(value / step) * step;
    };
    auto clampValue = [](float value, float minValue, float maxValue) {
        return (std::max)(minValue, (std::min)(value, maxValue));
    };
    auto nudgeButtons = [&](const char* id, float& value, float step, float minValue, float maxValue) {
        bool nudged = false;
        ImGui::SameLine();
        ImGui::PushID(id);
        if (ImGui::SmallButton("-")) {
            value = clampValue(value - step, minValue, maxValue);
            nudged = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) {
            value = clampValue(value + step, minValue, maxValue);
            nudged = true;
        }
        ImGui::PopID();
        return nudged;
    };

    // X Offset with finer 0.01 step and +/- buttons
    if (ImGui::SliderFloat("X Offset", &settings_.cameraOffsetX, -1.0f, 1.0f, "%.3f")) {
        // Snap to 0.01 step for fine control
        float snapped = snapToStep(settings_.cameraOffsetX, 0.01f);
        if (std::fabs(snapped - settings_.cameraOffsetX) > 0.0001f) {
            settings_.cameraOffsetX = snapped;
        }
        changed = true;
    }
    changed |= nudgeButtons("xoffset", settings_.cameraOffsetX, 0.01f, -1.0f, 1.0f);

    // Y Offset with finer 0.01 step and +/- buttons
    if (ImGui::SliderFloat("Y Offset", &settings_.cameraOffsetY, -1.0f, 1.0f, "%.3f")) {
        // Snap to 0.01 step for fine control
        float snapped = snapToStep(settings_.cameraOffsetY, 0.01f);
        if (std::fabs(snapped - settings_.cameraOffsetY) > 0.0001f) {
            settings_.cameraOffsetY = snapped;
        }
        changed = true;
    }
    changed |= nudgeButtons("yoffset", settings_.cameraOffsetY, 0.01f, -1.0f, 1.0f);

    // Z Offset with +/- buttons
    changed |= ImGui::SliderFloat("Z Offset", &settings_.cameraOffsetZ, -1.0f, 1.0f, "%.3f");
    changed |= nudgeButtons("zoffset", settings_.cameraOffsetZ, 0.01f, -1.0f, 1.0f);

    ImGui::Separator();
    ImGui::Text("Stereo Mode");
    const char* stereoModes[] = {"Depth Reprojection (2D)", "Alternate-Eye (True Stereo)"};
    int stereoMode = settings_.stereoMode;
    if (ImGui::Combo("Mode", &stereoMode, stereoModes, IM_ARRAYSIZE(stereoModes))) {
        settings_.stereoMode = stereoMode;
        changed = true;
    }
    changed |= ImGui::Checkbox("Auto IPD (runtime)", &settings_.ipdAuto);
    ImGui::SetItemTooltip("Eye offset taken from the HMD's own tracking data - the physically correct IPD. Recommended.");
    if (!settings_.ipdAuto) {
        changed |= ImGui::SliderFloat("Stereo IPD (m)", &settings_.stereoIPD, 0.04f, 0.08f, "%.4f");
        ImGui::SetItemTooltip("Manual IPD (only when Auto is off). Fallback: used when the runtime reports no eye offset.");
    }
    changed |= ImGui::Checkbox("Head Tracking", &settings_.headTracking);
    changed |= ImGui::Checkbox("Position Tracking", &settings_.positionTracking);
    if (settings_.stereoMode != 0 || settings_.headLookEnabled) {
        auto& stats = VR::GetRuntimeStats();
        bool configLoaded = stats.cameraConfigLoaded.load();
        bool hookReady = stats.cameraHookReady.load();
        if (!configLoaded) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                "Camera config missing: place gtavr_camera.ini next to GTA5.exe");
        } else if (!hookReady) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                "Camera hook not ready: waiting for camera matrix (load into game)");
            uint64_t addr = stats.cameraMatrixAddress.load();
            if (addr) {
                bool writable = stats.cameraMatrixWritable.load();
                ImGui::Text("Matrix addr: 0x%llX (%s)",
                            static_cast<unsigned long long>(addr),
                            writable ? "writable" : "not writable");
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Image Alignment");
    constexpr float kImageStep = 0.01f;
    // Note: 1.0 unit = 1% of the image width/height (Stereo/ImageFit.hpp
    // kImageOffsetScale). Convergence needs double-digit units on AER -
    // the old +/-1 range (1%) was far too small to ever reach superposition.
    bool offsetXChanged = ImGui::SliderFloat("Stereo Offset X", &settings_.imageOffsetX, -30.0f, 30.0f, "%.2f");
    offsetXChanged |= nudgeButtons("StereoOffsetX", settings_.imageOffsetX, kImageStep, -100.0f, 100.0f);
    if (offsetXChanged) {
        settings_.imageOffsetX = clampValue(snapToStep(settings_.imageOffsetX, kImageStep), -100.0f, 100.0f);
        changed = true;
    }

    bool offsetYChanged = ImGui::SliderFloat("Stereo Offset Y", &settings_.imageOffsetY, -15.0f, 15.0f, "%.2f");
    offsetYChanged |= nudgeButtons("StereoOffsetY", settings_.imageOffsetY, kImageStep, -100.0f, 100.0f);
    if (offsetYChanged) {
        settings_.imageOffsetY = clampValue(snapToStep(settings_.imageOffsetY, kImageStep), -100.0f, 100.0f);
        changed = true;
    }

    bool scaleChanged = ImGui::SliderFloat("Image Scale (FOV)", &settings_.imageScale, 0.25f, 4.0f, "%.2f");
    scaleChanged |= nudgeButtons("ImageScale", settings_.imageScale, kImageStep, 0.01f, 100.0f);
    if (scaleChanged) {
        settings_.imageScale = clampValue(snapToStep(settings_.imageScale, kImageStep), 0.01f, 100.0f);
        changed = true;
    }
    if (ImGui::Button("Recenter Image")) {
        settings_.imageOffsetX = 0.0f;
        settings_.imageOffsetY = 0.0f;
        settings_.imageScale = 1.0f;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Recenter View")) {
        recenter_requested_ = true;
    }

    ImGui::Separator();
    ImGui::Text("Camera FOV Override");
    changed |= ImGui::Checkbox("Override Camera FOV", &settings_.fovOverride);
    if (settings_.fovOverride) {
        ImGui::Indent();
        changed |= ImGui::Checkbox("Per Camera Type", &settings_.fovPerType);

        if (settings_.fovPerType) {
            changed |= ImGui::SliderFloat("FOV (FP Ped)", &settings_.fovFpPed, 1.0f, 130.0f, "%.1f");
            changed |= ImGui::SliderFloat("FOV (TP Ped)", &settings_.fovTpPed, 1.0f, 130.0f, "%.1f");
            changed |= ImGui::SliderFloat("FOV (TP Aim)", &settings_.fovTpAim, 1.0f, 130.0f, "%.1f");
            changed |= ImGui::SliderFloat("FOV (FP Vehicle)", &settings_.fovFpVehicle, 1.0f, 130.0f, "%.1f");
            changed |= ImGui::SliderFloat("FOV (TP Vehicle)", &settings_.fovTpVehicle, 1.0f, 130.0f, "%.1f");
        } else {
            changed |= ImGui::SliderFloat("FOV (Global)", &settings_.fovGlobal, 1.0f, 130.0f, "%.1f");
        }

        changed |= ImGui::Checkbox("Manual FOV Offset", &settings_.fovOffsetOverride);
        if (settings_.fovOffsetOverride) {
            int offset = settings_.fovManualOffset;
            if (ImGui::InputInt("FOV Offset (bytes)", &offset)) {
                if (offset < 0) offset = 0;
                settings_.fovManualOffset = offset;
                changed = true;
            }
        }
        ImGui::Unindent();
    }

    if (changed) NotifySettingsChanged();
#endif
}

void XROverlayUI::RenderComfortSettings() {
#ifdef HAS_IMGUI
    ImGui::Text("Comfort Settings");
    ImGui::Spacing();

    bool changed = false;

    changed |= ImGui::Checkbox("Snap Turning", &settings_.snapTurning);
    ImGui::SetItemTooltip("Use discrete turn angles instead of smooth turning (default ON - comfort safety)");

    if (settings_.snapTurning) {
        ImGui::Indent();
        changed |= ImGui::SliderFloat("Snap Angle", &settings_.snapTurnAngle, 15.0f, 90.0f, "%.0f deg");
        ImGui::Unindent();
    } else {
        ImGui::Indent();
        changed |= ImGui::SliderFloat("Smooth Turn Speed", &settings_.smoothTurnSpeed, 30.0f, 360.0f, "%.0f deg/s");
        ImGui::SetItemTooltip("Continuous turn speed (right stick). Smooth turning is the most common sickness trigger - prefer snap turn.");
        ImGui::Unindent();
    }
    ImGui::TextDisabled("Quick recenter: right stick click");

    ImGui::Separator();

    changed |= ImGui::Checkbox("Vignette (Tunnel Vision)", &settings_.vignetteEnabled);
    ImGui::SetItemTooltip("Reduce peripheral vision during movement to reduce motion sickness (default ON). Fades in with locomotion, off while stationary.");

    if (settings_.vignetteEnabled) {
        ImGui::Indent();
        changed |= ImGui::SliderFloat("Vignette Intensity", &settings_.vignetteIntensity, 0.1f, 1.0f, "%.2f");
        ImGui::Unindent();
    }

    ImGui::Separator();

    changed |= ImGui::Checkbox("Vehicle Horizon Lock", &settings_.vehicleHorizonLock);
    ImGui::SetItemTooltip("Suppress vehicle pitch/roll from rotating the VR view (default ON). Yaw passes through. Requires VR decoupling.");
    ImGui::Text("VR Decoupling");
    ImGui::SetItemTooltip("Separate VR head rotation from game camera rotation");

    changed |= ImGui::Checkbox("Enable Decoupling", &settings_.decouplingEnabled);
    ImGui::SetItemTooltip("VR head looks around independently from game camera direction");

    if (settings_.decouplingEnabled) {
        ImGui::Indent();

        const char* decouplingModes[] = {"Always", "Only When Aiming", "Never"};
        int mode = settings_.decouplingMode;
        if (ImGui::Combo("Mode", &mode, decouplingModes, IM_ARRAYSIZE(decouplingModes))) {
            settings_.decouplingMode = mode;
            changed = true;
        }
        ImGui::SetItemTooltip("When to apply VR head decoupling from game camera");

        changed |= ImGui::SliderFloat("Max Pitch", &settings_.decouplingMaxPitch, 30.0f, 90.0f, "%.0f deg");
        ImGui::SetItemTooltip("Maximum up/down look angle for VR head");

        changed |= ImGui::SliderFloat("Max Yaw", &settings_.decouplingMaxYaw, 30.0f, 180.0f, "%.0f deg");
        ImGui::SetItemTooltip("Maximum left/right look angle for VR head");

        changed |= ImGui::SliderFloat("Aim Cone", &settings_.decouplingAimCone, 10.0f, 60.0f, "%.0f deg");
        ImGui::SetItemTooltip("Cone limit when aiming (semi-libre mode)");

        ImGui::Unindent();
    }

    ImGui::Separator();
    ImGui::Text("Cutscene Handling");

    const char* cutsceneModes[] = {"Normal (VR Tracking)", "Virtual Screen (Cinema)", "Unlock (Free Look)"};
    int cutsceneMode = settings_.cutsceneMode;
    if (ImGui::Combo("Cutscene Mode", &cutsceneMode, cutsceneModes, IM_ARRAYSIZE(cutsceneModes))) {
        settings_.cutsceneMode = cutsceneMode;
        changed = true;
    }
    ImGui::SetItemTooltip("How to handle cutscenes in VR");

    if (settings_.cutsceneMode == 1) { // Virtual Screen mode
        ImGui::Indent();

        changed |= ImGui::SliderFloat("Screen Distance", &settings_.cutsceneScreenDistance, 1.0f, 10.0f, "%.1f m");
        ImGui::SetItemTooltip("Distance of the virtual cinema screen");

        changed |= ImGui::SliderFloat("Screen Scale", &settings_.cutsceneScreenScale, 1.0f, 6.0f, "%.1f");
        ImGui::SetItemTooltip("Size of the virtual cinema screen");

        changed |= ImGui::SliderFloat("Screen Curve", &settings_.cutsceneScreenCurve, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("0 = flat screen, 1 = curved screen");

        ImGui::Unindent();
    }

    if (changed) NotifySettingsChanged();
#endif
}

void XROverlayUI::RenderPerformanceSettings() {
#ifdef HAS_IMGUI
    ImGui::Text("Performance Settings");
    ImGui::Spacing();

    // Live frametime stats (PerfStats, Phase 8 instrumentation). Percentile
    // evidence matters: p99/p99.9, never averages.
    {
        Perf::PerfStats::Snapshot snap = Perf::PerfStats::Get().GetSnapshot();
        ImGui::Text("Live Frame Stats");
        if (snap.totalFrames > 0) {
            ImGui::BulletText("FPS: %.1f", snap.fps);
            ImGui::BulletText("Frametime p50:  %.2f ms", snap.p50Ms);
            ImGui::BulletText("Frametime p99:  %.2f ms", snap.p99Ms);
            ImGui::BulletText("Frametime p99.9: %.2f ms", snap.p999Ms);
            ImGui::BulletText("Dropped frames (est): %u", snap.droppedEstimate);
            ImGui::BulletText("Engine avg: blit %.2f ms, submit %.2f ms, camera %.2f ms",
                              snap.blitAvgMs, snap.submitAvgMs, snap.cameraWriteAvgMs);
        } else {
            ImGui::TextDisabled("No frames recorded yet.");
        }
        ImGui::TextDisabled("F11 exports gtavr_perf.csv on demand.");
        ImGui::Separator();
    }

    bool changed = false;

    int runtime = VR::GetRuntimeStats().activeRuntime.load();
    bool openxrRuntime = (runtime == 2);
    // OpenXR has no consumer for this toggle (only the OpenVR backend writes
    // it through to SteamVR) - grey it out instead of letting users think it
    // works on the OpenXR runtime.
    if (openxrRuntime) ImGui::BeginDisabled();
    changed |= ImGui::Checkbox("Async Reprojection", &settings_.asyncReprojection);
    if (openxrRuntime) ImGui::EndDisabled();
    ImGui::SetItemTooltip("Enable runtime reprojection for smoother visuals at lower framerates (OpenVR only)");
    if (openxrRuntime) {
        ImGui::TextDisabled("Informational on OpenXR: the runtime manages reprojection itself.");
    }

    changed |= ImGui::SliderFloat("Render Scale", &settings_.renderScale, 0.5f, 2.0f, "%.2f");
    ImGui::SetItemTooltip("Adjust render resolution (lower = better performance, higher = sharper)");
    ImGui::BeginDisabled();
    ImGui::SliderFloat("Game Resolution Scale", &settings_.gameResolutionScale, 0.5f, 2.0f, "%.2f");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Live game backbuffer resizing is disabled for stability; adjust GTA V resolution in-game instead.");
    ImGui::TextDisabled("Game Resolution Scale is currently disabled to avoid DXGI swapchain crashes.");

    ImGui::Separator();
    ImGui::Text("Stereo Reprojection (Fake 3D)");
    if (settings_.stereoMode != 0) {
        ImGui::TextDisabled("Note: Alternate-eye stereo overrides reprojection.");
    }

    changed |= ImGui::Checkbox("Enable Depth Reprojection", &settings_.reprojectionEnabled);
    ImGui::SetItemTooltip("Generate a second eye view using the game's depth buffer");

    if (settings_.reprojectionEnabled) {
        ImGui::Indent();
        changed |= ImGui::SliderFloat("Reprojection IPD (m)", &settings_.reprojectionIPD, 0.0f, 0.12f, "%.3f");
        ImGui::SetItemTooltip("Virtual IPD scaling used by the reprojection shader");

        changed |= ImGui::SliderFloat("Depth Scale", &settings_.reprojectionDepthScale, 0.0f, 0.1f, "%.3f");
        ImGui::SetItemTooltip("Controls how much depth influences stereo offset");

        changed |= ImGui::SliderFloat("Depth Bias", &settings_.reprojectionDepthBias, -1.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("Bias depth sampling to reduce near-plane artifacts");

        changed |= ImGui::Checkbox("Invert Depth", &settings_.reprojectionInvertDepth);
        ImGui::SetItemTooltip("Toggle if the depth buffer is reversed");
        ImGui::Unindent();
    }

    ImGui::Separator();
    ImGui::Text("Advanced Features");
    ImGui::BulletText("Optical Flow (ASW): %s",
                      HasOpticalFlow() ? "Detected (integration pending)" : "Not detected");
    ImGui::BulletText("DLSS/NGX: %s",
                      HasNgx() ? "Detected (integration pending)" : "Not detected");
    ImGui::TextDisabled("Full integration requires NVIDIA SDKs.");

    if (changed) NotifySettingsChanged();
#endif
}

void XROverlayUI::RenderControlsSettings() {
#ifdef HAS_IMGUI
    ImGui::Text("Control Settings");
    ImGui::Spacing();

    bool changed = false;

    changed |= ImGui::Checkbox("Swap Hands", &settings_.swapHands);
    ImGui::SetItemTooltip("Swap left and right controller functions");

    ImGui::Separator();

    changed |= ImGui::SliderFloat("Trigger Threshold", &settings_.triggerThreshold, 0.1f, 0.9f, "%.2f");
    ImGui::SetItemTooltip("How far trigger needs to be pressed to register as 'pressed'");

    changed |= ImGui::SliderFloat("Grip Threshold", &settings_.gripThreshold, 0.1f, 0.9f, "%.2f");
    ImGui::SetItemTooltip("How far grip needs to be pressed to register as 'pressed'");

    ImGui::Separator();
    ImGui::Text("Head Look (Right Stick Emulation)");

    changed |= ImGui::Checkbox("Enable Head Look", &settings_.headLookEnabled);
    ImGui::SetItemTooltip("Map headset rotation to camera look input (locks view to headset)");

    if (settings_.headLookEnabled) {
        ImGui::Indent();
        changed |= ImGui::SliderFloat("Max Angle", &settings_.headLookMaxAngle, 5.0f, 60.0f, "%.0f deg");
        changed |= ImGui::SliderFloat("Deadzone", &settings_.headLookDeadzone, 0.0f, 10.0f, "%.1f deg");
        changed |= ImGui::SliderFloat("Sensitivity", &settings_.headLookSensitivity, 10.0f, 1200.0f, "%.0f");
        changed |= ImGui::Checkbox("Invert Y", &settings_.headLookInvertY);
        ImGui::TextDisabled("Note: Head Look locks the VR view while still rotating the game camera.");
        ImGui::Unindent();
    }

    ImGui::Separator();
    ImGui::Text("Overlay Settings");

    changed |= ImGui::SliderFloat("Overlay Distance", &settings_.overlayDistance, 0.5f, 3.0f, "%.1f m");
    changed |= ImGui::SliderFloat("Overlay Scale", &settings_.overlayScale, 0.5f, 2.0f, "%.1f");
    changed |= ImGui::SliderFloat("Overlay Opacity", &settings_.overlayOpacity, 0.3f, 1.0f, "%.2f");

    if (changed) NotifySettingsChanged();
#endif
}

void XROverlayUI::RenderDebugInfo() {
#ifdef HAS_IMGUI
    ImGui::Text("Debug Information");
    ImGui::Spacing();

    bool changed = false;
    changed |= ImGui::Checkbox("Show Debug Info", &settings_.showDebugInfo);
    changed |= ImGui::Checkbox("Show Controller Models", &settings_.showControllerModels);
    if (changed) NotifySettingsChanged();

    ImGui::Separator();
    if (!settings_.showDebugInfo) {
        ImGui::TextDisabled("Enable 'Show Debug Info' to display runtime stats.");
        return;
    }

    // Display runtime info
    ImGui::Text("VR Runtime Info:");
    ImGui::BulletText("OpenXR Active");
    // Could add more runtime stats here

    ImGui::Separator();

    // Controller tracking
    if (settings_.showControllerModels) {
        if (auto* backend = VR::VRManager::Get().GetBackend()) {
            const auto& left = backend->GetControllerState(VR::Hand::Left);
            const auto& right = backend->GetControllerState(VR::Hand::Right);
            ImGui::Text("Controller Tracking:");
            ImGui::BulletText("Left: %s (%.2f, %.2f, %.2f)",
                              left.isTracked ? "Tracked" : "Not tracked",
                              left.position.x, left.position.y, left.position.z);
            ImGui::BulletText("Right: %s (%.2f, %.2f, %.2f)",
                              right.isTracked ? "Tracked" : "Not tracked",
                              right.position.x, right.position.y, right.position.z);
            ImGui::Separator();
        }
    } else {
        ImGui::TextDisabled("Controller models hidden.");
        ImGui::Separator();
    }

    // Frame stats
    ImGui::Text("Frame Statistics:");
    float fps = VR::GetRuntimeStats().fps.load();
    if (fps > 0.0f) {
        ImGui::BulletText("FPS: %.1f", fps);
        ImGui::BulletText("Frame Time: %.2f ms", 1000.0f / fps);
    } else {
        ImGui::BulletText("FPS: --");
        ImGui::BulletText("Frame Time: -- ms");
    }

    auto& stats = VR::GetRuntimeStats();
    bool camReady = stats.cameraHookReady.load();
    bool configLoaded = stats.cameraConfigLoaded.load();
    ImGui::BulletText("Camera Config: %s", configLoaded ? "Loaded" : "Missing");
    ImGui::BulletText("Camera Hook: %s", camReady ? "Ready" : "Waiting");
    uint64_t matrixAddr = stats.cameraMatrixAddress.load();
    if (matrixAddr) {
        bool writable = stats.cameraMatrixWritable.load();
        ImGui::BulletText("Matrix Addr: 0x%llX (%s)",
                          static_cast<unsigned long long>(matrixAddr),
                          writable ? "writable" : "not writable");
    }
    uint32_t camHash = stats.activeCameraHash.load();
    uint32_t camHashName = stats.activeCameraHashName.load();
    int fovOffset = stats.activeFovOffset.load();
    float activeFov = stats.activeFov.load();
    if (camHash || camHashName) {
        ImGui::BulletText("Active Camera Hash: 0x%08X", camHash);
        ImGui::BulletText("Active Camera HashName: 0x%08X", camHashName);
        ImGui::BulletText("FOV Offset: %d", fovOffset);
        ImGui::BulletText("Active FOV: %.2f", activeFov);
    }
#endif
}

//-----------------------------------------------------------------------------
// Settings Persistence
//-----------------------------------------------------------------------------

bool XROverlayUI::LoadSettings(const char* filename) {
    std::string path = ResolveSettingsPath(filename);
    std::ifstream file(path);
    if (!file.is_open()) {
        LOGSTRF("XROverlayUI: Could not open settings file: %s\n", path.c_str());
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim whitespace (both ends - CRLF files otherwise parse every
        // bool key as false because the value is "1\r").
        while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
        while (!value.empty() && isspace((unsigned char)value.front())) value.erase(0, 1);
        while (!value.empty() && isspace((unsigned char)value.back())) value.pop_back();

        // Parse values
        if (key == "worldScale") settings_.worldScale = std::stof(value);
        else if (key == "playerHeight") settings_.playerHeight = std::stof(value);
        else if (key == "cameraOffsetX") settings_.cameraOffsetX = std::stof(value);
        else if (key == "cameraOffsetY") settings_.cameraOffsetY = std::stof(value);
        else if (key == "cameraOffsetZ") settings_.cameraOffsetZ = std::stof(value);
        else if (key == "imageOffsetX") settings_.imageOffsetX = std::stof(value);
        else if (key == "imageOffsetY") settings_.imageOffsetY = std::stof(value);
        else if (key == "imageScale") settings_.imageScale = std::stof(value);
        else if (key == "fovOverride") settings_.fovOverride = (value == "1" || value == "true");
        else if (key == "fovPerType") settings_.fovPerType = (value == "1" || value == "true");
        else if (key == "fovGlobal") settings_.fovGlobal = std::stof(value);
        else if (key == "fovFpPed") settings_.fovFpPed = std::stof(value);
        else if (key == "fovTpPed") settings_.fovTpPed = std::stof(value);
        else if (key == "fovTpAim") settings_.fovTpAim = std::stof(value);
        else if (key == "fovFpVehicle") settings_.fovFpVehicle = std::stof(value);
        else if (key == "fovTpVehicle") settings_.fovTpVehicle = std::stof(value);
        else if (key == "fovOffsetOverride") settings_.fovOffsetOverride = (value == "1" || value == "true");
        else if (key == "fovManualOffset") settings_.fovManualOffset = std::stoi(value, nullptr, 0);
        else if (key == "stereoMode") settings_.stereoMode = std::stoi(value);
        else if (key == "stereoIPD") settings_.stereoIPD = std::stof(value);
        else if (key == "ipdAuto") settings_.ipdAuto = (value == "1" || value == "true");
        else if (key == "headTracking") settings_.headTracking = (value == "1" || value == "true");
        else if (key == "positionTracking") settings_.positionTracking = (value == "1" || value == "true");
        else if (key == "snapTurning") settings_.snapTurning = (value == "1" || value == "true");
        else if (key == "snapTurnAngle") settings_.snapTurnAngle = std::stof(value);
        else if (key == "vignetteEnabled") settings_.vignetteEnabled = (value == "1" || value == "true");
        else if (key == "vignetteIntensity") settings_.vignetteIntensity = std::stof(value);
        else if (key == "vehicleHorizonLock") settings_.vehicleHorizonLock = (value == "1" || value == "true");
        else if (key == "smoothTurnSpeed") settings_.smoothTurnSpeed = std::stof(value);
        else if (key == "asyncReprojection") settings_.asyncReprojection = (value == "1" || value == "true");
        else if (key == "renderScale") settings_.renderScale = std::stof(value);
        else if (key == "gameResolutionScale") settings_.gameResolutionScale = std::stof(value);
        else if (key == "desktopMirrorSyncOverride") settings_.desktopMirrorSyncOverride = (value == "1" || value == "true");
        else if (key == "reprojectionEnabled") settings_.reprojectionEnabled = (value == "1" || value == "true");
        else if (key == "reprojectionIPD") settings_.reprojectionIPD = std::stof(value);
        else if (key == "reprojectionDepthScale") settings_.reprojectionDepthScale = std::stof(value);
        else if (key == "reprojectionDepthBias") settings_.reprojectionDepthBias = std::stof(value);
        else if (key == "reprojectionInvertDepth") settings_.reprojectionInvertDepth = (value == "1" || value == "true");
        else if (key == "swapHands") settings_.swapHands = (value == "1" || value == "true");
        else if (key == "triggerThreshold") settings_.triggerThreshold = std::stof(value);
        else if (key == "gripThreshold") settings_.gripThreshold = std::stof(value);
        else if (key == "headLookEnabled") settings_.headLookEnabled = (value == "1" || value == "true");
        else if (key == "headLookMaxAngle") settings_.headLookMaxAngle = std::stof(value);
        else if (key == "headLookDeadzone") settings_.headLookDeadzone = std::stof(value);
        else if (key == "headLookSensitivity") settings_.headLookSensitivity = std::stof(value);
        else if (key == "headLookInvertY") settings_.headLookInvertY = (value == "1" || value == "true");
        else if (key == "overlayDistance") settings_.overlayDistance = std::stof(value);
        else if (key == "overlayScale") settings_.overlayScale = std::stof(value);
        else if (key == "overlayOpacity") settings_.overlayOpacity = std::stof(value);
        else if (key == "showDebugInfo") settings_.showDebugInfo = (value == "1" || value == "true");
        else if (key == "showControllerModels") settings_.showControllerModels = (value == "1" || value == "true");
        // Decoupling settings
        else if (key == "decouplingEnabled") settings_.decouplingEnabled = (value == "1" || value == "true");
        else if (key == "decouplingMode") settings_.decouplingMode = std::stoi(value);
        else if (key == "decouplingMaxPitch") settings_.decouplingMaxPitch = std::stof(value);
        else if (key == "decouplingMaxYaw") settings_.decouplingMaxYaw = std::stof(value);
        else if (key == "decouplingAimCone") settings_.decouplingAimCone = std::stof(value);
        // Cutscene settings
        else if (key == "cutsceneMode") settings_.cutsceneMode = std::stoi(value);
        else if (key == "cutsceneScreenDistance") settings_.cutsceneScreenDistance = std::stof(value);
        else if (key == "cutsceneScreenScale") settings_.cutsceneScreenScale = std::stof(value);
        else if (key == "cutsceneScreenCurve") settings_.cutsceneScreenCurve = std::stof(value);
    }

    LOGSTRF("XROverlayUI: Loaded settings from %s\n", path.c_str());
    NotifySettingsChanged();
    return true;
}

bool XROverlayUI::SaveSettings(const char* filename) {
    std::string path = ResolveSettingsPath(filename);
    std::ofstream file(path);
    if (!file.is_open()) {
        LOGSTRF("XROverlayUI: Could not save settings to: %s\n", path.c_str());
        return false;
    }

    file << "# GTA VR Settings\n";
    file << "# Auto-generated - edit with care\n\n";

    file << "[World]\n";
    file << "worldScale=" << settings_.worldScale << "\n";
    file << "playerHeight=" << settings_.playerHeight << "\n";
    file << "cameraOffsetX=" << settings_.cameraOffsetX << "\n";
    file << "cameraOffsetY=" << settings_.cameraOffsetY << "\n";
    file << "cameraOffsetZ=" << settings_.cameraOffsetZ << "\n\n";
    file << "imageOffsetX=" << settings_.imageOffsetX << "\n";
    file << "imageOffsetY=" << settings_.imageOffsetY << "\n";
    file << "imageScale=" << settings_.imageScale << "\n\n";

    file << "[CameraFov]\n";
    file << "fovOverride=" << (settings_.fovOverride ? "1" : "0") << "\n";
    file << "fovPerType=" << (settings_.fovPerType ? "1" : "0") << "\n";
    file << "fovGlobal=" << settings_.fovGlobal << "\n";
    file << "fovFpPed=" << settings_.fovFpPed << "\n";
    file << "fovTpPed=" << settings_.fovTpPed << "\n";
    file << "fovTpAim=" << settings_.fovTpAim << "\n";
    file << "fovFpVehicle=" << settings_.fovFpVehicle << "\n";
    file << "fovTpVehicle=" << settings_.fovTpVehicle << "\n";
    file << "fovOffsetOverride=" << (settings_.fovOffsetOverride ? "1" : "0") << "\n";
    file << "fovManualOffset=" << settings_.fovManualOffset << "\n\n";

    file << "[Stereo]\n";
    file << "stereoMode=" << settings_.stereoMode << "\n";
    file << "stereoIPD=" << settings_.stereoIPD << "\n";
    file << "ipdAuto=" << (settings_.ipdAuto ? 1 : 0) << "\n";
    file << "headTracking=" << (settings_.headTracking ? "1" : "0") << "\n";
    file << "positionTracking=" << (settings_.positionTracking ? "1" : "0") << "\n\n";

    file << "[Comfort]\n";
    file << "snapTurning=" << (settings_.snapTurning ? "1" : "0") << "\n";
    file << "snapTurnAngle=" << settings_.snapTurnAngle << "\n";
    file << "vignetteEnabled=" << (settings_.vignetteEnabled ? "1" : "0") << "\n";
    file << "vignetteIntensity=" << settings_.vignetteIntensity << "\n";
    file << "vehicleHorizonLock=" << (settings_.vehicleHorizonLock ? "1" : "0") << "\n";
    file << "smoothTurnSpeed=" << settings_.smoothTurnSpeed << "\n\n";

    file << "[Performance]\n";
    file << "asyncReprojection=" << (settings_.asyncReprojection ? "1" : "0") << "\n";
    file << "renderScale=" << settings_.renderScale << "\n\n";
    file << "gameResolutionScale=" << settings_.gameResolutionScale << "\n\n";
    file << "desktopMirrorSyncOverride=" << (settings_.desktopMirrorSyncOverride ? "1" : "0") << "\n\n";
    file << "reprojectionEnabled=" << (settings_.reprojectionEnabled ? "1" : "0") << "\n";
    file << "reprojectionIPD=" << settings_.reprojectionIPD << "\n";
    file << "reprojectionDepthScale=" << settings_.reprojectionDepthScale << "\n";
    file << "reprojectionDepthBias=" << settings_.reprojectionDepthBias << "\n";
    file << "reprojectionInvertDepth=" << (settings_.reprojectionInvertDepth ? "1" : "0") << "\n\n";

    file << "[Controls]\n";
    file << "swapHands=" << (settings_.swapHands ? "1" : "0") << "\n";
    file << "triggerThreshold=" << settings_.triggerThreshold << "\n";
    file << "gripThreshold=" << settings_.gripThreshold << "\n";
    file << "headLookEnabled=" << (settings_.headLookEnabled ? "1" : "0") << "\n";
    file << "headLookMaxAngle=" << settings_.headLookMaxAngle << "\n";
    file << "headLookDeadzone=" << settings_.headLookDeadzone << "\n";
    file << "headLookSensitivity=" << settings_.headLookSensitivity << "\n";
    file << "headLookInvertY=" << (settings_.headLookInvertY ? "1" : "0") << "\n\n";

    file << "[Overlay]\n";
    file << "overlayDistance=" << settings_.overlayDistance << "\n";
    file << "overlayScale=" << settings_.overlayScale << "\n";
    file << "overlayOpacity=" << settings_.overlayOpacity << "\n\n";

    file << "[Decoupling]\n";
    file << "decouplingEnabled=" << (settings_.decouplingEnabled ? "1" : "0") << "\n";
    file << "decouplingMode=" << settings_.decouplingMode << "\n";
    file << "decouplingMaxPitch=" << settings_.decouplingMaxPitch << "\n";
    file << "decouplingMaxYaw=" << settings_.decouplingMaxYaw << "\n";
    file << "decouplingAimCone=" << settings_.decouplingAimCone << "\n\n";

    file << "[Cutscene]\n";
    file << "cutsceneMode=" << settings_.cutsceneMode << "\n";
    file << "cutsceneScreenDistance=" << settings_.cutsceneScreenDistance << "\n";
    file << "cutsceneScreenScale=" << settings_.cutsceneScreenScale << "\n";
    file << "cutsceneScreenCurve=" << settings_.cutsceneScreenCurve << "\n\n";

    file << "[Debug]\n";
    file << "showDebugInfo=" << (settings_.showDebugInfo ? "1" : "0") << "\n";
    file << "showControllerModels=" << (settings_.showControllerModels ? "1" : "0") << "\n";

    LOGSTRF("XROverlayUI: Saved settings to %s\n", path.c_str());
    return true;
}

void XROverlayUI::NotifySettingsChanged() {
    if (on_settings_changed_) {
        on_settings_changed_(settings_);
    }
}

bool XROverlayUI::ConsumeRecenterRequest() {
    bool requested = recenter_requested_;
    recenter_requested_ = false;
    return requested;
}

} // namespace XR
} // namespace OVRInject
