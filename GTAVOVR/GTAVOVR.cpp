#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cwctype>

// GTAVOVR launcher exit codes (preflight is human-readable, never silent):
//   0 - ok to try (or, with --check, no blockers found)
//   2 - unsupported game build (no manifest section); the mod would stay inert
//   3 - no usable OpenXR/OpenVR runtime
//   4 - injection failure (missing payload, launch failure, or inject failure)
#define GTAVR_EXIT_OK                0
#define GTAVR_EXIT_UNSUPPORTED_BUILD 2
#define GTAVR_EXIT_RUNTIME_MISSING   3
#define GTAVR_EXIT_INJECTION_FAILURE 4

static std::wstring GetEnvVar(const wchar_t* name) {
	wchar_t buffer[MAX_PATH] = {};
	DWORD len = GetEnvironmentVariableW(name, buffer, MAX_PATH);
	if (len > 0 && len < MAX_PATH) {
		return std::wstring(buffer);
	}
	return L"";
}

static std::wstring GetModuleDir() {
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring full(path);
	size_t pos = full.find_last_of(L"\\/");
	return (pos == std::wstring::npos) ? L"." : full.substr(0, pos);
}

static std::wstring GetInstallDir() {
	std::wstring envPath = GetEnvVar(L"GTAV_INSTALL_DIR");
	if (!envPath.empty()) {
		return envPath;
	}
	return GetModuleDir();
}

static bool FileExists(const std::wstring& path) {
	DWORD attrs = GetFileAttributesW(path.c_str());
	return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring DirNameOf(const std::wstring& path); // fwd (defined below)

// Copy `src` to `dst` when dst is missing or older than src. Returns true when
// dst ends up present and current.
static bool SyncFile(const std::wstring& src, const std::wstring& dst) {
	if (!FileExists(src)) return false;
	bool copy = true;
	WIN32_FILE_ATTRIBUTE_DATA s = {}, d = {};
	if (GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &s) &&
		GetFileAttributesExW(dst.c_str(), GetFileExInfoStandard, &d)) {
		copy = (CompareFileTime(&s.ftLastWriteTime, &d.ftLastWriteTime) > 0);
	}
	if (copy) {
		if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) return false;
	}
	return FileExists(dst);
}

// Stage everything the injected DLL needs next to it, so the mod never
// depends on the game's environment (the "manifest not found" and
// LoadLibrary-import failures both came from missing staging):
//  - openvr_api.dll / openxr_loader.dll beside GTAVOVR.exe (imports resolve
//    for the target process via its own dir/PATH)
//  - gtav_legacy.ini beside the injected OVRInject.dll (BuildManifest reads
//    <dll dir>\gtav_legacy.ini or GTAVR_SETTINGS_DIR)
// Dev layout: GTAVOVR.exe at <repo>\x64\Release -> repo root two levels up.
static int StageRuntimeFiles(const std::wstring& moduleDir, const std::wstring& dllDir) {
	int staged = 0;
	std::wstring repoRoot = DirNameOf(DirNameOf(moduleDir));

	const wchar_t* runtimeDlls[] = {
		L"openvr_api.dll", L"openxr_loader.dll",
	};
	for (const wchar_t* name : runtimeDlls) {
		std::wstring dst = moduleDir + L"\\" + name;
		if (FileExists(dst)) continue;
		std::wstring src = repoRoot + L"\\ThirdParty\\openvr\\bin\\x64\\" + name;
		if (!FileExists(src)) {
			src = repoRoot + L"\\ThirdParty\\openxr\\bin\\x64\\" + name;
		}
		if (SyncFile(src, dst)) {
			printf("[stage] %ls synced -> beside GTAVOVR.exe\n", name);
			staged++;
		} else {
			printf("[stage] WARNING: %ls missing (not beside GTAVOVR.exe, no ThirdParty source) - injection may fail on imports\n", name);
		}
	}

	std::wstring manifestDst = dllDir + L"\\gtav_legacy.ini";
	std::wstring manifestSrc = moduleDir + L"\\gtav_legacy.ini";
	if (!FileExists(manifestSrc)) manifestSrc = moduleDir + L"\\manifests\\gtav_legacy.ini";
	if (!FileExists(manifestSrc)) manifestSrc = repoRoot + L"\\manifests\\gtav_legacy.ini";
	if (SyncFile(manifestSrc, manifestDst)) {
		printf("[stage] gtav_legacy.ini synced -> %ls\n", dllDir.c_str());
		staged++;
	}

	// Camera config (pattern overrides, negate flags, resolve timeouts).
	std::wstring cameraDst = dllDir + L"\\gtavr_camera.ini";
	std::wstring cameraSrc = moduleDir + L"\\gtavr_camera.ini";
	if (!FileExists(cameraSrc)) cameraSrc = repoRoot + L"\\gtavr_camera.ini";
	if (SyncFile(cameraSrc, cameraDst)) {
		printf("[stage] gtavr_camera.ini synced -> %ls\n", dllDir.c_str());
		staged++;
	}
	return staged;
}


