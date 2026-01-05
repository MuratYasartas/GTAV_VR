#include "HUDProjection.h"
#include "../core/Logger.h"
#include <cmath>
#include <algorithm>

namespace GTA5VR {

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;

template<typename T>
void SafeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

HUDProjection::HUDProjection() {
    LOG_DEBUG("HUDProjection", "Constructor called");

    // Initialize HUD elements
    m_elements = {
        {HUDElementType::Minimap, 0.02f, 0.72f, 0.2f, 0.26f, 1.0f, true, true},
        {HUDElementType::Health, 0.02f, 0.98f, 0.15f, 0.02f, 1.0f, true, true},
        {HUDElementType::Armor, 0.02f, 0.96f, 0.15f, 0.02f, 1.0f, true, true},
        {HUDElementType::Weapon, 0.85f, 0.90f, 0.13f, 0.08f, 1.0f, true, false},
        {HUDElementType::Ammo, 0.85f, 0.96f, 0.13f, 0.03f, 1.0f, true, false},
        {HUDElementType::WantedLevel, 0.85f, 0.05f, 0.13f, 0.03f, 1.0f, true, true},
        {HUDElementType::Money, 0.85f, 0.02f, 0.13f, 0.03f, 1.0f, true, false},
        {HUDElementType::Subtitles, 0.1f, 0.85f, 0.8f, 0.1f, 1.0f, true, true},
    };
}

HUDProjection::~HUDProjection() {
    Shutdown();
}

bool HUDProjection::Initialize(ID3D11Device* device) {
    if (m_initialized) {
        return true;
    }

    if (!device) {
        LOG_ERROR("HUDProjection", "Null device provided");
        return false;
    }

    m_device = device;
    m_device->GetImmediateContext(&m_context);

    LOG_INFO("HUDProjection", "Initializing HUD projection");

    // Generate initial curved mesh
    GenerateCurvedMesh();

    m_initialized = true;
    return true;
}

void HUDProjection::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("HUDProjection", "Shutting down HUD projection");

    SafeRelease(m_capture.srv);
    SafeRelease(m_capture.texture);
    SafeRelease(m_stagingTexture);
    SafeRelease(m_context);

    m_device = nullptr;
    m_initialized = false;
}

void HUDProjection::Update(float deltaTime, const HeadPose& headPose) {
    if (!m_initialized) {
        return;
    }

    m_lastHeadPose = headPose;

    switch (m_settings.mode) {
        case HUDMode::Floating:
            UpdateFloatingMode(deltaTime, headPose);
            break;
        case HUDMode::Helmet:
            UpdateHelmetMode(deltaTime, headPose);
            break;
        case HUDMode::WorldLocked:
            UpdateWorldLockedMode(deltaTime);
            break;
        case HUDMode::Disabled:
        default:
            break;
    }

    // Update subtitle timer
    if (m_subtitleTimer > 0.0f) {
        m_subtitleTimer -= deltaTime;
        if (m_subtitleTimer <= 0.0f) {
            m_subtitleText.clear();
        }
    }
}

void HUDProjection::SetMode(HUDMode mode) {
    if (m_settings.mode == mode) {
        return;
    }

    m_settings.mode = mode;
    LOG_INFO("HUDProjection", "HUD mode changed to %d", static_cast<int>(mode));

    // Reset position when changing modes
    if (mode == HUDMode::Floating || mode == HUDMode::Helmet) {
        m_hudPosition = Vector3(0.0f, 0.0f, -m_settings.distance);
        m_hudOrientation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
    }
}

void HUDProjection::SetSettings(const HUDSettings& settings) {
    m_settings = settings;

    // Regenerate mesh if curvature changed
    m_meshDirty = true;
}

void HUDProjection::SetDistance(float distance) {
    m_settings.distance = std::clamp(distance, 0.5f, 10.0f);
}

void HUDProjection::SetScale(float scale) {
    m_settings.scale = std::clamp(scale, 0.1f, 3.0f);
}

void HUDProjection::SetOpacity(float opacity) {
    m_settings.opacity = std::clamp(opacity, 0.0f, 1.0f);
}

void HUDProjection::SetOffset(float x, float y) {
    m_settings.offsetX = std::clamp(x, -1.0f, 1.0f);
    m_settings.offsetY = std::clamp(y, -1.0f, 1.0f);
}

void HUDProjection::SetCurvature(float curvature) {
    if (m_settings.curvature != curvature) {
        m_settings.curvature = std::clamp(curvature, 0.0f, 1.0f);
        m_meshDirty = true;
    }
}

void HUDProjection::SetElementVisible(HUDElementType type, bool visible) {
    for (auto& element : m_elements) {
        if (element.type == type) {
            element.visible = visible;
            break;
        }
    }
}

