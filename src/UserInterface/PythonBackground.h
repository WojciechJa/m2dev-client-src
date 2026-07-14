// PythonBackground.h: interface for the CPythonBackground class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PYTHONBACKGROUND_H__A202CB18_9553_4CF3_8500_5D7062B55432__INCLUDED_)
#define AFX_PYTHONBACKGROUND_H__A202CB18_9553_4CF3_8500_5D7062B55432__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "GameLib/MapManager.h"
#include "GameLib/TerrainDecal.h"
#include "GameLib/SnowEnvironment.h"
#include "GameLib/RainEnvironment.h"
#include "GameLib/StormEnvironment.h"
#include <cstdint>

class CInstanceBase;
struct ID3D11Device;
struct ID3D11DeviceContext;

class CPythonBackground : public CMapManager, public CSingleton<CPythonBackground>  
{
public:
	struct SDX11WorldSubmitTelemetry
	{
		int iTerrainPatches;
		int iTerrainSplats;
		int iWaterPatches;
		uint32_t dwObjectSubmitted;
		uint32_t dwEffectSubmitted;
		uint32_t dwEffectParticleSubmitted;
		uint32_t dwEffectMeshSubmitted;
		uint32_t dwSpeedTreeSubmitted;
		uint32_t dwObservedMask;
		uint32_t dwSubmittedMask;
		uint32_t dwApplicableMask;
		uint32_t dwCommittedMask;

		SDX11WorldSubmitTelemetry()
			: iTerrainPatches(0)
			, iTerrainSplats(0)
			, iWaterPatches(0)
			, dwObjectSubmitted(0u)
			, dwEffectSubmitted(0u)
			, dwEffectParticleSubmitted(0u)
			, dwEffectMeshSubmitted(0u)
			, dwSpeedTreeSubmitted(0u)
			, dwObservedMask(0u)
			, dwSubmittedMask(0u)
			, dwApplicableMask(0u)
			, dwCommittedMask(0u)
		{
		}
	};

	enum
	{
		SHADOW_NONE,
		SHADOW_GROUND,
		SHADOW_GROUND_AND_SOLO,
		SHADOW_ALL,
		SHADOW_ALL_HIGH,
		SHADOW_ALL_MAX,
	};

	enum
	{
		DISTANCE0,
		DISTANCE1,
		DISTANCE2,
		DISTANCE3,
		DISTANCE4,
		NUM_DISTANCE_SET
	};

	enum
	{
		DAY_MODE_LIGHT,
		DAY_MODE_DARK,
	};

	typedef struct SVIEWDISTANCESET
	{
		float m_fFogStart;
		float m_fFogEnd;
		float m_fFarClip;
		DirectX::SimpleMath::Vector3 m_v3SkyBoxScale;
	} TVIEWDISTANCESET;

public:
	CPythonBackground();
	virtual ~CPythonBackground();

	void Initialize();

	void Destroy();
	void Create();

	void GlobalPositionToLocalPosition(int32_t& rGlobalX, int32_t& rGlobalY);
	void LocalPositionToGlobalPosition(int32_t& rLocalX, int32_t& rLocalY);

	void GlobalPositionToLocalPosition(uint32_t& rGlobalX, uint32_t& rGlobalY);
	void LocalPositionToGlobalPosition(uint32_t& rLocalX, uint32_t& rLocalY);

	void EnableTerrainOnlyForHeight();
	bool SetSplatLimit(int iSplatNum);
	bool SetVisiblePart(int ePart, bool isVisible);
	bool SetShadowLevel(int eLevel);
	void RefreshShadowLevel();
	void SetDrawShadow(bool bEnable);
	void SetDrawCharacterShadow(bool bEnable);
	// STP path removed in GameLib; compatibility API kept for UI/python callers.
	bool IsSoftwareTilingEnable() const;
	void ReserveSoftwareTilingEnable(bool isEnable);
	void SelectViewDistanceNum(int eNum);
	void SetViewDistanceSet(int eNum, float fFarClip);
	float GetFarClip();

