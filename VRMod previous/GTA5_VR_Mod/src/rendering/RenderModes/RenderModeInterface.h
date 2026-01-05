#pragma once

#include <string>
#include <cstdint>
#include <d3d11.h>

namespace GTA5VR {

class VRCore;

// Abstract render mode interface
class IRenderMode {
public:
    virtual ~IRenderMode() = default;

    // Lifecycle
    virtual bool Initialize(VRCore* vrCore) = 0;
    virtual void Shutdown() = 0;

    // Frame management
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    // Eye rendering
    virtual void BeginEye(uint32_t eyeIndex) = 0;
    virtual void EndEye(uint32_t eyeIndex) = 0;

    // Current state
    virtual uint32_t GetCurrentEye() const = 0;
    virtual bool IsRenderingEye() const = 0;

    // Information
    virtual std::string GetName() const = 0;
    virtual std::string GetDescription() const = 0;

    // Capabilities
    virtual bool RequiresDoubleRendering() const = 0;
    virtual bool SupportsNativeInstancing() const = 0;
    virtual float GetPerformanceImpact() const = 0;
};

} // namespace GTA5VR
