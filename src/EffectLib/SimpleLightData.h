#pragma once

#include "EterLib/DirectXMathHelpers.h"

#include "EterLib/LightDesc.h"
#include "EterLib/TextFileLoader.h"

#include "Type.h"
#include "EffectElementBase.h"

class CLightData : public CEffectElementBase
{
	friend class CLightInstance;
	public:
		CLightData();
		virtual ~CLightData();

		void GetRange(float fTime, float& rRange);
		float GetDuration();
		BOOL isLoop()
		{
			return m_bLoopFlag;
		}
		int GetLoopCount()
		{
			return m_iLoopCount;
		}
		void InitializeLight(SLightDesc& light);

	protected:
		void OnClear();
		bool OnIsData();

		BOOL OnLoadScript(CTextFileLoader & rTextFileLoader);

	protected:
		float m_fMaxRange;
		float m_fDuration;
		TTimeEventTableFloat m_TimeEventTableRange;
		
		D3DXCOLOR m_cAmbient;
		D3DXCOLOR m_cDiffuse;
		D3DXVECTOR3 m_vDirection;
		ELightDescType m_eLightType;

		BOOL m_bLoopFlag;
		int m_iLoopCount;

		float m_fAttenuation0;
		float m_fAttenuation1;
		float m_fAttenuation2;
		float m_fFalloff;
		float m_fTheta;
		float m_fPhi;

	public:
		static void DestroySystem();

		static CLightData* New();
		static void Delete(CLightData* pkData);

		static CDynamicPool<CLightData>		ms_kPool;
};

