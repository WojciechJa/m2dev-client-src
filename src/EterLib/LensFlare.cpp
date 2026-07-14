///////////////////////////////////////////////////////////////////////  
//	CLensFlare Class
//
//	(c) 2003 IDV, Inc.
//
//	*** INTERACTIVE DATA VISUALIZATION (IDV) PROPRIETARY INFORMATION ***
//
//	This software is supplied under the terms of a license agreement or
//	nondisclosure agreement with Interactive Data Visualization and may
//	not be copied or disclosed except in accordance with the terms of
//	that agreement.
//
//      Copyright (c) 2001-2003 IDV, Inc.
//      All Rights Reserved.
//
//		IDV, Inc.
//		1233 Washington St. Suite 610
//		Columbia, SC 29201
//		Voice: (803) 799-1699
//		Fax:   (803) 931-0320
//		Web:   http://www.idvinc.com
//


///////////////////////////////////////////////////////////////////////  
//	Preprocessor
#include "StdAfx.h"
#include "LensFlare.h"
#include "Camera.h"
#include "ResourceManager.h"
#include "GrpDeviceDX11.h"
#include "GrpTextureDX11.h"
#include "EterBase/Timer.h"
#include "UserInterface/config.h"

#include <algorithm>
#include <math.h>
using namespace std;

///////////////////////////////////////////////////////////////////////  
//	Variables

static string g_strFiles[] = 
{
	"flare2.dds",
	"flare1.dds",
	"flare2.dds",
	"flare1.dds",
	"flare6.dds",
	"flare4.dds",
	"flare2.dds",
	"flare3.dds",
	""
};
static float g_fPosition[] =
{
	-0.55f,
	-0.5f,
	-0.45f,
	0.2f,
	0.3f,
	0.95f,
	0.9f,
	1.0f
};
static float g_fWidth[] =
{
	20.0f,
	32.0f,
	20.0f,
	32.0f,
	100.0f,
	32.0f,
	20.0f,
	250.0f
};

static float g_afColors[ ][4] =
{
    { 1.0f, 1.0f, 0.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.0f, 1.0f, 0.0f, 0.8f },
	{ 0.3f, 0.5f, 1.0f, 0.9f },
	{ 0.3f, 0.5f, 1.0f, 0.6f },
	{ 1.0f, 0.6f, 0.9f, 0.4f },
	{ 1.0f, 0.0f, 0.0f, 0.5f },
	{ 1.0f, 0.6f, 0.3f, 0.4f }
};

namespace
{
static DWORD gs_dwLensMainParityLogTick = 0u;
static DWORD gs_dwLensElementsParityLogTick = 0u;

inline void LogSkyenvLensParity(DWORD& rdwTick, const char* c_szPhase, UINT uExpected, UINT uSubmitted)
{
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u != rdwTick && (dwNow - rdwTick) < 2000u)
		return;

	rdwTick = dwNow;
	TraceError("DX11_PIPELINE_STATE_PARITY pass=skyenv path=dx11_native stage=lens phase=%s", c_szPhase);
	TraceError("DX11_PIPELINE_SUBMIT_PARITY pass=skyenv expected=%u submitted=%u stage=lens phase=%s",
		uExpected,
		uSubmitted,
		c_szPhase);
}

// M2-LENSFLARE-NATIVE-58: Helper to convert ARGB to float colors
inline void UnpackColorARGB(DWORD dwColor, float& fR, float& fG, float& fB, float& fA)
{
	fA = ((dwColor >> 24) & 0xFF) / 255.0f;
	fR = ((dwColor >> 16) & 0xFF) / 255.0f;
	fG = ((dwColor >> 8) & 0xFF) / 255.0f;
	fB = (dwColor & 0xFF) / 255.0f;
}

inline DWORD PackColor(float fR, float fG, float fB, float fA)
{
	return (DWORD(std::clamp(fA, 0.0f, 1.0f) * 255.0f) << 24) |
		(DWORD(std::clamp(fR, 0.0f, 1.0f) * 255.0f) << 16) |
		(DWORD(std::clamp(fG, 0.0f, 1.0f) * 255.0f) << 8) |
		DWORD(std::clamp(fB, 0.0f, 1.0f) * 255.0f);
}

