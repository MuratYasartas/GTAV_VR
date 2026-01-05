#include "StereoRenderer.h"
#include "../core/VRCore.h"
#include "../core/Logger.h"
#include "RenderModes/RenderModeInterface.h"

namespace GTA5VR {

StereoRenderer& StereoRenderer::GetInstance() {
    static StereoRenderer instance;
    return instance;
}

bool StereoRenderer::Initialize(VRCore* vrCore) {
    if (m_initialized) return true;
    m_vrCore = vrCore;
    m_initialized = true;
    LOG_INFO("StereoRenderer initialized");
    return true;
}

void StereoRenderer::Shutdown() {
    m_initialized = false;
    m_active = false;
}

void StereoRenderer::BeginFrame() {
    if (!m_initialized) return;
    m_active = true;
    if (auto* mode = m_vrCore->GetRenderMode()) {
        mode->BeginFrame();
    }
}

void StereoRenderer::EndFrame() {
    if (!m_initialized) return;
    if (auto* mode = m_vrCore->GetRenderMode()) {
        mode->EndFrame();
    }
    m_active = false;
}

void StereoRenderer::RenderEye(uint32_t eyeIndex) {
    if (!m_initialized || eyeIndex >= 2) return;
    if (auto* mode = m_vrCore->GetRenderMode()) {
        mode->BeginEye(eyeIndex);
    }
}

void StereoRenderer::SubmitEyes() {
    if (!m_initialized) return;
    m_vrCore->SubmitFrame();
}

uint32_t StereoRenderer::GetCurrentEye() const {
    if (!m_vrCore) return 0;
    return m_vrCore->GetCurrentEye();
}

ID3D11RenderTargetView* StereoRenderer::GetEyeRTV(uint32_t eye) const {
    if (!m_vrCore) return nullptr;
    return m_vrCore->GetEyeRenderTarget(eye);
}

ID3D11DepthStencilView* StereoRenderer::GetEyeDSV(uint32_t eye) const {
    if (!m_vrCore) return nullptr;
    return m_vrCore->GetEyeDepthStencil(eye);
}

ID3D11ShaderResourceView* StereoRenderer::GetEyeSRV(uint32_t eye) const {
    return nullptr; // Would need to be created from texture
}

} // namespace GTA5VR
