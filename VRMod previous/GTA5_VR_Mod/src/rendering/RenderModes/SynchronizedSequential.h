#pragma once

#include "RenderModeInterface.h"

namespace GTA5VR {

// Synchronized Sequential Rendering Mode
// - Renders left eye completely, then right eye
// - Both eyes rendered on the same engine tick
// - No temporal artifacts
// - Good balance of compatibility and quality
class SynchronizedSequentialMode : public IRenderMode {
public:
    SynchronizedSequentialMode();
    ~SynchronizedSequentialMode() override;

    // IRenderMode implementation
    bool Initialize(VRCore* vrCore) override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;

    void BeginEye(uint32_t eyeIndex) override;
    void EndEye(uint32_t eyeIndex) override;

    uint32_t GetCurrentEye() const override;
    bool IsRenderingEye() const override;

    std::string GetName() const override { return "Synchronized Sequential"; }
    std::string GetDescription() const override {
        return "Renders each eye sequentially in the same frame. Balanced compatibility and quality.";
    }

    bool RequiresDoubleRendering() const override { return true; }
    bool SupportsNativeInstancing() const override { return false; }
    float GetPerformanceImpact() const override { return 2.0f; } // ~2x render time

    // State queries
    bool IsFirstEyeComplete() const { return m_firstEyeComplete; }
    bool IsBothEyesComplete() const { return m_bothEyesComplete; }

private:
    void SaveRenderState();
    void RestoreRenderState();

    VRCore* m_vrCore = nullptr;

    uint32_t m_currentEye = 0;
    bool m_isRendering = false;
    bool m_initialized = false;

    // Frame state
    bool m_firstEyeComplete = false;
    bool m_bothEyesComplete = false;
    uint64_t m_frameStartTime = 0;

    // Saved render state for restoration between eyes
    struct SavedRenderState {
        ID3D11RenderTargetView* rtv[8];
        ID3D11DepthStencilView* dsv;
        D3D11_VIEWPORT viewports[8];
        UINT numViewports;
    };
    SavedRenderState m_savedState;
};

} // namespace GTA5VR