static std::wstring BaseNameOf(const std::wstring& path) {
	size_t pos = path.find_last_of(L"\\/");
	return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

static std::wstring DirNameOf(const std::wstring& path) {
	size_t pos = path.find_last_of(L"\\/");
	return (pos == std::wstring::npos) ? std::wstring() : path.substr(0, pos);
}

static bool GetRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* value, std::wstring& out) {
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

static bool IsOpenXRAvailable(std::wstring* outRuntimePath = nullptr) {
	std::wstring runtimePath;
	if (GetRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", runtimePath) ||
		GetRegistryString(HKEY_CURRENT_USER, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", runtimePath)) {
		if (outRuntimePath) {
			*outRuntimePath = runtimePath;
		}
		return FileExists(runtimePath);
	}
	return false;
}

static bool IsOpenVRAvailable(std::wstring* outDetail = nullptr) {
	wchar_t envPath[MAX_PATH] = {};
	if (GetEnvironmentVariableW(L"STEAMVR_RUNTIME_PATH", envPath, MAX_PATH) > 0) {
		std::wstring vrserver = std::wstring(envPath) + L"\\bin\\win64\\vrserver.exe";
		if (FileExists(vrserver)) {
			if (outDetail) {
				*outDetail = vrserver;
			}
			return true;
		}
	}

	std::wstring steamPath;
	if (GetRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", steamPath) ||
		GetRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", steamPath)) {
		std::wstring vrserver = steamPath + L"\\steamapps\\common\\SteamVR\\bin\\win64\\vrserver.exe";
		if (FileExists(vrserver)) {
			if (outDetail) {
				*outDetail = vrserver;
			}
			return true;
		}
	}

	return false;
}

static DWORD FindProcessId(const wchar_t* processName) {
	DWORD pid = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);

	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, processName) == 0) {
				pid = entry.th32ProcessID;
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return pid;
}

static bool LaunchProcess(const std::wstring& exePath) {
	STARTUPINFOW si = {};
	PROCESS_INFORMATION pi = {};
	si.cb = sizeof(si);

	std::wstring cmd = L"\"" + exePath + L"\"";
	std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
	cmdBuf.push_back(L'\0');
	if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, 0, nullptr,
		nullptr, &si, &pi)) {
		return false;
	}

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

static std::wstring NormalizePath(std::wstring path) {
	for (auto& c : path) {
		if (c == L'/') {
			c = L'\\';
		}
		c = static_cast<wchar_t>(towlower(c));
	}
	while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
		path.pop_back();
	}
	return path;
}

