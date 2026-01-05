#include "ShaderInterceptor.h"
#include "../core/Logger.h"
#include <algorithm>

namespace GTA5VR {

// FNV-1a hash constants
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

template<typename T>
void SafeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

ShaderInterceptor::ShaderInterceptor() {
    LOG_DEBUG("ShaderInterceptor", "Constructor called");
}

ShaderInterceptor::~ShaderInterceptor() {
    Shutdown();
}

bool ShaderInterceptor::Initialize(ID3D11Device* device) {
    if (m_initialized) {
        LOG_WARN("ShaderInterceptor", "Already initialized");
        return true;
    }

    if (!device) {
        LOG_ERROR("ShaderInterceptor", "Null device provided");
        return false;
    }

    m_device = device;
    m_device->GetImmediateContext(&m_context);

    LOG_INFO("ShaderInterceptor", "Initializing shader interceptor");

    if (!CreateStereoConstantBuffer()) {
        LOG_ERROR("ShaderInterceptor", "Failed to create stereo constant buffer");
        return false;
    }

    // Initialize known shader lists
    // These would be populated from analysis of GTA V's shaders
    m_knownStereoShaders.clear();
    m_knownUIShaders.clear();

    m_initialized = true;
    LOG_INFO("ShaderInterceptor", "Shader interceptor initialized successfully");
    return true;
}

void ShaderInterceptor::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("ShaderInterceptor", "Shutting down shader interceptor");

    std::lock_guard<std::mutex> lock(m_cacheMutex);

    // Release cached shaders
    for (auto& pair : m_shaderCache) {
        ShaderInfo& info = pair.second;
        // Don't release original shaders - we don't own them
        SafeRelease(info.stereoShader);
        SafeRelease(info.originalBytecode);
        SafeRelease(info.stereoBytecode);
    }
    m_shaderCache.clear();

    SafeRelease(m_stereoConstantBuffer);
    SafeRelease(m_context);
    m_device = nullptr;
    m_initialized = false;
}

bool ShaderInterceptor::CreateStereoConstantBuffer() {
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(StereoConstants);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateBuffer(&bufferDesc, nullptr, &m_stereoConstantBuffer);
    if (FAILED(hr)) {
        LOG_ERROR("ShaderInterceptor", "Failed to create stereo constant buffer: 0x%08X", hr);
        return false;
    }

    LOG_DEBUG("ShaderInterceptor", "Created stereo constant buffer");
    return true;
}

uint64_t ShaderInterceptor::ComputeShaderHash(const void* bytecode, size_t length) {
    // FNV-1a hash
    uint64_t hash = FNV_OFFSET_BASIS;
    const uint8_t* data = static_cast<const uint8_t*>(bytecode);

    for (size_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= FNV_PRIME;
    }

    return hash;
}

ID3D11VertexShader* ShaderInterceptor::InterceptVertexShader(ID3D11VertexShader* original,
                                                              const void* bytecode,
                                                              size_t bytecodeLength) {
    if (!m_initialized || !original || !bytecode || bytecodeLength == 0) {
        return original;
    }

    uint64_t hash = ComputeShaderHash(bytecode, bytecodeLength);

    std::lock_guard<std::mutex> lock(m_cacheMutex);

    // Check cache
    auto it = m_shaderCache.find(hash);
    if (it != m_shaderCache.end()) {
        if (it->second.stereoShader && it->second.needsStereoFix) {
            return static_cast<ID3D11VertexShader*>(it->second.stereoShader);
        }
        return original;
    }

    // Create new cache entry
    ShaderInfo info;
    info.originalShader = original;
    info.type = ShaderType::Vertex;
    info.hash = hash;

    // Analyze shader
    if (!AnalyzeShaderBytecode(bytecode, bytecodeLength, info)) {
        LOG_DEBUG("ShaderInterceptor", "Shader analysis failed for hash 0x%llX", hash);
    }

    // Store original bytecode
    D3DCreateBlob(bytecodeLength, &info.originalBytecode);
    if (info.originalBytecode) {
        memcpy(info.originalBytecode->GetBufferPointer(), bytecode, bytecodeLength);
    }

    // Check if this shader needs stereo modification
    if (m_autoDetect) {
        info.needsStereoFix = HasWorldViewProjectionMatrix(bytecode, bytecodeLength) &&
                              HasPositionOutput(bytecode, bytecodeLength) &&
                              !IsUIShader(bytecode, bytecodeLength);
    }

    // Create stereo version if needed
    if (info.needsStereoFix) {
        ID3DBlob* stereoBytecode = CreateStereoVertexShader(bytecode, bytecodeLength);
        if (stereoBytecode) {
            HRESULT hr = m_device->CreateVertexShader(
                stereoBytecode->GetBufferPointer(),
                stereoBytecode->GetBufferSize(),
                nullptr,
                reinterpret_cast<ID3D11VertexShader**>(&info.stereoShader));

            if (SUCCEEDED(hr)) {
                info.stereoBytecode = stereoBytecode;
                LOG_DEBUG("ShaderInterceptor", "Created stereo VS for hash 0x%llX", hash);
            } else {
                SafeRelease(stereoBytecode);
                LOG_WARN("ShaderInterceptor", "Failed to create stereo VS: 0x%08X", hr);
            }
        }
    }

    info.processed = true;
    m_shaderCache[hash] = info;

    if (info.stereoShader && info.needsStereoFix) {
        return static_cast<ID3D11VertexShader*>(info.stereoShader);
    }
    return original;
}