// M2-LENSFLARE-NATIVE-58: DX11 helper for rendering flare quads
inline bool DrawFlareQuadDX11(
	CGraphicDeviceDX11* pDX11Device,
	float fX, float fY, float fSize,
	DWORD dwColor,
	ID3D11ShaderResourceView* pSRV,
	bool bAdditive,
	bool bUseDepthRead)
{
	if (!pDX11Device || !pDX11Device->IsValid())
		return false;

	if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
		return false;

	ID3D11Device* pDevice = pDX11Device->GetDevice();
	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pDevice || !pContext)
		return false;

	ID3D11ShaderResourceView* pWhiteSRV = CGraphicTextureDX11::GetWhiteFallbackTexture(pDevice);
	if (!pWhiteSRV)
		return false;
	if (!pSRV)
		pSRV = pWhiteSRV;

	ID3D11InputLayout* pIL = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11VertexShader* pVS = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pPS = pDX11Device->GetBootstrapUITexturePixelShader();
	ID3D11Buffer* pVB = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11BlendState* pBS = bAdditive ?
		pDX11Device->GetBootstrapUIAdditiveBlendState() :
		pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11DepthStencilState* pDSS = bUseDepthRead ? pDX11Device->GetBootstrapUIDepthReadState() : pDX11Device->GetBootstrapUIDepthDisableState();
	ID3D11RasterizerState* pRasterizerState = pDX11Device->GetBootstrapRasterizerState();
	ID3D11SamplerState* pSampler = pDX11Device->GetBootstrapUISamplerState();
	if (!pIL || !pVS || !pPS || !pVB || !pBS || !pDSS || !pRasterizerState || !pSampler)
		return false;

	// Convert screen-space to clip-space (NDC)
	// Screen (0 to width, 0 to height) → NDC (-1 to 1)
	const float fHalfWidth = pDX11Device->GetWidth() * 0.5f;
	const float fHalfHeight = pDX11Device->GetHeight() * 0.5f;

	const float fLeft = ((fX - fSize * 0.5f) / fHalfWidth) - 1.0f;
	const float fRight = ((fX + fSize * 0.5f) / fHalfWidth) - 1.0f;
	const float fTop = 1.0f - ((fY + fSize * 0.5f) / fHalfHeight);
	const float fBottom = 1.0f - ((fY - fSize * 0.5f) / fHalfHeight);

	float fR, fG, fB, fA;
	UnpackColorARGB(dwColor, fR, fG, fB, fA);

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	// Triangle strip quad (4 vertices)
	SBootstrapVertex akVertices[4] = {
		{ fLeft,  fTop,    0.0f, fR, fG, fB, fA, 0.0f, 0.0f },
		{ fRight, fTop,    0.0f, fR, fG, fB, fA, 1.0f, 0.0f },
		{ fLeft,  fBottom, 0.0f, fR, fG, fB, fA, 0.0f, 1.0f },
		{ fRight, fBottom, 0.0f, fR, fG, fB, fA, 1.0f, 1.0f }
	};

	D3D11_MAPPED_SUBRESOURCE kMapped = {};
	HRESULT hMapResult = pContext->Map(pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMapped);
	if (FAILED(hMapResult) || !kMapped.pData)
		return false;

	memcpy(kMapped.pData, akVertices, sizeof(akVertices));
	pContext->Unmap(pVB, 0);

	pDX11Device->BindMainRenderTargets();

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	pContext->IASetInputLayout(pIL);
	pContext->IASetVertexBuffers(0, 1, &pVB, &uStride, &uOffset);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	pContext->VSSetShader(pVS, nullptr, 0);
	pContext->PSSetShader(pPS, nullptr, 0);
	pContext->PSSetSamplers(0, 1, &pSampler);
	pContext->PSSetShaderResources(0, 1, &pSRV);

	const FLOAT afBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	pContext->OMSetBlendState(pBS, afBlendFactor, 0xffffffffu);
	pContext->OMSetDepthStencilState(pDSS, 0);
	pContext->RSSetState(pRasterizerState);

	pContext->Draw(4, 0);
	pDX11Device->IncrementFrameDrawCalls(1u, 2u);

	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);
	pContext->OMSetDepthStencilState(nullptr, 0);
	pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);
	pContext->RSSetState(nullptr);

	return true;
}
}


