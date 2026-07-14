#include "StdAfx.h"
#include "GrpScreen.h"
#include "Camera.h"
#include "GrpDeviceDX11.h"
#include "GrpTextureDX11.h"
#include "GrpTexture.h"

#include <comdef.h>
#include <utf8.h>
#include <EterBase/Timer.h>
#include <cmath>
#include <cfloat>
#include <EterBase/Stl.h>

DWORD		CScreen::ms_diffuseColor = 0xffffffff;
DWORD		CScreen::ms_clearColor = 0L;
DWORD		CScreen::ms_clearStencil = 0L;
float		CScreen::ms_clearDepth = 1.0f;
Frustum		CScreen::ms_frustum;

extern bool GRAPHICS_CAPS_CAN_NOT_DRAW_LINE;

namespace
{
	enum class EDX11TextureColorOp
	{
		TextureModulate,
		ColorOnly,
		ConstantColor,
		TextureAddColor,
	};

	static EDX11TextureColorOp gs_eDX11TextureColorOp = EDX11TextureColorOp::TextureModulate;
	static DWORD gs_dwDX11TextureFactor = 0xFFFFFFFFu;

	struct SBootstrapUIVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	inline void ConvertDiffuseToFloat4(const DWORD dwDiffuse, float& r, float& g, float& b, float& a)
	{
		a = static_cast<float>((dwDiffuse >> 24) & 0xFF) / 255.0f;
		r = static_cast<float>((dwDiffuse >> 16) & 0xFF) / 255.0f;
		g = static_cast<float>((dwDiffuse >> 8) & 0xFF) / 255.0f;
		b = static_cast<float>(dwDiffuse & 0xFF) / 255.0f;
	}

	bool DrawUIPrimitiveDX11(
		const SPDTVertexRaw* pVertices,
		const UINT uVertexCount,
		const D3D11_PRIMITIVE_TOPOLOGY eTopology,
		const char* c_szFailKey,
		ID3D11ShaderResourceView* pTextureSRV = NULL)
	{
		if (!pVertices || 0 == uVertexCount)
			return false;

		CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
		if (!pDX11Device || !pDX11Device->IsValid())
			return false;

		ID3D11Device* pDevice = pDX11Device->GetDevice();
		ID3D11DeviceContext* pContext = pDX11Device->GetContext();
		if (!pDevice || !pContext)
			return false;

		if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
		{
			static bool s_bLoggedBootstrapFail = false;
			if (!s_bLoggedBootstrapFail)
			{
				s_bLoggedBootstrapFail = true;
				TraceError("%s reason=bootstrap_not_ready", c_szFailKey);
			}
			return false;
		}

		UINT uBackBufferWidth = 0;
		UINT uBackBufferHeight = 0;
		CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
		if (0 == uBackBufferWidth || 0 == uBackBufferHeight)
		{
			// M2-UI22.A: Single-shot diagnostic for backbuffer size zero
			static bool s_bLoggedBackbufferZero = false;
			if (!s_bLoggedBackbufferZero)
			{
				s_bLoggedBackbufferZero = true;
				TraceError("%s reason=backbuffer_size_zero width=%u height=%u", c_szFailKey, uBackBufferWidth, uBackBufferHeight);
			}
			return false;
		}

		ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
		ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
		ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
		ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
		ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
		ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();
		ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
		ID3D11ShaderResourceView* pWhiteSRV = CGraphicTextureDX11::GetWhiteFallbackTexture(pDevice);
		if (!pInputLayout || !pVertexShader || !pPixelShader || !pVertexBuffer || !pAlphaBlendState || !pDepthDisableState || !pSamplerState || !pWhiteSRV)
		{
			// M2-UI22.A: Single-shot diagnostic for missing bootstrap resources
			static bool s_bLoggedMissingResource = false;
			if (!s_bLoggedMissingResource)
			{
				s_bLoggedMissingResource = true;
				TraceError("%s reason=missing_bootstrap_resource layout=%d vs=%d ps=%d vb=%d blend=%d depth=%d sampler=%d white=%d",
					c_szFailKey,
					!pInputLayout, !pVertexShader, !pPixelShader, !pVertexBuffer,
					!pAlphaBlendState, !pDepthDisableState, !pSamplerState, !pWhiteSRV);
			}
			return false;
		}

		// Bootstrap UI VB has headroom (4096 vertices). Keep a hard guard anyway.
		if (uVertexCount > 4096u)
		{
			// M2-UI22.A: Single-shot diagnostic for vertex count exceeding buffer
			static bool s_bLoggedVertexCountExceeded = false;
			if (!s_bLoggedVertexCountExceeded)
			{
				s_bLoggedVertexCountExceeded = true;
				TraceError("%s reason=vertex_count_exceeds_buffer count=%u max=4096", c_szFailKey, uVertexCount);
			}
			return false;
		}

		std::vector<SBootstrapUIVertex> akVertices;
		akVertices.resize(uVertexCount);
		const EDX11TextureColorOp eColorOp = gs_eDX11TextureColorOp;
		const bool bForceWhiteTexture =
			(eColorOp == EDX11TextureColorOp::ColorOnly) ||
			(eColorOp == EDX11TextureColorOp::ConstantColor);
		float fFactorR = 1.0f;
		float fFactorG = 1.0f;
		float fFactorB = 1.0f;
		float fFactorA = 1.0f;
		ConvertDiffuseToFloat4(gs_dwDX11TextureFactor, fFactorR, fFactorG, fFactorB, fFactorA);

		if (eColorOp == EDX11TextureColorOp::TextureAddColor)
		{
			static bool s_bLoggedAddColorApprox = false;
			if (!s_bLoggedAddColorApprox)
			{
				s_bLoggedAddColorApprox = true;
				TraceError("DX11_PIPELINE_STATE_PARITY pass=ui_color_op mode=texture_add emulation=vertex_bias");
			}
		}

		float fMinInputX = FLT_MAX;
		float fMaxInputX = -FLT_MAX;
		float fMinInputY = FLT_MAX;
		float fMaxInputY = -FLT_MAX;
		const float fMaxAbsInputX = static_cast<float>(uBackBufferWidth) * 4.0f;
		const float fMaxAbsInputY = static_cast<float>(uBackBufferHeight) * 4.0f;
		for (UINT i = 0; i < uVertexCount; ++i)
		{
			const float fPX = pVertices[i].px;
			const float fPY = pVertices[i].py;
			const float fPZ = pVertices[i].pz;
			// Guard against invalid/absurd coordinates that can explode into giant fan artifacts.
			if (!std::isfinite(fPX) || !std::isfinite(fPY) || !std::isfinite(fPZ) ||
				std::fabs(fPX) > 32768.0f || std::fabs(fPY) > 32768.0f || std::fabs(fPZ) > 1000000.0f)
			{
				static DWORD s_dwInvalidCoordLogTick = 0u;
				const DWORD dwNow = ELTimer_GetMSec();
				if (0u == s_dwInvalidCoordLogTick || (dwNow - s_dwInvalidCoordLogTick) >= 2000u)
				{
					s_dwInvalidCoordLogTick = dwNow;
					TraceError("%s reason=invalid_ui_vertex_coord px=%.3f py=%.3f pz=%.3f idx=%u", c_szFailKey, fPX, fPY, fPZ, i);
				}
				return false;
			}
			if (std::fabs(fPX) > fMaxAbsInputX || std::fabs(fPY) > fMaxAbsInputY)
			{
				static DWORD s_dwInputRangeRejectLogTick = 0u;
				const DWORD dwNow = ELTimer_GetMSec();
				if (0u == s_dwInputRangeRejectLogTick || (dwNow - s_dwInputRangeRejectLogTick) >= 2000u)
				{
					s_dwInputRangeRejectLogTick = dwNow;
					TraceError("%s reason=ui_input_range_reject px=%.3f py=%.3f max_abs_x=%.1f max_abs_y=%.1f idx=%u",
						c_szFailKey, fPX, fPY, fMaxAbsInputX, fMaxAbsInputY, i);
				}
				return false;
			}

			if (fPX < fMinInputX) fMinInputX = fPX;
			if (fPX > fMaxInputX) fMaxInputX = fPX;
			if (fPY < fMinInputY) fMinInputY = fPY;
			if (fPY > fMaxInputY) fMaxInputY = fPY;

			float r = 1.0f;
			float g = 1.0f;
			float b = 1.0f;
			float a = 1.0f;
			ConvertDiffuseToFloat4(pVertices[i].diffuse, r, g, b, a);
			switch (eColorOp)
			{
				case EDX11TextureColorOp::ColorOnly:
					break;
				case EDX11TextureColorOp::ConstantColor:
					r = fFactorR;
					g = fFactorG;
					b = fFactorB;
					a = fFactorA;
					break;
				case EDX11TextureColorOp::TextureAddColor:
					r = std::min(1.0f, r + fFactorR);
					g = std::min(1.0f, g + fFactorG);
					b = std::min(1.0f, b + fFactorB);
					a = std::min(1.0f, a * std::max(0.0f, fFactorA));
					break;
				case EDX11TextureColorOp::TextureModulate:
				default:
					break;
			}

			akVertices[i].x = (2.0f * pVertices[i].px / static_cast<float>(uBackBufferWidth)) - 1.0f;
			akVertices[i].y = 1.0f - (2.0f * pVertices[i].py / static_cast<float>(uBackBufferHeight));
			// UI primitives are strictly screen-space in DX11 path. Keep a fixed clip-space Z to
			// avoid artifacts caused by legacy per-vertex world/projection depth values.
			akVertices[i].z = 0.0f;
			akVertices[i].r = r;
			akVertices[i].g = g;
			akVertices[i].b = b;
			akVertices[i].a = a;
			akVertices[i].u = pVertices[i].u;
			akVertices[i].v = pVertices[i].v;
		}

		const float fInputSpanX = fMaxInputX - fMinInputX;
		const float fInputSpanY = fMaxInputY - fMinInputY;
		const float fMaxAllowedSpanX = static_cast<float>(uBackBufferWidth) * 3.0f;
		const float fMaxAllowedSpanY = static_cast<float>(uBackBufferHeight) * 3.0f;
		if (!std::isfinite(fInputSpanX) || !std::isfinite(fInputSpanY) ||
			fInputSpanX > fMaxAllowedSpanX || fInputSpanY > fMaxAllowedSpanY)
		{
			static DWORD s_dwUISpanRejectLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwUISpanRejectLogTick || (dwNow - s_dwUISpanRejectLogTick) >= 2000u)
			{
				s_dwUISpanRejectLogTick = dwNow;
				TraceError("%s reason=ui_span_guard span_x=%.1f span_y=%.1f max_x=%.1f max_y=%.1f",
					c_szFailKey, fInputSpanX, fInputSpanY, fMaxAllowedSpanX, fMaxAllowedSpanY);
			}
			return false;
		}

