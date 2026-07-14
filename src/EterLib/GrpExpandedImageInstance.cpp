#include "StdAfx.h"
#include "EterBase/CRC32.h"
#include "GrpExpandedImageInstance.h"
#include "GrpDeviceDX11.h"
#include <cmath>
#include <EterBase/Timer.h>

// M2-UIWIDGET-24: External declaration of aggregated widget counters (defined in GrpImageInstance.cpp)
extern struct SUIWidgetCounters
{
	DWORD dwImageSuccess;
	DWORD dwExpandedSuccess;
	DWORD dwMarkSuccess;
	DWORD dwImageFail;
	DWORD dwExpandedFail;
	DWORD dwMarkFail;
	DWORD dwLastHeartbeatTime;
} s_kUIWidgetCounters;

CDynamicPool<CGraphicExpandedImageInstance>		CGraphicExpandedImageInstance::ms_kPool;

void CGraphicExpandedImageInstance::CreateSystem(UINT uCapacity)
{
	ms_kPool.Create(uCapacity);
}

void CGraphicExpandedImageInstance::DestroySystem()
{
	ms_kPool.Destroy();
}

CGraphicExpandedImageInstance* CGraphicExpandedImageInstance::New()
{
	return ms_kPool.Alloc();
}

void CGraphicExpandedImageInstance::Delete(CGraphicExpandedImageInstance* pkImgInst)
{
	pkImgInst->Destroy();
	ms_kPool.Free(pkImgInst);
}

