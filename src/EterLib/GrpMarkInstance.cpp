#include "StdAfx.h"
#include "GrpMarkInstance.h"
#include "ResourceManager.h"
#include "GrpDeviceDX11.h"

#include "EterBase/CRC32.h"
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

CDynamicPool<CGraphicMarkInstance> CGraphicMarkInstance::ms_kPool;

void CGraphicMarkInstance::SetImageFileName(const char* c_szFileName)
{
	m_stImageFileName = c_szFileName;
}

const std::string& CGraphicMarkInstance::GetImageFileName()
{
	return m_stImageFileName;
}

void CGraphicMarkInstance::CreateSystem(UINT uCapacity)
{
	ms_kPool.Create(uCapacity);
}

void CGraphicMarkInstance::DestroySystem()
{
	ms_kPool.Destroy();
}

CGraphicMarkInstance* CGraphicMarkInstance::New()
{
	return ms_kPool.Alloc();
}

void CGraphicMarkInstance::Delete(CGraphicMarkInstance* pkImgInst)
{
	pkImgInst->Destroy();
	ms_kPool.Free(pkImgInst);
}

void CGraphicMarkInstance::Render()
{
	if (IsEmpty())
		return;

	assert(!IsEmpty());

	OnRender();
}

void CGraphicMarkInstance::OnRender()
{
	if (!OnRenderDX11())
	{
		static bool s_bLoggedMarkFail = false;
		if (!s_bLoggedMarkFail)
		{
			s_bLoggedMarkFail = true;
			TraceError("DX11_UI_MARK_FAIL reason=dx11_path_failed");
		}
	}
}

