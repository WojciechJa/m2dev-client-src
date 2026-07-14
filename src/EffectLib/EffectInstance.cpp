#include "StdAfx.h"
#include "EffectInstance.h"
#include "ParticleSystemInstance.h"
#include "SimpleLightInstance.h"
#include "EffectManager.h"

#include "EterBase/Stl.h"
#include "EterLib/GrpDeviceDX11.h"
#include "AudioLib/SoundEngine.h"

CDynamicPool<CEffectInstance>	CEffectInstance::ms_kPool;
int CEffectInstance::ms_iRenderingEffectCount = 0;

bool CEffectInstance::LessRenderOrder(CEffectInstance* pkEftInst)
{
	return (m_pkEftData<pkEftInst->m_pkEftData);	
}

DWORD CEffectInstance::GetActiveParticleCount() const
{
	DWORD dwCount = 0;
	for (std::vector<CParticleSystemInstance*>::const_iterator it = m_ParticleInstanceVector.begin(); it != m_ParticleInstanceVector.end(); ++it)
	{
		dwCount += (*it)->GetEmissionCount();
	}

	return dwCount;
}

void CEffectInstance::ResetRenderingEffectCount()
{
	ms_iRenderingEffectCount = 0;
}

int CEffectInstance::GetRenderingEffectCount()
{
	return ms_iRenderingEffectCount;
}

CEffectInstance* CEffectInstance::New()
{
	CEffectInstance* pkEftInst=ms_kPool.Alloc();
	return pkEftInst;
}

void CEffectInstance::Delete(CEffectInstance* pkEftInst)
{
	pkEftInst->Clear();
	ms_kPool.Free(pkEftInst);
}

void CEffectInstance::DestroySystem()
{
	ms_kPool.Destroy();

	CParticleSystemInstance::DestroySystem();
	CEffectMeshInstance::DestroySystem();
	CLightInstance::DestroySystem();
}

void CEffectInstance::UpdateSound()
{
	if (m_pSoundInstanceVector)
	{
		SoundEngine::Instance().UpdateSoundInstance(m_matGlobal._41,
													m_matGlobal._42,
													m_matGlobal._43,
													m_dwFrame,
													m_pSoundInstanceVector,
													false);
		// NOTE : 매트릭스에서 위치를 직접 얻어온다 - [levites]
	}
	++m_dwFrame;
}

struct FEffectUpdator
{
	BOOL isAlive;
	float fElapsedTime;
	FEffectUpdator(float fElapsedTime)
		: isAlive(FALSE), fElapsedTime(fElapsedTime)
	{
	}
	void operator () (CEffectElementBaseInstance * pInstance)
	{
		if (pInstance->Update(fElapsedTime)) [[likely]]
			isAlive = TRUE;
	}
};

void CEffectInstance::OnUpdate()
{
	Transform();

	FEffectUpdator f(CTimer::Instance().GetCurrentSecond()-m_fLastTime);
	f = std::for_each(m_ParticleInstanceVector.begin(), m_ParticleInstanceVector.end(),f);
	f = std::for_each(m_MeshInstanceVector.begin(), m_MeshInstanceVector.end(),f);
	f = std::for_each(m_LightInstanceVector.begin(), m_LightInstanceVector.end(),f);
	m_isAlive = f.isAlive;

	m_fLastTime = CTimer::Instance().GetCurrentSecond();
}

void CEffectInstance::OnRender()
{
	// Use DX11 effect path whenever DX11 runtime/resources are ready.
	// Native-present state must not gate submit counters, otherwise world mask can stall.
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	CEffectManager& rkEffectManager = CEffectManager::Instance();
	if (rkEffectManager.EnsureDX11EffectResourcesReady() &&
		pDX11Device &&
		pDX11Device->IsValid())
	{
		const uint32_t uSubmittedBefore = rkEffectManager.GetDX11SubmittedEffectCount();
		std::for_each(m_ParticleInstanceVector.begin(), m_ParticleInstanceVector.end(), std::mem_fn(&CEffectElementBaseInstance::Render));
		std::for_each(m_MeshInstanceVector.begin(), m_MeshInstanceVector.end(), std::mem_fn(&CEffectElementBaseInstance::Render));
		const uint32_t uSubmittedAfter = rkEffectManager.GetDX11SubmittedEffectCount();
		if (uSubmittedAfter > uSubmittedBefore)
			++ms_iRenderingEffectCount;
		return;
	}

	if (pDX11Device && pDX11Device->IsValid())
	{
		static DWORD s_dwLastStrictSkipLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastStrictSkipLogMS || (dwNow - s_dwLastStrictSkipLogMS) >= 2000u)
		{
			s_dwLastStrictSkipLogMS = dwNow;
			TraceError("DX11_EFFECT_SKIP reason=dx11_resources_not_ready");
		}
		return;
	}

	// Full-DX11 migration policy: no DX9 fallback path in effect instance renderer.
	static DWORD s_dwLastRuntimeInactiveLogMS = 0u;
	const DWORD dwNowRuntime = ELTimer_GetMSec();
	if (0u == s_dwLastRuntimeInactiveLogMS || (dwNowRuntime - s_dwLastRuntimeInactiveLogMS) >= 3000u)
	{
		s_dwLastRuntimeInactiveLogMS = dwNowRuntime;
		TraceError("DX11_EFFECT_SKIP reason=dx11_runtime_inactive");
	}
	return;
}

