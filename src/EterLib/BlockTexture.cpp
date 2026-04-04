#include "StdAfx.h"
#include "BlockTexture.h"
#include "GrpBase.h"
#include "GrpDib.h"
#include "Eterbase/Stl.h"
#include "GrpDeviceDX11.h"
#include "GrpImageTexture.h"
#include "EterBase/Timer.h"

void CBlockTexture::SetClipRect(const RECT & c_rRect)
{
	m_bClipEnable = TRUE;
	m_clipRect = c_rRect;
}

void CBlockTexture::Render(int ix, int iy)
{
	// M2-ETERLIB-STATE-50: Pure DX11 implementation - no DX9 fallback
	static bool s_bHadBlockTextureFail = false;
	// Bootstrap UI vertex structure (must match DX11 input layout)
	struct SBootstrapUIVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedBlockTextureSkip = false;
		if (!s_bLoggedBlockTextureSkip)
		{
			s_bLoggedBlockTextureSkip = true;
			TraceError("DX11_BLOCK_TEXTURE_SKIP reason=no_dx11_device_available");
		}
		s_bHadBlockTextureFail = true;
		return;
	}

	// M2-ETERLIB-STATE-50: Telemetry for block texture rendering (30s throttle)
	static DWORD s_dwLastBlockTextureLog = 0;
	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwLastBlockTextureLog > 30000)
	{
		s_dwLastBlockTextureLog = dwNow;
		TraceError("DX11_PIPELINE_STATE_PARITY pass=block_texture path=dx11_native");
	}

	int isx = ix + m_rect.left;
	int isy = iy + m_rect.top;
	int iex = ix + m_rect.left + m_dwWidth;
	int iey = iy + m_rect.top + m_dwHeight;

	float su = 0.0f;
	float sv = 0.0f;
	float eu = 1.0f;
	float ev = 1.0f;

	if (m_bClipEnable)
	{
		if (isx > m_clipRect.right)
			return;
		if (iex < m_clipRect.left)
			return;

		if (isy > m_clipRect.bottom)
			return;
		if (iey < m_clipRect.top)
			return;

		if (m_clipRect.left > isx)
		{
			int idx = m_clipRect.left - isx;
			isx += idx;
			su += float(idx) / float(m_dwWidth);
		}
		if (iex > m_clipRect.right)
		{
			int idx = iex - m_clipRect.right;
			iex -= idx;
			eu -= float(idx) / float(m_dwWidth);
		}

		if (m_clipRect.top > isy)
		{
			int idy = m_clipRect.top - isy;
			isy += idy;
			sv += float(idy) / float(m_dwHeight);
		}
		if (iey < m_clipRect.bottom)
		{
			int idy = iey - m_clipRect.bottom;
			iey -= idy;
			ev -= float(idy) / float(m_dwHeight);
		}
	}

	// M2-ETERLIB-STATE-50: Convert TPDTVertex to SPDTVertexRaw for DX11 rendering
	// Note: DX11 uses pixel coordinates directly, NDC conversion happens in DrawUIPrimitiveDX11
	SPDTVertexRaw vertices[4];
	vertices[0] = { (float)isx, (float)isy, 0.0f, 0xffffffff, su, sv };
	vertices[1] = { (float)iex, (float)isy, 0.0f, 0xffffffff, eu, sv };
	vertices[2] = { (float)isx, (float)iey, 0.0f, 0xffffffff, su, ev };
	vertices[3] = { (float)iex, (float)iey, 0.0f, 0xffffffff, eu, ev };

	// M2-ETERLIB-NATIVE-58: Use DX11 texture SRV for sampling
	// Migrated from ID3D11ShaderResourceView* to ID3D11ShaderResourceView
	ID3D11ShaderResourceView* pTextureSRV = m_pDX11TextureSRV;
	if (!pTextureSRV)
	{
		static bool s_bLoggedTextureNull = false;
		if (!s_bLoggedTextureNull)
		{
			s_bLoggedTextureNull = true;
			TraceError("DX11_BLOCK_TEXTURE_FAIL reason=texture_srv_null");
		}
		s_bHadBlockTextureFail = true;
		return;
	}

	// M2-ETERLIB-STATE-50: Call DX11 primitive rendering using inline implementation
	// (DrawUIPrimitiveDX11 pattern from GrpScreen.cpp)
	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pContext || !pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
	{
		static bool s_bLoggedBootstrapFail = false;
		if (!s_bLoggedBootstrapFail)
		{
			s_bLoggedBootstrapFail = true;
			TraceError("DX11_BLOCK_TEXTURE_FAIL reason=bootstrap_not_ready");
		}
		s_bHadBlockTextureFail = true;
		return;
	}

	// Update vertex buffer with transformed vertices
	UINT uBackBufferWidth = 0, uBackBufferHeight = 0;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (uBackBufferWidth == 0 || uBackBufferHeight == 0)
	{
		static bool s_bLoggedBackbufferFail = false;
		if (!s_bLoggedBackbufferFail)
		{
			s_bLoggedBackbufferFail = true;
			TraceError("DX11_BLOCK_TEXTURE_FAIL reason=backbuffer_size_invalid");
		}
		s_bHadBlockTextureFail = true;
		return;
	}

	// Convert pixel coordinates to NDC
	SBootstrapUIVertex ndcVertices[4];
	for (int i = 0; i < 4; ++i)
	{
		ndcVertices[i].x = (2.0f * vertices[i].px / (float)uBackBufferWidth) - 1.0f;
		ndcVertices[i].y = 1.0f - (2.0f * vertices[i].py / (float)uBackBufferHeight);
		ndcVertices[i].z = 0.0f;
		ndcVertices[i].r = ((vertices[i].diffuse >> 16) & 0xFF) / 255.0f;
		ndcVertices[i].g = ((vertices[i].diffuse >> 8) & 0xFF) / 255.0f;
		ndcVertices[i].b = (vertices[i].diffuse & 0xFF) / 255.0f;
		ndcVertices[i].a = ((vertices[i].diffuse >> 24) & 0xFF) / 255.0f;
		ndcVertices[i].u = vertices[i].u;
		ndcVertices[i].v = vertices[i].v;
	}

	// Set baseline UI state
	pDX11Device->SetUI2DBaselineState();

	// Update vertex buffer
	D3D11_MAPPED_SUBRESOURCE mappedRes;
	HRESULT hRes = pContext->Map(pDX11Device->GetBootstrapUIVertexBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedRes);
	if (FAILED(hRes))
	{
		static bool s_bLoggedMapFail = false;
		if (!s_bLoggedMapFail)
		{
			s_bLoggedMapFail = true;
			TraceError("DX11_BLOCK_TEXTURE_FAIL reason=vertex_buffer_map_failed");
		}
		s_bHadBlockTextureFail = true;
		return;
	}

	memcpy(mappedRes.pData, ndcVertices, sizeof(ndcVertices));
	pContext->Unmap(pDX11Device->GetBootstrapUIVertexBuffer(), 0);

	// Explicit DX11 pipeline setup (no inherited world/UI state assumptions)
	UINT uStride = sizeof(SBootstrapUIVertex);
	UINT uOffset = 0u;
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();

	if (!pVertexBuffer || !pInputLayout || !pVertexShader || !pPixelShader || !pSamplerState || !pAlphaBlendState || !pDepthDisableState)
	{
		static bool s_bLoggedPipelineResourceFail = false;
		if (!s_bLoggedPipelineResourceFail)
		{
			s_bLoggedPipelineResourceFail = true;
			TraceError("DX11_BLOCK_TEXTURE_FAIL reason=bootstrap_pipeline_resource_null");
		}
		s_bHadBlockTextureFail = true;
		return;
	}

	pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	pContext->VSSetShader(pVertexShader, nullptr, 0);
	pContext->PSSetShader(pPixelShader, nullptr, 0);
	pContext->PSSetShaderResources(0, 1, &pTextureSRV);
	pContext->PSSetSamplers(0, 1, &pSamplerState);
	pContext->OMSetBlendState(pAlphaBlendState, nullptr, 0xFFFFFFFFu);
	pContext->OMSetDepthStencilState(pDepthDisableState, 0u);
	if (ID3D11RasterizerState* pRaster = pDX11Device->GetBootstrapRasterizerState())
		pContext->RSSetState(pRaster);

	pContext->Draw(4, 0);

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

	if (s_bHadBlockTextureFail)
	{
		s_bHadBlockTextureFail = false;
		TraceError("DX11_BLOCK_TEXTURE_RECOVER path=dx11_native");
	}
}

