// PythonBackground.cpp: implementation of the CPythonBackground class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "EterLib/CullingManager.h"
#include "EterLib/Camera.h"
#include "EterLib/StateManager.h"
#include "EterLib/StateManager11.h"
#include "EterLib/GrpDeviceDX11.h"
#include "EterGrnLib/LODController.h"
#include "PackLib/PackManager.h"
#include "GameLib/MapOutDoor.h"
#include "GameLib/Area.h"
#include "GameLib/PropertyLoader.h"

#include "PythonBackground.h"
#include "PythonCharacterManager.h"
#include "PythonNetworkStream.h"
#include "PythonMiniMap.h"
#include "PythonSystem.h"
#include "config.h"

std::string g_strEffectName = "d:/ymir work/effect/etc/direction/direction_land.mse";
namespace
{
constexpr float kDX11ViewDistanceFarClipFixed = DX11RuntimeConfig::kViewDistanceFarClipMax;
constexpr float kDX11ViewDistanceMinFarClip = DX11RuntimeConfig::kViewDistanceFarClipMin;
}

DWORD CPythonBackground::GetRenderShadowTime()
{
	return m_dwRenderShadowTime;
}

bool CPythonBackground::SetVisiblePart(int eMapOutDoorPart, bool isVisible)
{
	if (!m_pkMap)
		return false;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.SetVisiblePart(eMapOutDoorPart, isVisible);
	return true;
}

void CPythonBackground::EnableTerrainOnlyForHeight()
{
	if (!m_pkMap)
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.EnableTerrainOnlyForHeight(TRUE);
}

bool CPythonBackground::SetSplatLimit(int iSplatNum)
{
	if (!m_pkMap)
		return false;

	CMapOutdoor& rkMap = GetMapOutdoorRef();
	rkMap.SetSplatLimit(iSplatNum);
	return true;
}

void CPythonBackground::CreateCharacterShadowTexture()
{
	if (!m_pkMap)
		return;

	CMapOutdoor& rkMap = GetMapOutdoorRef();
	rkMap.CreateCharacterShadowTexture();
}

void CPythonBackground::ReleaseCharacterShadowTexture()
{
	if (!m_pkMap)
		return;

	CMapOutdoor& rkMap = GetMapOutdoorRef();
	rkMap.ReleaseCharacterShadowTexture();
}

void CPythonBackground::RefreshShadowLevel()
{
	SetShadowLevel(CPythonSystem::Instance().GetShadowLevel());
}

void CPythonBackground::SetDrawShadow(bool bEnable)
{
	if (!m_pkMap)
		return;

	CMapOutdoor& rkMap = GetMapOutdoorRef();
	rkMap.SetDrawShadow(bEnable);
}

void CPythonBackground::SetDrawCharacterShadow(bool bEnable)
{
	if (!m_pkMap)
		return;

	CMapOutdoor& rkMap = GetMapOutdoorRef();
	rkMap.SetDrawCharacterShadow(bEnable);
}

bool CPythonBackground::IsSoftwareTilingEnable() const
{
	return m_bSoftwareTilingReserved;
}

void CPythonBackground::ReserveSoftwareTilingEnable(bool isEnable)
{
	if (m_bSoftwareTilingReserved == isEnable)
		return;

	m_bSoftwareTilingReserved = isEnable;
	TraceError("DX11_SOFTWARE_TILING_COMPAT reserved=%d", isEnable ? 1 : 0);
}

bool CPythonBackground::SetShadowLevel(int eLevel)
{
	if (!m_pkMap)
		return false;

	if (m_eShadowLevel == eLevel)
		return true;

	CMapOutdoor& rkMap = GetMapOutdoorRef();

	m_eShadowLevel = eLevel;

	switch (m_eShadowLevel)
	{
		case SHADOW_NONE:
			rkMap.SetDrawShadow(false);
			rkMap.SetShadowTextureSize(512);
			break;

		case SHADOW_GROUND:
			rkMap.SetDrawShadow(true);
			rkMap.SetDrawCharacterShadow(false);
			rkMap.SetShadowTextureSize(512);
			break;

		case SHADOW_GROUND_AND_SOLO:
			rkMap.SetDrawShadow(true);
			rkMap.SetDrawCharacterShadow(true);
			rkMap.SetShadowTextureSize(512);
			break;

		case SHADOW_ALL:
			rkMap.SetDrawShadow(true);	
			rkMap.SetDrawCharacterShadow(true);
			rkMap.SetShadowTextureSize(512);
			break;

		case SHADOW_ALL_HIGH:
			rkMap.SetDrawShadow(true);	
			rkMap.SetDrawCharacterShadow(true);
			rkMap.SetShadowTextureSize(1024);
			break;

		case SHADOW_ALL_MAX:
			rkMap.SetDrawShadow(true);	
			rkMap.SetDrawCharacterShadow(true);
			rkMap.SetShadowTextureSize(2048);
			break;
	}

	return true;
}

void CPythonBackground::SelectViewDistanceNum(int eNum)
{
    if (!m_pkMap)
        return;

    CMapOutdoor& rkMap = GetMapOutdoorRef();

    if (!mc_pcurEnvironmentData)
    {
        TraceError("CPythonBackground::SelectViewDistanceNum(int eNum=%d) mc_pcurEnvironmentData is NULL", eNum);
        return;
    }

    TraceError("M3_SELECT_VIEW_DIST num=%d bApplied=%d lastIndex=%d",
        eNum, m_bPresetApplied, m_iLastPresetIndex);

    m_eViewDistanceNum = eNum;
    TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);

    TraceError("M3_SELECT_VIEW_DIST_BEFORE fog_near=%.1f fog_far=%.1f sky_scale=%.1f",
        env->m_fFogNearDistance, env->m_fFogFarDistance, env->v3SkyBoxScale.x);

    const bool bDX11Active = (CGraphicDeviceDX11::GetActiveDevice() != nullptr);
    if (env->bReserve || bDX11Active)
    {
        // M3-SKY-PRESET-PERSIST-74: Don't overwrite fog distances if preset is active
        // Preset values are managed by ApplyEnvironmentPreset, not ViewDistanceSet
        if (!m_bPresetApplied)
        {
            env->m_fFogNearDistance = m_ViewDistanceSet[m_eViewDistanceNum].m_fFogStart;
            env->m_fFogFarDistance = m_ViewDistanceSet[m_eViewDistanceNum].m_fFogEnd;
        }

        env->v3SkyBoxScale = m_ViewDistanceSet[m_eViewDistanceNum].m_v3SkyBoxScale;
        rkMap.ApplyEnvironmentDistanceOnly();

        TraceError("M3_SELECT_VIEW_DIST_AFTER fog_near=%.1f fog_far=%.1f sky_scale=%.1f preset_active=%d from_ViewDistSet_start=%.1f end=%.1f",
            env->m_fFogNearDistance, env->m_fFogFarDistance, env->v3SkyBoxScale.x,
            m_bPresetApplied ? 1 : 0,
            m_ViewDistanceSet[m_eViewDistanceNum].m_fFogStart,
            m_ViewDistanceSet[m_eViewDistanceNum].m_fFogEnd);
    }

    // M3-ENV-DEFAULT-DAY: Apply Day preset once on first initialization
    static bool s_bDefaultPresetApplied = false;
    if (!s_bDefaultPresetApplied)
    {
        s_bDefaultPresetApplied = true;
        ApplyEnvironmentPreset(0); // 0 = Day preset
    }
}

void CPythonBackground::SetViewDistanceSet(int eNum, float fFarClip)
{
    if (!m_pkMap)
        return;

    const float fClampedFarClip = fMAX(kDX11ViewDistanceMinFarClip, fMIN(kDX11ViewDistanceFarClipFixed, fFarClip));
    const float fFogStart = fClampedFarClip * DX11RuntimeConfig::kViewDistanceFogStartRatio;
    const float fFogEnd = fClampedFarClip * DX11RuntimeConfig::kViewDistanceFogEndRatio;
    // M3-SKY-SCALE-DEFAULT: Use default sky scale from config.h instead of calculating from far clip
    const float fSkyBoxScale = DX11RuntimeConfig::kEnvironmentSkyScaleDefault;

    const float fPrevFarClip = m_ViewDistanceSet[eNum].m_fFarClip;
    const float fPrevFogStart = m_ViewDistanceSet[eNum].m_fFogStart;
    const float fPrevFogEnd = m_ViewDistanceSet[eNum].m_fFogEnd;
    const DirectX::SimpleMath::Vector3 v3PrevSkyScale = m_ViewDistanceSet[eNum].m_v3SkyBoxScale;

    CGrannyLODController::SetGlobalLODDistanceFromFarClip(fClampedFarClip);
    m_ViewDistanceSet[eNum].m_fFogStart = fFogStart;
    m_ViewDistanceSet[eNum].m_fFogEnd = fFogEnd;
    m_ViewDistanceSet[eNum].m_v3SkyBoxScale = DirectX::SimpleMath::Vector3(fSkyBoxScale, fSkyBoxScale, fSkyBoxScale);
    m_ViewDistanceSet[eNum].m_fFarClip = fClampedFarClip;

    auto AbsDiff = [](float a, float b) -> float
    {
        const float d = a - b;
        return (d >= 0.0f) ? d : -d;
    };

    const bool bDistanceChanged =
        (AbsDiff(fPrevFarClip, fClampedFarClip) > 0.01f) ||
        (AbsDiff(fPrevFogStart, fFogStart) > 0.01f) ||
        (AbsDiff(fPrevFogEnd, fFogEnd) > 0.01f) ||
        (AbsDiff(v3PrevSkyScale.x, fSkyBoxScale) > 0.01f) ||
        (AbsDiff(v3PrevSkyScale.y, fSkyBoxScale) > 0.01f) ||
        (AbsDiff(v3PrevSkyScale.z, fSkyBoxScale) > 0.01f);

    static DWORD s_dwViewDistanceLogTick = 0u;
    const DWORD dwNow = ELTimer_GetMSec();
    if (0u == s_dwViewDistanceLogTick || (dwNow - s_dwViewDistanceLogTick) >= 2000u)
    {
        s_dwViewDistanceLogTick = dwNow;
        TraceError("DX11_VIEWDIST_CONFIG far_clip=%.1f fog_start=%.1f fog_end=%.1f sky_scale=%.1f requested_far=%.1f changed=%u",
            m_ViewDistanceSet[eNum].m_fFarClip,
            m_ViewDistanceSet[eNum].m_fFogStart,
            m_ViewDistanceSet[eNum].m_fFogEnd,
            fSkyBoxScale,
            fFarClip,
            bDistanceChanged ? 1u : 0u);
    }

    if (eNum == m_eViewDistanceNum && bDistanceChanged)
        SelectViewDistanceNum(eNum);
}