void CEffectInstance::OnBlendRender()
{
	// Effect blend submission follows the same DX11 runtime path as regular effect rendering.
	OnRender();
}

void CEffectInstance::OnRenderToShadowMap()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	CEffectManager& rkEffectManager = CEffectManager::Instance();
	if (!rkEffectManager.EnsureDX11EffectResourcesReady())
		return;

	// Shadow/occluder-compatible subset: mesh effects only.
	std::for_each(m_MeshInstanceVector.begin(), m_MeshInstanceVector.end(), std::mem_fn(&CEffectElementBaseInstance::Render));
}

void CEffectInstance::OnRenderShadow()
{
	OnRenderToShadowMap();
}

void CEffectInstance::OnRenderPCBlocker()
{
	// Keep PC blocker pass consistent with shadow subset for mesh-based effects.
	OnRenderToShadowMap();
}

void CEffectInstance::OnUpdateCollisionData(const CStaticCollisionDataVector * pscdVector)
{
	if (!pscdVector)
		return;

	const D3DXMATRIX& c_rMatrix = GetTransform();
	for (CStaticCollisionDataVector::const_iterator it = pscdVector->begin(); it != pscdVector->end(); ++it)
		AddCollision(&(*it), &c_rMatrix);
}

void CEffectInstance::OnUpdateHeighInstance(CAttributeInstance * pAttributeInstance)
{
	if (!pAttributeInstance)
		return;

	SetHeightInstance(pAttributeInstance);
}

bool CEffectInstance::OnGetObjectHeight(float fX, float fY, float * pfHeight)
{
	if (!m_pHeightAttributeInstance || !pfHeight)
		return false;

	return m_pHeightAttributeInstance->GetHeight(fX, fY, pfHeight) != 0;
}

void CEffectInstance::SetGlobalMatrix(const D3DXMATRIX & c_rmatGlobal)
{
	m_matGlobal = c_rmatGlobal;
}

BOOL CEffectInstance::isAlive()
{
	return m_isAlive;
}

void CEffectInstance::SetActive()
{
	std::for_each(
		m_ParticleInstanceVector.begin(),
		m_ParticleInstanceVector.end(),
		std::mem_fn(&CEffectElementBaseInstance::SetActive));
	std::for_each(
		m_MeshInstanceVector.begin(),
		m_MeshInstanceVector.end(),
		std::mem_fn(&CEffectElementBaseInstance::SetActive));
	std::for_each(
		m_LightInstanceVector.begin(),
		m_LightInstanceVector.end(),
		std::mem_fn(&CEffectElementBaseInstance::SetActive));
}

void CEffectInstance::SetDeactive()
{
	std::for_each(
		m_ParticleInstanceVector.begin(),
		m_ParticleInstanceVector.end(),
		std::mem_fn(&CEffectElementBaseInstance::SetDeactive));
	std::for_each(
		m_MeshInstanceVector.begin(),
		m_MeshInstanceVector.end(),
		std::mem_fn(&CEffectElementBaseInstance::SetDeactive));
	std::for_each(
		m_LightInstanceVector.begin(),
		m_LightInstanceVector.end(),
		std::mem_fn(&CEffectElementBaseInstance::SetDeactive));
}

