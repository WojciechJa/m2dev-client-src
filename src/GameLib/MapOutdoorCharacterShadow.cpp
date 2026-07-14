#include "StdAfx.h"
#include "EterLib/Camera.h"
#include "EterLib/GrpDeviceDX11.h"

#include "MapOutdoor.h"

static int recreate = false;

void CMapOutdoor::SetShadowTextureSize(WORD size)
{
	if (m_wShadowMapSize != size)
	{
		recreate = true;
		Tracenf("ShadowTextureSize changed %d -> %d", m_wShadowMapSize, size);
	}

	m_wShadowMapSize = size;
}

void CMapOutdoor::CreateCharacterShadowTexture()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		// Native DX11 world path manages character shadowing in the main frame pass.
		// No legacy shadow-texture resource is created in strict mode.
		return;
	}
}

void CMapOutdoor::ReleaseCharacterShadowTexture()
{
	// Native DX11 world path does not own legacy character shadow-texture resources.
	// Keep function as a safe lifecycle boundary for callers.
}

bool CMapOutdoor::BeginRenderCharacterShadowToTexture()
{
	// In strict DX11 mode the dedicated legacy shadow texture pass is bypassed.
	// Character shadow contribution is expected from native world rendering stages.
	return false;
}

void CMapOutdoor::EndRenderCharacterShadowToTexture()
{
	// Legacy shadow texture pass is bypassed in strict DX11 mode.
}