		D3D11_MAPPED_SUBRESOURCE kMappedResource = {};
		HRESULT hMapResult = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
		if (FAILED(hMapResult) || !kMappedResource.pData)
		{
			// M2-UI22.A: Single-shot diagnostic for Map() failure
			static bool s_bLoggedMapFailed = false;
			if (!s_bLoggedMapFailed)
			{
				s_bLoggedMapFailed = true;
				TraceError("%s reason=map_failed hr=0x%08x pdata=%d", c_szFailKey, hMapResult, kMappedResource.pData != nullptr);
			}
			return false;
		}
		memcpy(kMappedResource.pData, &akVertices[0], sizeof(SBootstrapUIVertex) * akVertices.size());
		pContext->Unmap(pVertexBuffer, 0);

		ID3D11RenderTargetView* pPreBindRTV = NULL;
		ID3D11DepthStencilView* pPreBindDSV = NULL;
		pContext->OMGetRenderTargets(1, &pPreBindRTV, &pPreBindDSV);
		const bool bHadActiveRTV = (pPreBindRTV != NULL);
		if (!bHadActiveRTV)
		{
			pDX11Device->BindMainRenderTargets();
			static DWORD s_dwLastUIRTRecoverLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwLastUIRTRecoverLogTick || (dwNow - s_dwLastUIRTRecoverLogTick) >= 10000u)
			{
				s_dwLastUIRTRecoverLogTick = dwNow;
				TraceError("%s reason=ui_rt_recovered_from_null", c_szFailKey);
			}
		}

		// M2-ETERLIB-STATE-48: Use explicit UI baseline state helper
		ID3D11BlendState* pOldBlendState = nullptr;
		FLOAT afOldBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
		UINT uOldSampleMask = 0u;
		ID3D11DepthStencilState* pOldDepthState = nullptr;
		UINT uOldStencilRef = 0u;
		ID3D11RasterizerState* pOldRasterState = nullptr;
		ID3D11SamplerState* pOldSampler0 = nullptr;
		pContext->OMGetBlendState(&pOldBlendState, afOldBlendFactor, &uOldSampleMask);
		pContext->OMGetDepthStencilState(&pOldDepthState, &uOldStencilRef);
		pContext->RSGetState(&pOldRasterState);
		pContext->PSGetSamplers(0, 1, &pOldSampler0);

		pDX11Device->SetUI2DBaselineState();

		UINT uStride = sizeof(SBootstrapUIVertex);
		UINT uOffset = 0;
		pContext->IASetInputLayout(pInputLayout);
		pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
		pContext->IASetPrimitiveTopology(eTopology);
		pContext->VSSetShader(pVertexShader, NULL, 0);
		pContext->PSSetShader(pPixelShader, NULL, 0);
		pContext->PSSetSamplers(0, 1, &pSamplerState);
		ID3D11ShaderResourceView* pBoundSRV = (!bForceWhiteTexture && pTextureSRV) ? pTextureSRV : pWhiteSRV;
		pContext->PSSetShaderResources(0, 1, &pBoundSRV);
		pContext->Draw(uVertexCount, 0);

		// M3-CORE-51: Telemetry for strict mode compliance (30s heartbeat)
		static DWORD s_dwLastGrpScreenTelemetryTick = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (dwNow - s_dwLastGrpScreenTelemetryTick > 30000)
		{
			s_dwLastGrpScreenTelemetryTick = dwNow;
			TraceError("DX11_PIPELINE_STATE_PARITY pass=ui_2d_strict path=dx11_native state_manager=unused");
		}

		ID3D11ShaderResourceView* pNullSRV = NULL;
		pContext->PSSetShaderResources(0, 1, &pNullSRV);
		pContext->PSSetSamplers(0, 1, &pOldSampler0);
		pContext->RSSetState(pOldRasterState);
		pContext->OMSetDepthStencilState(pOldDepthState, uOldStencilRef);
		pContext->OMSetBlendState(pOldBlendState, afOldBlendFactor, uOldSampleMask);

		safe_release(pOldSampler0);
		safe_release(pOldRasterState);
		safe_release(pOldDepthState);
		safe_release(pOldBlendState);
		if (pPreBindRTV)
			pPreBindRTV->Release();
		if (pPreBindDSV)
			pPreBindDSV->Release();
		return true;
	}

	bool DrawWorldPrimitiveDX11(
		const SPDTVertexRaw* pVerticesWorld,
		const UINT uVertexCount,
		const D3D11_PRIMITIVE_TOPOLOGY eTopology,
		const char* c_szFailKey)
	{
		if (!pVerticesWorld || 0 == uVertexCount)
			return false;

		UINT uBackBufferWidth = 0u;
		UINT uBackBufferHeight = 0u;
		CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
		if (0u == uBackBufferWidth || 0u == uBackBufferHeight)
			return false;

		GrpViewport kViewport;
		kViewport.X = 0u;
		kViewport.Y = 0u;
		kViewport.Width = uBackBufferWidth;
		kViewport.Height = uBackBufferHeight;
		kViewport.MinZ = 0.0f;
		kViewport.MaxZ = 1.0f;

		const D3DXMATRIX& matProj = CGraphicBase::GetProjMatrix();
		const D3DXMATRIX& matView = CGraphicBase::GetViewMatrix();
		const D3DXMATRIX& matWorld = CGraphicBase::GetIdentityMatrix();
		D3DXMATRIX matViewProj;
		D3DXMatrixMultiply(&matViewProj, &matView, &matProj);

		std::vector<SPDTVertexRaw> kProjected;
		kProjected.resize(uVertexCount);
		float fMinX = FLT_MAX;
		float fMaxX = -FLT_MAX;
		float fMinY = FLT_MAX;
		float fMaxY = -FLT_MAX;

		for (UINT i = 0u; i < uVertexCount; ++i)
		{
			const D3DXVECTOR3 kInWorld(pVerticesWorld[i].px, pVerticesWorld[i].py, pVerticesWorld[i].pz);
			D3DXVECTOR3 kOutView;
			D3DXVec3TransformCoord(&kOutView, &kInWorld, &matView);

			// If any vertex is on/behind camera plane, skip whole primitive.
			// CPU-projected world primitives can otherwise explode into screen-space fans.
			if (!std::isfinite(kOutView.z) || kOutView.z > -0.05f)
			{
				static DWORD s_dwWorldPrimitiveViewRejectLogTick = 0u;
				const DWORD dwNow = ELTimer_GetMSec();
				if (0u == s_dwWorldPrimitiveViewRejectLogTick || (dwNow - s_dwWorldPrimitiveViewRejectLogTick) >= 2000u)
				{
					s_dwWorldPrimitiveViewRejectLogTick = dwNow;
					TraceError("%s reason=view_space_plane_reject view_z=%.3f idx=%u", c_szFailKey, kOutView.z, i);
				}
				return false;
			}

			// Manual clip-space W (avoid extra D3DX helper dependency in strict DX11 builds).
			const float fClipW =
				(kInWorld.x * matViewProj._14) +
				(kInWorld.y * matViewProj._24) +
				(kInWorld.z * matViewProj._34) +
				matViewProj._44;
			if (!std::isfinite(fClipW) || std::fabs(fClipW) < 1e-5f)
				return false;

			D3DXVECTOR3 kOutScreen;
			D3DXVec3Project(&kOutScreen, &kInWorld, &kViewport, &matProj, &matView, &matWorld);

			if (!std::isfinite(kOutScreen.x) || !std::isfinite(kOutScreen.y) || !std::isfinite(kOutScreen.z))
				return false;

			// Fully invalid projection (behind near/far) - skip primitive instead of exploding into a fan.
			if (kOutScreen.z < -0.01f || kOutScreen.z > 1.01f)
				return false;

			kProjected[i] = pVerticesWorld[i];
			kProjected[i].px = kOutScreen.x;
			kProjected[i].py = kOutScreen.y;
			kProjected[i].pz = 0.0f;

			if (kOutScreen.x < fMinX) fMinX = kOutScreen.x;
			if (kOutScreen.x > fMaxX) fMaxX = kOutScreen.x;
			if (kOutScreen.y < fMinY) fMinY = kOutScreen.y;
			if (kOutScreen.y > fMaxY) fMaxY = kOutScreen.y;
		}

		const float fSpanX = fMaxX - fMinX;
		const float fSpanY = fMaxY - fMinY;
		const float fMaxAllowedX = static_cast<float>(uBackBufferWidth) * 3.0f;
		const float fMaxAllowedY = static_cast<float>(uBackBufferHeight) * 3.0f;
		if (!std::isfinite(fSpanX) || !std::isfinite(fSpanY) || fSpanX > fMaxAllowedX || fSpanY > fMaxAllowedY)
		{
			static DWORD s_dwWorldPrimitiveClippedLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwWorldPrimitiveClippedLogTick || (dwNow - s_dwWorldPrimitiveClippedLogTick) >= 2000u)
			{
				s_dwWorldPrimitiveClippedLogTick = dwNow;
				TraceError("%s reason=projected_span_guard span_x=%.1f span_y=%.1f max_x=%.1f max_y=%.1f",
					c_szFailKey, fSpanX, fSpanY, fMaxAllowedX, fMaxAllowedY);
			}
			return false;
		}

		return DrawUIPrimitiveDX11(&kProjected[0], uVertexCount, eTopology, c_szFailKey);
	}

	inline bool IsDX11RuntimeReady()
	{
		CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
		return pDX11Device && pDX11Device->IsValid();
	}
}