///////////////////////////////////////////////////////////////////////  
//	CLensFlare::CLensFlare

CLensFlare::CLensFlare() :
    m_fSunSize(0),
    m_fBeforeBright(0.0f),
    m_fAfterBright(0.0f),
    m_bFlareVisible(false),
    m_bDrawFlare(true),
    m_bDrawBrightScreen(true),
	m_bEnabled(true),
	m_bShowMainFlare(true),
	m_fMaxBrightness(1.0f)
{
    m_pControlPixels = new float[c_nDepthTestDimension * c_nDepthTestDimension];
    m_pTestPixels = new float[c_nDepthTestDimension * c_nDepthTestDimension];
	m_afColor[0] = m_afColor[1] = m_afColor[2] = 1.0f;
	m_uDX11OcclusionWriteIndex = 0u;
	m_uDX11OcclusionReadIndex = 0u;
	m_fDX11OcclusionVisibility = 1.0f;
	m_bDX11OcclusionReady = false;
	for (unsigned int i = 0; i < c_uDX11OcclusionQueryCount; ++i)
	{
		m_apDX11OcclusionQueries[i] = nullptr;
		m_abDX11OcclusionPending[i] = false;
	}
}


///////////////////////////////////////////////////////////////////////  
//	CLensFlare::~CLensFlare

CLensFlare::~CLensFlare()
{
	for (unsigned int i = 0; i < c_uDX11OcclusionQueryCount; ++i)
	{
		if (m_apDX11OcclusionQueries[i])
		{
			m_apDX11OcclusionQueries[i]->Release();
			m_apDX11OcclusionQueries[i] = nullptr;
		}
		m_abDX11OcclusionPending[i] = false;
	}

    delete[] m_pControlPixels;
    delete[] m_pTestPixels;
}

///////////////////////////////////////////////////////////////////////  
//	CLensFlare::Interpolate

float CLensFlare::Interpolate(float fStart, float fEnd, float fPercent)
{
	return fStart + (fEnd - fStart) * fPercent;
}

///////////////////////////////////////////////////////////////////////  
//	CLensFlare::DrawBeforeFlare

void CLensFlare::Compute(const DirectX::SimpleMath::Vector3& c_rv3LightDirection)
{
	float afSunPos[3];

	DirectX::SimpleMath::Vector3 v3Target = CCameraManager::Instance().GetCurrentCamera()->GetTarget();
	
	afSunPos[0]	= v3Target.x - c_rv3LightDirection.x * 99999999.0f;
	afSunPos[1]	= v3Target.y - c_rv3LightDirection.y * 99999999.0f;
	afSunPos[2]	= v3Target.z - c_rv3LightDirection.z * 99999999.0f;
	
	float fX, fY;
	ProjectPosition(afSunPos[0], afSunPos[1], afSunPos[2], &fX, &fY);
	
	// set flare location
	SetFlareLocation(fX, fY);
	
	// determine visibility
	float fSunVectorMagnitude = sqrtf(afSunPos[0] * afSunPos[0] +
		afSunPos[1] * afSunPos[1] +
		afSunPos[2] * afSunPos[2]);
	float afSunVector[3];
	afSunVector[0] = -afSunPos[0] / fSunVectorMagnitude;
	afSunVector[1] = -afSunPos[1] / fSunVectorMagnitude;
	afSunVector[2] = -afSunPos[2] / fSunVectorMagnitude;
	
	float afCameraDirection[3];
	afCameraDirection[0] = ms_matView._13;
	afCameraDirection[1] = ms_matView._23;
	afCameraDirection[2] = ms_matView._33;
	

	float fDotProduct = 
		(afSunVector[0] * afCameraDirection[0]) +
		(afSunVector[1] * afCameraDirection[1]) +
		(afSunVector[2] * afCameraDirection[2]);
	
	if (acosf(fDotProduct) < 0.5f * DirectX::XM_PI)
		SetVisible(true);
	else
		SetVisible(false);
	
	// set flare brightness
	fX /= ms_Viewport.Width;
	fY /= ms_Viewport.Height;
	
	float fDistance = sqrtf(((0.5f - fX) * (0.5f - fX)) + ((0.5f - fY) * (0.5f - fY)));
	float fBeforeBright = Interpolate(0.0f, c_fHalfMaxBright, 1.0f - (fDistance * c_fDistanceScale));
	float fAfterBright = Interpolate(0.0f, 1.0f, 1.0f - (fDistance * c_fDistanceScale));
	
	SetBrightnesses(fBeforeBright, fAfterBright);
}

