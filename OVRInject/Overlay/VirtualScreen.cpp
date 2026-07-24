#include "VirtualScreen.hpp"
#include "../Log.hpp"
#include <d3dcompiler.h>

using namespace DirectX;

namespace OVRInject {

namespace {

// Simple vertex shader for screen rendering
const char* kScreenVertexShader = R"(
cbuffer ScreenConstants : register(b0)
{
    matrix worldViewProj;
    float opacity;
    float3 padding;
};

struct VS_INPUT
{
    float3 pos : POSITION;
    float2 tex : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(float4(input.pos, 1.0f), worldViewProj);
    output.tex = input.tex;
    return output;
}
)";

// Simple pixel shader for screen rendering
const char* kScreenPixelShader = R"(
cbuffer ScreenConstants : register(b0)
{
    matrix worldViewProj;
    float opacity;
    float3 padding;
};

Texture2D screenTexture : register(t0);
SamplerState screenSampler : register(s0);

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 color = screenTexture.Sample(screenSampler, input.tex);
    color.a *= opacity;
    return color;
}
)";

DXGI_FORMAT ResolveCopyFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    default:
        return format;
    }
}

struct ScreenVertex {
    float pos[3];
    float tex[2];
};

} // namespace

VirtualScreen::VirtualScreen() {
    LOGSTR("VirtualScreen: Created\n");
}

VirtualScreen::~VirtualScreen() {
    Shutdown();
    LOGSTR("VirtualScreen: Destroyed\n");
}

bool VirtualScreen::Initialize(ID3D11Device* device, VR::IVRBackend* backend) {
    if (initialized_) {
        return true;
    }

    if (!device || !backend) {
        LOGSTR("VirtualScreen: Invalid device or backend\n");
        return false;
    }

    device_ = device;
    device_->GetImmediateContext(&context_);
    backend_ = backend;

    // Load settings
    auto& settings = VR::GetCutsceneSettings();
    distance_ = settings.screenDistance.load();
    scale_ = settings.screenScale.load();
    curvature_ = settings.screenCurve.load();

    if (!CreateResources()) {
        Shutdown();
        return false;
    }

    initialized_ = true;
    LOGSTR("VirtualScreen: Initialized\n");
    return true;
}

void VirtualScreen::Shutdown() {
    ReleaseResources();

    if (context_) {
        context_->Release();
        context_ = nullptr;
    }

    device_ = nullptr;
    backend_ = nullptr;
    initialized_ = false;
}

bool VirtualScreen::CreateResources() {
    if (!CreateShaders()) {
        LOGSTR("VirtualScreen: Failed to create shaders\n");
        return false;
    }

    if (!CreateGeometry()) {
        LOGSTR("VirtualScreen: Failed to create geometry\n");
        return false;
    }

    if (!CreateRenderTargets()) {
        LOGSTR("VirtualScreen: Failed to create render targets\n");
        return false;
    }

    // Create sampler state
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    if (FAILED(device_->CreateSamplerState(&samplerDesc, &sampler_))) {
        LOGSTR("VirtualScreen: Failed to create sampler\n");
        return false;
    }

    return true;
}

void VirtualScreen::ReleaseResources() {
    if (vertex_shader_) { vertex_shader_->Release(); vertex_shader_ = nullptr; }
    if (pixel_shader_) { pixel_shader_->Release(); pixel_shader_ = nullptr; }
    if (input_layout_) { input_layout_->Release(); input_layout_ = nullptr; }
    if (vertex_buffer_) { vertex_buffer_->Release(); vertex_buffer_ = nullptr; }
    if (index_buffer_) { index_buffer_->Release(); index_buffer_ = nullptr; }
    if (constant_buffer_) { constant_buffer_->Release(); constant_buffer_ = nullptr; }
    if (source_srv_) { source_srv_->Release(); source_srv_ = nullptr; }
    if (source_copy_srv_) { source_copy_srv_->Release(); source_copy_srv_ = nullptr; }
    if (source_copy_) { source_copy_->Release(); source_copy_ = nullptr; }
    source_copy_format_ = DXGI_FORMAT_UNKNOWN;
    source_copy_width_ = 0;
    source_copy_height_ = 0;
    if (sampler_) { sampler_->Release(); sampler_ = nullptr; }

    for (int i = 0; i < 2; i++) {
        if (eye_rtvs_[i]) { eye_rtvs_[i]->Release(); eye_rtvs_[i] = nullptr; }
        if (eye_textures_[i]) { eye_textures_[i]->Release(); eye_textures_[i] = nullptr; }
    }
}

