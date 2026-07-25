// Unit tests for OVRInject/Game/BuildManifest - the version-pinned signature
// manifest that feeds GtaCameraHook's camera resolution.
//
// Motivation (P0 regression guard): on the live b3889 build the externally
// verified camera chain (pattern 48 8B C7 F3 0F 10 0D, patternOffset -0x1D,
// relativeOffsets 0,3, pointerOffsets 0, matrixOffset 0x1F0) resolved to a
// plausible camera matrix, while the in-mod resolver never engaged. These
// tests parse the REAL manifests/gtav_legacy.ini through the REAL
// BuildManifest code and assert the [b3889] primary entry comes out
// byte-for-byte intact, so a parse/encoding regression can never again pass
// unnoticed. DetectBuild() is bypassed via BuildManifest::InitializeForTest
// (the test exe is not GTA5.exe, so FileVersion matching cannot work here).

#include "TestFramework.hpp"

#include "../OVRInject/Game/BuildManifest.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using OVRInject::Game::BuildManifest;
using OVRInject::Game::CameraPatternEntry;
using OVRInject::Game::FovPatternEntry;

// Locates the repo's manifests/gtav_legacy.ini regardless of the working
// directory the test exe is launched from.
std::wstring FindRealManifestPath() {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;

    // 1) Relative to this source file (MSBuild usually passes absolute paths).
    candidates.push_back(fs::path(__FILE__).parent_path() / L".." / L"manifests" / L"gtav_legacy.ini");

    // 2) Relative to the test executable: tests/x64/<cfg>/GTAVRTests.exe -> repo root.
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        candidates.push_back(fs::path(exePath).parent_path() / L".." / L".." / L".." / L"manifests" / L"gtav_legacy.ini");
    }

    // 3) Working-directory fallbacks (repo root, tests/, tests/x64/<cfg>).
    candidates.push_back(fs::path(L"manifests") / L"gtav_legacy.ini");
    candidates.push_back(fs::path(L"..") / L"manifests" / L"gtav_legacy.ini");
    candidates.push_back(fs::path(L"..") / L".." / L".." / L"manifests" / L"gtav_legacy.ini");

    for (const fs::path& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
            return fs::weakly_canonical(candidate, ec).wstring();
        }
    }
    return std::wstring();
}

void CheckB3889PrimaryEntry(const CameraPatternEntry& e) {
    // The externally verified b3889 chain (tools/verify_camera_chain.py).
    CHECK(e.pattern == "48 8B C7 F3 0F 10 0D");
    CHECK(e.patternOffset == -0x1D);
    CHECK(e.relativeOffsets.size() == 2);
    if (e.relativeOffsets.size() == 2) {
        CHECK(e.relativeOffsets[0] == 0);
        CHECK(e.relativeOffsets[1] == 3);
    }
    // relativeOffsets present -> the single ripOffset stays disabled.
    CHECK(e.ripOffsetSet == false);
    CHECK(e.pointerOffsets.size() == 1);
    if (e.pointerOffsets.size() == 1) {
        CHECK(e.pointerOffsets[0] == 0);
    }
    CHECK(e.matrixOffset == 0x1F0);
}

} // namespace

TEST(BuildManifest_B3889_CameraPatternsIntact) {
    std::wstring path = FindRealManifestPath();
    CHECK(!path.empty());
    if (path.empty()) return;

    BuildManifest& manifest = BuildManifest::Get();
    CHECK(manifest.InitializeForTest(path, "b3889"));
    CHECK(manifest.IsLoaded());
    CHECK(manifest.IsBuildSupported());
    CHECK(manifest.GetBuildInfo().section == "b3889");
    // verified=0 in the file (control unverified) - must parse as false.
    CHECK(manifest.GetBuildInfo().verified == false);

    std::vector<CameraPatternEntry> entries;
    CHECK(manifest.GetCameraPatterns(entries));
    CHECK(entries.size() == 3); // primary + alternates .1/.2
    if (entries.size() >= 1) {
        CheckB3889PrimaryEntry(entries[0]);
        CHECK(entries[0].source.find("GTAForums") != std::string::npos);
    }
    if (entries.size() >= 2) {
        // Alt .1: "camera manager reference" - single RIP chain.
        const CameraPatternEntry& e = entries[1];
        CHECK(e.pattern == "48 8B 0D ? ? ? ? 48 85 C9 74 ? E8 ? ? ? ? 84 C0");
        CHECK(e.patternOffset == 0);
        CHECK(e.relativeOffsets.empty());
        CHECK(e.ripOffsetSet);
        CHECK(e.ripOffset == 3);
        CHECK(e.pointerOffsets.empty());
        CHECK(e.matrixOffset == 0x1F0);
    }
    if (entries.size() >= 3) {
        // Alt .2: "scene camera".
        const CameraPatternEntry& e = entries[2];
        CHECK(e.pattern == "F3 0F 10 ? ? ? ? ? F3 0F 10 ? ? ? ? ? F3 0F 10 ? ? ? ? ? 0F 28");
        CHECK(e.patternOffset == 0);
        CHECK(e.relativeOffsets.empty());
        CHECK(e.ripOffsetSet);
        CHECK(e.ripOffset == 4);
        CHECK(e.pointerOffsets.empty());
        CHECK(e.matrixOffset == 0);
    }

    // Flat getters mirror the primary entry's chain tail.
    std::vector<int64_t> pointerOffsets;
    CHECK(manifest.GetPointerOffsets(pointerOffsets));
    CHECK(pointerOffsets.size() == 1);
    if (pointerOffsets.size() == 1) {
        CHECK(pointerOffsets[0] == 0);
    }
    int64_t matrixOffset = 0;
    CHECK(manifest.GetMatrixOffset(matrixOffset));
    CHECK(matrixOffset == 0x1F0);
}