	DWORD GetRenderShadowTime();
	void GetDistanceSetInfo(int * peNum, float * pfStart, float * pfEnd, float * pfFarClip);

	bool GetPickingPoint(DirectX::SimpleMath::Vector3 * v3IntersectPt);
	bool GetPickingPointWithRay(const CRay & rRay, DirectX::SimpleMath::Vector3 * v3IntersectPt);
	bool GetPickingPointWithRayOnlyTerrain(const CRay & rRay, DirectX::SimpleMath::Vector3 * v3IntersectPt);
	BOOL GetLightDirection(DirectX::SimpleMath::Vector3 & rv3LightDirection);

	void Update(float fCenterX, float fCenterY, float fCenterZ);

	void CreateCharacterShadowTexture();
	void ReleaseCharacterShadowTexture();
	void Render();
	bool RenderTerrainDX11(
		ID3D11Device* pDevice,
		ID3D11DeviceContext* pContext,
		uint32_t* pdwOutWorldPortMaskObserved = nullptr,
		uint32_t* pdwOutWorldPortMaskSubmitted = nullptr,
		uint32_t* pdwOutWorldPortMaskApplicable = nullptr);
	void SetDX11WorldSubmitCommittedMask(uint32_t dwCommittedMask);
	const SDX11WorldSubmitTelemetry& GetDX11WorldSubmitTelemetry() const { return m_kDX11WorldSubmitTelemetry; }
	void RenderSnow();
	void RenderRain();
	void RenderStorm();
	void RenderPCBlocker();
	void RenderCollision();
	void RenderCharacterShadowToTexture();
	void RenderSky();
	void RenderCloud();
	void RenderWater();
	void RenderEffect();
	void RenderBeforeLensFlare();
	void RenderAfterLensFlare();
	void RenderScreenFiltering();  // M3-SCREEN-FILTER-FIX: Screen overlay tint for night/atmosphere

	bool CheckAdvancing(CInstanceBase * pInstance);

	void SetCharacterDirLight();
	void SetBackgroundDirLight();

	void ChangeToDay();
	void ChangeToNight();
	void EnableSnowEnvironment();
	void DisableSnowEnvironment();
	void EnableRainEnvironment();
	void DisableRainEnvironment();
	void EnableStormEnvironment();
	void DisableStormEnvironment();

	// Public getters for weather environments (for DebugUI)
	class CSnowEnvironment* GetSnowEnvironment();
	class CRainEnvironment* GetRainEnvironment();
	class CStormEnvironment* GetStormEnvironment();

	void SetWeatherMonth(int iMonth);
	int GetWeatherMonth() const { return m_iWeatherMonth; }
	void SetRainIntensity(float fIntensity);
	float GetRainIntensity() const { return m_fRainIntensity; }
	void SetXMaxTree(int iGrade);

	// Sun position control
	void SetSunDirection(float x, float y, float z);
	void SetSunAzimuth(float fAzimuthDegrees);
	void SetSunElevation(float fElevationDegrees);
	void GetSunDirection(float& x, float& y, float& z) const;

	// Environment parameter controls (for Admin Panel TAB7)
	void SetSkyScale(float fScale);
	void SetCloudScale(float fX, float fY);
	void SetCloudHeight(float fHeight);
	void SetCloudTextureScale(float fX, float fY);
	void SetCloudSpeed(float fX, float fY);
	void SetFogEnable(bool bEnable);
	void SetFogDensity(bool bDensity);
	void SetFogNear(float fDistance);
	void SetFogFar(float fDistance);
	void SetFogLevel(BYTE byLevel);
	void SetFogColor(float r, float g, float b, float a);
	void SetLensFlareEnable(bool bEnable);
	void SetMainFlareEnable(bool bEnable);
	void SetSunSize(float fSize);
	void SetSunBrightness(float fBrightness);
	void SetSunColor(float r, float g, float b, float a);
	void SetBGDirectionalLightEnable(bool bEnable);
	void SetCharDirectionalLightEnable(bool bEnable);
	void SetBGLightDirection(float x, float y, float z);
	void SetBGLightAmbient(float r, float g, float b, float a);
	void SetBGLightDiffuse(float r, float g, float b, float a);
	void SetCharLightDirection(float x, float y, float z);
	void SetCharLightAmbient(float r, float g, float b, float a);
	void SetCharLightDiffuse(float r, float g, float b, float a);
	void SetScreenFilterEnable(bool bEnable);
	void SetScreenFilterColor(float r, float g, float b, float a);
	void SetWindStrength(float fStrength);
	void SetWindRandomness(float fRandomness);
	void ApplyEnvironmentPreset(int iPresetIndex);