bool VirtualScreen::CreateShaders() {
    // Compile vertex shader
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(kScreenVertexShader, strlen(kScreenVertexShader),
                            nullptr, nullptr, nullptr, "main", "vs_5_0",
                            D3DCOMPILE_ENABLE_STRICTNESS, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            LOGSTRF("VirtualScreen: VS compile error: %s\n",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            errorBlob->Release();
        }
        return false;
    }

    hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                     nullptr, &vertex_shader_);
    if (FAILED(hr)) {
        vsBlob->Release();
        return false;
    }

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = device_->CreateInputLayout(layoutDesc, 2, vsBlob->GetBufferPointer(),
                                    vsBlob->GetBufferSize(), &input_layout_);
    vsBlob->Release();
    if (FAILED(hr)) {
        return false;
    }

    // Compile pixel shader
    ID3DBlob* psBlob = nullptr;
    hr = D3DCompile(kScreenPixelShader, strlen(kScreenPixelShader),
                    nullptr, nullptr, nullptr, "main", "ps_5_0",
                    D3DCOMPILE_ENABLE_STRICTNESS, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            LOGSTRF("VirtualScreen: PS compile error: %s\n",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            errorBlob->Release();
        }
        return false;
    }

    hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                    nullptr, &pixel_shader_);
    psBlob->Release();
    if (FAILED(hr)) {
        return false;
    }

    return true;
}

bool VirtualScreen::CreateGeometry() {
    // Create a simple quad for the screen
    // Aspect ratio 16:9
    float aspectRatio = 16.0f / 9.0f;
    float halfWidth = scale_ * aspectRatio * 0.5f;
    float halfHeight = scale_ * 0.5f;

    ScreenVertex vertices[] = {
        { { -halfWidth, -halfHeight, 0.0f }, { 0.0f, 1.0f } },  // Bottom-left
        { { -halfWidth,  halfHeight, 0.0f }, { 0.0f, 0.0f } },  // Top-left
        { {  halfWidth,  halfHeight, 0.0f }, { 1.0f, 0.0f } },  // Top-right
        { {  halfWidth, -halfHeight, 0.0f }, { 1.0f, 1.0f } }   // Bottom-right
    };

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbData = { vertices, 0, 0 };
    if (FAILED(device_->CreateBuffer(&vbDesc, &vbData, &vertex_buffer_))) {
        return false;
    }

    // Create index buffer
    uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
    index_count_ = 6;

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.Usage = D3D11_USAGE_DEFAULT;
    ibDesc.ByteWidth = sizeof(indices);
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibData = { indices, 0, 0 };
    if (FAILED(device_->CreateBuffer(&ibDesc, &ibData, &index_buffer_))) {
        return false;
    }

    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.ByteWidth = sizeof(ScreenConstants);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    if (FAILED(device_->CreateBuffer(&cbDesc, nullptr, &constant_buffer_))) {
        return false;
    }

    return true;
}

bool VirtualScreen::CreateRenderTargets() {
    uint32_t width = backend_->GetRecommendedWidth();
    uint32_t height = backend_->GetRecommendedHeight();

    for (int i = 0; i < 2; i++) {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device_->CreateTexture2D(&texDesc, nullptr, &eye_textures_[i]))) {
            return false;
        }

        if (FAILED(device_->CreateRenderTargetView(eye_textures_[i], nullptr, &eye_rtvs_[i]))) {
            return false;
        }
    }

    return true;
}

void VirtualScreen::Update() {
    if (!initialized_ || !enabled_) {
        return;
    }

    // Update settings and check for changes
    auto& settings = VR::GetCutsceneSettings();
    float newDistance = settings.screenDistance.load();
    float newScale = settings.screenScale.load();
    float newCurvature = settings.screenCurve.load();

    // Recreate geometry if scale changed (affects vertex positions)
    bool scaleChanged = fabsf(newScale - scale_) > 0.01f;
    distance_ = newDistance;
    scale_ = newScale;
    curvature_ = newCurvature;

    if (scaleChanged) {
        // Recreate vertex buffer with new scale
        if (vertex_buffer_) {
            vertex_buffer_->Release();
            vertex_buffer_ = nullptr;
        }
        if (index_buffer_) {
            index_buffer_->Release();
            index_buffer_ = nullptr;
        }
        CreateGeometry();
        LOGSTRF("VirtualScreen: Recreated geometry with scale %.2f\n", scale_);
    }

    // Update screen position based on head pose
    if (backend_) {
        XMMATRIX headPose = backend_->GetHeadPoseMatrix();
        XMVECTOR headPos = headPose.r[3];

        // Get head's forward direction (negative Z in view space = forward in world)
        // Note: In VR, the forward direction is typically -Z
        XMVECTOR headForward = XMVectorNegate(headPose.r[2]);
        headForward = XMVector3Normalize(headForward);

        if (locked_to_head_) {
            // Screen follows head - always in front
            XMVECTOR screenPos = XMVectorAdd(headPos, XMVectorScale(headForward, distance_));
            XMStoreFloat3(&screen_position_, screenPos);
        } else {
            // Screen stays in world position but we ensure it was placed correctly
            // On first update or after recenter, place in front of head
            if (needs_initial_position_) {
                XMVECTOR screenPos = XMVectorAdd(headPos, XMVectorScale(headForward, distance_));
                // Slightly below eye level for comfortable viewing
                screenPos = XMVectorAdd(screenPos, XMVectorSet(0, -0.3f, 0, 0));
                XMStoreFloat3(&screen_position_, screenPos);
                needs_initial_position_ = false;
            }
        }
    }

    // Calculate screen world matrix (facing the viewer)
    screen_world_matrix_ = CalculateScreenTransform();
}

