#include "StdAfx.h"
#include "ModelInstance.h"
#include "Model.h"
#include "../EterLib/GrpDeviceDX11.h"  // S1.2: For DX11 device access
#include <algorithm>

void CGrannyModelInstance::Clear()
{
	m_kMtrlPal.Clear();
	
	DestroyDeviceObjects();
	// WORK
	__DestroyMeshBindingVector();
	// END_OF_WORK
	__DestroyMeshMatrices();
	__DestroyModelInstance();
	__DestroyWorldPose();

	__Initialize();
}

void CGrannyModelInstance::SetMainModelPointer(CGrannyModel* pModel, CGraphicVertexBuffer* pkSharedDeformableVertexBuffer)
{
	SetLinkedModelPointer(pModel, pkSharedDeformableVertexBuffer, NULL);
}

void CGrannyModelInstance::SetLinkedModelPointer(CGrannyModel* pkModel, CGraphicVertexBuffer* pkSharedDeformableVertexBuffer, CGrannyModelInstance** ppkSkeletonInst)
{
	Clear();

	m_pModel = pkModel;

	m_pModel->AddReference();
	
	if (pkSharedDeformableVertexBuffer)
		__SetSharedDeformableVertexBuffer(pkSharedDeformableVertexBuffer);
	else
		__CreateDynamicVertexBuffer();

	__CreateModelInstance();
	
	// WORK
	if (ppkSkeletonInst && *ppkSkeletonInst)
	{
		m_ppkSkeletonInst = ppkSkeletonInst;
		__CreateWorldPose(*ppkSkeletonInst);			
		__CreateMeshBindingVector(*ppkSkeletonInst);
	}
	else
	{
		__CreateWorldPose(NULL);			
		__CreateMeshBindingVector(NULL);
	}
	// END_OF_WORK	

	__CreateMeshMatrices();

	ResetLocalTime();
	
	m_kMtrlPal.Copy(pkModel->GetMaterialPalette());
}

// WORK
granny_world_pose* CGrannyModelInstance::__GetWorldPosePtr() const
{
	if (m_pgrnWorldPoseReal)
		return m_pgrnWorldPoseReal;
	
	if (m_ppkSkeletonInst && *m_ppkSkeletonInst && (*m_ppkSkeletonInst)->m_pgrnWorldPoseReal)
		return (*m_ppkSkeletonInst)->m_pgrnWorldPoseReal;

	static bool s_bLoggedMissingWorldPose = false;
	if (!s_bLoggedMissingWorldPose)
	{
		s_bLoggedMissingWorldPose = true;
		TraceError("DX11_GRANNY_GUARD world_pose_unavailable fallback=skip_pose_update");
	}
	return NULL;	
}

int* CGrannyModelInstance::__GetMeshBoneIndices(unsigned int iMeshBinding) const
{
	assert(iMeshBinding<m_vct_pgrnMeshBinding.size());
	return (int*)GrannyGetMeshBindingToBoneIndices(m_vct_pgrnMeshBinding[iMeshBinding]);
}

bool CGrannyModelInstance::__CreateMeshBindingVector(CGrannyModelInstance* pkDstModelInst)
{
	assert(m_vct_pgrnMeshBinding.empty());

	if (!m_pModel)
		return false;	
	
	granny_model* pgrnModel = m_pModel->GetGrannyModelPointer();
	if (!pgrnModel)
		return false;

	granny_skeleton* pgrnDstSkeleton = pgrnModel->Skeleton;
	if (pkDstModelInst && pkDstModelInst->m_pModel && pkDstModelInst->m_pModel->GetGrannyModelPointer())
		pgrnDstSkeleton = pkDstModelInst->m_pModel->GetGrannyModelPointer()->Skeleton;
	
	m_vct_pgrnMeshBinding.reserve(pgrnModel->MeshBindingCount);

	granny_int32 iMeshBinding;
	for (iMeshBinding = 0; iMeshBinding != pgrnModel->MeshBindingCount; ++iMeshBinding)
		m_vct_pgrnMeshBinding.push_back(GrannyNewMeshBinding(pgrnModel->MeshBindings[iMeshBinding].Mesh, pgrnModel->Skeleton, pgrnDstSkeleton));

	return true;
}

