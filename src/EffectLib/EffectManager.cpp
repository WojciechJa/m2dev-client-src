#include "StdAfx.h"
#include "EterBase/Random.h"
#include "EterLib/Camera.h"
#include "EffectManager.h"

// DX11 includes (Batch W2)
#include <d3d11.h>
#include <d3dcompiler.h>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "EterLib/GrpDeviceDX11.h"  // W2 finalize: for GetEffectTextureSRV
#include "EterLib/GrpTextureDX11.h"  // T2: DX11 texture loading helper
#include "EterLib/GrpImage.h"  // T2: for CGraphicImage filename access

namespace
{
	constexpr uint32_t kDefaultEffectDynamicVBVertexCapacity = 4096u;

	void BuildEffectBlendDesc(D3D11_BLEND srcBlend, D3D11_BLEND destBlend, D3D11_BLEND_DESC& outDesc)
	{
		ZeroMemory(&outDesc, sizeof(outDesc));
		D3D11_RENDER_TARGET_BLEND_DESC& rt = outDesc.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = srcBlend;
		rt.DestBlend = destBlend;
		rt.BlendOp = D3D11_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D11_BLEND_ONE;
		rt.DestBlendAlpha = D3D11_BLEND_ZERO;
		rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	}

	uint32_t RoundUpToPowerOfTwo(uint32_t value)
	{
		if (value <= 1u)
			return 1u;

		--value;
		value |= value >> 1u;
		value |= value >> 2u;
		value |= value >> 4u;
		value |= value >> 8u;
		value |= value >> 16u;
		return value + 1u;
	}
}

std::string CEffectManager::NormalizeEffectPath(const char* cszFile)
{
	if (!cszFile || !cszFile[0])
		return std::string();

	std::string stPath;
	StringPath(cszFile, stPath);
	for (size_t i = 0; i < stPath.size(); ++i)
	{
		char& c = stPath[i];
		if (c == '\\')
			c = '/';
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}

	return stPath;
}

EEffectRenderClass CEffectManager::ClassifyEffectPath(const char* c_szFileName) const
{
	const std::string stPath = NormalizeEffectPath(c_szFileName);
	if (stPath.empty())
		return EFFECT_RENDER_CLASS_DEFAULT;

	if (std::string::npos != stPath.find("/effect/etc/click/") &&
		(std::string::npos != stPath.find("click_select.mse") ||
		 std::string::npos != stPath.find("click_glow_select.mse")))
	{
		return EFFECT_RENDER_CLASS_TARGET_RING;
	}

	return EFFECT_RENDER_CLASS_DEFAULT;
}

DWORD CEffectManager::GetEffectDataCRC(const CEffectData* pEffectData) const
{
	if (!pEffectData)
		return 0u;

	const char* c_szFileName = pEffectData->GetFileName();
	if (!c_szFileName || !c_szFileName[0])
		return 0u;

	std::string stPath;
	StringPath(c_szFileName, stPath);
	return GetCaseCRC32(stPath.c_str(), static_cast<int>(stPath.length()));
}

EEffectRenderClass CEffectManager::GetEffectRenderClassByCRC(DWORD dwEffectCRC) const
{
	const std::unordered_map<DWORD, EEffectRenderClass>::const_iterator it = m_kEffectRenderClassByCRC.find(dwEffectCRC);
	if (it != m_kEffectRenderClassByCRC.end())
		return it->second;
	return EFFECT_RENDER_CLASS_DEFAULT;
}

EEffectRenderClass CEffectManager::GetEffectRenderClass(const CEffectData* pEffectData) const
{
	const DWORD dwEffectCRC = GetEffectDataCRC(pEffectData);
	if (0u == dwEffectCRC)
		return EFFECT_RENDER_CLASS_DEFAULT;

	const EEffectRenderClass eCachedClass = GetEffectRenderClassByCRC(dwEffectCRC);
	if (eCachedClass != EFFECT_RENDER_CLASS_DEFAULT)
		return eCachedClass;

	return ClassifyEffectPath(pEffectData ? pEffectData->GetFileName() : nullptr);
}

void CEffectManager::SetDX11TargetRingAlphaClipThreshold(float fThreshold)
{
	if (!std::isfinite(fThreshold))
		return;

	if (fThreshold < 0.0f)
		fThreshold = 0.0f;
	else if (fThreshold > 1.0f)
		fThreshold = 1.0f;
	m_fDX11TargetRingAlphaClipThreshold = fThreshold;
}

uint32_t CEffectManager::GetDX11TargetRingActiveInstanceCount() const
{
	uint32_t dwCount = 0u;
	for (TEffectInstanceMap::const_iterator it = m_kEftInstMap.begin(); it != m_kEftInstMap.end(); ++it)
	{
		CEffectInstance* pEffectInstance = it->second;
		if (!pEffectInstance || !pEffectInstance->isAlive())
			continue;

		if (EFFECT_RENDER_CLASS_TARGET_RING == GetEffectRenderClass(pEffectInstance->GetEffectDataPointer()))
			++dwCount;
	}
	return dwCount;
}

void CEffectManager::UpdateDX11TargetRingActiveInstanceCount()
{
	m_kDX11TargetRingDiagnostics.dwActiveInstanceCount = GetDX11TargetRingActiveInstanceCount();
}

void CEffectManager::ResetDX11TargetRingFrameCounters()
{
	UpdateDX11TargetRingActiveInstanceCount();
	m_kDX11TargetRingDiagnostics.dwSubmittedCount = 0u;
	m_kDX11TargetRingDiagnostics.dwSkippedCount = 0u;
}

void CEffectManager::AddDX11TargetRingSubmittedCount(uint32_t dwCount, DWORD dwEffectCRC)
{
	if (0u == dwCount)
		return;

	if (m_kDX11TargetRingDiagnostics.dwSubmittedCount > 0xffffffffu - dwCount)
		m_kDX11TargetRingDiagnostics.dwSubmittedCount = 0xffffffffu;
	else
		m_kDX11TargetRingDiagnostics.dwSubmittedCount += dwCount;

	if (dwEffectCRC != 0u)
		m_kDX11TargetRingDiagnostics.dwLastEffectCRC = dwEffectCRC;
	m_kDX11TargetRingDiagnostics.dwLastTimestampMS = ELTimer_GetMSec();
}

void CEffectManager::AddDX11TargetRingSkippedCount(uint32_t dwCount, const char* c_szReason, DWORD dwEffectCRC)
{
	if (dwCount > 0u)
	{
		if (m_kDX11TargetRingDiagnostics.dwSkippedCount > 0xffffffffu - dwCount)
			m_kDX11TargetRingDiagnostics.dwSkippedCount = 0xffffffffu;
		else
			m_kDX11TargetRingDiagnostics.dwSkippedCount += dwCount;
	}

	if (dwEffectCRC != 0u)
		m_kDX11TargetRingDiagnostics.dwLastEffectCRC = dwEffectCRC;

	if (c_szReason && c_szReason[0])
	{
		strncpy_s(
			m_kDX11TargetRingDiagnostics.szLastReason,
			sizeof(m_kDX11TargetRingDiagnostics.szLastReason),
			c_szReason,
			_TRUNCATE);
	}

	m_kDX11TargetRingDiagnostics.dwLastTimestampMS = ELTimer_GetMSec();
}

void CEffectManager::SetDX11TargetRingBlendState(bool bBlendingEnable, BYTE bySrcBlend, BYTE byDestBlend)
{
	m_kDX11TargetRingDiagnostics.dwLastBlendState =
		(static_cast<uint32_t>(bBlendingEnable ? 1u : 0u) << 16u) |
		(static_cast<uint32_t>(bySrcBlend) << 8u) |
		static_cast<uint32_t>(byDestBlend);
	m_kDX11TargetRingDiagnostics.dwLastTimestampMS = ELTimer_GetMSec();
}

void CEffectManager::SetDX11TargetRingPipelineState(bool bDepthTestLessEqual, bool bDepthWriteEnable, bool bNoCull, bool bAlphaClip)
{
	uint32_t dwFlags = 0u;
	if (bDepthTestLessEqual)
		dwFlags |= (1u << 0u);
	if (bDepthWriteEnable)
		dwFlags |= (1u << 1u);
	if (bNoCull)
		dwFlags |= (1u << 2u);
	if (bAlphaClip)
		dwFlags |= (1u << 3u);

	m_kDX11TargetRingDiagnostics.dwLastPipelineFlags = dwFlags;
	m_kDX11TargetRingDiagnostics.dwLastTimestampMS = ELTimer_GetMSec();
}