bool CGraphicExpandedImageInstance::OnRenderDX11()
{
	// SBootstrapVertex structure
	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	// Validation
	CGraphicImage * pImage = m_roImage.GetPointer();
	if (!pImage)
		return false;

	CGraphicTexture * pTexture = pImage->GetTexturePointer();
	if (!pTexture)
		return false;

	// Get DX11 device
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return false;

	// Ensure bootstrap pipeline ready
	if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
	{
		static bool s_bLoggedBootstrapFail = false;
		if (!s_bLoggedBootstrapFail)
		{
			s_bLoggedBootstrapFail = true;
			TraceError("DX11_UI_EXPANDED_IMAGE_FAIL reason=bootstrap_resources_incomplete");
		}
		s_kUIWidgetCounters.dwExpandedFail++;
		return false;
	}

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11BlendState* pScreenBlendState = pDX11Device->GetBootstrapUIScreenBlendState();
	ID3D11BlendState* pModulateBlendState = pDX11Device->GetBootstrapUIModulateBlendState();
	ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();

	if (!pContext || !pVertexBuffer || !pVertexShader || !pPixelShader || !pInputLayout || !pSamplerState || !pAlphaBlendState || !pDepthDisableState)
	{
		static bool s_bLoggedResourceFail = false;
		if (!s_bLoggedResourceFail)
		{
			s_bLoggedResourceFail = true;
			TraceError("DX11_UI_EXPANDED_IMAGE_FAIL reason=bootstrap_resources_null");
		}
		s_kUIWidgetCounters.dwExpandedFail++;
		return false;
	}

	// Get texture SRV
	ID3D11ShaderResourceView* pTextureSRV = pTexture->GetD3D11TextureSRV();
	if (!pTextureSRV)
	{
		static bool s_bLoggedTextureFail = false;
		if (!s_bLoggedTextureFail)
		{
			s_bLoggedTextureFail = true;
			TraceError("DX11_UI_EXPANDED_IMAGE_FAIL reason=texture_srv_null");
		}
		s_kUIWidgetCounters.dwExpandedFail++;
		return false;
	}

	// Get backbuffer dimensions for NDC conversion
	UINT uBackBufferWidth = 0, uBackBufferHeight = 0;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (uBackBufferWidth == 0 || uBackBufferHeight == 0)
		return false;

	// World/game passes can leave non-main RT bound; force UI draw to visible backbuffer.
	pDX11Device->BindMainRenderTargets();

	// M2-UI23.A: Validate input coordinates before NDC conversion (same guard as DrawUIPrimitiveDX11)
	const float fMaxAbsInputX = 32768.0f;
	const float fMaxAbsInputY = 32768.0f;

	if (!std::isfinite(m_v2Position.x) || !std::isfinite(m_v2Position.y) ||
		std::fabs(m_v2Position.x) > fMaxAbsInputX || std::fabs(m_v2Position.y) > fMaxAbsInputY)
	{
		static DWORD s_dwInvalidCoordLogTick = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwInvalidCoordLogTick || (dwNow - s_dwInvalidCoordLogTick) >= 2000u)
		{
			s_dwInvalidCoordLogTick = dwNow;
			TraceError("DX11_UI_EXPANDED_IMAGE_FAIL reason=ui_input_range_reject px=%.3f py=%.3f max_abs=%.1f",
				m_v2Position.x, m_v2Position.y, fMaxAbsInputX);
		}
		s_kUIWidgetCounters.dwExpandedFail++;
		return false;
	}

	// NDC conversion lambdas
	auto PixelToNDCX = [uBackBufferWidth](float fX) -> float {
		return (2.0f * fX / static_cast<float>(uBackBufferWidth)) - 1.0f;
	};
	auto PixelToNDCY = [uBackBufferHeight](float fY) -> float {
		return 1.0f - (2.0f * fY / static_cast<float>(uBackBufferHeight));
	};

	// Calculate texture coordinates
	const RECT& c_rRect = pImage->GetRectReference();
	float texReverseWidth = 1.0f / float(pTexture->GetWidth());
	float texReverseHeight = 1.0f / float(pTexture->GetHeight());

	// Keep center-slice UV parity with legacy path, but with correct half-texel handling.
	const float uvInsetU = 0.5f * texReverseWidth;
	const float uvInsetV = 0.5f * texReverseHeight;

	float su = (c_rRect.left - m_RenderingRect.left) * texReverseWidth + uvInsetU;
	float sv = (c_rRect.top - m_RenderingRect.top) * texReverseHeight + uvInsetV;
	float eu = (c_rRect.left + m_RenderingRect.right + (c_rRect.right - c_rRect.left)) * texReverseWidth - uvInsetU;
	float ev = (c_rRect.top + m_RenderingRect.bottom + (c_rRect.bottom - c_rRect.top)) * texReverseHeight - uvInsetV;

	// Center-sliced widgets intentionally expand UVs outside [0..1]; clamp sampler causes banding.
	const bool bNeedsWrapSampler = (su < 0.0f) || (sv < 0.0f) || (eu > 1.0f) || (ev > 1.0f);
	static ID3D11SamplerState* s_pWrapPointSampler = nullptr;
	if (bNeedsWrapSampler && !s_pWrapPointSampler)
	{
		if (ID3D11Device* pDevice = pDX11Device->GetDevice())
		{
			D3D11_SAMPLER_DESC kSamplerDesc = {};
			kSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			kSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
			kSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
			kSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			kSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			kSamplerDesc.MinLOD = 0.0f;
			kSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			pDevice->CreateSamplerState(&kSamplerDesc, &s_pWrapPointSampler);
		}
	}
	if (bNeedsWrapSampler && s_pWrapPointSampler)
		pSamplerState = s_pWrapPointSampler;

	// M3-UI-COMPOSITE-PARITY-41A: Telemetry for center-slice stretched rendering (throttled 5s)
	if (m_RenderingRect.right > 0.0f || m_RenderingRect.bottom > 0.0f)
	{
		static DWORD s_dwLastCenterSliceLog = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (dwNow - s_dwLastCenterSliceLog >= 5000)
		{
			const char* mode = (m_RenderingRect.right > 0.0f && m_RenderingRect.bottom > 0.0f) ? "both" :
			                   (m_RenderingRect.right > 0.0f) ? "horizontal" : "vertical";
			TraceError("DX11_UI_CENTER_SLICE_PARITY mode=%s sampler=%s stretch_x=%.1f stretch_y=%.1f su=%.4f sv=%.4f eu=%.4f ev=%.4f",
			           mode,
			           (bNeedsWrapSampler ? "wrap_point" : "clamp_default"),
			           static_cast<float>(m_RenderingRect.right),
			           static_cast<float>(m_RenderingRect.bottom),
			           su, sv, eu, ev);
			s_dwLastCenterSliceLog = dwNow;
		}
	}

	// Build vertices with transformations (rotation, scale)
	float vertices_x[4] = { m_v2Position.x - 0.5f, m_v2Position.x - 0.5f, m_v2Position.x - 0.5f, m_v2Position.x - 0.5f };
	float vertices_y[4] = { m_v2Position.y - 0.5f, m_v2Position.y - 0.5f, m_v2Position.y - 0.5f, m_v2Position.y - 0.5f };

	if (0.0f == m_fRotation)
	{
		// No rotation - simple axis-aligned quad
		float fimgWidth = float(pImage->GetWidth()) * m_v2Scale.x;
		float fimgHeight = float(pImage->GetHeight()) * m_v2Scale.y;

		vertices_x[0] -= m_RenderingRect.left;
		vertices_y[0] -= m_RenderingRect.top;
		vertices_x[1] += fimgWidth + m_RenderingRect.right;
		vertices_y[1] -= m_RenderingRect.top;
		vertices_x[2] -= m_RenderingRect.left;
		vertices_y[2] += fimgHeight + m_RenderingRect.bottom;
		vertices_x[3] += fimgWidth + m_RenderingRect.right;
		vertices_y[3] += fimgHeight + m_RenderingRect.bottom;
	}
	else
	{
		// Rotation - apply rotation matrix
		float fimgHalfWidth = float(pImage->GetWidth()) / 2.0f * m_v2Scale.x;
		float fimgHalfHeight = float(pImage->GetHeight()) / 2.0f * m_v2Scale.y;

		for (int i = 0; i < 4; ++i)
		{
			vertices_x[i] += m_v2Origin.x;
			vertices_y[i] += m_v2Origin.y;
		}

		float fRadian = D3DXToRadian(m_fRotation);
		float cosTheta = cosf(fRadian);
		float sinTheta = sinf(fRadian);

		vertices_x[0] += (-fimgHalfWidth * cosTheta) - (-fimgHalfHeight * sinTheta);
		vertices_y[0] += (-fimgHalfWidth * sinTheta) + (-fimgHalfHeight * cosTheta);
		vertices_x[1] += (+fimgHalfWidth * cosTheta) - (-fimgHalfHeight * sinTheta);
		vertices_y[1] += (+fimgHalfWidth * sinTheta) + (-fimgHalfHeight * cosTheta);
		vertices_x[2] += (-fimgHalfWidth * cosTheta) - (+fimgHalfHeight * sinTheta);
		vertices_y[2] += (-fimgHalfWidth * sinTheta) + (+fimgHalfHeight * cosTheta);
		vertices_x[3] += (+fimgHalfWidth * cosTheta) - (+fimgHalfHeight * sinTheta);
		vertices_y[3] += (+fimgHalfWidth * sinTheta) + (+fimgHalfHeight * cosTheta);
	}

	// M2-UI23.A: Validate geometry span to prevent oversized rendered artifacts
	float fMinInputX = FLT_MAX;
	float fMaxInputX = -FLT_MAX;
	float fMinInputY = FLT_MAX;
	float fMaxInputY = -FLT_MAX;

	for (int i = 0; i < 4; ++i)
	{
		if (vertices_x[i] < fMinInputX) fMinInputX = vertices_x[i];
		if (vertices_x[i] > fMaxInputX) fMaxInputX = vertices_x[i];
		if (vertices_y[i] < fMinInputY) fMinInputY = vertices_y[i];
		if (vertices_y[i] > fMaxInputY) fMaxInputY = vertices_y[i];
	}

	const float fInputSpanX = fMaxInputX - fMinInputX;
	const float fInputSpanY = fMaxInputY - fMinInputY;
	const float fMaxAllowedSpanX = static_cast<float>(uBackBufferWidth) * 8.0f;
	const float fMaxAllowedSpanY = static_cast<float>(uBackBufferHeight) * 8.0f;

	if (!std::isfinite(fInputSpanX) || !std::isfinite(fInputSpanY) ||
		fInputSpanX > fMaxAllowedSpanX || fInputSpanY > fMaxAllowedSpanY)
	{
		static DWORD s_dwSpanRejectLogTick = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwSpanRejectLogTick || (dwNow - s_dwSpanRejectLogTick) >= 2000u)
		{
			s_dwSpanRejectLogTick = dwNow;
			TraceError("DX11_UI_EXPANDED_IMAGE_FAIL reason=ui_span_guard span_x=%.1f span_y=%.1f max_x=%.1f max_y=%.1f",
				fInputSpanX, fInputSpanY, fMaxAllowedSpanX, fMaxAllowedSpanY);
		}
		s_kUIWidgetCounters.dwExpandedFail++;
		return false;
	}

	// Convert to NDC and build SBootstrapVertex array (6 vertices for 2 triangles)
	SBootstrapVertex akVertices[6];

	// Triangle 1: 0-1-2
	akVertices[0] = {
		PixelToNDCX(vertices_x[0]), PixelToNDCY(vertices_y[0]), 0.0f,
		m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a,
		su, sv
	};
	akVertices[1] = {
		PixelToNDCX(vertices_x[1]), PixelToNDCY(vertices_y[1]), 0.0f,
		m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a,
		eu, sv
	};
	akVertices[2] = {
		PixelToNDCX(vertices_x[2]), PixelToNDCY(vertices_y[2]), 0.0f,
		m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a,
		su, ev
	};

	// Triangle 2: 1-3-2
	akVertices[3] = {
		PixelToNDCX(vertices_x[1]), PixelToNDCY(vertices_y[1]), 0.0f,
		m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a,
		eu, sv
	};
	akVertices[4] = {
		PixelToNDCX(vertices_x[3]), PixelToNDCY(vertices_y[3]), 0.0f,
		m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a,
		eu, ev
	};
	akVertices[5] = {
		PixelToNDCX(vertices_x[2]), PixelToNDCY(vertices_y[2]), 0.0f,
		m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a,
		su, ev
	};

	// Map vertex buffer
	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	HRESULT hr = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hr))
	{
		static bool s_bLoggedMapFail = false;
		if (!s_bLoggedMapFail)
		{
			s_bLoggedMapFail = true;
			TraceError("DX11_UI_EXPANDED_IMAGE_FAIL reason=vertex_buffer_map_failed");
		}
		s_kUIWidgetCounters.dwExpandedFail++;
		return false;
	}

	memcpy(kMappedResource.pData, akVertices, sizeof(akVertices));
	pContext->Unmap(pVertexBuffer, 0);

	// Set up pipeline state
	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(pVertexShader, nullptr, 0);
	pContext->PSSetShader(pPixelShader, nullptr, 0);

	// Set texture and sampler
	pContext->PSSetShaderResources(0, 1, &pTextureSRV);
	pContext->PSSetSamplers(0, 1, &pSamplerState);

	// Keep UI quads deterministic regardless of previous world cull state.
	if (pDX11Device->GetBootstrapRasterizerState())
		pContext->RSSetState(pDX11Device->GetBootstrapRasterizerState());

	// Match legacy DX9 blend behavior for expanded-image render modes.
	ID3D11BlendState* pBlendStateToUse = pAlphaBlendState;
	switch (m_iRenderingMode)
	{
	case RENDERING_MODE_SCREEN:
	case RENDERING_MODE_COLOR_DODGE:
		if (pScreenBlendState)
			pBlendStateToUse = pScreenBlendState;
		break;
	case RENDERING_MODE_MODULATE:
		if (pModulateBlendState)
			pBlendStateToUse = pModulateBlendState;
		break;
	default:
		break;
	}
	float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	pContext->OMSetBlendState(pBlendStateToUse, blendFactor, 0xFFFFFFFF);

	// Set depth state (disabled for 2D UI)
	pContext->OMSetDepthStencilState(pDepthDisableState, 0);

	// Draw
	pContext->Draw(6, 0);

	// M2-UIWIDGET-24: Increment aggregated success counter
	s_kUIWidgetCounters.dwExpandedSuccess++;

	// M2-UIWIDGET-24: Aggregated widget heartbeat (throttled to 5 seconds)
	DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_kUIWidgetCounters.dwLastHeartbeatTime > 5000)
	{
		s_kUIWidgetCounters.dwLastHeartbeatTime = dwNow;
		DWORD dwTotalFails = s_kUIWidgetCounters.dwImageFail + s_kUIWidgetCounters.dwExpandedFail + s_kUIWidgetCounters.dwMarkFail;
		TraceError("DX11_UI_WIDGET_HEARTBEAT image=%d expanded=%d mark=%d fail=%d",
			s_kUIWidgetCounters.dwImageSuccess,
			s_kUIWidgetCounters.dwExpandedSuccess,
			s_kUIWidgetCounters.dwMarkSuccess,
			dwTotalFails);

		// Reset counters after heartbeat to show delta since last report
		s_kUIWidgetCounters.dwImageSuccess = 0;
		s_kUIWidgetCounters.dwExpandedSuccess = 0;
		s_kUIWidgetCounters.dwMarkSuccess = 0;
		s_kUIWidgetCounters.dwImageFail = 0;
		s_kUIWidgetCounters.dwExpandedFail = 0;
		s_kUIWidgetCounters.dwMarkFail = 0;
	}

	return true;
}

