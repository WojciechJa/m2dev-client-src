#include "StdAfx.h"
#include "MapOutdoor.h"

#include <set>
#include <string>

namespace
{
void LogHTPLegacyDisabledOnce(const char* passName)
{
	static std::set<std::string> s_loggedPasses;
	if (s_loggedPasses.find(passName) != s_loggedPasses.end())
		return;

	s_loggedPasses.insert(passName);
	TraceError("DX11_PIPELINE_STATE_PARITY pass=%s path=dx11_native mode=legacy_htp_disabled", passName);
}
}

void CMapOutdoor::__RenderTerrain_RenderHardwareTransformPatch()
{
	LogHTPLegacyDisabledOnce("terrain_htp");
	TraceError("DX11_PIPELINE_SUBMIT_PARITY pass=terrain_htp expected=0 submitted=0 reason=handled_by_terrain_dx11");
}

void CMapOutdoor::__HardwareTransformPatch_RenderPatchSplat(long patchnum, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType)
{
	(void)patchnum;
	(void)wPrimitiveCount;
	(void)ePrimitiveType;
	LogHTPLegacyDisabledOnce("terrain_htp_patch_splat");
}

void CMapOutdoor::__HardwareTransformPatch_RenderPatchNone(long patchnum, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType)
{
	(void)patchnum;
	(void)wPrimitiveCount;
	(void)ePrimitiveType;
	LogHTPLegacyDisabledOnce("terrain_htp_patch_none");
}