bool CEffectManager::IsHighPriorityFX(const CEffectInstance* pEffectInstance) const
{
	if (!pEffectInstance)
		return false;

	CEffectData* pEffectData = pEffectInstance->GetEffectDataPointer();
	if (!pEffectData)
		return false;

	const std::string stPath = NormalizeEffectPath(pEffectData->GetFileName());
	if (stPath.empty())
		return false;

	return stPath.find("pc/common/effect/armor/armor_") != std::string::npos ||
		stPath.find("pc/common/effect/sword/sword_") != std::string::npos;
}

void CEffectManager::GetInfo(std::string* pstInfo)
{
	char szInfo[256];
	
	sprintf(szInfo, "Effect: Inst - ED %zd, EI %zd Pool - PSI %zd, MI %zd, LI %zd, PI %zd, EI %zd, ED %zd, PSD %zd, EM %zd, LD %zd", 		
		m_kEftDataMap.size(),
		m_kEftInstMap.size(),		
		CParticleSystemInstance::ms_kPool.GetCapacity(),
		CEffectMeshInstance::ms_kPool.GetCapacity(),
		CLightInstance::ms_kPool.GetCapacity(),		
		CParticleInstance::ms_kPool.GetCapacity(),
		//CRayParticleInstance::ms_kPool.GetCapacity(),
		CEffectInstance::ms_kPool.GetCapacity(),
		CEffectData::ms_kPool.GetCapacity(),
		CParticleSystemData::ms_kPool.GetCapacity(),
		CEffectMeshScript::ms_kPool.GetCapacity(),
		CLightData::ms_kPool.GetCapacity()
	);
	pstInfo->append(szInfo);
}

void CEffectManager::UpdateSound()
{
	for (TEffectInstanceMap::iterator itor = m_kEftInstMap.begin(); itor != m_kEftInstMap.end(); ++itor)
	{
		CEffectInstance * pEffectInstance = itor->second;

		pEffectInstance->UpdateSound();
	}
}

bool CEffectManager::IsAliveEffect(DWORD dwInstanceIndex)
{
	TEffectInstanceMap::iterator f = m_kEftInstMap.find(dwInstanceIndex);
	if (m_kEftInstMap.end()==f)
		return false;

	return f->second->isAlive() ? true : false;
}

void CEffectManager::Update()
{
	++m_dwUpdateFrame;

	D3DXVECTOR3 v3CameraEye(0.0f, 0.0f, 0.0f);
	D3DXVECTOR3 v3CameraTarget(0.0f, 0.0f, 0.0f);
	CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
	if (pCamera)
	{
		v3CameraEye = pCamera->GetEye();
		v3CameraTarget = pCamera->GetTarget();
	}

	// 2004. 3. 1. myevan. 이펙트 모니터링 하는 코드
	/*
	if (GetAsyncKeyState(VK_F9))
	{
		Tracenf("CEffectManager::m_EffectInstancePool %d", m_EffectInstancePool.GetCapacity());
		Tracenf("CEffectManager::m_EffectDataPool %d", m_EffectDataPool.GetCapacity());
		Tracenf("CEffectInstance::ms_LightInstancePool %d", CEffectInstance::ms_LightInstancePool.GetCapacity());
		Tracenf("CEffectInstance::ms_MeshInstancePool %d", CEffectInstance::ms_MeshInstancePool.GetCapacity());
		Tracenf("CEffectInstance::ms_ParticleSystemInstancePool %d", CEffectInstance::ms_ParticleSystemInstancePool.GetCapacity());
		Tracenf("CParticleInstance::ms_ParticleInstancePool %d", CParticleInstance::ms_kPool.GetCapacity());
		Tracenf("CRayParticleInstance::ms_RayParticleInstancePool %d", CRayParticleInstance::ms_kPool.GetCapacity());		
		Tracen("---------------------------------------------");
	}
	*/

	for (TEffectInstanceMap::iterator itor = m_kEftInstMap.begin(); itor != m_kEftInstMap.end();)
	{
		const DWORD dwInstanceID = itor->first;
		CEffectInstance * pEffectInstance = itor->second;

		DWORD dwUpdateStride = 1;
		if (m_bAdaptiveFX && pCamera)
		{
			const bool bHighPriorityFX = IsHighPriorityFX(pEffectInstance);
			D3DXVECTOR3 v3Center;
			float fRadius = 0.0f;
			if (pEffectInstance->GetBoundingSphere(v3Center, fRadius))
			{
				D3DXVECTOR3 v3DiffEye = v3Center - v3CameraEye;
				const float fDistEyeSq = D3DXVec3Dot(&v3DiffEye, &v3DiffEye);
				D3DXVECTOR3 v3DiffTarget = v3Center - v3CameraTarget;
				const float fDistTargetSq = D3DXVec3Dot(&v3DiffTarget, &v3DiffTarget);
				const float fDistSq = std::min(fDistEyeSq, fDistTargetSq);
				const bool bImportantFX = bHighPriorityFX || (fDistSq < (4500.0f * 4500.0f));

				if (m_iPerfProfile >= 2 && !bImportantFX && !bHighPriorityFX)
				{
					if (fDistSq > (9000.0f * 9000.0f))
						dwUpdateStride = m_bOverBudgetReduced ? 4 : 3;
					else if (fDistSq > (5000.0f * 5000.0f))
						dwUpdateStride = 2;

					if (m_iFXStrideBias <= 0)
					{
						if (dwUpdateStride > 1)
							--dwUpdateStride;
					}
					else if (m_iFXStrideBias >= 2)
					{
						if (dwUpdateStride > 1)
							++dwUpdateStride;
					}
				}
			}
		}

		if (dwUpdateStride <= 1 || ((m_dwUpdateFrame + (dwInstanceID % dwUpdateStride)) % dwUpdateStride) == 0)
			pEffectInstance->Update(/*fElapsedTime*/);

		if (pEffectInstance->isAlive()) [[likely]] {
			++itor;
			continue;
		}

		itor = m_kEftInstMap.erase(itor);
		CEffectInstance::Delete(pEffectInstance);
		m_bSortDirty = true;
	}
}


struct CEffectManager_LessEffectInstancePtrRenderOrder
{
	bool operator() (CEffectInstance* pkLeft, CEffectInstance* pkRight)
	{
		return pkLeft->LessRenderOrder(pkRight);		
	}
};

struct CEffectManager_FEffectInstanceRender
{
	inline void operator () (CEffectInstance * pkEftInst)
	{
		pkEftInst->Render();
	}
};

void CEffectManager::Render()
{
	++m_dwRenderFrame;

	if (m_isDisableSortRendering)
	{	
		for (TEffectInstanceMap::iterator itor = m_kEftInstMap.begin(); itor != m_kEftInstMap.end();)
		{
			CEffectInstance * pEffectInstance = itor->second;
			pEffectInstance->Render();
			++itor;
		}
	}
	else
	{
		const bool bCanReuseSort =
			!m_bSortDirty &&
			m_dwSortInterval > 1 &&
			(m_dwRenderFrame % m_dwSortInterval) != 0 &&
			m_kVctSortedCache.size() == m_kEftInstMap.size();

		if (!bCanReuseSort)
		{
			m_kVctSortedCache.clear();
			m_kVctSortedCache.reserve(m_kEftInstMap.size());

			TEffectInstanceMap::iterator i;
			for (i = m_kEftInstMap.begin(); i != m_kEftInstMap.end(); ++i)
				m_kVctSortedCache.push_back(i->second);

			std::sort(m_kVctSortedCache.begin(), m_kVctSortedCache.end(), CEffectManager_LessEffectInstancePtrRenderOrder());
			m_bSortDirty = false;
		}

		std::for_each(m_kVctSortedCache.begin(), m_kVctSortedCache.end(), CEffectManager_FEffectInstanceRender());
	}
}

