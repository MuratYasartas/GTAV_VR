#include "targetver.h"

#include <Windows.h>

#include "Detour/D3D11DetourManager.hpp"
#include "CrashDump.hpp"

#include "Log.hpp"

using namespace OVRInject;

D3D11DetourManager* detourManager = nullptr;

void init();
DWORD WINAPI InitThread(LPVOID);

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		DisableThreadLibraryCalls(hModule);
		// Crash handler first: it covers everything the init thread does next.
		CrashDump::Install(hModule);
		CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);

		break;
	}
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
	{
		// lpReserved != NULL means the process is terminating: the other
		// threads are already gone and DLLs are being torn down, so touching
		// hooks/D3D/XR here risks deadlocks and crashes for zero benefit -
		// the OS reclaims everything. Only a FreeLibrary unload
		// (lpReserved == NULL) runs the ordered shutdown:
		// stop camera writes -> disable Present hook -> drain in-flight
		// hooked frames -> XR backend Shutdown -> overlay/imgui shutdown ->
		// remove hooks -> MH_Uninitialize (see VRMgr::UninstallHooks; runs
		// from ~D3D11DetourManager, never from inside a hooked frame).
		if (lpReserved == nullptr) {
			if (detourManager) {
				delete detourManager;
				detourManager = nullptr;
			}
		} else {
			LOGSTR("OVRInject: process terminating - skipping hook teardown, OS reclaims resources\n");
		}
		CrashDump::Uninstall();
		break;
	}
	}
	return TRUE;
}

void init()
{
	detourManager = new D3D11DetourManager();
}

DWORD WINAPI InitThread(LPVOID)
{
	LOGSTRF("Attached main DLL to GTA: V\n");
	try {
		init();
	} catch (...) {
		LOGSTRF("OVRInject init failed (exception during detour setup)\n");
	}
	return 0;
}