void CScreen::RenderLine3d(float sx, float sy, float sz, float ex, float ey, float ez)
{
	if (GRAPHICS_CAPS_CAN_NOT_DRAW_LINE)
		return;

	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderLine3dDX11(sx, sy, sz, ex, ey, ez);
}

void CScreen::RenderBox3d(float sx, float sy, float sz, float ex, float ey, float ez)
{
	if (GRAPHICS_CAPS_CAN_NOT_DRAW_LINE)
		return;

	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderBox3dDX11(sx, sy, sz, ex, ey, ez);
}

void CScreen::RenderBar3d(float sx, float sy, float sz, float ex, float ey, float ez)
{
	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderBar3dDX11(sx, sy, sz, ex, ey, ez);
}

void CScreen::RenderBar3d(const D3DXVECTOR3 * c_pv3Positions)
{
	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderBar3dDX11(c_pv3Positions);
}

void CScreen::RenderGradationBar3d(float sx, float sy, float sz, float ex, float ey, float ez, DWORD dwStartColor, DWORD dwEndColor)
{
	if (sx==ex) return;
	if (sy==ey) return;

	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderGradationBar3dDX11(sx, sy, sz, ex, ey, ez, dwStartColor, dwEndColor);
}

void CScreen::RenderLineCube(float sx, float sy, float sz, float ex, float ey, float ez)
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	D3DXVECTOR3 v[8] =
	{
		D3DXVECTOR3(sx, sy, sz),
		D3DXVECTOR3(ex, sy, sz),
		D3DXVECTOR3(sx, ey, sz),
		D3DXVECTOR3(ex, ey, sz),
		D3DXVECTOR3(sx, sy, ez),
		D3DXVECTOR3(ex, sy, ez),
		D3DXVECTOR3(sx, ey, ez),
		D3DXVECTOR3(ex, ey, ez),
	};

	static const int s_edges[12][2] =
	{
		{0,1},{1,3},{3,2},{2,0},
		{4,5},{5,7},{7,6},{6,4},
		{0,4},{1,5},{2,6},{3,7}
	};

	for (int i = 0; i < 12; ++i)
	{
		const D3DXVECTOR3& a = v[s_edges[i][0]];
		const D3DXVECTOR3& b = v[s_edges[i][1]];
		RenderLine3dDX11(a.x, a.y, a.z, b.x, b.y, b.z);
	}
}

void CScreen::RenderCube(float sx, float sy, float sz, float ex, float ey, float ez)
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	const D3DXVECTOR3 v[8] =
	{
		D3DXVECTOR3(sx, sy, sz),
		D3DXVECTOR3(ex, sy, sz),
		D3DXVECTOR3(sx, ey, sz),
		D3DXVECTOR3(ex, ey, sz),
		D3DXVECTOR3(sx, sy, ez),
		D3DXVECTOR3(ex, sy, ez),
		D3DXVECTOR3(sx, ey, ez),
		D3DXVECTOR3(ex, ey, ez),
	};

	static const int s_triIndices[36] =
	{
		0,1,3, 0,3,2, // front
		4,6,7, 4,7,5, // back
		0,2,6, 0,6,4, // left
		1,5,7, 1,7,3, // right
		0,4,5, 0,5,1, // top
		2,3,7, 2,7,6  // bottom
	};

	SPDTVertexRaw akFillVertices[36];
	for (int i = 0; i < 36; ++i)
	{
		const D3DXVECTOR3& p = v[s_triIndices[i]];
		akFillVertices[i].px = p.x;
		akFillVertices[i].py = p.y;
		akFillVertices[i].pz = p.z;
		akFillVertices[i].diffuse = ms_diffuseColor;
		akFillVertices[i].u = 0.0f;
		akFillVertices[i].v = 0.0f;
	}

	DrawWorldPrimitiveDX11(akFillVertices, 36, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "DX11_SCREEN_CUBE_RENDER_FAIL");
}

