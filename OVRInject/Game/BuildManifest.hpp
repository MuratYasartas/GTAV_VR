#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace OVRInject {
namespace Game {

/**
 * CameraPatternEntry - one AOB pattern + the walk from match to camera matrix.
 *
 * Mirrors the fields GtaCameraHook::CameraConfig consumes, so a manifest
 * entry can be dropped straight into the existing resolution pipeline.
 */
struct CameraPatternEntry {
    std::string pattern;               // IDA-style AOB, e.g. "48 8B 41 10 ??"
    int64_t patternOffset = 0;         // applied to the match address first
    std::vector<int64_t> relativeOffsets; // chained RIP-relative disp32 resolves
    int64_t ripOffset = 0;             // single RIP-relative disp32 offset (0 = disabled)
    bool ripOffsetSet = false;
    std::vector<int64_t> pointerOffsets;  // pointer chain after RIP resolve
    int64_t matrixOffset = 0;          // offset from resolved base to the matrix
    std::string source;                // attribution comment from the manifest
};

/**
 * FovPatternEntry - one AOB pattern for locating the camera director
 * (consumed by GtaCameraFov::Resolve).
 *
 * callOffset >= 0      : E8 relative call at match + callOffset
 * globalPtrOffset >= 0 : RIP-relative global pointer (48 8B 0D/05) at match + globalPtrOffset
 * both < 0             : reference-only entry, skipped by the resolver
 */
struct FovPatternEntry {
    std::string pattern;
    int callOffset = -1;
    int globalPtrOffset = -1;
    std::string source;
};

/**
 * BuildInfo - identity of the running game build as detected at runtime.
 */
struct BuildInfo {
    std::string section;        // manifest section that matched, e.g. "b3095"
    std::string fileVersion;    // detected FileVersion string, e.g. "1.0.3095.0"
    uint64_t moduleSize = 0;    // SizeOfImage of the game module
    std::string moduleName;     // module the version was read from, e.g. "GTA5.exe"
    bool verified = false;      // false while any value in the section is UNVERIFIED
};

/**
 * BuildManifest - version-pinned signature manifest.
 *
 * Loads manifests/gtav_legacy.ini (search order: GTAVR_SETTINGS_DIR,
 * game exe directory, this DLL's directory), identifies the running build
 * via GTA5.exe FileVersion (GetFileVersionInfo) plus module size, and
 * exposes typed getters for the AOB patterns / offsets of that build.
 *
 * Graceful failure: an unknown build logs a diagnostic naming the detected
 * version and states it is unsupported; all getters then return false.
 * Callers MUST handle false - there is deliberately no fallback to
 * hardcoded patterns anywhere in the codebase.
 */
class BuildManifest {
public:
    BuildManifest();

    // Process-wide singleton (GtaCameraHook / GtaCameraFov share one manifest).
    static BuildManifest& Get();

    // Idempotent. Loads + parses the INI and detects the build.
    // Returns true when the INI was found and parsed (check IsBuildSupported
    // separately - a parsed INI can still lack a section for this build).
    bool Initialize();

    // Test/diagnostic support: parses the INI at `path` and forces the build
    // to the named section, bypassing FileVersion detection. Unit tests run
    // outside the game process, where DetectBuild can never match a [bNNNN]
    // section. Returns false when the file cannot be parsed or the section
    // does not exist. Not used by production runtime paths.
    bool InitializeForTest(const std::wstring& path, const std::string& sectionName);

    bool IsLoaded() const { return loaded_; }
    bool IsBuildSupported() const { return build_supported_; }
    const BuildInfo& GetBuildInfo() const { return build_info_; }

    // Full path of the INI that was loaded (empty when not found).
    const std::wstring& GetManifestPath() const { return manifest_path_; }

    // Typed getters. All return false on unknown build / missing keys.
    bool GetCameraPatterns(std::vector<CameraPatternEntry>& outEntries) const;
    bool GetFovPatterns(std::vector<FovPatternEntry>& outEntries) const;
    bool GetPointerOffsets(std::vector<int64_t>& outOffsets) const;
    bool GetMatrixOffset(int64_t& outOffset) const;
    bool GetMetadataSweepIntervalSeconds(int& outSeconds) const;

private:
    struct IniSection {
        std::string name;
        std::vector<std::pair<std::string, std::string>> keys; // lowercased key -> raw value
    };

    std::wstring ResolveManifestPath() const;
    bool ParseIni(const std::wstring& path);
    const IniSection* FindSection(const std::string& name) const;
    bool GetKey(const IniSection& section, const std::string& key, std::string& outValue) const;
    bool DetectBuild();
    bool ReadGameFileVersion(std::string& outVersion, uint32_t& outBuild) const;

    // Build-number matcher: "b3095" matches a detected 1.0.3095.x version.
    static bool SectionMatchesBuild(const std::string& sectionName, uint32_t build);

    bool init_attempted_ = false;
    bool loaded_ = false;
    bool build_supported_ = false;
    std::wstring manifest_path_;
    std::vector<IniSection> sections_;
    BuildInfo build_info_;
    int metadata_sweep_interval_sec_ = 5; // [detect] metadataSweepIntervalSec
};

} // namespace Game
} // namespace OVRInject
