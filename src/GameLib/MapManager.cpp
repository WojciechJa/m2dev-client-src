#include "StdAfx.h"
#include "EterLib/StateManager11.h"
#include "EterLib/GrpDeviceDX11.h"
#include "PackLib/PackManager.h"

#include "MapManager.h"
#include "MapOutdoor.h"

#include "PropertyLoader.h"

// MR-14: Fog update by Alaric
// Not the proper way to handle this but I'm lazy
#ifdef _DEBUG
	#undef _DEBUG
	#include <python/python.h>
	#define _DEBUG
#else
	#include <python/python.h>
#endif

#include "UserInterface/PythonSystem.h"
#include "UserInterface/config.h"
// MR-14: -- END OF -- Fog update by Alaric

namespace
{
inline CStateManager11* GetDX11StateManager()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return nullptr;
	return CStateManager11::InstancePtr();
}

inline DWORD MakeEnvironmentVersionKey(const TEnvironmentData* pEnvironmentData)
{
	return static_cast<DWORD>(reinterpret_cast<uintptr_t>(pEnvironmentData) & 0xFFFFFFFFu);
}

inline int ClampFogLevel(int iFogLevel)
{
	if (iFogLevel < 0)
		return 0;
	if (iFogLevel > 2)
		return 2;
	return iFogLevel;
}

inline float GetLinearFogScaleByLevel(int iFogLevel)
{
	static constexpr float kFogScaleByLevel[3] =
	{
		DX11RuntimeConfig::kFogLinearScaleLight,
		DX11RuntimeConfig::kFogLinearScaleMiddle,
		DX11RuntimeConfig::kFogLinearScaleDense
	};
	return kFogScaleByLevel[ClampFogLevel(iFogLevel)];
}

inline float GetDensityFogBaseByLevel(int iFogLevel)
{
	static constexpr float kFogDensityByLevel[3] =
	{
		DX11RuntimeConfig::kFogDensityLight,
		DX11RuntimeConfig::kFogDensityMiddle,
		DX11RuntimeConfig::kFogDensityDense
	};
	return kFogDensityByLevel[ClampFogLevel(iFogLevel)];
}

inline float ClampFogNearDistance(float fFogNear)
{
	return fMAX(DX11RuntimeConfig::kFogNearMinDistance, fFogNear);
}

inline float ClampFogFarDistance(float fFogFar)
{
	return fMAX(
		DX11RuntimeConfig::kFogFarMinDistance,
		fMIN(DX11RuntimeConfig::kFogFarMaxDistance, fFogFar));
}
}

//////////////////////////////////////////////////////////////////////////
// 기본 함수
//////////////////////////////////////////////////////////////////////////

bool CMapManager::IsMapOutdoor()
{
	if (m_pkMap)
		return true;

	return false;
}

CMapOutdoor& CMapManager::GetMapOutdoorRef()
{
	assert(NULL!=m_pkMap);
	return *m_pkMap;
}


CMapManager::CMapManager() : mc_pcurEnvironmentData(NULL)
{
	m_pkMap = NULL;

//	Initialize();
}

CMapManager::~CMapManager()
{
	Destroy();
}

void CMapManager::Initialize()
{
	mc_pcurEnvironmentData = NULL;
	__LoadMapInfoVector();
}

void CMapManager::Create()
{
	assert(NULL==m_pkMap && "CMapManager::Create");
	if (m_pkMap)
	{
		Clear();
		return;
	}

	m_pkMap = (CMapOutdoor*)AllocMap();

	assert(NULL!=m_pkMap && "CMapManager::Create MAP is NULL");
		
}

void CMapManager::Destroy()
{
	stl_wipe_second(m_EnvironmentDataMap);

	if (m_pkMap)
	{
		m_pkMap->Clear();
		delete m_pkMap;
		m_pkMap = NULL;
	}
}

void CMapManager::Clear()
{
	if (m_pkMap)
		m_pkMap->Clear();
}

CMapBase * CMapManager::AllocMap()
{
	return new CMapOutdoor;
}

