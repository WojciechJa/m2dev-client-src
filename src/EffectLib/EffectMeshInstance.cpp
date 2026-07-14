#include "StdAfx.h"
#include "EterLib/ResourceManager.h"
#include "EffectMeshInstance.h"
#include "EffectManager.h"
#include "Eterlib/GrpMath.h"

// W2 finalize includes
#include "EterLib/GrpDeviceDX11.h"
#include "EterLib/Camera.h"
#include <cmath>

namespace
{
	template <typename T>
	void ReleaseCOM(T*& pPtr)
	{
		if (pPtr)
		{
			pPtr->Release();
			pPtr = nullptr;
		}
	}
}

CDynamicPool<CEffectMeshInstance>		CEffectMeshInstance::ms_kPool;

void CEffectMeshInstance::DestroySystem()
{
	ms_kPool.Destroy();
}

CEffectMeshInstance* CEffectMeshInstance::New()
{
	return ms_kPool.Alloc();
}

void CEffectMeshInstance::Delete(CEffectMeshInstance* pkMeshInstance)
{
	pkMeshInstance->Destroy();
	ms_kPool.Free(pkMeshInstance);
}

BOOL CEffectMeshInstance::isActive()
{
	if (!CEffectElementBaseInstance::isActive())
		return FALSE;

	if (!m_MeshFrameController.isActive())
		return FALSE;

	for (DWORD j = 0; j < m_TextureInstanceVector.size(); ++j)
	{
		int iCurrentFrame = m_MeshFrameController.GetCurrentFrame();
		if (m_TextureInstanceVector[j].TextureFrameController.isActive(iCurrentFrame))
			return TRUE;
	}

	return FALSE;
}

bool CEffectMeshInstance::OnUpdate(float fElapsedTime)
{
	if (!isActive())
		return false;

	if (m_MeshFrameController.isActive())
		m_MeshFrameController.Update(fElapsedTime);

	for (DWORD j = 0; j < m_TextureInstanceVector.size(); ++j)
	{
		int iCurrentFrame = m_MeshFrameController.GetCurrentFrame();
		if (m_TextureInstanceVector[j].TextureFrameController.isActive(iCurrentFrame))
			m_TextureInstanceVector[j].TextureFrameController.Update(fElapsedTime);
	}

	return true;
}

void CEffectMeshInstance::OnRender()
{
	if (!isActive())
		return;
	const bool bTargetRing = (m_eDX11RenderClass == EFFECT_RENDER_CLASS_TARGET_RING);

	// DX11 render path (Batch W2)
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	CEffectManager& rkEffectManager = CEffectManager::Instance();
	if (rkEffectManager.EnsureDX11EffectResourcesReady() &&
		pDX11Device &&
		pDX11Device->IsValid())
	{
		OnRenderDX11();
		return;
	}

	if (pDX11Device && pDX11Device->IsValid())
	{
		static DWORD s_dwLastStrictSkipLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastStrictSkipLogMS || (dwNow - s_dwLastStrictSkipLogMS) >= 2000u)
		{
			s_dwLastStrictSkipLogMS = dwNow;
			TraceError("DX11_EFFECT_MESH_SKIP reason=dx11_resources_not_ready");
		}
		if (bTargetRing)
			rkEffectManager.AddDX11TargetRingSkippedCount(1u, "dx11_resources_not_ready", m_dwOwnerEffectCRC);
		return;
	}

	// Full-DX11 migration policy: no DX9 fallback path in mesh effect renderer.
	static DWORD s_dwLastRuntimeInactiveLogMS = 0u;
	const DWORD dwNowRuntime = ELTimer_GetMSec();
	if (0u == s_dwLastRuntimeInactiveLogMS || (dwNowRuntime - s_dwLastRuntimeInactiveLogMS) >= 3000u)
	{
		s_dwLastRuntimeInactiveLogMS = dwNowRuntime;
		TraceError("DX11_EFFECT_MESH_SKIP reason=dx11_runtime_inactive");
	}
	if (bTargetRing)
		rkEffectManager.AddDX11TargetRingSkippedCount(1u, "dx11_runtime_inactive", m_dwOwnerEffectCRC);
	return;
}

