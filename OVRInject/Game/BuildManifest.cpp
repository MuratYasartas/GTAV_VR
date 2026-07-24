#include "BuildManifest.hpp"

#include "../Log.hpp"

#include <Windows.h>
#include <Psapi.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace OVRInject {
namespace Game {

namespace {

constexpr const wchar_t* kManifestFileName = L"gtav_legacy.ini";
constexpr int kDefaultMetadataSweepIntervalSec = 5;
constexpr int kMaxIndexedEntries = 64; // scan cameraPattern.N / fovPattern.N up to this index

std::string IniTrim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string IniToLower(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

int64_t IniParseInt(const std::string& value, int64_t fallback = 0) {
    if (value.empty()) return fallback;
    char* end = nullptr;
    long long result = std::strtoll(value.c_str(), &end, 0);
    if (end == value.c_str()) {
        return fallback;
    }
    return static_cast<int64_t>(result);
}

bool IniParseBool(const std::string& value) {
    std::string lower = IniToLower(IniTrim(value));
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

std::vector<int64_t> IniParseOffsetList(const std::string& value) {
    std::vector<int64_t> result;
    std::stringstream ss(value);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = IniTrim(token);
        if (token.empty()) continue;
        result.push_back(IniParseInt(token, 0));
    }
    return result;
}

std::string IniWideToUtf8(const std::wstring& value) {
    if (value.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &out[0], needed, nullptr, nullptr);
    return out;
}

std::wstring GetDirectoryOf(const std::wstring& fullPath) {
    size_t lastSlash = fullPath.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return std::wstring();
    }
    return fullPath.substr(0, lastSlash + 1);
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

BuildManifest::BuildManifest() = default;

BuildManifest& BuildManifest::Get() {
    static BuildManifest s_instance;
    return s_instance;
}

bool BuildManifest::Initialize() {
    if (init_attempted_) {
        return loaded_;
    }
    init_attempted_ = true;

    manifest_path_ = ResolveManifestPath();
    if (manifest_path_.empty()) {
        LOGSTR("BuildManifest: gtav_legacy.ini not found (searched GTAVR_SETTINGS_DIR, exe dir, module dir)\n");
        LOGSTR("BuildManifest: no build data available - all pattern getters will fail\n");
        return false;
    }

    LOGSTRF("BuildManifest: Loading manifest from: %s\n", IniWideToUtf8(manifest_path_).c_str());
    if (!ParseIni(manifest_path_)) {
        LOGSTR("BuildManifest: failed to parse manifest - all pattern getters will fail\n");
        return false;
    }
    loaded_ = true;

    build_supported_ = DetectBuild();
    if (!build_supported_) {
        LOGSTRF("BuildManifest: *** UNSUPPORTED BUILD *** detected %s version '%s' (image size 0x%llX). "
                "No manifest section matches this build; all pattern getters will fail. "
                "Add a matching section to gtav_legacy.ini.\n",
                build_info_.moduleName.c_str(),
                build_info_.fileVersion.empty() ? "<unknown>" : build_info_.fileVersion.c_str(),
                static_cast<unsigned long long>(build_info_.moduleSize));
    } else {
        LOGSTRF("BuildManifest: build '%s' matched section [%s] (%s)\n",
                build_info_.fileVersion.c_str(),
                build_info_.section.c_str(),
                build_info_.verified ? "verified" : "UNVERIFIED values - treat with caution");
    }
    return true;
}

std::wstring BuildManifest::ResolveManifestPath() const {
    // 1) GTAVR_SETTINGS_DIR\gtav_legacy.ini
    wchar_t envPath[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"GTAVR_SETTINGS_DIR", envPath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::wstring full(envPath);
        if (!full.empty() && full.back() != L'\\' && full.back() != L'/') {
            full.push_back(L'\\');
        }
        full += kManifestFileName;
        if (FileExists(full)) {
            return full;
        }
    }

    // 2) Game exe directory (the process we are injected into)
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        std::wstring full = GetDirectoryOf(exePath) + kManifestFileName;
        if (FileExists(full)) {
            return full;
        }
    }

    // 3) This DLL's directory (same trick as GtaCameraHook::ResolveConfigPath)
    HMODULE hModule = nullptr;
    static int s_moduleMarker = 0;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&s_moduleMarker), &hModule);
    if (hModule) {
        wchar_t dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(hModule, dllPath, MAX_PATH)) {
            std::wstring full = GetDirectoryOf(dllPath) + kManifestFileName;
            if (FileExists(full)) {
                return full;
            }
        }
    }

    return std::wstring();
}