//////////////////////////////////////////////////////////////////////////
// Map
//////////////////////////////////////////////////////////////////////////
void CMapManager::LoadProperty()
{
	CPropertyLoader PropertyLoader;
	PropertyLoader.SetPropertyManager(&m_PropertyManager);
	PropertyLoader.Create("*.*", "Property");
}

bool CMapManager::LoadMap(const std::string & c_rstrMapName, float x, float y, float z)
{
	CMapOutdoor& rkMap = GetMapOutdoorRef();

	rkMap.Leave();
	rkMap.SetName(c_rstrMapName);
	rkMap.LoadProperty();

	if ( CMapBase::MAPTYPE_INDOOR == rkMap.GetType())
	{
		TraceError("CMapManager::LoadMap() Indoor Map Load Failed");
		return false;
	}
	else if (CMapBase::MAPTYPE_OUTDOOR == rkMap.GetType())
	{
		if (!rkMap.Load(x, y, z))
		{
			TraceError("CMapManager::LoadMap() Outdoor Map Load Failed");
			return false;
		}

		RegisterEnvironmentData(0, rkMap.GetEnvironmentDataName().c_str());
		
		SetEnvironmentData(0);
	}
	else
	{
		TraceError("CMapManager::LoadMap() Invalid Map Type");
		return false;
	}

	rkMap.Enter();
	return true;
}

bool CMapManager::IsMapReady()
{
	if (!m_pkMap)
		return false;

	return m_pkMap->IsReady();
}

bool CMapManager::UnloadMap(const std::string c_strMapName)
{
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	if (c_strMapName != rkMap.GetName() && "" != rkMap.GetName())
	{
		LogBoxf("%s: Unload Map Failed", c_strMapName.c_str());
		return false;
	}

	Clear();
	return true;
}

bool CMapManager::UpdateMap(float fx, float fy, float fz)
{
	if (!m_pkMap)
		return false;
	
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.Update(fx, -fy, fz);
}

void CMapManager::UpdateAroundAmbience(float fx, float fy, float fz)
{
	if (!m_pkMap)
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.UpdateAroundAmbience(fx, -fy, fz);
}

float CMapManager::GetHeight(float fx, float fy)
{
	if (!m_pkMap)
	{
		TraceError("CMapManager::GetHeight(%f, %f) - 맵이 생성되지 않은 상태에서 접근", fx, fy);
		return 0.0f;
	}
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetHeight(fx, fy);
}

float CMapManager::GetTerrainHeight(float fx, float fy)
{
	if (!m_pkMap)
	{
		TraceError("CMapManager::GetTerrainHeight(%f, %f) - 맵이 생성되지 않은 상태에서 접근", fx, fy);
		return 0.0f;
	}
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetTerrainHeight(fx, fy);
}

bool CMapManager::GetWaterHeight(int iX, int iY, long * plWaterHeight)
{
	if (!m_pkMap)
	{
		TraceError("CMapManager::GetTerrainHeight(%f, %f) - 맵이 생성되지 않은 상태에서 접근", iX, iY);
		return false;
	}

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetWaterHeight(iX, iY, plWaterHeight);
}