void CScreen::RenderCube(float sx, float sy, float sz, float ex, float ey, float ez, D3DXMATRIX matRotation)
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	D3DXVECTOR3 v3Center = D3DXVECTOR3((sx + ex) * 0.5f, (sy + ey) * 0.5f, (sz + ez) * 0.5f);
	D3DXVECTOR3 v3Vertex[8] =
	{
		D3DXVECTOR3(sx, sy, sz),
		D3DXVECTOR3(ex, sy, sz),
		D3DXVECTOR3(sx, ey, sz),
		D3DXVECTOR3(ex, ey, sz),
		D3DXVECTOR3(sx, sy, ez),
		D3DXVECTOR3(ex, sy, ez),
		D3DXVECTOR3(sx, ey, ez),
		D3DXVECTOR3(ex, ey, ez),
	};
	SPDTVertexRaw vertices[8];

	for(int i = 0; i < 8; i++)
	{
		v3Vertex[i] = v3Vertex[i] - v3Center;
		D3DXVec3TransformCoord(&v3Vertex[i], &v3Vertex[i], &matRotation);
		v3Vertex[i] = v3Vertex[i] + v3Center;
		vertices[i].px = v3Vertex[i].x;
		vertices[i].py = v3Vertex[i].y;
		vertices[i].pz = v3Vertex[i].z;
		vertices[i].diffuse = ms_diffuseColor;
		vertices[i].u = 0.0f; vertices[i].v = 0.0f;
	}

	static const int s_triIndices[36] =
	{
		0,1,3, 0,3,2, // front
		4,6,7, 4,7,5, // back
		0,2,6, 0,6,4, // left
		1,5,7, 1,7,3, // right
		0,4,5, 0,5,1, // top
		2,3,7, 2,7,6  // bottom
	};

	SPDTVertexRaw akFillVertices[36];
	for (int i = 0; i < 36; ++i)
	{
		const SPDTVertexRaw& src = vertices[s_triIndices[i]];
		akFillVertices[i] = src;
	}

	DrawWorldPrimitiveDX11(akFillVertices, 36, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "DX11_SCREEN_CUBE_ROT_RENDER_FAIL");
}

void CScreen::RenderLine2d(float sx, float sy, float ex, float ey, float z)
{
	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderLine2dDX11(sx, sy, ex, ey, z);
}

void CScreen::RenderBox2d(float sx, float sy, float ex, float ey, float z)
{
	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderBox2dDX11(sx, sy, ex, ey, z);
}

void CScreen::RenderBar2d(float sx, float sy, float ex, float ey, float z)
{
	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderBar2dDX11(sx, sy, ex, ey, z);
}

void CScreen::RenderGradationBar2d(float sx, float sy, float ex, float ey, DWORD dwStartColor, DWORD dwEndColor, float ez)
{
	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderGradationBar2dDX11(sx, sy, ex, ey, dwStartColor, dwEndColor, ez);
}

bool CScreen::RenderBox2dDX11(float sx, float sy, float ex, float ey, float z)
{
	SPDTVertexRaw vertices[8] =
	{
		{ sx, sy, z, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, sy, z, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, sy, z, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, ey, z, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, ey, z, ms_diffuseColor, 0.0f, 0.0f },
		{ sx, ey, z, ms_diffuseColor, 0.0f, 0.0f },
		{ sx, ey, z, ms_diffuseColor, 0.0f, 0.0f },
		{ sx, sy, z, ms_diffuseColor, 0.0f, 0.0f },
	};

	return DrawUIPrimitiveDX11(vertices, 8, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "DX11_UI2D_BOX_FAIL");
}

bool CScreen::RenderBar2dDX11(float sx, float sy, float ex, float ey, float z)
{
	SPDTVertexRaw vertices[4] =
	{
		{ sx, sy, z, ms_diffuseColor, 0.0f, 0.0f },
		{ sx, ey, z, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, sy, z, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, ey, z, ms_diffuseColor, 0.0f, 0.0f },
	};

	return DrawUIPrimitiveDX11(vertices, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "DX11_UI2D_BAR_FAIL");
}

bool CScreen::RenderLine2dDX11(float sx, float sy, float ex, float ey, float z)
{
	if (GRAPHICS_CAPS_CAN_NOT_DRAW_LINE)
		return false;

	SPDTVertexRaw vertices[2] =
	{
		{ sx, sy, z, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, ey, z, ms_diffuseColor, 0.0f, 0.0f },
	};

	return DrawUIPrimitiveDX11(vertices, 2, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "DX11_UI2D_LINE_FAIL");
}

bool CScreen::RenderGradationBar2dDX11(float sx, float sy, float ex, float ey, DWORD dwStartColor, DWORD dwEndColor, float ez)
{
	if (sx == ex || sy == ey)
	{
		// Degenerate gradation quads are used by legacy UI as separators/pixel bars.
		// Keep a 1px strip (not a pure line) to preserve fill parity with old UI.
		float sx0 = sx;
		float sy0 = sy;
		float ex0 = ex;
		float ey0 = ey;
		if (sx == ex)
			ex0 = sx + 1.0f;
		if (sy == ey)
			ey0 = sy + 1.0f;

		SPDTVertexRaw vertices[4] =
		{
			{ sx0, sy0, ez, dwStartColor, 0.0f, 0.0f },
			{ sx0, ey0, ez, dwEndColor, 0.0f, 0.0f },
			{ ex0, sy0, ez, dwStartColor, 0.0f, 0.0f },
			{ ex0, ey0, ez, dwEndColor, 0.0f, 0.0f },
		};
		const bool bStripRendered = DrawUIPrimitiveDX11(vertices, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "DX11_UI2D_GRADBAR_FAIL");

		static DWORD s_dwDegenerateGradbarStatsTick = 0u;
		static DWORD s_dwDegenerateGradbarCalls = 0u;
		static DWORD s_dwDegenerateGradbarFails = 0u;
		++s_dwDegenerateGradbarCalls;
		if (!bStripRendered)
			++s_dwDegenerateGradbarFails;

		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwDegenerateGradbarStatsTick || (dwNow - s_dwDegenerateGradbarStatsTick) >= 10000u)
		{
			s_dwDegenerateGradbarStatsTick = dwNow;
			TraceError(
				"DX11_UI2D_GRADBAR_DEGENERATE_STATS calls=%u fails=%u last_sx=%.3f last_sy=%.3f last_ex=%.3f last_ey=%.3f",
				s_dwDegenerateGradbarCalls, s_dwDegenerateGradbarFails, sx, sy, ex, ey);
			s_dwDegenerateGradbarCalls = 0u;
			s_dwDegenerateGradbarFails = 0u;
		}
		return bStripRendered;
	}

	SPDTVertexRaw vertices[4] =
	{
		{ sx, sy, ez, dwStartColor, 0.0f, 0.0f },
		{ sx, ey, ez, dwEndColor, 0.0f, 0.0f },
		{ ex, sy, ez, dwStartColor, 0.0f, 0.0f },
		{ ex, ey, ez, dwEndColor, 0.0f, 0.0f },
	};

	return DrawUIPrimitiveDX11(vertices, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "DX11_UI2D_GRADBAR_FAIL");
}

bool CScreen::RenderCircle2dDX11(float fx, float fy, float fz, float fRadius, int iStep)
{
	if (GRAPHICS_CAPS_CAN_NOT_DRAW_LINE)
		return false;

	if (iStep < 3 || iStep > 200)
		return false;

	std::vector<SPDTVertexRaw> vertices;
	vertices.reserve(iStep * 2);

	float theta = 0.0f;
	const float delta = 2.0f * D3DX_PI / float(iStep);
	D3DXVECTOR3 first(0.0f, 0.0f, fz);
	D3DXVECTOR3 prev(0.0f, 0.0f, fz);

	for (int count = 0; count < iStep; ++count)
	{
		D3DXVECTOR3 curr(
			fx + fRadius * cosf(theta),
			fy + fRadius * sinf(theta),
			fz);
		theta += delta;

		if (0 == count)
		{
			first = curr;
			prev = curr;
			continue;
		}

		vertices.push_back({ prev.x, prev.y, prev.z, ms_diffuseColor, 0.0f, 0.0f });
		vertices.push_back({ curr.x, curr.y, curr.z, ms_diffuseColor, 0.0f, 0.0f });
		prev = curr;
	}

	vertices.push_back({ prev.x, prev.y, prev.z, ms_diffuseColor, 0.0f, 0.0f });
	vertices.push_back({ first.x, first.y, first.z, ms_diffuseColor, 0.0f, 0.0f });

	return DrawUIPrimitiveDX11(&vertices[0], static_cast<UINT>(vertices.size()), D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "DX11_UI2D_CIRCLE_FAIL");
}

