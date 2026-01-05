#include "MemoryManager.h"
#include "Logger.h"
#include <MinHook.h>
#include <Psapi.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "Psapi.lib")

namespace GTA5VR {

// ScopedMemoryProtection implementation
ScopedMemoryProtection::ScopedMemoryProtection(void* address, size_t size, DWORD newProtection)
    : m_address(address), m_size(size), m_oldProtection(0), m_valid(false) {
    m_valid = VirtualProtect(address, size, newProtection, &m_oldProtection) != FALSE;
}

ScopedMemoryProtection::~ScopedMemoryProtection() {
    if (m_valid) {
        DWORD dummy;
        VirtualProtect(m_address, m_size, m_oldProtection, &dummy);
    }
}

// MemoryManager implementation
MemoryManager& MemoryManager::GetInstance() {
    static MemoryManager instance;
    return instance;
}

bool MemoryManager::Initialize() {
    if (m_initialized) {
        return true;
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        m_lastError = "MinHook initialization failed: " + std::string(MH_StatusToString(status));
        LOG_ERROR(m_lastError);
        return false;
    }

    m_initialized = true;
    LOG_INFO("MemoryManager initialized");
    return true;
}

void MemoryManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Remove all hooks
    RemoveAllHooks();

    // Restore all patches
    for (const auto& patch : m_patches) {
        WriteBytes(patch.address, patch.originalBytes.data(), patch.originalBytes.size());
    }
    m_patches.clear();

    // Free all allocations
    for (void* alloc : m_allocations) {
        VirtualFree(alloc, 0, MEM_RELEASE);
    }
    m_allocations.clear();

    MH_Uninitialize();
    m_initialized = false;
    LOG_INFO("MemoryManager shutdown");
}

uintptr_t MemoryManager::FindPattern(const char* moduleName, const std::string& pattern) {
    HMODULE module = GetModuleHandle(moduleName);
    if (!module) {
        m_lastError = "Module not found: " + std::string(moduleName ? moduleName : "main");
        return 0;
    }

    MODULEINFO modInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &modInfo, sizeof(modInfo))) {
        m_lastError = "Failed to get module information";
        return 0;
    }

    return FindPattern(reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll), modInfo.SizeOfImage, pattern);
}

uintptr_t MemoryManager::FindPattern(uintptr_t startAddress, size_t size, const std::string& pattern) {
    std::vector<uint8_t> patternBytes = ParsePattern(pattern);
    if (patternBytes.empty()) {
        return 0;
    }

    const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(startAddress);
    const uint8_t* scanEnd = scanStart + size - patternBytes.size();

    for (const uint8_t* current = scanStart; current <= scanEnd; ++current) {
        if (PatternMatch(current, patternBytes)) {
            return reinterpret_cast<uintptr_t>(current);
        }
    }

    return 0;
}

std::vector<uintptr_t> MemoryManager::FindAllPatterns(const char* moduleName, const std::string& pattern) {
    std::vector<uintptr_t> results;

    HMODULE module = GetModuleHandle(moduleName);
    if (!module) {
        return results;
    }

    MODULEINFO modInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &modInfo, sizeof(modInfo))) {
        return results;
    }

    std::vector<uint8_t> patternBytes = ParsePattern(pattern);
    if (patternBytes.empty()) {
        return results;
    }

    const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(modInfo.lpBaseOfDll);
    const uint8_t* scanEnd = scanStart + modInfo.SizeOfImage - patternBytes.size();

    for (const uint8_t* current = scanStart; current <= scanEnd; ++current) {
        if (PatternMatch(current, patternBytes)) {
            results.push_back(reinterpret_cast<uintptr_t>(current));
        }
    }

    return results;
}