float CPythonBackground::GetFarClip()
{
	if (!m_pkMap)
		return 50000.0f;

	if (m_ViewDistanceSet[m_eViewDistanceNum].m_fFarClip==0.0f)
	{
		TraceError("CPythonBackground::GetFarClip m_eViewDistanceNum=%d", m_eViewDistanceNum);
		m_ViewDistanceSet[m_eViewDistanceNum].m_fFarClip=kDX11ViewDistanceFarClipFixed;
	}

	return m_ViewDistanceSet[m_eViewDistanceNum].m_fFarClip;
}

void CPythonBackground::GetDistanceSetInfo(int * peNum, float * pfStart, float * pfEnd, float * pfFarClip)
{
	if (!m_pkMap)
	{
		*peNum = 4;
		*pfStart= 10000.0f;
		*pfEnd= 15000.0f;
		*pfFarClip = kDX11ViewDistanceFarClipFixed;
		return;
	}
	*peNum = m_eViewDistanceNum;
	*pfStart = m_ViewDistanceSet[m_eViewDistanceNum].m_fFogStart;
	*pfEnd= m_ViewDistanceSet[m_eViewDistanceNum].m_fFogEnd;
	*pfFarClip = m_ViewDistanceSet[m_eViewDistanceNum].m_fFarClip;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CPythonBackground::CPythonBackground()
{
	m_dwRenderShadowTime=0;
	m_eViewDistanceNum=0;
	m_eViewDistanceNum=0;
	m_eViewDistanceNum=0;
	m_eShadowLevel=SHADOW_NONE;
	m_dwBaseX=0;
	m_dwBaseY=0;
	m_strMapName="";
	m_iDayMode = DAY_MODE_LIGHT;
	m_bSnowEnvironmentEnabled = false;
	m_bRainEnvironmentEnabled = false;
	m_bStormEnvironmentEnabled = false;
	m_iWeatherMonth = 1;
	m_fRainIntensity = 0.0f;
	m_iXMasTreeGrade = 0;
	// M3-SKY-PRESET-PERSIST-74: Initialize preset tracking
	m_bPresetApplied = false;
	m_iLastPresetIndex = -1;
	m_bVisibleGuildArea = FALSE;
	m_bSoftwareTilingReserved = false;

	SetViewDistanceSet(4, kDX11ViewDistanceFarClipFixed);
	SetViewDistanceSet(3, kDX11ViewDistanceFarClipFixed);
	SetViewDistanceSet(2, kDX11ViewDistanceFarClipFixed);
	SetViewDistanceSet(1, kDX11ViewDistanceFarClipFixed);
	SetViewDistanceSet(0, kDX11ViewDistanceFarClipFixed);
	Initialize();
}

CPythonBackground::~CPythonBackground()
{
	Tracen("CPythonBackground Clear");
}

void CPythonBackground::Initialize()
{
	std::string stAtlasInfoFileName(GetLocalePath());
	stAtlasInfoFileName += "/AtlasInfo.txt";
	SetAtlasInfoFileName(stAtlasInfoFileName.c_str());
	m_kDX11WorldSubmitTelemetry = SDX11WorldSubmitTelemetry();
	CMapManager::Initialize();
}

void CPythonBackground::__CreateProperty()
{
	m_PropertyManager.Initialize("pack/property.pck");
}

//////////////////////////////////////////////////////////////////////
// Normal Functions
//////////////////////////////////////////////////////////////////////

bool CPythonBackground::GetPickingPoint(DirectX::SimpleMath::Vector3 * v3IntersectPt)
{
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetPickingPoint(v3IntersectPt);
}

bool CPythonBackground::GetPickingPointWithRay(const CRay & rRay, DirectX::SimpleMath::Vector3 * v3IntersectPt)
{
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetPickingPointWithRay(rRay, v3IntersectPt);
}

bool CPythonBackground::GetPickingPointWithRayOnlyTerrain(const CRay & rRay, DirectX::SimpleMath::Vector3 * v3IntersectPt)
{
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetPickingPointWithRayOnlyTerrain(rRay, v3IntersectPt);
}

BOOL CPythonBackground::GetLightDirection(DirectX::SimpleMath::Vector3 & rv3LightDirection)
{
	if (!mc_pcurEnvironmentData)
		return FALSE;

	rv3LightDirection.x = mc_pcurEnvironmentData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction.x;
	rv3LightDirection.y = mc_pcurEnvironmentData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction.y;
	rv3LightDirection.z = mc_pcurEnvironmentData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction.z;
	return TRUE;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
void CPythonBackground::Destroy()
{
	CMapManager::Destroy();
	m_StormEnvironment.Destroy();
	m_SnowEnvironment.Destroy();
	m_RainEnvironment.Destroy();
	m_bVisibleGuildArea = FALSE;
	m_kDX11WorldSubmitTelemetry = SDX11WorldSubmitTelemetry();
}

void CPythonBackground::Create()
{
	static int s_isCreateProperty=false;

	if (!s_isCreateProperty)
	{
		s_isCreateProperty=true;
		__CreateProperty();
	}

	CMapManager::Create();

	m_SnowEnvironment.Create();
	m_RainEnvironment.Create();
	m_StormEnvironment.Create();
}

struct FGetPortalID
{
	float m_fRequestX, m_fRequestY;
	std::set<int> m_kSet_iPortalID;
	FGetPortalID(float fRequestX, float fRequestY)
	{
		m_fRequestX=fRequestX;
		m_fRequestY=fRequestY;
	}
	void operator () (CGraphicObjectInstance * pObject)
	{
		for (int i = 0; i < PORTAL_ID_MAX_NUM; ++i)
		{
			int iID = pObject->GetPortal(i);
			if (0 == iID)
				break;

			m_kSet_iPortalID.insert(iID);
		}
	}
};

void CPythonBackground::Update(float fCenterX, float fCenterY, float fCenterZ)
{
	if (!IsMapReady())
		return;
#ifdef __PERFORMANCE_CHECKER__
	DWORD t1=ELTimer_GetMSec();
#endif
	UpdateMap(fCenterX, fCenterY, fCenterZ);
#ifdef __PERFORMANCE_CHECKER__
	DWORD t2=ELTimer_GetMSec();
#endif
	UpdateAroundAmbience(fCenterX, fCenterY, fCenterZ);
#ifdef __PERFORMANCE_CHECKER__
	DWORD t3=ELTimer_GetMSec();
#endif
	__SyncDX11EnvironmentBridgeState();
	m_SnowEnvironment.Update(DirectX::SimpleMath::Vector3(fCenterX, -fCenterY, fCenterZ));
	m_RainEnvironment.Update(DirectX::SimpleMath::Vector3(fCenterX, -fCenterY, fCenterZ));

	// Calculate elapsed time for storm (in seconds)
	static long s_lLastStormTime = CTimer::Instance().GetCurrentMillisecond();
	long lcurStormTime = CTimer::Instance().GetCurrentMillisecond();
	float fStormElapsedTime = float(lcurStormTime - s_lLastStormTime) / 1000.0f;
	s_lLastStormTime = lcurStormTime;
	m_StormEnvironment.Update(fStormElapsedTime);

#ifdef __PERFORMANCE_CHECKER__
	{
		static FILE* fp=fopen("perf_bg_update.txt", "w");
		if (t3-t1>5)
		{
			fprintf(fp, "BG.Total %d (Time %f)\n", t3-t1, ELTimer_GetMSec()/1000.0f);
			fprintf(fp, "BG.UpdateMap %d\n", t2-t1);
			fprintf(fp, "BG.UpdateAmb %d\n", t3-t2);
			fflush(fp);
		}
	}
#endif

	// Portal Process
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	if (rkMap.IsEnablePortal())
	{
		CCullingManager & rkCullingMgr = CCullingManager::Instance();
		FGetPortalID kGetPortalID(fCenterX, -fCenterY);

		Vector3d aVector3d;
		aVector3d.Set(fCenterX, -fCenterY, fCenterZ);

		Vector3d toTop;
		toTop.Set(0, 0, 25000.0f);

		rkCullingMgr.ForInRay(aVector3d, toTop, &kGetPortalID);

		std::set<int>::iterator itor = kGetPortalID.m_kSet_iPortalID.begin();
		if (!__IsSame(kGetPortalID.m_kSet_iPortalID, m_kSet_iShowingPortalID))
		{
			ClearPortal();
			std::set<int>::iterator itor=kGetPortalID.m_kSet_iPortalID.begin();
			for (; itor!=kGetPortalID.m_kSet_iPortalID.end(); ++itor)
			{
				AddShowingPortalID(*itor);
			}
			RefreshPortal();

			m_kSet_iShowingPortalID = kGetPortalID.m_kSet_iPortalID;
		}
	}

	// Target Effect Process
	{
		std::map<DWORD, DWORD>::iterator itor = m_kMap_dwTargetID_dwChrID.begin();
		for (; itor != m_kMap_dwTargetID_dwChrID.end(); ++itor)
		{
			DWORD dwTargetID = itor->first;
			DWORD dwChrID = itor->second;

			CInstanceBase * pInstance = CPythonCharacterManager::Instance().GetInstancePtr(dwChrID);

			if (!pInstance)
				continue;

			TPixelPosition kPixelPosition;
			pInstance->NEW_GetPixelPosition(&kPixelPosition);

			CreateSpecialEffect(dwTargetID,
								+kPixelPosition.x,
								-kPixelPosition.y,
								+kPixelPosition.z,
								g_strEffectName.c_str());
		}
	}

	// Reserve Target Effect
	{
		std::map<DWORD, SReserveTargetEffect>::iterator itor = m_kMap_dwID_kReserveTargetEffect.begin();
		for (; itor != m_kMap_dwID_kReserveTargetEffect.end();)
		{
			DWORD dwID = itor->first;
			SReserveTargetEffect & rReserveTargetEffect = itor->second;

			float ilx = float(rReserveTargetEffect.ilx);
			float ily = float(rReserveTargetEffect.ily);

			float fHeight = rkMap.GetHeight(ilx, ily);
			if (0.0f == fHeight)
			{
				++itor;
				continue;
			}

			CreateSpecialEffect(dwID, ilx, ily, fHeight, g_strEffectName.c_str());

			itor = m_kMap_dwID_kReserveTargetEffect.erase(itor);
		}
	}
}

bool CPythonBackground::__IsSame(std::set<int> & rleft, std::set<int> & rright)
{
	std::set<int>::iterator itor_l;
	std::set<int>::iterator itor_r;

	for (itor_l=rleft.begin(); itor_l!=rleft.end(); ++itor_l)
	{
		if (rright.end() == rright.find(*itor_l))
			return false;
	}

	for (itor_r=rright.begin(); itor_r!=rright.end(); ++itor_r)
	{
		if (rleft.end() == rleft.find(*itor_r))
			return false;
	}

	return true;
}

void CPythonBackground::Render()
{
	if (!IsMapReady())
		return;

	m_SnowEnvironment.Deform();
	m_RainEnvironment.Deform();

	CMapOutdoor& rkMap=GetMapOutdoorRef();

	// DX11 native world path owns terrain/object/water rendering. Running legacy rkMap.Render()
	// here replays a second world pass and can overwrite native water output.
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsUsingNativeWorldPresentPath())
	{
		if (m_bVisibleGuildArea)
			rkMap.RenderMarkedArea();

		static bool s_bLoggedLegacyRenderBypass = false;
		if (!s_bLoggedLegacyRenderBypass)
		{
			s_bLoggedLegacyRenderBypass = true;
			TraceError("DX11_BG_RENDER_BYPASS mode=native_world_present reason=avoid_legacy_world_overdraw");
		}
		return;
	}

	rkMap.Render();
	if (m_bVisibleGuildArea)
		rkMap.RenderMarkedArea();
}

bool CPythonBackground::RenderTerrainDX11(
	ID3D11Device* pDevice,
	ID3D11DeviceContext* pContext,
	uint32_t* pdwOutWorldPortMaskObserved,
	uint32_t* pdwOutWorldPortMaskSubmitted,
	uint32_t* pdwOutWorldPortMaskApplicable)
{
	uint32_t dwWorldPortMaskObserved = CGraphicDeviceDX11::WORLD_TERRAIN_DX11;
	uint32_t dwWorldPortMaskSubmitted = 0u;
	uint32_t dwWorldPortMaskApplicable = CGraphicDeviceDX11::WORLD_TERRAIN_DX11;
	auto PublishSubmitTelemetry = [&](uint32_t dwObservedMask, uint32_t dwSubmittedMask, uint32_t dwApplicableMask)
	{
		m_kDX11WorldSubmitTelemetry = SDX11WorldSubmitTelemetry();
		m_kDX11WorldSubmitTelemetry.dwObservedMask = dwObservedMask;
		m_kDX11WorldSubmitTelemetry.dwSubmittedMask = dwSubmittedMask;
		m_kDX11WorldSubmitTelemetry.dwApplicableMask = dwApplicableMask;
		m_kDX11WorldSubmitTelemetry.dwCommittedMask = 0u;
		if (pdwOutWorldPortMaskObserved)
			*pdwOutWorldPortMaskObserved = dwObservedMask;
		if (pdwOutWorldPortMaskSubmitted)
			*pdwOutWorldPortMaskSubmitted = dwSubmittedMask;
		if (pdwOutWorldPortMaskApplicable)
			*pdwOutWorldPortMaskApplicable = dwApplicableMask;
	};
	if (!IsMapReady())
	{
		PublishSubmitTelemetry(dwWorldPortMaskObserved, dwWorldPortMaskSubmitted, 0u);
		return true;
	}

	if (!pDevice || !pContext)
	{
		PublishSubmitTelemetry(dwWorldPortMaskObserved, dwWorldPortMaskSubmitted, 0u);
		return false;
	}

	CMapOutdoor& rkMap = GetMapOutdoorRef();
	if (!rkMap.InitializeDX11TerrainResources(pDevice))
	{
		PublishSubmitTelemetry(dwWorldPortMaskObserved, dwWorldPortMaskSubmitted, 0u);
		return false;
	}

	// Water path is optional for runtime stability; init failures should not block terrain.
	static DWORD s_dwDX11WaterInitRetryAfterMS = 0;
	static DWORD s_dwDX11WaterInitFailLogTick = 0;
	static DWORD s_dwDX11ShadowInitRetryAfterMS = 0;
	static DWORD s_dwDX11ShadowInitFailLogTick = 0;
	const DWORD dwNow = ELTimer_GetMSec();
	if (!rkMap.IsDX11WaterReady() && dwNow >= s_dwDX11WaterInitRetryAfterMS)
	{
		if (!rkMap.InitializeDX11WaterResources(pDevice))
		{
			s_dwDX11WaterInitRetryAfterMS = dwNow + 2000u;
			if (0 == s_dwDX11WaterInitFailLogTick || dwNow - s_dwDX11WaterInitFailLogTick >= 30000u)
			{
				s_dwDX11WaterInitFailLogTick = dwNow;
				TraceError("DX11_WATER_RUNTIME state=init_failed retry_after_ms=2000");
			}
		}
		else
		{
			s_dwDX11WaterInitRetryAfterMS = 0;
			TraceError("DX11_WATER_RUNTIME state=ready");
		}
	}

	// Dynamic shadows are optional for frame safety, but must use real DX11 resources when enabled.
	if (!rkMap.IsDX11DynamicShadowsReady() && dwNow >= s_dwDX11ShadowInitRetryAfterMS)
	{
		if (!rkMap.InitializeDX11ShadowResources(pDevice))
		{
			s_dwDX11ShadowInitRetryAfterMS = dwNow + 2000u;
			if (0 == s_dwDX11ShadowInitFailLogTick || dwNow - s_dwDX11ShadowInitFailLogTick >= 30000u)
			{
				s_dwDX11ShadowInitFailLogTick = dwNow;
				TraceError("DX11_DYNAMIC_SHADOW_RUNTIME state=init_failed retry_after_ms=2000");
			}
		}
		else
		{
			s_dwDX11ShadowInitRetryAfterMS = 0u;
			TraceError("DX11_DYNAMIC_SHADOW_RUNTIME state=ready");
		}
	}

	// Build DX11 terrain VBs once per loaded map (or until first success).
	static CMapOutdoor* s_pLastMapOutdoor = NULL;
	static bool s_bDX11TerrainVBReadyLatched = false;
	if (s_pLastMapOutdoor != &rkMap)
	{
		s_pLastMapOutdoor = &rkMap;
		s_bDX11TerrainVBReadyLatched = false;
	}
	if (!s_bDX11TerrainVBReadyLatched)
	{
		if (rkMap.BuildDX11TerrainVertexBuffers(pDevice))
			s_bDX11TerrainVBReadyLatched = true;
	}

	// Register visible character instances every frame.
	// Runtime DX11 world color pass also consumes this list, so it cannot depend on shadow readiness.
	rkMap.ClearCharacterShadowCasters();
	CPythonCharacterManager& rkChrMgr = CPythonCharacterManager::Instance();
	std::vector<CGraphicThingInstance*> kVisibleCharacterThings;
	rkChrMgr.CollectVisibleThingInstancesDX11(kVisibleCharacterThings);
	for (auto* pThingInstance : kVisibleCharacterThings)
	{
		if (pThingInstance)
			rkMap.RegisterCharacterShadowCaster(pThingInstance);
	}

	// S2: Register object/dungeon shadow casters when dynamic shadow resources are active.
	if (rkMap.IsDX11DynamicShadowsReady())
	{
		// Register visible static object instances as shadow casters.
		// Priority order:
		// 1) authored shadow-flag object instances,
		// 2) currently collected visible thing instances (opaque+blend) as compatibility feed.
		std::set<CGraphicThingInstance*> kRegisteredObjectCasters;
		std::vector<CGraphicThingInstance*> kOpaqueThingCandidates;
		std::vector<CGraphicThingInstance*> kBlendThingCandidates;
		kOpaqueThingCandidates.reserve(512);
		kBlendThingCandidates.reserve(256);

		DWORD dwAreaObjectInstances = 0;
		DWORD dwAreaThingPointers = 0;
		DWORD dwAreaDungeonPointers = 0;
		DWORD dwShadowFlagEligible = 0;

		auto RegisterThingCaster = [&](CGraphicThingInstance* pThingInstance) -> bool
		{
			if (!pThingInstance || !pThingInstance->isShow())
				return false;

			DirectX::SimpleMath::Vector3 v3BBoxMin, v3BBoxMax;
			if (!pThingInstance->GetBoundingAABB(v3BBoxMin, v3BBoxMax))
				return false;

			const float fHeight = fabsf(v3BBoxMax.z - v3BBoxMin.z);
			if (!rkMap.IsDynamicShadowCaster(fHeight))
				return false;

			if (!kRegisteredObjectCasters.insert(pThingInstance).second)
				return false;

			rkMap.RegisterObjectShadowCaster(pThingInstance);
			return true;
		};

		for (int iAreaIndex = 0; iAreaIndex < AROUND_AREA_NUM; ++iAreaIndex)
		{
			CArea* pArea = nullptr;
			if (!rkMap.GetAreaPointer(static_cast<BYTE>(iAreaIndex), &pArea) || !pArea)
				continue;

			// Compatibility feed: collect currently visible render candidates from area lists.
			pArea->CollectRenderingObject(kOpaqueThingCandidates);
			pArea->CollectBlendRenderingObject(kBlendThingCandidates);

			const DWORD dwObjectCount = pArea->GetObjectInstanceCount();
			dwAreaObjectInstances += dwObjectCount;

			for (DWORD dwObjectIndex = 0; dwObjectIndex < dwObjectCount; ++dwObjectIndex)
			{
				const CArea::TObjectInstance* pObjectInstance = nullptr;
				if (!pArea->GetObjectInstancePointer(dwObjectIndex, &pObjectInstance) || !pObjectInstance)
					continue;

				if (pObjectInstance->pThingInstance)
					++dwAreaThingPointers;
				if (pObjectInstance->pDungeonBlock)
					++dwAreaDungeonPointers;

				// Authored path
				if (pObjectInstance->isShadowFlag && RegisterThingCaster(pObjectInstance->pThingInstance))
					++dwShadowFlagEligible;
			}
		}

		// Compatibility path for maps without authored flags: visible thing instances.
		for (auto* pThingInstance : kOpaqueThingCandidates)
			RegisterThingCaster(pThingInstance);
		for (auto* pThingInstance : kBlendThingCandidates)
			RegisterThingCaster(pThingInstance);

		static DWORD s_dwObjectCasterFeedLogTick = 0;
		const DWORD dwNowObjectCaster = ELTimer_GetMSec();
		if (0 == s_dwObjectCasterFeedLogTick || dwNowObjectCaster - s_dwObjectCasterFeedLogTick >= 5000u)
		{
			s_dwObjectCasterFeedLogTick = dwNowObjectCaster;
			TraceError(
				"DX11_OBJECT_CASTER_FEED area_objects=%u thing_ptr=%u dungeon_ptr=%u opaque=%u blend=%u flagged=%u registered=%u",
				dwAreaObjectInstances,
				dwAreaThingPointers,
				dwAreaDungeonPointers,
				static_cast<unsigned int>(kOpaqueThingCandidates.size()),
				static_cast<unsigned int>(kBlendThingCandidates.size()),
				dwShadowFlagEligible,
				static_cast<unsigned int>(kRegisteredObjectCasters.size()));
		}
	}

	// Native-present path bypasses CPythonBackground::Render(); run full world color passes here.
	rkMap.RenderArea();
	rkMap.RenderTree();
	rkMap.RenderTerrainDX11(
		pDevice,
		pContext,
		&dwWorldPortMaskObserved,
		&dwWorldPortMaskSubmitted,
		&dwWorldPortMaskApplicable);
	rkMap.RenderBlendArea();

	const bool bObjectsReady = rkMap.ProbeDX11ObjectsReady();
	if (bObjectsReady)
	{
		dwWorldPortMaskApplicable |= CGraphicDeviceDX11::WORLD_OBJECTS_DX11;
		dwWorldPortMaskObserved |= CGraphicDeviceDX11::WORLD_OBJECTS_DX11;
		if (rkMap.GetDX11LastSubmittedObjectCount() > 0u)
			dwWorldPortMaskSubmitted |= CGraphicDeviceDX11::WORLD_OBJECTS_DX11;
	}

	rkMap.RenderEffect();

	const bool bEffectsReady = rkMap.ProbeDX11EffectsReady();
	if (bEffectsReady)
	{
		dwWorldPortMaskApplicable |= CGraphicDeviceDX11::WORLD_EFFECTS_DX11;
		dwWorldPortMaskObserved |= CGraphicDeviceDX11::WORLD_EFFECTS_DX11;
		if (rkMap.GetDX11LastSubmittedEffectCount() > 0u)
			dwWorldPortMaskSubmitted |= CGraphicDeviceDX11::WORLD_EFFECTS_DX11;
	}

	const bool bSpeedTreeReady = rkMap.ProbeDX11SpeedTreeReady();
	if (bSpeedTreeReady)
	{
		dwWorldPortMaskApplicable |= CGraphicDeviceDX11::WORLD_SPEEDTREE_DX11;
		dwWorldPortMaskObserved |= CGraphicDeviceDX11::WORLD_SPEEDTREE_DX11;
		if (rkMap.GetDX11LastSubmittedSpeedTreeCount() > 0u)
			dwWorldPortMaskSubmitted |= CGraphicDeviceDX11::WORLD_SPEEDTREE_DX11;
	}

	int iRenderedWaterPatches = 0;
	int iObservedWaterPatches = 0;
	if (rkMap.IsDX11WaterReady())
	{
		rkMap.RenderWaterDX11(pDevice, pContext);
		iRenderedWaterPatches = rkMap.GetDX11LastRenderedWaterPatchCount();
		iObservedWaterPatches = rkMap.GetDX11LastObservedWaterPatchCount();
		if (iObservedWaterPatches > 0)
		{
			dwWorldPortMaskApplicable |= CGraphicDeviceDX11::WORLD_WATER_DX11;
			dwWorldPortMaskObserved |= CGraphicDeviceDX11::WORLD_WATER_DX11;
		}
		if (iRenderedWaterPatches > 0)
			dwWorldPortMaskSubmitted |= CGraphicDeviceDX11::WORLD_WATER_DX11;
	}
	int iRenderedTerrainPatches = 0;
	int iRenderedTerrainSplats = 0;
	float fRenderedTerrainSplatRatio = 0.0f;
	rkMap.GetRenderedSplatNum(&iRenderedTerrainPatches, &iRenderedTerrainSplats, &fRenderedTerrainSplatRatio);

	m_kDX11WorldSubmitTelemetry.iTerrainPatches = iRenderedTerrainPatches;
	m_kDX11WorldSubmitTelemetry.iTerrainSplats = iRenderedTerrainSplats;
	m_kDX11WorldSubmitTelemetry.iWaterPatches = rkMap.GetDX11LastRenderedWaterPatchCount();
	m_kDX11WorldSubmitTelemetry.dwObjectSubmitted = rkMap.GetDX11LastSubmittedObjectCount();
	m_kDX11WorldSubmitTelemetry.dwEffectSubmitted = rkMap.GetDX11LastSubmittedEffectCount();
	m_kDX11WorldSubmitTelemetry.dwEffectParticleSubmitted = rkMap.GetDX11LastSubmittedEffectParticleCount();
	m_kDX11WorldSubmitTelemetry.dwEffectMeshSubmitted = rkMap.GetDX11LastSubmittedEffectMeshCount();
	m_kDX11WorldSubmitTelemetry.dwSpeedTreeSubmitted = rkMap.GetDX11LastSubmittedSpeedTreeCount();
	m_kDX11WorldSubmitTelemetry.dwObservedMask = dwWorldPortMaskObserved;
	m_kDX11WorldSubmitTelemetry.dwSubmittedMask = dwWorldPortMaskSubmitted;
	m_kDX11WorldSubmitTelemetry.dwApplicableMask = dwWorldPortMaskApplicable;
	m_kDX11WorldSubmitTelemetry.dwCommittedMask = 0u;

	static DWORD s_dwWorldPassOrderLogTick = 0u;
	if (0u == s_dwWorldPassOrderLogTick || (dwNow - s_dwWorldPassOrderLogTick) >= 5000u)
	{
		s_dwWorldPassOrderLogTick = dwNow;
		TraceError(
			"DX11_WORLD_PASS_ORDER frame_ms=%u terrain_submit=%d water_submit=%d object_blend_submit=%u",
			static_cast<unsigned int>(dwNow),
			(iRenderedTerrainPatches > 0) ? 1 : 0,
			(iRenderedWaterPatches > 0) ? 1 : 0,
			static_cast<unsigned int>(rkMap.GetDX11LastSubmittedObjectCount()));
	}
	if (pdwOutWorldPortMaskObserved)
		*pdwOutWorldPortMaskObserved = dwWorldPortMaskObserved;
	if (pdwOutWorldPortMaskSubmitted)
		*pdwOutWorldPortMaskSubmitted = dwWorldPortMaskSubmitted;
	if (pdwOutWorldPortMaskApplicable)
		*pdwOutWorldPortMaskApplicable = dwWorldPortMaskApplicable;
	return true;
}

void CPythonBackground::SetDX11WorldSubmitCommittedMask(uint32_t dwCommittedMask)
{
	m_kDX11WorldSubmitTelemetry.dwCommittedMask = dwCommittedMask;
}

void CPythonBackground::RenderSnow()
{
	m_SnowEnvironment.Render();
}

void CPythonBackground::RenderRain()
{
	m_RainEnvironment.Render();
}

void CPythonBackground::RenderStorm()
{
	m_StormEnvironment.Render();
}

void CPythonBackground::RenderPCBlocker()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RenderPCBlocker();
}