///////////////////////////////////////////////////////////////////////  
//	CLensFlare::DrawBeforeFlare

void CLensFlare::DrawBeforeFlare()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedDX11Unavailable = false;
		if (!s_bLoggedDX11Unavailable)
		{
			s_bLoggedDX11Unavailable = true;
			TraceError("DX11_SKYENV_DRAW_FAIL stage=lens phase=main reason=dx11_device_unavailable");
		}
		return;
	}

	if (!m_bFlareVisible || !m_bEnabled || !m_bShowMainFlare)
		return;

	if (m_SunFlareImageInstance.IsEmpty())
		return;

	ID3D11ShaderResourceView* pSRV = nullptr;
	if (m_SunFlareImageInstance.GetGraphicImagePointer())
		pSRV = m_SunFlareImageInstance.GetGraphicImagePointer()->GetTextureReference().GetD3D11TextureSRV();

	if (!pSRV)
		pSRV = CGraphicTextureDX11::GetWhiteFallbackTexture(pDX11Device->GetDevice());

	const float fScreenX = m_afFlarePos[0] * ms_Viewport.Width;
	const float fScreenY = m_afFlarePos[1] * ms_Viewport.Height;
	const float fScreenSize = m_fSunSize * ms_Viewport.Height;

	SubmitDX11OcclusionProbe(pDX11Device->GetContext(), pDX11Device);

	const DWORD dwColor = PackColor(1.0f, 1.0f, 1.0f, 1.0f);
	const UINT uExpected = 1u;
	UINT uSubmitted = 0u;
	if (DrawFlareQuadDX11(pDX11Device, fScreenX, fScreenY, fScreenSize, dwColor, pSRV, false, false))
		uSubmitted = 1u;

	LogSkyenvLensParity(gs_dwLensMainParityLogTick, "main", uExpected, uSubmitted);

	static bool s_bLoggedSunFlareFail = false;
	if (0u == uSubmitted && !s_bLoggedSunFlareFail)
	{
		s_bLoggedSunFlareFail = true;
		TraceError("DX11_SKYENV_DRAW_FAIL stage=lens phase=main reason=bootstrap_draw_failed");
	}
}

///////////////////////////////////////////////////////////////////////
//	CLensFlare::DrawAfterFlare

void CLensFlare::DrawAfterFlare()
{
	if (m_bEnabled && m_fAfterBright != 0.0f && m_bDrawBrightScreen)
	{
		SetDiffuseColor(m_afColor[0], m_afColor[1], m_afColor[2], m_fAfterBright);

		// FIX: Use actual viewport size instead of hardcoded 1024x1024
		// This prevents screen split issue where lens flare only covers part of screen
		const float fWidth = static_cast<float>(ms_Viewport.Width);
		const float fHeight = static_cast<float>(ms_Viewport.Height);
		RenderBar2d(0.0f, 0.0f, fWidth, fHeight);
	}
}


///////////////////////////////////////////////////////////////////////  
//	CLensFlare::SetMainFlare