void CEffectManager::SetPerformanceSettings(int iProfile, bool bAdaptiveFX, bool bOverBudgetReduced, int iStrideBias)
{
	m_iPerfProfile = iProfile;
	if (m_iPerfProfile < 0)
		m_iPerfProfile = 0;
	else if (m_iPerfProfile > 2)
		m_iPerfProfile = 2;

	m_bAdaptiveFX = bAdaptiveFX;
	m_bOverBudgetReduced = bOverBudgetReduced;
	if (iStrideBias < 0)
		m_iFXStrideBias = 0;
	else if (iStrideBias > 2)
		m_iFXStrideBias = 2;
	else
		m_iFXStrideBias = iStrideBias;

	if (m_iPerfProfile <= 0)
		m_dwSortInterval = 1;
	else if (m_iPerfProfile == 1)
		m_dwSortInterval = 2;
	else
		m_dwSortInterval = 3;

	if (!m_bAdaptiveFX)
		CParticleSystemInstance::SetGlobalEmissionScale(1.0f);
	else
	{
		float fScale = 1.0f;
		if (m_iPerfProfile == 1)
			fScale = 0.90f;
		else if (m_iPerfProfile >= 2)
			fScale = 0.80f;

		if (m_bOverBudgetReduced)
			fScale *= 0.9f;

		const float fMinScale = (m_iPerfProfile >= 2) ? 0.72f : 0.80f;
		if (fScale < fMinScale)
			fScale = fMinScale;

		CParticleSystemInstance::SetGlobalEmissionScale(fScale);
	}
}

DWORD CEffectManager::GetActiveEffectCount() const
{
	return static_cast<DWORD>(m_kEftInstMap.size());
}

DWORD CEffectManager::GetActiveParticleCount() const
{
	DWORD dwParticleCount = 0;
	for (TEffectInstanceMap::const_iterator it = m_kEftInstMap.begin(); it != m_kEftInstMap.end(); ++it)
	{
		dwParticleCount += it->second->GetActiveParticleCount();
	}

	return dwParticleCount;
}

void CEffectManager::ResetDX11SubmittedEffectCount()
{
	m_dwDX11SubmittedEffectCount = 0;
	m_dwDX11SubmittedParticleCount = 0;
	m_dwDX11SubmittedMeshEffectCount = 0;
}

void CEffectManager::BeginDX11WorldFrameTelemetry()
{
	// Order-sensitive for world mask contract:
	// - world pass consumes previous-frame counters first
	// - then we reset before any current-frame effect draws
	ResetDX11SubmittedEffectCount();
	ResetDX11TargetRingFrameCounters();
	CEffectInstance::ResetRenderingEffectCount();
}

void CEffectManager::AddDX11SubmittedEffectCount(uint32_t dwCount)
{
	if (0 == dwCount)
		return;

	if (m_dwDX11SubmittedEffectCount > 0xffffffffu - dwCount)
		m_dwDX11SubmittedEffectCount = 0xffffffffu;
	else
		m_dwDX11SubmittedEffectCount += dwCount;
}

uint32_t CEffectManager::GetDX11SubmittedEffectCount() const
{
	return m_dwDX11SubmittedEffectCount;
}

// W4.2: Telemetry split for particle vs mesh effect draws
void CEffectManager::AddDX11SubmittedParticleCount(uint32_t dwCount)
{
	if (0 == dwCount)
		return;

	if (m_dwDX11SubmittedParticleCount > 0xffffffffu - dwCount)
		m_dwDX11SubmittedParticleCount = 0xffffffffu;
	else
		m_dwDX11SubmittedParticleCount += dwCount;

	// Throttled telemetry (once per 5 seconds)
	static DWORD s_dwLastParticleTelemetryMS = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwLastParticleTelemetryMS || (dwNow - s_dwLastParticleTelemetryMS) >= 5000u)
	{
		s_dwLastParticleTelemetryMS = dwNow;
		TraceError("DX11_EFFECT_TELEMETRY type=particle_submit count=%u", m_dwDX11SubmittedParticleCount);
	}
}

void CEffectManager::AddDX11SubmittedMeshEffectCount(uint32_t dwCount)
{
	if (0 == dwCount)
		return;

	if (m_dwDX11SubmittedMeshEffectCount > 0xffffffffu - dwCount)
		m_dwDX11SubmittedMeshEffectCount = 0xffffffffu;
	else
		m_dwDX11SubmittedMeshEffectCount += dwCount;

	// Throttled telemetry (once per 5 seconds)
	static DWORD s_dwLastMeshTelemetryMS = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwLastMeshTelemetryMS || (dwNow - s_dwLastMeshTelemetryMS) >= 5000u)
	{
		s_dwLastMeshTelemetryMS = dwNow;
		TraceError("DX11_EFFECT_TELEMETRY type=mesh_submit count=%u", m_dwDX11SubmittedMeshEffectCount);
	}
}

int CEffectManager::GetRenderingEffectCount()
{
	return CEffectInstance::GetRenderingEffectCount();
}

BOOL CEffectManager::RegisterEffect(const char * c_szFileName,bool isExistDelete,bool isNeedCache)
{
	std::string strFileName;
	StringPath(c_szFileName, strFileName);
	DWORD dwCRC = GetCaseCRC32(strFileName.c_str(), strFileName.length());

	TEffectDataMap::iterator itor = m_kEftDataMap.find(dwCRC);
	if (m_kEftDataMap.end() != itor)
	{
		if (isExistDelete)
		{
			CEffectData* pkEftData=itor->second;
			CEffectData::Delete(pkEftData);			
			m_kEftDataMap.erase(itor);
			m_kEffectRenderClassByCRC.erase(dwCRC);
		}
		else
		{
			if (m_kEffectRenderClassByCRC.find(dwCRC) == m_kEffectRenderClassByCRC.end())
				m_kEffectRenderClassByCRC[dwCRC] = ClassifyEffectPath(itor->second ? itor->second->GetFileName() : nullptr);
			//TraceError("CEffectManager::RegisterEffect - m_kEftDataMap.find [%s] Already Exist", c_szFileName);
			return TRUE;
		}
	}

	CEffectData * pkEftData = CEffectData::New();

	if (!pkEftData->LoadScript(c_szFileName))
	{
		TraceError("CEffectManager::RegisterEffect - LoadScript(%s) Error", c_szFileName);
		CEffectData::Delete(pkEftData);
		return FALSE;
	}

	m_kEftDataMap.insert(TEffectDataMap::value_type(dwCRC, pkEftData));
	const EEffectRenderClass eRenderClass = ClassifyEffectPath(pkEftData->GetFileName());
	m_kEffectRenderClassByCRC[dwCRC] = eRenderClass;
	if (eRenderClass == EFFECT_RENDER_CLASS_TARGET_RING)
	{
		TraceError("DX11_TARGETFX_CLASSIFY crc=0x%08X path=%s class=target_ring", dwCRC, pkEftData->GetFileName());
	}

	if (isNeedCache)
	{
		if (m_kEftCacheMap.find(dwCRC)==m_kEftCacheMap.end())
		{
			CEffectInstance* pkNewEftInst=CEffectInstance::New();
			pkNewEftInst->SetEffectDataPointer(pkEftData);
			m_kEftCacheMap.insert(TEffectInstanceMap::value_type(dwCRC, pkNewEftInst));
		}
	}

	return TRUE;
}
// CEffectData 를 포인터형으로 리턴하게 하고..
// CEffectData에서 CRC를 얻을수 있게 한다
BOOL CEffectManager::RegisterEffect2(const char * c_szFileName, DWORD* pdwRetCRC, bool isNeedCache)
{	
	std::string strFileName;
	StringPath(c_szFileName, strFileName);
	DWORD dwCRC = GetCaseCRC32(strFileName.c_str(), strFileName.length());
	*pdwRetCRC=dwCRC;

	return RegisterEffect(c_szFileName,false,isNeedCache);
}

int CEffectManager::CreateEffect(const char * c_szFileName, const D3DXVECTOR3 & c_rv3Position, const D3DXVECTOR3 & c_rv3Rotation)
{
	DWORD dwID = GetCaseCRC32(c_szFileName, strlen(c_szFileName));
	return CreateEffect(dwID, c_rv3Position, c_rv3Rotation);
}