ID3D11PixelShader* ShaderInterceptor::InterceptPixelShader(ID3D11PixelShader* original,
                                                            const void* bytecode,
                                                            size_t bytecodeLength) {
    if (!m_initialized || !original || !bytecode || bytecodeLength == 0) {
        return original;
    }

    uint64_t hash = ComputeShaderHash(bytecode, bytecodeLength);

    std::lock_guard<std::mutex> lock(m_cacheMutex);

    // Check cache
    auto it = m_shaderCache.find(hash);
    if (it != m_shaderCache.end()) {
        // Pixel shaders typically don't need stereo modification
        return original;
    }

    // Create new cache entry for tracking
    ShaderInfo info;
    info.originalShader = original;
    info.type = ShaderType::Pixel;
    info.hash = hash;
    info.needsStereoFix = false;
    info.processed = true;

    m_shaderCache[hash] = info;
    return original;
}

void ShaderInterceptor::UpdateStereoConstants(uint32_t eyeIndex, const float* viewMatrix,
                                               const float* projMatrix, const float* eyePos) {
    if (!m_stereoConstantBuffer || !m_context) {
        return;
    }

    // Update stereo constants
    if (viewMatrix) {
        memcpy(m_currentStereoConstants.viewMatrix, viewMatrix, 16 * sizeof(float));
    }
    if (projMatrix) {
        memcpy(m_currentStereoConstants.projectionMatrix, projMatrix, 16 * sizeof(float));
    }
    if (eyePos) {
        memcpy(m_currentStereoConstants.eyePosition, eyePos, 4 * sizeof(float));
    }

    m_currentStereoConstants.stereoParams[0] = static_cast<float>(eyeIndex);

    // Compute view-projection matrix
    // Simple matrix multiplication (view * projection)
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += m_currentStereoConstants.viewMatrix[i * 4 + k] *
                       m_currentStereoConstants.projectionMatrix[k * 4 + j];
            }
            m_currentStereoConstants.viewProjectionMatrix[i * 4 + j] = sum;
        }
    }

    // Map and update buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(m_stereoConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, &m_currentStereoConstants, sizeof(StereoConstants));
        m_context->Unmap(m_stereoConstantBuffer, 0);
    }
}

void ShaderInterceptor::BindStereoConstantBuffer(ID3D11DeviceContext* context, uint32_t slot) {
    if (!context || !m_stereoConstantBuffer) {
        return;
    }

    context->VSSetConstantBuffers(slot, 1, &m_stereoConstantBuffer);
    context->PSSetConstantBuffers(slot, 1, &m_stereoConstantBuffer);
}

ID3D11VertexShader* ShaderInterceptor::GetStereoVertexShader(ID3D11VertexShader* original) {
    if (!original) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_cacheMutex);

    for (auto& pair : m_shaderCache) {
        if (pair.second.originalShader == original &&
            pair.second.type == ShaderType::Vertex &&
            pair.second.stereoShader) {
            return static_cast<ID3D11VertexShader*>(pair.second.stereoShader);
        }
    }

    return original;
}