TEST(BuildManifest_B3889_FovPatternsIntact) {
    std::wstring path = FindRealManifestPath();
    CHECK(!path.empty());
    if (path.empty()) return;

    BuildManifest& manifest = BuildManifest::Get();
    CHECK(manifest.InitializeForTest(path, "b3889"));

    std::vector<FovPatternEntry> entries;
    CHECK(manifest.GetFovPatterns(entries));
    CHECK(entries.size() == 4);
    if (!entries.empty()) {
        CHECK(entries[0].pattern == "E8 ?? ?? ?? ?? 48 8D 48 30 EB 0B");
        CHECK(entries[0].callOffset == 0);
        CHECK(entries[0].globalPtrOffset == -1);
    }
}

TEST(BuildManifest_B3095_PrimaryCameraEntryIntact) {
    // Second shipped legacy section - guards the state reset between forced
    // inits and the other build's primary chain values.
    std::wstring path = FindRealManifestPath();
    CHECK(!path.empty());
    if (path.empty()) return;

    BuildManifest& manifest = BuildManifest::Get();
    CHECK(manifest.InitializeForTest(path, "b3095"));
    CHECK(manifest.IsBuildSupported());
    CHECK(manifest.GetBuildInfo().section == "b3095");

    std::vector<CameraPatternEntry> entries;
    CHECK(manifest.GetCameraPatterns(entries));
    CHECK(!entries.empty());
    if (!entries.empty()) {
        CheckB3889PrimaryEntry(entries[0]); // b3095 shares the same primary chain
    }
}

TEST(BuildManifest_DetectSection_SweepInterval) {
    std::wstring path = FindRealManifestPath();
    CHECK(!path.empty());
    if (path.empty()) return;

    BuildManifest& manifest = BuildManifest::Get();
    CHECK(manifest.InitializeForTest(path, "b3889"));
    int seconds = 0;
    CHECK(manifest.GetMetadataSweepIntervalSeconds(seconds));
    CHECK(seconds == 5); // [detect] metadataSweepIntervalSec=5
}

TEST(BuildManifest_UnknownSection_FailsCleanly) {
    std::wstring path = FindRealManifestPath();
    CHECK(!path.empty());
    if (path.empty()) return;

    BuildManifest& manifest = BuildManifest::Get();
    CHECK(!manifest.InitializeForTest(path, "b9999"));
    CHECK(!manifest.IsBuildSupported());

    std::vector<CameraPatternEntry> cameraEntries;
    CHECK(!manifest.GetCameraPatterns(cameraEntries));
    std::vector<FovPatternEntry> fovEntries;
    CHECK(!manifest.GetFovPatterns(fovEntries));

    // Nonexistent file must also fail cleanly.
    CHECK(!manifest.InitializeForTest(L"C:\\gtavr_no_such_manifest_file.ini", "b3889"));
}

TEST(BuildManifest_SyntheticIni_ParseSemantics) {
    // Synthetic section exercising the parser semantics a regression would
    // break: case-insensitive keys, ';'/'#' comments, negative hex offsets,
    // whitespace in offset lists, verified=1.
    namespace fs = std::filesystem;
    fs::path tempPath = fs::temp_directory_path() / L"gtavr_test_manifest_semantics.ini";
    {
        std::ofstream out(tempPath);
        out << "; comment line\n";
        out << "[detect]\n";
        out << "metadataSweepIntervalSec=7\n";
        out << "[bTEST]\n";
        out << "version=1.0.0.0\n";
        out << "verified=1\n";
        out << "cameraPattern=48 8B C7 F3 0F 10 0D   ; trailing comment\n";
        out << "CAMERAPATTERNOFFSET=-0x1D\n";
        out << "relativeOffsets=0, 3 # another comment\n";
        out << "pointerOffsets=0\n";
        out << "matrixOffset=0x1F0\n";
        out << "source=Synthetic Test Entry\n";
    }

    BuildManifest& manifest = BuildManifest::Get();
    CHECK(manifest.InitializeForTest(tempPath.wstring(), "btest"));
    CHECK(manifest.IsBuildSupported());
    CHECK(manifest.GetBuildInfo().verified == true);

    int seconds = 0;
    CHECK(manifest.GetMetadataSweepIntervalSeconds(seconds));
    CHECK(seconds == 7);

    std::vector<CameraPatternEntry> entries;
    CHECK(manifest.GetCameraPatterns(entries));
    CHECK(entries.size() == 1);
    if (!entries.empty()) {
        CheckB3889PrimaryEntry(entries[0]);
        CHECK(entries[0].source == "Synthetic Test Entry");
    }

    std::error_code ec;
    fs::remove(tempPath, ec);
}