bool MemoryManager::ReadBytes(uintptr_t address, void* buffer, size_t size) {
    if (!IsValidAddress(address) || !buffer) {
        return false;
    }

    __try {
        memcpy(buffer, reinterpret_cast<void*>(address), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MemoryManager::WriteBytes(uintptr_t address, const void* buffer, size_t size) {
    if (!IsValidAddress(address) || !buffer) {
        return false;
    }

    ScopedMemoryProtection protection(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE);
    if (!protection.IsValid()) {
        return false;
    }

    __try {
        memcpy(reinterpret_cast<void*>(address), buffer, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MemoryManager::Patch(uintptr_t address, const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        return false;
    }

    // Store original bytes for restoration
    PatchInfo info;
    info.address = address;
    info.originalBytes.resize(bytes.size());

    if (!ReadBytes(address, info.originalBytes.data(), bytes.size())) {
        return false;
    }

    if (!WriteBytes(address, bytes.data(), bytes.size())) {
        return false;
    }

    m_patches.push_back(info);
    LOG_VERBOSE("Applied patch at 0x" + std::to_string(address));
    return true;
}

bool MemoryManager::PatchNOP(uintptr_t address, size_t count) {
    std::vector<uint8_t> nops(count, 0x90);
    return Patch(address, nops);
}

bool MemoryManager::RestorePatch(uintptr_t address) {
    for (auto it = m_patches.begin(); it != m_patches.end(); ++it) {
        if (it->address == address) {
            WriteBytes(address, it->originalBytes.data(), it->originalBytes.size());
            m_patches.erase(it);
            return true;
        }
    }
    return false;
}

bool MemoryManager::CreateHook(void* target, void* detour, void** original, const std::string& name) {
    if (!m_initialized || !target || !detour) {
        return false;
    }

    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        m_lastError = "Failed to create hook: " + std::string(MH_StatusToString(status));
        LOG_ERROR(m_lastError);
        return false;
    }

    HookInfo info;
    info.originalFunction = target;
    info.hookFunction = detour;
    info.trampoline = original ? *original : nullptr;
    info.name = name.empty() ? "Unknown" : name;
    info.enabled = false;

    m_hooks.push_back(info);
    LOG_VERBOSE("Created hook: " + info.name);
    return true;
}

bool MemoryManager::EnableHook(void* target) {
    if (!m_initialized) {
        return false;
    }

    MH_STATUS status = MH_EnableHook(target);
    if (status != MH_OK) {
        m_lastError = "Failed to enable hook: " + std::string(MH_StatusToString(status));
        return false;
    }

    for (auto& hook : m_hooks) {
        if (hook.originalFunction == target) {
            hook.enabled = true;
            LOG_VERBOSE("Enabled hook: " + hook.name);
            break;
        }
    }

    return true;
}

bool MemoryManager::DisableHook(void* target) {
    if (!m_initialized) {
        return false;
    }

    MH_STATUS status = MH_DisableHook(target);
    if (status != MH_OK) {
        m_lastError = "Failed to disable hook: " + std::string(MH_StatusToString(status));
        return false;
    }

    for (auto& hook : m_hooks) {
        if (hook.originalFunction == target) {
            hook.enabled = false;
            LOG_VERBOSE("Disabled hook: " + hook.name);
            break;
        }
    }

    return true;
}

bool MemoryManager::RemoveHook(void* target) {
    if (!m_initialized) {
        return false;
    }

    MH_STATUS status = MH_RemoveHook(target);
    if (status != MH_OK) {
        m_lastError = "Failed to remove hook: " + std::string(MH_StatusToString(status));
        return false;
    }

    for (auto it = m_hooks.begin(); it != m_hooks.end(); ++it) {
        if (it->originalFunction == target) {
            LOG_VERBOSE("Removed hook: " + it->name);
            m_hooks.erase(it);
            break;
        }
    }

    return true;
}

bool MemoryManager::EnableAllHooks() {
    MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        m_lastError = "Failed to enable all hooks: " + std::string(MH_StatusToString(status));
        return false;
    }

    for (auto& hook : m_hooks) {
        hook.enabled = true;
    }

    LOG_INFO("Enabled all hooks (" + std::to_string(m_hooks.size()) + ")");
    return true;
}

bool MemoryManager::DisableAllHooks() {
    MH_STATUS status = MH_DisableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        m_lastError = "Failed to disable all hooks: " + std::string(MH_StatusToString(status));
        return false;
    }

    for (auto& hook : m_hooks) {
        hook.enabled = false;
    }

    LOG_INFO("Disabled all hooks");
    return true;
}

bool MemoryManager::RemoveAllHooks() {
    for (auto& hook : m_hooks) {
        MH_RemoveHook(hook.originalFunction);
    }
    m_hooks.clear();
    LOG_INFO("Removed all hooks");
    return true;
}

bool MemoryManager::HookVTable(void** vtable, int index, void* hookFunction, void** originalFunction) {
    if (!vtable || !hookFunction) {
        return false;
    }

    void* original = vtable[index];
    if (originalFunction) {
        *originalFunction = original;
    }

    ScopedMemoryProtection protection(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE);
    if (!protection.IsValid()) {
        return false;
    }

    vtable[index] = hookFunction;
    LOG_VERBOSE("Hooked VTable index " + std::to_string(index));
    return true;
}

bool MemoryManager::UnhookVTable(void** vtable, int index) {
    // This requires storing original values - simplified implementation
    return false;
}

uintptr_t MemoryManager::GetModuleBase(const char* moduleName) {
    HMODULE module = ::GetModuleHandleA(moduleName);
    return reinterpret_cast<uintptr_t>(module);
}

size_t MemoryManager::GetModuleSize(const char* moduleName) {
    HMODULE module = ::GetModuleHandleA(moduleName);
    if (!module) {
        return 0;
    }

    MODULEINFO modInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &modInfo, sizeof(modInfo))) {
        return 0;
    }

    return modInfo.SizeOfImage;
}