//////////////////////////////////////////////////////////////////////////
// Environment
//////////////////////////////////////////////////////////////////////////
void CMapManager::BeginEnvironment()
{
	if (!m_pkMap)
		return;

	if (!mc_pcurEnvironmentData)
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();

	// Native DX11 runtime path: mirror environment state through CStateManager11.
	if (CStateManager11* pStateManager11 = GetDX11StateManager())
	{
		// Light always on
		pStateManager11->SaveLightingEnabled(TRUE);

		// Fog
		pStateManager11->SaveFogEnabled(mc_pcurEnvironmentData->bFogEnable ? TRUE : FALSE);

		// Material
		pStateManager11->SetMaterial(&mc_pcurEnvironmentData->Material);

		// Directional Light
		if (mc_pcurEnvironmentData->bDirLightsEnable[ENV_DIRLIGHT_BACKGROUND])
		{
			pStateManager11->SetLightEnable(0, TRUE);

			rkMap.ApplyLight(MakeEnvironmentVersionKey(mc_pcurEnvironmentData), mc_pcurEnvironmentData->DirLights[ENV_DIRLIGHT_BACKGROUND]);		
		}
		else
			pStateManager11->SetLightEnable(0, FALSE);

		if (mc_pcurEnvironmentData->bFogEnable)
		{
			const DWORD dwFogColor = mc_pcurEnvironmentData->FogColor;
			pStateManager11->SetFogColorValue(dwFogColor);

			const int iFogLevel = ClampFogLevel(CPythonSystem::Instance().GetFogLevel()); // 0=Light,1=Middle,2=Dense

			if (mc_pcurEnvironmentData->bDensityFog && (mc_pcurEnvironmentData->bFogLevel != 0))
			{
				float fDensity = mc_pcurEnvironmentData->bFogLevel * GetDensityFogBaseByLevel(iFogLevel);

				pStateManager11->SetFogModeExp();
				pStateManager11->SetFogExpDensity(fDensity);

				float fApproxFogFar = DX11RuntimeConfig::kFogDensityFarApproxFactor / fMAX(0.0000001f, fDensity);
				fApproxFogFar = ClampFogFarDistance(fApproxFogFar);
				CSpeedTreeForestDirectX& rkForest = CSpeedTreeForestDirectX::Instance();
				rkForest.SetFog(0.0f, fApproxFogFar);
			}
			else
			{
				float fFogNear = mc_pcurEnvironmentData->GetFogNearDistance();
				float fFogFar = mc_pcurEnvironmentData->GetFogFarDistance();
				const float fFogScale = GetLinearFogScaleByLevel(iFogLevel);

				fFogNear = ClampFogNearDistance(fFogNear * fFogScale);
				fFogFar = ClampFogFarDistance(fFogFar * fFogScale);
				if (fFogFar <= fFogNear)
					fFogFar = ClampFogFarDistance(fFogNear + 1000.0f);

				CSpeedTreeForestDirectX& rkForest=CSpeedTreeForestDirectX::Instance();
				rkForest.SetFog(fFogNear, fFogFar);

				pStateManager11->SetFogModeLinear();
				pStateManager11->SetFogRangeEnabled(TRUE);
				pStateManager11->SetFogLinearRange(fFogNear, fFogFar);
			}
		}
	}
	else
	{
		static bool s_bLoggedDX11EnvStateMissing = false;
		if (!s_bLoggedDX11EnvStateMissing)
		{
			s_bLoggedDX11EnvStateMissing = true;
			TraceError("DX11_ENV_BIND_FAIL stage=begin reason=state_manager11_unavailable");
		}
	}

	rkMap.OnBeginEnvironment();
}

void CMapManager::EndEnvironment()
{
	if (!mc_pcurEnvironmentData)
		return;

	if (CStateManager11* pStateManager11 = GetDX11StateManager())
	{
		pStateManager11->RestoreLightingEnabled();
		pStateManager11->RestoreFogEnabled();
		return;
	}

	static bool s_bLoggedDX11EnvStateEndMissing = false;
	if (!s_bLoggedDX11EnvStateEndMissing)
	{
		s_bLoggedDX11EnvStateEndMissing = true;
		TraceError("DX11_ENV_BIND_FAIL stage=end reason=state_manager11_unavailable");
	}
}

void CMapManager::SetEnvironmentData(int nEnvDataIndex)
{
	const TEnvironmentData * c_pEnvironmenData;
	
	if (GetEnvironmentData(nEnvDataIndex, &c_pEnvironmenData))
		SetEnvironmentDataPtr(c_pEnvironmenData);
}

void CMapManager::SetEnvironmentDataPtr(const TEnvironmentData * c_pEnvironmentData)
{
	if (!m_pkMap)
		return;
	
	if (!c_pEnvironmentData)
	{
		assert(!"null environment data");
		TraceError("null environment data");
		return;
	}

	CMapOutdoor& rkMap=GetMapOutdoorRef();


	mc_pcurEnvironmentData = c_pEnvironmentData;

	
	rkMap.SetEnvironmentDataPtr(mc_pcurEnvironmentData);
}

