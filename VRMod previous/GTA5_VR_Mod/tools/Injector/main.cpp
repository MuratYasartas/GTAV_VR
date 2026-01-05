/*
 * GTA5VR DLL Injector
 *
 * A complete DLL injection tool for the GTA 5 VR Mod.
 * Supports automatic process detection, verification, and detailed logging.
 *
 * Usage:
 *   GTA5VR_Injector.exe [options]
 *
 * Options:
 *   --auto           Auto-inject when GTA5.exe is detected
 *   --pid <id>       Inject into specific process ID
 *   --dll <path>     Use custom DLL path
 *   --verify         Verify installation without injecting
 *   --verbose        Show detailed output
 *   --help           Show help message
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "Psapi.lib")

// Console colors
namespace Color {
    void SetGreen() { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY); }
    void SetRed() { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY); }
    void SetYellow() { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); }
    void SetCyan() { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY); }
    void SetWhite() { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); }
    void SetReset() { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); }
}

// Injector class
class GTA5VRInjector {
public:
    GTA5VRInjector() = default;
    ~GTA5VRInjector() = default;

    // Parse command line arguments
    bool ParseArgs(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--auto" || arg == "-a") {
                m_autoMode = true;
            }
            else if (arg == "--pid" || arg == "-p") {
                if (i + 1 < argc) {
                    m_targetPID = std::stoul(argv[++i]);
                }
            }
            else if (arg == "--dll" || arg == "-d") {
                if (i + 1 < argc) {
                    m_dllPath = argv[++i];
                }
            }
            else if (arg == "--verify" || arg == "-v") {
                m_verifyOnly = true;
            }
            else if (arg == "--verbose") {
                m_verbose = true;
            }
            else if (arg == "--timeout" || arg == "-t") {
                if (i + 1 < argc) {
                    m_timeout = std::stoul(argv[++i]);
                }
            }
            else if (arg == "--sysinfo") {
                m_showSysInfo = true;
            }
            else if (arg == "--help" || arg == "-h" || arg == "/?") {
                ShowHelp();
                return false;
            }
            else {
                std::cerr << "Unknown argument: " << arg << std::endl;
                return false;
            }
        }
        return true;
    }

    // Main run function
    int Run() {
        ShowBanner();

        // Show system info if requested
        if (m_showSysInfo) {
            ShowSystemInfo();
            return 0;
        }

        // Find DLL if not specified
        if (m_dllPath.empty()) {
            m_dllPath = FindDLL();
            if (m_dllPath.empty()) {
                Error("Could not find GTA5VR.dll");
                Error("Please ensure the DLL is in the same folder as this injector");
                return 1;
            }
        }

        // Verify DLL exists
        if (!std::filesystem::exists(m_dllPath)) {
            Error("DLL not found: " + m_dllPath);
            return 1;
        }

        Info("Found DLL: " + m_dllPath);

        // Verify only mode
        if (m_verifyOnly) {
            return VerifyInstallation() ? 0 : 1;
        }

        // Auto mode - wait for process
        if (m_autoMode) {
            Info("Auto mode enabled. Waiting for GTA5.exe...");
            if (!WaitForProcess("GTA5.exe", 300000)) { // 5 minute timeout
                Error("Timeout waiting for GTA5.exe");
                return 1;
            }
        }

        // Find target process
        if (m_targetPID == 0) {
            m_targetPID = FindProcess("GTA5.exe");
            if (m_targetPID == 0) {
                Error("GTA5.exe not found. Is the game running?");
                Info("Start GTA V first, then run this injector.");
                return 1;
            }
        }

        Success("Found GTA5.exe (PID: " + std::to_string(m_targetPID) + ")");

        // Check if already injected
        if (IsAlreadyInjected()) {
            Warning("GTA5VR.dll appears to already be loaded!");
            std::cout << "Continue anyway? (y/n): ";
            char c;
            std::cin >> c;
            if (c != 'y' && c != 'Y') {
                return 0;
            }
        }

        // Perform injection
        Info("Injecting GTA5VR.dll...");

        if (!InjectDLL()) {
            Error("Injection failed!");
            ShowTroubleshootingTips();
            return 1;
        }

        Success("Injection successful!");
        Info("Put on your VR headset and enjoy!");
        Info("Press F12 to recenter, INSERT for settings menu.");

        return 0;
    }

private:
    // Display functions
    void ShowBanner() {
        Color::SetCyan();
        std::cout << R"(
  _____ _____  _    ____   __     ______
 / ____|_   _|/ \  | ___| \ \   / /  _ \
| |  ___ | | / _ \ |___ \  \ \ / /| |_) |
| | |_ | | |/ ___ \ ___) |  \ V / |  _ <
| |__| | |_/_/   \_\____/    \_/  |_| \_\
 \_____|___|
)" << std::endl;
        Color::SetWhite();
        std::cout << "  GTA 5 VR Mod Injector v1.0.0" << std::endl;
        std::cout << "  ==============================" << std::endl << std::endl;
        Color::SetReset();
    }

    void ShowHelp() {
        std::cout << "Usage: GTA5VR_Injector.exe [options]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --auto, -a        Auto-inject when GTA5.exe is detected\n";
        std::cout << "  --pid, -p <id>    Inject into specific process ID\n";
        std::cout << "  --dll, -d <path>  Use custom DLL path\n";
        std::cout << "  --verify, -v      Verify installation without injecting\n";
        std::cout << "  --verbose         Show detailed output\n";
        std::cout << "  --timeout <ms>    Set injection timeout (default: 10000)\n";
        std::cout << "  --sysinfo         Display system information\n";
        std::cout << "  --help, -h        Show this help message\n\n";
        std::cout << "Examples:\n";
        std::cout << "  GTA5VR_Injector.exe                  # Interactive mode\n";
        std::cout << "  GTA5VR_Injector.exe --auto           # Wait and auto-inject\n";
        std::cout << "  GTA5VR_Injector.exe --verify         # Check installation\n";
    }

    void Info(const std::string& msg) {
        Color::SetWhite();
        std::cout << "[INFO] " << msg << std::endl;
        Color::SetReset();
    }

    void Success(const std::string& msg) {
        Color::SetGreen();
        std::cout << "[OK] " << msg << std::endl;
        Color::SetReset();
    }

    void Warning(const std::string& msg) {
        Color::SetYellow();
        std::cout << "[WARN] " << msg << std::endl;
        Color::SetReset();
    }

    void Error(const std::string& msg) {
        Color::SetRed();
        std::cout << "[ERROR] " << msg << std::endl;
        Color::SetReset();
    }

    void Verbose(const std::string& msg) {
        if (m_verbose) {
            Color::SetCyan();
            std::cout << "[DEBUG] " << msg << std::endl;
            Color::SetReset();
        }
    }

    // Process functions
    DWORD FindProcess(const std::string& processName) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return 0;
        }

        PROCESSENTRY32 entry;
        entry.dwSize = sizeof(entry);

        DWORD pid = 0;

        if (Process32First(snapshot, &entry)) {
            do {
                if (_stricmp(entry.szExeFile, processName.c_str()) == 0) {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return pid;
    }

    bool WaitForProcess(const std::string& processName, DWORD timeoutMs) {
        DWORD startTime = GetTickCount();

        while (GetTickCount() - startTime < timeoutMs) {
            DWORD pid = FindProcess(processName);
            if (pid != 0) {
                m_targetPID = pid;
                // Wait a bit for process to fully initialize
                std::this_thread::sleep_for(std::chrono::seconds(3));
                return true;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "." << std::flush;
        }

        std::cout << std::endl;
        return false;
    }

    bool IsAlreadyInjected() {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, m_targetPID);
        if (!process) {
            return false;
        }

        HMODULE modules[1024];
        DWORD needed;
        bool found = false;

        if (EnumProcessModules(process, modules, sizeof(modules), &needed)) {
            for (unsigned int i = 0; i < (needed / sizeof(HMODULE)); i++) {
                char moduleName[MAX_PATH];
                if (GetModuleBaseNameA(process, modules[i], moduleName, sizeof(moduleName))) {
                    if (_stricmp(moduleName, "GTA5VR.dll") == 0) {
                        found = true;
                        break;
                    }
                }
            }
        }

        CloseHandle(process);
        return found;
    }

    // DLL functions
    std::string FindDLL() {
        // Check current directory
        std::string path = "GTA5VR.dll";
        if (std::filesystem::exists(path)) {
            return std::filesystem::absolute(path).string();
        }

        // Check executable directory
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

        path = (exeDir / "GTA5VR.dll").string();
        if (std::filesystem::exists(path)) {
            return path;
        }

        // Check parent directory
        path = (exeDir.parent_path() / "GTA5VR.dll").string();
        if (std::filesystem::exists(path)) {
            return path;
        }

        // Check bin/Release
        path = (exeDir.parent_path() / "bin" / "Release" / "GTA5VR.dll").string();
        if (std::filesystem::exists(path)) {
            return path;
        }

        return "";
    }

    // Injection
    bool InjectDLL() {
        // Open target process
        HANDLE process = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, m_targetPID);

        if (!process) {
            DWORD error = GetLastError();
            Error("Failed to open process (Error: " + std::to_string(error) + ")");
            if (error == 5) {
                Error("Access denied. Try running as Administrator.");
            }
            return false;
        }

        Verbose("Process opened successfully");

        // Get full DLL path
        std::string fullPath = std::filesystem::absolute(m_dllPath).string();
        size_t pathSize = fullPath.length() + 1;

        Verbose("Full DLL path: " + fullPath);

        // Allocate memory in target process
        LPVOID remotePath = VirtualAllocEx(process, NULL, pathSize,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (!remotePath) {
            Error("Failed to allocate memory in target process");
            CloseHandle(process);
            return false;
        }

        Verbose("Allocated remote memory at: " + std::to_string(reinterpret_cast<uintptr_t>(remotePath)));

        // Write DLL path to target process
        SIZE_T written;
        if (!WriteProcessMemory(process, remotePath, fullPath.c_str(), pathSize, &written)) {
            Error("Failed to write to target process memory");
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            CloseHandle(process);
            return false;
        }

        Verbose("Written " + std::to_string(written) + " bytes to remote process");

        // Get LoadLibraryA address
        HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
        if (!kernel32) {
            Error("Failed to get kernel32.dll handle");
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            CloseHandle(process);
            return false;
        }

        LPVOID loadLibraryAddr = (LPVOID)GetProcAddress(kernel32, "LoadLibraryA");
        if (!loadLibraryAddr) {
            Error("Failed to get LoadLibraryA address");
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            CloseHandle(process);
            return false;
        }

        Verbose("LoadLibraryA address: " + std::to_string(reinterpret_cast<uintptr_t>(loadLibraryAddr)));

        // Create remote thread to load DLL
        HANDLE thread = CreateRemoteThread(process, NULL, 0,
            (LPTHREAD_START_ROUTINE)loadLibraryAddr, remotePath, 0, NULL);

        if (!thread) {
            DWORD error = GetLastError();
            Error("Failed to create remote thread (Error: " + std::to_string(error) + ")");
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            CloseHandle(process);
            return false;
        }

        Verbose("Remote thread created successfully");

        // Wait for thread to complete
        Info("Waiting for DLL to load...");
        DWORD waitResult = WaitForSingleObject(thread, m_timeout);

        if (waitResult == WAIT_TIMEOUT) {
            Error("Timeout waiting for DLL to load");
            TerminateThread(thread, 0);
            CloseHandle(thread);
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            CloseHandle(process);
            return false;
        }

        if (waitResult != WAIT_OBJECT_0) {
            Error("Failed waiting for thread (Error: " + std::to_string(GetLastError()) + ")");
            CloseHandle(thread);
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            CloseHandle(process);
            return false;
        }

        // Check thread exit code (should be module handle if successful)
        DWORD exitCode;
        GetExitCodeThread(thread, &exitCode);

        Verbose("Thread exit code: " + std::to_string(exitCode));

        // Cleanup
        CloseHandle(thread);
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        CloseHandle(process);

        // Exit code 0 means LoadLibrary returned NULL (failed)
        if (exitCode == 0) {
            Error("LoadLibrary returned NULL - DLL failed to load");
            Error("Check that all DLL dependencies are present");
            return false;
        }

        return true;
    }

    // Verification
    bool VerifyInstallation() {
        std::cout << "\n=== Installation Verification ===" << std::endl << std::endl;

        bool allGood = true;

        // Check DLL
        std::cout << "Checking GTA5VR.dll... ";
        if (std::filesystem::exists(m_dllPath)) {
            auto size = std::filesystem::file_size(m_dllPath);
            Color::SetGreen();
            std::cout << "OK (" << size << " bytes)" << std::endl;
            Color::SetReset();
        } else {
            Color::SetRed();
            std::cout << "NOT FOUND" << std::endl;
            Color::SetReset();
            allGood = false;
        }

        // Check config
        std::string configPath = std::filesystem::path(m_dllPath).parent_path().string() + "\\GTA5VR.ini";
        std::cout << "Checking GTA5VR.ini... ";
        if (std::filesystem::exists(configPath)) {
            Color::SetGreen();
            std::cout << "OK" << std::endl;
            Color::SetReset();
        } else {
            Color::SetYellow();
            std::cout << "NOT FOUND (will use defaults)" << std::endl;
            Color::SetReset();
        }

        // Check OpenXR loader
        std::cout << "Checking openxr_loader.dll... ";
        std::string openxrPath = std::filesystem::path(m_dllPath).parent_path().string() + "\\openxr_loader.dll";
        if (std::filesystem::exists(openxrPath)) {
            Color::SetGreen();
            std::cout << "OK" << std::endl;
            Color::SetReset();
        } else {
            Color::SetYellow();
            std::cout << "NOT FOUND (OpenXR may not work)" << std::endl;
            Color::SetReset();
        }

        // Check OpenVR API
        std::cout << "Checking openvr_api.dll... ";
        std::string openvrPath = std::filesystem::path(m_dllPath).parent_path().string() + "\\openvr_api.dll";
        if (std::filesystem::exists(openvrPath)) {
            Color::SetGreen();
            std::cout << "OK" << std::endl;
            Color::SetReset();
        } else {
            Color::SetYellow();
            std::cout << "NOT FOUND (OpenVR may not work)" << std::endl;
            Color::SetReset();
        }

        // Check VR runtime
        std::cout << "\n=== VR Runtime Check ===" << std::endl << std::endl;

        // Check SteamVR
        std::cout << "Checking SteamVR... ";
        DWORD steamvrPid = FindProcess("vrserver.exe");
        if (steamvrPid != 0) {
            Color::SetGreen();
            std::cout << "RUNNING (PID: " << steamvrPid << ")" << std::endl;
            Color::SetReset();
        } else {
            Color::SetYellow();
            std::cout << "NOT RUNNING" << std::endl;
            Color::SetReset();
        }

        // Check Oculus
        std::cout << "Checking Oculus Runtime... ";
        DWORD oculusPid = FindProcess("OVRServer_x64.exe");
        if (oculusPid != 0) {
            Color::SetGreen();
            std::cout << "RUNNING (PID: " << oculusPid << ")" << std::endl;
            Color::SetReset();
        } else {
            Color::SetYellow();
            std::cout << "NOT RUNNING" << std::endl;
            Color::SetReset();
        }

        // Check GTA V
        std::cout << "\n=== Game Check ===" << std::endl << std::endl;

        std::cout << "Checking GTA5.exe... ";
        DWORD gtaPid = FindProcess("GTA5.exe");
        if (gtaPid != 0) {
            Color::SetGreen();
            std::cout << "RUNNING (PID: " << gtaPid << ")" << std::endl;
            Color::SetReset();
        } else {
            Color::SetYellow();
            std::cout << "NOT RUNNING" << std::endl;
            Color::SetReset();
        }

        std::cout << std::endl;

        if (allGood) {
            Success("All critical files present!");
        } else {
            Error("Some files are missing. Please check installation.");
        }

        return allGood;
    }

    void ShowSystemInfo() {
        std::cout << "\n=== System Information ===" << std::endl << std::endl;

        // OS version
        OSVERSIONINFOEXA osvi;
        ZeroMemory(&osvi, sizeof(osvi));
        osvi.dwOSVersionInfoSize = sizeof(osvi);

        std::cout << "OS: Windows ";
        if (IsWindows10OrGreater()) {
            std::cout << "10/11";
        } else {
            std::cout << "Unknown";
        }
        std::cout << " (64-bit)" << std::endl;

        // CPU
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        std::cout << "CPU Cores: " << sysInfo.dwNumberOfProcessors << std::endl;

        // Memory
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(memInfo);
        GlobalMemoryStatusEx(&memInfo);
        std::cout << "RAM: " << (memInfo.ullTotalPhys / (1024 * 1024 * 1024)) << " GB" << std::endl;
        std::cout << "Available RAM: " << (memInfo.ullAvailPhys / (1024 * 1024 * 1024)) << " GB" << std::endl;

        std::cout << std::endl;
    }

    bool IsWindows10OrGreater() {
        OSVERSIONINFOEXA osvi;
        ZeroMemory(&osvi, sizeof(osvi));
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        osvi.dwMajorVersion = 10;

        DWORDLONG conditionMask = 0;
        VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);

        return VerifyVersionInfoA(&osvi, VER_MAJORVERSION, conditionMask) != FALSE;
    }

    void ShowTroubleshootingTips() {
        std::cout << std::endl;
        Color::SetYellow();
        std::cout << "=== Troubleshooting Tips ===" << std::endl;
        Color::SetReset();
        std::cout << "1. Run this injector as Administrator" << std::endl;
        std::cout << "2. Disable antivirus temporarily" << std::endl;
        std::cout << "3. Disable Steam/Discord/GeForce overlays" << std::endl;
        std::cout << "4. Make sure GTA V is fully loaded (at main menu)" << std::endl;
        std::cout << "5. Verify game files through launcher" << std::endl;
        std::cout << "6. Check TROUBLESHOOTING.md for more help" << std::endl;
    }

    // Member variables
    bool m_autoMode = false;
    bool m_verifyOnly = false;
    bool m_verbose = false;
    bool m_showSysInfo = false;
    DWORD m_targetPID = 0;
    DWORD m_timeout = 10000;
    std::string m_dllPath;
};

// Main entry point
int main(int argc, char* argv[]) {
    // Enable ANSI colors on Windows 10+
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // Set console title
    SetConsoleTitleA("GTA5VR Injector");

    GTA5VRInjector injector;

    if (!injector.ParseArgs(argc, argv)) {
        return 1;
    }

    int result = injector.Run();

    // Wait for key press before closing (if not in auto mode)
    if (argc == 1) {
        std::cout << std::endl << "Press Enter to exit...";
        std::cin.get();
    }

    return result;
}
