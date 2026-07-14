#include "StdAfx.h"
#include "MapOutdoor.h"
#include "TerrainPatch.h"
#include "AreaTerrain.h"
#include "TerrainQuadtree.h"

#include "EterLib/Camera.h"
#include "EterLib/StateManager11.h"
#include "EterLib/GrpLightManager.h"
#include "EterLib/GrpDeviceDX11.h"
#include "EffectLib/EffectManager.h"
#include "UserInterface/config.h"


#define MAX_RENDER_SPALT 150

CArea::TCRCWithNumberVector m_dwRenderedCRCWithNumberVector;

namespace
{
class CScopedDX11LightFlush
{
public:
	CScopedDX11LightFlush()
		: m_bActive(false)
	{
		CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
		CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
		if (!pDX11Device || !pDX11Device->IsValid() || !pStateManager11)
			return;

		CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
		if (pCamera)
			CLightManager::Instance().SetCenterPosition(pCamera->GetEye());

		CLightManager::Instance().FlushLight();
		m_bActive = true;
	}

	~CScopedDX11LightFlush()
	{
		if (!m_bActive)
			return;

		CLightManager::Instance().RestoreLight();
	}

private:
	bool m_bActive;
};
}

void CMapOutdoor::RenderTerrain()
{
	if (!IsVisiblePart(PART_TERRAIN))
		return;

	if (!m_bSettingTerrainVisible)
		return;

	if (!m_pTerrainPatchProxyList)
		return;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedDX11TerrainDeviceUnavailable = false;
		if (!s_bLoggedDX11TerrainDeviceUnavailable)
		{
			s_bLoggedDX11TerrainDeviceUnavailable = true;
			TraceError("DX11_TERRAIN_RENDER_FAIL reason=dx11_device_unavailable");
		}
		return;
	}

	ID3D11Device* pDevice = pDX11Device->GetDevice();
	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pDevice || !pContext)
	{
		static bool s_bLoggedDX11TerrainContextUnavailable = false;
		if (!s_bLoggedDX11TerrainContextUnavailable)
		{
			s_bLoggedDX11TerrainContextUnavailable = true;
			TraceError("DX11_TERRAIN_RENDER_FAIL reason=dx11_context_unavailable");
		}
		return;
	}

	RenderTerrainDX11(pDevice, pContext, nullptr, nullptr, nullptr);
}

// 2004. 2. 17. myevan. 모든 부분을 보이게 초기화 한다
void CMapOutdoor::InitializeVisibleParts()
{
	m_dwVisiblePartFlags=0xffffffff;
}

// 2004. 2. 17. myevan. 특정 부분을 보이게 하거나 감추는 함수
void CMapOutdoor::SetVisiblePart(int ePart, bool isVisible)
{
	DWORD dwMask=(1<<ePart);
	if (isVisible)
	{
		m_dwVisiblePartFlags|=dwMask;
	}	
	else
	{
		DWORD dwReverseMask=~dwMask;
		m_dwVisiblePartFlags&=dwReverseMask;
	}
}

// 2004. 2. 17. myevan. 특정 부분이 보이는지 알아내는 함수
bool CMapOutdoor::IsVisiblePart(int ePart)
{
	DWORD dwMask=(1<<ePart);
	if (dwMask & m_dwVisiblePartFlags)
		return true;

	return false;
}

// Splat 개수 제한
void CMapOutdoor::SetSplatLimit(int iSplatNum)
{
	m_iSplatLimit = iSplatNum;
}

std::vector<int> & CMapOutdoor::GetRenderedSplatNum(int * piPatch, int * piSplat, float * pfSplatRatio)
{	
	*piPatch = m_iRenderedPatchNum;
	*piSplat = m_iRenderedSplatNum;
	if (m_iRenderedPatchNum > 0)
		*pfSplatRatio = m_iRenderedSplatNumSqSum / float(m_iRenderedPatchNum);
	else
		*pfSplatRatio = 0.0f;

	return m_RenderedTextureNumVector;
}

CArea::TCRCWithNumberVector & CMapOutdoor::GetRenderedGraphicThingInstanceNum(DWORD * pdwGraphicThingInstanceNum, DWORD * pdwCRCNum)
{
	*pdwGraphicThingInstanceNum = m_dwRenderedGraphicThingInstanceNum;
	*pdwCRCNum = m_dwRenderedCRCNum;

	return m_dwRenderedCRCWithNumberVector;
}

