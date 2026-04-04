#include "StdAfx.h"
#include "GrpShadowTexture.h"

namespace
{
static bool gs_bLoggedShadowTextureCreateSkip = false;
static bool gs_bLoggedShadowTextureSetSkip = false;
static bool gs_bLoggedShadowTextureBeginSkip = false;
static bool gs_bLoggedShadowTextureEndSkip = false;
}

void CGraphicShadowTexture::Destroy()
{
    CGraphicTexture::Destroy();
    Initialize();
}

bool CGraphicShadowTexture::Create(int width, int height)
{
    Destroy();

    m_width = width;
    m_height = height;

    if (!gs_bLoggedShadowTextureCreateSkip)
    {
        gs_bLoggedShadowTextureCreateSkip = true;
        TraceError("DX11_SHADOW_TEXTURE create_skipped reason=strict_dx11_manages_shadows");
        TraceError("DX11_PIPELINE_STATE_PARITY pass=shadow_texture_legacy path=csm_native reason=legacy_retired entry=Create csm_location=MapOutdoorRenderDX11.cpp:4453");
    }

    return false;
}

void CGraphicShadowTexture::Set(int stage) const
{
    if (!gs_bLoggedShadowTextureSetSkip)
    {
        gs_bLoggedShadowTextureSetSkip = true;
        TraceError("DX11_SHADOW_TEXTURE_GUARD set_skipped reason=legacy_shadow_texture_retired stage=%d", stage);
        TraceError("DX11_PIPELINE_STATE_PARITY pass=shadow_texture_legacy path=csm_native reason=legacy_retired entry=Set csm_location=MapOutdoorRenderDX11.cpp:4963");
    }
}

const D3DXMATRIX& CGraphicShadowTexture::GetLightVPMatrixReference() const
{
    return m_d3dLightVPMatrix;
}

ID3D11ShaderResourceView* CGraphicShadowTexture::GetD3DTexture() const
{
    return NULL;
}

void CGraphicShadowTexture::Begin()
{
    // Keep matrix update for any caller still sampling this value.
    D3DXMatrixMultiply(&m_d3dLightVPMatrix, &ms_matView, &ms_matProj);

    if (!gs_bLoggedShadowTextureBeginSkip)
    {
        gs_bLoggedShadowTextureBeginSkip = true;
        TraceError("DX11_SHADOW_TEXTURE begin_shadow_map_skipped reason=strict_dx11_manages_shadows");
        TraceError("DX11_PIPELINE_STATE_PARITY pass=shadow_texture_legacy path=csm_native reason=legacy_retired entry=Begin csm_location=MapOutdoorRenderDX11.cpp:4769");
    }
}

void CGraphicShadowTexture::End()
{
    if (!gs_bLoggedShadowTextureEndSkip)
    {
        gs_bLoggedShadowTextureEndSkip = true;
        TraceError("DX11_SHADOW_TEXTURE end_shadow_map_skipped reason=strict_dx11_manages_shadows");
        TraceError("DX11_PIPELINE_STATE_PARITY pass=shadow_texture_legacy path=csm_native reason=legacy_retired entry=End csm_location=MapOutdoorRenderDX11.cpp");
    }
}

void CGraphicShadowTexture::Initialize()
{
    CGraphicTexture::Initialize();

    D3DXMatrixIdentity(&m_d3dLightVPMatrix);
    ZeroMemory(&m_d3dOldViewport, sizeof(m_d3dOldViewport));

    m_lpd3dShadowTexture = NULL;
}

CGraphicShadowTexture::CGraphicShadowTexture()
{
    Initialize();
}

CGraphicShadowTexture::~CGraphicShadowTexture()
{
    Destroy();
}