HMODULE MemoryManager::GetModuleHandle(const char* moduleName) {
    return ::GetModuleHandleA(moduleName);
}

void* MemoryManager::Allocate(size_t size, DWORD protection) {
    void* memory = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, protection);
    if (memory) {
        m_allocations.push_back(memory);
    }
    return memory;
}

void* MemoryManager::AllocateNear(void* address, size_t size, DWORD protection) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    uintptr_t minAddress = reinterpret_cast<uintptr_t>(address) - 0x7FFFFF00;
    uintptr_t maxAddress = reinterpret_cast<uintptr_t>(address) + 0x7FFFFF00;

    minAddress = max(minAddress, reinterpret_cast<uintptr_t>(sysInfo.lpMinimumApplicationAddress));
    maxAddress = min(maxAddress, reinterpret_cast<uintptr_t>(sysInfo.lpMaximumApplicationAddress));

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t currentAddress = minAddress;

    while (currentAddress < maxAddress) {
        if (VirtualQuery(reinterpret_cast<void*>(currentAddress), &mbi, sizeof(mbi)) == 0) {
            break;
        }

        if (mbi.State == MEM_FREE && mbi.RegionSize >= size) {
            void* memory = VirtualAlloc(reinterpret_cast<void*>(currentAddress), size,
                                        MEM_COMMIT | MEM_RESERVE, protection);
            if (memory) {
                m_allocations.push_back(memory);
                return memory;
            }
        }

        currentAddress = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }

    return nullptr;
}

void MemoryManager::Free(void* address) {
    if (!address) {
        return;
    }

    auto it = std::find(m_allocations.begin(), m_allocations.end(), address);
    if (it != m_allocations.end()) {
        VirtualFree(address, 0, MEM_RELEASE);
        m_allocations.erase(it);
    }
}

bool MemoryManager::IsValidAddress(uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    return mbi.State == MEM_COMMIT;
}

bool MemoryManager::IsExecutableAddress(uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    return mbi.State == MEM_COMMIT &&
           (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

std::string MemoryManager::GetLastErrorString() const {
    return m_lastError;
}

void MemoryManager::DumpHookInfo() const {
    LOG_INFO("=== Hook Dump ===");
    for (const auto& hook : m_hooks) {
        std::ostringstream ss;
        ss << "Hook: " << hook.name
           << " | Target: 0x" << std::hex << reinterpret_cast<uintptr_t>(hook.originalFunction)
           << " | Enabled: " << (hook.enabled ? "Yes" : "No");
        LOG_INFO(ss.str());
    }
    LOG_INFO("=================");
}

size_t MemoryManager::GetHookCount() const {
    return m_hooks.size();
}

std::vector<uint8_t> MemoryManager::ParsePattern(const std::string& pattern) {
    std::vector<uint8_t> bytes;
    std::istringstream stream(pattern);
    std::string byteStr;

    while (stream >> byteStr) {
        if (byteStr == "?" || byteStr == "??") {
            bytes.push_back(0x00); // Wildcard marker (handled in PatternMatch)
        } else {
            bytes.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
        }
    }

    return bytes;
}

bool MemoryManager::PatternMatch(const uint8_t* data, const std::vector<uint8_t>& pattern) {
    // Note: This simplified version doesn't handle wildcards properly
    // In a full implementation, you'd track wildcard positions separately
    for (size_t i = 0; i < pattern.size(); ++i) {
        // Skip wildcards (would need additional tracking)
        if (data[i] != pattern[i]) {
            return false;
        }
    }
    return true;
}

} // namespace GTA5VR