void VirtualScreen::Render(ID3D11Texture2D* sourceTexture) {
    if (!initialized_ || !enabled_ || !sourceTexture) {
        return;
    }

    // Create SRV for source texture if needed
    if (source_srv_) {
        if (source_srv_ != source_copy_srv_) {
            source_srv_->Release();
        }
        source_srv_ = nullptr;
    }

    D3D11_TEXTURE2D_DESC srcDesc = {};
    sourceTexture->GetDesc(&srcDesc);

    bool needsCopy = (srcDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0;
    if (!needsCopy) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = ResolveCopyFormat(srcDesc.Format);
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(device_->CreateShaderResourceView(sourceTexture, &srvDesc, &source_srv_))) {
            if (FAILED(device_->CreateShaderResourceView(sourceTexture, nullptr, &source_srv_))) {
                needsCopy = true;
            }
        }
    }

    if (needsCopy) {
        if (source_copy_ &&
            (source_copy_width_ != srcDesc.Width ||
             source_copy_height_ != srcDesc.Height ||
             source_copy_format_ != srcDesc.Format)) {
            if (source_copy_srv_) {
                source_copy_srv_->Release();
                source_copy_srv_ = nullptr;
            }
            source_copy_->Release();
            source_copy_ = nullptr;
            source_copy_width_ = 0;
            source_copy_height_ = 0;
            source_copy_format_ = DXGI_FORMAT_UNKNOWN;
        }

        if (!source_copy_) {
            D3D11_TEXTURE2D_DESC copyDesc = srcDesc;
            copyDesc.SampleDesc.Count = 1;
            copyDesc.SampleDesc.Quality = 0;
            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            copyDesc.MiscFlags = 0;
            copyDesc.CPUAccessFlags = 0;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            copyDesc.Format = ResolveCopyFormat(srcDesc.Format);
            if (FAILED(device_->CreateTexture2D(&copyDesc, nullptr, &source_copy_)) || !source_copy_) {
                LOGSTR("VirtualScreen: Failed to create source copy texture\n");
                return;
            }
            source_copy_width_ = srcDesc.Width;
            source_copy_height_ = srcDesc.Height;
            source_copy_format_ = srcDesc.Format;
        }

        DXGI_FORMAT resolveFormat = ResolveCopyFormat(srcDesc.Format);
        if (srcDesc.SampleDesc.Count > 1) {
            context_->ResolveSubresource(source_copy_, 0, sourceTexture, 0, resolveFormat);
        } else {
            context_->CopyResource(source_copy_, sourceTexture);
        }

        if (!source_copy_srv_) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = ResolveCopyFormat(srcDesc.Format);
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            if (FAILED(device_->CreateShaderResourceView(source_copy_, &srvDesc, &source_copy_srv_))) {
                LOGSTR("VirtualScreen: Failed to create SRV for source copy\n");
                return;
            }
        }

        source_srv_ = source_copy_srv_;
    }

    // Render to each eye
    RenderToEye(VR::Eye::Left, sourceTexture);
    RenderToEye(VR::Eye::Right, sourceTexture);
}

