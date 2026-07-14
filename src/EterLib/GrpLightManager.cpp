#include "StdAfx.h"
#include <algorithm>
#include "EterBase/Timer.h"

#include "GrpLightManager.h"
#include "StateManager11.h"

namespace
{
	constexpr DWORD kInvalidLightSlot = 0xFFFFFFFFu;

	DWORD ComputeBindableLightSlotCapacity(DWORD dwSkipIndex)
	{
		if (dwSkipIndex >= static_cast<DWORD>(MAX_LIGHTS))
			return 0u;
		return static_cast<DWORD>(MAX_LIGHTS) - dwSkipIndex;
	}
}

float CLightBase::ms_fCurTime = 0.0f;

CLightManager::CLightManager()
{
	m_v3CenterPosition			= D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	m_dwLimitLightCount			= LIGHT_LIMIT_DEFAULT;
	m_dwSkipIndex				= 1u;
	m_kTelemetry				= SLightTelemetry();
}

CLightManager::~CLightManager()
{
}

void CLightManager::Destroy()
{
	for (TLightMap::iterator itor = m_LightMap.begin(); itor != m_LightMap.end(); ++itor)
	{
		CLight* pLight = itor->second;
		if (pLight)
			pLight->Clear();
	}

	m_LightSortVector.clear();
	m_LightMap.clear();
	m_NonUsingLightIDDeque.clear();
	m_kTelemetry = SLightTelemetry();
	m_LightPool.FreeAll();
	m_LightPool.Destroy();
}

void CLightManager::Initialize()
{
	SetSkipIndex(1);

	for (TLightMap::iterator itor = m_LightMap.begin(); itor != m_LightMap.end(); ++itor)
	{
		CLight* pLight = itor->second;
		if (pLight)
			pLight->Clear();
	}

	m_LightSortVector.clear();
	m_NonUsingLightIDDeque.clear();
	m_LightMap.clear();
	m_kTelemetry = SLightTelemetry();
	m_LightPool.FreeAll();
}

void CLightManager::RegisterLight(ELightType LightType, TLightID * poutLightID, const SLightDesc& LightData)
{
	CLight * pLight = m_LightPool.Alloc();
	TLightID ID = NewLightID();
	pLight->SetParameter(ID, LightType, LightData);
	m_LightMap[ID] = pLight;
	*poutLightID = ID;
}

void CLightManager::DeleteLight(TLightID LightID)
{
	TLightMap::iterator itor = m_LightMap.find(LightID);

	if (m_LightMap.end() == itor)
	{
		static DWORD s_dwLastDeleteMissingLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastDeleteMissingLogMS || (dwNow - s_dwLastDeleteMissingLogMS) >= 2000u)
		{
			s_dwLastDeleteMissingLogMS = dwNow;
			TraceError("DX11_LIGHT_MANAGER delete_skip reason=unknown_light_id id=%u",
				static_cast<unsigned int>(LightID));
		}
		return;
	}

	CLight * pLight = itor->second;

	pLight->Clear();
	m_LightPool.Free(pLight);

	m_LightMap.erase(itor);

	ReleaseLightID(LightID);
}

CLight * CLightManager::GetLight(TLightID LightID)
{
	TLightMap::iterator itor = m_LightMap.find(LightID);

	if (m_LightMap.end() == itor)
	{
		static DWORD s_dwLastGetMissingLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastGetMissingLogMS || (dwNow - s_dwLastGetMissingLogMS) >= 2000u)
		{
			s_dwLastGetMissingLogMS = dwNow;
			TraceError("DX11_LIGHT_MANAGER get_fail reason=unknown_light_id id=%u",
				static_cast<unsigned int>(LightID));
		}
		return NULL;
	}

	return itor->second;
}

void CLightManager::SetCenterPosition(const D3DXVECTOR3 & c_rv3Position)
{
	m_v3CenterPosition = c_rv3Position;
}

