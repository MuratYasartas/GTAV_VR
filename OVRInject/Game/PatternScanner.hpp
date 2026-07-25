#pragma once

#include <atomic>
#include <cstdint>
#include <Windows.h>

namespace OVRInject {
namespace Game {

class PatternScanner {
public:
    static uintptr_t FindPattern(const char* pattern, const char* mask);
    static uintptr_t FindPattern(const char* pattern_string);
    static uintptr_t FindPattern(const char* pattern, const char* mask, HMODULE module);
    static uintptr_t FindPattern(const char* pattern_string, HMODULE module);

    // Abort-aware variant for background workers: checks abortFlag roughly
    // every 1MB scanned and returns 0 early when it becomes set (keeps worker
    // shutdown latency bounded; a plain full-module scan can take seconds).
    static uintptr_t FindPattern(const char* pattern_string, HMODULE module,
                                 const std::atomic<bool>* abortFlag);
};

} // namespace Game
} // namespace OVRInject
