#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "../VR/IVRBackend.hpp"

#include "Math/Helpers.hpp"

namespace OVRInject
{
	class HMDRenderer {
	public:
		HMDRenderer(IDXGISwapChain* swap_chain,
		           VR::IVRBackend* backend,
		           float render_scale = 1.0f,
		           ID3D11Device* device = nullptr,
		           ID3D11DeviceContext* context = nullptr);
		virtual ~HMDRenderer();

		void Render(VR::Eye eye, ID3D11Texture2D* texture);

		ID3D11Texture2D* GetEyeTexture(VR::Eye eye);
		ID3D11RenderTargetView* GetEyeRenderTarget(VR::Eye eye);
		void Resize(float render_scale);

	private:
		void Initialize();
		void ReleaseTargets();
		DXGI_FORMAT ResolveEyeFormat() const;
		static DXGI_FORMAT NormalizeFormat(DXGI_FORMAT format);
		static DXGI_FORMAT ResolveViewFormat(DXGI_FORMAT format);

		VR::IVRBackend* backend_;
		float render_scale_ = 1.0f;
		DXGI_FORMAT eye_format_ = DXGI_FORMAT_R8G8B8A8_UNORM;

		IDXGISwapChain* swap_chain_;
		ID3D11Device* device_ = nullptr;
		ID3D11DeviceContext* context_ = nullptr;

		ID3D11Texture2D* eye_textures_[2] = {};
		ID3D11RenderTargetView* eye_rtvs_[2] = {};
	};
};