void CMapOutdoor::RenderBeforeLensFlare()
{
	if (!mc_pEnvironmentData)
	{
		TraceError("CMapOutdoor::RenderBeforeLensFlare mc_pEnvironmentData is NULL");
		return;
	}

	m_LensFlare.Compute(mc_pEnvironmentData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction);
	m_LensFlare.DrawBeforeFlare();
}

void CMapOutdoor::RenderAfterLensFlare()
{
	m_LensFlare.AdjustBrightness();
	m_LensFlare.DrawFlare();
}

void CMapOutdoor::RenderCollision()
{
	for (int i = 0; i < AROUND_AREA_NUM; ++i)
	{
		CArea * pArea;
		if (GetAreaPointer(i, &pArea))
			pArea->RenderCollision();
	}
}

void CMapOutdoor::RenderScreenFiltering()
{
	m_ScreenFilter.Render();
}

void CMapOutdoor::RenderSky()
{
	if (IsVisiblePart(PART_SKY))
		m_SkyBox.Render();
}

void CMapOutdoor::RenderCloud()
{
	if (IsVisiblePart(PART_CLOUD))
		m_SkyBox.RenderCloud();
}

void CMapOutdoor::RenderTree()
{
	m_dwDX11LastSubmittedSpeedTreeCount = 0u;

	if (IsVisiblePart(PART_TREE))
	{
		// Ensure SpeedTree DX11 resources are available before Render() attempts DX11 billboard path.
		CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
		if (pDX11Device && pDX11Device->IsValid() && !CSpeedTreeForestDirectX::Instance().IsDX11SpeedTreeResourcesReady())
			CSpeedTreeForestDirectX::Instance().InitializeDX11SpeedTreeResources(pDX11Device->GetDevice());

		CSpeedTreeForestDirectX& rkForest = CSpeedTreeForestDirectX::Instance();
		rkForest.Render();
		m_dwDX11LastSubmittedSpeedTreeCount = rkForest.GetLastDX11SubmittedInstanceCount();
	}
}

void CMapOutdoor::SetInverseViewAndDynamicShaodwMatrices()
{
	CCamera * pCamera = CCameraManager::Instance().GetCurrentCamera();

	if (!pCamera)
		return;

	m_matViewInverse = pCamera->GetInverseViewMatrix();
	
	DirectX::SimpleMath::Vector3 v3Target = pCamera->GetTarget();

	DirectX::SimpleMath::Vector3 v3LightEye(v3Target.x - 1.732f * 1250.0f,
						   v3Target.y - 1250.0f,
						   v3Target.z + 2.0f * 1.732f * 1250.0f);

	// M3-WORLD-MATERIAL-59: Migrated from D3DXMatrixLookAtRH to DirectX::SimpleMath::Matrix::CreateLookAt
	const DirectX::SimpleMath::Vector3 vUp(0.0f, 0.0f, 1.0f);
	m_matLightView = DirectX::SimpleMath::Matrix::CreateLookAt(v3LightEye, v3Target, vUp);
	m_matDynamicShadow = m_matViewInverse * m_matLightView * m_matDynamicShadowScale;
}

