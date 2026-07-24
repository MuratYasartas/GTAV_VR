// Do NOT include dxgi.h or dxgi1_3.h here - they declare the DXGI factory
// functions with __declspec(dllimport) which conflicts with our dllexport.
// We only need Windows.h for REFIID (via objbase.h) and basic types.
#include <Windows.h>
#include <string>

#include "Log.hpp"

namespace {

HMODULE g_dxgi = nullptr;

HMODULE LoadSystemDxgi() {
    if (g_dxgi) {
        return g_dxgi;
    }

    wchar_t systemDir[MAX_PATH] = {};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) == 0) {
        return nullptr;
    }

    std::wstring path(systemDir);
    path += L"\\dxgi.dll";
    g_dxgi = LoadLibraryW(path.c_str());
    if (!g_dxgi) {
        LOGWNDF("DXGI proxy: Failed to load system dxgi.dll\n");
    }
    return g_dxgi;
}

template <typename T>
T LoadDxgiProc(const char* name) {
    HMODULE module = LoadSystemDxgi();
    if (!module) {
        return nullptr;
    }
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

} // namespace

extern "C" __declspec(dllexport)
HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
    using Fn = HRESULT (WINAPI*)(REFIID, void**);
    static Fn fn = LoadDxgiProc<Fn>("CreateDXGIFactory");
    if (!fn) {
        return E_FAIL;
    }
    return fn(riid, ppFactory);
}

extern "C" __declspec(dllexport)
HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    using Fn = HRESULT (WINAPI*)(REFIID, void**);
    static Fn fn = LoadDxgiProc<Fn>("CreateDXGIFactory1");
    if (!fn) {
        return E_FAIL;
    }
    return fn(riid, ppFactory);
}

extern "C" __declspec(dllexport)
HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) {
    using Fn = HRESULT (WINAPI*)(UINT, REFIID, void**);
    static Fn fn = LoadDxgiProc<Fn>("CreateDXGIFactory2");
    if (!fn) {
        return E_FAIL;
    }
    return fn(flags, riid, ppFactory);
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DXGIDeclareAdapterRemovalSupport(void) {
    using Fn = HRESULT (WINAPI*)(void);
    static Fn fn = LoadDxgiProc<Fn>("DXGIDeclareAdapterRemovalSupport");
    if (!fn) {
        return E_FAIL;
    }
    return fn();
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID riid, void** ppDebug) {
    using Fn = HRESULT (WINAPI*)(UINT, REFIID, void**);
    static Fn fn = LoadDxgiProc<Fn>("DXGIGetDebugInterface1");
    if (!fn) {
        return E_FAIL;
    }
    return fn(flags, riid, ppDebug);
}
