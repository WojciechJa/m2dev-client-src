///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper Class
//
//	(c) 2003 IDV, Inc.
//
//	This class is provided to illustrate one way to incorporate
//	SpeedTreeRT into an OpenGL application.  All of the SpeedTreeRT
//	calls that must be made on a per tree basis are done by this class.
//	Calls that apply to all trees (i.e. static SpeedTreeRT functions)
//	are made in the functions in main.cpp.
//
//
//	*** INTERACTIVE DATA VISUALIZATION (IDV) PROPRIETARY INFORMATION ***
//
//	This software is supplied under the terms of a license agreement or
//	nondisclosure agreement with Interactive Data Visualization and may
//	not be copied or disclosed except in accordance with the terms of
//	that agreement.
//
//      Copyright (c) 2001-2003 IDV, Inc.
//      All Rights Reserved.
//
//		IDV, Inc.
//		1233 Washington St. Suite 610
//		Columbia, SC 29201
//		Voice: (803) 799-1699
//		Fax:   (803) 931-0320
//		Web:   http://www.idvinc.com
//

#pragma warning(disable:4786)

///////////////////////////////////////////////////////////////////////  
//	Include Files
#include "StdAfx.h"

#include <stdlib.h>
#include <stdio.h>
#include "EterBase/Debug.h"
#include "EterBase/Timer.h"
#include "EterBase/Filename.h"
#include "EterLib/ResourceManager.h"
#include "EterLib/Camera.h"
#include "EterLib/StateManager.h"
#include "EterLib/GrpDeviceDX11.h"
#include "UserInterface/config.h"

#include "SpeedTreeConfig.h"
#include "SpeedTreeForestDirectX.h"
#include "SpeedTreeWrapper.h"

#include <filesystem>
#include <cmath>
#include <algorithm>

using namespace std;

namespace
{
inline bool CanUseLegacyRenderDevice()
{
	// DX11-only runtime.
	return false;
}
}

// DX11: Static vertex shader members removed - handled by SpeedTreeForestDirectX
bool CSpeedTreeWrapper::ms_bSelfShadowOn = true;

///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::CSpeedTreeWrapper
CSpeedTreeWrapper::CSpeedTreeWrapper() :
m_pSpeedTree(new CSpeedTreeRT),
m_bIsInstance(false),
m_pInstanceOf(NULL),
m_pGeometryCache(NULL),
m_usNumLeafLods(0),
m_unBranchVertexCount(0),
m_unFrondVertexCount(0),
m_pTextureInfo(NULL)
{
	// set initial position
	m_afPos[0] = m_afPos[1] = m_afPos[2] = 0.0f;

	m_pSpeedTree->SetWindStrength(1.0f);
	m_pSpeedTree->SetLocalMatrices(0, 4);
}

///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::OnRenderPCBlocker
void CSpeedTreeWrapper::OnRenderPCBlocker()
{
	// DX11: tree rendering is scheduled by CSpeedTreeForestDirectX world pass.
}

void CSpeedTreeWrapper::OnRender()
{
	// DX11: tree rendering is scheduled by CSpeedTreeForestDirectX world pass.
}

void CSpeedTreeWrapper::OnBlendRender()
{
	// Keep blend entrypoint aligned with the primary tree submission contract.
	OnRender();
}

void CSpeedTreeWrapper::OnRenderToShadowMap()
{
	OnRenderShadow();
}

void CSpeedTreeWrapper::OnRenderShadow()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pContext)
		return;

	// Tree shadow submission is forest-managed; guard against duplicate dispatch storms
	// when legacy per-instance callbacks are invoked in tight loops.
	static DWORD s_dwLastShadowDispatchMS = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow == s_dwLastShadowDispatchMS)
		return;
	s_dwLastShadowDispatchMS = dwNow;

	const DirectX::SimpleMath::Matrix matLightViewProj = CGraphicBase::GetViewMatrix() * CGraphicBase::GetProjMatrix();
	CSpeedTreeForestDirectX::Instance().RenderToShadowMapDX11(pContext, matLightViewProj);
}

void CSpeedTreeWrapper::OnUpdateHeighInstance(CAttributeInstance* pAttributeInstance)
{
	if (!pAttributeInstance)
		return;

	SetHeightInstance(pAttributeInstance);
}