static bool PathStartsWith(const std::wstring& path, const std::wstring& prefix) {
	if (prefix.empty()) {
		return true;
	}
	std::wstring normPath = NormalizePath(path);
	std::wstring normPrefix = NormalizePath(prefix);
	if (normPath.size() < normPrefix.size()) {
		return false;
	}
	if (normPath.compare(0, normPrefix.size(), normPrefix) != 0) {
		return false;
	}
	return normPath.size() == normPrefix.size() || normPath[normPrefix.size()] == L'\\';
}

static bool GetProcessImagePath(DWORD pid, std::wstring& outPath) {
	outPath.clear();
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) {
		return false;
	}
	wchar_t buffer[MAX_PATH] = {};
	DWORD size = MAX_PATH;
	bool ok = QueryFullProcessImageNameW(process, 0, buffer, &size) != 0;
	CloseHandle(process);
	if (ok && size > 0) {
		outPath.assign(buffer, size);
		return true;
	}
	return false;
}

static bool WaitForProcess(const wchar_t* processName, DWORD& outPid, int timeoutMs) {
	const int stepMs = 100;
	int waited = 0;
	while (waited < timeoutMs) {
		outPid = FindProcessId(processName);
		if (outPid != 0) {
			return true;
		}
		Sleep(stepMs);
		waited += stepMs;
	}
	return false;
}

static bool InjectDll(DWORD pid, const std::wstring& dllPath) {
	HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
		PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
	if (!process) {
		return false;
	}

	size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
	void* remoteMem = VirtualAllocEx(process, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!remoteMem) {
		CloseHandle(process);
		return false;
	}

	if (!WriteProcessMemory(process, remoteMem, dllPath.c_str(), bytes, nullptr)) {
		VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}

	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	FARPROC loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");
	if (!loadLibrary) {
		VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}

	HANDLE thread = CreateRemoteThread(process, nullptr, 0,
		reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary), remoteMem, 0, nullptr);
	if (!thread) {
		VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}

	WaitForSingleObject(thread, INFINITE);
	DWORD exitCode = 0;
	if (!GetExitCodeThread(thread, &exitCode) || exitCode == 0) {
		CloseHandle(thread);
		VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
		CloseHandle(process);
		printf("LoadLibrary failed inside target process.\n");
		return false;
	}
	CloseHandle(thread);
	VirtualFreeEx(process, remoteMem, 0, MEM_RELEASE);
	CloseHandle(process);
	return true;
}

// ---------------------------------------------------------------------------
// Preflight (Phase 10): build detection, runtime check, BattlEye posture.
// Everything prints human-readable output; --check runs only this section.
// ---------------------------------------------------------------------------

// version.lib is not in this project's linker dependencies, so resolve
// version.dll dynamically (same approach as OVRInject/Game/BuildManifest.cpp).
static bool GetExeFileVersion(const std::wstring& exePath, std::wstring& outVersion, DWORD& outBuild) {
	outVersion.clear();
	outBuild = 0;

	typedef DWORD(APIENTRY* GetFileVersionInfoSizeWFn)(LPCWSTR, LPDWORD);
	typedef BOOL(APIENTRY* GetFileVersionInfoWFn)(LPCWSTR, DWORD, DWORD, LPVOID);
	typedef BOOL(APIENTRY* VerQueryValueWFn)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

	HMODULE versionDll = LoadLibraryW(L"version.dll");
	if (!versionDll) {
		return false;
	}
	GetFileVersionInfoSizeWFn pSize = reinterpret_cast<GetFileVersionInfoSizeWFn>(
		GetProcAddress(versionDll, "GetFileVersionInfoSizeW"));
	GetFileVersionInfoWFn pInfo = reinterpret_cast<GetFileVersionInfoWFn>(
		GetProcAddress(versionDll, "GetFileVersionInfoW"));
	VerQueryValueWFn pQuery = reinterpret_cast<VerQueryValueWFn>(
		GetProcAddress(versionDll, "VerQueryValueW"));

	bool ok = false;
	if (pSize && pInfo && pQuery) {
		DWORD handle = 0;
		DWORD size = pSize(exePath.c_str(), &handle);
		if (size > 0) {
			std::vector<BYTE> data(size);
			if (pInfo(exePath.c_str(), handle, size, data.data())) {
				VS_FIXEDFILEINFO* fixedInfo = nullptr;
				UINT fixedLen = 0;
				if (pQuery(data.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedInfo), &fixedLen) &&
					fixedInfo && fixedLen >= sizeof(VS_FIXEDFILEINFO)) {
					wchar_t buf[64] = {};
					swprintf_s(buf, _countof(buf), L"%u.%u.%u.%u",
						HIWORD(fixedInfo->dwFileVersionMS), LOWORD(fixedInfo->dwFileVersionMS),
						HIWORD(fixedInfo->dwFileVersionLS), LOWORD(fixedInfo->dwFileVersionLS));
					outVersion = buf;
					outBuild = HIWORD(fixedInfo->dwFileVersionLS);
					ok = true;
				}
			}
		}
	}
	FreeLibrary(versionDll);
	return ok;
}