void CLensFlare::SetMainFlare(string strSunFile, float fSunSize)
{
	if (m_bEnabled && m_bShowMainFlare)
	{
		m_fSunSize = fSunSize;
		CResource * pResource = CResourceManager::Instance().GetResourcePointer(strSunFile.c_str()); 

		if (!pResource->IsType(CGraphicImage::Type()))
			assert(false);
		
		m_SunFlareImageInstance.SetImagePointer(static_cast<CGraphicImage *> (pResource));
	}
}


///////////////////////////////////////////////////////////////////////  
//	CLensFlare::DrawFlare

void CLensFlare::DrawFlare()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedDX11Unavailable = false;
		if (!s_bLoggedDX11Unavailable)
		{
			s_bLoggedDX11Unavailable = true;
			TraceError("DX11_SKYENV_DRAW_FAIL stage=lens phase=elements reason=dx11_device_unavailable");
		}
		return;
	}

	if (m_bEnabled && m_bFlareVisible && m_bDrawFlare && m_fAfterBright != 0.0f)
	{
		DrawAfterFlare();
		m_cFlare.Draw(
			m_fAfterBright,
			ms_Viewport.Width,
			ms_Viewport.Height,
			static_cast<int>(m_afFlareWinPos[0]),
			static_cast<int>(m_afFlareWinPos[1]));
	}
}

///////////////////////////////////////////////////////////////////////
//	CLensFlare::CharacterizeFlare
void CLensFlare::CharacterizeFlare(bool bEnabled, bool bShowMainFlare, float fMaxBrightness, const DirectX::SimpleMath::Color& c_rColor)
{
	m_bEnabled = bEnabled;
	m_bShowMainFlare = bShowMainFlare;
	m_fMaxBrightness = fMaxBrightness;

	m_afColor[0] = c_rColor.x;
	m_afColor[1] = c_rColor.y;
	m_afColor[2] = c_rColor.z;
}


///////////////////////////////////////////////////////////////////////  
//	CLensFlare::Initialize
void CLensFlare::Initialize(std::string strPath)
{
	if (m_bEnabled)
		m_cFlare.Init(strPath);
}


///////////////////////////////////////////////////////////////////////  
//	CLensFlare::SetFlareLocation
void CLensFlare::SetFlareLocation(double dX, double dY)
{
	if (m_bEnabled)
	{
		m_afFlareWinPos[0] = float(dX);
		m_afFlareWinPos[1] = float(dY);

		m_afFlarePos[0] = float(dX) / ms_Viewport.Width;
		m_afFlarePos[1] = float(dY) / ms_Viewport.Height;
	}
}


///////////////////////////////////////////////////////////////////////  
//	CLensFlare::SetBrightnesses

void CLensFlare::SetBrightnesses(float fBeforeBright, float fAfterBright)
{
	if (m_bEnabled)
	{
	    m_fBeforeBright = fBeforeBright;
	    m_fAfterBright = fAfterBright;

		ClampBrightness();
	}
}


bool CLensFlare::EnsureDX11OcclusionQueries(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (m_bDX11OcclusionReady)
		return true;

	D3D11_QUERY_DESC kQueryDesc = {};
	kQueryDesc.Query = D3D11_QUERY_OCCLUSION;
	kQueryDesc.MiscFlags = 0;

	for (unsigned int i = 0; i < c_uDX11OcclusionQueryCount; ++i)
	{
		if (m_apDX11OcclusionQueries[i])
			continue;

		if (FAILED(pDevice->CreateQuery(&kQueryDesc, &m_apDX11OcclusionQueries[i])) || !m_apDX11OcclusionQueries[i])
			return false;
	}

	m_bDX11OcclusionReady = true;
	return true;
}

