#include "StdAfx.h"
#include "EterBase/Stl.h"
#include "GrpTexture.h"
#include "GrpDeviceDX11.h"

void CGraphicTexture::DestroyDeviceObjects()
{
	safe_release(m_lpd3dTexture);
}

void CGraphicTexture::Destroy()
{
	DestroyDeviceObjects();

	Initialize();
}

void CGraphicTexture::Initialize()
{
	m_lpd3dTexture = NULL;
	m_width = 0;
	m_height = 0;
	m_bEmpty = true;
}

bool CGraphicTexture::IsEmpty() const
{
	return m_bEmpty;
}

void CGraphicTexture::SetTextureStage(int stage) const
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		pDX11Device->SetBootstrapTextureStageSRV(static_cast<UINT>(stage), GetD3D11TextureSRV());
		return;
	}

	static bool s_bLoggedTextureStageRedirectFail = false;
	if (!s_bLoggedTextureStageRedirectFail)
	{
		s_bLoggedTextureStageRedirectFail = true;
		TraceError("DX11_TEXTURE_STAGE_BLOCK path=grptexture_set_texture_stage reason=dx11_device_unavailable");
	}
	return;
}

ID3D11ShaderResourceView* CGraphicTexture::GetD3DTexture() const
{
	return m_lpd3dTexture;
}

int CGraphicTexture::GetWidth() const
{
	return m_width;
}

int CGraphicTexture::GetHeight() const
{
	return m_height;
}

CGraphicTexture::CGraphicTexture()
{
	Initialize();
}

CGraphicTexture::~CGraphicTexture()	
{
}