void CGrannyModelInstance::__DestroyMeshBindingVector()
{
	std::for_each(m_vct_pgrnMeshBinding.begin(), m_vct_pgrnMeshBinding.end(), GrannyFreeMeshBinding);
	m_vct_pgrnMeshBinding.clear();		
}

// END_OF_WORK


void CGrannyModelInstance::__CreateWorldPose(CGrannyModelInstance* pkSkeletonInst)
{
	assert(m_pgrnModelInstance != NULL);
	assert(m_pgrnWorldPoseReal == NULL);

	// WORK
	if (pkSkeletonInst)
		return;	
	// END_OF_WORK

	granny_skeleton * pgrnSkeleton = GrannyGetSourceSkeleton(m_pgrnModelInstance);		

	// WORK
	m_pgrnWorldPoseReal = GrannyNewWorldPose(pgrnSkeleton->BoneCount);	
	// END_OF_WORK
}

void CGrannyModelInstance::__DestroyWorldPose()
{
	if (!m_pgrnWorldPoseReal)
		return;

	GrannyFreeWorldPose(m_pgrnWorldPoseReal);
	m_pgrnWorldPoseReal = NULL;	
}

void CGrannyModelInstance::__CreateModelInstance()
{	
	assert(m_pModel != NULL);
	assert(m_pgrnModelInstance == NULL);

	const granny_model * pgrnModel = m_pModel->GetGrannyModelPointer();	
	m_pgrnModelInstance = GrannyInstantiateModel(pgrnModel);
}

void CGrannyModelInstance::__DestroyModelInstance()
{
	if (!m_pgrnModelInstance) 
		return;

	GrannyFreeModelInstance(m_pgrnModelInstance);
	m_pgrnModelInstance = NULL;
}

void CGrannyModelInstance::__CreateMeshMatrices()
{
	assert(m_pModel != NULL);
	
	if (m_pModel->GetMeshCount() <= 0) // 메쉬가 없는 (카메라 같은) 모델도 간혹 있다..
		return;
	
	int meshCount = m_pModel->GetMeshCount();	
	m_meshMatrices = new D3DXMATRIX[meshCount];
}

void CGrannyModelInstance::__DestroyMeshMatrices()
{
	if (!m_meshMatrices)
		return;

	delete [] m_meshMatrices;
	m_meshMatrices = NULL;
}

DWORD CGrannyModelInstance::GetDeformableVertexCount()
{
	if (!m_pModel)
		return 0;

	return m_pModel->GetDeformVertexCount();
}

DWORD CGrannyModelInstance::GetVertexCount()
{
	if (!m_pModel)
		return 0;

	return m_pModel->GetVertexCount();
}

// WORK

void CGrannyModelInstance::__SetSharedDeformableVertexBuffer(CGraphicVertexBuffer* pkSharedDeformableVertexBuffer)
{
	m_pkSharedDeformableVertexBuffer = pkSharedDeformableVertexBuffer;
}

bool CGrannyModelInstance::__IsDeformableVertexBuffer()
{
	if (m_pkSharedDeformableVertexBuffer)
		return true;

	return m_kLocalDeformableVertexBuffer.IsEmpty();
}

ID3D11Buffer* CGrannyModelInstance::__GetDeformableD3DVertexBufferPtr()
{
	return __GetDeformableVertexBufferRef().GetD3DVertexBuffer();
}

CGraphicVertexBuffer& CGrannyModelInstance::__GetDeformableVertexBufferRef()
{
	if (m_pkSharedDeformableVertexBuffer)
		return *m_pkSharedDeformableVertexBuffer;

	return m_kLocalDeformableVertexBuffer;
}