// Locate gtav_legacy.ini. Roots mirror the injected mod's BuildManifest
// (GTAVR_SETTINGS_DIR, game exe dir) plus the launcher dir and cwd; each root
// is checked both as <root>\gtav_legacy.ini (what BuildManifest reads) and
// <root>\manifests\gtav_legacy.ini (the shipped/installer layout).
static std::wstring FindManifestFile(const std::wstring& gameDir) {
	std::vector<std::wstring> roots;
	std::wstring settingsDir = GetEnvVar(L"GTAVR_SETTINGS_DIR");
	if (!settingsDir.empty()) {
		roots.push_back(settingsDir);
	}
	if (!gameDir.empty()) {
		roots.push_back(gameDir);
	}
	roots.push_back(GetModuleDir());
	wchar_t cwd[MAX_PATH] = {};
	if (GetCurrentDirectoryW(MAX_PATH, cwd) > 0) {
		roots.push_back(cwd);
	}

	for (const auto& root : roots) {
		std::wstring direct = root + L"\\gtav_legacy.ini";
		if (FileExists(direct)) {
			return direct;
		}
		std::wstring nested = root + L"\\manifests\\gtav_legacy.ini";
		if (FileExists(nested)) {
			return nested;
		}
	}
	return std::wstring();
}

// A manifest section matches when it is named bNNNN with NNNN == the exe's
// build number, or when it pins matchModule=<exe name> (GTA5_Enhanced.exe).
static bool ManifestMatch(const std::wstring& manifestPath, const std::wstring& exeName,
	DWORD build, std::wstring& outSection, bool& outVerified) {
	std::vector<wchar_t> sections(32768, L'\0');
	DWORD count = GetPrivateProfileSectionNamesW(sections.data(),
		static_cast<DWORD>(sections.size()), manifestPath.c_str());
	if (count == 0) {
		return false;
	}

	wchar_t wantSection[32] = {};
	swprintf_s(wantSection, _countof(wantSection), L"b%u", static_cast<unsigned>(build));

	for (const wchar_t* s = sections.data(); *s; s += wcslen(s) + 1) {
		bool match = (_wcsicmp(s, wantSection) == 0);
		if (!match) {
			wchar_t matchModule[MAX_PATH] = {};
			DWORD len = GetPrivateProfileStringW(s, L"matchModule", L"", matchModule,
				MAX_PATH, manifestPath.c_str());
			match = (len > 0 && _wcsicmp(matchModule, exeName.c_str()) == 0);
		}
		if (match) {
			outSection = s;
			wchar_t verified[16] = {};
			GetPrivateProfileStringW(s, L"verified", L"0", verified, 16, manifestPath.c_str());
			outVerified = (verified[0] == L'1');
			return true;
		}
	}
	return false;
}

enum BattlEyePosture {
	kBattlEyeClear = 0,
	kBattlEyeInstalledNotRunning,
	kBattlEyeActive,
};