void CPythonBackground::RenderCollision()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RenderCollision();
}

void CPythonBackground::RenderCharacterShadowToTexture()
{
	extern bool GRAPHICS_CAPS_CAN_NOT_DRAW_SHADOW;
	if (GRAPHICS_CAPS_CAN_NOT_DRAW_SHADOW)
		return;

	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	DWORD t1=ELTimer_GetMSec();

	if (m_eShadowLevel == SHADOW_ALL ||
		m_eShadowLevel == SHADOW_ALL_HIGH ||
		m_eShadowLevel == SHADOW_ALL_MAX ||
		m_eShadowLevel == SHADOW_GROUND_AND_SOLO)
	{
		CStateManager* pStateManager = CStateManager::InstancePtr();
		// DX11-only mode: Skip if StateManager/device is not initialized
		if (!pStateManager || !pStateManager->GetDevice())
			return;

		DirectX::SimpleMath::Matrix matWorld;
	pStateManager->GetTransform(GRP_TS_WORLD, &matWorld);

		bool canRender=rkMap.BeginRenderCharacterShadowToTexture();
		if (canRender)
		{
			CPythonCharacterManager& rkChrMgr=CPythonCharacterManager::Instance();

			if (m_eShadowLevel == SHADOW_GROUND_AND_SOLO)
				rkChrMgr.RenderShadowMainInstance();
			else
				rkChrMgr.RenderShadowAllInstances();
		}
		rkMap.EndRenderCharacterShadowToTexture();

	pStateManager->SetTransform(GRP_TS_WORLD, &matWorld);
	}

	DWORD t2=ELTimer_GetMSec();

	m_dwRenderShadowTime=t2-t1;	
}