bool HUDProjection::IsElementVisible(HUDElementType type) const {
    for (const auto& element : m_elements) {
        if (element.type == type) {
            return element.visible && (!m_settings.minimalMode || element.important);
        }
    }
    return false;
}

void HUDProjection::SetMinimalMode(bool minimal) {
    m_settings.minimalMode = minimal;
}

void HUDProjection::CaptureHUD(ID3D11DeviceContext* context, ID3D11Texture2D* gameHUD) {
    if (!m_initialized || !context || !gameHUD) {
        return;
    }

    // Get source texture description
    D3D11_TEXTURE2D_DESC srcDesc;
    gameHUD->GetDesc(&srcDesc);

    // Create capture texture if needed
    if (!m_capture.texture ||
        m_capture.width != srcDesc.Width ||
        m_capture.height != srcDesc.Height) {
        CreateCaptureTexture(srcDesc.Width, srcDesc.Height);
    }

    if (!m_capture.texture) {
        return;
    }

    // Copy HUD texture
    context->CopyResource(m_capture.texture, gameHUD);
    m_capture.valid = true;
}

void HUDProjection::GetHUDTransform(Vector3& position, Quaternion& orientation,
                                     float& width, float& height) const {
    position = m_hudPosition;
    orientation = m_hudOrientation;
    width = m_settings.width * m_settings.scale;
    height = m_settings.height * m_settings.scale;
}

void HUDProjection::GetHUDMeshVertices(std::vector<float>& vertices,
                                        std::vector<uint32_t>& indices) const {
    if (m_meshDirty) {
        const_cast<HUDProjection*>(this)->GenerateCurvedMesh();
    }

    vertices = m_meshVertices;
    indices = m_meshIndices;
}

void HUDProjection::SetWorldPosition(const Vector3& position, const Quaternion& orientation) {
    m_worldPosition = position;
    m_worldOrientation = orientation;
}

void HUDProjection::AttachToHead(const HeadPose& pose) {
    // Position HUD in front of head
    float distance = m_settings.distance;

    // Forward direction from head orientation
    float xx = pose.orientation.x * pose.orientation.x;
    float yy = pose.orientation.y * pose.orientation.y;
    float xz = pose.orientation.x * pose.orientation.z;
    float wy = pose.orientation.w * pose.orientation.y;

    Vector3 forward;
    forward.x = 2.0f * (xz + wy);
    forward.y = 2.0f * (pose.orientation.y * pose.orientation.z - pose.orientation.w * pose.orientation.x);
    forward.z = 1.0f - 2.0f * (xx + yy);

    // Normalize
    float len = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (len > 0.001f) {
        forward.x /= len;
        forward.y /= len;
        forward.z /= len;
    }

    m_hudPosition.x = pose.position.x + forward.x * distance + m_settings.offsetX;
    m_hudPosition.y = pose.position.y + forward.y * distance + m_settings.offsetY;
    m_hudPosition.z = pose.position.z + forward.z * distance;

    m_hudOrientation = pose.orientation;
}

void HUDProjection::SetSubtitleText(const std::string& text) {
    m_subtitleText = text;
    m_subtitleTimer = 5.0f;  // Default 5 second display
}

void HUDProjection::UpdateFloatingMode(float deltaTime, const HeadPose& headPose) {
    // In floating mode, HUD stays at fixed position relative to initial view
    // but can be recentered

    // Calculate target position based on head forward direction
    float yaw = 0.0f;

    // Extract yaw from head orientation
    float siny = 2.0f * (headPose.orientation.w * headPose.orientation.y +
                         headPose.orientation.z * headPose.orientation.x);
    float cosy = 1.0f - 2.0f * (headPose.orientation.x * headPose.orientation.x +
                                 headPose.orientation.y * headPose.orientation.y);
    yaw = std::atan2(siny, cosy);

    // Position HUD at distance, looking at head
    float distance = m_settings.distance;
    m_hudPosition.x = headPose.position.x + std::sin(yaw) * distance + m_settings.offsetX;
    m_hudPosition.y = headPose.position.y + m_settings.offsetY;
    m_hudPosition.z = headPose.position.z - std::cos(yaw) * distance;

    // Orient HUD to face the player
    float halfYaw = (yaw + PI) * 0.5f;
    m_hudOrientation.w = std::cos(halfYaw);
    m_hudOrientation.x = 0.0f;
    m_hudOrientation.y = std::sin(halfYaw);
    m_hudOrientation.z = 0.0f;
}