void CLightManager::SetLimitLightCount(DWORD dwLightCount)
{
	m_dwLimitLightCount = dwLightCount;
}

void CLightManager::SetSkipIndex(DWORD dwSkipIndex)
{
	m_dwSkipIndex = dwSkipIndex;
}

struct LightComp
{
	bool operator () (const CLight * l, const CLight * r) const
	{
		return l->GetDistance() < r->GetDistance();
	}
};

// NOTE : FlushLight후 렌더링
//        그 후 반드시 RestoreLight를 해줘야만 한다.
void CLightManager::FlushLight()
{
	CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
	if (pStateManager11)
		pStateManager11->BeginLightBatch();

	Update();

	m_LightSortVector.clear();

	// NOTE: Dynamic과 Static을 분리 시키고 CenterPosition이 바뀔때마다 Static만
	//		 다시 Flush 하는 식으로 최적화 할 수 있다. - [levites]

	// light들의 거리를 추출해 정렬한다.
	TLightMap::iterator itor = m_LightMap.begin();
	DWORD dwRegisteredStaticCount = 0u;
	DWORD dwRegisteredDynamicCount = 0u;

	for (; itor != m_LightMap.end(); ++itor)
	{
		CLight * pLight = itor->second;
		if (!pLight)
			continue;

		if (pLight->GetLightType() == LIGHT_TYPE_STATIC)
			++dwRegisteredStaticCount;
		else
			++dwRegisteredDynamicCount;

		D3DXVECTOR3 v3LightPos(pLight->GetPosition());
		D3DXVECTOR3 v3Distance(v3LightPos - m_v3CenterPosition);
		pLight->SetDistance(D3DXVec3Length(&v3Distance));
		m_LightSortVector.push_back(pLight);
	}

	// quick sort lights
	std::sort(m_LightSortVector.begin(), m_LightSortVector.end(), LightComp());

	// NOTE - 거리로 정렬된 라이트를 Limit 갯수 만큼 제한해서 켜준다.
	const DWORD dwRequestedByLimit = static_cast<DWORD>(std::min(static_cast<size_t>(m_dwLimitLightCount), m_LightSortVector.size()));
	const DWORD dwSlotCapacity = ComputeBindableLightSlotCapacity(m_dwSkipIndex);
	const DWORD dwActiveLightCount = std::min(dwRequestedByLimit, dwSlotCapacity);
	const DWORD dwClippedBySlotCapacity = (dwRequestedByLimit > dwActiveLightCount)
		? (dwRequestedByLimit - dwActiveLightCount)
		: 0u;
	DWORD dwActiveStaticCount = 0u;
	DWORD dwActiveDynamicCount = 0u;
	for (DWORD k = 0; k < dwActiveLightCount; ++k)
	{
		if (m_LightSortVector[k]->GetLightType() == LIGHT_TYPE_STATIC)
			++dwActiveStaticCount;
		else
			++dwActiveDynamicCount;

		m_LightSortVector[k]->Update();
		m_LightSortVector[k]->SetDeviceLightSlot(m_dwSkipIndex + k, TRUE);
	}

	m_kTelemetry.dwRegisteredStaticCount = dwRegisteredStaticCount;
	m_kTelemetry.dwRegisteredDynamicCount = dwRegisteredDynamicCount;
	m_kTelemetry.dwActiveStaticCount = dwActiveStaticCount;
	m_kTelemetry.dwActiveDynamicCount = dwActiveDynamicCount;
	m_kTelemetry.dwRequestedActiveCount = dwRequestedByLimit;
	m_kTelemetry.dwBoundActiveCount = dwActiveLightCount;
	m_kTelemetry.dwClippedBySlotCount = dwClippedBySlotCapacity;
	m_kTelemetry.dwSlotCapacity = dwSlotCapacity;
	m_kTelemetry.dwSkipIndex = m_dwSkipIndex;

	static DWORD s_dwLastLightTelemetryMS = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwLastLightTelemetryMS || (dwNow - s_dwLastLightTelemetryMS) >= 5000u)
	{
		s_dwLastLightTelemetryMS = dwNow;
		TraceError(
			"DX11_EFFECT_TELEMETRY type=light_bind count=%u requested=%u clipped_slot=%u slot_capacity=%u skip_index=%u active_static=%u active_dynamic=%u registered_static=%u registered_dynamic=%u",
			static_cast<unsigned int>(dwActiveLightCount),
			static_cast<unsigned int>(dwRequestedByLimit),
			static_cast<unsigned int>(dwClippedBySlotCapacity),
			static_cast<unsigned int>(dwSlotCapacity),
			static_cast<unsigned int>(m_dwSkipIndex),
			static_cast<unsigned int>(dwActiveStaticCount),
			static_cast<unsigned int>(dwActiveDynamicCount),
			static_cast<unsigned int>(dwRegisteredStaticCount),
			static_cast<unsigned int>(dwRegisteredDynamicCount));
	}

	if (dwClippedBySlotCapacity > 0u)
	{
		static DWORD s_dwLastLightSlotClipLogMS = 0u;
		if (0u == s_dwLastLightSlotClipLogMS || (dwNow - s_dwLastLightSlotClipLogMS) >= 2000u)
		{
			s_dwLastLightSlotClipLogMS = dwNow;
			TraceError(
				"DX11_LIGHT_MANAGER slot_clip requested=%u bindable=%u clipped=%u skip_index=%u max_lights=%u",
				static_cast<unsigned int>(dwRequestedByLimit),
				static_cast<unsigned int>(dwSlotCapacity),
				static_cast<unsigned int>(dwClippedBySlotCapacity),
				static_cast<unsigned int>(m_dwSkipIndex),
				static_cast<unsigned int>(MAX_LIGHTS));
		}
	}

	if (pStateManager11)
		pStateManager11->EndLightBatch();
}