void CEffectMeshInstance::OnRenderDX11()
{
	if (!isActive())
		return;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return;

	ID3D11DeviceContext* pContext = pGrpDevice->GetContext();
	if (!pContext)
		return;

	CEffectManager& rMgr = CEffectManager::Instance();
	const bool bTargetRing = (m_eDX11RenderClass == EFFECT_RENDER_CLASS_TARGET_RING);

	ID3D11RenderTargetView* pCurrentRTV = nullptr;
	ID3D11DepthStencilView* pCurrentDSV = nullptr;
	pContext->OMGetRenderTargets(1, &pCurrentRTV, &pCurrentDSV);
	const bool bHasRTV = (pCurrentRTV != nullptr);
	const bool bHasDSV = (pCurrentDSV != nullptr);
	const bool bDepthOnlyPass = (!bHasRTV && bHasDSV);
	ReleaseCOM(pCurrentRTV);
	ReleaseCOM(pCurrentDSV);
	if (!bHasRTV && !bHasDSV)
	{
		if (bTargetRing)
			rMgr.AddDX11TargetRingSkippedCount(1u, "no_render_targets", m_dwOwnerEffectCRC);
		return;
	}

	if (!rMgr.IsDX11EffectResourcesReady())
	{
		if (bTargetRing)
			rMgr.AddDX11TargetRingSkippedCount(1u, "dx11_resources_not_ready", m_dwOwnerEffectCRC);
		return;
	}

	ID3D11RasterizerState* pNoCullRS = rMgr.GetDX11EffectNoCullRasterizerState();
	ID3D11DepthStencilState* pDepthState = bDepthOnlyPass ? rMgr.GetDX11EffectDepthWriteState() : rMgr.GetDX11EffectDepthReadOnlyState();
	ID3D11VertexShader* pVertexShader = rMgr.GetDX11EffectVertexShader();
	ID3D11PixelShader* pPixelShader = nullptr;
	if (bDepthOnlyPass)
		pPixelShader = rMgr.GetDX11EffectShadowAlphaPixelShader();
	else
		pPixelShader = bTargetRing ? rMgr.GetDX11EffectTargetRingPixelShader() : rMgr.GetDX11EffectPixelShader();
	ID3D11InputLayout* pInputLayout = rMgr.GetDX11EffectInputLayout();
	ID3D11SamplerState* pSampler = rMgr.GetDX11EffectSamplerState();
	ID3D11Buffer* pCB = rMgr.GetDX11EffectConstantBuffer();

	if (!pNoCullRS || !pDepthState || !pVertexShader || !pPixelShader || !pInputLayout || !pSampler || !pCB)
	{
		static DWORD s_dwLastMissingPipelineStateLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastMissingPipelineStateLogMS || (dwNow - s_dwLastMissingPipelineStateLogMS) >= 2000u)
		{
			s_dwLastMissingPipelineStateLogMS = dwNow;
			TraceError(
				"DX11_EFFECT_MESH_SKIP reason=missing_pipeline_state mode=%s no_cull=%u depth_state=%u vs=%u ps=%u layout=%u sampler=%u cb=%u target_ring=%u",
				bDepthOnlyPass ? "depth_only" : "color",
				pNoCullRS ? 1u : 0u,
				pDepthState ? 1u : 0u,
				pVertexShader ? 1u : 0u,
				pPixelShader ? 1u : 0u,
				pInputLayout ? 1u : 0u,
				pSampler ? 1u : 0u,
				pCB ? 1u : 0u,
				bTargetRing ? 1u : 0u);
		}
		if (bTargetRing)
			rMgr.AddDX11TargetRingSkippedCount(1u, "missing_pipeline_state", m_dwOwnerEffectCRC);
		return;
	}

	CEffectMesh* pEffectMesh = m_roMesh.GetPointer();
	if (!pEffectMesh || !m_pMeshScript || !mc_pmatLocal)
	{
		if (bTargetRing)
			rMgr.AddDX11TargetRingSkippedCount(1u, "mesh_instance_not_ready", m_dwOwnerEffectCRC);
		return;
	}

	CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
	if (!pCamera)
	{
		if (bTargetRing)
			rMgr.AddDX11TargetRingSkippedCount(1u, "camera_missing", m_dwOwnerEffectCRC);
		return;
	}

	const D3DXMATRIX& matView = pCamera->GetViewMatrix();
	const D3DXMATRIX& matProj = CGraphicBase::GetProjMatrix();

	ID3D11InputLayout* pPrevInputLayout = nullptr;
	D3D11_PRIMITIVE_TOPOLOGY ePrevTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	ID3D11Buffer* pPrevVB = nullptr;
	UINT uPrevVBStride = 0u;
	UINT uPrevVBOffset = 0u;
	ID3D11VertexShader* pPrevVS = nullptr;
	ID3D11PixelShader* pPrevPS = nullptr;
	ID3D11Buffer* pPrevVSCB = nullptr;
	ID3D11Buffer* pPrevPSCB = nullptr;
	ID3D11RasterizerState* pPrevRS = nullptr;
	ID3D11DepthStencilState* pPrevDSS = nullptr;
	ID3D11BlendState* pPrevBlendState = nullptr;
	ID3D11SamplerState* pPrevSampler = nullptr;
	ID3D11ShaderResourceView* pPrevSRV = nullptr;
	UINT uPrevStencilRef = 0u;
	UINT uPrevSampleMask = 0xffffffffu;
	float afPrevBlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	pContext->IAGetInputLayout(&pPrevInputLayout);
	pContext->IAGetPrimitiveTopology(&ePrevTopology);
	pContext->IAGetVertexBuffers(0, 1, &pPrevVB, &uPrevVBStride, &uPrevVBOffset);
	pContext->VSGetShader(&pPrevVS, nullptr, nullptr);
	pContext->PSGetShader(&pPrevPS, nullptr, nullptr);
	pContext->VSGetConstantBuffers(0, 1, &pPrevVSCB);
	pContext->PSGetConstantBuffers(0, 1, &pPrevPSCB);
	pContext->RSGetState(&pPrevRS);
	pContext->OMGetDepthStencilState(&pPrevDSS, &uPrevStencilRef);
	pContext->OMGetBlendState(&pPrevBlendState, afPrevBlendFactor, &uPrevSampleMask);
	pContext->PSGetSamplers(0, 1, &pPrevSampler);
	pContext->PSGetShaderResources(0, 1, &pPrevSRV);

	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(pVertexShader, nullptr, 0u);
	pContext->PSSetShader(pPixelShader, nullptr, 0u);
	pContext->VSSetConstantBuffers(0, 1, &pCB);
	pContext->PSSetConstantBuffers(0, 1, &pCB);
	pContext->RSSetState(pNoCullRS);
	pContext->OMSetDepthStencilState(pDepthState, 0u);
	pContext->PSSetSamplers(0, 1, &pSampler);

	if (bTargetRing)
		rMgr.SetDX11TargetRingPipelineState(true, bDepthOnlyPass, true, !bDepthOnlyPass);

	DWORD dwDrawCount = 0u;
	UINT64 ullPrimitiveCount = 0u;
	int iInvalidGeometrySkips = 0;
	for (DWORD i = 0; i < pEffectMesh->GetMeshCount(); ++i)
	{
		if (i >= m_TextureInstanceVector.size())
			continue;

		CFrameController& rTexFrameCtrl = m_TextureInstanceVector[i].TextureFrameController;
		if (!rTexFrameCtrl.isActive(m_MeshFrameController.GetCurrentFrame()))
			continue;

		D3DXMATRIX matWorld;
		D3DXMatrixIdentity(&matWorld);

		const int iBillboardType = m_pMeshScript->GetBillboardType(i);
		switch (iBillboardType)
		{
			case MESH_BILLBOARD_TYPE_ALL:
			{
				D3DXMATRIX matTemp;
				D3DXMatrixRotationX(&matTemp, 90.0f);
				D3DXMatrixInverse(&matWorld, NULL, &CScreen::GetViewMatrix());
				matWorld = matTemp * matWorld;
				break;
			}
			case MESH_BILLBOARD_TYPE_Y:
			{
				D3DXMATRIX matTemp;
				D3DXMatrixInverse(&matTemp, NULL, &CScreen::GetViewMatrix());
				matWorld._11 = matTemp._11;
				matWorld._12 = matTemp._12;
				matWorld._21 = matTemp._21;
				matWorld._22 = matTemp._22;
				break;
			}
			case MESH_BILLBOARD_TYPE_MOVE:
			{
				D3DXVECTOR3 vPosition;
				m_pMeshScript->GetPosition(m_fLocalTime, vPosition);
				D3DXVECTOR3 vLastPosition;
				m_pMeshScript->GetPosition(m_fLocalTime - CTimer::Instance().GetElapsedSecond(), vLastPosition);
				vPosition -= vLastPosition;
				if (D3DXVec3LengthSq(&vPosition) > 0.001f)
				{
					D3DXVec3Normalize(&vPosition, &vPosition);
					D3DXQUATERNION q = SafeRotationNormalizedArc(D3DXVECTOR3(0.0f, -1.0f, 0.0f), vPosition);
					D3DXMatrixRotationQuaternion(&matWorld, &q);
				}
				break;
			}
		}

		D3DXVECTOR3 vPosition;
		m_pMeshScript->GetPosition(m_fLocalTime, vPosition);
		if (!std::isfinite(vPosition.x) || !std::isfinite(vPosition.y) || !std::isfinite(vPosition.z))
		{
			++iInvalidGeometrySkips;
			continue;
		}
		matWorld._41 = vPosition.x;
		matWorld._42 = vPosition.y;
		matWorld._43 = vPosition.z;
		matWorld = matWorld * *mc_pmatLocal;

		D3DXMATRIX matWorldViewProj = matWorld * matView * matProj;
		const float* pfMat = reinterpret_cast<const float*>(&matWorldViewProj);
		bool bValidWorldViewProj = true;
		for (int m = 0; m < 16; ++m)
		{
			if (!std::isfinite(pfMat[m]))
			{
				bValidWorldViewProj = false;
				break;
			}
		}
		if (!bValidWorldViewProj)
		{
			++iInvalidGeometrySkips;
			continue;
		}

		D3DXCOLOR color(1.0f, 1.0f, 1.0f, 1.0f);
		m_pMeshScript->GetColorFactor(i, &color);
		TTimeEventTableFloat* pTableAlpha = nullptr;
		float fAlpha = 1.0f;
		if (m_pMeshScript->GetTimeTableAlphaPointer(i, &pTableAlpha) && pTableAlpha && !pTableAlpha->empty())
			fAlpha = GetTimeEventBlendValue(m_fLocalTime, *pTableAlpha);

		CEffectMesh::TEffectMeshData* pMeshData = pEffectMesh->GetMeshDataPointer(i);
		if (!pMeshData)
			continue;

		if (m_MeshFrameController.GetCurrentFrame() >= pMeshData->EffectFrameDataVector.size())
			continue;

		CEffectMesh::TEffectFrameData& rFrameData = pMeshData->EffectFrameDataVector[m_MeshFrameController.GetCurrentFrame()];
		if (rFrameData.PDTVertexVector.empty())
		{
			++iInvalidGeometrySkips;
			continue;
		}

		const float fFinalAlpha = fAlpha * rFrameData.fVisibility;
		const bool bUseAlphaClip = bTargetRing && !bDepthOnlyPass && (m_pMeshScript->isTextureAlphaEnable(i) != FALSE);

		struct SEffectMeshCBData
		{
			D3DXMATRIX matViewProj;
			D3DXVECTOR4 vColorFactor;
			D3DXVECTOR4 vEffectParams;
		} cbData = {};
		cbData.matViewProj = matWorldViewProj;
		cbData.vColorFactor = D3DXVECTOR4(color.r, color.g, color.b, fFinalAlpha);
		cbData.vEffectParams = D3DXVECTOR4(
			bUseAlphaClip ? rMgr.GetDX11TargetRingAlphaClipThreshold() : 0.0f,
			0.0f,
			0.0f,
			0.0f);
		pContext->UpdateSubresource(pCB, 0u, nullptr, &cbData, 0u, 0u);

		DWORD dwTexFrame = rTexFrameCtrl.GetCurrentFrame();
		CGraphicImageInstance* pImgInst = nullptr;
		if (dwTexFrame < m_TextureInstanceVector[i].TextureInstanceVector.size())
			pImgInst = m_TextureInstanceVector[i].TextureInstanceVector[dwTexFrame];

		ID3D11ShaderResourceView* pSRV = rMgr.GetEffectTextureSRV(pImgInst);
		pContext->PSSetShaderResources(0, 1, &pSRV);

		if (!bDepthOnlyPass)
		{
			const bool bBlendingEnable = (m_pMeshScript->isBlendingEnable(i) != FALSE);
			const BYTE byBlendingSrcType = static_cast<BYTE>(m_pMeshScript->GetBlendingSrcType(i));
			const BYTE byBlendingDestType = static_cast<BYTE>(m_pMeshScript->GetBlendingDestType(i));
			ID3D11BlendState* pBlendState = rMgr.ResolveDX11EffectBlendState(bBlendingEnable, byBlendingSrcType, byBlendingDestType);

			const float afBlendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
			pContext->OMSetBlendState(pBlendState, afBlendFactor, 0xFFFFFFFFu);
			if (bTargetRing)
				rMgr.SetDX11TargetRingBlendState(bBlendingEnable, byBlendingSrcType, byBlendingDestType);
		}

		const UINT uVertexCount = static_cast<UINT>(rFrameData.PDTVertexVector.size());
		const UINT64 ullVertexDataSize = static_cast<UINT64>(sizeof(TPTVertex)) * static_cast<UINT64>(uVertexCount);
		if (0u == uVertexCount || ullVertexDataSize > static_cast<UINT64>(0xffffffffu))
		{
			++iInvalidGeometrySkips;
			continue;
		}

		if (!rMgr.EnsureDX11EffectDynamicVB(uVertexCount))
		{
			++iInvalidGeometrySkips;
			continue;
		}

		ID3D11Buffer* pDynamicVB = rMgr.GetDX11EffectDynamicVB();
		if (!pDynamicVB)
		{
			++iInvalidGeometrySkips;
			continue;
		}

		bool bValidMeshVertices = true;
		for (UINT uVert = 0; uVert < uVertexCount; ++uVert)
		{
			const D3DXVECTOR3& kPos = rFrameData.PDTVertexVector[uVert].position;
			const D3DXVECTOR2& kUV = rFrameData.PDTVertexVector[uVert].texCoord;
			if (!std::isfinite(kPos.x) || !std::isfinite(kPos.y) || !std::isfinite(kPos.z) ||
				std::fabs(kPos.x) > 1000000.0f || std::fabs(kPos.y) > 1000000.0f || std::fabs(kPos.z) > 1000000.0f ||
				!std::isfinite(kUV.x) || !std::isfinite(kUV.y))
			{
				bValidMeshVertices = false;
				break;
			}
		}
		if (!bValidMeshVertices)
		{
			++iInvalidGeometrySkips;
			continue;
		}

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const HRESULT hr = pContext->Map(pDynamicVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr) || !mapped.pData)
		{
			++iInvalidGeometrySkips;
			continue;
		}

		const UINT uVertexDataSize = static_cast<UINT>(ullVertexDataSize);
		memcpy(mapped.pData, &rFrameData.PDTVertexVector[0], uVertexDataSize);
		pContext->Unmap(pDynamicVB, 0);

		UINT uStride = sizeof(TPTVertex);
		UINT uOffset = 0u;
		pContext->IASetVertexBuffers(0, 1, &pDynamicVB, &uStride, &uOffset);
		pContext->Draw(uVertexCount, 0u);
		++dwDrawCount;
		ullPrimitiveCount += static_cast<UINT64>(uVertexCount / 3u);
	}

	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);

	pContext->IASetInputLayout(pPrevInputLayout);
	pContext->IASetPrimitiveTopology(ePrevTopology);
	pContext->IASetVertexBuffers(0, 1, &pPrevVB, &uPrevVBStride, &uPrevVBOffset);
	pContext->VSSetShader(pPrevVS, nullptr, 0u);
	pContext->PSSetShader(pPrevPS, nullptr, 0u);
	pContext->VSSetConstantBuffers(0, 1, &pPrevVSCB);
	pContext->PSSetConstantBuffers(0, 1, &pPrevPSCB);
	pContext->PSSetShaderResources(0, 1, &pPrevSRV);
	pContext->PSSetSamplers(0, 1, &pPrevSampler);
	pContext->OMSetBlendState(pPrevBlendState, afPrevBlendFactor, uPrevSampleMask);
	pContext->RSSetState(pPrevRS);
	pContext->OMSetDepthStencilState(pPrevDSS, uPrevStencilRef);

	ReleaseCOM(pPrevInputLayout);
	ReleaseCOM(pPrevVB);
	ReleaseCOM(pPrevVS);
	ReleaseCOM(pPrevPS);
	ReleaseCOM(pPrevVSCB);
	ReleaseCOM(pPrevPSCB);
	ReleaseCOM(pPrevSRV);
	ReleaseCOM(pPrevSampler);
	ReleaseCOM(pPrevBlendState);
	ReleaseCOM(pPrevRS);
	ReleaseCOM(pPrevDSS);

	if (!bDepthOnlyPass && dwDrawCount > 0u)
	{
		pGrpDevice->IncrementFrameDrawCalls(dwDrawCount, static_cast<UINT>(ullPrimitiveCount));
		extern void ReportImGuiEffectsDrawCalls(UINT32 draws, UINT64 prims);
		ReportImGuiEffectsDrawCalls(dwDrawCount, ullPrimitiveCount);
		rMgr.AddDX11SubmittedEffectCount(dwDrawCount);
		rMgr.AddDX11SubmittedMeshEffectCount(dwDrawCount);
		if (bTargetRing)
			rMgr.AddDX11TargetRingSubmittedCount(dwDrawCount, m_dwOwnerEffectCRC);
	}

	if (iInvalidGeometrySkips > 0)
	{
		static DWORD s_dwInvalidMeshGeomTick = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwInvalidMeshGeomTick || (dwNow - s_dwInvalidMeshGeomTick) >= 2000u)
		{
			s_dwInvalidMeshGeomTick = dwNow;
			TraceError("DX11_EFFECT_MESH_SKIP reason=invalid_geometry skipped=%d target_ring=%u", iInvalidGeometrySkips, bTargetRing ? 1u : 0u);
		}
		if (bTargetRing)
			rMgr.AddDX11TargetRingSkippedCount(static_cast<uint32_t>(iInvalidGeometrySkips), "invalid_geometry", m_dwOwnerEffectCRC);
	}
}