void CGrannyModelInstance::__CreateDynamicVertexBuffer()
{
	assert(m_pModel != NULL);
	assert(m_kLocalDeformableVertexBuffer.IsEmpty());

	int vtxCount = m_pModel->GetDeformVertexCount();

	if (0 != vtxCount)
	{
		if (!m_kLocalDeformableVertexBuffer.Create(vtxCount,
									   FVF_XYZ|FVF_NORMAL|FVF_TEX1,
									   D3DUSAGE_DYNAMIC, D3DPOOL_DEFAULT
		))
			return;
	}	
}

void CGrannyModelInstance::__DestroyDynamicVertexBuffer()
{
	m_kLocalDeformableVertexBuffer.Destroy();
	m_pkSharedDeformableVertexBuffer = NULL;
}

// END_OF_WORK

bool CGrannyModelInstance::GetBoneIndexByName(const char * c_szBoneName, int * pBoneIndex) const
{
	assert(m_pgrnModelInstance != NULL);

	granny_skeleton * pgrnSkeleton = GrannyGetSourceSkeleton(m_pgrnModelInstance);

	if (!GrannyFindBoneByName(pgrnSkeleton, c_szBoneName, pBoneIndex))
		return false;

	return true;
}

const float * CGrannyModelInstance::GetBoneMatrixPointer(int iBone) const
{
	const float* bones = GrannyGetWorldPose4x4(__GetWorldPosePtr(), iBone);
	if (!bones)
	{
		granny_model* pModel = m_pModel->GetGrannyModelPointer();		
		//TraceError("GrannyModelInstance(%s).GetBoneMatrixPointer(boneIndex(%d)).NOT_FOUND_BONE", pModel->Name, iBone);
		return NULL;
	}
	return bones;
}

const float * CGrannyModelInstance::GetCompositeBoneMatrixPointer(int iBone) const
{
	// NOTE : GrannyGetWorldPose4x4는 스케일 값등이 잘못나올 수 있음.. 그래니가 속도를 위해
	//        GrannyGetWorldPose4x4에 모든 matrix 원소를 제 값으로 넣지 않음
	return GrannyGetWorldPoseComposite4x4(__GetWorldPosePtr(), iBone);
}

void CGrannyModelInstance::ReloadTexture()
{
	assert("현재 사용하지 않음 - CGrannyModelInstance::ReloadTexture()");
/*
	assert(m_pModel != NULL);
	const CGrannyMaterialPalette & c_rGrannyMaterialPalette = m_pModel->GetMaterialPalette();
	DWORD dwMaterialCount = c_rGrannyMaterialPalette.GetMaterialCount();
	for (DWORD dwMtrIndex = 0; dwMtrIndex < dwMaterialCount; ++dwMtrIndex)
	{
		const CGrannyMaterial & c_rGrannyMaterial = c_rGrannyMaterialPalette.GetMaterialRef(dwMtrIndex);
		CGraphicImage * pImageStage0 = c_rGrannyMaterial.GetImagePointer(0);
		if (pImageStage0)
			pImageStage0->Reload();
		CGraphicImage * pImageStage1 = c_rGrannyMaterial.GetImagePointer(1);
		if (pImageStage1)
			pImageStage1->Reload();
	}
*/
}

// ================================================================================================
// S1.2: DX11 Vertex Buffer Management for Character Shadow Rendering
// ================================================================================================