void CLightManager::RestoreLight()
{
	CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
	if (pStateManager11)
		pStateManager11->BeginLightBatch();

	const DWORD dwRequestedByLimit = static_cast<DWORD>(std::min(static_cast<size_t>(m_dwLimitLightCount), m_LightSortVector.size()));
	const DWORD dwSlotCapacity = ComputeBindableLightSlotCapacity(m_dwSkipIndex);
	const DWORD dwRestoreCount = std::min(dwRequestedByLimit, dwSlotCapacity);
	for (DWORD k = 0; k < dwRestoreCount; ++k)
		m_LightSortVector[k]->SetDeviceLightSlot(m_dwSkipIndex + k, FALSE);

	if (pStateManager11)
		pStateManager11->EndLightBatch();
}

TLightID CLightManager::NewLightID()
{
	if (!m_NonUsingLightIDDeque.empty())
	{
		TLightID id = m_NonUsingLightIDDeque.back();
		m_NonUsingLightIDDeque.pop_back();
		return (id);
	}

	return m_dwSkipIndex + m_LightMap.size();
}

void CLightManager::ReleaseLightID(TLightID LightID)
{
	m_NonUsingLightIDDeque.push_back(LightID);
}

void CLightManager::Update()
{
	//static DWORD s_dwStartTime = ELTimer_GetMSec();
	//ms_fCurTime = float(ELTimer_GetMSec() - s_dwStartTime) / 1000.0f;
	ms_fCurTime = CTimer::Instance().GetCurrentSecond();
}

//////////////////////////////////////////////////////////////////////////
CLight::CLight()
{
	Initialize();
}

CLight::~CLight()
{
	Clear();
}

void CLight::Initialize()
{
	m_LightID	= 0;
	m_eLightType = LIGHT_TYPE_DYNAMIC;
	m_isEdited	= TRUE;
	m_fDistance	= 0.0f;
	m_dwActiveSlot = kInvalidLightSlot;

	m_kLightDesc = SLightDesc();
}

void CLight::Clear()
{
	if (m_dwActiveSlot != kInvalidLightSlot)
		SetDeviceLightSlot(m_dwActiveSlot, FALSE);
	Initialize();
}