bool BuildManifest::ParseIni(const std::wstring& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    IniSection* current = nullptr;
    std::string line;
    while (std::getline(file, line)) {
        size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        line = IniTrim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            IniSection section;
            section.name = IniToLower(IniTrim(line.substr(1, line.size() - 2)));
            sections_.push_back(section);
            current = &sections_.back();
            continue;
        }

        if (!current) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = IniToLower(IniTrim(line.substr(0, eq)));
        std::string value = IniTrim(line.substr(eq + 1));
        current->keys.emplace_back(std::move(key), std::move(value));
    }

    return !sections_.empty();
}

const BuildManifest::IniSection* BuildManifest::FindSection(const std::string& name) const {
    for (const IniSection& section : sections_) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

bool BuildManifest::GetKey(const IniSection& section, const std::string& key, std::string& outValue) const {
    for (const auto& kv : section.keys) {
        if (kv.first == key) {
            outValue = kv.second;
            return true;
        }
    }
    return false;
}

bool BuildManifest::ReadGameFileVersion(std::string& outVersion, uint32_t& outBuild) const {
    outVersion.clear();
    outBuild = 0;

    // version.lib is not in OVRInject's linker dependencies and the project
    // file is owned by another workstream, so resolve version.dll dynamically.
    HMODULE versionDll = LoadLibraryW(L"version.dll");
    if (!versionDll) {
        LOGSTR("BuildManifest: version.dll unavailable, cannot read FileVersion\n");
        return false;
    }

    auto pGetSize = reinterpret_cast<decltype(&GetFileVersionInfoSizeW)>(
        GetProcAddress(versionDll, "GetFileVersionInfoSizeW"));
    auto pGetInfo = reinterpret_cast<decltype(&GetFileVersionInfoW)>(
        GetProcAddress(versionDll, "GetFileVersionInfoW"));
    auto pQuery = reinterpret_cast<decltype(&VerQueryValueW)>(
        GetProcAddress(versionDll, "VerQueryValueW"));
    if (!pGetSize || !pGetInfo || !pQuery) {
        LOGSTR("BuildManifest: version.dll exports missing\n");
        return false;
    }

    // [detect] module= (default GTA5.exe); fall back to the main module so the
    // diagnostic can still name whatever exe we were injected into.
    std::wstring moduleName = L"GTA5.exe";
    if (const IniSection* detect = FindSection("detect")) {
        std::string value;
        if (GetKey(*detect, "module", value) && !value.empty()) {
            moduleName.assign(value.begin(), value.end());
        }
    }

    HMODULE gameModule = GetModuleHandleW(moduleName.c_str());
    if (!gameModule) {
        gameModule = GetModuleHandleW(nullptr);
    }

    wchar_t gamePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(gameModule, gamePath, MAX_PATH)) {
        return false;
    }

    DWORD handle = 0;
    DWORD size = pGetSize(gamePath, &handle);
    if (size == 0) {
        LOGSTRF("BuildManifest: no version resource in %s\n", IniWideToUtf8(gamePath).c_str());
        return false;
    }

    std::vector<uint8_t> buffer(size);
    if (!pGetInfo(gamePath, handle, size, buffer.data())) {
        return false;
    }

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoLen = 0;
    if (!pQuery(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &infoLen) || !info) {
        return false;
    }

    uint32_t major = HIWORD(info->dwFileVersionMS);
    uint32_t minor = LOWORD(info->dwFileVersionMS);
    uint32_t build = HIWORD(info->dwFileVersionLS);
    uint32_t qfe = LOWORD(info->dwFileVersionLS);

    char text[64] = {};
    sprintf_s(text, "%u.%u.%u.%u", major, minor, build, qfe);
    outVersion = text;
    outBuild = build;
    return true;
}