bool CScreen::RenderLine3dDX11(float sx, float sy, float sz, float ex, float ey, float ez)
{
	if (GRAPHICS_CAPS_CAN_NOT_DRAW_LINE)
		return false;

	SPDTVertexRaw vertices[2] =
	{
		{ sx, sy, sz, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, ey, ez, ms_diffuseColor, 0.0f, 0.0f }
	};

	return DrawWorldPrimitiveDX11(vertices, 2, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "DX11_UI_LINE_FAIL");
}

bool CScreen::RenderBox3dDX11(float sx, float sy, float sz, float ex, float ey, float ez)
{
	if (GRAPHICS_CAPS_CAN_NOT_DRAW_LINE)
		return false;

	SPDTVertexRaw vertices[8] =
	{
		{ sx, sy, sz, ms_diffuseColor, 0.0f, 0.0f },	// 0
		{ ex, sy, sz, ms_diffuseColor, 0.0f, 0.0f },	// 1

		{ sx, sy, sz, ms_diffuseColor, 0.0f, 0.0f },	// 0
		{ sx, ey, ez, ms_diffuseColor, 0.0f, 0.0f },	// 2

		{ ex, sy, sz, ms_diffuseColor, 0.0f, 0.0f },	// 1
		{ ex, ey, ez, ms_diffuseColor, 0.0f, 0.0f },	// 3

		{ sx, ey, ez, ms_diffuseColor, 0.0f, 0.0f },	// 2
		{ ex+1.0f, ey, ez, ms_diffuseColor, 0.0f, 0.0f }	// 3, keep last edge closed
	};

	return DrawWorldPrimitiveDX11(vertices, 8, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "DX11_UI_BOX_FAIL");
}

bool CScreen::RenderBar3dDX11(float sx, float sy, float sz, float ex, float ey, float ez)
{
	SPDTVertexRaw vertices[4] =
	{
		{ sx, sy, sz, ms_diffuseColor, 0.0f, 0.0f },
		{ sx, ey, ez, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, sy, sz, ms_diffuseColor, 0.0f, 0.0f },
		{ ex, ey, ez, ms_diffuseColor, 0.0f, 0.0f },
	};

	return DrawWorldPrimitiveDX11(vertices, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "DX11_UI_BAR_FAIL");
}

bool CScreen::RenderBar3dDX11(const D3DXVECTOR3 * c_pv3Positions)
{
	SPDTVertexRaw vertices[4] =
	{
		{ c_pv3Positions[0].x, c_pv3Positions[0].y, c_pv3Positions[0].z, ms_diffuseColor, 0.0f, 0.0f },
		{ c_pv3Positions[2].x, c_pv3Positions[2].y, c_pv3Positions[2].z, ms_diffuseColor, 0.0f, 0.0f },
		{ c_pv3Positions[1].x, c_pv3Positions[1].y, c_pv3Positions[1].z, ms_diffuseColor, 0.0f, 0.0f },
		{ c_pv3Positions[3].x, c_pv3Positions[3].y, c_pv3Positions[3].z, ms_diffuseColor, 0.0f, 0.0f },
	};

	return DrawWorldPrimitiveDX11(vertices, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "DX11_UI_BAR_FAIL");
}

bool CScreen::RenderGradationBar3dDX11(float sx, float sy, float sz, float ex, float ey, float ez, DWORD dwStartColor, DWORD dwEndColor)
{
	if (sx==ex) return false;
	if (sy==ey) return false;

	SPDTVertexRaw vertices[4] =
	{
		{ sx, sy, sz, dwStartColor, 0.0f, 0.0f },
		{ sx, ey, ez, dwEndColor, 0.0f, 0.0f },
		{ ex, sy, sz, dwStartColor, 0.0f, 0.0f },
		{ ex, ey, ez, dwEndColor, 0.0f, 0.0f },
	};

	return DrawWorldPrimitiveDX11(vertices, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "DX11_UI_GRADBAR_FAIL");
}

bool CScreen::RenderCircle3dDX11(float fx, float fy, float fz, float fRadius, int iStep)
{
	if (iStep < 3 || iStep > 200)
		return false;

	std::vector<D3DXVECTOR3> pts;
	pts.resize(iStep);

	float theta = 0.0f;
	float delta = 2 * D3DX_PI / float(iStep);

	const D3DXMATRIX & c_rmatInvView = CCameraManager::Instance().GetCurrentCamera()->GetBillboardMatrix();

	for (int count=0; count<iStep; count++)
	{
		pts[count] = D3DXVECTOR3(fRadius * cosf(theta), fRadius * sinf(theta), 0.0f);
		D3DXVec3TransformCoord(&pts[count], &pts[count], &c_rmatInvView);
		theta += delta;
	}

	// Build line list vertices (2 vertices per line segment)
	std::vector<SPDTVertexRaw> vertices;
	vertices.reserve(iStep * 2);

	for (int count=0; count<iStep - 1; count++)
	{
		vertices.push_back({ fx+pts[count].x, fy+pts[count].y, fz+pts[count].z, ms_diffuseColor, 0.0f, 0.0f });
		vertices.push_back({ fx+pts[count + 1].x, fy+pts[count + 1].y, fz+pts[count + 1].z, ms_diffuseColor, 0.0f, 0.0f });
	}
	// Close the circle
	vertices.push_back({ fx+pts[iStep - 1].x, fy+pts[iStep - 1].y, fz+pts[iStep - 1].z, ms_diffuseColor, 0.0f, 0.0f });
	vertices.push_back({ fx+pts[0].x, fy+pts[0].y, fz+pts[0].z, ms_diffuseColor, 0.0f, 0.0f });

	return DrawWorldPrimitiveDX11(&vertices[0], static_cast<UINT>(vertices.size()), D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "DX11_UI_CIRCLE_FAIL");
}

void CScreen::RenderCircle2d(float fx, float fy, float fz, float fRadius, int iStep)
{
	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderCircle2dDX11(fx, fy, fz, fRadius, iStep);
}

void CScreen::RenderCircle3d(float fx, float fy, float fz, float fRadius, int iStep)
{
	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderCircle3dDX11(fx, fy, fz, fRadius, iStep);
}

class CD3DXMeshRenderingOption : public CScreen
{
public:
	DWORD	m_dwVS;
	
	CD3DXMeshRenderingOption(GrpFillModeType fillMode, const D3DXMATRIX & c_rmatWorld)
	{
		m_dwVS = 0u;
		(void)fillMode;
		(void)c_rmatWorld;
	}

	virtual ~CD3DXMeshRenderingOption()
	{
	}
};

void CScreen::RenderD3DXMesh(LPD3DXMESH lpMesh, const D3DXMATRIX * c_pmatWorld, float fx, float fy, float fz, float fRadius, GrpFillModeType fillMode)
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	static bool s_bLoggedMeshEmulation = false;
	if (!s_bLoggedMeshEmulation)
	{
		s_bLoggedMeshEmulation = true;
		TraceError("DX11_SCREEN_MESH_RENDER d3dxmesh_emulated_dx11 reason=strict_no_legacy_mesh path=debug_wireframe");
	}

	auto TransformPoint = [c_pmatWorld](const D3DXVECTOR3& in) -> D3DXVECTOR3
	{
		if (!c_pmatWorld)
			return in;
		D3DXVECTOR3 out;
		D3DXVec3TransformCoord(&out, &in, c_pmatWorld);
		return out;
	};

	const int iStep = 24;
	for (int axis = 0; axis < 3; ++axis)
	{
		for (int i = 0; i < iStep; ++i)
		{
			const float a0 = (2.0f * D3DX_PI * i) / iStep;
			const float a1 = (2.0f * D3DX_PI * (i + 1)) / iStep;
			D3DXVECTOR3 p0, p1;
			if (axis == 0)
			{
				p0 = D3DXVECTOR3(fx + std::cos(a0) * fRadius, fy + std::sin(a0) * fRadius, fz);
				p1 = D3DXVECTOR3(fx + std::cos(a1) * fRadius, fy + std::sin(a1) * fRadius, fz);
			}
			else if (axis == 1)
			{
				p0 = D3DXVECTOR3(fx, fy + std::cos(a0) * fRadius, fz + std::sin(a0) * fRadius);
				p1 = D3DXVECTOR3(fx, fy + std::cos(a1) * fRadius, fz + std::sin(a1) * fRadius);
			}
			else
			{
				p0 = D3DXVECTOR3(fx + std::cos(a0) * fRadius, fy, fz + std::sin(a0) * fRadius);
				p1 = D3DXVECTOR3(fx + std::cos(a1) * fRadius, fy, fz + std::sin(a1) * fRadius);
			}

			const D3DXVECTOR3 w0 = TransformPoint(p0);
			const D3DXVECTOR3 w1 = TransformPoint(p1);
			this->RenderLine3dDX11(w0.x, w0.y, w0.z, w1.x, w1.y, w1.z);
		}
	}

	(void)lpMesh;
	(void)fillMode;
	return;
}