int CEffectManager::CreateEffect(DWORD dwID, const D3DXVECTOR3 & c_rv3Position, const D3DXVECTOR3 & c_rv3Rotation)
{
	int iInstanceIndex = GetEmptyIndex();

	CreateEffectInstance(iInstanceIndex, dwID);
	SelectEffectInstance(iInstanceIndex);
	D3DXMATRIX mat;
	D3DXMatrixRotationYawPitchRoll(&mat,D3DXToRadian(c_rv3Rotation.x),D3DXToRadian(c_rv3Rotation.y),D3DXToRadian(c_rv3Rotation.z));
	mat._41 = c_rv3Position.x;
	mat._42 = c_rv3Position.y;
	mat._43 = c_rv3Position.z;
	SetEffectInstanceGlobalMatrix(mat);

	return iInstanceIndex;
}

void CEffectManager::CreateEffectInstance(DWORD dwInstanceIndex, DWORD dwID)
{
	if (!dwID)
		return;

	CEffectData * pEffect;
	if (!GetEffectData(dwID, &pEffect))
	{
		Tracef("CEffectManager::CreateEffectInstance - NO DATA :%d\n", dwID); 
		return;
	}

	CEffectInstance * pEffectInstance = CEffectInstance::New();	
	pEffectInstance->SetEffectDataPointer(pEffect);

	m_kEftInstMap.insert(TEffectInstanceMap::value_type(dwInstanceIndex, pEffectInstance));
	m_bSortDirty = true;
}

bool CEffectManager::DestroyEffectInstance(DWORD dwInstanceIndex)
{
	TEffectInstanceMap::iterator itor = m_kEftInstMap.find(dwInstanceIndex);

	if (itor == m_kEftInstMap.end())
		return false;

	CEffectInstance * pEffectInstance = itor->second;

	m_kEftInstMap.erase(itor);

	CEffectInstance::Delete(pEffectInstance);
	m_bSortDirty = true;

	return true;
}

void CEffectManager::DeactiveEffectInstance(DWORD dwInstanceIndex)
{
	TEffectInstanceMap::iterator itor = m_kEftInstMap.find(dwInstanceIndex);

	if (itor == m_kEftInstMap.end())
		return;

	CEffectInstance * pEffectInstance = itor->second;
	pEffectInstance->SetDeactive();
}

void CEffectManager::CreateUnsafeEffectInstance(DWORD dwEffectDataID, CEffectInstance ** ppEffectInstance)
{
	CEffectData * pEffect;
	if (!GetEffectData(dwEffectDataID, &pEffect))
	{
		Tracef("CEffectManager::CreateEffectInstance - NO DATA :%d\n", dwEffectDataID); 
		return;
	}

	CEffectInstance* pkEftInstNew=CEffectInstance::New();
	pkEftInstNew->SetEffectDataPointer(pEffect);

	*ppEffectInstance = pkEftInstNew;	
}

bool CEffectManager::DestroyUnsafeEffectInstance(CEffectInstance * pEffectInstance)
{
	if (!pEffectInstance)
		return false;

	CEffectInstance::Delete(pEffectInstance);
	
	return true;
}

BOOL CEffectManager::SelectEffectInstance(DWORD dwInstanceIndex)
{
	TEffectInstanceMap::iterator itor = m_kEftInstMap.find(dwInstanceIndex);

	m_pSelectedEffectInstance = NULL;

	if (m_kEftInstMap.end() == itor)
		return FALSE;

	m_pSelectedEffectInstance = itor->second;

	return TRUE;
}

void CEffectManager::SetEffectTextures(DWORD dwID, std::vector<std::string> textures)
{
	CEffectData * pEffectData;
	if (!GetEffectData(dwID, &pEffectData))
	{
		Tracef("CEffectManager::CreateEffectInstance - NO DATA :%d\n", dwID); 
		return;
	}

	for(DWORD i = 0; i < textures.size(); i++)
	{
		CParticleSystemData * pParticle = pEffectData->GetParticlePointer(i);
		pParticle->ChangeTexture(textures.at(i).c_str());
	}
}

void CEffectManager::SetEffectInstancePosition(const D3DXVECTOR3 & c_rv3Position)
{
	if (!m_pSelectedEffectInstance)
	{
//		assert(!"Instance to use is not yet set!");
		return;
	}

	m_pSelectedEffectInstance->SetPosition(c_rv3Position);
}

void CEffectManager::SetEffectInstanceRotation(const D3DXVECTOR3 & c_rv3Rotation)
{
	if (!m_pSelectedEffectInstance)
	{
//		assert(!"Instance to use is not yet set!");
		return;
	}

	m_pSelectedEffectInstance->SetRotation(c_rv3Rotation.x,c_rv3Rotation.y,c_rv3Rotation.z);
}

void CEffectManager::SetEffectInstanceGlobalMatrix(const D3DXMATRIX & c_rmatGlobal)
{
	if (!m_pSelectedEffectInstance)
		return;

	m_pSelectedEffectInstance->SetGlobalMatrix(c_rmatGlobal);
}

void CEffectManager::ShowEffect()
{
	if (!m_pSelectedEffectInstance)
		return;

	m_pSelectedEffectInstance->Show();
}

void CEffectManager::HideEffect()
{
	if (!m_pSelectedEffectInstance)
		return;

	m_pSelectedEffectInstance->Hide();
}

void CEffectManager::ApplyAlwaysHidden()
{
	if (!m_pSelectedEffectInstance)
		return;

	m_pSelectedEffectInstance->ApplyAlwaysHidden();
}

void CEffectManager::ReleaseAlwaysHidden()
{
	if (!m_pSelectedEffectInstance)
		return;

	m_pSelectedEffectInstance->ReleaseAlwaysHidden();
}

bool CEffectManager::GetEffectData(DWORD dwID, CEffectData ** ppEffect)
{
	TEffectDataMap::iterator itor = m_kEftDataMap.find(dwID);

	if (itor == m_kEftDataMap.end())
		return false;

	*ppEffect = itor->second;

	return true;
}

bool CEffectManager::GetEffectData(DWORD dwID, const CEffectData ** c_ppEffect)
{
	TEffectDataMap::iterator itor = m_kEftDataMap.find(dwID);

	if (itor == m_kEftDataMap.end())
		return false;

	*c_ppEffect = itor->second;

	return true;
}

DWORD CEffectManager::GetRandomEffect()
{
	int iIndex = random() % m_kEftDataMap.size();

	TEffectDataMap::iterator itor = m_kEftDataMap.begin();
	for (int i = 0; i < iIndex; ++i, ++itor);

	return itor->first;
}

int CEffectManager::GetEmptyIndex()
{
	static int iMaxIndex=1;

	if (iMaxIndex>2100000000)
		iMaxIndex = 1;

	int iNextIndex = iMaxIndex++;
	while(m_kEftInstMap.find(iNextIndex) != m_kEftInstMap.end())
		iNextIndex++;

	return iNextIndex;
}

void CEffectManager::DeleteAllInstances()
{
	__DestroyEffectInstanceMap();
}

void CEffectManager::__DestroyEffectInstanceMap()
{
	for (TEffectInstanceMap::iterator i = m_kEftInstMap.begin(); i != m_kEftInstMap.end(); ++i)
	{
		CEffectInstance * pkEftInst = i->second;	
		CEffectInstance::Delete(pkEftInst);			
	}

	m_kEftInstMap.clear();
	m_bSortDirty = true;
	m_kVctSortedCache.clear();
}

void CEffectManager::__DestroyEffectCacheMap()
{
	for (TEffectInstanceMap::iterator i = m_kEftCacheMap.begin(); i != m_kEftCacheMap.end(); ++i)
	{
		CEffectInstance * pkEftInst = i->second;	
		CEffectInstance::Delete(pkEftInst);			
	}

	m_kEftCacheMap.clear();
}

void CEffectManager::__DestroyEffectDataMap()
{
	for (TEffectDataMap::iterator i = m_kEftDataMap.begin(); i != m_kEftDataMap.end(); ++i)
	{
		CEffectData * pData = i->second;
		CEffectData::Delete(pData);				
	}

	m_kEftDataMap.clear();
	m_kEffectRenderClassByCRC.clear();
}

void CEffectManager::Destroy()
{	
	DestroyDX11EffectResources();
	__DestroyEffectInstanceMap();
	__DestroyEffectCacheMap();
	__DestroyEffectDataMap();
		
	__Initialize();
}

