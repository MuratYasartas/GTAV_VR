#pragma once

#include <d3d11.h>
#include <cstdint>
#include <memory>

namespace GTA5VR {

class VRCore;
class IRenderMode;

class StereoRenderer {
public:
    static StereoRenderer& GetInstance();

    bool Initialize(VRCore* vrCore);
    void Shutdown();

    // Frame rendering
    void BeginFrame();
    void EndFrame();

    // Eye rendering
    void RenderEye(uint32_t eyeIndex);
    void SubmitEyes();

    // State
    bool IsActive() const { return m_active; }
    uint32_t GetCurrentEye() const;

    // Render targets
    ID3D11RenderTargetView* GetEyeRTV(uint32_t eye) const;
    ID3D11DepthStencilView* GetEyeDSV(uint32_t eye) const;
    ID3D11ShaderResourceView* GetEyeSRV(uint32_t eye) const;

private:
    StereoRenderer() = default;
    ~StereoRenderer() = default;
    StereoRenderer(const StereoRenderer&) = delete;
    StereoRenderer& operator=(const StereoRenderer&) = delete;

    VRCore* m_vrCore = nullptr;
    bool m_active = false;
    bool m_initialized = false;
};

} // namespace GTA5VR