void CEffectInstance::__SetParticleData(CParticleSystemData * pData)
{
	CParticleSystemInstance * pInstance = CParticleSystemInstance::New();
	pInstance->SetDataPointer(pData);
	pInstance->SetLocalMatrixPointer(&m_matGlobal);

	m_ParticleInstanceVector.push_back(pInstance);
}
void CEffectInstance::__SetMeshData(CEffectMeshScript * pMesh)
{
	CEffectMeshInstance * pMeshInstance = CEffectMeshInstance::New();
	pMeshInstance->SetDataPointer(pMesh);
	pMeshInstance->SetLocalMatrixPointer(&m_matGlobal);
	pMeshInstance->SetDX11RenderClass(CEffectManager::Instance().GetEffectRenderClass(m_pkEftData));
	pMeshInstance->SetOwnerEffectCRC(CEffectManager::Instance().GetEffectDataCRC(m_pkEftData));

	m_MeshInstanceVector.push_back(pMeshInstance);
}

void CEffectInstance::__SetLightData(CLightData* pData)
{
	CLightInstance * pInstance = CLightInstance::New();
	pInstance->SetDataPointer(pData);
	pInstance->SetLocalMatrixPointer(&m_matGlobal);

	m_LightInstanceVector.push_back(pInstance);
}

void CEffectInstance::SetEffectDataPointer(CEffectData * pEffectData)
{
	m_isAlive=true;

	m_pkEftData=pEffectData;

	m_fLastTime = CTimer::Instance().GetCurrentSecond();
	m_fBoundingSphereRadius = pEffectData->GetBoundingSphereRadius();
	m_v3BoundingSpherePosition = pEffectData->GetBoundingSpherePosition();

	if (m_fBoundingSphereRadius > 0.0f)
		CGraphicObjectInstance::RegisterBoundingSphere();

	DWORD i;

	for (i = 0; i < pEffectData->GetParticleCount(); ++i)
	{
		CParticleSystemData * pParticle = pEffectData->GetParticlePointer(i);

		__SetParticleData(pParticle);
	}

	for (i = 0; i < pEffectData->GetMeshCount(); ++i)
	{
		CEffectMeshScript * pMesh = pEffectData->GetMeshPointer(i);

		__SetMeshData(pMesh);
	}

	for (i = 0; i < pEffectData->GetLightCount(); ++i)
	{
		CLightData * pLight = pEffectData->GetLightPointer(i);

		__SetLightData(pLight);
	}

	m_pSoundInstanceVector = pEffectData->GetSoundInstanceVector();
}

bool CEffectInstance::GetBoundingSphere(D3DXVECTOR3 & v3Center, float & fRadius)
{
	v3Center.x = m_matGlobal._41 + m_v3BoundingSpherePosition.x;
	v3Center.y = m_matGlobal._42 + m_v3BoundingSpherePosition.y;
	v3Center.z = m_matGlobal._43 + m_v3BoundingSpherePosition.z;
	fRadius = m_fBoundingSphereRadius;
	return true;
}

void CEffectInstance::Clear()
{
	if (!m_ParticleInstanceVector.empty())
	{
		std::for_each(m_ParticleInstanceVector.begin(), m_ParticleInstanceVector.end(), CParticleSystemInstance::Delete);
		m_ParticleInstanceVector.clear();
	}

	if (!m_MeshInstanceVector.empty())
	{
		std::for_each(m_MeshInstanceVector.begin(), m_MeshInstanceVector.end(), CEffectMeshInstance::Delete);
		m_MeshInstanceVector.clear();
	}

	if (!m_LightInstanceVector.empty())
	{
		std::for_each(m_LightInstanceVector.begin(), m_LightInstanceVector.end(), CLightInstance::Delete);
		m_LightInstanceVector.clear();
	}

	__Initialize();
}

void CEffectInstance::__Initialize()
{
	ReleaseAlwaysHidden();
	
	m_isAlive = FALSE;
	m_dwFrame = 0;
	m_pSoundInstanceVector = NULL;
	m_fBoundingSphereRadius = 0.0f;
	m_v3BoundingSpherePosition.x = m_v3BoundingSpherePosition.y = m_v3BoundingSpherePosition.z = 0.0f;

	m_pkEftData=NULL;

	D3DXMatrixIdentity(&m_matGlobal);
}

CEffectInstance::CEffectInstance() 
{
	__Initialize();
}
CEffectInstance::~CEffectInstance()
{
	assert(m_ParticleInstanceVector.empty());
	assert(m_MeshInstanceVector.empty());
	assert(m_LightInstanceVector.empty());
}