bool BuildManifest::SectionMatchesBuild(const std::string& sectionName, uint32_t build) {
    // Sections are named "bNNNN" where NNNN is the 3rd FileVersion component.
    if (sectionName.size() < 2 || sectionName[0] != 'b' || build == 0) {
        return false;
    }
    for (size_t i = 1; i < sectionName.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(sectionName[i]))) {
            return false;
        }
    }
    return static_cast<uint32_t>(IniParseInt(sectionName.substr(1), 0)) == build;
}

bool BuildManifest::DetectBuild() {
    // Image size of the game module (psapi maps to kernel32 on Win7+).
    const IniSection* detect = FindSection("detect");
    std::wstring moduleName = L"GTA5.exe";
    if (detect) {
        std::string value;
        if (GetKey(*detect, "module", value) && !value.empty()) {
            moduleName.assign(value.begin(), value.end());
        }
        if (GetKey(*detect, "metadatasweepintervalsec", value)) {
            int parsed = static_cast<int>(IniParseInt(value, kDefaultMetadataSweepIntervalSec));
            if (parsed < 1) parsed = 1;
            if (parsed > 300) parsed = 300;
            metadata_sweep_interval_sec_ = parsed;
        }
    }

    HMODULE gameModule = GetModuleHandleW(moduleName.c_str());
    if (!gameModule) {
        gameModule = GetModuleHandleW(nullptr);
    }

    wchar_t gamePath[MAX_PATH] = {};
    if (GetModuleFileNameW(gameModule, gamePath, MAX_PATH)) {
        std::wstring fileName = gamePath;
        size_t lastSlash = fileName.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            fileName = fileName.substr(lastSlash + 1);
        }
        build_info_.moduleName = IniWideToUtf8(fileName);
    }

    MODULEINFO mi = {};
    if (GetModuleInformation(GetCurrentProcess(), gameModule, &mi, sizeof(mi))) {
        build_info_.moduleSize = static_cast<uint64_t>(mi.SizeOfImage);
    }

    uint32_t build = 0;
    ReadGameFileVersion(build_info_.fileVersion, build);
    if (build_info_.fileVersion.empty()) {
        LOGSTR("BuildManifest: FileVersion unavailable, trying module-name match only\n");
    }

    for (const IniSection& section : sections_) {
        if (section.name == "detect") {
            continue;
        }

        bool match = SectionMatchesBuild(section.name, build);
        if (!match) {
            // Exact version pin, e.g. version=1.0.3095.0
            std::string version;
            if (!build_info_.fileVersion.empty() &&
                GetKey(section, "version", version) &&
                version == build_info_.fileVersion) {
                match = true;
            }
        }
        if (!match) {
            // Module-name match for builds whose version we cannot read
            // (e.g. GTA5_Enhanced.exe is not guaranteed to carry the same scheme).
            std::string matchModule;
            if (GetKey(section, "matchmodule", matchModule) &&
                IniToLower(matchModule) == IniToLower(build_info_.moduleName)) {
                match = true;
            }
        }
        if (!match) {
            continue;
        }

        build_info_.section = section.name;
        std::string verified;
        build_info_.verified = GetKey(section, "verified", verified) && IniParseBool(verified);
        return true;
    }

    return false;
}

