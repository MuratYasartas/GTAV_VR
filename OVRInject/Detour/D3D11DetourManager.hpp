#pragma once
#include "targetver.h"

namespace OVRInject
{
	// Loads d3d11/dxgi and kicks off VR hook installation (VRMgr::Initialize).
	// The old MinHook-based DetourManager base class was dead code and has been
	// removed; D3DHooks_VRManager.hpp initializes MinHook itself.
	// The destructor runs the ordered hook teardown (VRMgr::UninstallHooks),
	// so `delete detourManager` from DLL_PROCESS_DETACH is the unload path.
	class D3D11DetourManager
	{
	public:
		D3D11DetourManager();
		~D3D11DetourManager();
	};
}
