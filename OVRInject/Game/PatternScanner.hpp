#pragma once

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
};

} // namespace Game
} // namespace OVRInject