void HUDProjection::UpdateHelmetMode(float deltaTime, const HeadPose& headPose) {
    // In helmet mode, HUD follows head with some lag

    // Calculate target position attached to head
    Vector3 targetPos;
    float distance = m_settings.distance;

    // Forward direction
    float xx = headPose.orientation.x * headPose.orientation.x;
    float yy = headPose.orientation.y * headPose.orientation.y;
    float xz = headPose.orientation.x * headPose.orientation.z;
    float wy = headPose.orientation.w * headPose.orientation.y;
    float yz = headPose.orientation.y * headPose.orientation.z;
    float wx = headPose.orientation.w * headPose.orientation.x;

    Vector3 forward;
    forward.x = 2.0f * (xz + wy);
    forward.y = 2.0f * (yz - wx);
    forward.z = 1.0f - 2.0f * (xx + yy);

    targetPos.x = headPose.position.x + forward.x * distance + m_settings.offsetX;
    targetPos.y = headPose.position.y + forward.y * distance + m_settings.offsetY;
    targetPos.z = headPose.position.z + forward.z * distance;

    // Smooth follow
    float followSpeed = m_settings.helmetFollowSpeed * deltaTime;
    followSpeed = std::min(followSpeed, 1.0f);

    m_hudPosition.x += (targetPos.x - m_hudPosition.x) * followSpeed;
    m_hudPosition.y += (targetPos.y - m_hudPosition.y) * followSpeed;
    m_hudPosition.z += (targetPos.z - m_hudPosition.z) * followSpeed;

    // Orientation follows head
    m_hudOrientation = headPose.orientation;
}

void HUDProjection::UpdateWorldLockedMode(float deltaTime) {
    // In world-locked mode, HUD stays at fixed world position
    m_hudPosition = m_worldPosition;
    m_hudOrientation = m_worldOrientation;
}

void HUDProjection::CreateCaptureTexture(uint32_t width, uint32_t height) {
    // Release old resources
    SafeRelease(m_capture.srv);
    SafeRelease(m_capture.texture);

    // Create texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_capture.texture);
    if (FAILED(hr)) {
        LOG_ERROR("HUDProjection", "Failed to create capture texture: 0x%08X", hr);
        return;
    }

    // Create SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = m_device->CreateShaderResourceView(m_capture.texture, &srvDesc, &m_capture.srv);
    if (FAILED(hr)) {
        LOG_ERROR("HUDProjection", "Failed to create capture SRV: 0x%08X", hr);
        SafeRelease(m_capture.texture);
        return;
    }

    m_capture.width = width;
    m_capture.height = height;

    LOG_DEBUG("HUDProjection", "Created capture texture %ux%u", width, height);
}

void HUDProjection::GenerateCurvedMesh() {
    m_meshVertices.clear();
    m_meshIndices.clear();

    // Generate a curved quad for the HUD
    const int segmentsX = 16;
    const int segmentsY = 9;

    float width = m_settings.width * m_settings.scale;
    float height = m_settings.height * m_settings.scale;
    float curvature = m_settings.curvature;

    // Generate vertices
    // Format: x, y, z, u, v (5 floats per vertex)
    for (int y = 0; y <= segmentsY; y++) {
        float v = static_cast<float>(y) / segmentsY;
        float yPos = (v - 0.5f) * height;

        for (int x = 0; x <= segmentsX; x++) {
            float u = static_cast<float>(x) / segmentsX;
            float xPos = (u - 0.5f) * width;

            // Apply curvature (bend around Y axis)
            float angle = (u - 0.5f) * curvature * PI;
            float radius = m_settings.distance;

            float zOffset = 0.0f;
            if (curvature > 0.001f) {
                // Curved position
                zOffset = radius * (1.0f - std::cos(angle));
                xPos = std::sin(angle) * radius;
            }

            // Position
            m_meshVertices.push_back(xPos);
            m_meshVertices.push_back(yPos);
            m_meshVertices.push_back(zOffset);

            // UV
            m_meshVertices.push_back(u);
            m_meshVertices.push_back(1.0f - v);  // Flip V
        }
    }

    // Generate indices
    for (int y = 0; y < segmentsY; y++) {
        for (int x = 0; x < segmentsX; x++) {
            uint32_t topLeft = y * (segmentsX + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (y + 1) * (segmentsX + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;

            // First triangle
            m_meshIndices.push_back(topLeft);
            m_meshIndices.push_back(bottomLeft);
            m_meshIndices.push_back(topRight);

            // Second triangle
            m_meshIndices.push_back(topRight);
            m_meshIndices.push_back(bottomLeft);
            m_meshIndices.push_back(bottomRight);
        }
    }

    m_meshDirty = false;
    LOG_DEBUG("HUDProjection", "Generated curved mesh: %zu vertices, %zu indices",
              m_meshVertices.size() / 5, m_meshIndices.size());
}

} // namespace GTA5VR