void CGrannyModelInstance::CreateDX11VertexBuffers(ID3D11Device* pDevice)
{
	if (!pDevice || !m_pModel || m_bDX11VertexBuffersReady)
		return;

	// DX11 Model Sync M3-EGRN-33: VB/IB upload from CPU shadow buffers (strict mode) or DX9 fallback (hybrid mode).
	// Current implementation: Priority 1: CPU shadow buffers (both modes), Priority 2: DX9 Lock (hybrid only).
	// Future upgrade path:
	// 1) build DX11 buffers directly from Granny mesh/index source data, or
	// 2) replace Granny runtime with a modern skeletal pipeline (e.g. glTF + custom skinning cache).
	// Keeping this note for planned engine/runtime modernization phase.

	DestroyDX11VertexBuffers();

	// DX11 Model Sync M3-EGRN-33: VB/IB upload source selection.
	// Priority 1: CPU shadow buffers (strict & hybrid mode), Priority 2: DX9 VB/IB Lock (hybrid mode only).
	// In strict mode, only CPU shadow is available.
	void* pLockedIndices = nullptr;
	void* pLockedRigidVertices = nullptr;
	bool bModelDataLocked = false;

	// 1. Create DX11 deformable vertex buffer (dynamic, updated per-frame)
	const int iDeformVertexCount = m_pModel->GetDeformVertexCount();
	if (iDeformVertexCount > 0)
	{
		D3D11_BUFFER_DESC vbDesc = {};
		vbDesc.Usage = D3D11_USAGE_DYNAMIC;
		vbDesc.ByteWidth = sizeof(TPNTVertex) * iDeformVertexCount;
		vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = pDevice->CreateBuffer(&vbDesc, nullptr, &m_pDX11DeformableVertexBuffer);
		if (FAILED(hr))
		{
			TraceError("DX11_CHARACTER_DEFORM_VB_CREATE_FAILED hr=0x%08X count=%d", hr, iDeformVertexCount);
			DestroyDX11VertexBuffers();
			return;
		}
	}

	// 2. Create DX11 rigid vertex buffer (static, copy from CPU shadow or DX9)
	// DX11 Model Sync M3-EGRN17.B + M3-EGRN-33: Rigid VB upload - prefer CPU shadow buffer first (strict & hybrid), fallback to DX9 Lock (hybrid only)
	const int iRigidVertexCount = m_pModel->GetRigidVertexCount();
	const bool bRigidVBRequired = (iRigidVertexCount > 0);
	bool bRigidVBReady = !bRigidVBRequired;
	if (iRigidVertexCount > 0)
	{
		// Priority 1: Try CPU shadow buffer (available in both strict and hybrid mode)
		if (!bModelDataLocked)
		{
			bModelDataLocked = m_pModel->LockVertices(&pLockedIndices, &pLockedRigidVertices);
			if (!bModelDataLocked)
			{
				static DWORD s_dwLastLockVerticesFailLogMS = 0u;
				static DWORD s_dwLockVerticesFailSinceLastLog = 0u;
				++s_dwLockVerticesFailSinceLastLog;

				const DWORD dwNow = GetTickCount();
				if (0u == s_dwLastLockVerticesFailLogMS || (dwNow - s_dwLastLockVerticesFailLogMS) >= 2000u)
				{
					s_dwLastLockVerticesFailLogMS = dwNow;
					TraceError("DX11_CHARACTER_LOCK_VERTICES_FAILED stage=rigid burst=%u", s_dwLockVerticesFailSinceLastLog);
					s_dwLockVerticesFailSinceLastLog = 0u;
				}
			}
		}

		TPNTVertex* pVertexData = nullptr;
		const UINT uRigidVertexStride = std::max<UINT>(static_cast<UINT>(sizeof(TPNTVertex)), m_pModel->GetRigidVertexStride());
		bool bNeedUnlockDX9VB = false;
		const char* pszDataSource = "none";

		// Use CPU shadow if available
		if (pLockedRigidVertices)
		{
			pVertexData = reinterpret_cast<TPNTVertex*>(pLockedRigidVertices);
			pszDataSource = "cpu_shadow";

			// DX11 Model Sync M3-EGRN-33: Throttled telemetry for rigid VB upload source tracking
			static DWORD s_dwLastRigidSourceHeartbeatMS = 0u;
			static DWORD s_dwRigidCPUShadowCount = 0u;
			++s_dwRigidCPUShadowCount;

			const DWORD dwNow = GetTickCount();
			if (0u == s_dwLastRigidSourceHeartbeatMS || (dwNow - s_dwLastRigidSourceHeartbeatMS) >= 5000u)
			{
				s_dwLastRigidSourceHeartbeatMS = dwNow;
				TraceError("DX11_EGRN33_RIGID_VB_SOURCE_HEARTBEAT source=cpu_shadow count=%u interval_ms=5000",
					s_dwRigidCPUShadowCount);
				s_dwRigidCPUShadowCount = 0u;
			}
		}
#if !defined(DX11_STRICT_ONLY)
		// Priority 2: Fallback to D3D9 VB Lock (hybrid mode only, never reached in strict mode due to #ifdef guard)
		else
		{
			ID3D11Buffer* pDX9RigidVB = m_pModel->GetPNTD3DVertexBuffer();
			if (pDX9RigidVB)
			{
				HRESULT hrLock = pDX9RigidVB->Lock(0, 0, (void**)&pVertexData, D3DLOCK_READONLY);
				if (FAILED(hrLock) || !pVertexData)
					pVertexData = nullptr;
				else
				{
					bNeedUnlockDX9VB = true;
					pszDataSource = "dx9_vb";

					// DX11 Model Sync M3-EGRN-33: Throttled telemetry for DX9 fallback usage (hybrid mode only)
					static DWORD s_dwLastDX9FallbackHeartbeatMS = 0u;
					static DWORD s_dwDX9RigidVBFallbackCount = 0u;
					++s_dwDX9RigidVBFallbackCount;

					const DWORD dwNow = GetTickCount();
					if (0u == s_dwLastDX9FallbackHeartbeatMS || (dwNow - s_dwLastDX9FallbackHeartbeatMS) >= 5000u)
					{
						s_dwLastDX9FallbackHeartbeatMS = dwNow;
						TraceError("DX11_EGRN33_RIGID_VB_DX9_FALLBACK_HEARTBEAT source=dx9_vb count=%u interval_ms=5000 mode=hybrid_only",
							s_dwDX9RigidVBFallbackCount);
						s_dwDX9RigidVBFallbackCount = 0u;
					}
				}
			}
		}
#endif // DX11_STRICT_ONLY

		if (pVertexData)
		{
			// Validate source data before passing to D3D11
			const UINT expectedByteSize = uRigidVertexStride * iRigidVertexCount;
			if (expectedByteSize == 0 || !pVertexData)
			{
				TraceError("DX11_CHARACTER_RIGID_VB_INVALID_DATA ptr=%p size=%u count=%d stride=%u",
					pVertexData, expectedByteSize, iRigidVertexCount, uRigidVertexStride);
#if !defined(DX11_STRICT_ONLY)
				if (bNeedUnlockDX9VB && m_pModel->GetPNTD3DVertexBuffer())
					m_pModel->GetPNTD3DVertexBuffer()->Unlock();
#endif
				DestroyDX11VertexBuffers();
				if (bModelDataLocked)
					m_pModel->UnlockVertices();
				return;
			}

			// Diagnostic: log buffer creation parameters before driver call
			static bool s_bLoggedRigidVBParams = false;
			if (!s_bLoggedRigidVBParams)
			{
				s_bLoggedRigidVBParams = true;
				TraceError("DX11_CHARACTER_RIGID_VB_CREATE_ATTEMPT ptr=%p size=%u count=%d stride=%u source=%s",
					pVertexData, expectedByteSize, iRigidVertexCount, uRigidVertexStride, pszDataSource);
			}

			D3D11_BUFFER_DESC vbDesc = {};
			vbDesc.Usage = D3D11_USAGE_DEFAULT;  // Changed from IMMUTABLE: forces immediate copy, eliminates pointer lifetime crash
			vbDesc.ByteWidth = expectedByteSize;
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

			D3D11_SUBRESOURCE_DATA initData = {};
			initData.pSysMem = pVertexData;

			HRESULT hr = pDevice->CreateBuffer(&vbDesc, &initData, &m_pDX11RigidVertexBuffer);
#if !defined(DX11_STRICT_ONLY)
			if (bNeedUnlockDX9VB && m_pModel->GetPNTD3DVertexBuffer())
				m_pModel->GetPNTD3DVertexBuffer()->Unlock();
#endif

			if (FAILED(hr))
			{
				TraceError("DX11_CHARACTER_RIGID_VB_CREATE_FAILED hr=0x%08X count=%d", hr, iRigidVertexCount);
				DestroyDX11VertexBuffers();
				if (bModelDataLocked)
					m_pModel->UnlockVertices();
				return;
			}
			bRigidVBReady = true;
		}
		else
		{
			static DWORD s_dwLastRigidSourceMissingLogMS = 0u;
			static DWORD s_dwRigidSourceMissingSinceLastLog = 0u;
			++s_dwRigidSourceMissingSinceLastLog;

			const DWORD dwNow = GetTickCount();
			if (0u == s_dwLastRigidSourceMissingLogMS || (dwNow - s_dwLastRigidSourceMissingLogMS) >= 2000u)
			{
				s_dwLastRigidSourceMissingLogMS = dwNow;
				TraceError("DX11_CHARACTER_RIGID_VB_SOURCE_MISSING count=%d source=%s burst=%u",
					iRigidVertexCount, pszDataSource, s_dwRigidSourceMissingSinceLastLog);
				s_dwRigidSourceMissingSinceLastLog = 0u;
			}
			DestroyDX11VertexBuffers();
			if (bModelDataLocked)
				m_pModel->UnlockVertices();
			return;
		}
	}

	// 3. Create DX11 index buffer (copy from CPU shadow or DX9)
	// DX11 Model Sync M3-EGRN17.B + M3-EGRN-33: Index buffer upload - prefer CPU shadow buffer first (strict & hybrid), fallback to DX9 Lock (hybrid only)
	const int iIndexCount = m_pModel->GetIdxCount();
	const bool bIndexBufferRequired = (iIndexCount > 0);
	bool bIndexBufferReady = !bIndexBufferRequired;
	if (iIndexCount > 0)
	{
		// Priority 1: Try CPU shadow buffer (available in both strict and hybrid mode)
		if (!bModelDataLocked)
		{
			bModelDataLocked = m_pModel->LockVertices(&pLockedIndices, &pLockedRigidVertices);
			if (!bModelDataLocked)
			{
				static DWORD s_dwLastLockVerticesFailLogMS = 0u;
				static DWORD s_dwLockVerticesFailSinceLastLog = 0u;
				++s_dwLockVerticesFailSinceLastLog;

				const DWORD dwNow = GetTickCount();
				if (0u == s_dwLastLockVerticesFailLogMS || (dwNow - s_dwLastLockVerticesFailLogMS) >= 2000u)
				{
					s_dwLastLockVerticesFailLogMS = dwNow;
					TraceError("DX11_CHARACTER_LOCK_VERTICES_FAILED stage=index burst=%u", s_dwLockVerticesFailSinceLastLog);
					s_dwLockVerticesFailSinceLastLog = 0u;
				}
			}
		}

		WORD* pIndexData = nullptr;
		bool bNeedUnlockDX9IB = false;
		const char* pszIndexSource = "none";

		// Use CPU shadow if available
		if (pLockedIndices)
		{
			pIndexData = reinterpret_cast<WORD*>(pLockedIndices);
			pszIndexSource = "cpu_shadow";

			// DX11 Model Sync M3-EGRN-33: Throttled telemetry for index buffer upload source tracking
			static DWORD s_dwLastIndexSourceHeartbeatMS = 0u;
			static DWORD s_dwIndexCPUShadowCount = 0u;
			++s_dwIndexCPUShadowCount;

			const DWORD dwNow = GetTickCount();
			if (0u == s_dwLastIndexSourceHeartbeatMS || (dwNow - s_dwLastIndexSourceHeartbeatMS) >= 5000u)
			{
				s_dwLastIndexSourceHeartbeatMS = dwNow;
				TraceError("DX11_EGRN33_INDEX_BUFFER_SOURCE_HEARTBEAT source=cpu_shadow count=%u interval_ms=5000",
					s_dwIndexCPUShadowCount);
				s_dwIndexCPUShadowCount = 0u;
			}
		}
#if !defined(DX11_STRICT_ONLY)
		// Priority 2: Fallback to D3D9 IB Lock (hybrid mode only, never reached in strict mode due to #ifdef guard)
		else
		{
			ID3D11Buffer* pDX9IndexBuf = m_pModel->GetD3DIndexBuffer();
			if (pDX9IndexBuf)
			{
				HRESULT hrLock = pDX9IndexBuf->Lock(0, 0, (void**)&pIndexData, D3DLOCK_READONLY);
				if (FAILED(hrLock) || !pIndexData)
					pIndexData = nullptr;
				else
				{
					bNeedUnlockDX9IB = true;
					pszIndexSource = "dx9_ib";
				}
			}
		}
#endif // DX11_STRICT_ONLY

		if (pIndexData)
		{
			// Validate source data before passing to D3D11
			const UINT expectedByteSize = sizeof(WORD) * iIndexCount;
			if (expectedByteSize == 0 || !pIndexData)
			{
				TraceError("DX11_CHARACTER_INDEX_BUFFER_INVALID_DATA ptr=%p size=%u count=%d",
					pIndexData, expectedByteSize, iIndexCount);
#if !defined(DX11_STRICT_ONLY)
				if (bNeedUnlockDX9IB && m_pModel->GetD3DIndexBuffer())
					m_pModel->GetD3DIndexBuffer()->Unlock();
#endif
				DestroyDX11VertexBuffers();
				if (bModelDataLocked)
					m_pModel->UnlockVertices();
				return;
			}

			// Diagnostic: log buffer creation parameters before driver call
			static bool s_bLoggedIndexBufParams = false;
			if (!s_bLoggedIndexBufParams)
			{
				s_bLoggedIndexBufParams = true;
				TraceError("DX11_CHARACTER_INDEX_BUFFER_CREATE_ATTEMPT ptr=%p size=%u count=%d source=%s",
					pIndexData, expectedByteSize, iIndexCount, pszIndexSource);
			}

			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.Usage = D3D11_USAGE_DEFAULT;  // Changed from IMMUTABLE: forces immediate copy, eliminates pointer lifetime crash
			ibDesc.ByteWidth = expectedByteSize;
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

			D3D11_SUBRESOURCE_DATA initData = {};
			initData.pSysMem = pIndexData;

			HRESULT hr = pDevice->CreateBuffer(&ibDesc, &initData, &m_pDX11IndexBuffer);
#if !defined(DX11_STRICT_ONLY)
			if (bNeedUnlockDX9IB && m_pModel->GetD3DIndexBuffer())
				m_pModel->GetD3DIndexBuffer()->Unlock();
#endif

			if (FAILED(hr))
			{
				TraceError("DX11_CHARACTER_INDEX_BUFFER_CREATE_FAILED hr=0x%08X count=%d", hr, iIndexCount);
				DestroyDX11VertexBuffers();
				if (bModelDataLocked)
					m_pModel->UnlockVertices();
				return;
			}
			bIndexBufferReady = true;
		}
		else
		{
			static DWORD s_dwLastIndexSourceMissingLogMS = 0u;
			static DWORD s_dwIndexSourceMissingSinceLastLog = 0u;
			++s_dwIndexSourceMissingSinceLastLog;

			const DWORD dwNow = GetTickCount();
			if (0u == s_dwLastIndexSourceMissingLogMS || (dwNow - s_dwLastIndexSourceMissingLogMS) >= 2000u)
			{
				s_dwLastIndexSourceMissingLogMS = dwNow;
				TraceError("DX11_CHARACTER_INDEX_BUFFER_SOURCE_MISSING count=%d source=%s burst=%u",
					iIndexCount, pszIndexSource, s_dwIndexSourceMissingSinceLastLog);
				s_dwIndexSourceMissingSinceLastLog = 0u;
			}
			DestroyDX11VertexBuffers();
			if (bModelDataLocked)
				m_pModel->UnlockVertices();
			return;
		}
	}

	if (bModelDataLocked)
		m_pModel->UnlockVertices();

	if (!bRigidVBReady || !bIndexBufferReady)
	{
		TraceError(
			"DX11_CHARACTER_VB_INIT_INCOMPLETE deform=%d rigid=%d index=%d rigid_ready=%d index_ready=%d",
			iDeformVertexCount,
			iRigidVertexCount,
			iIndexCount,
			bRigidVBReady ? 1 : 0,
			bIndexBufferReady ? 1 : 0);
		DestroyDX11VertexBuffers();
		return;
	}

	m_bDX11VertexBuffersReady = true;
	// High-frequency character spawn/despawn can create many model instances in bursts.
	// Emit aggregated heartbeat instead of per-instance log spam.
	static DWORD s_dwLastVBInitLogMS = 0u;
	static DWORD s_dwVBInitSinceLastLog = 0u;
	++s_dwVBInitSinceLastLog;

	const DWORD dwNow = GetTickCount();
	if (0u == s_dwLastVBInitLogMS || (dwNow - s_dwLastVBInitLogMS) >= 5000u)
	{
		s_dwLastVBInitLogMS = dwNow;
		TraceError(
			"DX11_CHARACTER_VB_INIT_BATCH count=%u last_deform=%d last_rigid=%d last_indices=%d",
			s_dwVBInitSinceLastLog,
			iDeformVertexCount,
			iRigidVertexCount,
			iIndexCount);
		s_dwVBInitSinceLastLog = 0u;
	}
}