bool CGraphicMarkInstance::OnRenderDX11()
{
	CGraphicImage * pImage = m_roImage.GetPointer();
	if (!pImage)
		return false;

	CGraphicTexture * pTexture = pImage->GetTexturePointer();
	if (!pTexture)
		return false;

	UINT uColCount = pImage->GetWidth() / MARK_WIDTH;
	if (uColCount == 0)
		return false;

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
			TraceError("DX11_UI_MARK_FAIL reason=bootstrap_resources_incomplete");
		}
		s_kUIWidgetCounters.dwMarkFail++;
		return false;
	}

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();

	if (!pContext || !pVertexBuffer || !pVertexShader || !pPixelShader || !pInputLayout || !pSamplerState || !pAlphaBlendState || !pDepthDisableState)
	{
		static bool s_bLoggedResourceFail = false;
		if (!s_bLoggedResourceFail)
		{
			s_bLoggedResourceFail = true;
			TraceError("DX11_UI_MARK_FAIL reason=bootstrap_resources_null");
		}
		s_kUIWidgetCounters.dwMarkFail++;
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
			TraceError("DX11_UI_MARK_FAIL reason=texture_srv_null");
		}
		s_kUIWidgetCounters.dwMarkFail++;
		return false;
	}

	// Get backbuffer dimensions for NDC conversion
	UINT uBackBufferWidth = 0, uBackBufferHeight = 0;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (uBackBufferWidth == 0 || uBackBufferHeight == 0)
		return false;

	// M2-UIWIDGET-24: Ensure UI draws to visible backbuffer (world/game passes can leave non-main RT bound)
	pDX11Device->BindMainRenderTargets();
	pDX11Device->SetUI2DBaselineState();

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
			TraceError("DX11_UI_MARK_FAIL reason=ui_input_range_reject px=%.3f py=%.3f max_abs=%.1f",
				m_v2Position.x, m_v2Position.y, fMaxAbsInputX);
		}
		s_kUIWidgetCounters.dwMarkFail++;
		return false;
	}

	// NDC conversion lambdas
	auto PixelToNDCX = [uBackBufferWidth](float fX) -> float {
		return (2.0f * fX / static_cast<float>(uBackBufferWidth)) - 1.0f;
	};
	auto PixelToNDCY = [uBackBufferHeight](float fY) -> float {
		return 1.0f - (2.0f * fY / static_cast<float>(uBackBufferHeight));
	};

	// SBootstrapVertex structure
	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	// Calculate sprite sheet UV coordinates
	UINT uCol = m_uIndex % uColCount;
	UINT uRow = m_uIndex / uColCount;

	RECT kRect;
	kRect.left = uCol * MARK_WIDTH;
	kRect.top = uRow * MARK_HEIGHT;
	kRect.right = kRect.left + MARK_WIDTH;
	kRect.bottom = kRect.top + MARK_HEIGHT;

	float texReverseWidth = 1.0f / float(pTexture->GetWidth());
	float texReverseHeight = 1.0f / float(pTexture->GetHeight());
	float su = kRect.left * texReverseWidth;
	float sv = kRect.top * texReverseHeight;
	float eu = kRect.right * texReverseWidth;
	float ev = kRect.bottom * texReverseHeight;

	float fRenderWidth = static_cast<float>(MARK_WIDTH) * m_fScale;
	float fRenderHeight = static_cast<float>(MARK_HEIGHT) * m_fScale;

	// Convert pixel positions to NDC
	float x0 = PixelToNDCX(m_v2Position.x - 0.5f);
	float x1 = PixelToNDCX(m_v2Position.x + fRenderWidth - 0.5f);
	float y0 = PixelToNDCY(m_v2Position.y - 0.5f);
	float y1 = PixelToNDCY(m_v2Position.y + fRenderHeight - 0.5f);

	// Build vertices (6 vertices for 2 triangles)
	SBootstrapVertex akVertices[6];

	// Triangle 1: (0, 1, 2)
	akVertices[0] = { x0, y0, 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, su, sv };
	akVertices[1] = { x1, y0, 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, eu, sv };
	akVertices[2] = { x0, y1, 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, su, ev };

	// Triangle 2: (2, 1, 3)
	akVertices[3] = { x0, y1, 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, su, ev };
	akVertices[4] = { x1, y0, 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, eu, sv };
	akVertices[5] = { x1, y1, 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, eu, ev };

	// Map vertex buffer
	D3D11_MAPPED_SUBRESOURCE kMappedResource = {};
	HRESULT hr = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hr))
	{
		static bool s_bLoggedMapFail = false;
		if (!s_bLoggedMapFail)
		{
			s_bLoggedMapFail = true;
			TraceError("DX11_UI_MARK_FAIL reason=vertex_buffer_map_failed");
		}
		s_kUIWidgetCounters.dwMarkFail++;
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
	pContext->PSSetSamplers(0, 1, &pSamplerState);
	pContext->PSSetShaderResources(0, 1, &pTextureSRV);
	pContext->OMSetDepthStencilState(pDepthDisableState, 0);

	// Set blend state for alpha blending
	float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	pContext->OMSetBlendState(pAlphaBlendState, blendFactor, 0xFFFFFFFF);

	// Draw
	pContext->Draw(6, 0);

	// M2-UIWIDGET-24: Increment aggregated success counter
	s_kUIWidgetCounters.dwMarkSuccess++;

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

const CGraphicTexture & CGraphicMarkInstance::GetTextureReference() const
{
	return m_roImage->GetTextureReference();
}

CGraphicTexture * CGraphicMarkInstance::GetTexturePointer()
{
	return m_roImage->GetTexturePointer();
}

CGraphicImage * CGraphicMarkInstance::GetGraphicImagePointer()
{
	return m_roImage.GetPointer();
}

void CGraphicMarkInstance::SetScale(float fScale)
{
	m_fScale=fScale;
}

void CGraphicMarkInstance::SetIndex(UINT uIndex)
{
	m_uIndex=uIndex;
}

int CGraphicMarkInstance::GetWidth()
{
	if (IsEmpty())
		return 0;
	
	//return m_roImage->GetWidth();
	return 16;
}

int CGraphicMarkInstance::GetHeight()
{
	if (IsEmpty())
		return 0;
	
	//return m_roImage->GetHeight();
	return 12;
}

void CGraphicMarkInstance::SetDiffuseColor(float fr, float fg, float fb, float fa)
{
	m_DiffuseColor.r = fr;
	m_DiffuseColor.g = fg;
	m_DiffuseColor.b = fb;
	m_DiffuseColor.a = fa;
}

void CGraphicMarkInstance::SetPosition(float fx, float fy)
{
	m_v2Position.x = fx;
	m_v2Position.y = fy;
}

void CGraphicMarkInstance::Load()
{
	if (GetImageFileName().empty())
		return;

	CResource * pResource = CResourceManager::Instance().GetResourcePointer(GetImageFileName().c_str());

	if (!pResource)
	{
		TraceError("CGraphicMarkinstance::Load - [%s] NOT EXIST", GetImageFileName().c_str());
		return;
	}

	if (pResource->IsType(CGraphicImage::Type()))
		SetImagePointer(static_cast<CGraphicImage*>(pResource));
}

void CGraphicMarkInstance::SetImagePointer(CGraphicImage * pImage)
{
	m_roImage.SetPointer(pImage);

	OnSetImagePointer();
}

bool CGraphicMarkInstance::IsEmpty() const
{
	if (!m_roImage.IsNull() && !m_roImage->IsEmpty())
		return false;

	return true;
}

bool CGraphicMarkInstance::operator == (const CGraphicMarkInstance & rhs) const
{
	return (m_roImage.GetPointer() == rhs.m_roImage.GetPointer());
}

DWORD CGraphicMarkInstance::Type()
{
	static DWORD s_dwType = GetCRC32("CGraphicMarkInstance", strlen("CGraphicMarkInstance"));
	return (s_dwType);
}

BOOL CGraphicMarkInstance::IsType(DWORD dwType)
{
	return OnIsType(dwType);
}

BOOL CGraphicMarkInstance::OnIsType(DWORD dwType)
{
	if (CGraphicMarkInstance::Type() == dwType)
		return TRUE;

	return FALSE;
}

void CGraphicMarkInstance::OnSetImagePointer()
{
}

void CGraphicMarkInstance::Initialize()
{
	m_DiffuseColor.r = m_DiffuseColor.g = m_DiffuseColor.b = m_DiffuseColor.a = 1.0f;
	m_v2Position.x = m_v2Position.y = 0.0f;
	m_uIndex = 0;
	m_fScale = 1.0f;
}

void CGraphicMarkInstance::Destroy()
{
	m_roImage.SetPointer(NULL); // CRef 에서 레퍼런스 카운트가 떨어져야 함.
	Initialize();
}

CGraphicMarkInstance::CGraphicMarkInstance()
{
	Initialize();
}

CGraphicMarkInstance::~CGraphicMarkInstance()
{
	Destroy();
}