void CMapOutdoor::OnRender()
{
#ifdef __PERFORMANCE_CHECKER__
	DWORD t1=ELTimer_GetMSec();
	SetInverseViewAndDynamicShaodwMatrices();

	SetBlendOperation();
	CScopedDX11LightFlush kScopedLightFlush;
	// M3-SKY-BLEND-FIX-74: Apply fog to GPU before rendering
	__ApplyFogToGPU();
	DWORD t2=ELTimer_GetMSec();
	RenderArea();
	DWORD t3=ELTimer_GetMSec();
	if (!m_bEnableTerrainOnlyForHeight)
		RenderTerrain();
	DWORD t4=ELTimer_GetMSec();
	RenderTree();
	DWORD t5=ELTimer_GetMSec();
	DWORD tEnd=ELTimer_GetMSec();

	if (tEnd-t1<7)
		return;

	static FILE* fp=fopen("perf_map_render.txt", "w");
 	fprintf(fp, "MAP.Total %d (Time %d)\n", tEnd-t1, ELTimer_GetMSec());
	fprintf(fp, "MAP.ENV %d\n", t2-t1);
	fprintf(fp, "MAP.OBJ %d\n", t3-t2);
	fprintf(fp, "MAP.TRN %d\n", t4-t3);
	fprintf(fp, "MAP.TRE %d\n", t5-t4);

#else
	SetInverseViewAndDynamicShaodwMatrices();

	SetBlendOperation();
	CScopedDX11LightFlush kScopedLightFlush;
	// M3-SKY-BLEND-FIX-74: Apply fog to GPU before rendering
	__ApplyFogToGPU();
	RenderArea();
	RenderTree();
	if (!m_bEnableTerrainOnlyForHeight)
		RenderTerrain();
	RenderBlendArea();
#endif
}

struct FAreaRenderShadow
{
	void operator () (CGraphicObjectInstance * pInstance)
	{
		pInstance->RenderShadow();
		pInstance->Hide();
	}
};

struct FPCBlockerHide
{
	void operator () (CGraphicObjectInstance * pInstance)
	{
		pInstance->Hide();
	}
};

struct FRenderPCBlocker
{
	void operator () (CGraphicObjectInstance * pInstance)
	{
		if (!pInstance)
			return;

		pInstance->Show();

		CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
		if (pDX11Device && pDX11Device->IsValid())
		{
			static DWORD s_dwLastParityLog = 0;
			const DWORD dwNow = ELTimer_GetMSec();
			if (dwNow - s_dwLastParityLog >= 5000)
			{
				s_dwLastParityLog = dwNow;
				TraceError("DX11_PIPELINE_STATE_PARITY pass=pc_blocker mode=dx11_native");
			}

			// Strict/native path: always use dedicated blocker render entry.
			// ThingInstance::OnRenderPCBlocker is already routed to DX11 in strict mode.
			pInstance->RenderPCBlocker();
			return;
		}

		static bool s_bLoggedPCBlockerDX11Missing = false;
		if (!s_bLoggedPCBlockerDX11Missing)
		{
			s_bLoggedPCBlockerDX11Missing = true;
			TraceError("DX11_PIPELINE_STATE_PARITY pass=pc_blocker mode=dx11_unavailable");
		}
	}
};

void CMapOutdoor::RenderEffect()
{
	m_dwDX11LastSubmittedEffectCount = 0u;
	m_dwDX11LastSubmittedEffectParticleCount = 0u;
	m_dwDX11LastSubmittedEffectMeshCount = 0u;

	if (!IsVisiblePart(PART_OBJECT))
		return;
	for (int i = 0; i < AROUND_AREA_NUM; ++i)
	{
		CArea * pArea;
		if (GetAreaPointer(i, &pArea))
		{
			pArea->RenderEffect();
		}
	}
	CEffectManager& rkEffectManager = CEffectManager::Instance();
	m_dwDX11LastSubmittedEffectCount = rkEffectManager.GetDX11SubmittedEffectCount();
	m_dwDX11LastSubmittedEffectParticleCount = rkEffectManager.GetDX11SubmittedParticleCount();
	m_dwDX11LastSubmittedEffectMeshCount = rkEffectManager.GetDX11SubmittedMeshEffectCount();
}

struct CMapOutdoor_LessThingInstancePtrRenderOrder
{
	bool operator() (CGraphicThingInstance* pkLeft, CGraphicThingInstance* pkRight)
	{
		// Camera-position-based sorting can be reintroduced here if required.
		CCamera * pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
		const DirectX::SimpleMath::Vector3 & c_rv3CameraPos = pCurrentCamera->GetEye();
		const DirectX::SimpleMath::Vector3 & c_v3LeftPos  = pkLeft->GetPosition();
		const DirectX::SimpleMath::Vector3 & c_v3RightPos = pkRight->GetPosition();
		// M3-WORLD-MATERIAL-59: Migrated from D3DXVec3LengthSq to DirectX::SimpleMath::Vector3::LengthSquared
		const DirectX::SimpleMath::Vector3 vv = c_rv3CameraPos - c_v3RightPos;
		const DirectX::SimpleMath::Vector3 vv2 = c_rv3CameraPos - c_v3LeftPos;

		return vv2.LengthSquared() < vv.LengthSquared();
	}
};

