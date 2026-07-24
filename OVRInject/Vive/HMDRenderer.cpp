#include "targetver.h"

#include "HMDRenderer.hpp"
#include "../Log.hpp"
#include "../VR/SharedSettings.hpp"

#include <cmath>

using namespace OVRInject;

namespace {

void LogRenderScaleClamp(const char* owner,
                         float requestedScale,
                         float effectiveScale,
                         uint32_t baseWidth,
                         uint32_t baseHeight) {
    if (std::fabs(requestedScale - effectiveScale) < 0.001f) {
        return;
    }

    LOGSTRF("%s: Requested render scale %.2f clamped to %.2f for %ux%u eye resolution\n",
            owner, requestedScale, effectiveScale, baseWidth, baseHeight);
}

}

HMDRenderer::HMDRenderer(IDXGISwapChain* swap_chain,
                         VR::IVRBackend* backend,
                         float render_scale,
                         ID3D11Device* device,
                         ID3D11DeviceContext* context)
{
	swap_chain_ = swap_chain;
	backend_ = backend;
	render_scale_ = render_scale;
	device_ = device;
	context_ = context;

	if (device_) {
		device_->AddRef();
	}
	if (context_) {
		context_->AddRef();
	}

	Initialize();
}

HMDRenderer::~HMDRenderer()
{
	ReleaseTargets();

	if (context_) {
		context_->Release();
		context_ = nullptr;
	}
	if (device_) {
		device_->Release();
		device_ = nullptr;
	}
}

DXGI_FORMAT HMDRenderer::NormalizeFormat(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		return DXGI_FORMAT_R8G8B8A8_TYPELESS;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		return DXGI_FORMAT_B8G8R8A8_TYPELESS;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		return DXGI_FORMAT_R16G16B16A16_TYPELESS;
	default:
		return format;
	}
}

DXGI_FORMAT HMDRenderer::ResolveViewFormat(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	default:
		return format;
	}
}

DXGI_FORMAT HMDRenderer::ResolveEyeFormat() const
{
	if (backend_) {
		DXGI_FORMAT preferred = backend_->GetPreferredSwapchainFormat();
		if (preferred != DXGI_FORMAT_UNKNOWN) {
			LOGSTRF("HMDRenderer: Using backend preferred format %u\n", static_cast<unsigned>(preferred));
			return NormalizeFormat(preferred);
		}
	}

	if (!swap_chain_) {
		LOGSTR("HMDRenderer: No swap chain available for format probe, using RGBA8 fallback\n");
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	LOGSTR("HMDRenderer: Probing backbuffer format from swap chain\n");
	ID3D11Texture2D* backBuffer = nullptr;
	if (FAILED(swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)) || !backBuffer) {
		LOGSTR("HMDRenderer: Backbuffer probe failed, using RGBA8 fallback\n");
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	D3D11_TEXTURE2D_DESC desc = {};
	backBuffer->GetDesc(&desc);
	backBuffer->Release();

	return NormalizeFormat(desc.Format);
}

void HMDRenderer::Initialize()
{
	LOGSTR("HMDRenderer: Initialize begin\n");
	if (!swap_chain_ || !backend_) {
		LOGSTR("HMDRenderer: Missing swap chain or VR backend\n");
		return;
	}

	if (!device_) {
		LOGSTR("HMDRenderer: Resolving D3D11 device from swap chain\n");
		if (FAILED(swap_chain_->GetDevice(__uuidof(ID3D11Device), (void**)&device_)) || !device_) {
			LOGSTR("HMDRenderer: Failed to get D3D11 device from swap chain\n");
			return;
		}
	} else {
		LOGSTR("HMDRenderer: Using injected D3D11 device\n");
	}

	if (!context_) {
		LOGSTR("HMDRenderer: Resolving immediate context from device\n");
		device_->GetImmediateContext(&context_);
		if (!context_) {
			LOGSTR("HMDRenderer: Failed to get immediate context\n");
			return;
		}
	} else {
		LOGSTR("HMDRenderer: Using injected immediate context\n");
	}

	uint32_t baseWidth = backend_->GetRecommendedWidth();
	uint32_t baseHeight = backend_->GetRecommendedHeight();
	LOGSTRF("HMDRenderer: Backend recommended size %ux%u\n", baseWidth, baseHeight);
	uint32_t eyeWidth = 0;
	uint32_t eyeHeight = 0;
	float requestedScale = render_scale_;
	VR::ComputeSafeRenderSize(baseWidth, baseHeight, requestedScale, render_scale_, eyeWidth, eyeHeight);
	LogRenderScaleClamp("HMDRenderer", requestedScale, render_scale_, baseWidth, baseHeight);
	eye_format_ = ResolveEyeFormat();
	LOGSTRF("HMDRenderer: Using eye texture format %u\n", static_cast<unsigned>(eye_format_));
	LOGSTRF("HMDRenderer: Creating eye textures %ux%u (scale=%.2f)\n",
	        eyeWidth, eyeHeight, render_scale_);

	for (int i = 0; i < 2; i++)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = eyeWidth;
		desc.Height = eyeHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = eye_format_;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &eye_textures_[i]);
		if (FAILED(hr) || !eye_textures_[i]) {
			LOGSTRF("HMDRenderer: CreateTexture2D failed for eye %d (hr=0x%08X, %ux%u)\n",
			        i, static_cast<unsigned>(hr), desc.Width, desc.Height);
			continue;
		}

		D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
		rtv_desc.Format = ResolveViewFormat(desc.Format);
		rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtv_desc.Texture2D.MipSlice = 0;

		hr = device_->CreateRenderTargetView(eye_textures_[i], &rtv_desc, &eye_rtvs_[i]);
		if (FAILED(hr) || !eye_rtvs_[i]) {
			LOGSTRF("HMDRenderer: CreateRenderTargetView failed for eye %d (hr=0x%08X)\n",
			        i, static_cast<unsigned>(hr));
		}
	}
}

void HMDRenderer::ReleaseTargets()
{
	for (int i = 0; i < 2; i++)
	{
		if (eye_rtvs_[i]) {
			eye_rtvs_[i]->Release();
			eye_rtvs_[i] = nullptr;
		}
		if (eye_textures_[i]) {
			eye_textures_[i]->Release();
			eye_textures_[i] = nullptr;
		}
	}
}

void HMDRenderer::Resize(float render_scale)
{
	float effectiveScale = render_scale;
	if (backend_) {
		effectiveScale = VR::ComputeSafeRenderScale(render_scale,
			backend_->GetRecommendedWidth(),
			backend_->GetRecommendedHeight());
	}
	if (std::fabs(effectiveScale - render_scale_) < 0.001f) {
		return;
	}
	render_scale_ = effectiveScale;
	ReleaseTargets();
	Initialize();
	LOGSTRF("HMDRenderer: resized eye textures (scale=%.2f)\n", render_scale_);
}

void HMDRenderer::Render(VR::Eye eye, ID3D11Texture2D* texture)
{
	if (!context_ || !texture || !eye_textures_[(int)eye]) {
		return;
	}
	context_->CopyResource(eye_textures_[(int)eye], texture);
}

ID3D11Texture2D* HMDRenderer::GetEyeTexture(VR::Eye eye)
{
	return eye_textures_[(int)eye];
}

ID3D11RenderTargetView* HMDRenderer::GetEyeRenderTarget(VR::Eye eye)
{
	return eye_rtvs_[(int)eye];
}
