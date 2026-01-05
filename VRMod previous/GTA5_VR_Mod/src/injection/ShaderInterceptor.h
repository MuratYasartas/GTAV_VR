#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <string>

namespace GTA5VR {

// Shader types we intercept
enum class ShaderType {
    Vertex,
    Pixel,
    Geometry,
    Hull,
    Domain,
    Compute
};

// Stereo constant buffer data
struct StereoConstants {
    float viewMatrix[16];           // View matrix for current eye
    float projectionMatrix[16];     // Projection matrix for current eye
    float viewProjectionMatrix[16]; // Combined VP matrix
    float eyePosition[4];           // Eye position in world space
    float eyeDirection[4];          // Eye forward direction
    float stereoParams[4];          // x=eye index, y=IPD, z=convergence, w=reserved
};

// Cached shader information
struct ShaderInfo {
    ID3D11DeviceChild* originalShader = nullptr;
    ID3D11DeviceChild* stereoShader = nullptr;
    ID3DBlob* originalBytecode = nullptr;
    ID3DBlob* stereoBytecode = nullptr;
    ShaderType type;
    uint64_t hash = 0;
    bool needsStereoFix = false;
    bool processed = false;
};

// Shader signature for identification
struct ShaderSignature {
    uint64_t bytecodeHash = 0;
    uint32_t inputLayoutHash = 0;
    std::string semanticHints;
};

class ShaderInterceptor {
public:
    ShaderInterceptor();
    ~ShaderInterceptor();

    // Prevent copying
    ShaderInterceptor(const ShaderInterceptor&) = delete;
    ShaderInterceptor& operator=(const ShaderInterceptor&) = delete;

    // Initialization
    bool Initialize(ID3D11Device* device);
    void Shutdown();

    // Shader interception
    ID3D11VertexShader* InterceptVertexShader(ID3D11VertexShader* original,
                                               const void* bytecode, size_t bytecodeLength);
    ID3D11PixelShader* InterceptPixelShader(ID3D11PixelShader* original,
                                             const void* bytecode, size_t bytecodeLength);

    // Stereo constant buffer management
    void UpdateStereoConstants(uint32_t eyeIndex, const float* viewMatrix,
                               const float* projMatrix, const float* eyePos);
    ID3D11Buffer* GetStereoConstantBuffer() const { return m_stereoConstantBuffer; }
    void BindStereoConstantBuffer(ID3D11DeviceContext* context, uint32_t slot);

    // Shader replacement
    ID3D11VertexShader* GetStereoVertexShader(ID3D11VertexShader* original);
    ID3D11PixelShader* GetStereoPixelShader(ID3D11PixelShader* original);

    // Query shader state
    bool IsStereoShader(ID3D11DeviceChild* shader) const;
    bool NeedsStereoFix(ID3D11DeviceChild* shader) const;

    // Shader analysis
    void AnalyzeActiveShaders(ID3D11DeviceContext* context);
    void MarkShaderForStereo(uint64_t hash, bool needsStereo);

    // Statistics
    size_t GetCachedShaderCount() const { return m_shaderCache.size(); }
    size_t GetStereoShaderCount() const;

    // Configuration
    void SetStereoConstantBufferSlot(uint32_t slot) { m_stereoBufferSlot = slot; }
    void SetAutoDetectStereoShaders(bool enable) { m_autoDetect = enable; }

private:
    uint64_t ComputeShaderHash(const void* bytecode, size_t length);
    bool AnalyzeShaderBytecode(const void* bytecode, size_t length, ShaderInfo& info);
    ID3DBlob* CreateStereoVertexShader(const void* bytecode, size_t length);
    ID3DBlob* CreateStereoPixelShader(const void* bytecode, size_t length);
    bool CreateStereoConstantBuffer();

    // Shader analysis helpers
    bool HasWorldViewProjectionMatrix(const void* bytecode, size_t length);
    bool HasPositionOutput(const void* bytecode, size_t length);
    bool IsUIShader(const void* bytecode, size_t length);

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Shader cache (hash -> ShaderInfo)
    std::unordered_map<uint64_t, ShaderInfo> m_shaderCache;
    std::mutex m_cacheMutex;

    // Stereo constant buffer
    ID3D11Buffer* m_stereoConstantBuffer = nullptr;
    StereoConstants m_currentStereoConstants = {};
    uint32_t m_stereoBufferSlot = 13;  // Reserved slot for stereo constants

    // Configuration
    bool m_autoDetect = true;
    bool m_initialized = false;

    // Shader patterns for auto-detection
    std::vector<uint64_t> m_knownStereoShaders;
    std::vector<uint64_t> m_knownUIShaders;
};

} // namespace GTA5VR