void CEffectMeshInstance::OnSetDataPointer(CEffectElementBase * pElement)
{
	CEffectMeshScript * pMesh = (CEffectMeshScript *)pElement;
	m_pMeshScript = pMesh;

	const char * c_szMeshFileName = pMesh->GetMeshFileName();

	m_pEffectMesh = (CEffectMesh *) CResourceManager::Instance().GetResourcePointer(c_szMeshFileName);

	if (!m_pEffectMesh)
		return;

	m_roMesh.SetPointer(m_pEffectMesh);

	m_MeshFrameController.Clear();
	m_MeshFrameController.SetMaxFrame(m_roMesh.GetPointer()->GetFrameCount());
	m_MeshFrameController.SetFrameTime(pMesh->GetMeshAnimationFrameDelay());
	m_MeshFrameController.SetLoopFlag(pMesh->isMeshAnimationLoop());
	m_MeshFrameController.SetLoopCount(pMesh->GetMeshAnimationLoopCount());
	m_MeshFrameController.SetStartFrame(0);

	m_TextureInstanceVector.clear();
	m_TextureInstanceVector.resize(m_pEffectMesh->GetMeshCount());
	for (DWORD j = 0; j < m_TextureInstanceVector.size(); ++j)
	{
		CEffectMeshScript::TMeshData * pMeshData;
		if (!m_pMeshScript->GetMeshDataPointer(j, &pMeshData))
			continue;
		
		CEffectMesh* pkEftMesh=m_roMesh.GetPointer();

		if (!pkEftMesh)
			continue;

		std::vector<CGraphicImage*>* pTextureVector = pkEftMesh->GetTextureVectorPointer(j);
		if (!pTextureVector)
			continue;

		std::vector<CGraphicImage*>& rTextureVector = *pTextureVector;

		CFrameController & rFrameController = m_TextureInstanceVector[j].TextureFrameController;
		rFrameController.Clear();
		rFrameController.SetMaxFrame(rTextureVector.size());
		rFrameController.SetFrameTime(pMeshData->fTextureAnimationFrameDelay);
		rFrameController.SetLoopFlag(pMeshData->bTextureAnimationLoopEnable);
		rFrameController.SetStartFrame(pMeshData->dwTextureAnimationStartFrame);

		std::vector<CGraphicImageInstance*> & rImageInstanceVector = m_TextureInstanceVector[j].TextureInstanceVector;
		rImageInstanceVector.clear();
		rImageInstanceVector.reserve(rTextureVector.size());
		for (std::vector<CGraphicImage*>::iterator itor = rTextureVector.begin(); itor != rTextureVector.end(); ++itor)
		{
			CGraphicImage * pImage = *itor;
			CGraphicImageInstance * pImageInstance = CGraphicImageInstance::ms_kPool.Alloc();
			pImageInstance->SetImagePointer(pImage);
			rImageInstanceVector.push_back(pImageInstance);
		}
	}
}