void CLensFlare::SubmitDX11OcclusionProbe(ID3D11DeviceContext* pContext, CGraphicDeviceDX11* pDX11Device)
{
	if (!pContext || !pDX11Device || !m_bEnabled || !m_bFlareVisible)
		return;

	if (!EnsureDX11OcclusionQueries(pDX11Device->GetDevice()))
		return;

	if (m_abDX11OcclusionPending[m_uDX11OcclusionWriteIndex])
		return;

	ID3D11Query* pQuery = m_apDX11OcclusionQueries[m_uDX11OcclusionWriteIndex];
	if (!pQuery)
		return;

	pContext->Begin(pQuery);
	const float fScreenX = m_afFlarePos[0] * ms_Viewport.Width;
	const float fScreenY = m_afFlarePos[1] * ms_Viewport.Height;
	const float fProbeSize = DX11RuntimeConfig::kLensFlareOcclusionProbeSizePx;
	ID3D11ShaderResourceView* pProbeSRV = CGraphicTextureDX11::GetWhiteFallbackTexture(pDX11Device->GetDevice());
	const DWORD dwProbeColor = PackColor(1.0f, 1.0f, 1.0f, 0.0f);
	DrawFlareQuadDX11(pDX11Device, fScreenX, fScreenY, fProbeSize, dwProbeColor, pProbeSRV, false, true);
	pContext->End(pQuery);

	m_abDX11OcclusionPending[m_uDX11OcclusionWriteIndex] = true;
	m_uDX11OcclusionWriteIndex = (m_uDX11OcclusionWriteIndex + 1u) % c_uDX11OcclusionQueryCount;
}

void CLensFlare::ConsumeDX11OcclusionSample(ID3D11DeviceContext* pContext)
{
	if (!pContext || !m_bDX11OcclusionReady)
		return;

	if (!m_abDX11OcclusionPending[m_uDX11OcclusionReadIndex])
		return;

	ID3D11Query* pQuery = m_apDX11OcclusionQueries[m_uDX11OcclusionReadIndex];
	if (!pQuery)
		return;

	UINT64 uSamples = 0u;
	const HRESULT hResult = pContext->GetData(pQuery, &uSamples, sizeof(uSamples), D3D11_ASYNC_GETDATA_DONOTFLUSH);
	if (hResult != S_OK)
		return;

	m_abDX11OcclusionPending[m_uDX11OcclusionReadIndex] = false;
	m_uDX11OcclusionReadIndex = (m_uDX11OcclusionReadIndex + 1u) % c_uDX11OcclusionQueryCount;

	const float fVisibility = (uSamples > 0u) ? 1.0f : 0.0f;
	const float fSmoothing = std::min(1.0f, std::max(0.0f, DX11RuntimeConfig::kLensFlareOcclusionSmoothing));
	m_fDX11OcclusionVisibility += (fVisibility - m_fDX11OcclusionVisibility) * fSmoothing;

	static DWORD s_dwLensOcclusionLogTick = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwLensOcclusionLogTick || (dwNow - s_dwLensOcclusionLogTick) >= 2000u)
	{
		s_dwLensOcclusionLogTick = dwNow;
		TraceError("DX11_LENSFLARE_OCCLUSION samples=%llu visibility=%.3f", static_cast<unsigned long long>(uSamples), m_fDX11OcclusionVisibility);
	}
}

///////////////////////////////////////////////////////////////////////  
//	CLensFlare::ReadControlPixels

void CLensFlare::ReadControlPixels()
{
	if (m_bEnabled)
		ReadDepthPixels(m_pControlPixels);
}


///////////////////////////////////////////////////////////////////////  
//	CLensFlare::AdjustBrightness

void CLensFlare::AdjustBrightness()
{
	if (!m_bEnabled)
		return;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
		ConsumeDX11OcclusionSample(pDX11Device->GetContext());

	m_fAfterBright *= m_fDX11OcclusionVisibility;

	static DWORD s_dwLensActiveLogTick = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwLensActiveLogTick || (dwNow - s_dwLensActiveLogTick) >= 2000u)
	{
		s_dwLensActiveLogTick = dwNow;
		TraceError("DX11_LENSFLARE_ACTIVE enabled=%d visible=%d occlusion_visibility=%.3f",
			m_bEnabled ? 1 : 0,
			m_bFlareVisible ? 1 : 0,
			m_fDX11OcclusionVisibility);
	}
}


///////////////////////////////////////////////////////////////////////  
//	CLensFlare::ReadDepthPixels

void CLensFlare::ReadDepthPixels(float * /*pPixels*/)
{
	// Depth sampling for lens flare occlusion is intentionally disabled in DX11 path.
	// The old fixed-function depth-surface readback path has been removed.
}