void CScreen::RenderSphere(const D3DXMATRIX * c_pmatWorld, float fx, float fy, float fz, float fRadius, GrpFillModeType fillMode)
{
	RenderD3DXMesh(NULL, c_pmatWorld, fx, fy, fz, fRadius, fillMode);
}

void CScreen::RenderCylinder(const D3DXMATRIX * c_pmatWorld, float fx, float fy, float fz, float fRadius, float /*fLength*/, GrpFillModeType fillMode)
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	static bool s_bLoggedCylinderEmulation = false;
	if (!s_bLoggedCylinderEmulation)
	{
		s_bLoggedCylinderEmulation = true;
		TraceError("DX11_SCREEN_CYLINDER_RENDER cylinder_emulated_dx11 reason=strict_no_legacy_mesh");
	}

	auto TransformPoint = [c_pmatWorld](const D3DXVECTOR3& in) -> D3DXVECTOR3
	{
		if (!c_pmatWorld)
			return in;
		D3DXVECTOR3 out;
		D3DXVec3TransformCoord(&out, &in, c_pmatWorld);
		return out;
	};

	const float fHalfLength = fRadius;
	const int iStep = 24;
	for (int i = 0; i < iStep; ++i)
	{
		const float a0 = (2.0f * D3DX_PI * i) / iStep;
		const float a1 = (2.0f * D3DX_PI * (i + 1)) / iStep;

		D3DXVECTOR3 pTop0(fx + std::cos(a0) * fRadius, fy + std::sin(a0) * fRadius, fz + fHalfLength);
		D3DXVECTOR3 pTop1(fx + std::cos(a1) * fRadius, fy + std::sin(a1) * fRadius, fz + fHalfLength);
		D3DXVECTOR3 pBot0(fx + std::cos(a0) * fRadius, fy + std::sin(a0) * fRadius, fz - fHalfLength);
		D3DXVECTOR3 pBot1(fx + std::cos(a1) * fRadius, fy + std::sin(a1) * fRadius, fz - fHalfLength);

		const D3DXVECTOR3 wTop0 = TransformPoint(pTop0);
		const D3DXVECTOR3 wTop1 = TransformPoint(pTop1);
		const D3DXVECTOR3 wBot0 = TransformPoint(pBot0);
		const D3DXVECTOR3 wBot1 = TransformPoint(pBot1);

		this->RenderLine3dDX11(wTop0.x, wTop0.y, wTop0.z, wTop1.x, wTop1.y, wTop1.z);
		this->RenderLine3dDX11(wBot0.x, wBot0.y, wBot0.z, wBot1.x, wBot1.y, wBot1.z);
	}

	for (int i = 0; i < 4; ++i)
	{
		const float a = (2.0f * D3DX_PI * i) / 4.0f;
		D3DXVECTOR3 pTop(fx + std::cos(a) * fRadius, fy + std::sin(a) * fRadius, fz + fHalfLength);
		D3DXVECTOR3 pBot(fx + std::cos(a) * fRadius, fy + std::sin(a) * fRadius, fz - fHalfLength);
		const D3DXVECTOR3 wTop = TransformPoint(pTop);
		const D3DXVECTOR3 wBot = TransformPoint(pBot);
		this->RenderLine3dDX11(wTop.x, wTop.y, wTop.z, wBot.x, wBot.y, wBot.z);
	}

	(void)fillMode;
	return;
}

void CScreen::RenderTextureBox(float sx, float sy, float ex, float ey, float z, float su, float sv, float eu, float ev)
{
	RenderTextureBox(sx, sy, ex, ey, static_cast<const CGraphicTexture*>(NULL), z, su, sv, eu, ev);
}

void CScreen::RenderTextureBox(float sx, float sy, float ex, float ey, const CGraphicTexture* pTexture, float z, float su, float sv, float eu, float ev)
{
	RenderTextureBox(sx, sy, ex, ey, pTexture, ms_diffuseColor, ms_diffuseColor, z, su, sv, eu, ev);
}

void CScreen::RenderTextureBox(float sx, float sy, float ex, float ey, const CGraphicTexture* pTexture, DWORD dwLeftColor, DWORD dwRightColor, float z, float su, float sv, float eu, float ev)
{
	TPDTVertex vertices[4];

	vertices[0].position = TPosition(sx, sy, z);
	vertices[0].diffuse = dwLeftColor;
	vertices[0].texCoord = TTextureCoordinate(su, sv);
	
	vertices[1].position = TPosition(ex, sy, z);
	vertices[1].diffuse = dwRightColor;
	vertices[1].texCoord = TTextureCoordinate(eu, sv);
	
	vertices[2].position = TPosition(sx, ey, z);
	vertices[2].diffuse = dwLeftColor;
	vertices[2].texCoord = TTextureCoordinate(su, ev);
	
	vertices[3].position = TPosition(ex, ey, z);
	vertices[3].diffuse = dwRightColor;
	vertices[3].texCoord = TTextureCoordinate(eu, ev);

	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		ID3D11ShaderResourceView* pTextureSRV = NULL;
		if (pTexture)
			pTextureSRV = pTexture->GetD3D11TextureSRV();
		DrawUIPrimitiveDX11(reinterpret_cast<const SPDTVertexRaw*>(vertices), 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "DX11_UI_TEXTURE_BOX_FAIL", pTextureSRV);
	}
	else
	{
		static bool s_bLoggedTextureBoxNoDX11Device = false;
		if (!s_bLoggedTextureBoxNoDX11Device)
		{
			s_bLoggedTextureBoxNoDX11Device = true;
			TraceError("DX11_UI_TEXTURE_BOX_FAIL reason=no_active_dx11_device");
		}
	}
}


bool CScreen::RenderBillboardDX11(D3DXVECTOR3 * Position, D3DXCOLOR & Color)
{
	DWORD dwColor = GetColor(Color.r, Color.g, Color.b, Color.a);

	SPDTVertexRaw vertices[4];
	vertices[0] = { Position[0].x, Position[0].y, Position[0].z, dwColor, 0.0f, 0.0f };
	vertices[1] = { Position[1].x, Position[1].y, Position[1].z, dwColor, 1.0f, 0.0f };
	vertices[2] = { Position[2].x, Position[2].y, Position[2].z, dwColor, 0.0f, 1.0f };
	vertices[3] = { Position[3].x, Position[3].y, Position[3].z, dwColor, 1.0f, 1.0f };

	return DrawUIPrimitiveDX11(vertices, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "DX11_UI_BILLBOARD_FAIL");
}

void CScreen::RenderBillboard(D3DXVECTOR3 * Position, D3DXCOLOR & Color)
{
	// M3-CORE-NATIVE-56: DX11 path mandatory in strict mode (guard removed)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		RenderBillboardDX11(Position, Color);
	else
	{
		static bool s_bLoggedBillboardNoDX11Device = false;
		if (!s_bLoggedBillboardNoDX11Device)
		{
			s_bLoggedBillboardNoDX11Device = true;
			TraceError("DX11_UI_BILLBOARD_FAIL reason=no_active_dx11_device");
		}
	}
}