void CMapManager::ResetEnvironmentDataPtr(const TEnvironmentData * c_pEnvironmentData)
{
	if (!m_pkMap)
		return;

	if (!c_pEnvironmentData)
	{
		assert(!"null environment data");
		TraceError("null environment data");
		return;
	}

	CMapOutdoor& rkMap=GetMapOutdoorRef();

	mc_pcurEnvironmentData = c_pEnvironmentData;
	rkMap.ResetEnvironmentDataPtr(mc_pcurEnvironmentData);
}

void CMapManager::BlendEnvironmentData(const TEnvironmentData * c_pEnvironmentData, int iTransitionTime)
{
}

bool CMapManager::RegisterEnvironmentData(DWORD dwIndex, const char * c_szFileName)
{
	TEnvironmentData * pEnvironmentData = AllocEnvironmentData();

	if (!LoadEnvironmentData(c_szFileName, pEnvironmentData))
	{
		DeleteEnvironmentData(pEnvironmentData);
		return false;
	}

	TEnvironmentDataMap::iterator f=m_EnvironmentDataMap.find(dwIndex);
	if (m_EnvironmentDataMap.end()==f)
	{
		m_EnvironmentDataMap.insert(TEnvironmentDataMap::value_type(dwIndex, pEnvironmentData));
	}
	else
	{
		delete f->second;
		f->second=pEnvironmentData;
	}
	return true;
}

void CMapManager::GetCurrentEnvironmentData(const TEnvironmentData ** c_ppEnvironmentData)
{
	*c_ppEnvironmentData = mc_pcurEnvironmentData;
}

bool CMapManager::GetEnvironmentData(DWORD dwIndex, const TEnvironmentData ** c_ppEnvironmentData)
{
	TEnvironmentDataMap::iterator itor = m_EnvironmentDataMap.find(dwIndex);

	if (m_EnvironmentDataMap.end() == itor)
	{
		*c_ppEnvironmentData = NULL;
		return false;
	}

	*c_ppEnvironmentData = itor->second;
	return true;
}

void CMapManager::RefreshPortal()
{
	if (!IsMapReady())
		return;

	CMapOutdoor & rMap = GetMapOutdoorRef();
	for (int i = 0; i < AROUND_AREA_NUM; ++i)
	{
		CArea * pArea;
		if (!rMap.GetAreaPointer(i, &pArea))
			continue;

		pArea->RefreshPortal();
	}
}

void CMapManager::ClearPortal()
{
	if (!IsMapReady())
		return;

	CMapOutdoor & rMap = GetMapOutdoorRef();
	for (int i = 0; i < AROUND_AREA_NUM; ++i)
	{
		CArea * pArea;
		if (!rMap.GetAreaPointer(i, &pArea))
			continue;

		pArea->ClearPortal();
	}
}

void CMapManager::AddShowingPortalID(int iID)
{
	if (!IsMapReady())
		return;

	CMapOutdoor & rMap = GetMapOutdoorRef();
	for (int i = 0; i < AROUND_AREA_NUM; ++i)
	{
		CArea * pArea;
		if (!rMap.GetAreaPointer(i, &pArea))
			continue;

		pArea->AddShowingPortalID(iID);
	}
}

TEnvironmentData * CMapManager::AllocEnvironmentData()
{
	TEnvironmentData * pEnvironmentData = new TEnvironmentData;
	Environment_Init(*pEnvironmentData);
	return pEnvironmentData;
}

void CMapManager::DeleteEnvironmentData(TEnvironmentData * pEnvironmentData)
{
	delete pEnvironmentData;
	pEnvironmentData = NULL;
}

BOOL CMapManager::LoadEnvironmentData(const char * c_szFileName, TEnvironmentData * pEnvironmentData)
{
	if (!pEnvironmentData)
		return FALSE;

	return (BOOL)Environment_Load(*pEnvironmentData, c_szFileName);
}

DWORD CMapManager::GetShadowMapColor(float fx, float fy)
{
	if (!IsMapReady())
		return 0xFFFFFFFF;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetShadowMapColor(fx, fy);
}

std::vector<int> & CMapManager::GetRenderedSplatNum(int * piPatch, int * piSplat, float * pfSplatRatio)
{
	if (!m_pkMap)
	{
		static std::vector<int> s_emptyVector;
		*piPatch = 0;
		*piSplat = 0;
		return s_emptyVector;
	}
	
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetRenderedSplatNum(piPatch, piSplat, pfSplatRatio);
}

