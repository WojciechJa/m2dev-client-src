#pragma once

#include "StdAfx.h"
#include "EffectInstance.h"

class CEffectManager : public CScreen, public CSingleton<CEffectManager>
{
	public:
		enum EEffectType
		{
			EFFECT_TYPE_NONE				= 0,
			EFFECT_TYPE_PARTICLE			= 1,
			EFFECT_TYPE_ANIMATION_TEXTURE	= 2,
			EFFECT_TYPE_MESH				= 3,
			EFFECT_TYPE_SIMPLE_LIGHT		= 4,

			EFFECT_TYPE_MAX_NUM				= 4,
		};

		typedef std::map<DWORD, CEffectData*> TEffectDataMap;
		typedef std::map<DWORD, CEffectInstance*> TEffectInstanceMap;

	public:
		CEffectManager();
		virtual ~CEffectManager();

		void Destroy();

		void UpdateSound();
		void Update();
		void Render();
		void SetPerformanceSettings(int iProfile, bool bAdaptiveFX, bool bOverBudgetReduced, int iStrideBias = 1);
		DWORD GetActiveEffectCount() const;
		DWORD GetActiveParticleCount() const;

		void GetInfo(std::string* pstInfo);

		bool IsAliveEffect(DWORD dwInstanceIndex);

		// Register
		BOOL RegisterEffect(const char * c_szFileName,bool isExistDelete=false,bool isNeedCache=false);
		BOOL RegisterEffect2(const char * c_szFileName, DWORD* pdwRetCRC, bool isNeedCache=false);

		void DeleteAllInstances();

		// Usage
		int CreateEffect(DWORD dwID, const D3DXVECTOR3 & c_rv3Position, const D3DXVECTOR3 & c_rv3Rotation);
		int CreateEffect(const char * c_szFileName, const D3DXVECTOR3 & c_rv3Position, const D3DXVECTOR3 & c_rv3Rotation);

		void CreateEffectInstance(DWORD dwInstanceIndex, DWORD dwID);
		BOOL SelectEffectInstance(DWORD dwInstanceIndex);
		bool DestroyEffectInstance(DWORD dwInstanceIndex);
		void DeactiveEffectInstance(DWORD dwInstanceIndex);

		void SetEffectTextures(DWORD dwID, std::vector<std::string> textures);
		void SetEffectInstancePosition(const D3DXVECTOR3 & c_rv3Position);
		void SetEffectInstanceRotation(const D3DXVECTOR3 & c_rv3Rotation);
		void SetEffectInstanceGlobalMatrix(const D3DXMATRIX & c_rmatGlobal);

		void ShowEffect();
		void HideEffect();

		void ApplyAlwaysHidden();
		void ReleaseAlwaysHidden();

		// Temporary function
		DWORD GetRandomEffect();
		int GetEmptyIndex();
		bool GetEffectData(DWORD dwID, CEffectData ** ppEffect);
		bool GetEffectData(DWORD dwID, const CEffectData ** c_ppEffect);

		// Area에 직접 찍는 Effect용 함수... EffectInstance의 Pointer를 반환한다.
		// EffectManager 내부 EffectInstanceMap을 이용하지 않는다.
		void CreateUnsafeEffectInstance(DWORD dwEffectDataID, CEffectInstance ** ppEffectInstance);
		bool DestroyUnsafeEffectInstance(CEffectInstance * pEffectInstance);

		int GetRenderingEffectCount();

		// Return the CRC of the effect-data for the selected effect instance.
		DWORD GetSelectedEffectDataCRC() const;

	protected:
		bool IsHighPriorityFX(const CEffectInstance* pEffectInstance) const;
		static std::string NormalizeEffectPath(const char* cszFile);

		void __Initialize();

		void __DestroyEffectInstanceMap();
		void __DestroyEffectCacheMap();
		void __DestroyEffectDataMap();

	protected:
		bool m_isDisableSortRendering;
		int m_iPerfProfile;
		bool m_bAdaptiveFX;
		bool m_bOverBudgetReduced;
		int m_iFXStrideBias;
		DWORD m_dwUpdateFrame;
		DWORD m_dwRenderFrame;
		DWORD m_dwSortInterval;
		bool m_bSortDirty;
		std::vector<CEffectInstance*> m_kVctSortedCache;
		TEffectDataMap					m_kEftDataMap;
		TEffectInstanceMap				m_kEftInstMap;
		TEffectInstanceMap				m_kEftCacheMap;

		CEffectInstance *				m_pSelectedEffectInstance;
};