// Warn-only posture check: BEService service exists AND running, or BattlEye
// modules already loaded inside the target process.
static BattlEyePosture CheckBattlEyePosture(DWORD pid) {
	bool serviceExists = false;
	bool serviceRunning = false;

	SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (scm) {
		SC_HANDLE service = OpenServiceW(scm, L"BEService", SERVICE_QUERY_STATUS);
		if (service) {
			serviceExists = true;
			SERVICE_STATUS status = {};
			if (QueryServiceStatus(service, &status)) {
				serviceRunning = (status.dwCurrentState == SERVICE_RUNNING ||
					status.dwCurrentState == SERVICE_START_PENDING);
			}
			CloseServiceHandle(service);
		}
		CloseServiceHandle(scm);
	}

	bool moduleFound = false;
	if (pid != 0) {
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
		if (snapshot != INVALID_HANDLE_VALUE) {
			MODULEENTRY32W entry = {};
			entry.dwSize = sizeof(entry);
			if (Module32FirstW(snapshot, &entry)) {
				do {
					if (wcsstr(entry.szModule, L"BEService") != nullptr ||
						wcsstr(entry.szModule, L"BEDaisy") != nullptr) {
						moduleFound = true;
						break;
					}
				} while (Module32NextW(snapshot, &entry));
			}
			CloseHandle(snapshot);
		}
	}

	if (serviceRunning || moduleFound) {
		return kBattlEyeActive;
	}
	return serviceExists ? kBattlEyeInstalledNotRunning : kBattlEyeClear;
}