bool CSpeedTreeWrapper::OnGetObjectHeight(float fX, float fY, float* pfHeight)
{
	if (!m_pHeightAttributeInstance || !pfHeight)
		return false;

	return m_pHeightAttributeInstance->GetHeight(fX, fY, pfHeight) != 0;
}

void CSpeedTreeWrapper::SetVertexShaders(void* pBranchVertexShader, void* pLeafVertexShader, void* pVertexShader)
{
	const bool bLegacyShaderPointersProvided = (pBranchVertexShader != nullptr) || (pLeafVertexShader != nullptr) || (pVertexShader != nullptr);
	if (bLegacyShaderPointersProvided)
	{
		static bool s_bLoggedLegacyPointerIgnore = false;
		if (!s_bLoggedLegacyPointerIgnore)
		{
			s_bLoggedLegacyPointerIgnore = true;
			TraceError("DX11_SPEEDTREE_LEGACY_SHADER_POINTERS_IGNORED mode=dx11_native");
		}
	}

	CSpeedTreeForestDirectX::Instance().EnsureVertexShaders();
}

///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper::~CSpeedTreeWrapper

CSpeedTreeWrapper::~CSpeedTreeWrapper()
{
	// if this is not an instance, clean up
	if (!m_bIsInstance)
	{
		// DX11: Buffer cleanup handled by SpeedTreeForestDirectX

		SAFE_DELETE(m_pTextureInfo);

		SAFE_DELETE(m_pGeometryCache);
	}
	
	// always delete the speedtree
	SAFE_DELETE(m_pSpeedTree);

	Clear();
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper::LoadTree
bool CSpeedTreeWrapper::LoadTree(const char * pszSptFile, const BYTE * c_pbBlock, unsigned int uiBlockSize, UINT nSeed, float fSize, float fSizeVariance)
{
    bool bSuccess = false;
	
	// directx, so allow for flipping of the texture coordinate
#ifdef WRAPPER_FLIP_T_TEXCOORD
	m_pSpeedTree->SetTextureFlip(true);
#endif
	
	// load the tree file
	if (!m_pSpeedTree->LoadTree(c_pbBlock, uiBlockSize))
	{
		if (!m_pSpeedTree->LoadTree(pszSptFile))
		{
			TraceError("SpeedTreeRT Error: %s", CSpeedTreeRT::GetCurrentError());
			return false;
		}
	}
		
	// override the lighting method stored in the spt file
#ifdef WRAPPER_USE_DYNAMIC_LIGHTING
	m_pSpeedTree->SetBranchLightingMethod(CSpeedTreeRT::LIGHT_DYNAMIC);
	m_pSpeedTree->SetLeafLightingMethod(CSpeedTreeRT::LIGHT_DYNAMIC);
	m_pSpeedTree->SetFrondLightingMethod(CSpeedTreeRT::LIGHT_DYNAMIC);
#else
	m_pSpeedTree->SetBranchLightingMethod(CSpeedTreeRT::LIGHT_STATIC);
	m_pSpeedTree->SetLeafLightingMethod(CSpeedTreeRT::LIGHT_STATIC);
	m_pSpeedTree->SetFrondLightingMethod(CSpeedTreeRT::LIGHT_STATIC);
#endif
	
	// set the wind method
#ifdef WRAPPER_USE_GPU_WIND
	m_pSpeedTree->SetBranchWindMethod(CSpeedTreeRT::WIND_GPU);
	m_pSpeedTree->SetLeafWindMethod(CSpeedTreeRT::WIND_GPU);
	m_pSpeedTree->SetFrondWindMethod(CSpeedTreeRT::WIND_GPU);
#endif
#ifdef WRAPPER_USE_CPU_WIND
	m_pSpeedTree->SetBranchWindMethod(CSpeedTreeRT::WIND_CPU);
	m_pSpeedTree->SetLeafWindMethod(CSpeedTreeRT::WIND_CPU);
	m_pSpeedTree->SetFrondWindMethod(CSpeedTreeRT::WIND_CPU);
#endif
#ifdef WRAPPER_USE_NO_WIND
	m_pSpeedTree->SetBranchWindMethod(CSpeedTreeRT::WIND_NONE);
	m_pSpeedTree->SetLeafWindMethod(CSpeedTreeRT::WIND_NONE);
	m_pSpeedTree->SetFrondWindMethod(CSpeedTreeRT::WIND_NONE);
#endif
	
	m_pSpeedTree->SetNumLeafRockingGroups(1);
	
	// override the size, if necessary
	if (fSize >= 0.0f && fSizeVariance >= 0.0f)
		m_pSpeedTree->SetTreeSize(fSize, fSizeVariance);
	
	// generate tree geometry
	if (m_pSpeedTree->Compute(NULL, nSeed, false))
	{
		// get the dimensions
		m_pSpeedTree->GetBoundingBox(m_afBoundingBox);
		
		// make the leaves rock in the wind
		m_pSpeedTree->SetLeafRockingState(true);
		
		// billboard setup
#ifdef WRAPPER_NO_BILLBOARD_MODE
		CSpeedTreeRT::SetDropToBillboard(false);
#else
		CSpeedTreeRT::SetDropToBillboard(DX11RuntimeConfig::kSpeedTreeDropToBillboard);
#endif
		
		// query & set materials
		m_cBranchMaterial.Set(m_pSpeedTree->GetBranchMaterial());
		m_cFrondMaterial.Set(m_pSpeedTree->GetFrondMaterial());
		m_cLeafMaterial.Set(m_pSpeedTree->GetLeafMaterial());
		
		// adjust lod distances
		float fHeight = m_afBoundingBox[5] - m_afBoundingBox[2];
		float fNearLod = fHeight * DX11RuntimeConfig::kSpeedTreeNearLodFactor;
		float fFarLod = fHeight * DX11RuntimeConfig::kSpeedTreeFarLodFactor;

		// DX11: Camera-scaled LOD limits for improved visual parity
		const float fCameraMaxDistance = CCamera::GetCameraMaxDistance();
		if (DX11RuntimeConfig::kSpeedTreeUseCameraScaledLodLimits &&
			fCameraMaxDistance > DX11RuntimeConfig::kSpeedTreeCameraDistanceThreshold)
		{
			const float fZoomFar = fCameraMaxDistance;
			const float fTargetFar = std::min(
				DX11RuntimeConfig::kSpeedTreeFarLodMax,
				std::max(fFarLod, fZoomFar * DX11RuntimeConfig::kSpeedTreeFarLodFromCameraScale));
			const float fTargetNear = std::max(
				fNearLod,
				fTargetFar * DX11RuntimeConfig::kSpeedTreeNearFromFarRatio);
			fFarLod = fTargetFar;
			fNearLod = std::min(fTargetNear, fFarLod - 1.0f);

			static DWORD s_dwSpeedTreeLodConfigLogMS = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwSpeedTreeLodConfigLogMS || (dwNow - s_dwSpeedTreeLodConfigLogMS) >= 5000u)
			{
				s_dwSpeedTreeLodConfigLogMS = dwNow;
				TraceError("DX11_SPEEDTREE_LOD_CONFIG height=%.1f camera_max=%.1f near=%.1f far=%.1f",
					fHeight, fZoomFar, fNearLod, fFarLod);
			}
		}

		m_pSpeedTree->SetLodLimits(fNearLod, fFarLod);
		
		// query textures
		m_pTextureInfo = new CSpeedTreeRT::STextures;
		m_pSpeedTree->GetTextures(*m_pTextureInfo);
		
		std::filesystem::path path = pszSptFile;
		path = path.parent_path();

		auto branchTexture = path / m_pTextureInfo->m_pBranchTextureFilename;
		branchTexture.replace_extension(".dds");

		// load branch textures
		LoadTexture(branchTexture.generic_string().c_str(), m_BranchImageInstance);
		
#ifdef WRAPPER_RENDER_SELF_SHADOWS
		auto selfShadowTexture = path / m_pTextureInfo->m_pSelfShadowFilename;
		selfShadowTexture.replace_extension(".dds");

		if (m_pTextureInfo->m_pSelfShadowFilename != NULL)
			LoadTexture(selfShadowTexture.generic_string().c_str(), m_ShadowImageInstance);
#endif

		auto compositeTexture = path / m_pTextureInfo->m_pCompositeFilename;
		compositeTexture.replace_extension(".dds");

		if (m_pTextureInfo->m_pCompositeFilename)
			LoadTexture(compositeTexture.generic_string().c_str(), m_CompositeImageInstance);
		
		// setup the index and vertex buffers
		SetupBuffers();
		
		// everything appeared to go well
		bSuccess = true;
	}
	else // tree failed to compute
		fprintf(stderr, "\nFatal Error, cannot compute tree [%s]\n\n", CSpeedTreeRT::GetCurrentError());
	
    return bSuccess;
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper::SetupBuffers

void CSpeedTreeWrapper::SetupBuffers(void)
{
	// read all the geometry for highest LOD into the geometry cache (just a precaution, it's updated later)
	if (DX11RuntimeConfig::kSpeedTreeForceFixedLodLevel)
		m_pSpeedTree->SetLodLevel(DX11RuntimeConfig::kSpeedTreeForcedLodLevel);
	
	if (m_pGeometryCache == NULL)
		m_pGeometryCache = new CSpeedTreeRT::SGeometry;
	
	m_pSpeedTree->GetGeometry(*m_pGeometryCache);
	
	// setup the buffers for each part
	SetupBranchBuffers();
	SetupFrondBuffers();
	SetupLeafBuffers();
}

///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::SetupBranchBuffers

void CSpeedTreeWrapper::SetupBranchBuffers(void)
{
	// DX11: branch buffers are generated on demand in CSpeedTreeForestDirectX.
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper::SetupFrondBuffers

void CSpeedTreeWrapper::SetupFrondBuffers(void)
{
	// DX11: frond buffers are generated on demand in CSpeedTreeForestDirectX.
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper::SetupLeafBuffers

void CSpeedTreeWrapper::SetupLeafBuffers(void)
{
	// DX11: leaf buffers are generated on demand in CSpeedTreeForestDirectX.
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper::Advance

void CSpeedTreeWrapper::Advance(void)
{
	// compute LOD level (based on distance from camera)
	m_pSpeedTree->ComputeLodLevel();
	if (DX11RuntimeConfig::kSpeedTreeForceFixedLodLevel)
		m_pSpeedTree->SetLodLevel(DX11RuntimeConfig::kSpeedTreeForcedLodLevel);
	
	// compute wind
#ifdef WRAPPER_USE_CPU_WIND
	m_pSpeedTree->ComputeWindEffects(true, true, true);
#endif
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper::MakeInstance
CSpeedTreeWrapper::SpeedTreeWrapperPtr CSpeedTreeWrapper::MakeInstance()
{
	auto spInstance = std::make_shared<CSpeedTreeWrapper>();
	
	// make an instance of this object's SpeedTree
	spInstance->m_bIsInstance = true;

	SAFE_DELETE(spInstance->m_pSpeedTree);
	spInstance->m_pSpeedTree = m_pSpeedTree->MakeInstance();
	
	if (spInstance->m_pSpeedTree)
    {
		// use the same materials
		spInstance->m_cBranchMaterial = m_cBranchMaterial;
		spInstance->m_cLeafMaterial = m_cLeafMaterial;
		spInstance->m_cFrondMaterial = m_cFrondMaterial;
		spInstance->m_CompositeImageInstance.SetImagePointer(m_CompositeImageInstance.GetGraphicImagePointer());
		spInstance->m_BranchImageInstance.SetImagePointer(m_BranchImageInstance.GetGraphicImagePointer());
		
		if (!m_ShadowImageInstance.IsEmpty())
			spInstance->m_ShadowImageInstance.SetImagePointer(m_ShadowImageInstance.GetGraphicImagePointer());
		
		spInstance->m_pTextureInfo = m_pTextureInfo;
		
		// use the same geometry cache
		spInstance->m_pGeometryCache = m_pGeometryCache;

		// DX11: Buffers handled by SpeedTreeForestDirectX
		spInstance->m_unBranchVertexCount = m_unBranchVertexCount;
		spInstance->m_unFrondVertexCount = m_unFrondVertexCount;
		spInstance->m_usNumLeafLods = m_usNumLeafLods;

		// new stuff
		memcpy(spInstance->m_afPos, m_afPos, 3 * sizeof(float));
		memcpy(spInstance->m_afBoundingBox, m_afBoundingBox, 6 * sizeof(float));
		spInstance->m_pInstanceOf = shared_from_this();
		m_vInstances.push_back(spInstance);
    }
    else
	{
		fprintf(stderr, "SpeedTreeRT Error: %s\n", m_pSpeedTree->GetCurrentError());
		spInstance.reset();
	}
	
	return spInstance;
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper::GetInstances
std::vector <CSpeedTreeWrapper::SpeedTreeWrapperPtr> CSpeedTreeWrapper::GetInstances(UINT& nCount)
{
	std::vector <SpeedTreeWrapperPtr> kResult;

	nCount = m_vInstances.size();
	if (nCount)
	{
		for (auto it : m_vInstances)
		{
			kResult.push_back(it);
		}
	}

	return kResult;
}

void CSpeedTreeWrapper::DeleteInstance(SpeedTreeWrapperPtr pInstance)
{
	auto itor = m_vInstances.begin();
	
	while (itor != m_vInstances.end())
	{
		if (*itor == pInstance)
		{
			itor = m_vInstances.erase(itor);
		}
		else
			++itor;
	}
}

///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::SetupBranchForTreeType

void CSpeedTreeWrapper::SetupBranchForTreeType(void) const
{
	CSpeedTreeWrapper* pMutableThis = const_cast<CSpeedTreeWrapper*>(this);
	if (!pMutableThis->m_pSpeedTree)
		return;

	if (!pMutableThis->m_pGeometryCache)
		pMutableThis->m_pGeometryCache = new CSpeedTreeRT::SGeometry;

	pMutableThis->m_pSpeedTree->GetGeometry(*pMutableThis->m_pGeometryCache, SpeedTree_BranchGeometry);
}


///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::RenderBranches

void CSpeedTreeWrapper::RenderBranches(void) const
{
	SetupBranchForTreeType();
	PositionTree();
}


///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::SetupFrondForTreeType

void CSpeedTreeWrapper::SetupFrondForTreeType(void) const
{
	CSpeedTreeWrapper* pMutableThis = const_cast<CSpeedTreeWrapper*>(this);
	if (!pMutableThis->m_pSpeedTree)
		return;

	if (!pMutableThis->m_pGeometryCache)
		pMutableThis->m_pGeometryCache = new CSpeedTreeRT::SGeometry;

	pMutableThis->m_pSpeedTree->GetGeometry(*pMutableThis->m_pGeometryCache, SpeedTree_FrondGeometry);
}


///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::RenderFronds

void CSpeedTreeWrapper::RenderFronds(void) const
{
	SetupFrondForTreeType();
	PositionTree();
}


///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::SetupLeafForTreeType

void CSpeedTreeWrapper::SetupLeafForTreeType(void) const
{
	CSpeedTreeWrapper* pMutableThis = const_cast<CSpeedTreeWrapper*>(this);
	if (!pMutableThis->m_pSpeedTree)
		return;

	if (!pMutableThis->m_pGeometryCache)
		pMutableThis->m_pGeometryCache = new CSpeedTreeRT::SGeometry;

	pMutableThis->m_pSpeedTree->GetGeometry(*pMutableThis->m_pGeometryCache, SpeedTree_LeafGeometry);
}


///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::UploadLeafTables

#ifdef WRAPPER_USE_GPU_LEAF_PLACEMENT
void CSpeedTreeWrapper::UploadLeafTables(UINT uiLocation) const
{
	if (uiLocation > 4096u)
	{
		static bool s_bLoggedUnexpectedLeafTableLocation = false;
		if (!s_bLoggedUnexpectedLeafTableLocation)
		{
			s_bLoggedUnexpectedLeafTableLocation = true;
			TraceError("DX11_SPEEDTREE_LEAF_TABLE_LOCATION_UNEXPECTED location=%u", uiLocation);
		}
	}

	SetupLeafForTreeType();
}
#endif


///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::RenderLeaves

void CSpeedTreeWrapper::RenderLeaves(void) const
{
	SetupLeafForTreeType();
	PositionTree();
}


///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::EndLeafForTreeType

void CSpeedTreeWrapper::EndLeafForTreeType(void)
{
	Advance();
}


///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::RenderBillboards

void CSpeedTreeWrapper::RenderBillboards(void) const
{
	CSpeedTreeWrapper* pMutableThis = const_cast<CSpeedTreeWrapper*>(this);
	if (!pMutableThis->m_pSpeedTree)
		return;

	if (!pMutableThis->m_pGeometryCache)
		pMutableThis->m_pGeometryCache = new CSpeedTreeRT::SGeometry;

	pMutableThis->m_pSpeedTree->GetGeometry(*pMutableThis->m_pGeometryCache, SpeedTree_BillboardGeometry);
	PositionTree();
}

///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::CleanUpMemory

void CSpeedTreeWrapper::CleanUpMemory(void)
{
	if (!m_bIsInstance)
		m_pSpeedTree->DeleteTransientData();
}

///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::PositionTree

void CSpeedTreeWrapper::PositionTree(void) const
{
	if (!m_pSpeedTree)
		return;

	m_pSpeedTree->SetTreePosition(m_afPos[0], m_afPos[1], m_afPos[2]);
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeWrapper::LoadTexture

bool CSpeedTreeWrapper::LoadTexture(const char * pFilename, CGraphicImageInstance & rImage)
{
	CResource * pResource = CResourceManager::Instance().GetResourcePointer(pFilename);
	rImage.SetImagePointer(static_cast<CGraphicImage *>(pResource));

	if (rImage.IsEmpty())
		return false;
	
	//TraceError("SpeedTreeWrapper::LoadTexture: %s", pFilename);
	return true;
}


///////////////////////////////////////////////////////////////////////
//	CSpeedTreeWrapper::SetShaderConstants

void CSpeedTreeWrapper::SetShaderConstants(const float* pMaterial) const
{
	if (pMaterial)
	{
		const bool bInvalidMaterialProbe = !std::isfinite(pMaterial[0]) || !std::isfinite(pMaterial[1]) || !std::isfinite(pMaterial[2]);
		if (bInvalidMaterialProbe)
		{
			static bool s_bLoggedInvalidMaterialProbe = false;
			if (!s_bLoggedInvalidMaterialProbe)
			{
				s_bLoggedInvalidMaterialProbe = true;
				TraceError("DX11_SPEEDTREE_MATERIAL_PROBE_INVALID");
			}
		}
	}

	PositionTree();
}

void CSpeedTreeWrapper::SetPosition(float x, float y, float z)
{
	m_afPos[0] = x;
	m_afPos[1] = y;
	m_afPos[2] = z;
	m_pSpeedTree->SetTreePosition(x, y, z);
	CGraphicObjectInstance::SetPosition(x, y, z);
}

bool CSpeedTreeWrapper::GetBoundingSphere(DirectX::SimpleMath::Vector3 & v3Center, float & fRadius)
{
	auto IsFinite = [](float v) -> bool
	{
		return std::isfinite(v) != 0;
	};

	DirectX::SimpleMath::Vector3 vecTreePos(0.0f, 0.0f, 0.0f);
	if (m_pSpeedTree)
	{
		const float* pTreePos = m_pSpeedTree->GetTreePosition();
		if (pTreePos)
			vecTreePos = DirectX::SimpleMath::Vector3(pTreePos[0], pTreePos[1], pTreePos[2]);
	}

	// Validate tree bounds before computing sphere (prevents invalid culling spheres
	// that can break SpherePack parent-child assumptions during map transitions).
	const float fMinX = m_afBoundingBox[0];
	const float fMinY = m_afBoundingBox[1];
	const float fMinZ = m_afBoundingBox[2];
	const float fMaxX = m_afBoundingBox[3];
	const float fMaxY = m_afBoundingBox[4];
	const float fMaxZ = m_afBoundingBox[5];

	const bool bBBoxFinite = IsFinite(fMinX) && IsFinite(fMinY) && IsFinite(fMinZ) &&
		IsFinite(fMaxX) && IsFinite(fMaxY) && IsFinite(fMaxZ);
	const bool bBBoxOrdered = (fMaxX >= fMinX) && (fMaxY >= fMinY) && (fMaxZ >= fMinZ);

	float fX = 0.0f;
	float fY = 0.0f;
	float fZ = 0.0f;

	if (bBBoxFinite && bBBoxOrdered)
	{
		fX = fMaxX - fMinX;
		fY = fMaxY - fMinY;
		fZ = fMaxZ - fMinZ;
	}

	if (!bBBoxFinite || !bBBoxOrdered || !IsFinite(fX) || !IsFinite(fY) || !IsFinite(fZ))
	{
		static bool s_bLoggedInvalidBBox = false;
		if (!s_bLoggedInvalidBBox)
		{
			s_bLoggedInvalidBBox = true;
			TraceError("DX11_SPEEDTREE_BSPHERE_FIXUP reason=invalid_bbox tree_pos=(%.2f,%.2f,%.2f)",
				vecTreePos.x, vecTreePos.y, vecTreePos.z);
		}

		// Safe fallback sphere around tree origin.
		v3Center = vecTreePos;
		v3Center.z += 32.0f;
		fRadius = 64.0f;
		return true;
	}

	v3Center.x = 0.0f;
	v3Center.y = 0.0f;
	v3Center.z = fZ * 0.5f;

	fRadius = sqrtf(fX * fX + fY * fY + fZ * fZ) * 0.5f * 0.9f; // 0.9f for reduce size
	if (!IsFinite(fRadius) || fRadius <= 0.0f)
	{
		static bool s_bLoggedInvalidRadius = false;
		if (!s_bLoggedInvalidRadius)
		{
			s_bLoggedInvalidRadius = true;
			TraceError("DX11_SPEEDTREE_BSPHERE_FIXUP reason=invalid_radius tree_pos=(%.2f,%.2f,%.2f)",
				vecTreePos.x, vecTreePos.y, vecTreePos.z);
		}
		fRadius = 64.0f;
	}

	// Keep culling radius in the same safe range as CCullingManager clamp.
	const float kMaxReasonableRadius = 3200.0f;
	if (fRadius > kMaxReasonableRadius)
	{
		static bool s_bLoggedRadiusClamp = false;
		if (!s_bLoggedRadiusClamp)
		{
			s_bLoggedRadiusClamp = true;
			TraceError("DX11_SPEEDTREE_BSPHERE_FIXUP reason=radius_clamp old=%.2f new=%.2f",
				fRadius, kMaxReasonableRadius);
		}
		fRadius = kMaxReasonableRadius;
	}

	v3Center += vecTreePos;
	return true;
}

void CSpeedTreeWrapper::CalculateBBox()
{
	float fX, fY, fZ;
	
	fX = m_afBoundingBox[3] - m_afBoundingBox[0];
	fY = m_afBoundingBox[4] - m_afBoundingBox[1];
	fZ = m_afBoundingBox[5] - m_afBoundingBox[2];
	
	m_v3BBoxMin.x = -fX / 2.0f;
	m_v3BBoxMin.y = -fY / 2.0f;
	m_v3BBoxMin.z = 0.0f;
	m_v3BBoxMax.x = fX / 2.0f;
	m_v3BBoxMax.y = fY / 2.0f;
	m_v3BBoxMax.z = fZ;
	
	m_v4TBBox[0] = D3DXVECTOR4(m_v3BBoxMin.x, m_v3BBoxMin.y, m_v3BBoxMin.z, 1.0f);
	m_v4TBBox[1] = D3DXVECTOR4(m_v3BBoxMin.x, m_v3BBoxMax.y, m_v3BBoxMin.z, 1.0f);
	m_v4TBBox[2] = D3DXVECTOR4(m_v3BBoxMax.x, m_v3BBoxMin.y, m_v3BBoxMin.z, 1.0f);
	m_v4TBBox[3] = D3DXVECTOR4(m_v3BBoxMax.x, m_v3BBoxMax.y, m_v3BBoxMin.z, 1.0f);
	m_v4TBBox[4] = D3DXVECTOR4(m_v3BBoxMin.x, m_v3BBoxMin.y, m_v3BBoxMax.z, 1.0f);
	m_v4TBBox[5] = D3DXVECTOR4(m_v3BBoxMin.x, m_v3BBoxMax.y, m_v3BBoxMax.z, 1.0f);
	m_v4TBBox[6] = D3DXVECTOR4(m_v3BBoxMax.x, m_v3BBoxMin.y, m_v3BBoxMax.z, 1.0f);
	m_v4TBBox[7] = D3DXVECTOR4(m_v3BBoxMax.x, m_v3BBoxMax.y, m_v3BBoxMax.z, 1.0f);
	
	const D3DXMATRIX & c_rmatTransform = GetTransform();
	
	for (DWORD i = 0; i < 8; ++i)
	{
		D3DXVec4Transform(&m_v4TBBox[i], &m_v4TBBox[i], &c_rmatTransform);
		if (0 == i)
		{
			m_v3TBBoxMin.x = m_v4TBBox[i].x;
			m_v3TBBoxMin.y = m_v4TBBox[i].y;
			m_v3TBBoxMin.z = m_v4TBBox[i].z;
			m_v3TBBoxMax.x = m_v4TBBox[i].x;
			m_v3TBBoxMax.y = m_v4TBBox[i].y;
			m_v3TBBoxMax.z = m_v4TBBox[i].z;
		}
		else
		{
			if (m_v3TBBoxMin.x > m_v4TBBox[i].x)
				m_v3TBBoxMin.x = m_v4TBBox[i].x;
			if (m_v3TBBoxMax.x < m_v4TBBox[i].x)
				m_v3TBBoxMax.x = m_v4TBBox[i].x;
			if (m_v3TBBoxMin.y > m_v4TBBox[i].y)
				m_v3TBBoxMin.y = m_v4TBBox[i].y;
			if (m_v3TBBoxMax.y < m_v4TBBox[i].y)
				m_v3TBBoxMax.y = m_v4TBBox[i].y;
			if (m_v3TBBoxMin.z > m_v4TBBox[i].z)
				m_v3TBBoxMin.z = m_v4TBBox[i].z;
			if (m_v3TBBoxMax.z < m_v4TBBox[i].z)
				m_v3TBBoxMax.z = m_v4TBBox[i].z;
		}
	}	
}

// collision detection routines
UINT CSpeedTreeWrapper::GetCollisionObjectCount()
{
	assert(m_pSpeedTree);
	return m_pSpeedTree->GetCollisionObjectCount();
}

void CSpeedTreeWrapper::GetCollisionObject(UINT nIndex, CSpeedTreeRT::ECollisionObjectType& eType, float* pPosition, float* pDimensions)
{
	assert(m_pSpeedTree);
	m_pSpeedTree->GetCollisionObject(nIndex, eType, pPosition, pDimensions);
}


const float * CSpeedTreeWrapper::GetPosition()
{
	return m_afPos;
}

void CSpeedTreeWrapper::GetTreeSize(float & r_fSize, float & r_fVariance)
{
	m_pSpeedTree->GetTreeSize(r_fSize, r_fVariance);
}

// pscdVector may be null
void CSpeedTreeWrapper::OnUpdateCollisionData(const CStaticCollisionDataVector * /*pscdVector*/)
{
	D3DXMATRIX mat;
	D3DXMatrixTranslation(&mat, m_afPos[0], m_afPos[1], m_afPos[2]);
	
	/////
	for (UINT i = 0; i < GetCollisionObjectCount(); ++i)
	{
		CSpeedTreeRT::ECollisionObjectType ObjectType;
		CStaticCollisionData CollisionData;
		
		GetCollisionObject(i, ObjectType, (float * )&CollisionData.v3Position, CollisionData.fDimensions);
		
		if (ObjectType == CSpeedTreeRT::CO_BOX)
			continue;
		
		switch(ObjectType)
		{
		case CSpeedTreeRT::CO_SPHERE:
			CollisionData.dwType = COLLISION_TYPE_SPHERE;
			CollisionData.fDimensions[0] = CollisionData.fDimensions[0] /** fSizeRatio*/;
			//AddCollision(&CollisionData);
			break;
			
		case CSpeedTreeRT::CO_CYLINDER:
			CollisionData.dwType = COLLISION_TYPE_CYLINDER;
			CollisionData.fDimensions[0] = CollisionData.fDimensions[0] /** fSizeRatio*/;
			CollisionData.fDimensions[1] = CollisionData.fDimensions[1] /** fSizeRatio*/;
			//AddCollision(&CollisionData);
			break;
			
			/*case CSpeedTreeRT::CO_BOX:
			break;*/
		}
		AddCollision(&CollisionData, &mat);
	}
}
