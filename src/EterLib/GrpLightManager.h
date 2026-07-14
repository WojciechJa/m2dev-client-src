#pragma once

#include "EterBase/Singleton.h"

#include "GrpBase.h"
#include "LightDesc.h"
#include "Util.h"
#include "Pool.h"

#include <deque>

typedef DWORD TLightID;

enum ELightType
{
	LIGHT_TYPE_STATIC,			// Continuously turning on light
	LIGHT_TYPE_DYNAMIC,			// Immediately turning off light
};

class CLightBase
{
	public:
		CLightBase() {};
		virtual ~CLightBase() {};

		void	SetCurrentTime();

	protected:
		static float ms_fCurTime;
};

class CLight : public CGraphicBase, public CLightBase
{
	public:
		CLight();
		virtual ~CLight();

		void		Initialize();
		void		Clear();
		
		void		Update();

		void		SetParameter(TLightID id, ELightType eLightType, const SLightDesc& c_rLight);

		void		SetDistance(float fDistance);
		float		GetDistance() const { return m_fDistance;	}

		TLightID	GetLightID()	{ return m_LightID;		}
		ELightType	GetLightType() const { return m_eLightType; }

		BOOL		isEdited()		{ return m_isEdited;	}
		void		SetDeviceLight(BOOL bActive);
		void		SetDeviceLightSlot(DWORD dwSlot, BOOL bActive);

		void		SetDiffuseColor(float fr, float fg, float fb, float fa = 1.0f);
		void		SetAmbientColor(float fr, float fg, float fb, float fa = 1.0f);
		void		SetRange(float fRange);
		void		SetPosition(float fx, float fy, float fz);
		void		SetDirection(float fx, float fy, float fz);

		const GrpVector & GetPosition() const;

		void		BlendDiffuseColor(const D3DXCOLOR & c_rColor, float fBlendTime, float fDelayTime = 0.0f);
		void		BlendAmbientColor(const D3DXCOLOR & c_rColor, float fBlendTime, float fDelayTime = 0.0f);
		void		BlendRange(float fRange, float fBlendTime, float fDelayTime = 0.0f);

	private:
		TLightID		m_LightID;		// Light ID. equal to D3D light index

		ELightType		m_eLightType;
		SLightDesc		m_kLightDesc;
		BOOL			m_isEdited;
		float			m_fDistance;
		DWORD			m_dwActiveSlot;

		TTransitorColor	m_DiffuseColorTransitor;
		TTransitorColor	m_AmbientColorTransitor;
		TTransitorFloat m_RangeTransitor;
};

class CLightManager : public CGraphicBase, public CLightBase, public CSingleton<CLightManager>
{
	public:
		struct SLightTelemetry
		{
			DWORD dwRegisteredStaticCount;
			DWORD dwRegisteredDynamicCount;
			DWORD dwActiveStaticCount;
			DWORD dwActiveDynamicCount;
			DWORD dwRequestedActiveCount;
			DWORD dwBoundActiveCount;
			DWORD dwClippedBySlotCount;
			DWORD dwSlotCapacity;
			DWORD dwSkipIndex;

			SLightTelemetry()
				: dwRegisteredStaticCount(0u)
				, dwRegisteredDynamicCount(0u)
				, dwActiveStaticCount(0u)
				, dwActiveDynamicCount(0u)
				, dwRequestedActiveCount(0u)
				, dwBoundActiveCount(0u)
				, dwClippedBySlotCount(0u)
				, dwSlotCapacity(0u)
				, dwSkipIndex(0u)
			{
			}
		};

		enum
		{
			LIGHT_LIMIT_DEFAULT = 3,
//			LIGHT_MAX_NUM = 32,
		};

		typedef std::deque<TLightID>			TLightIDDeque;
		typedef std::map<TLightID, CLight *>	TLightMap;
		typedef std::vector<CLight *>			TLightSortVector;

	public:
		CLightManager();
		virtual ~CLightManager();
		
		void		Destroy();

		void		Initialize();

		// NOTE : FlushLight후 렌더링
		//        그 후 반드시 RestoreLight를 해줘야만 한다.
		void		Update();
		void		FlushLight();
		void		RestoreLight();

		/////
		void		RegisterLight(ELightType LightType, TLightID * poutLightID, const SLightDesc& LightData);
		CLight *	GetLight(TLightID LightID);
		void		DeleteLight(TLightID LightID);
		/////

		void		SetCenterPosition(const D3DXVECTOR3 & c_rv3Position);
		void		SetLimitLightCount(DWORD dwLightCount);
		void		SetSkipIndex(DWORD dwSkipIndex);
		const SLightTelemetry& GetTelemetry() const { return m_kTelemetry; }

	protected:
		TLightIDDeque			m_NonUsingLightIDDeque;

		TLightMap				m_LightMap;
		TLightSortVector		m_LightSortVector;

		D3DXVECTOR3				m_v3CenterPosition;
		DWORD					m_dwLimitLightCount;
		DWORD					m_dwSkipIndex;
		SLightTelemetry			m_kTelemetry;

	protected:
		TLightID				NewLightID();
		void					ReleaseLightID(TLightID LightID);

		CDynamicPool<CLight>	m_LightPool;
};