inline float Interpolate(float fStart, float fEnd, float fPercent)
{
	return fStart + (fEnd - fStart) * fPercent;
}
struct CollisionChecker
{
	bool isBlocked;
	CInstanceBase* pInstance;
	CollisionChecker(CInstanceBase* pInstance) : pInstance(pInstance), isBlocked(false) {}
	void operator () (CGraphicObjectInstance* pOpponent)
	{
		if (isBlocked)
			return;

		if (!pOpponent)
			return;

		if (pInstance->IsBlockObject(*pOpponent))
			isBlocked=true;
	}
};

struct CollisionAdjustChecker
{
	bool isBlocked;
	CInstanceBase* pInstance;
	CollisionAdjustChecker(CInstanceBase* pInstance) : pInstance(pInstance), isBlocked(false) {}
	void operator () (CGraphicObjectInstance* pOpponent)
	{
		if (!pOpponent)
			return;

		if (pInstance->AvoidObject(*pOpponent))
			isBlocked=true;
	}
};

bool CPythonBackground::CheckAdvancing(CInstanceBase * pInstance)
{
	if (!IsMapReady())
		return true;

	Vector3d center;
	float radius;
	pInstance->GetGraphicThingInstanceRef().GetBoundingSphere(center,radius);

	CCullingManager & rkCullingMgr = CCullingManager::Instance();

	CollisionAdjustChecker kCollisionAdjustChecker(pInstance);
	rkCullingMgr.ForInRange(center, radius, &kCollisionAdjustChecker);
	if (kCollisionAdjustChecker.isBlocked)
	{
		CollisionChecker kCollisionChecker(pInstance);
		rkCullingMgr.ForInRange(center, radius, &kCollisionChecker);
		if (kCollisionChecker.isBlocked)
		{
			pInstance->BlockMovement();
			return true;
		}
		else
		{
			pInstance->NEW_MoveToDestPixelPositionDirection(pInstance->NEW_GetDstPixelPositionRef());
		}
		return false;
	}
	return false;
}

