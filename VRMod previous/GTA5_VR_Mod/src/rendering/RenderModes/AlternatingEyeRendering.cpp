#include "AlternatingEyeRendering.h"
#include "../../core/VRCore.h"
#include "../../core/Logger.h"

namespace GTA5VR {

AlternatingEyeMode::AlternatingEyeMode() {
}

AlternatingEyeMode::~AlternatingEyeMode() {
    Shutdown();
}

bool AlternatingEyeMode::Initialize(VRCore* vrCore) {
    if (m_initialized) {
        return true;
    }

    m_vrCore = vrCore;

    if (!m_vrCore || !m_vrCore->GetDevice()) {
        LOG_ERROR("AlternatingEyeMode: Invalid VRCore or device");
        return false;
    }

    if (!CreatePreviousFrameBuffer()) {
        LOG_ERROR("AlternatingEyeMode: Failed to create previous frame buffer");
        return false;
    }

    m_initialized = true;
    LOG_INFO("AlternatingEyeMode initialized");
    return true;
}

void AlternatingEyeMode::Shutdown() {
    if (m_previousEyeTexture) {
        m_previousEyeTexture->Release();
        m_previousEyeTexture = nullptr;
    }

    m_initialized = false;
    LOG_INFO("AlternatingEyeMode shutdown");
}

void AlternatingEyeMode::BeginFrame() {
    if (!m_initialized) {
        return;
    }

    // Determine which eye to render this frame
    m_currentEye = (m_frameCount % 2 == 0) ? 0 : 1;
    m_isRendering = false;
}

void AlternatingEyeMode::EndFrame() {
    if (!m_initialized) {
        return;
    }

    // Copy current eye to previous buffer for next frame's reprojection
    CopyCurrentToPrevious();

    m_previousEyeIndex = m_currentEye;
    m_isRendering = false;
    m_frameCount++;
}

void AlternatingEyeMode::BeginEye(uint32_t eyeIndex) {
    if (!m_initialized) {
        return;
    }

    // In AER mode, we only render one eye per frame
    // Ignore requests for the "wrong" eye
    if (eyeIndex != m_currentEye) {
        LOG_VERBOSE("AlternatingEyeMode: Skipping eye " + std::to_string(eyeIndex) +
                    " (current frame renders eye " + std::to_string(m_currentEye) + ")");
        return;
    }

    m_isRendering = true;

    // Set render target for current eye
    m_vrCore->SetEyeRenderTarget(m_currentEye);

    // Clear render target
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_vrCore->ClearEyeRenderTarget(m_currentEye, clearColor);
}

void AlternatingEyeMode::EndEye(uint32_t eyeIndex) {
    if (!m_initialized || eyeIndex != m_currentEye) {
        return;
    }

    m_isRendering = false;
}

uint32_t AlternatingEyeMode::GetCurrentEye() const {
    return m_currentEye;
}

bool AlternatingEyeMode::IsRenderingEye() const {
    return m_isRendering;
}

uint32_t AlternatingEyeMode::GetPreviousEye() const {
    return m_previousEyeIndex;
}

ID3D11Texture2D* AlternatingEyeMode::GetPreviousEyeTexture() const {
    return m_previousEyeTexture;
}

bool AlternatingEyeMode::CreatePreviousFrameBuffer() {
    uint32_t width, height;
    m_vrCore->GetRecommendedRenderSize(&width, &height);

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_vrCore->GetDevice()->CreateTexture2D(&texDesc, nullptr, &m_previousEyeTexture);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create previous eye texture");
        return false;
    }

    return true;
}

void AlternatingEyeMode::CopyCurrentToPrevious() {
    if (!m_vrCore || !m_previousEyeTexture) {
        return;
    }

    // Get current eye texture from VRCore
    ID3D11RenderTargetView* rtv = m_vrCore->GetEyeRenderTarget(m_currentEye);
    if (!rtv) {
        return;
    }

    // Get the underlying texture from the RTV
    ID3D11Resource* resource = nullptr;
    rtv->GetResource(&resource);
    if (!resource) {
        return;
    }

    ID3D11Texture2D* currentTexture = nullptr;
    resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&currentTexture);
    resource->Release();

    if (currentTexture) {
        m_vrCore->GetContext()->CopyResource(m_previousEyeTexture, currentTexture);
        currentTexture->Release();
    }
}

} // namespace GTA5VR