void CGraphicExpandedImageInstance::OnRender()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedExpandedImageDeviceMissing = false;
		if (!s_bLoggedExpandedImageDeviceMissing)
		{
			s_bLoggedExpandedImageDeviceMissing = true;
			TraceError("DX11_UI_EXPANDED_IMAGE_FAIL reason=dx11_device_unavailable");
		}
		s_kUIWidgetCounters.dwExpandedFail++;
		return;
	}

	if (OnRenderDX11())
		return;

	static bool s_bLoggedExpandedDX11Fail = false;
	if (!s_bLoggedExpandedDX11Fail)
	{
		s_bLoggedExpandedDX11Fail = true;
		TraceError("DX11_UI_EXPANDED_IMAGE_FAIL reason=dx11_path_failed");
	}
}

void CGraphicExpandedImageInstance::SetDepth(float fDepth)
{
	m_fDepth = fDepth;
}

void CGraphicExpandedImageInstance::SetOrigin()
{
	SetOrigin(float(GetWidth()) / 2.0f, float(GetHeight()) / 2.0f);
}

void CGraphicExpandedImageInstance::SetOrigin(float fx, float fy)
{
	m_v2Origin.x = fx;
	m_v2Origin.y = fy;
}

void CGraphicExpandedImageInstance::SetRotation(float fRotation)
{
	m_fRotation = fRotation;
}