void CPythonBackground::RenderSky()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RenderSky();
}

void CPythonBackground::RenderCloud()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RenderCloud();
}

void CPythonBackground::RenderWater()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RenderWater();
}

void CPythonBackground::RenderEffect()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RenderEffect();
}

void CPythonBackground::RenderBeforeLensFlare()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RenderBeforeLensFlare();
}

void CPythonBackground::RenderAfterLensFlare()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RenderAfterLensFlare();
}

void CPythonBackground::RenderScreenFiltering()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RenderScreenFiltering();
}

void CPythonBackground::ClearGuildArea()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.ClearGuildArea();
}

void CPythonBackground::RegisterGuildArea(int isx, int isy, int iex, int iey)
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.RegisterGuildArea(isx, isy, iex, iey);
}

void CPythonBackground::SetCharacterDirLight()
{
	if (!IsMapReady())
		return;

	if (!mc_pcurEnvironmentData)
		return;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
		if (!pStateManager11)
		{
			static bool s_bLoggedCharacterDirLightStateManager11Missing = false;
			if (!s_bLoggedCharacterDirLightStateManager11Missing)
			{
				s_bLoggedCharacterDirLightStateManager11Missing = true;
				TraceError("DX11_LIGHT_BIND_FAIL pass=python_character_light reason=state_manager11_unavailable");
			}
			return;
		}

		pStateManager11->SetLight(0, &mc_pcurEnvironmentData->DirLights[ENV_DIRLIGHT_CHARACTER]);
		pStateManager11->SetLightEnable(0, TRUE);
		return;
	}

	static bool s_bLoggedCharacterDirLightDX11Unavailable = false;
	if (!s_bLoggedCharacterDirLightDX11Unavailable)
	{
		s_bLoggedCharacterDirLightDX11Unavailable = true;
		TraceError("DX11_LIGHT_BIND_FAIL pass=python_character_light reason=dx11_device_unavailable");
	}
	return;
}

void CPythonBackground::SetBackgroundDirLight()
{
	if (!IsMapReady())
		return;
	if (!mc_pcurEnvironmentData)
		return;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
		if (!pStateManager11)
		{
			static bool s_bLoggedBackgroundDirLightStateManager11Missing = false;
			if (!s_bLoggedBackgroundDirLightStateManager11Missing)
			{
				s_bLoggedBackgroundDirLightStateManager11Missing = true;
				TraceError("DX11_LIGHT_BIND_FAIL pass=python_background_light reason=state_manager11_unavailable");
			}
			return;
		}

		pStateManager11->SetLight(0, &mc_pcurEnvironmentData->DirLights[ENV_DIRLIGHT_BACKGROUND]);
		pStateManager11->SetLightEnable(0, TRUE);
		return;
	}

	static bool s_bLoggedBackgroundDirLightDX11Unavailable = false;
	if (!s_bLoggedBackgroundDirLightDX11Unavailable)
	{
		s_bLoggedBackgroundDirLightDX11Unavailable = true;
		TraceError("DX11_LIGHT_BIND_FAIL pass=python_background_light reason=dx11_device_unavailable");
	}
	return;
}

void CPythonBackground::GlobalPositionToLocalPosition(int32_t& rGlobalX, int32_t& rGlobalY)
{
	rGlobalX-=m_dwBaseX;
	rGlobalY-=m_dwBaseY;
}