void VirtualScreen::RenderToEye(VR::Eye eye, ID3D11Texture2D* sourceTexture) {
    int eyeIndex = static_cast<int>(eye);

    // Get eye view and projection matrices
    XMMATRIX viewMatrix = backend_->GetViewMatrix(eye);
    XMMATRIX projMatrix = backend_->GetProjectionMatrix(eye, 0.1f, 1000.0f);

    // Calculate world-view-projection matrix
    XMMATRIX worldViewProj = XMMatrixMultiply(screen_world_matrix_, viewMatrix);
    worldViewProj = XMMatrixMultiply(worldViewProj, projMatrix);

    // Update constant buffer
    ScreenConstants constants;
    constants.worldViewProj = XMMatrixTranspose(worldViewProj);
    constants.opacity = 1.0f;
    context_->UpdateSubresource(constant_buffer_, 0, nullptr, &constants, 0, 0);

    // Set render target
    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    context_->ClearRenderTargetView(eye_rtvs_[eyeIndex], clearColor);
    context_->OMSetRenderTargets(1, &eye_rtvs_[eyeIndex], nullptr);

    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(backend_->GetRecommendedWidth());
    viewport.Height = static_cast<float>(backend_->GetRecommendedHeight());
    viewport.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &viewport);

    // Set shaders and resources
    context_->VSSetShader(vertex_shader_, nullptr, 0);
    context_->PSSetShader(pixel_shader_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &constant_buffer_);
    context_->PSSetConstantBuffers(0, 1, &constant_buffer_);
    context_->PSSetShaderResources(0, 1, &source_srv_);
    context_->PSSetSamplers(0, 1, &sampler_);

    // Set geometry
    UINT stride = sizeof(ScreenVertex);
    UINT offset = 0;
    context_->IASetVertexBuffers(0, 1, &vertex_buffer_, &stride, &offset);
    context_->IASetIndexBuffer(index_buffer_, DXGI_FORMAT_R16_UINT, 0);
    context_->IASetInputLayout(input_layout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Draw
    context_->DrawIndexed(index_count_, 0, 0);

    // Cleanup
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context_->PSSetShaderResources(0, 1, &nullSRV);
}

ID3D11Texture2D* VirtualScreen::GetEyeTexture(VR::Eye eye) const {
    return eye_textures_[static_cast<int>(eye)];
}

void VirtualScreen::Recenter() {
    if (!backend_) return;

    // Get current head position and place screen in front
    XMMATRIX headPose = backend_->GetHeadPoseMatrix();
    XMVECTOR headPos = headPose.r[3];

    // Use negative Z as forward direction (standard VR convention)
    XMVECTOR headForward = XMVectorNegate(headPose.r[2]);
    headForward = XMVector3Normalize(headForward);

    XMVECTOR screenPos = XMVectorAdd(headPos, XMVectorScale(headForward, distance_));
    // Slightly below eye level for comfortable viewing
    screenPos = XMVectorAdd(screenPos, XMVectorSet(0, -0.3f, 0, 0));
    XMStoreFloat3(&screen_position_, screenPos);

    screen_world_matrix_ = CalculateScreenTransform();

    // Mark that we've been positioned (in case Update is called later)
    needs_initial_position_ = false;

    LOGSTR("VirtualScreen: Recentered\n");
}

XMMATRIX VirtualScreen::CalculateScreenTransform() const {
    // Get current head position from backend to face screen toward viewer
    XMVECTOR headPos = XMVectorZero();
    if (backend_) {
        XMMATRIX headPose = backend_->GetHeadPoseMatrix();
        headPos = headPose.r[3];
    }

    // Position the screen at screen_position_, facing the viewer (head)
    XMVECTOR pos = XMLoadFloat3(&screen_position_);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    // Calculate direction from screen to head (screen faces toward viewer)
    XMVECTOR toViewer = XMVectorSubtract(headPos, pos);
    XMVECTOR length = XMVector3Length(toViewer);
    float dist = XMVectorGetX(length);

    // If head is too close or at same position, use default forward direction
    XMVECTOR dir;
    if (dist < 0.1f) {
        dir = XMVectorSet(0, 0, 1, 0);  // Default: face toward +Z
    } else {
        dir = XMVector3Normalize(toViewer);
    }

    // Build orthonormal basis with screen facing viewer
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, dir));

    // Handle degenerate case where dir is parallel to up
    float dotUp = fabsf(XMVectorGetX(XMVector3Dot(dir, up)));
    if (dotUp > 0.99f) {
        right = XMVectorSet(1, 0, 0, 0);  // Use world X as right
    }

    XMVECTOR realUp = XMVector3Normalize(XMVector3Cross(dir, right));

    // Build rotation matrix (screen local space -> world)
    // Screen coordinates: right = local X, realUp = local Y, dir = local Z (toward viewer)
    XMMATRIX rotation;
    rotation.r[0] = right;
    rotation.r[1] = realUp;
    rotation.r[2] = dir;
    rotation.r[3] = XMVectorSet(0, 0, 0, 1);

    // Build translation matrix
    XMMATRIX translation = XMMatrixTranslationFromVector(pos);

    // Combine: rotation * translation (local rotation then translate to world position)
    return XMMatrixMultiply(rotation, translation);
}

} // namespace OVRInject
