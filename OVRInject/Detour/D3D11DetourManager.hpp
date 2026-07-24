#pragma once
#include "targetver.h"

namespace OVRInject
{
	// Loads d3d11/dxgi and kicks off VR hook installation (VRMgr::Initialize).
	// The old MinHook-based DetourManager base class was dead code and has been
	// removed; D3DHooks_VRManager.hpp initializes MinHook itself.
	class D3D11DetourManager
	{
	public:
		D3D11DetourManager();
		~D3D11DetourManager();
	};

	// Backward-compat alias: dllmain.cpp declares `DetourManager* detourManager`
	// and assigns `new D3D11DetourManager()`; keep that name resolving to this
	// type now that the DetourManager base class is gone.
	using DetourManager = D3D11DetourManager;
}
