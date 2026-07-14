#include "StdAfx.h"
#include "ScreenFilter.h"
#include "StateManager.h"
#include "GrpDeviceDX11.h"

// M3-ETERLIB-STATECORE-69: DX11-native screen filter implementation
// Replaces DX9 StateManager calls with semantic DX11 state API

void CScreenFilter::Render()
{
	if (!m_bEnable)
		return;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedNoDX11Device = false;
		if (!s_bLoggedNoDX11Device)
		{
			s_bLoggedNoDX11Device = true;
			TraceError("DX11_SCREEN_FILTER_RENDER filter_skipped reason=no_active_dx11_device");
		}
		return;
	}

	// Get StateManager11 instance (DX11-only, no DX9 fallback)
	CStateManager11* pStateManager = CStateManager11::InstancePtr();
	if (!pStateManager)
	{
		static bool s_bLoggedNoStateManager = false;
		if (!s_bLoggedNoStateManager)
		{
			s_bLoggedNoStateManager = true;
			TraceError("DX11_SCREEN_FILTER_RENDER filter_skipped reason=no_statemanager11");
		}
		return;
	}

	// Map blend constants to semantic DX11 blend modes.
	CStateManager11::EBlendMode blendMode = CStateManager11::EBlendMode::AlphaBlend; // Default: SRCALPHA, INVSRCALPHA

	// Map custom blend combinations to semantic modes
	if (m_bySrcType == GRP_BLEND_SRCALPHA && m_byDestType == GRP_BLEND_INVSRCALPHA)
	{
		blendMode = CStateManager11::EBlendMode::AlphaBlend;
	}
	else if (m_bySrcType == GRP_BLEND_ONE && m_byDestType == GRP_BLEND_ONE)
	{
		blendMode = CStateManager11::EBlendMode::Additive;
	}
	else if (m_bySrcType == GRP_BLEND_SRCALPHA && m_byDestType == GRP_BLEND_INVSRCCOLOR)
	{
		blendMode = CStateManager11::EBlendMode::Screen;
	}
	else if (m_bySrcType == GRP_BLEND_DESTCOLOR && m_byDestType == GRP_BLEND_ZERO)
	{
		blendMode = CStateManager11::EBlendMode::Modulate;
	}
	else if (m_bySrcType == GRP_BLEND_SRCALPHA && m_byDestType == GRP_BLEND_ONE)
	{
		blendMode = CStateManager11::EBlendMode::ColorDodge;
	}
	else
	{
		// Fallback: Use custom blend factors for unsupported combinations
		pStateManager->SetCustomBlendFactors(m_bySrcType, m_byDestType);

		static bool s_bLoggedBlendFallback = false;
		if (!s_bLoggedBlendFallback)
		{
			s_bLoggedBlendFallback = true;
			TraceError("DX11_SCREEN_FILTER_RENDER blend_fallback src=%u dest=%u using_custom_blend_factors",
				m_bySrcType, m_byDestType);
		}
	}

	// Apply semantic DX11 blend state
	pStateManager->SetBlendMode(blendMode);

	// Disable depth testing for 2D screen filter overlay.
	pStateManager->SetDepthMode(CStateManager11::EDepthMode::Disabled);

	// Set diffuse color and render full-screen quad
	SetDiffuseColor(m_Color.x, m_Color.y, m_Color.z, m_Color.w);
	RenderBar2d(0.0f, 0.0f, static_cast<float>(CScreen::ms_iWidth), static_cast<float>(CScreen::ms_iHeight), 0.0f);
}

void CScreenFilter::SetEnable(BOOL bFlag)
{
	m_bEnable = bFlag;
}

void CScreenFilter::SetBlendType(BYTE bySrcType, BYTE byDestType)
{
	m_bySrcType = bySrcType;
	m_byDestType = byDestType;
}
void CScreenFilter::SetColor(const DirectX::SimpleMath::Color& c_rColor)
{
	m_Color = c_rColor;
}

CScreenFilter::CScreenFilter()
{
	m_bEnable = FALSE;
	m_bySrcType = GRP_BLEND_SRCALPHA;
	m_byDestType = GRP_BLEND_INVSRCALPHA;
	m_Color = DirectX::SimpleMath::Color(0.0f, 0.0f, 0.0f, 0.0f);
}
CScreenFilter::~CScreenFilter()
{
}
