#pragma once

#include <cstdint>
#include <array>

// Forward declarations for OpenXR types
struct XrPosef;
struct XrVector3f;
struct XrFovf;

namespace GTA5VR {

class IRuntimeInterface;

class VRCamera {
public:
    VRCamera();
    ~VRCamera();

    bool Initialize(IRuntimeInterface* runtime);
    void Shutdown();

    // Update
    void Update(float deltaTime);

    // Tracking
    void ApplyHeadTracking(const XrPosef& headPose);
    void ApplyPositionalTracking(const XrVector3f& position);

    // View matrices (column-major 4x4)
    void GetViewMatrix(uint32_t eyeIndex, float* outMatrix) const;
    void GetProjectionMatrix(uint32_t eyeIndex, float* outMatrix) const;

    // Settings
    void SetIPD(float ipd);
    float GetIPD() const { return m_ipd; }

    void SetWorldScale(float scale);
    float GetWorldScale() const { return m_worldScale; }

    void SetSnapTurning(bool enabled, float degrees);
    bool IsSnapTurningEnabled() const { return m_snapTurnEnabled; }
    float GetSnapTurnDegrees() const { return m_snapTurnDegrees; }

    void SetVignetteOnTurn(bool enabled);

    // Recentering
    void Recenter();
    void RecenterOnHeadShake();

    // Position
    void GetPosition(float* outX, float* outY, float* outZ) const;
    void GetRotation(float* outX, float* outY, float* outZ, float* outW) const;

private:
    void CreateAsymmetricProjection(const XrFovf& fov, float nearPlane, float farPlane, float* outMatrix);
    void UpdateViewMatrices();

    IRuntimeInterface* m_runtime = nullptr;

    // Position and rotation
    float m_position[3] = { 0, 0, 0 };
    float m_rotation[4] = { 0, 0, 0, 1 }; // Quaternion

    // Offset for recentering
    float m_positionOffset[3] = { 0, 0, 0 };
    float m_rotationOffset[4] = { 0, 0, 0, 1 };

    // Eye matrices
    std::array<float, 16> m_viewMatrix[2];
    std::array<float, 16> m_projectionMatrix[2];

    // Settings
    float m_ipd = 0.063f;
    float m_worldScale = 1.0f;
    float m_nearClip = 0.1f;
    float m_farClip = 10000.0f;

    bool m_snapTurnEnabled = true;
    float m_snapTurnDegrees = 45.0f;
    bool m_vignetteOnTurn = true;

    bool m_initialized = false;
};

} // namespace GTA5VR
