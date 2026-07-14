#include "StdAfx.h"
#include "EterLib/ResourceManager.h"
#include "EterLib/GrpDeviceDX11.h"

#include "MapOutdoor.h"

void CMapOutdoor::LoadWaterTexture()
{
	UnloadWaterTexture();
	char buf[256];
	for (int i = 0; i < 30; ++i)
	{
		sprintf(buf, "d:/ymir Work/special/water/%02d.dds", i + 1);
		m_WaterInstances[i].SetImagePointer((CGraphicImage*)CResourceManager::Instance().GetResourcePointer(buf));
	}
}

void CMapOutdoor::UnloadWaterTexture()
{
	for (int i = 0; i < 30; ++i)
		m_WaterInstances[i].Destroy();
}

void CMapOutdoor::RenderWater()
{
	if (m_PatchVector.empty() || !IsVisiblePart(PART_WATER))
		return;

	static bool s_bLoggedWaterLegacyDisabled = false;
	if (!s_bLoggedWaterLegacyDisabled)
	{
		s_bLoggedWaterLegacyDisabled = true;
		TraceError("DX11_WATER_LEGACY_PATH status=disabled reason=native_water_owned_by_MapOutdoorRenderDX11");
	}
}

void CMapOutdoor::DrawWater(long patchnum)
{
	(void)patchnum;
}