///////////////////////////////////////////////////////////////////////  
//	CLensFlare::ClampBrightness

void CLensFlare::ClampBrightness()
{
	// before
    if (m_fBeforeBright < 0.0f)
        m_fBeforeBright = 0.0f;
    else if (m_fBeforeBright > 1.0f)
        m_fBeforeBright = 1.0f;

	m_fBeforeBright *= m_fMaxBrightness;

    if (m_fAfterBright < 0.0f)
        m_fAfterBright = 0.0f;
    else if (m_fAfterBright > 1.0f)
        m_fAfterBright = 1.0f;
	
	m_fAfterBright *= m_fMaxBrightness;
}

///////////////////////////////////////////////////////////////////////  
//	CFlare implementation
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////  
//	CFlare::CFlare

CFlare::CFlare()
{
}


///////////////////////////////////////////////////////////////////////  
//	CFlare::~CFlare

CFlare::~CFlare()
{
}


///////////////////////////////////////////////////////////////////////  
//	CFlare::Init

void CFlare::Init(std::string strPath)
{
	int i = 0;

	while (g_strFiles[i] != "")
	{
		CResource * pResource = CResourceManager::Instance().GetResourcePointer((strPath + "/" + string(g_strFiles[i])).c_str());
		
		if (!pResource->IsType(CGraphicImage::Type()))
			assert(false);

		SFlarePiece * pPiece = new SFlarePiece;

		pPiece->m_imageInstance.SetImagePointer(static_cast<CGraphicImage *> (pResource));
		pPiece->m_fPosition = g_fPosition[i];
		pPiece->m_fWidth = g_fWidth[i];
		pPiece->m_pColor = g_afColors[i];

		m_vFlares.push_back(pPiece);
		i++;
	}
}


///////////////////////////////////////////////////////////////////////  
//	CFlare::Draw
void CFlare::Draw(float fBrightScale, int nWidth, int nHeight, int nX, int nY)
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedDX11Unavailable = false;
		if (!s_bLoggedDX11Unavailable)
		{
			s_bLoggedDX11Unavailable = true;
			TraceError("DX11_SKYENV_DRAW_FAIL stage=lens phase=elements reason=dx11_device_unavailable");
		}
		return;
	}

	const float fDX = float(nX) - float(nWidth) / 2.0f;
	const float fDY = float(nY) - float(nHeight) / 2.0f;
	const UINT uExpected = static_cast<UINT>(m_vFlares.size());
	UINT uSubmitted = 0u;
	ID3D11ShaderResourceView* pWhiteSRV = CGraphicTextureDX11::GetWhiteFallbackTexture(pDX11Device->GetDevice());

	for (unsigned int i = 0; i < m_vFlares.size(); i++)
	{
		const float fCenterX = float(nX) - (m_vFlares[i]->m_fPosition + 1.0f) * fDX;
		const float fCenterY = float(nY) - (m_vFlares[i]->m_fPosition + 1.0f) * fDY;
		const float fW = m_vFlares[i]->m_fWidth;

		ID3D11ShaderResourceView* pSRV = nullptr;
		if (m_vFlares[i]->m_imageInstance.GetGraphicImagePointer())
			pSRV = m_vFlares[i]->m_imageInstance.GetGraphicImagePointer()->GetTextureReference().GetD3D11TextureSRV();

		if (!pSRV)
			pSRV = pWhiteSRV;

		const DWORD dwColor = PackColor(
			m_vFlares[i]->m_pColor[0] * fBrightScale,
			m_vFlares[i]->m_pColor[1] * fBrightScale,
			m_vFlares[i]->m_pColor[2] * fBrightScale,
			m_vFlares[i]->m_pColor[3] * fBrightScale);

		if (DrawFlareQuadDX11(pDX11Device, fCenterX, fCenterY, fW * 2.0f, dwColor, pSRV, true, false))
			++uSubmitted;
	}

	LogSkyenvLensParity(gs_dwLensElementsParityLogTick, "elements", uExpected, uSubmitted);
}

