#include "targetver.h"

#include "D3D11DetourManager.hpp"

#include "D3DHook/D3DHooks_VRManager.hpp"

#include <d3d11.h>
#include <dxgi.h>

#include "Log.hpp"

using namespace OVRInject;

HMODULE d3d11Module;
HMODULE dxgiModule;

D3D11DetourManager::D3D11DetourManager()
{
	LOGSTRF("Attemping to hook D3D/DXGI device creation methods..\n");

	d3d11Module = LoadLibrary(L"d3d11.dll");
	LOGSTRF("D3D11Module: 0x%p\n", d3d11Module);

	dxgiModule = LoadLibrary(L"dxgi.dll");
	LOGSTRF("DXGIModule: 0x%p\n", dxgiModule);

	// Use the VRMgr initialization which sets up hooks internally
	VRMgr::Initialize();
}

D3D11DetourManager::~D3D11DetourManager()
{
	// Ordered unload path (DLL_PROCESS_DETACH / FreeLibrary): disable the
	// Present hook, drain in-flight hooked frames, shut down VR, then tear
	// down MinHook. Idempotent; safe to run even if nothing was hooked.
	VRMgr::UninstallHooks();
}