struct CMapOutdoor_FOpaqueThingInstanceRender
{
	inline void operator () (CGraphicThingInstance * pkThingInst)
	{
		pkThingInst->Render();
	}
};
struct CMapOutdoor_FBlendThingInstanceRender
{
	inline void operator () (CGraphicThingInstance * pkThingInst)
	{
		pkThingInst->BlendRender();
	}
};

void CMapOutdoor::RenderArea(bool bRenderAmbience)
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedObjectDX11Unavailable = false;
		if (!s_bLoggedObjectDX11Unavailable)
		{
			s_bLoggedObjectDX11Unavailable = true;
			TraceError("DX11_PIPELINE_STATE_PARITY pass=object mode=dx11_unavailable");
		}
		return;
	}

	if (!IsVisiblePart(PART_OBJECT))
		return;

	m_dwDX11LastSubmittedObjectCount = 0u;
	CGraphicThingInstance::ResetDX11SubmittedDrawCount();
	if (m_bDX11ShadowResourcesReady)
		RenderShadowReceiversDX11(pDX11Device->GetContext());
	__RenderObjectsDX11(pDX11Device->GetDevice(), pDX11Device->GetContext());
	__RenderCharactersDX11(pDX11Device->GetContext());
	m_dwDX11LastSubmittedObjectCount = CGraphicThingInstance::GetDX11SubmittedDrawCount();

	static DWORD s_dwLastObjectNativeLog = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwLastObjectNativeLog || (dwNow - s_dwLastObjectNativeLog) >= 5000u)
	{
		s_dwLastObjectNativeLog = dwNow;
		TraceError("DX11_PIPELINE_STATE_PARITY pass=object path=dx11_native");
		TraceError("DX11_PIPELINE_SUBMIT_PARITY pass=object wrapper_submitted=%u last_object_counter=%u",
			m_dwDX11LastSubmittedObjectCount,
			m_dwDX11LastSubmittedObjectCount);
	}
}

void CMapOutdoor::RenderBlendArea()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		static DWORD s_dwLastBlendNativeLog = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastBlendNativeLog || (dwNow - s_dwLastBlendNativeLog) >= 5000u)
		{
			s_dwLastBlendNativeLog = dwNow;
			TraceError("DX11_PIPELINE_STATE_PARITY pass=object_blend path=dx11_native");
			TraceError("DX11_PIPELINE_SUBMIT_PARITY pass=object_blend submitted=%u",
				CGraphicThingInstance::GetDX11SubmittedDrawCount());
		}
		return;
	}

	static bool s_bLoggedBlendDX11Unavailable = false;
	if (!s_bLoggedBlendDX11Unavailable)
	{
		s_bLoggedBlendDX11Unavailable = true;
		TraceError("DX11_PIPELINE_STATE_PARITY pass=object_blend mode=dx11_unavailable");
	}
}

void CMapOutdoor::RenderDungeon()
{
	for (int i = 0; i < AROUND_AREA_NUM; ++i)
	{
		CArea * pArea;
		if (!GetAreaPointer(i, &pArea))
			continue;
		pArea->RenderDungeon();
	}
}