void CLight::SetDeviceLight(BOOL bActive)
{
	if (bActive)
		SetDeviceLightSlot(static_cast<DWORD>(m_LightID), TRUE);
	else
		SetDeviceLightSlot(m_dwActiveSlot, FALSE);
}

void CLight::SetDeviceLightSlot(DWORD dwSlot, BOOL bActive)
{
	CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
	if (!pStateManager11)
	{
		static bool s_bLoggedStateManagerMissing = false;
		if (!s_bLoggedStateManagerMissing)
		{
			s_bLoggedStateManagerMissing = true;
			TraceError("DX11_LIGHT_MANAGER bind_fail reason=state_manager11_unavailable");
		}
		return;
	}

	if (!bActive)
	{
		if (dwSlot == kInvalidLightSlot)
			dwSlot = m_dwActiveSlot;
		if (dwSlot != kInvalidLightSlot && dwSlot < static_cast<DWORD>(MAX_LIGHTS))
			pStateManager11->SetLightEnable(dwSlot, FALSE);
		if (dwSlot == m_dwActiveSlot)
			m_dwActiveSlot = kInvalidLightSlot;
		return;
	}

	if (dwSlot >= static_cast<DWORD>(MAX_LIGHTS))
	{
		static DWORD s_dwLastOutOfRangeLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastOutOfRangeLogMS || (dwNow - s_dwLastOutOfRangeLogMS) >= 5000u)
		{
			s_dwLastOutOfRangeLogMS = dwNow;
			TraceError("DX11_LIGHT_MANAGER bind_skip reason=slot_out_of_range slot=%u light_id=%u max_lights=%u",
				static_cast<unsigned int>(dwSlot),
				static_cast<unsigned int>(m_LightID),
				static_cast<unsigned int>(MAX_LIGHTS));
		}
		return;
	}

	if (m_dwActiveSlot != kInvalidLightSlot && m_dwActiveSlot != dwSlot)
		pStateManager11->SetLightEnable(m_dwActiveSlot, FALSE);

	const bool bNeedsUpload = m_isEdited || (m_dwActiveSlot != dwSlot);
	if (bNeedsUpload)
		pStateManager11->SetLight(dwSlot, &m_kLightDesc);

	pStateManager11->SetLightEnable(dwSlot, TRUE);
	m_dwActiveSlot = dwSlot;
	m_isEdited = FALSE;
}

void CLight::SetParameter(TLightID id, ELightType eLightType, const SLightDesc& c_rLight)
{
	m_LightID	= id;
	m_eLightType = eLightType;
	m_kLightDesc	= c_rLight;
	m_isEdited = TRUE;
}

void CLight::SetDiffuseColor(float fr, float fg, float fb, float fa)
{
	if (m_kLightDesc.Diffuse.r == fr
		&& m_kLightDesc.Diffuse.g == fg
		&& m_kLightDesc.Diffuse.b == fb
		&& m_kLightDesc.Diffuse.a == fa
		)
		return;	
	m_kLightDesc.Diffuse.r = fr;
	m_kLightDesc.Diffuse.g = fg;
	m_kLightDesc.Diffuse.b = fb;
	m_kLightDesc.Diffuse.a = fa;
	m_isEdited = TRUE;
}

void CLight::SetAmbientColor(float fr, float fg, float fb, float fa)
{
	if (m_kLightDesc.Ambient.r == fr
		&& m_kLightDesc.Ambient.g == fg
		&& m_kLightDesc.Ambient.b == fb
		&& m_kLightDesc.Ambient.a == fa
		)
		return;
	m_kLightDesc.Ambient.r = fr;
	m_kLightDesc.Ambient.g = fg;
	m_kLightDesc.Ambient.b = fb;
	m_kLightDesc.Ambient.a = fa;
	m_isEdited = TRUE;
}

