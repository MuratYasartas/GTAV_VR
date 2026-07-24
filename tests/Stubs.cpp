// Link stubs for production translation units compiled into the test exe.
//
// OVRInject/OpenXR/XRCore.cpp references two kinds of externals that must
// not drag their real implementations into a unit-test binary:
//   1. The mod's logging functions (OVRInject/Log.hpp) -- no-ops here so
//      test output stays clean.
//   2. OpenXR loader entry points used by CheckXrResult /
//      EnumerateExtensions -- minimal stubs; the math under test never
//      calls them, but the linker still needs the symbols.

#include <cstdarg>
#include <cstdio>

#include "../OVRInject/Log.hpp"

#include <openxr/openxr.h>

void LOGSTRF(const char* format, ...) {
    (void)format;
}

void LOGWNDF(const char* format, ...) {
    (void)format;
}

void LOGOUTF(const char* format, ...) {
    (void)format;
}

void LOGFATALF(const char* format, ...) {
    (void)format;
}

extern "C" {

XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(
    XrInstance instance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE]) {
    (void)instance;
    (void)value;
    if (buffer != nullptr) {
        std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_STUB");
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(
    const char* layerName,
    uint32_t propertyCapacityInput,
    uint32_t* propertyCountOutput,
    XrExtensionProperties* properties) {
    (void)layerName;
    (void)propertyCapacityInput;
    (void)properties;
    if (propertyCountOutput != nullptr) {
        *propertyCountOutput = 0;
    }
    return XR_SUCCESS;
}

} // extern "C"