void CMapOutdoor::RenderPCBlocker()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		ID3D11DeviceContext* pContext = pDX11Device->GetContext();
		if (!pContext)
			return;

		if (!m_pDX11ObjectVS || !m_pDX11ObjectPS || !m_pDX11ObjectInputLayout || !m_pDX11ObjectConstantBuffer)
		{
			ID3D11Device* pDevice = nullptr;
			pContext->GetDevice(&pDevice);
			if (pDevice)
			{
				__CreateDX11ObjectShaders(pDevice);
				pDevice->Release();
			}
		}

		if (!m_pDX11ObjectVS || !m_pDX11ObjectPS || !m_pDX11ObjectInputLayout || !m_pDX11ObjectConstantBuffer)
		{
			static DWORD s_dwLastPCBlockerShaderFailLogMS = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwLastPCBlockerShaderFailLogMS || (dwNow - s_dwLastPCBlockerShaderFailLogMS) >= 2000u)
			{
				s_dwLastPCBlockerShaderFailLogMS = dwNow;
				TraceError("DX11_PC_BLOCKER_SHADER_BIND_FAIL vs=%p ps=%p layout=%p cb=%p",
					m_pDX11ObjectVS, m_pDX11ObjectPS, m_pDX11ObjectInputLayout, m_pDX11ObjectConstantBuffer);
			}
			return;
		}

		CGrannyModelInstance::SetDX11ObjectShaders(
			m_pDX11ObjectVS,
			m_pDX11ObjectPS,
			m_pDX11ObjectInputLayout,
			m_pDX11ObjectConstantBuffer,
			m_pDX11ObjectSamplerState,
			D3DXVECTOR4(0.0f, 0.0f, -1.0f, 0.0f),
			D3DXVECTOR4(0.62f, 0.62f, 0.62f, 0.0f));

		size_t submittedCount = 0;
		for (auto* pInstance : m_PCBlockerVector)
		{
			if (!pInstance)
				continue;

			FRenderPCBlocker{}(pInstance);
			++submittedCount;
		}

		static DWORD s_dwLastPCBlockerParityLog = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (dwNow - s_dwLastPCBlockerParityLog >= 5000)
		{
			s_dwLastPCBlockerParityLog = dwNow;
			TraceError("DX11_PIPELINE_STATE_PARITY pass=pc_blocker path=dx11_native");
			TraceError("DX11_PIPELINE_SUBMIT_PARITY pass=pc_blocker expected=%u submitted=%u",
				static_cast<unsigned>(m_PCBlockerVector.size()),
				static_cast<unsigned>(submittedCount));
		}

		return;
	}

	static bool s_bLoggedPCBlockerDX11Unavailable = false;
	if (!s_bLoggedPCBlockerDX11Unavailable)
	{
		s_bLoggedPCBlockerDX11Unavailable = true;
		TraceError("DX11_PIPELINE_STATE_PARITY pass=pc_blocker mode=dx11_unavailable");
	}
}

void CMapOutdoor::SelectIndexBuffer(BYTE byLODLevel, WORD * pwPrimitiveCount, GrpPrimitiveType * pePrimitiveType)
{
	// Legacy DX9 index binding path removed. Keep primitive metadata only.
	if (0 == byLODLevel)
	{
		*pwPrimitiveCount = m_wNumIndices[byLODLevel] - 2;
		*pePrimitiveType = GRP_PT_TRIANGLESTRIP;
	}
	else
	{
		*pwPrimitiveCount =  m_wNumIndices[byLODLevel]/3;
		*pePrimitiveType = GRP_PT_TRIANGLELIST;
	}
}

void CMapOutdoor::SetPatchDrawVector()
{
	assert(NULL!=m_pTerrainPatchProxyList && "CMapOutdoor::__SetPatchDrawVector");

	m_PatchDrawStructVector.clear();

	std::vector<std::pair<float, long> >::iterator aDistancePatchVectorIterator;

	TPatchDrawStruct aPatchDrawStruct;

	aDistancePatchVectorIterator = m_PatchVector.begin();
	while(aDistancePatchVectorIterator != m_PatchVector.end())
	{
		std::pair<float, long> adistancePatchPair = *aDistancePatchVectorIterator;

		CTerrainPatchProxy * pTerrainPatchProxy = &m_pTerrainPatchProxyList[adistancePatchPair.second];

		if (!pTerrainPatchProxy->isUsed())
		{
			++aDistancePatchVectorIterator;
			continue;
		}

		long lPatchNum = pTerrainPatchProxy->GetPatchNum();
		if (lPatchNum < 0)
		{
			++aDistancePatchVectorIterator;
			continue;
		}

		BYTE byTerrainNum = pTerrainPatchProxy->GetTerrainNum();
		if (0xFF == byTerrainNum)
		{
			++aDistancePatchVectorIterator;
			continue;
		}

		CTerrain * pTerrain;
		if (!GetTerrainPointer(byTerrainNum, &pTerrain))
		{
			++aDistancePatchVectorIterator;
			continue;
		}

		aPatchDrawStruct.fDistance				= adistancePatchPair.first;
		aPatchDrawStruct.byTerrainNum			= byTerrainNum;
		aPatchDrawStruct.lPatchNum				= lPatchNum;
		aPatchDrawStruct.pTerrainPatchProxy		= pTerrainPatchProxy;

		m_PatchDrawStructVector.push_back(aPatchDrawStruct);

		++aDistancePatchVectorIterator;
	}
}

