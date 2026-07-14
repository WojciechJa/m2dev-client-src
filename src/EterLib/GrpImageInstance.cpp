#include "StdAfx.h"
#include "GrpImageInstance.h"
#include "GrpDeviceDX11.h"

#include "EterBase/CRC32.h"

// Shared UI widget counters used by image/expanded/mark instances.
struct SUIWidgetCounters
{
	DWORD dwImageSuccess = 0;
	DWORD dwExpandedSuccess = 0;
	DWORD dwMarkSuccess = 0;
	DWORD dwImageFail = 0;
	DWORD dwExpandedFail = 0;
	DWORD dwMarkFail = 0;
	DWORD dwLastHeartbeatTime = 0;
};

SUIWidgetCounters s_kUIWidgetCounters;

CDynamicPool<CGraphicImageInstance> CGraphicImageInstance::ms_kPool;

void CGraphicImageInstance::CreateSystem(UINT uCapacity)
{
	ms_kPool.Create(uCapacity);
}

void CGraphicImageInstance::DestroySystem()
{
	ms_kPool.Destroy();
}

CGraphicImageInstance* CGraphicImageInstance::New()
{
	return ms_kPool.Alloc();
}

void CGraphicImageInstance::Delete(CGraphicImageInstance* pkImgInst)
{
	pkImgInst->Destroy();
	ms_kPool.Free(pkImgInst);
}

void CGraphicImageInstance::Render()
{
	if (IsEmpty())
		return;

	assert(!IsEmpty());

	static bool s_bHadRenderFail = false;
	if (!OnRenderDX11())
	{
		static bool s_bLoggedRenderFail = false;
		if (!s_bLoggedRenderFail)
		{
			s_bLoggedRenderFail = true;
			TraceError("DX11_UI_IMAGE_FAIL reason=dx11_path_failed");
		}
		s_bHadRenderFail = true;
	}
	else if (s_bHadRenderFail)
	{
		s_bHadRenderFail = false;
		TraceError("DX11_UI_IMAGE_RECOVER reason=dx11_path_restored");
	}
}

void CGraphicImageInstance::OnRender()
{
	OnRenderDX11();
}

const CGraphicTexture& CGraphicImageInstance::GetTextureReference() const
{
	return m_roImage->GetTextureReference();
}

CGraphicTexture* CGraphicImageInstance::GetTexturePointer()
{
	CGraphicImage* pkImage = m_roImage.GetPointer();
	return pkImage ? pkImage->GetTexturePointer() : NULL;
}

CGraphicImage* CGraphicImageInstance::GetGraphicImagePointer()
{
	return m_roImage.GetPointer();
}

int CGraphicImageInstance::GetWidth()
{
	if (IsEmpty())
		return 0;

	return m_roImage->GetWidth();
}

int CGraphicImageInstance::GetHeight()
{
	if (IsEmpty())
		return 0;

	return m_roImage->GetHeight();
}

void CGraphicImageInstance::SetDiffuseColor(float fr, float fg, float fb, float fa)
{
	m_DiffuseColor.r = fr;
	m_DiffuseColor.g = fg;
	m_DiffuseColor.b = fb;
	m_DiffuseColor.a = fa;
}

void CGraphicImageInstance::SetPosition(float fx, float fy)
{
	m_v2Position.x = fx;
	m_v2Position.y = fy;
}

void CGraphicImageInstance::SetImagePointer(CGraphicImage* pImage)
{
	m_roImage.SetPointer(pImage);
	OnSetImagePointer();
}

void CGraphicImageInstance::ReloadImagePointer(CGraphicImage* pImage)
{
	if (m_roImage.IsNull())
	{
		SetImagePointer(pImage);
		return;
	}

	CGraphicImage* pkImage = m_roImage.GetPointer();
	if (pkImage)
		pkImage->Reload();
}

bool CGraphicImageInstance::IsEmpty() const
{
	return (m_roImage.IsNull() || m_roImage->IsEmpty());
}

bool CGraphicImageInstance::operator == (const CGraphicImageInstance& rhs) const
{
	return (m_roImage.GetPointer() == rhs.m_roImage.GetPointer());
}

DWORD CGraphicImageInstance::Type()
{
	static DWORD s_dwType = GetCRC32("CGraphicImageInstance", strlen("CGraphicImageInstance"));
	return s_dwType;
}

BOOL CGraphicImageInstance::IsType(DWORD dwType)
{
	return OnIsType(dwType);
}

BOOL CGraphicImageInstance::OnIsType(DWORD dwType)
{
	return (CGraphicImageInstance::Type() == dwType) ? TRUE : FALSE;
}

void CGraphicImageInstance::OnSetImagePointer()
{
}

void CGraphicImageInstance::Initialize()
{
	m_DiffuseColor.r = m_DiffuseColor.g = m_DiffuseColor.b = m_DiffuseColor.a = 1.0f;
	m_v2Position.x = m_v2Position.y = 0.0f;
}

void CGraphicImageInstance::Destroy()
{
	m_roImage.SetPointer(NULL);
	Initialize();
}