void CPythonBackground::LocalPositionToGlobalPosition(int32_t& rLocalX, int32_t& rLocalY)
{
	rLocalX+=m_dwBaseX;
	rLocalY+=m_dwBaseY;
}

void CPythonBackground::GlobalPositionToLocalPosition(uint32_t& rGlobalX, uint32_t& rGlobalY)
{
	rGlobalX -= m_dwBaseX;
	rGlobalY -= m_dwBaseY;
}

void CPythonBackground::LocalPositionToGlobalPosition(uint32_t& rLocalX, uint32_t& rLocalY)
{
	rLocalX += m_dwBaseX;
	rLocalY += m_dwBaseY;
}

void CPythonBackground::RegisterDungeonMapName(const char * c_szMapName)
{
	m_kSet_strDungeonMapName.insert(c_szMapName);
}

CPythonBackground::TMapInfo* CPythonBackground::GlobalPositionToMapInfo(DWORD dwGlobalX, DWORD dwGlobalY)
{
	TMapInfoVector::iterator f = std::find_if(m_kVct_kMapInfo.begin(), m_kVct_kMapInfo.end(), FFindWarpMapName(dwGlobalX, dwGlobalY));
	if (f == m_kVct_kMapInfo.end())
		return NULL;

	return &(*f);
}

void CPythonBackground::Warp(DWORD dwX, DWORD dwY)
{
	TMapInfo* pkMapInfo = GlobalPositionToMapInfo(dwX, dwY);
	if (!pkMapInfo)
	{
		TraceError("NOT_FOUND_GLOBAL_POSITION(%d, %d)", dwX, dwY);
		return;
	}

	RefreshShadowLevel();
	TMapInfo & rMapInfo = *pkMapInfo;
	assert( (dwX >= rMapInfo.m_dwBaseX) && (dwY >= rMapInfo.m_dwBaseY) );

	if (!LoadMap(rMapInfo.m_strName, float(dwX - rMapInfo.m_dwBaseX), float(dwY - rMapInfo.m_dwBaseY), 0))
	{
		// LOAD_MAP_ERROR_HANDLING
		PostQuitMessage(0);
		// END_OF_LOAD_MAP_ERROR_HANDLING
		return;
	}

	CPythonMiniMap::Instance().LoadAtlas();

	m_dwBaseX=rMapInfo.m_dwBaseX;
	m_dwBaseY=rMapInfo.m_dwBaseY;

	m_strMapName = rMapInfo.m_strName;

	SetXMaxTree(m_iXMasTreeGrade);

	if (m_kSet_strDungeonMapName.end() != m_kSet_strDungeonMapName.find(m_strMapName))
	{
		EnableTerrainOnlyForHeight();

		CMapOutdoor& rkMap=GetMapOutdoorRef();
		rkMap.EnablePortal(TRUE);
	}

	m_kSet_iShowingPortalID.clear();
	m_kMap_dwTargetID_dwChrID.clear();
	m_kMap_dwID_kReserveTargetEffect.clear();
	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::VisibleGuildArea()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.VisibleMarkedArea();

	m_bVisibleGuildArea = TRUE;
}

void CPythonBackground::DisableGuildArea()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.DisableMarkedArea();

	m_bVisibleGuildArea = FALSE;
}

const char * CPythonBackground::GetWarpMapName()
{
	return m_strMapName.c_str();
}

void CPythonBackground::ChangeToDay()
{
	m_iDayMode = DAY_MODE_LIGHT;
	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::ChangeToNight()
{
	m_iDayMode = DAY_MODE_DARK;
	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::EnableSnowEnvironment()
{
	m_bSnowEnvironmentEnabled = true;
	m_SnowEnvironment.Enable();
	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::DisableSnowEnvironment()
{
	m_bSnowEnvironmentEnabled = false;
	m_SnowEnvironment.Disable();
	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::EnableRainEnvironment()
{
	m_bRainEnvironmentEnabled = true;
	m_RainEnvironment.Enable();
	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::DisableRainEnvironment()
{
	m_bRainEnvironmentEnabled = false;
	m_RainEnvironment.Disable();
	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::EnableStormEnvironment()
{
	m_bStormEnvironmentEnabled = true;

	// Link storm to rain environment
	m_StormEnvironment.SetRainEnvironment(&m_RainEnvironment);

	// Enable rain if not already enabled (storm requires rain)
	if (!m_bRainEnvironmentEnabled)
	{
		EnableRainEnvironment();
	}

	m_StormEnvironment.Enable();
	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::DisableStormEnvironment()
{
	m_bStormEnvironmentEnabled = false;
	m_StormEnvironment.Disable();
	__SyncDX11EnvironmentBridgeState();
}

bool CPythonBackground::RunStormWeatherRestoreDiagnostic()
{
	m_StormEnvironment.SetRainEnvironment(&m_RainEnvironment);
	return m_StormEnvironment.RunWeatherRestoreDiagnostic();
}

// Public getters for weather environments (for DebugUI)
CSnowEnvironment* CPythonBackground::GetSnowEnvironment()
{
	return &m_SnowEnvironment;
}

CRainEnvironment* CPythonBackground::GetRainEnvironment()
{
	return &m_RainEnvironment;
}

CStormEnvironment* CPythonBackground::GetStormEnvironment()
{
	return &m_StormEnvironment;
}

void CPythonBackground::SetWeatherMonth(int iMonth)
{
	if (iMonth < 1)
		iMonth = 1;
	if (iMonth > 12)
		iMonth = 12;

	m_iWeatherMonth = iMonth;
	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::SetRainIntensity(float fIntensity)
{
	if (fIntensity < 0.0f)
		fIntensity = 0.0f;
	if (fIntensity > 1.0f)
		fIntensity = 1.0f;

	m_fRainIntensity = fIntensity;

	// Map intensity 0.0-1.0 to particle count 0-10000
	DWORD dwParticleCount = (DWORD)(fIntensity * 10000.0f);
	m_RainEnvironment.SetParticleCount(dwParticleCount);

	__SyncDX11EnvironmentBridgeState();
}

void CPythonBackground::__SyncDX11EnvironmentBridgeState()
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap = GetMapOutdoorRef();
	rkMap.UpdateDX11EnvironmentBridgeState(
		m_bSnowEnvironmentEnabled,
		m_iDayMode,
		m_iWeatherMonth,
		m_fRainIntensity);
}

const DirectX::SimpleMath::Vector3 c_v3TreePos = DirectX::SimpleMath::Vector3(76500.0f, -60900.0f, 20215.0f);

void CPythonBackground::SetXMaxTree(int iGrade)
{
	if (!m_pkMap)
		return;

	assert(iGrade >= 0 && iGrade <= 3);
	m_iXMasTreeGrade = iGrade;

	CMapOutdoor& rkMap=GetMapOutdoorRef();

	if ("map_n_snowm_01" != m_strMapName)
	{
		rkMap.XMasTree_Destroy();
		return;
	}

	if (0 == iGrade)
	{
		rkMap.XMasTree_Destroy();
		return;
	}

	//////////////////////////////////////////////////////////////////////

	iGrade -= 1;
	iGrade = std::max(iGrade, 0);
	iGrade = std::min(iGrade, 2);

	static std::string s_strTreeName[3] = {
		"d:/ymir work/tree/christmastree1.spt",
		"d:/ymir work/tree/christmastree2.spt",
		"d:/ymir work/tree/christmastree3.spt"
	};
	static std::string s_strEffectName[3] = {
		"d:/ymir work/effect/etc/christmas_tree/tree_1s.mse",
		"d:/ymir work/effect/etc/christmas_tree/tree_2s.mse",
		"d:/ymir work/effect/etc/christmas_tree/tree_3s.mse",
	};
	rkMap.XMasTree_Set(c_v3TreePos.x, c_v3TreePos.y, c_v3TreePos.z, s_strTreeName[iGrade].c_str(), s_strEffectName[iGrade].c_str());
}

//////////////////////////////////////////////////////////////////////
// Sun Position Control
//////////////////////////////////////////////////////////////////////

void CPythonBackground::SetSunDirection(float x, float y, float z)
{
	if (!mc_pcurEnvironmentData)
		return;

	// Cast to non-const to modify environment data
	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);

	// Normalize direction vector
	D3DXVECTOR3 v3Dir(x, y, z);
	D3DXVec3Normalize(&v3Dir, &v3Dir);

	// Set background light direction (sun)
	env->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction = v3Dir;

	// Update light in DX11 immediately
	SetBackgroundDirLight();
}

void CPythonBackground::SetSunAzimuth(float fAzimuthDegrees)
{
	if (!mc_pcurEnvironmentData)
		return;

	// Cast to non-const to modify environment data
	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);

	// Get current elevation (preserve it)
	D3DXVECTOR3 v3CurrentDir = env->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction;
	D3DXVec3Normalize(&v3CurrentDir, &v3CurrentDir);

	// Calculate current elevation from Y component
	float fElevationRadians = asinf(std::max(-1.0f, std::min(1.0f, v3CurrentDir.y)));

	// Convert azimuth to radians
	float fAzimuthRadians = D3DXToRadian(fAzimuthDegrees);

	// Convert spherical (azimuth, elevation) to cartesian coordinates
	// Azimuth: 0°=N, 90°=E, 180°=S, 270°=W
	// Elevation: -90°=zenith, 0°=horizon, +90°=nadir
	v3CurrentDir.x = cos(fElevationRadians) * sin(fAzimuthRadians);
	v3CurrentDir.y = sin(fElevationRadians);
	v3CurrentDir.z = cos(fElevationRadians) * cos(fAzimuthRadians);

	D3DXVec3Normalize(&v3CurrentDir, &v3CurrentDir);
	env->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction = v3CurrentDir;
	SetBackgroundDirLight();
}

void CPythonBackground::SetSunElevation(float fElevationDegrees)
{
	if (!mc_pcurEnvironmentData)
		return;

	// Cast to non-const to modify environment data
	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);

	// Get current azimuth (preserve it)
	D3DXVECTOR3 v3CurrentDir = env->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction;
	D3DXVec3Normalize(&v3CurrentDir, &v3CurrentDir);

	// Calculate current azimuth from X and Z components
	float fAzimuthRadians = atan2f(v3CurrentDir.x, v3CurrentDir.z);

	// Convert elevation to radians
	float fElevationRadians = D3DXToRadian(fElevationDegrees);

	// Clamp elevation to valid range (-85° to +85° to avoid gimbal lock)
	fElevationRadians = std::max(-1.483f, std::min(1.483f, fElevationRadians));

	// Convert spherical to cartesian
	v3CurrentDir.x = cos(fElevationRadians) * sin(fAzimuthRadians);
	v3CurrentDir.y = sin(fElevationRadians);
	v3CurrentDir.z = cos(fElevationRadians) * cos(fAzimuthRadians);

	D3DXVec3Normalize(&v3CurrentDir, &v3CurrentDir);
	env->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction = v3CurrentDir;
	SetBackgroundDirLight();
}

void CPythonBackground::GetSunDirection(float& x, float& y, float& z) const
{
	if (!mc_pcurEnvironmentData)
	{
		x = y = z = 0.0f;
		return;
	}

	x = mc_pcurEnvironmentData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction.x;
	y = mc_pcurEnvironmentData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction.y;
	z = mc_pcurEnvironmentData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction.z;
}

void CPythonBackground::CreateTargetEffect(DWORD dwID, DWORD dwChrVID)
{
	m_kMap_dwTargetID_dwChrID.insert(std::make_pair(dwID, dwChrVID));
}

void CPythonBackground::CreateTargetEffect(DWORD dwID, long lx, long ly)
{
	if (m_kMap_dwTargetID_dwChrID.end() != m_kMap_dwTargetID_dwChrID.find(dwID))
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();

	DWORD dwBaseX;
	DWORD dwBaseY;
	rkMap.GetBaseXY(&dwBaseX, &dwBaseY);

	int ilx = +(lx-int(dwBaseX));
	int ily = -(ly-int(dwBaseY));

	float fHeight = rkMap.GetHeight(float(ilx), float(ily));

	if (0.0f == fHeight)
	{
		SReserveTargetEffect ReserveTargetEffect;
		ReserveTargetEffect.ilx = ilx;
		ReserveTargetEffect.ily = ily;
		m_kMap_dwID_kReserveTargetEffect.insert(std::make_pair(dwID, ReserveTargetEffect));
		return;
	}

	CreateSpecialEffect(dwID, ilx, ily, fHeight, g_strEffectName.c_str());
}

void CPythonBackground::DeleteTargetEffect(DWORD dwID)
{
	if (m_kMap_dwID_kReserveTargetEffect.end() != m_kMap_dwID_kReserveTargetEffect.find(dwID))
	{
		m_kMap_dwID_kReserveTargetEffect.erase(dwID);
	}
	if (m_kMap_dwTargetID_dwChrID.end() != m_kMap_dwTargetID_dwChrID.find(dwID))
	{
		m_kMap_dwTargetID_dwChrID.erase(dwID);
	}

	DeleteSpecialEffect(dwID);
}

void CPythonBackground::CreateSpecialEffect(DWORD dwID, float fx, float fy, float fz, const char * c_szFileName)
{
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.SpecialEffect_Create(dwID, fx, fy, fz, c_szFileName);
}

void CPythonBackground::DeleteSpecialEffect(DWORD dwID)
{
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.SpecialEffect_Delete(dwID);
}

// M3-ENV-ADMIN-PANEL-74: Environment parameter controls for Admin Panel TAB7
// These methods allow real-time environment tweaking from Python UI

void CPythonBackground::SetSkyScale(float fScale)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->v3SkyBoxScale = D3DXVECTOR3(fScale, fScale, fScale);

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentSkyBox();
	}
}

void CPythonBackground::SetCloudScale(float fX, float fY)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->v2CloudScale = D3DXVECTOR2(fX, fY);

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentSkyBox();
	}
}

