#pragma once

#include "RenderModeInterface.h"

namespace GTA5VR {

// Alternating Eye Rendering (AER) Mode
// - Frame N: Left eye, Frame N+1: Right eye
// - Relies on reprojection for the "stale" eye
// - Works with almost any game (maximum compatibility)
// - Some temporal artifacts possible
// - Used by Luke Ross R.E.A.L. VR mod
class AlternatingEyeMode : public IRenderMode {
public:
    AlternatingEyeMode();
    ~AlternatingEyeMode() override;

    // IRenderMode implementation
    bool Initialize(VRCore* vrCore) override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;

    void BeginEye(uint32_t eyeIndex) override;
    void EndEye(uint32_t eyeIndex) override;

    uint32_t GetCurrentEye() const override;
    bool IsRenderingEye() const override;

    std::string GetName() const override { return "Alternating Eye Rendering"; }
    std::string GetDescription() const override {
        return "Alternates between eyes each frame. Maximum compatibility, relies on reprojection.";
    }

    bool RequiresDoubleRendering() const override { return false; }
    bool SupportsNativeInstancing() const override { return false; }
    float GetPerformanceImpact() const override { return 1.0f; } // Single eye per frame

    // AER specific
    uint64_t GetFrameCount() const { return m_frameCount; }
    bool IsLeftEyeFrame() const { return (m_frameCount % 2) == 0; }
    bool IsRightEyeFrame() const { return (m_frameCount % 2) == 1; }

    // Get the eye that was rendered last frame (for reprojection)
    uint32_t GetPreviousEye() const;

    // Get the saved texture from previous eye
    ID3D11Texture2D* GetPreviousEyeTexture() const;

private:
    bool CreatePreviousFrameBuffer();
    void CopyCurrentToPrevious();

    VRCore* m_vrCore = nullptr;

    uint64_t m_frameCount = 0;
    uint32_t m_currentEye = 0;
    bool m_isRendering = false;
    bool m_initialized = false;

    // Store previous frame for the other eye
    ID3D11Texture2D* m_previousEyeTexture = nullptr;
    uint32_t m_previousEyeIndex = 0;
};

} // namespace GTA5VR