void CEffectManager::__Initialize()
{
	m_pSelectedEffectInstance = NULL;
	m_isDisableSortRendering = false;
	m_iPerfProfile = 1;
	m_bAdaptiveFX = true;
	m_bOverBudgetReduced = false;
	m_iFXStrideBias = 1;
	m_dwUpdateFrame = 0;
	m_dwRenderFrame = 0;
	m_dwSortInterval = 2;
	m_bSortDirty = true;
	m_kVctSortedCache.clear();
	m_dwDX11SubmittedEffectCount = 0;
	m_dwDX11SubmittedParticleCount = 0;      // W4.2: telemetry split
	m_dwDX11SubmittedMeshEffectCount = 0;   // W4.2: telemetry split

	// DX11 effect resources (Batch W2)
	m_pDX11EffectVertexShader = nullptr;
	m_pDX11EffectPixelShader = nullptr;
	m_pDX11EffectTargetRingPixelShader = nullptr;
	m_pDX11EffectShadowAlphaPixelShader = nullptr;
	m_pDX11EffectInputLayout = nullptr;
	m_pDX11EffectConstantBuffer = nullptr;
	m_pDX11EffectSamplerState = nullptr;
	m_pDX11EffectBlendStateAdditive = nullptr;
	m_pDX11EffectBlendStateAlpha = nullptr;
	m_pDX11EffectBlendStateScreen = nullptr;
	m_pDX11EffectNoCullRasterizerState = nullptr;
	m_pDX11EffectDepthReadOnlyState = nullptr;
	m_pDX11EffectDepthWriteState = nullptr;
	m_kDX11EffectBlendStateCache.clear();
	m_pDX11EffectDefaultTextureSRV = nullptr;
	m_pDX11EffectDynamicVB = nullptr;
	m_uDX11EffectDynamicVBCapacity = 0u;
	m_bDX11EffectResourcesReady = false;
	m_fDX11TargetRingAlphaClipThreshold = 1.0f / 255.0f;
	m_kEffectRenderClassByCRC.clear();
	m_kDX11TargetRingDiagnostics = SDX11TargetRingDiagnostics();
}

CEffectManager::CEffectManager()
{
	__Initialize();
}

CEffectManager::~CEffectManager()
{
	Destroy();
}

DWORD CEffectManager::GetSelectedEffectDataCRC() const
{
	if (!m_pSelectedEffectInstance)
		return 0;

	CEffectData* pData = m_pSelectedEffectInstance->GetEffectDataPointer();
	
	if (!pData)
		return 0;

	const char* cszFile = pData->GetFileName();

	if (!cszFile || !cszFile[0])
		return 0;

	std::string str;
	StringPath(cszFile, str);

	return GetCaseCRC32(str.c_str(), (int)str.length());
}

// ============================================================================
// DX11 Effect Rendering Infrastructure (Batch W2)
// ============================================================================

// Inline HLSL Effect Shaders
static const char* s_szDX11EffectVertexShader = R"(
cbuffer cbPerFrame : register(b0)
{
	row_major matrix g_matViewProj;
	float4 g_vColorFactor;
	float4 g_vEffectParams;
};

struct VS_INPUT
{
	float3 vPosition : POSITION;
	float2 vTexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;
	output.vPosition = mul(float4(input.vPosition, 1.0f), g_matViewProj);
	output.vTexCoord = input.vTexCoord;
	return output;
}
)";

static const char* s_szDX11EffectPixelShader = R"(
Texture2D g_txDiffuse : register(t0);
SamplerState g_sampler : register(s0);

cbuffer cbPerFrame : register(b0)
{
	row_major matrix g_matViewProj;
	float4 g_vColorFactor;
	float4 g_vEffectParams;
};

struct PS_INPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
	float4 vColor = g_txDiffuse.Sample(g_sampler, input.vTexCoord);
	vColor *= g_vColorFactor;
	return vColor;
}
)";

static const char* s_szDX11EffectTargetRingPixelShader = R"(
Texture2D g_txDiffuse : register(t0);
SamplerState g_sampler : register(s0);

cbuffer cbPerFrame : register(b0)
{
	row_major matrix g_matViewProj;
	float4 g_vColorFactor;
	float4 g_vEffectParams;
};

struct PS_INPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
	float4 vColor = g_txDiffuse.Sample(g_sampler, input.vTexCoord) * g_vColorFactor;
	clip(vColor.a - g_vEffectParams.x);
	return vColor;
}
)";

static const char* s_szDX11EffectShadowAlphaPixelShader = R"(
Texture2D g_txDiffuse : register(t0);
SamplerState g_sampler : register(s0);

cbuffer cbPerFrame : register(b0)
{
	row_major matrix g_matViewProj;
	float4 g_vColorFactor;
	float4 g_vEffectParams;
};

struct PS_INPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
};

void main(PS_INPUT input)
{
	const float fAlpha = g_txDiffuse.Sample(g_sampler, input.vTexCoord).a * g_vColorFactor.a;
	clip(fAlpha - 0.00392157f); // 1/255 alpha threshold
}
)";

