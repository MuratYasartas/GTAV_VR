#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cwctype>

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

static bool IsOpenXRAvailable() {
	std::wstring runtimePath;
	if (GetRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", runtimePath) ||
		GetRegistryString(HKEY_CURRENT_USER, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", runtimePath)) {
		return FileExists(runtimePath);
	}
	return false;
}

static bool IsOpenVRAvailable() {
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

static bool RuntimeCheckOk() {
	wchar_t backend[64] = {};
	DWORD len = GetEnvironmentVariableW(L"GTAVR_BACKEND", backend, 64);
	std::wstring preferred = (len > 0) ? std::wstring(backend) : L"";
	for (auto& c : preferred) c = towlower(c);

	bool hasOpenXR = IsOpenXRAvailable();
	bool hasOpenVR = IsOpenVRAvailable();

	if (preferred == L"openxr") {
		return hasOpenXR;
	}
	if (preferred == L"openvr") {
		return hasOpenVR;
	}

	return hasOpenXR || hasOpenVR;
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

int wmain(int argc, wchar_t* argv[]) {
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
	if (argc > 1 && argv[1] && argv[1][0] != L'\0') {
		targetOverride = argv[1];
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

	std::wstring launcherPath = GetEnvVar(L"GTAV_LAUNCHER_PATH");
	if (launcherPath.empty()) {
		std::wstring playPath = baseDir + L"\\PlayGTAV.exe";
		if (FileExists(playPath)) {
			launcherPath = playPath;
		}
	}

	if (!FileExists(dllPath)) {
		printf("Missing OVRInject.dll (place it next to GTAVOVR.exe or set GTAV_INSTALL_DIR).\n");
		return 1;
	}

	if (!RuntimeCheckOk()) {
		printf("No OpenXR/OpenVR runtime detected (or preferred runtime missing).\n");
		printf("Set GTAVR_BACKEND=openxr/openvr or install a runtime.\n");
		return 1;
	}

	DWORD pid = FindProcessId(processName.c_str());
	if (pid == 0) {
		std::wstring launchPath = launcherPath.empty() ? gameExePath : launcherPath;
		if (!FileExists(launchPath)) {
			printf("Launch target not found: %ls\n", launchPath.c_str());
			if (!launcherPath.empty()) {
				printf("Set GTAV_LAUNCHER_PATH or GTAV_EXE_PATH to a valid executable.\n");
			} else {
				printf("Place GTAVOVR.exe next to GTA5.exe or set GTAV_INSTALL_DIR/GTAV_EXE_PATH.\n");
			}
			return 1;
		}

		printf("Launching %ls...\n", launchPath.c_str());
		if (!LaunchProcess(launchPath)) {
			printf("Failed to launch %ls\n", launchPath.c_str());
			return 1;
		}

		if (!WaitForProcess(processName.c_str(), pid, 120000)) {
			printf("Timed out waiting for %ls\n", processName.c_str());
			return 1;
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
		return 1;
	}

	printf("Injection successful.\n");
	return 0;
}