void CGraphicExpandedImageInstance::SetScale(float fx, float fy)
{
	m_v2Scale.x = fx;
	m_v2Scale.y = fy;
}

void CGraphicExpandedImageInstance::SetRenderingRect(float fLeft, float fTop, float fRight, float fBottom)
{
	if (IsEmpty())
		return;

	float fWidth = float(GetWidth());
	float fHeight = float(GetHeight());

	m_RenderingRect.left = fWidth * fLeft;
	m_RenderingRect.top = fHeight * fTop;
	m_RenderingRect.right = fWidth * fRight;
	m_RenderingRect.bottom = fHeight * fBottom;
}

void CGraphicExpandedImageInstance::SetRenderingMode(int iMode)
{
	m_iRenderingMode = iMode;
}

DWORD CGraphicExpandedImageInstance::Type()
{
	static DWORD s_dwType = GetCRC32("CGraphicExpandedImageInstance", strlen("CGraphicExpandedImageInstance"));
	return (s_dwType);
}

void CGraphicExpandedImageInstance::OnSetImagePointer()
{
	if (IsEmpty())
		return;

	SetOrigin(float(GetWidth()) / 2.0f, float(GetHeight()) / 2.0f);
}

BOOL CGraphicExpandedImageInstance::OnIsType(DWORD dwType)
{
	if (CGraphicExpandedImageInstance::Type() == dwType)
		return TRUE;

	return CGraphicImageInstance::IsType(dwType);
}

void CGraphicExpandedImageInstance::Initialize()
{
	m_iRenderingMode = RENDERING_MODE_NORMAL;
	m_fDepth = 0.0f;
	m_v2Origin.x = m_v2Origin.y = 0.0f;
	m_v2Scale.x = m_v2Scale.y = 1.0f;
	m_fRotation = 0.0f;
	memset(&m_RenderingRect, 0, sizeof(RECT));
}

void CGraphicExpandedImageInstance::Destroy()
{
	CGraphicImageInstance::Destroy();
	Initialize();
}

CGraphicExpandedImageInstance::CGraphicExpandedImageInstance()
{
	Initialize();
}

CGraphicExpandedImageInstance::~CGraphicExpandedImageInstance()
{
	Destroy();
}
