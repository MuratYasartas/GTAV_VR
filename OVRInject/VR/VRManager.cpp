#include "VRManager.hpp"
#include "OpenXRBackend.hpp"
#include "OpenVRBackend.hpp"
#include "../Log.hpp"

#include <cstdlib>
#include <string>
#include <vector>
#include <Windows.h>

namespace OVRInject {
namespace VR {

namespace {

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool GetRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* value, std::wstring& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ) {
        RegCloseKey(key);
        return false;
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t));
    if (RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return false;
    }

    RegCloseKey(key);
    out.assign(buffer.data());
    return !out.empty();
}

} // namespace

//-----------------------------------------------------------------------------
// Singleton
//-----------------------------------------------------------------------------

VRManager& VRManager::Get() {
    static VRManager instance;
    return instance;
}

VRManager::~VRManager() {
    Shutdown();
}

//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------

bool VRManager::Initialize(ID3D11Device* device, Runtime preferred) {
    if (IsInitialized()) {
        LOGSTR("VRManager: Already initialized\n");
        return true;
    }

    if (!device) {
        LOGSTR("VRManager: D3D11 device is null\n");
        return false;
    }

    device_ = device;

    // Check for config/environment override
    Runtime configPreferred = GetPreferredRuntimeFromConfig();
    if (configPreferred != Runtime::Auto) {
        preferred = configPreferred;
    }

    LOGSTRF("VRManager: Initializing with preferred runtime: %s\n",
            preferred == Runtime::Auto ? "Auto" :
            preferred == Runtime::OpenVR ? "OpenVR" : "OpenXR");

    bool success = false;

    switch (preferred) {
        case Runtime::OpenVR:
            success = TryInitializeOpenVR(device);
            break;

        case Runtime::OpenXR:
            success = TryInitializeOpenXR(device);
            break;

        case Runtime::Auto:
        default:
            // Try OpenXR first (more modern, broader support)
            if (IsOpenXRAvailable()) {
                LOGSTR("VRManager: Trying OpenXR...\n");
                success = TryInitializeOpenXR(device);
            }

            // Fall back to OpenVR if OpenXR fails
            if (!success && IsOpenVRAvailable()) {
                LOGSTR("VRManager: Trying OpenVR...\n");
                success = TryInitializeOpenVR(device);
            }
            break;
    }

    if (success) {
        LOGSTRF("VRManager: Initialized with %s\n", GetActiveRuntimeName());
    } else {
        LOGSTR("VRManager: Failed to initialize any VR runtime\n");
    }

    return success;
}

void VRManager::Shutdown() {
    if (backend_) {
        backend_->Shutdown();
        backend_.reset();
    }

    active_runtime_ = Runtime::Auto;
    device_ = nullptr;

    LOGSTR("VRManager: Shutdown complete\n");
}

bool VRManager::IsInitialized() const {
    return backend_ && backend_->IsInitialized();
}

//-----------------------------------------------------------------------------
// Backend Access
//-----------------------------------------------------------------------------

IVRBackend* VRManager::GetBackend() const {
    return backend_.get();
}

Runtime VRManager::GetActiveRuntime() const {
    return active_runtime_;
}

const char* VRManager::GetActiveRuntimeName() const {
    if (!backend_) return "None";
    return backend_->GetRuntimeName();
}

//-----------------------------------------------------------------------------
// Runtime Detection
//-----------------------------------------------------------------------------

bool VRManager::IsOpenVRAvailable() {
    wchar_t envPath[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"STEAMVR_RUNTIME_PATH", envPath, MAX_PATH) > 0) {
        std::wstring vrserver = std::wstring(envPath) + L"\\bin\\win64\\vrserver.exe";
        if (FileExists(vrserver)) {
            return true;
        }
    }

    std::wstring steamPath;
    if (GetRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", steamPath) ||
        GetRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", steamPath)) {
        std::wstring vrserver = steamPath + L"\\steamapps\\common\\SteamVR\\bin\\win64\\vrserver.exe";
        if (FileExists(vrserver)) {
            return true;
        }
    }

    return false;
}

bool VRManager::IsOpenXRAvailable() {
    std::wstring runtimePath;
    if (GetRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", runtimePath) ||
        GetRegistryString(HKEY_CURRENT_USER, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", runtimePath)) {
        return FileExists(runtimePath);
    }

    wchar_t envPath[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"XR_RUNTIME_JSON", envPath, MAX_PATH) > 0) {
        return FileExists(envPath);
    }

    return false;
}

