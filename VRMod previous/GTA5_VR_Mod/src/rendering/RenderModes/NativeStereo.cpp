#include "NativeStereo.h"
#include "../../core/VRCore.h"
#include "../../core/VRConfig.h"
#include "../../core/Logger.h"

namespace GTA5VR {

NativeStereoMode::NativeStereoMode() {
    m_stereoParams.separation = 0.063f; // Default IPD in meters
    m_stereoParams.convergence = 1.0f;
    m_stereoParams.eyeIndex = -1.0f;
    m_stereoParams.enabled = 1.0f;
}

NativeStereoMode::~NativeStereoMode() {
    Shutdown();
}

bool NativeStereoMode::Initialize(VRCore* vrCore) {
    if (m_initialized) {
        return true;
    }

    m_vrCore = vrCore;

    if (!m_vrCore || !m_vrCore->GetDevice()) {
        LOG_ERROR("NativeStereoMode: Invalid VRCore or device");
        return false;
    }

    if (!SetupStereoConstantBuffer()) {
        LOG_ERROR("NativeStereoMode: Failed to create stereo constant buffer");
        return false;
    }

    m_initialized = true;
    LOG_INFO("NativeStereoMode initialized");
    return true;
}

void NativeStereoMode::Shutdown() {
    if (m_stereoConstantBuffer) {
        m_stereoConstantBuffer->Release();
        m_stereoConstantBuffer = nullptr;
    }

    m_initialized = false;
    LOG_INFO("NativeStereoMode shutdown");
}

void NativeStereoMode::BeginFrame() {
    if (!m_initialized) {
        return;
    }

    m_currentEye = 0;
    m_isRendering = false;

    // Update stereo parameters from config
    auto& config = VRConfig::GetInstance().GetConfig();
    m_stereoParams.separation = config.ipdValue / 1000.0f; // mm to meters
}

void NativeStereoMode::EndFrame() {
    m_isRendering = false;
}

void NativeStereoMode::BeginEye(uint32_t eyeIndex) {
    if (!m_initialized || eyeIndex >= 2) {
        return;
    }

    m_currentEye = eyeIndex;
    m_isRendering = true;

    // Update stereo params for this eye
    UpdateStereoParams(eyeIndex);

    // Set render target
    m_vrCore->SetEyeRenderTarget(eyeIndex);

    // Clear with black
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_vrCore->ClearEyeRenderTarget(eyeIndex, clearColor);
}

void NativeStereoMode::EndEye(uint32_t eyeIndex) {
    if (!m_initialized || eyeIndex != m_currentEye) {
        return;
    }

    m_isRendering = false;
}

uint32_t NativeStereoMode::GetCurrentEye() const {
    return m_currentEye;
}

bool NativeStereoMode::IsRenderingEye() const {
    return m_isRendering;
}

bool NativeStereoMode::SetupStereoConstantBuffer() {
    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = sizeof(StereoParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = &m_stereoParams;

    HRESULT hr = m_vrCore->GetDevice()->CreateBuffer(&bufferDesc, &initData, &m_stereoConstantBuffer);
    if (FAILED(hr)) {
        return false;
    }

    // Bind to slot b13 (commonly used for stereo params)
    m_vrCore->GetContext()->VSSetConstantBuffers(13, 1, &m_stereoConstantBuffer);
    m_vrCore->GetContext()->PSSetConstantBuffers(13, 1, &m_stereoConstantBuffer);

    return true;
}

void NativeStereoMode::UpdateStereoParams(uint32_t eyeIndex) {
    if (!m_stereoConstantBuffer) {
        return;
    }

    // Set eye index (-1 for left, +1 for right)
    m_stereoParams.eyeIndex = (eyeIndex == 0) ? -1.0f : 1.0f;

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_vrCore->GetContext()->Map(m_stereoConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, &m_stereoParams, sizeof(StereoParams));
        m_vrCore->GetContext()->Unmap(m_stereoConstantBuffer, 0);
    }
}

} // namespace GTA5VR