void CLight::SetRange(float fRange)
{
	if (m_kLightDesc.Range == fRange)
		return;
	
	m_kLightDesc.Range = fRange;
	m_isEdited = TRUE;
}

const GrpVector & CLight::GetPosition() const
{
	return m_kLightDesc.Position;
}

void CLight::SetPosition(float fx, float fy, float fz)
{
	if (m_kLightDesc.Position.x == fx && m_kLightDesc.Position.y == fy && m_kLightDesc.Position.z == fz)
		return;

	m_kLightDesc.Position.x = fx;
	m_kLightDesc.Position.y = fy;
	m_kLightDesc.Position.z = fz;
	m_isEdited = TRUE;
}

void CLight::SetDirection(float fx, float fy, float fz)
{
	D3DXVECTOR3 vDirection(fx, fy, fz);
	const float fLengthSq = D3DXVec3LengthSq(&vDirection);
	if (fLengthSq <= 0.000001f)
		return;

	D3DXVec3Normalize(&vDirection, &vDirection);
	if (m_kLightDesc.Direction.x == vDirection.x &&
		m_kLightDesc.Direction.y == vDirection.y &&
		m_kLightDesc.Direction.z == vDirection.z)
		return;

	m_kLightDesc.Direction.x = vDirection.x;
	m_kLightDesc.Direction.y = vDirection.y;
	m_kLightDesc.Direction.z = vDirection.z;
	m_isEdited = TRUE;
}

void CLight::SetDistance(float fDistance)
{
	m_fDistance = fDistance;
}

void CLight::BlendDiffuseColor(const D3DXCOLOR & c_rColor, float fBlendTime, float fDelayTime)
{
	D3DXCOLOR Color(m_kLightDesc.Diffuse);
	m_DiffuseColorTransitor.SetTransition(Color, c_rColor, ms_fCurTime + fDelayTime, fBlendTime);
}

void CLight::BlendAmbientColor(const D3DXCOLOR & c_rColor, float fBlendTime, float fDelayTime)
{
	D3DXCOLOR Color(m_kLightDesc.Ambient);
	m_AmbientColorTransitor.SetTransition(Color, c_rColor, ms_fCurTime + fDelayTime, fBlendTime);
}

void CLight::BlendRange(float fRange, float fBlendTime, float fDelayTime)
{
	m_RangeTransitor.SetTransition(m_kLightDesc.Range, fRange, ms_fCurTime + fDelayTime, fBlendTime);
}

void CLight::Update()
{
	if (m_AmbientColorTransitor.isActiveTime(ms_fCurTime))
	{
		if (!m_AmbientColorTransitor.isActive())
		{
			m_AmbientColorTransitor.SetActive();
			m_AmbientColorTransitor.SetSourceValue(m_kLightDesc.Ambient);
		}
		else
		{
			D3DXCOLOR Color;

			m_AmbientColorTransitor.GetValue(ms_fCurTime, &Color);
			SetAmbientColor(Color.r, Color.g, Color.b, Color.a);
		}
	}

	if (m_DiffuseColorTransitor.isActiveTime(ms_fCurTime))
	{
		if (!m_DiffuseColorTransitor.isActive())
		{
			m_DiffuseColorTransitor.SetActive();
			m_DiffuseColorTransitor.SetSourceValue(m_kLightDesc.Diffuse);
		}
		else
		{
			D3DXCOLOR Color;
			m_DiffuseColorTransitor.GetValue(ms_fCurTime, &Color);
			SetDiffuseColor(Color.r, Color.g, Color.b, Color.a);
		}
	}

	if (m_RangeTransitor.isActiveTime(ms_fCurTime))
	{
		if (!m_RangeTransitor.isActive())
		{
			m_RangeTransitor.SetActive();
			m_RangeTransitor.SetSourceValue(m_kLightDesc.Range);
		}
		else
		{
			float fRange;
			m_RangeTransitor.GetValue(ms_fCurTime, &fRange);
			SetRange(fRange);
		}
	}
}