void CScreen::DrawMinorGrid(float xMin, float yMin, float xMax, float yMax, float xminorStep, float yminorStep, float zPos)
{
	float x, y;
	
	for (y = yMin; y <= yMax; y += yminorStep)
		RenderLine2d(xMin, y, xMax, y, zPos);

	for (x = xMin; x <= xMax; x += xminorStep)
		RenderLine2d(x, yMin, x, yMax, zPos);
}

void CScreen::DrawGrid(float xMin, float yMin, float xMax, float yMax, float xmajorStep, float ymajorStep, float xminorStep, float yminorStep, float zPos)
{
	xMin*=xminorStep;
	xMax*=xminorStep;
	yMin*=yminorStep;
	yMax*=yminorStep;
	xmajorStep*=xminorStep;
	ymajorStep*=yminorStep;
	
	float x, y;
	
	SetDiffuseColor(0.5f, 0.5f, 0.5f);
	DrawMinorGrid(xMin, yMin, xMax, yMax, xminorStep, yminorStep, zPos);
	
	SetDiffuseColor(0.7f, 0.7f, 0.7f);
	for (y = 0.0f; y >= yMin; y -= ymajorStep)
		RenderLine2d(xMin, y, xMax, y, zPos);
	
	for (y = 0.0f; y <= yMax; y += ymajorStep)
		RenderLine2d(xMin, y, xMax, y, zPos);
	
	for (x = 0.0f; x >= xMin; x -= xmajorStep)
		RenderLine2d(x, yMin, x, yMax, zPos);
	
	for (x = 0.0f; x <= yMax; x += xmajorStep)
		RenderLine2d(x, yMin, x, yMax, zPos);

	SetDiffuseColor(1.0f, 1.0f, 1.0f);
	RenderLine2d(xMin, 0.0f, xMax, 0.0f, zPos);
	RenderLine2d(0.0f, yMin, 0.0f, yMax, zPos);
}

void CScreen::SetCursorPosition(int x, int y, int hres, int vres)
{
	D3DXVECTOR3 v;
	v.x = -(((2.0f * x) / hres) - 1) / ms_matProj._11;
	v.y = (((2.0f * y) / vres) - 1) / ms_matProj._22;
	v.z = 1.0f;

    D3DXMATRIX matViewInverse=ms_matInverseView;
    //D3DXMatrixInverse(&matViewInverse, NULL, &ms_matView);

    ms_vtPickRayDir.x = v.x * matViewInverse._11 + 
						v.y * matViewInverse._21 +
						v.z * matViewInverse._31;

    ms_vtPickRayDir.y = v.x * matViewInverse._12 +
						v.y * matViewInverse._22 +
						v.z * matViewInverse._32;

    ms_vtPickRayDir.z = v.x * matViewInverse._13 +
						v.y * matViewInverse._23 +
						v.z * matViewInverse._33;

    ms_vtPickRayOrig.x = matViewInverse._41;
    ms_vtPickRayOrig.y = matViewInverse._42;
    ms_vtPickRayOrig.z = matViewInverse._43;
	
//	// 2003. 9. 9 ë™í˜„ ì¶”ê°€
//	// ì§€í˜• pickingì„ ìœ„í•œ ë»˜ì§“... ã…¡ã…¡; ìœ„ì— ê²ƒê³¼ í†µí•© í•„ìš”...
	ms_Ray.SetStartPoint(ms_vtPickRayOrig);
	ms_Ray.SetDirection(-ms_vtPickRayDir, 51200.0f);
//	// 2003. 9. 9 ë™í˜„ ì¶”ê°€
}

bool CScreen::GetCursorPosition(float* px, float* py, float* pz)
{
	if (!GetCursorXYPosition(px, py)) return false;
	if (!GetCursorZPosition(pz)) return false;

	return true;
}

bool CScreen::GetCursorXYPosition(float* px, float* py)
{
	D3DXVECTOR3 v3Eye = CCameraManager::Instance().GetCurrentCamera()->GetEye();

	TPosition posVertices[4];
	posVertices[0] = TPosition(v3Eye.x-90000000.0f, v3Eye.y+90000000.0f, 0.0f);
	posVertices[1] = TPosition(v3Eye.x-90000000.0f, v3Eye.y-90000000.0f, 0.0f);
	posVertices[2] = TPosition(v3Eye.x+90000000.0f, v3Eye.y+90000000.0f, 0.0f);
	posVertices[3] = TPosition(v3Eye.x+90000000.0f, v3Eye.y-90000000.0f, 0.0f);

	static const WORD sc_awFillRectIndices[6] = { 0, 2, 1, 2, 3, 1, };

	float u, v, t;	
	for (int i = 0; i < 2; ++i)
	{
		if (IntersectTriangle(ms_vtPickRayOrig, ms_vtPickRayDir,
							 posVertices[sc_awFillRectIndices[i*3+0]],
							 posVertices[sc_awFillRectIndices[i*3+1]],
							 posVertices[sc_awFillRectIndices[i*3+2]],
							 &u, &v, &t))
		{
			*px = ms_vtPickRayOrig.x + ms_vtPickRayDir.x * t;
			*py = ms_vtPickRayOrig.y + ms_vtPickRayDir.y * t;
			return true;
		}
	}
	return false;
}

bool CScreen::GetCursorZPosition(float* pz)
{
	D3DXVECTOR3 v3Eye = CCameraManager::Instance().GetCurrentCamera()->GetEye();

	TPosition posVertices[4];
	posVertices[0] = TPosition(v3Eye.x-90000000.0f, 0.0f, v3Eye.z+90000000.0f);
	posVertices[1] = TPosition(v3Eye.x-90000000.0f, 0.0f, v3Eye.z-90000000.0f);
	posVertices[2] = TPosition(v3Eye.x+90000000.0f, 0.0f, v3Eye.z+90000000.0f);
	posVertices[3] = TPosition(v3Eye.x+90000000.0f, 0.0f, v3Eye.z-90000000.0f);

	static const WORD sc_awFillRectIndices[6] = { 0, 2, 1, 2, 3, 1, };

	float u, v, t;
	for (int i = 0; i < 2; ++i)
	{
		if (IntersectTriangle(ms_vtPickRayOrig, ms_vtPickRayDir,
							 posVertices[sc_awFillRectIndices[i*3+0]],
							 posVertices[sc_awFillRectIndices[i*3+1]],
							 posVertices[sc_awFillRectIndices[i*3+2]],
							 &u, &v, &t))
		{
			*pz = ms_vtPickRayOrig.z + ms_vtPickRayDir.z * t;
			return true;
		}
	}
	return false;
}

void CScreen::GetPickingPosition(float t, float* x, float* y, float* z)
{
	*x = ms_vtPickRayOrig.x + ms_vtPickRayDir.x * t;
	*y = ms_vtPickRayOrig.y + ms_vtPickRayDir.y * t;
	*z = ms_vtPickRayOrig.z + ms_vtPickRayDir.z * t;
}

void CScreen::SetDiffuseColor(DWORD diffuseColor)
{
	ms_diffuseColor = diffuseColor;
}

void CScreen::SetDiffuseColor(float r, float g, float b, float a)
{
	ms_diffuseColor = GetColor(r, g, b, a);
}

void CScreen::SetClearColor(float r, float g, float b, float a)
{
	ms_clearColor = GetColor(r, g, b, a);
}

void CScreen::SetClearDepth(float depth)
{
	ms_clearDepth = depth;
}

void CScreen::SetClearStencil(DWORD stencil)
{
	ms_clearStencil = stencil;
}

void CScreen::ClearDepthBuffer()
{
	if (IsDX11RuntimeReady())
		return;

	static bool s_bLoggedClearDepthStrictFail = false;
	if (!s_bLoggedClearDepthStrictFail)
	{
		s_bLoggedClearDepthStrictFail = true;
		TraceError("DX11_SCREEN_GUARD clear_depth_skipped reason=dx11_not_ready");
	}
	return;
}