float CMapOutdoor::__GetNoFogDistance()
{
	return (float)(CTerrainImpl::CELLSCALE * m_lViewRadius) * DX11RuntimeConfig::kTerrainNoFogDistanceRatio;
}

float CMapOutdoor::__GetFogDistance()
{
	return (float)(CTerrainImpl::CELLSCALE * m_lViewRadius) * DX11RuntimeConfig::kTerrainFogDistanceRatio;
}

struct FPatchNumMatch
{
	long m_lPatchNumToCheck;
	FPatchNumMatch(long lPatchNum)
	{
		m_lPatchNumToCheck = lPatchNum;
	}
	bool operator() (std::pair<long, BYTE> aPair)
	{
		return m_lPatchNumToCheck == aPair.first;
	}
};

void CMapOutdoor::NEW_DrawWireFrame(CTerrainPatchProxy * pTerrainPatchProxy, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType)
{
	(void)pTerrainPatchProxy;
	(void)wPrimitiveCount;
	(void)ePrimitiveType;
	static bool s_bLoggedWireframeLegacyDisabled = false;
	if (!s_bLoggedWireframeLegacyDisabled)
	{
		s_bLoggedWireframeLegacyDisabled = true;
		TraceError("DX11_LEGACY_WORLD_SKIP pass=terrain_wireframe reason=legacy_dx9_path_removed");
	}
}

void CMapOutdoor::DrawWireFrame(long patchnum, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType)
{
	(void)patchnum;
	(void)wPrimitiveCount;
	(void)ePrimitiveType;
	NEW_DrawWireFrame(nullptr, 0, GRP_PT_TRIANGLELIST);
}

// Attr
void CMapOutdoor::RenderMarkedArea()
{
	static bool s_bLoggedMarkedAreaLegacyDisabled = false;
	if (!s_bLoggedMarkedAreaLegacyDisabled)
	{
		s_bLoggedMarkedAreaLegacyDisabled = true;
		TraceError("DX11_LEGACY_WORLD_SKIP pass=terrain_marked_area reason=legacy_dx9_path_removed");
	}
}

void CMapOutdoor::RecurseRenderAttr(CTerrainQuadtreeNode *Node, bool bCullEnable)
{
	if (bCullEnable)
	{
		if (__RenderTerrain_RecurseRenderQuadTree_CheckBoundingCircle(Node->center, Node->radius)==VIEW_NONE)
			return;
	}

	{
		if (Node->Size == 1)
		{
			DrawPatchAttr(Node->PatchNum);
		}
		else
		{
			if (Node->NW_Node != NULL)
				RecurseRenderAttr(Node->NW_Node, bCullEnable);
			if (Node->NE_Node != NULL)
				RecurseRenderAttr(Node->NE_Node, bCullEnable);
			if (Node->SW_Node != NULL)
				RecurseRenderAttr(Node->SW_Node, bCullEnable);
			if (Node->SE_Node != NULL)
				RecurseRenderAttr(Node->SE_Node, bCullEnable);
		}
 	}
}

void CMapOutdoor::DrawPatchAttr(long patchnum)
{
	(void)patchnum;
	static bool s_bLoggedPatchAttrLegacyDisabled = false;
	if (!s_bLoggedPatchAttrLegacyDisabled)
	{
		s_bLoggedPatchAttrLegacyDisabled = true;
		TraceError("DX11_LEGACY_WORLD_SKIP pass=patch_attr reason=legacy_dx9_path_removed");
	}
}

// STP was removed from GameLib DX11 migration branch.
// Keep legacy symbols as lightweight compatibility hooks for startup/shutdown callsites.
void CMapOutdoor::__SoftwareTransformPatch_Initialize(void)
{
}

bool CMapOutdoor::__SoftwareTransformPatch_Create(void)
{
	return true;
}

void CMapOutdoor::__SoftwareTransformPatch_Destroy(void)
{
}