void CEffectMeshInstance_DeleteImageInstance(CGraphicImageInstance * pkInstance)
{
	CGraphicImageInstance::Delete(pkInstance);
}

void CEffectMeshInstance_DeleteTextureInstance(CEffectMeshInstance::TTextureInstance & rkInstance)
{
	std::vector<CGraphicImageInstance*> & rVector = rkInstance.TextureInstanceVector;
	for (std::vector<CGraphicImageInstance*>::iterator it = rVector.begin(); it != rVector.end(); ++it)
	{
		CEffectMeshInstance_DeleteImageInstance(*it);
		*it = nullptr;
	}
	rVector.clear();
}

void CEffectMeshInstance::OnInitialize()
{
	m_pMeshScript = nullptr;
	m_pEffectMesh = nullptr;
	m_eDX11RenderClass = EFFECT_RENDER_CLASS_DEFAULT;
	m_dwOwnerEffectCRC = 0u;
}

void CEffectMeshInstance::OnDestroy()
{
	for_each(m_TextureInstanceVector.begin(), m_TextureInstanceVector.end(), CEffectMeshInstance_DeleteTextureInstance);
	m_TextureInstanceVector.clear();
	m_roMesh.SetPointer(NULL);
	m_pMeshScript = nullptr;
	m_pEffectMesh = nullptr;
	m_eDX11RenderClass = EFFECT_RENDER_CLASS_DEFAULT;
	m_dwOwnerEffectCRC = 0u;
}

CEffectMeshInstance::CEffectMeshInstance()
{
	Initialize();
}

CEffectMeshInstance::~CEffectMeshInstance()
{
	Destroy();
}