void CScreen::Clear()
{
	if (IsDX11RuntimeReady())
		return;

	static bool s_bLoggedClearStrictFail = false;
	if (!s_bLoggedClearStrictFail)
	{
		s_bLoggedClearStrictFail = true;
		TraceError("DX11_SCREEN_GUARD clear_skipped reason=dx11_not_ready");
	}
	return;
}

BOOL CScreen::IsLostDevice()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	return (!pDX11Device || !pDX11Device->IsValid()) ? TRUE : FALSE;
}

BOOL CScreen::RestoreDevice()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return FALSE;

	// DX11 runtime does not use cooperative-level reset semantics from 
	// .
	return TRUE;
	
}

bool CScreen::Begin()
{
	ResetFaceCount();

	if (IsDX11RuntimeReady())
		return true;

	static bool s_bLoggedBeginStrictFail = false;
	if (!s_bLoggedBeginStrictFail)
	{
		s_bLoggedBeginStrictFail = true;
		TraceError("DX11_SCREEN_GUARD begin_skipped reason=dx11_not_ready");
	}
	return false;
}

void CScreen::End()
{
	if (IsDX11RuntimeReady())
		return;

	static bool s_bLoggedEndStrictFail = false;
	if (!s_bLoggedEndStrictFail)
	{
		s_bLoggedEndStrictFail = true;
		TraceError("DX11_SCREEN_GUARD end_skipped reason=dx11_not_ready");
	}
}

extern bool g_isBrowserMode;
extern RECT g_rcBrowser;

void CScreen::Show(HWND hWnd)
{
	if (IsDX11RuntimeReady())
	{
		CGraphicDeviceDX11::GetActiveDevice()->Present();
		return;
	}

	static bool s_bLoggedShowStrictFail = false;
	if (!s_bLoggedShowStrictFail)
	{
		s_bLoggedShowStrictFail = true;
		TraceError("DX11_SCREEN_GUARD show_skipped reason=dx11_not_ready");
	}
	return;
}

void CScreen::Show(RECT * pSrcRect)
{
	if (IsDX11RuntimeReady())
	{
		CGraphicDeviceDX11::GetActiveDevice()->Present();
		return;
	}

	static bool s_bLoggedShowRectStrictFail = false;
	if (!s_bLoggedShowRectStrictFail)
	{
		s_bLoggedShowRectStrictFail = true;
		TraceError("DX11_SCREEN_GUARD show_rect_skipped reason=dx11_not_ready");
	}
	return;
}

void CScreen::Show(RECT * pSrcRect, HWND hWnd)
{
	if (IsDX11RuntimeReady())
	{
		CGraphicDeviceDX11::GetActiveDevice()->Present();
		return;
	}

	static bool s_bLoggedShowRectHwndStrictFail = false;
	if (!s_bLoggedShowRectHwndStrictFail)
	{
		s_bLoggedShowRectHwndStrictFail = true;
		TraceError("DX11_SCREEN_GUARD show_rect_hwnd_skipped reason=dx11_not_ready");
	}
	return;
}

void CScreen::ProjectPosition(float x, float y, float z, float * pfX, float * pfY)
{
	// DX11 strict path can bypass legacy viewport updates; keep projection viewport in sync.
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
	{
		if (pDX11Device->IsValid())
		{
			UINT uBackBufferWidth = 0u;
			UINT uBackBufferHeight = 0u;
			CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
			if (uBackBufferWidth > 0u && uBackBufferHeight > 0u &&
				(ms_Viewport.Width != uBackBufferWidth || ms_Viewport.Height != uBackBufferHeight ||
				 ms_Viewport.MaxZ <= ms_Viewport.MinZ))
			{
				SetViewport(0u, 0u, uBackBufferWidth, uBackBufferHeight, 0.0f, 1.0f);
			}
		}
	}

	D3DXVECTOR3 Input(x, y, z);
	D3DXVECTOR3 Output;
	D3DXVec3Project(&Output, &Input, &ms_Viewport, &ms_matProj, &ms_matView, &ms_matWorld);

	*pfX = Output.x;
	*pfY = Output.y;
}

void CScreen::ProjectPosition(float x, float y, float z, float * pfX, float * pfY, float * pfZ)
{
	// DX11 strict path can bypass legacy viewport updates; keep projection viewport in sync.
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
	{
		if (pDX11Device->IsValid())
		{
			UINT uBackBufferWidth = 0u;
			UINT uBackBufferHeight = 0u;
			CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
			if (uBackBufferWidth > 0u && uBackBufferHeight > 0u &&
				(ms_Viewport.Width != uBackBufferWidth || ms_Viewport.Height != uBackBufferHeight ||
				 ms_Viewport.MaxZ <= ms_Viewport.MinZ))
			{
				SetViewport(0u, 0u, uBackBufferWidth, uBackBufferHeight, 0.0f, 1.0f);
			}
		}
	}

	D3DXVECTOR3 Input(x, y, z);
	D3DXVECTOR3 Output;
	D3DXVec3Project(&Output, &Input, &ms_Viewport, &ms_matProj, &ms_matView, &ms_matWorld);

	*pfX = Output.x;
	*pfY = Output.y;
	*pfZ = Output.z;
}

void CScreen::UnprojectPosition(float x, float y, float z, float * pfX, float * pfY, float * pfZ)
{
	// Keep viewport synced in strict DX11 path for stable pick/unproject results.
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
	{
		if (pDX11Device->IsValid())
		{
			UINT uBackBufferWidth = 0u;
			UINT uBackBufferHeight = 0u;
			CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
			if (uBackBufferWidth > 0u && uBackBufferHeight > 0u &&
				(ms_Viewport.Width != uBackBufferWidth || ms_Viewport.Height != uBackBufferHeight ||
				 ms_Viewport.MaxZ <= ms_Viewport.MinZ))
			{
				SetViewport(0u, 0u, uBackBufferWidth, uBackBufferHeight, 0.0f, 1.0f);
			}
		}
	}

	D3DXVECTOR3 Input(x, y, z);
	D3DXVECTOR3 Output;
	D3DXVec3Unproject(&Output, &Input, &ms_Viewport, &ms_matProj, &ms_matView, &ms_matWorld);

	*pfX = Output.x;
	*pfY = Output.y;
	*pfZ = Output.z;
}

void CScreen::SetColorOperation()
{
	gs_eDX11TextureColorOp = EDX11TextureColorOp::ColorOnly;
	gs_dwDX11TextureFactor = 0xFFFFFFFFu;
}

void CScreen::SetDiffuseOperation()
{
	gs_eDX11TextureColorOp = EDX11TextureColorOp::TextureModulate;
	gs_dwDX11TextureFactor = 0xFFFFFFFFu;
}

void CScreen::SetBlendOperation()
{
	gs_eDX11TextureColorOp = EDX11TextureColorOp::TextureModulate;
	gs_dwDX11TextureFactor = 0xFFFFFFFFu;
}

void CScreen::SetOneColorOperation(D3DXCOLOR & rColor)
{
	gs_eDX11TextureColorOp = EDX11TextureColorOp::ConstantColor;
	gs_dwDX11TextureFactor = GetColor(rColor.r, rColor.g, rColor.b, rColor.a);
}

void CScreen::SetAddColorOperation(D3DXCOLOR & rColor)
{
	gs_eDX11TextureColorOp = EDX11TextureColorOp::TextureAddColor;
	gs_dwDX11TextureFactor = GetColor(rColor.r, rColor.g, rColor.b, rColor.a);
}

void CScreen::Identity()
{
	ms_matWorld = ms_matIdentity;
}

CScreen::CScreen()
{
}

CScreen::~CScreen()
{
}
//void BuildViewFrustum() { ms_frustum.BuildViewFrustum(ms_matView*ms_matProj); }

void CScreen::BuildViewFrustum()
{
	const D3DXVECTOR3& c_rv3Eye=CCameraManager::Instance().GetCurrentCamera()->GetEye();
	const D3DXVECTOR3& c_rv3View=CCameraManager::Instance().GetCurrentCamera()->GetView();
	auto vv = ms_matView * ms_matProj;
	ms_frustum.BuildViewFrustum2(
		vv,
		ms_fNearY,
		ms_fFarY,
		ms_fFieldOfView,
		ms_fAspect,
		c_rv3Eye, c_rv3View);
}