CArea::TCRCWithNumberVector & CMapManager::GetRenderedGraphicThingInstanceNum(DWORD * pdwGraphicThingInstanceNum, DWORD * pdwCRCNum)
{
	if (!m_pkMap)
	{
		static CArea::TCRCWithNumberVector s_emptyVector;
		*pdwGraphicThingInstanceNum = 0;
		*pdwCRCNum = 0;
		return s_emptyVector;
	}

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetRenderedGraphicThingInstanceNum(pdwGraphicThingInstanceNum, pdwCRCNum);
}

bool CMapManager::GetNormal(int ix, int iy, D3DXVECTOR3 * pv3Normal)
{
	if (!IsMapReady())
		return false;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetNormal(ix, iy, pv3Normal);
}

bool CMapManager::isPhysicalCollision(const D3DXVECTOR3 & c_rvCheckPosition)
{
	if (!IsMapReady())
		return false;
	
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.isAttrOn(c_rvCheckPosition.x, -c_rvCheckPosition.y, CTerrainImpl::ATTRIBUTE_BLOCK);
}

bool CMapManager::isAttrOn(float fX, float fY, BYTE byAttr)
{
	if (!IsMapReady())
		return false;
	
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.isAttrOn(fX, fY, byAttr);
}

bool CMapManager::GetAttr(float fX, float fY, BYTE * pbyAttr)
{
	if (!IsMapReady())
		return false;
	
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetAttr(fX, fY, pbyAttr);
}

bool CMapManager::isAttrOn(int iX, int iY, BYTE byAttr)
{
	if (!IsMapReady())
		return false;
	
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.isAttrOn(iX, iY, byAttr);
}

bool CMapManager::GetAttr(int iX, int iY, BYTE * pbyAttr)
{
	if (!IsMapReady())
		return false;
	
	CMapOutdoor& rkMap=GetMapOutdoorRef();
	return rkMap.GetAttr(iX, iY, pbyAttr);
}

void CMapManager::SetTransparentTree(bool bTransparenTree)
{
	if (!IsMapReady())
		return;

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.SetTransparentTree(bTransparenTree);
}

void CMapManager::GetBaseXY(DWORD * pdwBaseX, DWORD * pdwBaseY)
{
	if (!IsMapReady())
	{
		*pdwBaseX = 0;
		*pdwBaseY = 0;
	}

	CMapOutdoor& rkMap=GetMapOutdoorRef();
	rkMap.GetBaseXY(pdwBaseX, pdwBaseY);
}

void CMapManager::__LoadMapInfoVector()
{
	TPackFile kFile;
	if (!CPackManager::Instance().GetFile(m_stAtlasInfoFileName, kFile))
		if (!CPackManager::Instance().GetFile("AtlasInfo.txt", kFile))
			return;

	CMemoryTextFileLoader textFileLoader;
	textFileLoader.Bind(kFile.size(), kFile.data());

	char szMapName[256];
	int x, y;
	int width, height;
	for (UINT uLineIndex=0; uLineIndex<textFileLoader.GetLineCount(); ++uLineIndex)
	{
		const std::string& c_rstLine=textFileLoader.GetLineString(uLineIndex);
		sscanf(c_rstLine.c_str(), "%s %d %d %d %d", 
			szMapName, 
			&x, &y, &width, &height);

		if ('\0'==szMapName[0])
			continue;

		TMapInfo kMapInfo;
		kMapInfo.m_strName = szMapName;
		kMapInfo.m_dwBaseX = x;
		kMapInfo.m_dwBaseY = y;

		kMapInfo.m_dwSizeX = width;
		kMapInfo.m_dwSizeY = height;

		kMapInfo.m_dwEndX = kMapInfo.m_dwBaseX + kMapInfo.m_dwSizeX * CTerrainImpl::TERRAIN_XSIZE;
		kMapInfo.m_dwEndY = kMapInfo.m_dwBaseY + kMapInfo.m_dwSizeY * CTerrainImpl::TERRAIN_YSIZE;

		m_kVct_kMapInfo.push_back(kMapInfo);
	}

	return;
}

