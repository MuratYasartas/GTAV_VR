#pragma once

#include <string>
#include <vector>

namespace GTA5VR {

struct RuntimeInfo {
    std::string name;
    std::string version;
    bool available;
    bool preferred;
};

class RuntimeDetector {
public:
    // Detect the best available runtime
    static std::string DetectBestRuntime();

    // Check specific runtime availability
    static bool IsOpenXRAvailable();
    static bool IsOpenVRAvailable();

    // Get detailed information about available runtimes
    static std::vector<RuntimeInfo> GetAvailableRuntimes();

    // Get OpenXR runtime information
    static std::string GetActiveOpenXRRuntime();
    static std::vector<std::string> GetInstalledOpenXRRuntimes();

private:
    static bool CheckOpenXRLoader();
    static bool CheckSteamVR();
    static std::string ReadRegistryString(const std::string& path, const std::string& value);
};

} // namespace GTA5VR