bool CEffectManager::InitializeDX11EffectResources(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (m_bDX11EffectResourcesReady)
		return true;

	HRESULT hr = S_OK;
	ID3DBlob* pVSBlob = nullptr;
	ID3DBlob* pPSBlob = nullptr;
	ID3DBlob* pErrorBlob = nullptr;
	ID3DBlob* pTargetRingPSBlob = nullptr;
	ID3DBlob* pShadowPSBlob = nullptr;

	auto ReleaseBlob = [](ID3DBlob*& pBlob)
	{
		if (pBlob)
		{
			pBlob->Release();
			pBlob = nullptr;
		}
	};

	auto FailInit = [&](const char* c_szReason, HRESULT hReason) -> bool
	{
		TraceError(
			"DX11_EFFECT_RESOURCES_INIT_FAIL reason=%s hr=0x%08X",
			c_szReason,
			static_cast<unsigned int>(hReason));
		ReleaseBlob(pVSBlob);
		ReleaseBlob(pPSBlob);
		ReleaseBlob(pTargetRingPSBlob);
		ReleaseBlob(pShadowPSBlob);
		ReleaseBlob(pErrorBlob);
		DestroyDX11EffectResources();
		return false;
	};

	const D3D_FEATURE_LEVEL eFeatureLevel = pDevice->GetFeatureLevel();
	const char* c_szVSTarget = "vs_4_0";
	const char* c_szPSTarget = "ps_4_0";
	static DWORD s_dwShaderModelLogTick = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwShaderModelLogTick || (dwNow - s_dwShaderModelLogTick) >= 5000u)
	{
		s_dwShaderModelLogTick = dwNow;
		TraceError(
			"DX11_EFFECT_SHADER_MODEL feature_level=0x%04X vs_target=%s ps_target=%s",
			static_cast<unsigned int>(eFeatureLevel),
			c_szVSTarget,
			c_szPSTarget);
	}

	UINT uCompileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	uCompileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	// Compile vertex shader
	hr = D3DCompile(
		s_szDX11EffectVertexShader,
		strlen(s_szDX11EffectVertexShader),
		"EffectVS",
		nullptr,
		nullptr,
		"main",
		c_szVSTarget,
		uCompileFlags,
		0,
		&pVSBlob,
		&pErrorBlob);
	if (FAILED(hr) || !pVSBlob)
	{
		if (pErrorBlob)
			TraceError(
				"DX11_EFFECT_SHADER_COMPILE_FAIL stage=vs target=%s hr=0x%08X error=%s",
				c_szVSTarget,
				static_cast<unsigned int>(hr),
				static_cast<const char*>(pErrorBlob->GetBufferPointer()));
		return FailInit("compile_vs", hr);
	}
	ReleaseBlob(pErrorBlob);

	hr = pDevice->CreateVertexShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11EffectVertexShader);
	if (FAILED(hr))
		return FailInit("create_vs", hr);

	// Create input layout
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	hr = pDevice->CreateInputLayout(
		layout,
		ARRAYSIZE(layout),
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		&m_pDX11EffectInputLayout);
	ReleaseBlob(pVSBlob);
	if (FAILED(hr))
		return FailInit("create_input_layout", hr);

	// Compile pixel shader
	hr = D3DCompile(
		s_szDX11EffectPixelShader,
		strlen(s_szDX11EffectPixelShader),
		"EffectPS",
		nullptr,
		nullptr,
		"main",
		c_szPSTarget,
		uCompileFlags,
		0,
		&pPSBlob,
		&pErrorBlob);
	if (FAILED(hr) || !pPSBlob)
	{
		if (pErrorBlob)
			TraceError(
				"DX11_EFFECT_SHADER_COMPILE_FAIL stage=ps target=%s hr=0x%08X error=%s",
				c_szPSTarget,
				static_cast<unsigned int>(hr),
				static_cast<const char*>(pErrorBlob->GetBufferPointer()));
		return FailInit("compile_ps", hr);
	}
	ReleaseBlob(pErrorBlob);

	hr = pDevice->CreatePixelShader(
		pPSBlob->GetBufferPointer(),
		pPSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11EffectPixelShader);
	ReleaseBlob(pPSBlob);
	if (FAILED(hr))
		return FailInit("create_ps", hr);

	// Compile target-ring alpha-clip pixel shader (selection/target hover FX).
	hr = D3DCompile(
		s_szDX11EffectTargetRingPixelShader,
		strlen(s_szDX11EffectTargetRingPixelShader),
		"EffectTargetRingPS",
		nullptr,
		nullptr,
		"main",
		c_szPSTarget,
		uCompileFlags,
		0,
		&pTargetRingPSBlob,
		&pErrorBlob);
	if (FAILED(hr) || !pTargetRingPSBlob)
	{
		if (pErrorBlob)
			TraceError(
				"DX11_EFFECT_SHADER_COMPILE_FAIL stage=ps_target_ring target=%s hr=0x%08X error=%s",
				c_szPSTarget,
				static_cast<unsigned int>(hr),
				static_cast<const char*>(pErrorBlob->GetBufferPointer()));
		return FailInit("compile_ps_target_ring", hr);
	}
	ReleaseBlob(pErrorBlob);

	hr = pDevice->CreatePixelShader(
		pTargetRingPSBlob->GetBufferPointer(),
		pTargetRingPSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11EffectTargetRingPixelShader);
	ReleaseBlob(pTargetRingPSBlob);
	if (FAILED(hr))
		return FailInit("create_ps_target_ring", hr);

	// Compile shadow alpha-clip pixel shader (depth-only pass support, no color target write).
	hr = D3DCompile(
		s_szDX11EffectShadowAlphaPixelShader,
		strlen(s_szDX11EffectShadowAlphaPixelShader),
		"EffectShadowPS",
		nullptr,
		nullptr,
		"main",
		c_szPSTarget,
		uCompileFlags,
		0,
		&pShadowPSBlob,
		&pErrorBlob);
	if (FAILED(hr) || !pShadowPSBlob)
	{
		if (pErrorBlob)
			TraceError(
				"DX11_EFFECT_SHADER_COMPILE_FAIL stage=ps_shadow target=%s hr=0x%08X error=%s",
				c_szPSTarget,
				static_cast<unsigned int>(hr),
				static_cast<const char*>(pErrorBlob->GetBufferPointer()));
		return FailInit("compile_ps_shadow", hr);
	}
	ReleaseBlob(pErrorBlob);

	hr = pDevice->CreatePixelShader(
		pShadowPSBlob->GetBufferPointer(),
		pShadowPSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11EffectShadowAlphaPixelShader);
	ReleaseBlob(pShadowPSBlob);
	if (FAILED(hr))
		return FailInit("create_ps_shadow", hr);

	// Create constant buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.Usage = D3D11_USAGE_DEFAULT;
	cbDesc.ByteWidth = sizeof(D3DXMATRIX) + sizeof(D3DXVECTOR4) * 2u;
	cbDesc.ByteWidth = (cbDesc.ByteWidth + 15) & ~15;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hr = pDevice->CreateBuffer(&cbDesc, nullptr, &m_pDX11EffectConstantBuffer);
	if (FAILED(hr))
		return FailInit("create_constant_buffer", hr);

	// Create sampler state
	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = pDevice->CreateSamplerState(&sampDesc, &m_pDX11EffectSamplerState);
	if (FAILED(hr))
		return FailInit("create_sampler", hr);

	// Create particle/effect fixed pipeline states (shared and lifecycle-managed in EffectManager).
	D3D11_RASTERIZER_DESC rsDesc = {};
	rsDesc.FillMode = D3D11_FILL_SOLID;
	rsDesc.CullMode = D3D11_CULL_NONE;
	rsDesc.FrontCounterClockwise = FALSE;
	rsDesc.DepthClipEnable = TRUE;
	hr = pDevice->CreateRasterizerState(&rsDesc, &m_pDX11EffectNoCullRasterizerState);
	if (FAILED(hr))
		return FailInit("create_no_cull_rs", hr);

	D3D11_DEPTH_STENCIL_DESC dsDesc = {};
	dsDesc.DepthEnable = TRUE;
	dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	dsDesc.StencilEnable = FALSE;
	hr = pDevice->CreateDepthStencilState(&dsDesc, &m_pDX11EffectDepthReadOnlyState);
	if (FAILED(hr))
		return FailInit("create_depth_readonly_dss", hr);

	dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	hr = pDevice->CreateDepthStencilState(&dsDesc, &m_pDX11EffectDepthWriteState);
	if (FAILED(hr))
		return FailInit("create_depth_write_dss", hr);

	// Create additive blend state
	D3D11_BLEND_DESC blendDesc = {};
	BuildEffectBlendDesc(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, blendDesc);
	hr = pDevice->CreateBlendState(&blendDesc, &m_pDX11EffectBlendStateAdditive);
	if (FAILED(hr))
		return FailInit("create_blend_additive", hr);

	// Create alpha blend state
	BuildEffectBlendDesc(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, blendDesc);
	hr = pDevice->CreateBlendState(&blendDesc, &m_pDX11EffectBlendStateAlpha);
	if (FAILED(hr))
		return FailInit("create_blend_alpha", hr);

	// Create screen blend state (SRC_ALPHA, INV_SRC_COLOR)
	BuildEffectBlendDesc(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_COLOR, blendDesc);
	hr = pDevice->CreateBlendState(&blendDesc, &m_pDX11EffectBlendStateScreen);
	if (FAILED(hr))
		return FailInit("create_blend_screen", hr);

	// Create default white texture (1x1)
	unsigned char whitePixel[4] = { 255, 255, 255, 255 };
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = 1;
	texDesc.Height = 1;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = whitePixel;
	initData.SysMemPitch = 4;

	ID3D11Texture2D* pDefaultTex = nullptr;
	hr = pDevice->CreateTexture2D(&texDesc, &initData, &pDefaultTex);
	if (FAILED(hr) || !pDefaultTex)
		return FailInit("create_default_texture", hr);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	hr = pDevice->CreateShaderResourceView(pDefaultTex, &srvDesc, &m_pDX11EffectDefaultTextureSRV);
	pDefaultTex->Release();
	pDefaultTex = nullptr;
	if (FAILED(hr))
		return FailInit("create_default_srv", hr);

	// Create dynamic vertex buffer for effect rendering with explicit capacity tracking.
	if (!CreateDX11EffectDynamicVB(pDevice, kDefaultEffectDynamicVBVertexCapacity))
		return FailInit("create_dynamic_vb", E_FAIL);

	m_bDX11EffectResourcesReady = true;
	TraceError("DX11_EFFECT_RESOURCES_INITIALIZED success");
	return true;
}

bool CEffectManager::EnsureDX11EffectResourcesReady()
{
	if (m_bDX11EffectResourcesReady)
		return true;

	static DWORD s_dwLastInitAttemptMS = 0u;
	static DWORD s_dwLastInitFailLogMS = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (s_dwLastInitAttemptMS != 0u && (dwNow - s_dwLastInitAttemptMS) < 2000u)
		return false;
	s_dwLastInitAttemptMS = dwNow;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		if (0u == s_dwLastInitFailLogMS || (dwNow - s_dwLastInitFailLogMS) >= 5000u)
		{
			s_dwLastInitFailLogMS = dwNow;
			TraceError("DX11_EFFECT_RESOURCES_RETRY_SKIP reason=dx11_device_unavailable");
		}
		return false;
	}

	ID3D11Device* pDevice = pDX11Device->GetDevice();
	if (!pDevice)
	{
		if (0u == s_dwLastInitFailLogMS || (dwNow - s_dwLastInitFailLogMS) >= 5000u)
		{
			s_dwLastInitFailLogMS = dwNow;
			TraceError("DX11_EFFECT_RESOURCES_RETRY_SKIP reason=dx11_device_null");
		}
		return false;
	}

	if (!InitializeDX11EffectResources(pDevice))
	{
		if (0u == s_dwLastInitFailLogMS || (dwNow - s_dwLastInitFailLogMS) >= 5000u)
		{
			s_dwLastInitFailLogMS = dwNow;
			TraceError("DX11_EFFECT_RESOURCES_RETRY_FAIL");
		}
		return false;
	}

	return true;
}