void CGrannyModelInstance::DestroyDX11VertexBuffers()
{
	if (m_pDX11IndexBuffer)
	{
		m_pDX11IndexBuffer->Release();
		m_pDX11IndexBuffer = nullptr;
	}
	if (m_pDX11RigidVertexBuffer)
	{
		m_pDX11RigidVertexBuffer->Release();
		m_pDX11RigidVertexBuffer = nullptr;
	}
	if (m_pDX11DeformableVertexBuffer)
	{
		m_pDX11DeformableVertexBuffer->Release();
		m_pDX11DeformableVertexBuffer = nullptr;
	}
	m_bDX11VertexBuffersReady = false;
}

void CGrannyModelInstance::UpdateDX11DeformableVertexBuffer(ID3D11DeviceContext* pContext)
{
	if (!pContext || !m_pDX11DeformableVertexBuffer || !m_pModel->CanDeformPNTVertices())
		return;

	const int iDeformVertexCount = m_pModel->GetDeformVertexCount();
	if (iDeformVertexCount == 0)
		return;

	// DX11 Model Sync M3-EGRN-33: Throttled telemetry for deformable VB update tracking
	static DWORD s_dwLastDeformUpdateHeartbeatMS = 0u;
	static DWORD s_dwDeformUpdateCount = 0u;
	++s_dwDeformUpdateCount;

	const DWORD dwNow = GetTickCount();
	if (0u == s_dwLastDeformUpdateHeartbeatMS || (dwNow - s_dwLastDeformUpdateHeartbeatMS) >= 5000u)
	{
		s_dwLastDeformUpdateHeartbeatMS = dwNow;
		TraceError("DX11_EGRN33_DEFORM_VB_UPDATE_HEARTBEAT updates=%u last_vtx_count=%d interval_ms=5000",
			s_dwDeformUpdateCount, iDeformVertexCount);
		s_dwDeformUpdateCount = 0u;
	}

	// DX11 Model Sync M3-EGRN-33: Get deformed data from deformable VB (already updated by Deform()).
	// In strict mode: CPU-side buffer. In hybrid mode: DX9 dynamic VB.
	CGraphicVertexBuffer& rkDeformableVB = __GetDeformableVertexBufferRef();
	TPNTVertex* pSrcVertices = nullptr;
	if (!rkDeformableVB.Lock((void**)&pSrcVertices))
		return;

	// Update DX11 buffer
	D3D11_MAPPED_SUBRESOURCE mapped;
	ZeroMemory(&mapped, sizeof(mapped));
	HRESULT hr = pContext->Map(m_pDX11DeformableVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr) && mapped.pData)
	{
		memcpy(mapped.pData, pSrcVertices, sizeof(TPNTVertex) * iDeformVertexCount);
		pContext->Unmap(m_pDX11DeformableVertexBuffer, 0);
	}

	rkDeformableVB.Unlock();
}