void CMapOutdoor::__RenderTerrain_RecurseRenderQuadTree(CTerrainQuadtreeNode *Node, bool bCullCheckNeed)
{
	if (bCullCheckNeed)
	{
		switch (__RenderTerrain_RecurseRenderQuadTree_CheckBoundingCircle(Node->center, Node->radius))
		{
			case VIEW_ALL:
				bCullCheckNeed = false;
				break;
			case VIEW_PART:
				break;
			case VIEW_NONE:
				return;
		}
	}

	if (Node->Size == 1)
	{
		DirectX::SimpleMath::Vector3 v3Center = Node->center;
		const float fDistX = fabs(v3Center.x + m_fXforDistanceCaculation);
		const float fDistY = fabs(-v3Center.y + m_fYforDistanceCaculation);
		const float fDistance = fMAX(fDistX, fDistY);
		__RenderTerrain_AppendPatch(v3Center, fDistance, Node->PatchNum);
	}
	else
	{
		if (Node->NW_Node != NULL)
			__RenderTerrain_RecurseRenderQuadTree(Node->NW_Node, bCullCheckNeed);
		if (Node->NE_Node != NULL)
			__RenderTerrain_RecurseRenderQuadTree(Node->NE_Node, bCullCheckNeed);
		if (Node->SW_Node != NULL)
			__RenderTerrain_RecurseRenderQuadTree(Node->SW_Node, bCullCheckNeed);
		if (Node->SE_Node != NULL)
			__RenderTerrain_RecurseRenderQuadTree(Node->SE_Node, bCullCheckNeed);
	}
}

int CMapOutdoor::__RenderTerrain_RecurseRenderQuadTree_CheckBoundingCircle(const DirectX::SimpleMath::Vector3 & c_v3Center, const float & c_fRadius)
{
	const int count = 6;

	DirectX::SimpleMath::Vector3 center = c_v3Center;
	if (!m_bDX11TerrainUsePositiveYForFrustum)
		center.y = -center.y;

	float distance[count];
	for (int i = 0; i < count; ++i)
	{
		distance[i] = m_plane[i].x * center.x + m_plane[i].y * center.y + m_plane[i].z * center.z + m_plane[i].w;
		if (distance[i] <= -c_fRadius)
			return VIEW_NONE;
	}

	for (int i = 0; i < count; ++i)
	{
		if (distance[i] <= c_fRadius)
			return VIEW_PART;
	}

	return VIEW_ALL;
}

void CMapOutdoor::__RenderTerrain_AppendPatch(const DirectX::SimpleMath::Vector3& c_rv3Center, float fDistance, long lPatchNum)
{
	assert(NULL != m_pTerrainPatchProxyList && "CMapOutdoor::__RenderTerrain_AppendPatch");
	if (!m_pTerrainPatchProxyList[lPatchNum].isUsed())
		return;

	m_pTerrainPatchProxyList[lPatchNum].SetCenterPosition(c_rv3Center);
	m_PatchVector.push_back(std::make_pair(fDistance, lPatchNum));
}

void CMapOutdoor::ApplyLight(DWORD dwVersion, const SLightDesc& c_rkLight)
{
	m_kSTPD.m_dwLightVersion = dwVersion;

	// Strict/native DX11 path: propagate map light into DX11 state manager CB.
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
		if (pStateManager11)
		{
			pStateManager11->SetLight(0, &c_rkLight);
			pStateManager11->SetLightEnable(0, TRUE);

			static DWORD s_dwLastDX11LightParityLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwLastDX11LightParityLogTick || (dwNow - s_dwLastDX11LightParityLogTick) >= 10000u)
			{
				s_dwLastDX11LightParityLogTick = dwNow;
				TraceError("DX11_LIGHT_BIND_PARITY pass=map_apply_light version=%u state_manager11=used", dwVersion);
			}
		}
		else
		{
			static bool s_bLoggedDX11LightStateManagerMissing = false;
			if (!s_bLoggedDX11LightStateManagerMissing)
			{
				s_bLoggedDX11LightStateManagerMissing = true;
				TraceError("DX11_LIGHT_BIND_FAIL pass=map_apply_light reason=state_manager11_unavailable");
			}
		}
		return;
	}
}