void CEffectManager::DestroyDX11EffectResources()
{
	DestroyDX11EffectBlendCache();

	if (m_pDX11EffectDefaultTextureSRV)
	{
		m_pDX11EffectDefaultTextureSRV->Release();
		m_pDX11EffectDefaultTextureSRV = nullptr;
	}

	if (m_pDX11EffectDynamicVB)
	{
		m_pDX11EffectDynamicVB->Release();
		m_pDX11EffectDynamicVB = nullptr;
	}
	m_uDX11EffectDynamicVBCapacity = 0u;

	if (m_pDX11EffectDepthReadOnlyState)
	{
		m_pDX11EffectDepthReadOnlyState->Release();
		m_pDX11EffectDepthReadOnlyState = nullptr;
	}

	if (m_pDX11EffectDepthWriteState)
	{
		m_pDX11EffectDepthWriteState->Release();
		m_pDX11EffectDepthWriteState = nullptr;
	}

	if (m_pDX11EffectNoCullRasterizerState)
	{
		m_pDX11EffectNoCullRasterizerState->Release();
		m_pDX11EffectNoCullRasterizerState = nullptr;
	}

	if (m_pDX11EffectBlendStateAlpha)
	{
		m_pDX11EffectBlendStateAlpha->Release();
		m_pDX11EffectBlendStateAlpha = nullptr;
	}

	if (m_pDX11EffectBlendStateScreen)
	{
		m_pDX11EffectBlendStateScreen->Release();
		m_pDX11EffectBlendStateScreen = nullptr;
	}

	if (m_pDX11EffectBlendStateAdditive)
	{
		m_pDX11EffectBlendStateAdditive->Release();
		m_pDX11EffectBlendStateAdditive = nullptr;
	}

	if (m_pDX11EffectSamplerState)
	{
		m_pDX11EffectSamplerState->Release();
		m_pDX11EffectSamplerState = nullptr;
	}

	if (m_pDX11EffectConstantBuffer)
	{
		m_pDX11EffectConstantBuffer->Release();
		m_pDX11EffectConstantBuffer = nullptr;
	}

	if (m_pDX11EffectInputLayout)
	{
		m_pDX11EffectInputLayout->Release();
		m_pDX11EffectInputLayout = nullptr;
	}

	if (m_pDX11EffectPixelShader)
	{
		m_pDX11EffectPixelShader->Release();
		m_pDX11EffectPixelShader = nullptr;
	}

	if (m_pDX11EffectTargetRingPixelShader)
	{
		m_pDX11EffectTargetRingPixelShader->Release();
		m_pDX11EffectTargetRingPixelShader = nullptr;
	}

	if (m_pDX11EffectShadowAlphaPixelShader)
	{
		m_pDX11EffectShadowAlphaPixelShader->Release();
		m_pDX11EffectShadowAlphaPixelShader = nullptr;
	}

	if (m_pDX11EffectVertexShader)
	{
		m_pDX11EffectVertexShader->Release();
		m_pDX11EffectVertexShader = nullptr;
	}

	m_bDX11EffectResourcesReady = false;
}

bool CEffectManager::CreateDX11EffectDynamicVB(ID3D11Device* pDevice, uint32_t uVertexCapacity)
{
	if (!pDevice || uVertexCapacity == 0u)
		return false;

	const uint64_t ullByteWidth = static_cast<uint64_t>(sizeof(TPTVertex)) * static_cast<uint64_t>(uVertexCapacity);
	if (ullByteWidth == 0ull || ullByteWidth > static_cast<uint64_t>(0xffffffffu))
	{
		TraceError(
			"DX11_EFFECT_DYNAMIC_VB_CREATE_FAIL reason=invalid_size vertices=%u",
			static_cast<unsigned int>(uVertexCapacity));
		return false;
	}

	D3D11_BUFFER_DESC vbDesc = {};
	vbDesc.Usage = D3D11_USAGE_DYNAMIC;
	vbDesc.ByteWidth = static_cast<UINT>(ullByteWidth);
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	ID3D11Buffer* pNewVB = nullptr;
	const HRESULT hr = pDevice->CreateBuffer(&vbDesc, nullptr, &pNewVB);
	if (FAILED(hr) || !pNewVB)
	{
		TraceError(
			"DX11_EFFECT_DYNAMIC_VB_CREATE_FAIL reason=create_buffer vertices=%u hr=0x%08X",
			static_cast<unsigned int>(uVertexCapacity),
			static_cast<unsigned int>(hr));
		return false;
	}

	if (m_pDX11EffectDynamicVB)
		m_pDX11EffectDynamicVB->Release();
	m_pDX11EffectDynamicVB = pNewVB;
	m_uDX11EffectDynamicVBCapacity = uVertexCapacity;
	return true;
}

bool CEffectManager::EnsureDX11EffectDynamicVB(uint32_t uMinVertexCount)
{
	if (!m_bDX11EffectResourcesReady)
		return false;

	if (uMinVertexCount == 0u)
		uMinVertexCount = 1u;

	if (m_pDX11EffectDynamicVB && m_uDX11EffectDynamicVBCapacity >= uMinVertexCount)
		return true;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice || !pGrpDevice->IsValid())
		return false;

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	if (!pDevice)
		return false;

	uint32_t uTargetCapacity = RoundUpToPowerOfTwo(uMinVertexCount);
	if (uTargetCapacity < kDefaultEffectDynamicVBVertexCapacity)
		uTargetCapacity = kDefaultEffectDynamicVBVertexCapacity;

	const uint32_t uPreviousCapacity = m_uDX11EffectDynamicVBCapacity;
	if (!CreateDX11EffectDynamicVB(pDevice, uTargetCapacity))
	{
		TraceError(
			"DX11_EFFECT_DYNAMIC_VB_RESIZE_FAIL requested=%u previous=%u",
			static_cast<unsigned int>(uMinVertexCount),
			static_cast<unsigned int>(uPreviousCapacity));
		return false;
	}

	TraceError(
		"DX11_EFFECT_DYNAMIC_VB_RESIZE_OK requested=%u previous=%u new=%u",
		static_cast<unsigned int>(uMinVertexCount),
		static_cast<unsigned int>(uPreviousCapacity),
		static_cast<unsigned int>(uTargetCapacity));
	return true;
}

ID3D11ShaderResourceView* CEffectManager::GetEffectTextureSRV(CGraphicImageInstance* pImageInstance)
{
	// T2: Load effect texture via DX11 helper (DDS preferred)
	if (!pImageInstance)
		return m_pDX11EffectDefaultTextureSRV;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return m_pDX11EffectDefaultTextureSRV;

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	if (!pDevice)
		return m_pDX11EffectDefaultTextureSRV;

	// Try to get filename from image
	CGraphicImage* pImage = pImageInstance->GetGraphicImagePointer();
	if (pImage)
	{
		const char* szFilename = pImage->GetFileName();
		if (szFilename && szFilename[0])
		{
			// M3-TEXTURE-ASYNC-10: Load via DX11 async helper (DDS preferred, WIC fallback)
			// Returns cached texture if available, or white fallback while loading asynchronously
			// PRIORITY_NORMAL: Effects are not critical, can use fallback for a few frames
			ID3D11ShaderResourceView* pSRV = CGraphicTextureDX11::LoadTextureAsync(
				pDevice,
				szFilename,
				CGraphicTextureDX11::PRIORITY_NORMAL,
				nullptr,  // No callback needed - effect renders every frame, will pick up cached texture when ready
				true);    // Enable cache
			if (pSRV)
				return pSRV;

			static std::unordered_set<std::string> s_kNativeOnlyEffectFallbackLogged;
			if (s_kNativeOnlyEffectFallbackLogged.insert(szFilename).second)
			{
				TraceError(
					"DX11_TEXTURE_LOAD path=native_only_default domain=effect file=%s reason=native_load_failed",
					szFilename);
			}
		}
	}

	// Final fallback: default texture without DX9 bridge.
	return m_pDX11EffectDefaultTextureSRV;
}

