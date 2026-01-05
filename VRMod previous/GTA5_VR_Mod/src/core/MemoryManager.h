#pragma once

#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace GTA5VR {

// Hook information structure
struct HookInfo {
    void* originalFunction;
    void* hookFunction;
    void* trampoline;
    std::string name;
    bool enabled;
};

// Memory protection scope guard
class ScopedMemoryProtection {
public:
    ScopedMemoryProtection(void* address, size_t size, DWORD newProtection);
    ~ScopedMemoryProtection();

    bool IsValid() const { return m_valid; }

private:
    void* m_address;
    size_t m_size;
    DWORD m_oldProtection;
    bool m_valid;
};

class MemoryManager {
public:
    static MemoryManager& GetInstance();

    // Initialization
    bool Initialize();
    void Shutdown();

    // Pattern scanning
    uintptr_t FindPattern(const char* moduleName, const std::string& pattern);
    uintptr_t FindPattern(uintptr_t startAddress, size_t size, const std::string& pattern);
    std::vector<uintptr_t> FindAllPatterns(const char* moduleName, const std::string& pattern);

    // Memory read/write
    template<typename T>
    T Read(uintptr_t address);

    template<typename T>
    bool Write(uintptr_t address, T value);

    bool ReadBytes(uintptr_t address, void* buffer, size_t size);
    bool WriteBytes(uintptr_t address, const void* buffer, size_t size);

    // Memory patching
    bool Patch(uintptr_t address, const std::vector<uint8_t>& bytes);
    bool PatchNOP(uintptr_t address, size_t count);
    bool RestorePatch(uintptr_t address);

    // Hooking (using MinHook)
    bool CreateHook(void* target, void* detour, void** original, const std::string& name = "");
    bool EnableHook(void* target);
    bool DisableHook(void* target);
    bool RemoveHook(void* target);
    bool EnableAllHooks();
    bool DisableAllHooks();
    bool RemoveAllHooks();

    // Virtual function table hooking
    bool HookVTable(void** vtable, int index, void* hookFunction, void** originalFunction);
    bool UnhookVTable(void** vtable, int index);

    // Module information
    uintptr_t GetModuleBase(const char* moduleName = nullptr);
    size_t GetModuleSize(const char* moduleName = nullptr);
    HMODULE GetModuleHandle(const char* moduleName = nullptr);

    // Memory allocation
    void* Allocate(size_t size, DWORD protection = PAGE_EXECUTE_READWRITE);
    void* AllocateNear(void* address, size_t size, DWORD protection = PAGE_EXECUTE_READWRITE);
    void Free(void* address);

    // Utility
    bool IsValidAddress(uintptr_t address);
    bool IsExecutableAddress(uintptr_t address);
    std::string GetLastErrorString() const;

    // Debug
    void DumpHookInfo() const;
    size_t GetHookCount() const;

private:
    MemoryManager() = default;
    ~MemoryManager() = default;
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    std::vector<uint8_t> ParsePattern(const std::string& pattern);
    bool PatternMatch(const uint8_t* data, const std::vector<uint8_t>& pattern);

    struct PatchInfo {
        uintptr_t address;
        std::vector<uint8_t> originalBytes;
    };

    std::vector<HookInfo> m_hooks;
    std::vector<PatchInfo> m_patches;
    std::vector<void*> m_allocations;
    bool m_initialized = false;
    std::string m_lastError;
};

// Template implementations
template<typename T>
T MemoryManager::Read(uintptr_t address) {
    T value{};
    if (IsValidAddress(address)) {
        __try {
            value = *reinterpret_cast<T*>(address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // Access violation, return default value
        }
    }
    return value;
}

template<typename T>
bool MemoryManager::Write(uintptr_t address, T value) {
    if (!IsValidAddress(address)) {
        return false;
    }

    ScopedMemoryProtection protection(reinterpret_cast<void*>(address), sizeof(T), PAGE_EXECUTE_READWRITE);
    if (!protection.IsValid()) {
        return false;
    }

    __try {
        *reinterpret_cast<T*>(address) = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace GTA5VR
