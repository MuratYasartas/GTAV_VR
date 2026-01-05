#include "RuntimeDetector.h"
#include "../core/Logger.h"
#include <Windows.h>
#include <filesystem>

namespace GTA5VR {

std::string RuntimeDetector::DetectBestRuntime() {
    LOG_INFO("Detecting best VR runtime...");

    // Check OpenXR first (preferred)
    if (IsOpenXRAvailable()) {
        std::string activeRuntime = GetActiveOpenXRRuntime();
        LOG_INFO("OpenXR available, active runtime: " + activeRuntime);
        return "openxr";
    }

    // Fall back to OpenVR/SteamVR
    if (IsOpenVRAvailable()) {
        LOG_INFO("OpenVR/SteamVR available");
        return "openvr";
    }

    LOG_WARNING("No VR runtime detected");
    return "";
}

bool RuntimeDetector::IsOpenXRAvailable() {
    // Check if OpenXR loader is available
    HMODULE loader = LoadLibraryA("openxr_loader.dll");
    if (loader) {
        FreeLibrary(loader);

        // Also check if there's an active runtime configured
        std::string activeRuntime = GetActiveOpenXRRuntime();
        return !activeRuntime.empty();
    }

    return false;
}

bool RuntimeDetector::IsOpenVRAvailable() {
    // Check if OpenVR API is available
    HMODULE api = LoadLibraryA("openvr_api.dll");
    if (api) {
        FreeLibrary(api);

        // Check if SteamVR is running or installed
        return CheckSteamVR();
    }

    return false;
}

std::vector<RuntimeInfo> RuntimeDetector::GetAvailableRuntimes() {
    std::vector<RuntimeInfo> runtimes;

    // Check OpenXR
    RuntimeInfo openxr;
    openxr.name = "OpenXR";
    openxr.available = IsOpenXRAvailable();
    if (openxr.available) {
        openxr.version = GetActiveOpenXRRuntime();
        openxr.preferred = true;
    }
    runtimes.push_back(openxr);

    // Check OpenVR
    RuntimeInfo openvr;
    openvr.name = "OpenVR/SteamVR";
    openvr.available = IsOpenVRAvailable();
    openvr.preferred = !openxr.available;
    runtimes.push_back(openvr);

    return runtimes;
}

std::string RuntimeDetector::GetActiveOpenXRRuntime() {
    // Check registry for active OpenXR runtime
    // HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime

    std::string runtimePath = ReadRegistryString(
        "SOFTWARE\\Khronos\\OpenXR\\1",
        "ActiveRuntime"
    );

    if (runtimePath.empty()) {
        return "";
    }

    // Extract runtime name from path
    std::filesystem::path path(runtimePath);
    std::string filename = path.stem().string();

    // Map common runtime files to names
    if (filename.find("steamvr") != std::string::npos ||
        filename.find("steamxr") != std::string::npos) {
        return "SteamVR";
    }
    if (filename.find("oculus") != std::string::npos) {
        return "Oculus";
    }
    if (filename.find("wmr") != std::string::npos ||
        filename.find("mixedreality") != std::string::npos) {
        return "Windows Mixed Reality";
    }
    if (filename.find("varjo") != std::string::npos) {
        return "Varjo";
    }
    if (filename.find("vive") != std::string::npos) {
        return "VIVE";
    }

    return filename;
}

std::vector<std::string> RuntimeDetector::GetInstalledOpenXRRuntimes() {
    std::vector<std::string> runtimes;

    // Check registry for available runtimes
    // HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1\AvailableRuntimes

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Khronos\\OpenXR\\1\\AvailableRuntimes",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {

        DWORD index = 0;
        char valueName[MAX_PATH];
        DWORD valueNameSize;
        DWORD valueType;
        DWORD valueData;
        DWORD valueDataSize;

        while (true) {
            valueNameSize = MAX_PATH;
            valueDataSize = sizeof(DWORD);

            if (RegEnumValueA(hKey, index++, valueName, &valueNameSize,
                nullptr, &valueType, (LPBYTE)&valueData, &valueDataSize) != ERROR_SUCCESS) {
                break;
            }

            if (valueData == 0) { // 0 means enabled
                runtimes.push_back(valueName);
            }
        }

        RegCloseKey(hKey);
    }

    return runtimes;
}

bool RuntimeDetector::CheckOpenXRLoader() {
    HMODULE loader = LoadLibraryA("openxr_loader.dll");
    if (loader) {
        FreeLibrary(loader);
        return true;
    }
    return false;
}

bool RuntimeDetector::CheckSteamVR() {
    // Check if SteamVR is installed via registry
    std::string steamvrPath = ReadRegistryString(
        "SOFTWARE\\WOW6432Node\\Valve\\Steam",
        "InstallPath"
    );

    if (!steamvrPath.empty()) {
        // Check if SteamVR exists in Steam apps
        std::string steamvrExe = steamvrPath + "\\steamapps\\common\\SteamVR\\bin\\win64\\vrserver.exe";
        if (std::filesystem::exists(steamvrExe)) {
            return true;
        }
    }

    // Also check if vrmonitor is running
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);

        if (Process32First(snapshot, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, "vrmonitor.exe") == 0 ||
                    _stricmp(pe.szExeFile, "vrserver.exe") == 0) {
                    CloseHandle(snapshot);
                    return true;
                }
            } while (Process32Next(snapshot, &pe));
        }

        CloseHandle(snapshot);
    }

    return false;
}

std::string RuntimeDetector::ReadRegistryString(const std::string& path, const std::string& value) {
    HKEY hKey;
    std::string result;

    // Try HKEY_LOCAL_MACHINE first
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[MAX_PATH];
        DWORD bufferSize = sizeof(buffer);
        DWORD type;

        if (RegQueryValueExA(hKey, value.c_str(), nullptr, &type,
            (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            if (type == REG_SZ) {
                result = buffer;
            }
        }

        RegCloseKey(hKey);
    }

    // Try HKEY_CURRENT_USER if not found
    if (result.empty()) {
        if (RegOpenKeyExA(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char buffer[MAX_PATH];
            DWORD bufferSize = sizeof(buffer);
            DWORD type;

            if (RegQueryValueExA(hKey, value.c_str(), nullptr, &type,
                (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
                if (type == REG_SZ) {
                    result = buffer;
                }
            }

            RegCloseKey(hKey);
        }
    }

    return result;
}

} // namespace GTA5VR