//-----------------------------------------------------------------------------
// Backend Initialization
//-----------------------------------------------------------------------------

bool VRManager::TryInitializeOpenVR(ID3D11Device* device) {
    auto openvrBackend = std::make_unique<OpenVRBackend>();

    if (!openvrBackend->Initialize(device)) {
        LOGSTR("VRManager: Failed to initialize OpenVR backend\n");
        return false;
    }

    backend_ = std::move(openvrBackend);
    active_runtime_ = Runtime::OpenVR;
    return true;
}

bool VRManager::TryInitializeOpenXR(ID3D11Device* device) {
    auto openxrBackend = std::make_unique<OpenXRBackend>();

    if (!openxrBackend->Initialize(device)) {
        LOGSTR("VRManager: Failed to initialize OpenXR backend\n");
        return false;
    }

    backend_ = std::move(openxrBackend);
    active_runtime_ = Runtime::OpenXR;
    return true;
}

//-----------------------------------------------------------------------------
// Configuration
//-----------------------------------------------------------------------------

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

Runtime VRManager::GetPreferredRuntimeFromConfig() const {
    // Check environment variable
    const char* envBackend = std::getenv("GTAVR_BACKEND");
    if (envBackend) {
        if (_stricmp(envBackend, "openxr") == 0) {
            LOGSTR("VRManager: Environment variable GTAVR_BACKEND=openxr\n");
            return Runtime::OpenXR;
        }
        if (_stricmp(envBackend, "openvr") == 0) {
            LOGSTR("VRManager: Environment variable GTAVR_BACKEND=openvr\n");
            return Runtime::OpenVR;
        }
    }

    // INI fallback: when the mod is injected into an ALREADY-RUNNING game,
    // the game's environment never contains GTAVR_BACKEND, so the backend
    // must also be selectable via gtavr_settings.ini [Runtime] backend=.
    // The control panel writes this key before injecting. Manual parse
    // (the Win32 INI API silently fails on LF-only files).
    char iniPath[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("GTAVR_SETTINGS_DIR", iniPath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        strncat_s(iniPath, sizeof(iniPath), "\\gtavr_settings.ini", _TRUNCATE);
    } else {
        char modulePath[MAX_PATH] = {};
        DWORD mlen = GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
        if (mlen > 0 && mlen < MAX_PATH) {
            char* lastSlash = strrchr(modulePath, '\\');
            if (lastSlash) {
                *(lastSlash + 1) = '\0';
                snprintf(iniPath, sizeof(iniPath), "%sgtavr_settings.ini", modulePath);
            }
        }
    }
    if (iniPath[0] != '\0') {
        FILE* fp = nullptr;
        if (fopen_s(&fp, iniPath, "rb") == 0 && fp) {
            char line[256];
            bool inRuntime = false;
            char backend[32] = {};
            while (fgets(line, sizeof(line), fp)) {
                char* p = line;
                while (*p == ' ' || *p == '\t') ++p;
                if (_strnicmp(p, "[Runtime]", 9) == 0) { inRuntime = true; continue; }
                if (p[0] == '[') { inRuntime = false; continue; }
                if (inRuntime && _strnicmp(p, "backend", 7) == 0) {
                    char* eq = strchr(p, '=');
                    if (eq) {
                        while (*++eq == ' ' || *eq == '\t') {}
                        size_t i = 0;
                        while (i < sizeof(backend) - 1 &&
                               ((*eq >= 'a' && *eq <= 'z') || (*eq >= 'A' && *eq <= 'Z') || *eq == '_')) {
                            backend[i++] = *eq++;
                        }
                        backend[i] = '\0';
                    }
                }
            }
            fclose(fp);
            if (_stricmp(backend, "openxr") == 0) {
                LOGSTR("VRManager: INI [Runtime] backend=openxr\n");
                return Runtime::OpenXR;
            }
            if (_stricmp(backend, "openvr") == 0) {
                LOGSTR("VRManager: INI [Runtime] backend=openvr\n");
                return Runtime::OpenVR;
            }
        }
    }

    return Runtime::Auto;
}

} // namespace VR
} // namespace OVRInject