// Runs the full preflight and prints every result. Returns GTAVR_EXIT_OK,
// GTAVR_EXIT_UNSUPPORTED_BUILD or GTAVR_EXIT_RUNTIME_MISSING.
static int RunPreflight(const std::wstring& gameExePath, const std::wstring& processName,
	bool sliceOverride, DWORD pidOrZero) {
	printf("[preflight] target: %ls (%ls)\n", processName.c_str(), gameExePath.c_str());

	// --- 1. Game build detection -------------------------------------------
	if (sliceOverride) {
		printf("[preflight] build check: skipped (non-GTA target override - vertical slice).\n");
	} else if (!FileExists(gameExePath)) {
		printf("[preflight] build check: %ls not found - cannot verify the game build.\n",
			gameExePath.c_str());
		printf("            Install GTA V Legacy beside GTAVOVR.exe or set GTAV_EXE_PATH / GTAV_INSTALL_DIR.\n");
	} else {
		std::wstring version;
		DWORD build = 0;
		if (!GetExeFileVersion(gameExePath, version, build)) {
			printf("[preflight] build check: could not read the FileVersion of %ls - cannot verify the build.\n",
				gameExePath.c_str());
		} else {
			std::wstring manifestPath = FindManifestFile(DirNameOf(gameExePath));
			if (manifestPath.empty()) {
				printf("[preflight] build check: UNSUPPORTED build %ls (no build manifest found).\n",
					version.c_str());
				printf("            Searched GTAVR_SETTINGS_DIR, the game dir, the GTAVOVR.exe dir and the cwd\n");
				printf("            for gtav_legacy.ini / manifests\\gtav_legacy.ini. Without a manifest the mod\n");
				printf("            stays inert; reinstall the release set.\n");
				return GTAVR_EXIT_UNSUPPORTED_BUILD;
			}

			std::wstring section;
			bool verified = false;
			if (ManifestMatch(manifestPath, BaseNameOf(gameExePath), build, section, verified)) {
				printf("[preflight] build check: supported build %ls (manifest section [%ls]%ls, %ls)\n",
					version.c_str(), section.c_str(), verified ? L"" : L", verified=0",
					verified ? L"verified" : L"UNVERIFIED values - treat with caution");
				printf("            manifest: %ls\n", manifestPath.c_str());
			} else {
				printf("[preflight] build check: UNSUPPORTED build %ls (no manifest section) - the mod will stay inert.\n",
					version.c_str());
				printf("            manifest: %ls\n", manifestPath.c_str());
				printf("            Add a [b%u] section to gtav_legacy.ini for this build, or wait for an updated manifest.\n",
					static_cast<unsigned>(build));
				return GTAVR_EXIT_UNSUPPORTED_BUILD;
			}
		}
	}

	// --- 2. VR runtime check ------------------------------------------------
	wchar_t backendEnv[64] = {};
	DWORD backendLen = GetEnvironmentVariableW(L"GTAVR_BACKEND", backendEnv, 64);
	std::wstring preferred = (backendLen > 0) ? std::wstring(backendEnv) : L"";
	for (auto& c : preferred) c = towlower(c);

	std::wstring openxrPath, openvrDetail;
	bool hasOpenXR = IsOpenXRAvailable(&openxrPath);
	bool hasOpenVR = IsOpenVRAvailable(&openvrDetail);

	if (hasOpenXR) {
		printf("[preflight] OpenXR runtime: %ls (ActiveRuntime registry key OK).\n", openxrPath.c_str());
	} else {
		std::wstring registered;
		if (GetRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", registered) ||
			GetRegistryString(HKEY_CURRENT_USER, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", registered)) {
			printf("[preflight] OpenXR runtime: ActiveRuntime points to missing file %ls - reinstall your headset runtime.\n",
				registered.c_str());
		} else {
			printf("[preflight] OpenXR runtime: none registered (HKLM/HKCU SOFTWARE\\Khronos\\OpenXR\\1 ActiveRuntime not set).\n");
		}
	}
	if (hasOpenVR) {
		printf("[preflight] OpenVR runtime: SteamVR vrserver found at %ls.\n", openvrDetail.c_str());
	} else {
		printf("[preflight] OpenVR runtime: SteamVR not found (no STEAMVR_RUNTIME_PATH and no Steam install with SteamVR).\n");
	}

	bool runtimeOk;
	if (preferred == L"openxr") {
		runtimeOk = hasOpenXR;
	} else if (preferred == L"openvr") {
		runtimeOk = hasOpenVR;
	} else {
		runtimeOk = hasOpenXR || hasOpenVR;
	}
	if (!runtimeOk) {
		if (preferred.empty()) {
			printf("[preflight] No usable VR runtime. To fix: install your headset's OpenXR runtime and set it as the\n");
			printf("            ActiveRuntime (SteamVR/Meta Link/Pimax/WMR all register one), or install SteamVR.\n");
			printf("            You can also force a backend with GTAVR_BACKEND=openxr or GTAVR_BACKEND=openvr.\n");
		} else {
			printf("[preflight] GTAVR_BACKEND=%ls but that runtime is missing. Install/register it, or clear\n",
				preferred.c_str());
			printf("            GTAVR_BACKEND to let the mod auto-detect.\n");
		}
		return GTAVR_EXIT_RUNTIME_MISSING;
	}

	// --- 3. BattlEye posture (warn only) ------------------------------------
	if (sliceOverride) {
		printf("[preflight] BattlEye: skipped (non-GTA target override).\n");
	} else {
		switch (CheckBattlEyePosture(pidOrZero)) {
		case kBattlEyeActive:
			printf("[preflight] WARNING: BattlEye is installed for GTA Online. Story-mode modding requires\n");
			printf("            BattlEye OFF (Rockstar launcher setting). The mod will refuse to activate\n");
			printf("            if BattlEye is active.\n");
			break;
		case kBattlEyeInstalledNotRunning:
			printf("[preflight] BattlEye: service installed but not running - OK for story mode.\n");
			printf("            The mod will refuse to activate if BattlEye becomes active.\n");
			break;
		default:
			printf("[preflight] BattlEye: not detected.\n");
			break;
		}
	}

	return GTAVR_EXIT_OK;
}

int wmain(int argc, wchar_t* argv[]) {
	// CLI: GTAVOVR.exe [--check] [target exe override]
	//   --check  run preflight only (no launch, no injection)
	//   argv[1]  (non-flag) target process override, as before
	bool checkOnly = false;
	std::wstring argTarget;
	for (int i = 1; i < argc; ++i) {
		if (!argv[i] || argv[i][0] == L'\0') {
			continue;
		}
		if (_wcsicmp(argv[i], L"--check") == 0) {
			checkOnly = true;
		} else if (_wcsicmp(argv[i], L"--help") == 0 || _wcsicmp(argv[i], L"-h") == 0 || _wcsicmp(argv[i], L"/?") == 0) {
			printf("GTAVOVR launcher (story-mode only; online sessions and BattlEye hard-disable the mod).\n");
			printf("usage: GTAVOVR.exe [--check] [target exe override]\n");
			printf("  --check   preflight only (build detection, VR runtime, BattlEye posture); no launch/inject\n");
			printf("exit codes: 0 ok-to-try, 2 unsupported build, 3 runtime missing, 4 injection failure\n");
			return GTAVR_EXIT_OK;
		} else if (argv[i][0] == L'-') {
			printf("Ignoring unknown option: %ls\n", argv[i]);
		} else if (argTarget.empty()) {
			argTarget = argv[i];
		}
	}

	std::wstring baseDir = GetInstallDir();
	std::wstring dllPath = baseDir + L"\\OVRInject.dll";
	std::wstring settingsPath = baseDir + L"\\gtavr_settings.ini";
	std::wstring cameraPath = baseDir + L"\\gtavr_camera.ini";
	// Give the injected DLL a reliable place to write logs when we launch the game.
	SetEnvironmentVariableW(L"GTAVR_LOG_DIR", baseDir.c_str());
	SetEnvironmentVariableW(L"GTAVR_SETTINGS_DIR", baseDir.c_str());
	SetEnvironmentVariableW(L"GTAVR_SETTINGS_PATH", settingsPath.c_str());
	SetEnvironmentVariableW(L"GTAVR_CAMERA_PATH", cameraPath.c_str());

	std::wstring exePathOverride = GetEnvVar(L"GTAV_EXE_PATH");
	std::wstring processName = GetEnvVar(L"GTAV_PROCESS_NAME");
	std::wstring gameExePath;

	if (!exePathOverride.empty()) {
		gameExePath = exePathOverride;
		size_t pos = exePathOverride.find_last_of(L"\\/");
		if (processName.empty()) {
			processName = (pos != std::wstring::npos) ? exePathOverride.substr(pos + 1) : exePathOverride;
		}
	} else {
		if (processName.empty()) {
			processName = L"GTA5.exe";
		}
		gameExePath = baseDir + L"\\" + processName;
	}

	// Vertical-slice override: target a non-GTA process (e.g. D3D11Cube.exe)
	// via argv[1] or GTAVR_TARGET_PROCESS. A full path is accepted; only the
	// exe name is used for process lookup. Without an override the original
	// GTA5.exe/PlayGTAV.exe behavior above is unchanged.
	std::wstring targetOverride;
	if (!argTarget.empty()) {
		targetOverride = argTarget;
	} else {
		targetOverride = GetEnvVar(L"GTAVR_TARGET_PROCESS");
	}
	if (!targetOverride.empty()) {
		size_t pos = targetOverride.find_last_of(L"\\/");
		processName = (pos != std::wstring::npos) ? targetOverride.substr(pos + 1) : targetOverride;
		if (exePathOverride.empty()) {
			gameExePath = (pos != std::wstring::npos) ? targetOverride : (baseDir + L"\\" + processName);
		}
	}
	bool sliceOverride = !targetOverride.empty();

	std::wstring launcherPath = GetEnvVar(L"GTAV_LAUNCHER_PATH");
	if (launcherPath.empty()) {
		std::wstring playPath = baseDir + L"\\PlayGTAV.exe";
		if (FileExists(playPath)) {
			launcherPath = playPath;
		}
	}

	// Preflight: build detection, VR runtime, BattlEye posture. Runs always
	// (never silent); --check stops here. A failed preflight aborts before any
	// launch/inject: with an unsupported build or no runtime the mod would
	// stay inert anyway.
	DWORD existingPid = FindProcessId(processName.c_str());
	int preflight = RunPreflight(gameExePath, processName, sliceOverride, existingPid);
	if (checkOnly) {
		printf("[preflight] --check complete, exit code %d.\n", preflight);
		return preflight;
	}
	if (preflight == GTAVR_EXIT_UNSUPPORTED_BUILD) {
		printf("Aborting: unsupported build. The mod would stay inert; not launching.\n");
		return preflight;
	}
	if (preflight == GTAVR_EXIT_RUNTIME_MISSING) {
		printf("Aborting: no usable VR runtime.\n");
		return preflight;
	}

	if (!FileExists(dllPath)) {
		printf("Missing OVRInject.dll (place it next to GTAVOVR.exe or set GTAV_INSTALL_DIR).\n");
		return GTAVR_EXIT_INJECTION_FAILURE;
	}

	// Stage runtime DLLs + manifest next to the payload, then print the
	// one-line readiness verdict (all preflight gates + staging results).
	int staged = StageRuntimeFiles(GetModuleDir(), DirNameOf(dllPath));
	bool manifestReady = FileExists(DirNameOf(dllPath) + L"\\gtav_legacy.ini");
	bool runtimesReady = FileExists(GetModuleDir() + L"\\openvr_api.dll") &&
	                     FileExists(GetModuleDir() + L"\\openxr_loader.dll");
	printf("[ready] build=OK runtime=OK battleye=clean manifest=%s openvr/openxr_dlls=%s staged=%d -> injectable\n",
		manifestReady ? "OK" : "MISSING", runtimesReady ? "OK" : "MISSING", staged);

	DWORD pid = existingPid;
	if (pid == 0) {
		std::wstring launchPath = launcherPath.empty() ? gameExePath : launcherPath;
		if (!FileExists(launchPath)) {
			printf("Launch target not found: %ls\n", launchPath.c_str());
			if (!launcherPath.empty()) {
				printf("Set GTAV_LAUNCHER_PATH or GTAV_EXE_PATH to a valid executable.\n");
			} else {
				printf("Place GTAVOVR.exe next to GTA5.exe or set GTAV_INSTALL_DIR/GTAV_EXE_PATH.\n");
			}
			return GTAVR_EXIT_INJECTION_FAILURE;
		}

		printf("Launching %ls...\n", launchPath.c_str());
		if (!LaunchProcess(launchPath)) {
			printf("Failed to launch %ls\n", launchPath.c_str());
			return GTAVR_EXIT_INJECTION_FAILURE;
		}

		if (!WaitForProcess(processName.c_str(), pid, 120000)) {
			printf("Timed out waiting for %ls\n", processName.c_str());
			return GTAVR_EXIT_INJECTION_FAILURE;
		}
	}

	std::wstring processPath;
	if (!baseDir.empty() && GetProcessImagePath(pid, processPath)) {
		if (!PathStartsWith(processPath, baseDir)) {
			printf("Warning: %ls is running from %ls (does not match GTAV_INSTALL_DIR %ls)\n",
				processName.c_str(), processPath.c_str(), baseDir.c_str());
		}
	}

	printf("Injecting %ls into PID %lu...\n", dllPath.c_str(), pid);
	if (!InjectDll(pid, dllPath)) {
		printf("Injection failed.\n");
		return GTAVR_EXIT_INJECTION_FAILURE;
	}

	printf("Injection successful.\n");
	return GTAVR_EXIT_OK;
}
