#include "SynchronizedSequential.h"
#include "../../core/VRCore.h"
#include "../../core/Logger.h"
#include <chrono>

namespace GTA5VR {

SynchronizedSequentialMode::SynchronizedSequentialMode() {
    memset(&m_savedState, 0, sizeof(m_savedState));
}

SynchronizedSequentialMode::~SynchronizedSequentialMode() {
    Shutdown();
}

bool SynchronizedSequentialMode::Initialize(VRCore* vrCore) {
    if (m_initialized) {
        return true;
    }

    m_vrCore = vrCore;

    if (!m_vrCore || !m_vrCore->GetDevice()) {
        LOG_ERROR("SynchronizedSequentialMode: Invalid VRCore or device");
        return false;
    }

    m_initialized = true;
    LOG_INFO("SynchronizedSequentialMode initialized");
    return true;
}

void SynchronizedSequentialMode::Shutdown() {
    m_initialized = false;
    LOG_INFO("SynchronizedSequentialMode shutdown");
}

void SynchronizedSequentialMode::BeginFrame() {
    if (!m_initialized) {
        return;
    }

    m_currentEye = 0;
    m_isRendering = false;
    m_firstEyeComplete = false;
    m_bothEyesComplete = false;

    // Record frame start time for timing
    auto now = std::chrono::high_resolution_clock::now();
    m_frameStartTime = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

void SynchronizedSequentialMode::EndFrame() {
    m_isRendering = false;
    m_bothEyesComplete = true;
}

void SynchronizedSequentialMode::BeginEye(uint32_t eyeIndex) {
    if (!m_initialized || eyeIndex >= 2) {
        return;
    }

    // For sequential mode, we must complete left eye before right
    if (eyeIndex == 1 && !m_firstEyeComplete) {
        LOG_WARNING("SynchronizedSequential: Attempting to render right eye before left is complete");
    }

    m_currentEye = eyeIndex;
    m_isRendering = true;

    // Save current render state if starting first eye
    if (eyeIndex == 0) {
        SaveRenderState();
    }

    // Set render target for this eye
    m_vrCore->SetEyeRenderTarget(eyeIndex);

    // Clear the render target
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_vrCore->ClearEyeRenderTarget(eyeIndex, clearColor);
}

void SynchronizedSequentialMode::EndEye(uint32_t eyeIndex) {
    if (!m_initialized || eyeIndex != m_currentEye) {
        return;
    }

    m_isRendering = false;

    if (eyeIndex == 0) {
        m_firstEyeComplete = true;
    } else if (eyeIndex == 1) {
        m_bothEyesComplete = true;
        // Restore original render state after both eyes complete
        RestoreRenderState();
    }
}

uint32_t SynchronizedSequentialMode::GetCurrentEye() const {
    return m_currentEye;
}

bool SynchronizedSequentialMode::IsRenderingEye() const {
    return m_isRendering;
}

void SynchronizedSequentialMode::SaveRenderState() {
    if (!m_vrCore || !m_vrCore->GetContext()) {
        return;
    }

    auto* context = m_vrCore->GetContext();

    // Save render targets
    context->OMGetRenderTargets(8, m_savedState.rtv, &m_savedState.dsv);

    // Save viewports
    m_savedState.numViewports = 8;
    context->RSGetViewports(&m_savedState.numViewports, m_savedState.viewports);
}

void SynchronizedSequentialMode::RestoreRenderState() {
    if (!m_vrCore || !m_vrCore->GetContext()) {
        return;
    }

    auto* context = m_vrCore->GetContext();

    // Restore render targets
    context->OMSetRenderTargets(8, m_savedState.rtv, m_savedState.dsv);

    // Release references we held
    for (int i = 0; i < 8; ++i) {
        if (m_savedState.rtv[i]) {
            m_savedState.rtv[i]->Release();
            m_savedState.rtv[i] = nullptr;
        }
    }
    if (m_savedState.dsv) {
        m_savedState.dsv->Release();
        m_savedState.dsv = nullptr;
    }

    // Restore viewports
    context->RSSetViewports(m_savedState.numViewports, m_savedState.viewports);
}

} // namespace GTA5VR