ID3D11PixelShader* ShaderInterceptor::GetStereoPixelShader(ID3D11PixelShader* original) {
    // Most pixel shaders don't need stereo modification
    return original;
}

bool ShaderInterceptor::IsStereoShader(ID3D11DeviceChild* shader) const {
    if (!shader) {
        return false;
    }

    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_cacheMutex));

    for (const auto& pair : m_shaderCache) {
        if (pair.second.stereoShader == shader) {
            return true;
        }
    }

    return false;
}

bool ShaderInterceptor::NeedsStereoFix(ID3D11DeviceChild* shader) const {
    if (!shader) {
        return false;
    }

    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_cacheMutex));

    for (const auto& pair : m_shaderCache) {
        if (pair.second.originalShader == shader) {
            return pair.second.needsStereoFix;
        }
    }

    return false;
}

void ShaderInterceptor::AnalyzeActiveShaders(ID3D11DeviceContext* context) {
    if (!context) {
        return;
    }

    // Get currently bound shaders for analysis
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11GeometryShader* gs = nullptr;

    context->VSGetShader(&vs, nullptr, nullptr);
    context->PSGetShader(&ps, nullptr, nullptr);
    context->GSGetShader(&gs, nullptr, nullptr);

    // Log for debugging
    if (vs) {
        LOG_DEBUG("ShaderInterceptor", "Active VS: %p, is stereo: %s",
                  vs, IsStereoShader(vs) ? "yes" : "no");
        vs->Release();
    }
    if (ps) {
        ps->Release();
    }
    if (gs) {
        gs->Release();
    }
}

void ShaderInterceptor::MarkShaderForStereo(uint64_t hash, bool needsStereo) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    auto it = m_shaderCache.find(hash);
    if (it != m_shaderCache.end()) {
        it->second.needsStereoFix = needsStereo;
        LOG_INFO("ShaderInterceptor", "Marked shader 0x%llX for stereo: %s",
                 hash, needsStereo ? "yes" : "no");
    }
}

size_t ShaderInterceptor::GetStereoShaderCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_cacheMutex));

    size_t count = 0;
    for (const auto& pair : m_shaderCache) {
        if (pair.second.stereoShader != nullptr) {
            count++;
        }
    }
    return count;
}

bool ShaderInterceptor::AnalyzeShaderBytecode(const void* bytecode, size_t length,
                                               ShaderInfo& info) {
    // Basic bytecode validation
    if (length < 20) {
        return false;
    }

    const uint32_t* header = static_cast<const uint32_t*>(bytecode);

    // Check DXBC magic number
    if (header[0] != 0x43425844) {  // "DXBC"
        LOG_DEBUG("ShaderInterceptor", "Invalid DXBC magic number");
        return false;
    }

    // Parse DXBC chunks for analysis
    // This is a simplified analysis - full analysis would parse RDEF, ISGN, OSGN chunks

    return true;
}

bool ShaderInterceptor::HasWorldViewProjectionMatrix(const void* bytecode, size_t length) {
    // Heuristic: Look for constant buffer usage patterns typical of WVP matrices
    // In a full implementation, this would analyze the shader's constant buffer definitions

    // For now, assume all vertex shaders with sufficient complexity need stereo
    return length > 500;  // Arbitrary threshold
}

bool ShaderInterceptor::HasPositionOutput(const void* bytecode, size_t length) {
    // Check if shader outputs SV_Position
    // Simplified: most vertex shaders do this
    return true;
}

bool ShaderInterceptor::IsUIShader(const void* bytecode, size_t length) {
    // Heuristic: UI shaders are typically simpler
    // In practice, we'd check for specific patterns or known hashes
    return length < 300;
}

ID3DBlob* ShaderInterceptor::CreateStereoVertexShader(const void* bytecode, size_t length) {
    // In a full implementation, this would:
    // 1. Disassemble the original shader
    // 2. Inject stereo projection matrix usage
    // 3. Recompile the modified shader

    // For now, return nullptr - the runtime stereo rendering handles this differently
    // by using multiple draw calls with different view matrices

    LOG_DEBUG("ShaderInterceptor", "Stereo shader injection not fully implemented");
    return nullptr;
}

ID3DBlob* ShaderInterceptor::CreateStereoPixelShader(const void* bytecode, size_t length) {
    // Pixel shaders typically don't need modification for stereo
    return nullptr;
}

} // namespace GTA5VR
