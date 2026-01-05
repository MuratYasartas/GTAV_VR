/*
 * GTA5VR - DLL Entry Point
 *
 * This is the main entry point for the GTA5VR.dll.
 * It initializes the VR system when the DLL is loaded into GTA V.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <thread>
#include <chrono>

#include "core/VRCore.h"
#include "core/VRConfig.h"
#include "core/Logger.h"
#include "core/MemoryManager.h"
#include "injection/DX11Hook.h"

// Forward declarations
bool InitializeVRMod();
void ShutdownVRMod();
void MainThread();

// Global state
static HMODULE g_hModule = nullptr;
static std::thread g_mainThread;
static bool g_running = false;
static bool g_initialized = false;

// Console for debugging
#ifdef _DEBUG
void CreateDebugConsole() {
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    SetConsoleTitleA("GTA5VR Debug Console");
    std::cout << "GTA5VR Debug Console Initialized" << std::endl;
}

void DestroyDebugConsole() {
    FreeConsole();
}
#endif

// Main initialization function
bool InitializeVRMod() {
    using namespace GTA5VR;

    // Get module path for config and log files
    char dllPath[MAX_PATH];
    GetModuleFileNameA(g_hModule, dllPath, MAX_PATH);
    std::string basePath = std::string(dllPath);
    size_t lastSlash = basePath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        basePath = basePath.substr(0, lastSlash + 1);
    }

    // Initialize logger
    std::string logPath = basePath + "GTA5VR.log";
    if (!Logger::GetInstance().Initialize(logPath, LogLevel::Verbose)) {
        MessageBoxA(nullptr, "Failed to initialize logger", "GTA5VR Error", MB_OK | MB_ICONERROR);
        return false;
    }

    LOG_INFO("==============================================");
    LOG_INFO("GTA5VR Mod v1.0.0 Starting...");
    LOG_INFO("==============================================");

    // Load configuration
    std::string configPath = basePath + "GTA5VR.ini";
    if (!VRConfig::GetInstance().Load(configPath)) {
        LOG_WARNING("Failed to load config from " + configPath + ", using defaults");
        VRConfig::GetInstance().ResetToDefaults();
    }

    auto& config = VRConfig::GetInstance().GetConfig();

    // Check if mod is enabled
    if (!config.modEnabled) {
        LOG_INFO("Mod is disabled in config, exiting");
        return false;
    }

    // Initialize memory manager
    if (!MemoryManager::GetInstance().Initialize()) {
        LOG_ERROR("Failed to initialize memory manager");
        return false;
    }

    // Wait for game to fully initialize
    LOG_INFO("Waiting for game initialization...");
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Initialize DX11 hooks
    LOG_INFO("Installing DirectX hooks...");
    auto& dx11Hook = DX11Hook::GetInstance();
    if (!dx11Hook.Initialize()) {
        LOG_ERROR("Failed to initialize DX11 hooks");
        return false;
    }

    // Install hooks
    if (!dx11Hook.InstallPresentHook()) {
        LOG_ERROR("Failed to install Present hook");
        return false;
    }

    LOG_INFO("DX11 hooks installed successfully");

    // Set up present callback for VR rendering
    dx11Hook.SetPresentCallback([](IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
        static bool vrInitialized = false;
        static ID3D11Device* device = nullptr;
        static ID3D11DeviceContext* context = nullptr;
        static HWND gameWindow = nullptr;

        // One-time VR initialization on first Present call
        if (!vrInitialized) {
            // Get D3D11 device from swapchain
            if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device))) {
                device->GetImmediateContext(&context);

                // Get game window
                DXGI_SWAP_CHAIN_DESC desc;
                swapChain->GetDesc(&desc);
                gameWindow = desc.OutputWindow;

                // Initialize VR core
                auto& vrCore = GTA5VR::VRCore::GetInstance();
                if (vrCore.Initialize(gameWindow, device, context)) {
                    LOG_INFO("VR system initialized successfully!");

                    // Apply config settings
                    vrCore.ApplyConfigChanges();

                    vrInitialized = true;
                } else {
                    LOG_ERROR("Failed to initialize VR system");
                }

                // Release our reference (VRCore keeps its own)
                device->Release();
                context->Release();
            }
        }

        // Main VR frame loop
        if (vrInitialized && g_running) {
            auto& vrCore = GTA5VR::VRCore::GetInstance();

            if (vrCore.IsInitialized()) {
                // Begin VR frame
                vrCore.BeginFrame();

                // Render both eyes
                for (uint32_t eye = 0; eye < 2; ++eye) {
                    vrCore.SetEyeRenderTarget(eye);

                    // The game will render to our redirected render target
                    // TODO: Implement actual scene rendering redirection
                }

                // End VR frame
                vrCore.EndFrame();

                // Submit to VR compositor
                vrCore.SubmitFrame();
            }
        }
    });

    g_initialized = true;
    LOG_INFO("GTA5VR initialization complete!");
    LOG_INFO("Press F12 to recenter view");
    LOG_INFO("Press INSERT to open settings menu");

    return true;
}

// Shutdown function
void ShutdownVRMod() {
    using namespace GTA5VR;

    if (!g_initialized) {
        return;
    }

    LOG_INFO("Shutting down GTA5VR...");

    // Shutdown VR core
    VRCore::GetInstance().Shutdown();

    // Remove DX11 hooks
    DX11Hook::GetInstance().Shutdown();

    // Shutdown memory manager
    MemoryManager::GetInstance().Shutdown();

    // Save config
    VRConfig::GetInstance().Save();

    LOG_INFO("GTA5VR shutdown complete");
    Logger::GetInstance().Shutdown();

    g_initialized = false;
}

// Main thread function
void MainThread() {
    // Wait a bit for game to stabilize
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Initialize the mod
    if (!InitializeVRMod()) {
        LOG_ERROR("VR mod initialization failed");
        g_running = false;
        return;
    }

    // Main loop - process hotkeys and updates
    while (g_running) {
        // Check for hotkeys
        if (GetAsyncKeyState(VK_F12) & 1) {
            // Recenter view
            GTA5VR::VRCore::GetInstance().Recenter();
        }

        if (GetAsyncKeyState(VK_INSERT) & 1) {
            // Toggle settings menu
            auto* overlay = GTA5VR::VRCore::GetInstance().GetOverlay();
            if (overlay) {
                if (overlay->IsSettingsMenuVisible()) {
                    overlay->HideSettingsMenu();
                } else {
                    overlay->ShowSettingsMenu();
                }
            }
        }

        if (GetAsyncKeyState(VK_HOME) & 1) {
            // Toggle performance overlay
            auto* overlay = GTA5VR::VRCore::GetInstance().GetOverlay();
            if (overlay) {
                overlay->ShowPerformanceStats();
            }
        }

        if (GetAsyncKeyState(VK_END) & 1) {
            // Toggle debug mode
            auto& vrCore = GTA5VR::VRCore::GetInstance();
            vrCore.EnableDebugMode(!vrCore.IsDebugModeEnabled());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 Hz polling
    }

    // Cleanup
    ShutdownVRMod();
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            DisableThreadLibraryCalls(hModule);

            #ifdef _DEBUG
            CreateDebugConsole();
            #endif

            // Start main thread
            g_running = true;
            g_mainThread = std::thread(MainThread);
            break;

        case DLL_PROCESS_DETACH:
            // Signal thread to stop
            g_running = false;

            // Wait for thread to finish
            if (g_mainThread.joinable()) {
                g_mainThread.join();
            }

            #ifdef _DEBUG
            DestroyDebugConsole();
            #endif
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }

    return TRUE;
}

// Export for manual initialization (if needed)
extern "C" __declspec(dllexport) bool GTA5VR_Initialize() {
    return InitializeVRMod();
}

extern "C" __declspec(dllexport) void GTA5VR_Shutdown() {
    ShutdownVRMod();
}

extern "C" __declspec(dllexport) const char* GTA5VR_GetVersion() {
    return "1.0.0";
}

extern "C" __declspec(dllexport) bool GTA5VR_IsInitialized() {
    return g_initialized;
}
