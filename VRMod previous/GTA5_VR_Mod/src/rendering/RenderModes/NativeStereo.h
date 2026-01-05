#pragma once

#include "RenderModeInterface.h"

namespace GTA5VR {

// Native Stereo Rendering Mode
// - Renders both eyes in a single draw call using instancing
// - Uses texture array swapchains (arraySize=2)
// - Best performance when supported
// - May cause compatibility issues with some games
class NativeStereoMode : public IRenderMode {
public:
    NativeStereoMode();
    ~NativeStereoMode() override;

    // IRenderMode implementation
    bool Initialize(VRCore* vrCore) override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;

    void BeginEye(uint32_t eyeIndex) override;
    void EndEye(uint32_t eyeIndex) override;

    uint32_t GetCurrentEye() const override;
    bool IsRenderingEye() const override;

    std::string GetName() const override { return "Native Stereo"; }
    std::string GetDescription() const override {
        return "Renders both eyes in single draw calls using stereo instancing. Best performance.";
    }

    bool RequiresDoubleRendering() const override { return false; }
    bool SupportsNativeInstancing() const override { return true; }
    float GetPerformanceImpact() const override { return 1.0f; } // Baseline

private:
    bool SetupStereoConstantBuffer();
    void UpdateStereoParams(uint32_t eyeIndex);

    VRCore* m_vrCore = nullptr;
    ID3D11Buffer* m_stereoConstantBuffer = nullptr;

    uint32_t m_currentEye = 0;
    bool m_isRendering = false;
    bool m_initialized = false;

    // Stereo parameters for shader
    struct StereoParams {
        float separation;
        float convergence;
        float eyeIndex;   // -1 for left, +1 for right
        float enabled;
    };
    StereoParams m_stereoParams;
};

} // namespace GTA5VR
