#include "PatternScanner.hpp"
#include "../Log.hpp"
#include <Windows.h>
#include <Psapi.h>
#include <cstring>
#include <vector>
#include <sstream>

namespace OVRInject {
namespace Game {

static uintptr_t FindPatternInRange(const char* pattern, const char* mask, uintptr_t start_address, uintptr_t end_address) {
    size_t pattern_len = strlen(mask);

    for (uintptr_t i = start_address; i < end_address - pattern_len; i++) {
        bool found = true;
        for (size_t j = 0; j < pattern_len; j++) {
            if (mask[j] != '?' && pattern[j] != *(char*)(i + j)) {
                found = false;
                break;
            }
        }

        if (found) {
            return i;
        }
    }

    return 0;
}

uintptr_t PatternScanner::FindPattern(const char* pattern, const char* mask) {
    return FindPattern(pattern, mask, GetModuleHandle(NULL));
}

uintptr_t PatternScanner::FindPattern(const char* pattern_string) {
    return FindPattern(pattern_string, GetModuleHandle(NULL));
}

uintptr_t PatternScanner::FindPattern(const char* pattern, const char* mask, HMODULE module) {
    if (!module) {
        module = GetModuleHandle(NULL);
    }

    MODULEINFO module_info = {};
    if (!GetModuleInformation(GetCurrentProcess(), module, &module_info, sizeof(MODULEINFO))) {
        return 0;
    }

    uintptr_t start_address = reinterpret_cast<uintptr_t>(module_info.lpBaseOfDll);
    uintptr_t end_address = start_address + module_info.SizeOfImage;
    return FindPatternInRange(pattern, mask, start_address, end_address);
}

uintptr_t PatternScanner::FindPattern(const char* pattern_string, HMODULE module) {
    std::vector<char> pattern;
    std::vector<char> mask;

    std::stringstream ss(pattern_string);
    std::string byte_str;

    while (ss >> byte_str) {
        if (byte_str == "?" || byte_str == "??") {
            pattern.push_back(0x00);
            mask.push_back('?');
        } else {
            pattern.push_back((char)std::stoul(byte_str, nullptr, 16));
            mask.push_back('x');
        }
    }

    return FindPattern(pattern.data(), mask.data(), module);
}

} // namespace Game
} // namespace OVRInject