CGraphicImageInstance::CGraphicImageInstance()
{
	Initialize();
}

CGraphicImageInstance::~CGraphicImageInstance()
{
	Destroy();
}

bool CGraphicImageInstance::OnRenderDX11()
{
	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	CGraphicImage* pImage = m_roImage.GetPointer();
	if (!pImage)
		return false;

	CGraphicTexture* pTexture = pImage->GetTexturePointer();
	if (!pTexture)
		return false;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return false;

	if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
		return false;

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();
	if (!pContext || !pVertexBuffer || !pVertexShader || !pPixelShader || !pInputLayout || !pSamplerState || !pAlphaBlendState || !pDepthDisableState)
		return false;

	ID3D11ShaderResourceView* pTextureSRV = pTexture->GetD3D11TextureSRV();
	if (!pTextureSRV)
	{
		s_kUIWidgetCounters.dwImageFail++;
		return false;
	}

	UINT uBackBufferWidth = 0;
	UINT uBackBufferHeight = 0;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (0u == uBackBufferWidth || 0u == uBackBufferHeight)
	{
		s_kUIWidgetCounters.dwImageFail++;
		return false;
	}

	pDX11Device->BindMainRenderTargets();
	pDX11Device->SetUI2DBaselineState();

	const RECT& c_rRect = pImage->GetRectReference();
	const float texReverseWidth = 1.0f / static_cast<float>(pTexture->GetWidth());
	const float texReverseHeight = 1.0f / static_cast<float>(pTexture->GetHeight());
	const float su = c_rRect.left * texReverseWidth;
	const float sv = c_rRect.top * texReverseHeight;
	const float eu = c_rRect.right * texReverseWidth;
	const float ev = c_rRect.bottom * texReverseHeight;

	const float x0 = m_v2Position.x - 0.5f;
	const float y0 = m_v2Position.y - 0.5f;
	const float x1 = m_v2Position.x + static_cast<float>(pImage->GetWidth()) - 0.5f;
	const float y1 = m_v2Position.y + static_cast<float>(pImage->GetHeight()) - 0.5f;

	auto PixelToNDCX = [uBackBufferWidth](float fX) -> float
	{
		return (2.0f * fX / static_cast<float>(uBackBufferWidth)) - 1.0f;
	};
	auto PixelToNDCY = [uBackBufferHeight](float fY) -> float
	{
		return 1.0f - (2.0f * fY / static_cast<float>(uBackBufferHeight));
	};

	SBootstrapVertex akVertices[6];
	akVertices[0] = { PixelToNDCX(x0), PixelToNDCY(y0), 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, su, sv };
	akVertices[1] = { PixelToNDCX(x1), PixelToNDCY(y0), 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, eu, sv };
	akVertices[2] = { PixelToNDCX(x0), PixelToNDCY(y1), 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, su, ev };
	akVertices[3] = { PixelToNDCX(x1), PixelToNDCY(y0), 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, eu, sv };
	akVertices[4] = { PixelToNDCX(x1), PixelToNDCY(y1), 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, eu, ev };
	akVertices[5] = { PixelToNDCX(x0), PixelToNDCY(y1), 0.0f, m_DiffuseColor.r, m_DiffuseColor.g, m_DiffuseColor.b, m_DiffuseColor.a, su, ev };

	D3D11_MAPPED_SUBRESOURCE kMappedResource = {};
	const HRESULT hr = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hr))
	{
		s_kUIWidgetCounters.dwImageFail++;
		return false;
	}

	memcpy(kMappedResource.pData, akVertices, sizeof(akVertices));
	pContext->Unmap(pVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(pVertexShader, nullptr, 0);
	pContext->PSSetShader(pPixelShader, nullptr, 0);
	pContext->PSSetShaderResources(0, 1, &pTextureSRV);
	pContext->PSSetSamplers(0, 1, &pSamplerState);
	pContext->OMSetBlendState(pAlphaBlendState, nullptr, 0xFFFFFFFF);
	pContext->OMSetDepthStencilState(pDepthDisableState, 0);
	if (pDX11Device->GetBootstrapRasterizerState())
		pContext->RSSetState(pDX11Device->GetBootstrapRasterizerState());

	pContext->Draw(6, 0);

	ID3D11ShaderResourceView* apNullSRV[1] = { nullptr };
	ID3D11SamplerState* apNullSampler[1] = { nullptr };
	pContext->PSSetShaderResources(0, 1, apNullSRV);
	pContext->PSSetSamplers(0, 1, apNullSampler);
	pContext->VSSetShader(nullptr, nullptr, 0);
	pContext->PSSetShader(nullptr, nullptr, 0);
	pContext->IASetInputLayout(nullptr);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
	pContext->RSSetState(nullptr);
	const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	pContext->OMSetBlendState(nullptr, afBlendFactor, 0xFFFFFFFFu);
	pContext->OMSetDepthStencilState(nullptr, 0u);

	s_kUIWidgetCounters.dwImageSuccess++;
	return true;
}