uint32_t CEffectManager::LegacyBlendToDX11Blend(BYTE byBlendType) const
{
	switch (byBlendType)
	{
		case GRP_BLEND_ZERO: return static_cast<uint32_t>(D3D11_BLEND_ZERO);
		case GRP_BLEND_ONE: return static_cast<uint32_t>(D3D11_BLEND_ONE);
		case GRP_BLEND_SRCCOLOR: return static_cast<uint32_t>(D3D11_BLEND_SRC_COLOR);
		case GRP_BLEND_INVSRCCOLOR: return static_cast<uint32_t>(D3D11_BLEND_INV_SRC_COLOR);
		case GRP_BLEND_SRCALPHA: return static_cast<uint32_t>(D3D11_BLEND_SRC_ALPHA);
		case GRP_BLEND_INVSRCALPHA: return static_cast<uint32_t>(D3D11_BLEND_INV_SRC_ALPHA);
		case GRP_BLEND_DESTALPHA: return static_cast<uint32_t>(D3D11_BLEND_DEST_ALPHA);
		case GRP_BLEND_INVDESTALPHA: return static_cast<uint32_t>(D3D11_BLEND_INV_DEST_ALPHA);
		case GRP_BLEND_DESTCOLOR: return static_cast<uint32_t>(D3D11_BLEND_DEST_COLOR);
		case GRP_BLEND_INVDESTCOLOR: return static_cast<uint32_t>(D3D11_BLEND_INV_DEST_COLOR);
		case GRP_BLEND_SRCALPHASAT: return static_cast<uint32_t>(D3D11_BLEND_SRC_ALPHA_SAT);
		case GRP_BLEND_BLENDFACTOR: return static_cast<uint32_t>(D3D11_BLEND_BLEND_FACTOR);
		case GRP_BLEND_INVBLENDFACTOR: return static_cast<uint32_t>(D3D11_BLEND_INV_BLEND_FACTOR);
		default:
			return static_cast<uint32_t>(D3D11_BLEND_SRC_ALPHA);
	}
}

namespace
{
	bool IsSupportedLegacyBlendType(BYTE byBlendType)
	{
		switch (byBlendType)
		{
			case GRP_BLEND_ZERO:
			case GRP_BLEND_ONE:
			case GRP_BLEND_SRCCOLOR:
			case GRP_BLEND_INVSRCCOLOR:
			case GRP_BLEND_SRCALPHA:
			case GRP_BLEND_INVSRCALPHA:
			case GRP_BLEND_DESTALPHA:
			case GRP_BLEND_INVDESTALPHA:
			case GRP_BLEND_DESTCOLOR:
			case GRP_BLEND_INVDESTCOLOR:
			case GRP_BLEND_SRCALPHASAT:
				return true;
			case GRP_BLEND_BLENDFACTOR:
				return true;
			case GRP_BLEND_INVBLENDFACTOR:
				return true;
			default:
				return false;
		}
	}
}

void CEffectManager::NormalizeLegacyBlendPair(BYTE& bySrcBlend, BYTE& byDestBlend) const
{
	if (bySrcBlend == GRP_BLEND_BOTHSRCALPHA || byDestBlend == GRP_BLEND_BOTHSRCALPHA)
	{
		bySrcBlend = static_cast<BYTE>(GRP_BLEND_SRCALPHA);
		byDestBlend = static_cast<BYTE>(GRP_BLEND_INVSRCALPHA);
		return;
	}

	if (bySrcBlend == GRP_BLEND_BOTHINVSRCALPHA || byDestBlend == GRP_BLEND_BOTHINVSRCALPHA)
	{
		bySrcBlend = static_cast<BYTE>(GRP_BLEND_INVSRCALPHA);
		byDestBlend = static_cast<BYTE>(GRP_BLEND_SRCALPHA);
	}
}

uint32_t CEffectManager::MakeBlendCacheKey(BYTE bySrcBlend, BYTE byDestBlend) const
{
	return (static_cast<uint32_t>(bySrcBlend) << 8u) | static_cast<uint32_t>(byDestBlend);
}

void CEffectManager::DestroyDX11EffectBlendCache()
{
	for (std::unordered_map<uint32_t, ID3D11BlendState*>::iterator it = m_kDX11EffectBlendStateCache.begin();
		it != m_kDX11EffectBlendStateCache.end();
		++it)
	{
		if (it->second)
			it->second->Release();
	}

	m_kDX11EffectBlendStateCache.clear();
}

ID3D11BlendState* CEffectManager::GetOrCreateDX11EffectBlendState(BYTE bySrcBlend, BYTE byDestBlend)
{
	if (!m_bDX11EffectResourcesReady)
		return nullptr;

	NormalizeLegacyBlendPair(bySrcBlend, byDestBlend);

	if (!IsSupportedLegacyBlendType(bySrcBlend) || !IsSupportedLegacyBlendType(byDestBlend))
	{
		static std::unordered_set<uint32_t> s_kUnsupportedBlendPairLogOnce;
		const uint32_t dwUnsupportedKey = MakeBlendCacheKey(bySrcBlend, byDestBlend);
		if (s_kUnsupportedBlendPairLogOnce.insert(dwUnsupportedKey).second)
		{
			TraceError(
				"DX11_EFFECT_BLEND_UNSUPPORTED src=%u dst=%u fallback=alpha",
				static_cast<unsigned>(bySrcBlend),
				static_cast<unsigned>(byDestBlend));
		}
		return m_pDX11EffectBlendStateAlpha;
	}

	if (bySrcBlend == GRP_BLEND_SRCALPHA && byDestBlend == GRP_BLEND_ONE)
		return m_pDX11EffectBlendStateAdditive;
	if (bySrcBlend == GRP_BLEND_SRCALPHA && byDestBlend == GRP_BLEND_INVSRCALPHA)
		return m_pDX11EffectBlendStateAlpha;
	if (bySrcBlend == GRP_BLEND_SRCALPHA && byDestBlend == GRP_BLEND_INVSRCCOLOR)
		return m_pDX11EffectBlendStateScreen;

	const uint32_t dwKey = MakeBlendCacheKey(bySrcBlend, byDestBlend);
	std::unordered_map<uint32_t, ID3D11BlendState*>::iterator itCached = m_kDX11EffectBlendStateCache.find(dwKey);
	if (itCached != m_kDX11EffectBlendStateCache.end())
		return itCached->second;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice || !pGrpDevice->IsValid())
		return m_pDX11EffectBlendStateAlpha;

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	if (!pDevice)
		return m_pDX11EffectBlendStateAlpha;

	const D3D11_BLEND srcBlend = static_cast<D3D11_BLEND>(LegacyBlendToDX11Blend(bySrcBlend));
	const D3D11_BLEND dstBlend = static_cast<D3D11_BLEND>(LegacyBlendToDX11Blend(byDestBlend));
	D3D11_BLEND_DESC blendDesc = {};
	BuildEffectBlendDesc(srcBlend, dstBlend, blendDesc);

	ID3D11BlendState* pBlendState = nullptr;
	const HRESULT hr = pDevice->CreateBlendState(&blendDesc, &pBlendState);
	if (FAILED(hr) || !pBlendState)
	{
		TraceError(
			"DX11_EFFECT_BLEND_CREATE_FAIL src=%u dst=%u hr=0x%08X fallback=alpha",
			static_cast<unsigned>(bySrcBlend),
			static_cast<unsigned>(byDestBlend),
			static_cast<unsigned>(hr));
		return m_pDX11EffectBlendStateAlpha;
	}

	m_kDX11EffectBlendStateCache.insert(std::unordered_map<uint32_t, ID3D11BlendState*>::value_type(dwKey, pBlendState));
	TraceError(
		"DX11_EFFECT_BLEND_CREATE_OK src=%u dst=%u",
		static_cast<unsigned>(bySrcBlend),
		static_cast<unsigned>(byDestBlend));
	return pBlendState;
}

ID3D11BlendState* CEffectManager::ResolveDX11EffectBlendState(bool bBlendingEnable, BYTE bySrcBlend, BYTE byDestBlend)
{
	if (!bBlendingEnable)
		return nullptr;

	ID3D11BlendState* pBlendState = GetOrCreateDX11EffectBlendState(bySrcBlend, byDestBlend);
	if (!pBlendState)
		return m_pDX11EffectBlendStateAlpha;

	return pBlendState;
}