	// M3-SKY-PRESET-PERSIST-74: Override to re-apply preset after environment data reset
	void ResetEnvironmentDataPtr(const TEnvironmentData * c_pEnvironmentData);

	void ClearGuildArea();
	void RegisterGuildArea(int isx, int isy, int iex, int iey);

	void CreateTargetEffect(DWORD dwID, DWORD dwChrVID);
	void CreateTargetEffect(DWORD dwID, long lx, long ly);
	void DeleteTargetEffect(DWORD dwID);

	void CreateSpecialEffect(DWORD dwID, float fx, float fy, float fz, const char * c_szFileName);
	void DeleteSpecialEffect(DWORD dwID);

	void Warp(DWORD dwX, DWORD dwY);

	void VisibleGuildArea();
	void DisableGuildArea();

	void RegisterDungeonMapName(const char * c_szMapName);
	TMapInfo* GlobalPositionToMapInfo(DWORD dwGlobalX, DWORD dwGlobalY);
	const char* GetWarpMapName();

protected:
	void __CreateProperty();
	bool __IsSame(std::set<int> & rleft, std::set<int> & rright);
	void __SyncDX11EnvironmentBridgeState();

protected:
	std::string m_strMapName;

private:
	CSnowEnvironment m_SnowEnvironment;
	CRainEnvironment m_RainEnvironment;
	CStormEnvironment m_StormEnvironment;

	int m_iDayMode;
	bool m_bSnowEnvironmentEnabled;
	bool m_bRainEnvironmentEnabled;
	bool m_bStormEnvironmentEnabled;
	int m_iWeatherMonth;
	float m_fRainIntensity;
	int m_iXMasTreeGrade;

	// M3-SKY-PRESET-PERSIST-74: Track last applied preset for re-application
	bool m_bPresetApplied;
	int m_iLastPresetIndex;

	int m_eShadowLevel;
	int m_eViewDistanceNum;

	BOOL m_bVisibleGuildArea;
	bool m_bSoftwareTilingReserved;

	DWORD m_dwRenderShadowTime;

	DWORD m_dwBaseX;
	DWORD m_dwBaseY;

	TVIEWDISTANCESET m_ViewDistanceSet[NUM_DISTANCE_SET];

	std::set<int> m_kSet_iShowingPortalID;
	std::set<std::string> m_kSet_strDungeonMapName;
	std::map<DWORD, DWORD> m_kMap_dwTargetID_dwChrID;
	SDX11WorldSubmitTelemetry m_kDX11WorldSubmitTelemetry;

	struct SReserveTargetEffect
	{
		int ilx;
		int ily;
	};
	std::map<DWORD, SReserveTargetEffect> m_kMap_dwID_kReserveTargetEffect;

	struct FFindWarpMapName
	{
		DWORD m_dwX, m_dwY;
		FFindWarpMapName(DWORD dwX, DWORD dwY)
		{
			m_dwX = dwX;
			m_dwY = dwY;
		}
		bool operator() (TMapInfo & rMapInfo)
		{
			if (m_dwX < rMapInfo.m_dwBaseX || m_dwX >= rMapInfo.m_dwEndX || m_dwY < rMapInfo.m_dwBaseY || m_dwY >= rMapInfo.m_dwEndY)
				return false;
			return true;
		}
	};
};

#endif // !defined(AFX_PYTHONBACKGROUND_H__A202CB18_9553_4CF3_8500_5D7062B55432__INCLUDED_)