void CPythonBackground::SetCloudHeight(float fHeight)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->fCloudHeight = fHeight;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentSkyBox();
	}
}

void CPythonBackground::SetCloudTextureScale(float fX, float fY)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->v2CloudTextureScale = D3DXVECTOR2(fX, fY);

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentSkyBox();
	}
}

void CPythonBackground::SetCloudSpeed(float fX, float fY)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->v2CloudSpeed = D3DXVECTOR2(fX, fY);

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentSkyBox();
	}
}

void CPythonBackground::SetFogEnable(bool bEnable)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->bFogEnable = bEnable;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.ApplyEnvironmentDistanceOnly();
	}
}

void CPythonBackground::SetFogDensity(bool bDensity)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->bDensityFog = bDensity;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.ApplyEnvironmentDistanceOnly();
	}
}

void CPythonBackground::SetFogNear(float fDistance)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->m_fFogNearDistance = fDistance;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.ApplyEnvironmentDistanceOnly();
	}
}

void CPythonBackground::SetFogFar(float fDistance)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->m_fFogFarDistance = fDistance;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.ApplyEnvironmentDistanceOnly();
	}
}

void CPythonBackground::SetFogLevel(BYTE byLevel)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->bFogLevel = byLevel;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.ApplyEnvironmentDistanceOnly();
	}
}

void CPythonBackground::SetFogColor(float r, float g, float b, float a)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->FogColor = D3DXCOLOR(r, g, b, a);

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.ApplyEnvironmentDistanceOnly();
	}
}

void CPythonBackground::SetLensFlareEnable(bool bEnable)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->bLensFlareEnable = bEnable;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentLensFlare();
	}
}

void CPythonBackground::SetMainFlareEnable(bool bEnable)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->bMainFlareEnable = bEnable;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentLensFlare();
	}
}

void CPythonBackground::SetSunSize(float fSize)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->fMainFlareSize = fSize;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentLensFlare();
	}
}

void CPythonBackground::SetSunBrightness(float fBrightness)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->fLensFlareMaxBrightness = fBrightness;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentLensFlare();
	}
}

void CPythonBackground::SetSunColor(float r, float g, float b, float a)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->LensFlareBrightnessColor = D3DXCOLOR(r, g, b, a);

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentLensFlare();
	}
}

void CPythonBackground::SetBGDirectionalLightEnable(bool bEnable)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->bDirLightsEnable[ENV_DIRLIGHT_BACKGROUND] = bEnable;
	SetBackgroundDirLight();
}

void CPythonBackground::SetCharDirectionalLightEnable(bool bEnable)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->bDirLightsEnable[ENV_DIRLIGHT_CHARACTER] = bEnable;
	SetCharacterDirLight();
}

void CPythonBackground::SetBGLightDirection(float x, float y, float z)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);

	D3DXVECTOR3 v3Dir(x, y, z);
	D3DXVec3Normalize(&v3Dir, &v3Dir);

	env->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction = v3Dir;
	SetBackgroundDirLight();
}

void CPythonBackground::SetBGLightAmbient(float r, float g, float b, float a)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient = D3DXCOLOR(r, g, b, a);
	SetBackgroundDirLight();
}

void CPythonBackground::SetBGLightDiffuse(float r, float g, float b, float a)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse = D3DXCOLOR(r, g, b, a);
	SetBackgroundDirLight();
}

void CPythonBackground::SetCharLightDirection(float x, float y, float z)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);

	D3DXVECTOR3 v3Dir(x, y, z);
	D3DXVec3Normalize(&v3Dir, &v3Dir);

	env->DirLights[ENV_DIRLIGHT_CHARACTER].Direction = v3Dir;
	SetCharacterDirLight();
}

void CPythonBackground::SetCharLightAmbient(float r, float g, float b, float a)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient = D3DXCOLOR(r, g, b, a);
	SetCharacterDirLight();
}

void CPythonBackground::SetCharLightDiffuse(float r, float g, float b, float a)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse = D3DXCOLOR(r, g, b, a);
	SetCharacterDirLight();
}

void CPythonBackground::SetScreenFilterEnable(bool bEnable)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->bFilteringEnable = bEnable;

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentScreenFilter();
	}
}