void CBlockTexture::InvalidateRect(const RECT & c_rsrcRect)
{
	// M2-ETERLIB-NATIVE-58: Use DX11 Map/Unmap instead of DX9 LockRect/UnlockRect
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedInvalidateRectSkip = false;
		if (!s_bLoggedInvalidateRectSkip)
		{
			s_bLoggedInvalidateRectSkip = true;
			TraceError("DX11_BLOCK_TEXTURE_INVALIDATE_SKIP reason=no_dx11_device_available");
		}
		return;
	}

	if (!m_pDX11Texture)
	{
		static bool s_bLoggedInvalidateRectSkip = false;
		if (!s_bLoggedInvalidateRectSkip)
		{
			s_bLoggedInvalidateRectSkip = true;
			TraceError("DX11_BLOCK_TEXTURE_INVALIDATE_SKIP reason=no_dx11_texture_available");
		}
		return;
	}

	RECT dstRect = m_rect;
	if (c_rsrcRect.right < dstRect.left ||
		c_rsrcRect.left > dstRect.right ||
		c_rsrcRect.bottom < dstRect.top ||
		c_rsrcRect.top > dstRect.bottom)
	{
		Tracef("InvalidateRect() - Strange rect");
		return;
	}

	// DIBBAR_LONGSIZE_BUGFIX
	const RECT clipRect = {
		std::max(c_rsrcRect.left - dstRect.left, 0l),
		std::max(c_rsrcRect.top - dstRect.top, 0l),
		std::min(c_rsrcRect.right - dstRect.left, dstRect.right - dstRect.left),
		std::min(c_rsrcRect.bottom - dstRect.top, dstRect.bottom - dstRect.top),
	};
	// END_OF_DIBBAR_LONGSIZE_BUGFIX

	DWORD * pdwSrc;
	pdwSrc = (DWORD *)m_pDIB->GetPointer();
	pdwSrc += dstRect.left + dstRect.top*m_pDIB->GetWidth();

	// M2-ETERLIB-NATIVE-58: Map entire texture for update (DX11 doesn't support sub-rect mapping like DX9)
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT hr = pDX11Device->GetContext()->Map(m_pDX11Texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(hr))
	{
		Tracef("InvalidateRect() - Failed to Map DX11 texture");
		return;
	}

	int iclipWidth = clipRect.right - clipRect.left;
	int iclipHeight = clipRect.bottom - clipRect.top;
	DWORD* pdwDst = (DWORD*)mappedResource.pData;
	UINT uDstPitch = mappedResource.RowPitch / sizeof(DWORD);
	UINT uSrcWidth = m_pDIB->GetWidth();

	// Copy only the clipped region
	for (int y = 0; y < iclipHeight; ++y)
	{
		// Calculate source and destination row offsets
		DWORD* pdwDstRow = pdwDst + (clipRect.top + y) * uDstPitch + clipRect.left;
		DWORD* pdwSrcRow = pdwSrc + y * uSrcWidth;
		for (int x = 0; x < iclipWidth; ++x)
		{
			pdwDstRow[x] = pdwSrcRow[x];
		}
	}

	pDX11Device->GetContext()->Unmap(m_pDX11Texture, 0);
}