bool BuildManifest::GetCameraPatterns(std::vector<CameraPatternEntry>& outEntries) const {
    outEntries.clear();
    if (!build_supported_) {
        return false;
    }
    const IniSection* section = FindSection(build_info_.section);
    if (!section) {
        return false;
    }

    auto readEntry = [&](const std::string& suffix) -> bool {
        CameraPatternEntry entry;
        std::string value;
        if (!GetKey(*section, "camerapattern" + suffix, value) || value.empty()) {
            return false;
        }
        entry.pattern = value;
        if (GetKey(*section, "camerapatternoffset" + suffix, value)) {
            entry.patternOffset = IniParseInt(value, 0);
        }
        if (GetKey(*section, "relativeoffsets" + suffix, value)) {
            entry.relativeOffsets = IniParseOffsetList(value);
        }
        if (GetKey(*section, "ripoffset" + suffix, value)) {
            entry.ripOffset = IniParseInt(value, 0);
            // Same convention as GtaCameraHook::LoadConfig: ripOffset=0 disables
            // RIP resolution so we never read a displacement from an opcode byte.
            entry.ripOffsetSet = (entry.ripOffset != 0);
        }
        if (GetKey(*section, "pointeroffsets" + suffix, value)) {
            entry.pointerOffsets = IniParseOffsetList(value);
        }
        if (GetKey(*section, "matrixoffset" + suffix, value)) {
            entry.matrixOffset = IniParseInt(value, 0);
        }
        if (GetKey(*section, "source" + suffix, value)) {
            entry.source = value;
        }
        outEntries.push_back(std::move(entry));
        return true;
    };

    // Flat keys are the primary entry; indexed keys (.1..N) are alternates.
    readEntry("");
    for (int i = 1; i <= kMaxIndexedEntries; ++i) {
        readEntry("." + std::to_string(i));
    }

    return !outEntries.empty();
}

bool BuildManifest::GetFovPatterns(std::vector<FovPatternEntry>& outEntries) const {
    outEntries.clear();
    if (!build_supported_) {
        return false;
    }
    const IniSection* section = FindSection(build_info_.section);
    if (!section) {
        return false;
    }

    auto readEntry = [&](const std::string& suffix) -> bool {
        FovPatternEntry entry;
        std::string value;
        if (!GetKey(*section, "fovpattern" + suffix, value) || value.empty()) {
            return false;
        }
        entry.pattern = value;
        if (GetKey(*section, "fovcalloffset" + suffix, value)) {
            entry.callOffset = static_cast<int>(IniParseInt(value, -1));
        }
        if (GetKey(*section, "fovglobalptroffset" + suffix, value)) {
            entry.globalPtrOffset = static_cast<int>(IniParseInt(value, -1));
        }
        if (GetKey(*section, "fovsource" + suffix, value)) {
            entry.source = value;
        }
        outEntries.push_back(std::move(entry));
        return true;
    };

    // "fovpatterns" (plural, the key name used by the manifest spec) is an
    // alias for the flat primary entry.
    if (!readEntry("")) {
        std::string value;
        if (GetKey(*section, "fovpatterns", value) && !value.empty()) {
            FovPatternEntry entry;
            entry.pattern = value;
            if (GetKey(*section, "fovcalloffset", value)) {
                entry.callOffset = static_cast<int>(IniParseInt(value, -1));
            }
            if (GetKey(*section, "fovglobalptroffset", value)) {
                entry.globalPtrOffset = static_cast<int>(IniParseInt(value, -1));
            }
            outEntries.push_back(std::move(entry));
        }
    }
    for (int i = 1; i <= kMaxIndexedEntries; ++i) {
        readEntry("." + std::to_string(i));
    }

    return !outEntries.empty();
}

bool BuildManifest::GetPointerOffsets(std::vector<int64_t>& outOffsets) const {
    outOffsets.clear();
    if (!build_supported_) {
        return false;
    }
    const IniSection* section = FindSection(build_info_.section);
    if (!section) {
        return false;
    }
    std::string value;
    if (!GetKey(*section, "pointeroffsets", value)) {
        return false;
    }
    outOffsets = IniParseOffsetList(value);
    return !outOffsets.empty();
}

bool BuildManifest::GetMatrixOffset(int64_t& outOffset) const {
    if (!build_supported_) {
        return false;
    }
    const IniSection* section = FindSection(build_info_.section);
    if (!section) {
        return false;
    }
    std::string value;
    if (!GetKey(*section, "matrixoffset", value)) {
        return false;
    }
    outOffset = IniParseInt(value, 0);
    return true;
}

bool BuildManifest::GetMetadataSweepIntervalSeconds(int& outSeconds) const {
    outSeconds = metadata_sweep_interval_sec_;
    return true; // always available; defaults to 5 when the manifest is absent
}

} // namespace Game
} // namespace OVRInject