void CPythonBackground::SetScreenFilterColor(float r, float g, float b, float a)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->FilteringColor = D3DXCOLOR(r, g, b, a);

	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentScreenFilter();
	}
}

void CPythonBackground::SetWindStrength(float fStrength)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->fWindStrength = fStrength;
}

void CPythonBackground::SetWindRandomness(float fRandomness)
{
	if (!mc_pcurEnvironmentData)
		return;

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
	env->fWindRandom = fRandomness;
}

void CPythonBackground::ApplyEnvironmentPreset(int iPresetIndex)
{
	if (!mc_pcurEnvironmentData)
		return;

	// M3-SKY-PRESET-PERSIST-74: Guard against recursion from SelectViewDistanceNum
	static bool s_bInApplyPreset = false;
	if (s_bInApplyPreset)
		return;
	s_bInApplyPreset = true;

	TraceError("M3_PRESET_APPLY START preset=%d bApplied=%d lastIndex=%d",
		iPresetIndex, m_bPresetApplied, m_iLastPresetIndex);

	TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);

	// M3-SKY-SCALE-PERSIST: Save current sky scale before applying preset
	// This prevents overwriting user's manual sky scale adjustments
	D3DXVECTOR3 v3SavedSkyScale = env->v3SkyBoxScale;
	TraceError("M3_PRESET_BEFORE fog_near=%.1f fog_far=%.1f sky_scale=%.1f fog_enable=%d density_fog=%d",
		env->m_fFogNearDistance, env->m_fFogFarDistance, env->v3SkyBoxScale.x,
		env->bFogEnable ? 1 : 0, env->bDensityFog ? 1 : 0);

	// Apply preset based on index (0=Day, 1=Night, 2=Sunset, 3=Overcast)
	switch (iPresetIndex)
	{
	case 0: // Day
		env->m_fFogNearDistance = 5000.0f;   // From config.h: kEnvironmentFogNearDistanceDay
		env->m_fFogFarDistance = 15000.0f;   // From config.h: kEnvironmentFogFarDistanceDay
		env->bFogLevel = 2;
		env->FogColor = D3DXCOLOR(0.7f, 0.8f, 0.9f, 1.0f);
		env->bFogEnable = TRUE;   // M3-SKY-PRESET-PERSIST-74: Enable fog (ImGui default)
		env->bDensityFog = FALSE; // M3-SKY-PRESET-PERSIST-74: Use distance fog, not density fog
		env->bLensFlareEnable = TRUE;
		env->fMainFlareSize = 0.4f;
		env->fLensFlareMaxBrightness = 1.0f;   // From config.h: kEnvironmentLensFlareBrightnessSun
		env->LensFlareBrightnessColor = D3DXCOLOR(1.0f, 0.95f, 0.9f, 1.0f);
		break;

	case 1: // Night
		env->m_fFogNearDistance = 3000.0f;   // From config.h: kEnvironmentFogNearDistanceNight
		env->m_fFogFarDistance = 12000.0f;   // From config.h: kEnvironmentFogFarDistanceNight
		env->bFogLevel = 7;
		env->FogColor = D3DXCOLOR(0.1f, 0.1f, 0.15f, 1.0f);
		env->bFogEnable = TRUE;   // M3-SKY-PRESET-PERSIST-74: Enable fog (ImGui default)
		env->bDensityFog = FALSE; // M3-SKY-PRESET-PERSIST-74: Use distance fog, not density fog
		env->bLensFlareEnable = FALSE;
		env->fMainFlareSize = 0.3f;
		env->fLensFlareMaxBrightness = 0.4f;   // From config.h: kEnvironmentLensFlareBrightnessMoon
		env->LensFlareBrightnessColor = D3DXCOLOR(0.8f, 0.8f, 1.0f, 1.0f);
		break;

	case 2: // Sunset
		env->m_fFogNearDistance = 3000.0f;
		env->m_fFogFarDistance = 20000.0f;
		env->bFogLevel = 5;
		env->FogColor = D3DXCOLOR(1.0f, 0.7f, 0.5f, 1.0f);
		env->bFogEnable = TRUE;   // M3-SKY-PRESET-PERSIST-74: Enable fog (ImGui default)
		env->bDensityFog = FALSE; // M3-SKY-PRESET-PERSIST-74: Use distance fog, not density fog
		env->bLensFlareEnable = TRUE;
		env->fMainFlareSize = 0.6f;
		env->fLensFlareMaxBrightness = 1.2f;
		env->LensFlareBrightnessColor = D3DXCOLOR(1.0f, 0.6f, 0.3f, 1.0f);
		break;

	case 3: // Overcast
		env->m_fFogNearDistance = 2000.0f;
		env->m_fFogFarDistance = 12000.0f;
		env->bFogLevel = 8;
		env->FogColor = D3DXCOLOR(0.5f, 0.5f, 0.55f, 1.0f);
		env->bFogEnable = TRUE;   // M3-SKY-PRESET-PERSIST-74: Enable fog (ImGui default)
		env->bDensityFog = FALSE; // M3-SKY-PRESET-PRESET-PERSIST-74: Use distance fog, not density fog
		env->bLensFlareEnable = FALSE;
		env->fMainFlareSize = 0.2f;
		env->fLensFlareMaxBrightness = 0.4f;
		env->LensFlareBrightnessColor = D3DXCOLOR(0.7f, 0.7f, 0.75f, 1.0f);
		break;

	default:
		return;
	}

	// M3-SKY-SCALE-PERSIST: Restore saved sky scale (don't overwrite user's manual setting)
	env->v3SkyBoxScale = v3SavedSkyScale;

	// M3-SKY-PRESET-PERSIST-74: Store preset for re-application when environment data changes
	m_bPresetApplied = true;
	m_iLastPresetIndex = iPresetIndex;

	// M3-SKY-PRESET-PERSIST-74: Update ViewDistanceSet to match preset values
	// This prevents SelectViewDistanceNum from overwriting preset fog distances
	m_ViewDistanceSet[m_eViewDistanceNum].m_fFogStart = env->m_fFogNearDistance;
	m_ViewDistanceSet[m_eViewDistanceNum].m_fFogEnd = env->m_fFogFarDistance;

	TraceError("M3_PRESET_AFTER fog_near=%.1f fog_far=%.1f sky_scale=%.1f fog_enable=%d density_fog=%d",
		env->m_fFogNearDistance, env->m_fFogFarDistance, env->v3SkyBoxScale.x,
		env->bFogEnable ? 1 : 0, env->bDensityFog ? 1 : 0);

	// Call ChangeToDay/ChangeToNight like ImGui does
	if (iPresetIndex == 1)
		ChangeToNight();
	else
		ChangeToDay();

	// Apply changes to the rendering system
	if (IsMapOutdoor())
	{
		CMapOutdoor& rkMap = GetMapOutdoorRef();
		rkMap.SetEnvironmentSkyBox();
		rkMap.SetEnvironmentLensFlare();
		rkMap.ApplyEnvironmentDistanceOnly();
		rkMap.SetEnvironmentScreenFilter();

		// M3-SKY-PRESET-PERSIST-74: Update DX11 bridge with new fog distances
		rkMap.UpdateDX11EnvironmentBridgeState(
			m_bSnowEnvironmentEnabled,
			m_iDayMode,
			m_iWeatherMonth,
			m_fRainIntensity);

		TraceError("M3_PRESET_APPLY_DONE preset=%d bridge_updated=1", iPresetIndex);
	}

	// Also forward to ImGui if available
	#ifdef ENABLE_IMGUI_ENVIRONMENT_CONTROLS
	if (ImGuiEnvCtrl() && IsImGuiEnvCtrlEnabled())
	{
		ImGuiEnvCtrl()->ApplyPreset(iPresetIndex);
	}
	#endif

	// M3-SKY-PRESET-PERSIST-74: Reset recursion guard
	s_bInApplyPreset = false;
}

// M3-SKY-PRESET-PERSIST-74: Override to re-apply preset after environment data reset
void CPythonBackground::ResetEnvironmentDataPtr(const TEnvironmentData * c_pEnvironmentData)
{
	TraceError("M3_RESET_ENV_PTR bApplied=%d lastIndex=%d", m_bPresetApplied, m_iLastPresetIndex);

	// Call base class to load new environment data
	CMapManager::ResetEnvironmentDataPtr(c_pEnvironmentData);

	if (mc_pcurEnvironmentData)
	{
		TEnvironmentData* env = ((TEnvironmentData*)mc_pcurEnvironmentData);
		TraceError("M3_RESET_ENV_AFTER_LOAD fog_near=%.1f fog_far=%.1f sky_scale=%.1f",
			env->m_fFogNearDistance, env->m_fFogFarDistance, env->v3SkyBoxScale.x);
	}

	// Re-apply preset if one was previously applied
	if (m_bPresetApplied && m_iLastPresetIndex >= 0)
	{
		TraceError("M3_RESET_ENV_REAPPLY preset=%d", m_iLastPresetIndex);
		ApplyEnvironmentPreset(m_iLastPresetIndex);
	}
	else
	{
		TraceError("M3_RESET_ENV_NO_REAPPLY bApplied=%d lastIndex=%d",
			m_bPresetApplied, m_iLastPresetIndex);
	}
}