bool CBlockTexture::Create(CGraphicDib * pDIB, const RECT & c_rRect, DWORD dwWidth, DWORD dwHeight)
{
	// M2-ETERLIB-NATIVE-58: Create DX11 texture and SRV instead of DX9 texture
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedCreateSkip = false;
		if (!s_bLoggedCreateSkip)
		{
			s_bLoggedCreateSkip = true;
			TraceError("DX11_BLOCK_TEXTURE_CREATE_SKIP reason=no_dx11_device_available");
		}
		return false;
	}

	// M2-ETERLIB-NATIVE-58: Create DX11 texture with dynamic CPU access for texture updates
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = dwWidth;
	desc.Height = dwHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = pDX11Device->GetDevice()->CreateTexture2D(&desc, nullptr, &m_pDX11Texture);
	if (FAILED(hr) || !m_pDX11Texture)
	{
		Tracef("Failed to create DX11 block texture %u, %u\n", dwWidth, dwHeight);
		return false;
	}

	// M2-ETERLIB-NATIVE-58: Create shader resource view for sampling
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;

	hr = pDX11Device->GetDevice()->CreateShaderResourceView(m_pDX11Texture, &srvDesc, &m_pDX11TextureSRV);
	if (FAILED(hr) || !m_pDX11TextureSRV)
	{
		Tracef("Failed to create DX11 block texture SRV %u, %u\n", dwWidth, dwHeight);
		if (m_pDX11Texture)
		{
			m_pDX11Texture->Release();
			m_pDX11Texture = NULL;
		}
		return false;
	}

	m_pDIB = pDIB;
	m_rect = c_rRect;
	m_dwWidth = dwWidth;
	m_dwHeight = dwHeight;
	m_bClipEnable = FALSE;

	return true;
}

CBlockTexture::CBlockTexture()
{
	m_pDIB = NULL;
	m_pDX11Texture = NULL;
	m_pDX11TextureSRV = NULL;
}

CBlockTexture::~CBlockTexture()
{
	// M2-ETERLIB-NATIVE-58: Release DX11 texture resources
	if (m_pDX11TextureSRV)
	{
		m_pDX11TextureSRV->Release();
		m_pDX11TextureSRV = NULL;
	}
	if (m_pDX11Texture)
	{
		m_pDX11Texture->Release();
		m_pDX11Texture = NULL;
	}
}
