#include "XRGraphicsBinding.hpp"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

namespace OVRInject {
namespace XR {

XRGraphicsBinding::XRGraphicsBinding() {
    memset(&binding_info_, 0, sizeof(binding_info_));
    binding_info_.type = XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;
}

XRGraphicsBinding::~XRGraphicsBinding() {
    Shutdown();
}

bool XRGraphicsBinding::Initialize(ID3D11Device* existingDevice) {
    if (device_ != nullptr) {
        LOGSTRF("Graphics binding already initialized\n");
        return true;
    }

    if (existingDevice == nullptr) {
        LOGSTRF("Cannot initialize with null device\n");
        return false;
    }

    LOGSTRF("Initializing graphics binding with existing D3D11 device...\n");

    device_ = existingDevice;
    device_->AddRef();  // Add reference since we're holding it

    device_->GetImmediateContext(&context_);

    owns_device_ = false;

    // Set up binding info
    binding_info_.device = device_;

    LOGSTRF("Graphics binding initialized with existing device\n");
    return true;
}

bool XRGraphicsBinding::InitializeWithLuid(const LUID& adapterLuid) {
    if (device_ != nullptr) {
        LOGSTRF("Graphics binding already initialized\n");
        return true;
    }

    LOGSTRF("Initializing graphics binding with adapter LUID %08lx-%08lx...\n",
            adapterLuid.HighPart, adapterLuid.LowPart);

    // Find the adapter matching the LUID
    IDXGIAdapter1* adapter = FindAdapterByLuid(adapterLuid);
    if (adapter == nullptr) {
        LOGSTRF("Could not find adapter with matching LUID\n");
        return false;
    }

    // Create device on this adapter
    bool success = CreateDeviceOnAdapter(adapter);
    adapter->Release();

    if (!success) {
        LOGSTRF("Failed to create D3D11 device\n");
        return false;
    }

    owns_device_ = true;

    // Set up binding info
    binding_info_.device = device_;

    LOGSTRF("Graphics binding initialized with new D3D11 device\n");
    return true;
}

void XRGraphicsBinding::Shutdown() {
    if (context_ != nullptr) {
        context_->Release();
        context_ = nullptr;
    }

    if (device_ != nullptr) {
        if (owns_device_) {
            LOGSTRF("Releasing owned D3D11 device\n");
        }
        device_->Release();
        device_ = nullptr;
    }

    owns_device_ = false;
    binding_info_.device = nullptr;
}

IDXGIAdapter1* XRGraphicsBinding::FindAdapterByLuid(const LUID& luid) {
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                     reinterpret_cast<void**>(factory.GetAddressOf()));
    if (FAILED(hr)) {
        LOGSTRF("Failed to create DXGI factory: 0x%08lx\n", hr);
        return nullptr;
    }

    IDXGIAdapter1* adapter = nullptr;
    UINT adapterIndex = 0;

    while (factory->EnumAdapters1(adapterIndex++, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.AdapterLuid.HighPart == luid.HighPart &&
            desc.AdapterLuid.LowPart == luid.LowPart) {
            LOGSTRF("Found adapter: %ls\n", desc.Description);
            return adapter;
        }

        adapter->Release();
    }

    return nullptr;
}

bool XRGraphicsBinding::CreateDeviceOnAdapter(IDXGIAdapter1* adapter) {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL actualFeatureLevel;

    HRESULT hr = D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,  // Must be UNKNOWN when specifying adapter
        nullptr,
        createFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &device_,
        &actualFeatureLevel,
        &context_);

    if (FAILED(hr)) {
        LOGSTRF("D3D11CreateDevice failed: 0x%08lx\n", hr);
        return false;
    }

    LOGSTRF("Created D3D11 device with feature level: 0x%x\n", actualFeatureLevel);
    return true;
}

ID3D11Texture2D* XRGraphicsBinding::CreateSharedTexture(
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format) {

    if (device_ == nullptr) {
        LOGSTRF("Cannot create texture: device not initialized\n");
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &texture);

    if (FAILED(hr)) {
        LOGSTRF("Failed to create shared texture: 0x%08lx\n", hr);
        return nullptr;
    }

    return texture;
}

ID3D11RenderTargetView* XRGraphicsBinding::CreateRenderTargetView(ID3D11Texture2D* texture) {
    if (device_ == nullptr || texture == nullptr) {
        return nullptr;
    }

    ID3D11RenderTargetView* rtv = nullptr;
    HRESULT hr = device_->CreateRenderTargetView(texture, nullptr, &rtv);

    if (FAILED(hr)) {
        LOGSTRF("Failed to create render target view: 0x%08lx\n", hr);
        return nullptr;
    }

    return rtv;
}

bool XRGraphicsBinding::CreateDepthStencil(
    uint32_t width,
    uint32_t height,
    ID3D11Texture2D** outTexture,
    ID3D11DepthStencilView** outView,
    ID3D11DepthStencilState** outState) {

    if (device_ == nullptr) {
        return false;
    }

    // Create depth texture
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depthTexture = nullptr;
    HRESULT hr = device_->CreateTexture2D(&depthDesc, nullptr, &depthTexture);
    if (FAILED(hr)) {
        LOGSTRF("Failed to create depth texture: 0x%08lx\n", hr);
        return false;
    }

    // Create depth stencil view
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    ID3D11DepthStencilView* dsv = nullptr;
    hr = device_->CreateDepthStencilView(depthTexture, &dsvDesc, &dsv);
    if (FAILED(hr)) {
        LOGSTRF("Failed to create depth stencil view: 0x%08lx\n", hr);
        depthTexture->Release();
        return false;
    }

    // Create depth stencil state
    D3D11_DEPTH_STENCIL_DESC depthStateDesc = {};
    depthStateDesc.DepthEnable = TRUE;
    depthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
    depthStateDesc.StencilEnable = FALSE;

    ID3D11DepthStencilState* depthState = nullptr;
    hr = device_->CreateDepthStencilState(&depthStateDesc, &depthState);
    if (FAILED(hr)) {
        LOGSTRF("Failed to create depth stencil state: 0x%08lx\n", hr);
        dsv->Release();
        depthTexture->Release();
        return false;
    }

    *outTexture = depthTexture;
    *outView = dsv;
    *outState = depthState;

    return true;
}

} // namespace XR
} // namespace OVRInject
