#include "StdAfx.h"
#include "EterBase/Random.h"
#include "ParticleSystemData.h"
#include "ParticleSystemInstance.h"
#include "ParticleInstance.h"
#include "EffectManager.h"
#include "EterLib/GrpDeviceDX11.h"
#include <cmath>

CDynamicPool<CParticleSystemInstance>	CParticleSystemInstance::ms_kPool;
float CParticleSystemInstance::ms_fGlobalEmissionScale = 1.0f;

void CParticleSystemInstance::SetGlobalEmissionScale(float fScale)
{
	if (fScale < 0.1f)
		fScale = 0.1f;
	else if (fScale > 1.0f)
		fScale = 1.0f;

	ms_fGlobalEmissionScale = fScale;
}

float CParticleSystemInstance::GetGlobalEmissionScale()
{
	return ms_fGlobalEmissionScale;
}

void CParticleSystemInstance::DestroySystem()
{
	ms_kPool.Destroy();

	CParticleInstance::DestroySystem();
	//CRayParticleInstance::DestroySystem();
}

CParticleSystemInstance* CParticleSystemInstance::New()
{
	return ms_kPool.Alloc();
}

void CParticleSystemInstance::Delete(CParticleSystemInstance* pkPSInst)
{
	pkPSInst->Destroy();
	ms_kPool.Free(pkPSInst);
}



DWORD CParticleSystemInstance::GetEmissionCount()
{
	return m_dwCurrentEmissionCount;
}

void CParticleSystemInstance::CreateParticles(float fElapsedTime)
{
	float fEmissionCount;
	m_pEmitterProperty->GetEmissionCountPerSecond(m_fLocalTime, &fEmissionCount);

	float fCreatingValue = fEmissionCount * ms_fGlobalEmissionScale * (fElapsedTime / 1.0f) + m_fEmissionResidue;
	int iCreatingCount = int(fCreatingValue);
	m_fEmissionResidue = fCreatingValue - iCreatingCount;

	int icurEmissionCount = GetEmissionCount();
	int iMaxEmissionCount = int(m_pEmitterProperty->GetMaxEmissionCount());
	int iNextEmissionCount = int(icurEmissionCount + iCreatingCount);
	iCreatingCount -= std::max(0, iNextEmissionCount - iMaxEmissionCount);

	float fLifeTime = 0.0f;
	float fEmittingSize = 0.0f;
	D3DXVECTOR3 _v3TimePosition;
	D3DXVECTOR3 _v3Velocity;
	float fVelocity = 0.0f;
	D3DXVECTOR2 v2HalfSize;
	float fLieRotation = 0;
	if (iCreatingCount)
	{
		m_pEmitterProperty->GetParticleLifeTime(m_fLocalTime, &fLifeTime);
		if (fLifeTime==0.0f)
		{
			return;
		}
		
		m_pEmitterProperty->GetEmittingSize(m_fLocalTime, &fEmittingSize);
		
		m_pData->GetPosition(m_fLocalTime, _v3TimePosition);
		
		m_pEmitterProperty->GetEmittingDirectionX(m_fLocalTime, &_v3Velocity.x);
		m_pEmitterProperty->GetEmittingDirectionY(m_fLocalTime, &_v3Velocity.y);
		m_pEmitterProperty->GetEmittingDirectionZ(m_fLocalTime, &_v3Velocity.z);
		
		m_pEmitterProperty->GetEmittingVelocity(m_fLocalTime, &fVelocity);
		
		m_pEmitterProperty->GetParticleSizeX(m_fLocalTime, &v2HalfSize.x);
		m_pEmitterProperty->GetParticleSizeY(m_fLocalTime, &v2HalfSize.y);

		if (BILLBOARD_TYPE_LIE == m_pParticleProperty->m_byBillboardType && mc_pmatLocal)
		{
			float fsx = mc_pmatLocal->_32;
			float fcx = sqrtf(1.0f - fsx * fsx);

			if (fcx >= 0.00001f) 
				fLieRotation = D3DXToDegree(atan2f(-mc_pmatLocal->_12, mc_pmatLocal->_22));
		}

	}

	for (int i = 0; i < iCreatingCount; ++i)
	{
		CParticleInstance * pInstance;

		pInstance = CParticleInstance::New();
		pInstance->m_pParticleProperty = m_pParticleProperty;
		pInstance->m_pEmitterProperty = m_pEmitterProperty;

		// LifeTime
		pInstance->m_fLifeTime = fLifeTime;
		pInstance->m_fLastLifeTime = fLifeTime;

		// Position
		switch (m_pEmitterProperty->GetEmitterShape())
		{
			case CEmitterProperty::EMITTER_SHAPE_POINT:
				pInstance->m_v3Position.x = 0.0f;
				pInstance->m_v3Position.y = 0.0f;
				pInstance->m_v3Position.z = 0.0f;
				break;

			case CEmitterProperty::EMITTER_SHAPE_ELLIPSE:
				pInstance->m_v3Position.x = frandom(-500.0f, 500.0f);
				pInstance->m_v3Position.y = frandom(-500.0f, 500.0f);
				pInstance->m_v3Position.z = 0.0f;
				D3DXVec3Normalize(&pInstance->m_v3Position, &pInstance->m_v3Position);

				if (m_pEmitterProperty->isEmitFromEdge())
				{
					pInstance->m_v3Position *= (m_pEmitterProperty->m_fEmittingRadius + fEmittingSize);
				}
				else
				{
					pInstance->m_v3Position *= (frandom(0.0f, m_pEmitterProperty->m_fEmittingRadius) + fEmittingSize);
				}
				break;

			case CEmitterProperty::EMITTER_SHAPE_SQUARE:
				pInstance->m_v3Position.x = (frandom(-m_pEmitterProperty->m_v3EmittingSize.x/2.0f, m_pEmitterProperty->m_v3EmittingSize.x/2.0f) + fEmittingSize);
				pInstance->m_v3Position.y = (frandom(-m_pEmitterProperty->m_v3EmittingSize.y/2.0f, m_pEmitterProperty->m_v3EmittingSize.y/2.0f) + fEmittingSize);
				pInstance->m_v3Position.z = (frandom(-m_pEmitterProperty->m_v3EmittingSize.z/2.0f, m_pEmitterProperty->m_v3EmittingSize.z/2.0f) + fEmittingSize);
				break;

			case CEmitterProperty::EMITTER_SHAPE_SPHERE:
				pInstance->m_v3Position.x = frandom(-500.0f, 500.0f);
				pInstance->m_v3Position.y = frandom(-500.0f, 500.0f);
				pInstance->m_v3Position.z = frandom(-500.0f, 500.0f);
				D3DXVec3Normalize(&pInstance->m_v3Position, &pInstance->m_v3Position);

				if (m_pEmitterProperty->isEmitFromEdge())
				{
					pInstance->m_v3Position *= (m_pEmitterProperty->m_fEmittingRadius + fEmittingSize);
				}
				else
				{
					pInstance->m_v3Position *= (frandom(0.0f, m_pEmitterProperty->m_fEmittingRadius) + fEmittingSize);
				}
				break;
		}

		// Position
		D3DXVECTOR3 v3TimePosition=_v3TimePosition;

		pInstance->m_v3Position += v3TimePosition;

		if (mc_pmatLocal && !m_pParticleProperty->m_bAttachFlag)
		{
			D3DXVec3TransformCoord(&pInstance->m_v3Position,&pInstance->m_v3Position,mc_pmatLocal);
			D3DXVec3TransformCoord(&v3TimePosition, &v3TimePosition, mc_pmatLocal);
		}
		pInstance->m_v3StartPosition = v3TimePosition;
		// NOTE : Update를 호출하지 않고 Rendering 되기 때문에 length가 0이 되는 문제가 있다.
		//        Velocity를 구한 후 그만큼 빼준 값으로 초기화 해주도록 바꿨음 - [levites]
		//pInstance->m_v3LastPosition = pInstance->m_v3Position;

		// Direction & Velocity
		pInstance->m_v3Velocity.x = 0.0f;
		pInstance->m_v3Velocity.y = 0.0f;
		pInstance->m_v3Velocity.z = 0.0f;

		if (CEmitterProperty::EMITTER_ADVANCED_TYPE_INNER == m_pEmitterProperty->GetEmitterAdvancedType())
		{
			auto d3dd = (pInstance->m_v3Position - v3TimePosition);
			D3DXVec3Normalize(&pInstance->m_v3Velocity, &d3dd);
			pInstance->m_v3Velocity *= -100.0f;
		}
		else if (CEmitterProperty::EMITTER_ADVANCED_TYPE_OUTER == m_pEmitterProperty->GetEmitterAdvancedType())
		{
			if (m_pEmitterProperty->GetEmitterShape() == CEmitterProperty::EMITTER_SHAPE_POINT)
			{
				pInstance->m_v3Velocity.x = frandom(-100.0f, 100.0f);
				pInstance->m_v3Velocity.y = frandom(-100.0f, 100.0f);
				pInstance->m_v3Velocity.z = frandom(-100.0f, 100.0f);
			}
			else
			{
				auto d3dd = (pInstance->m_v3Position - v3TimePosition);
				D3DXVec3Normalize(&pInstance->m_v3Velocity, &d3dd);
				pInstance->m_v3Velocity *= 100.0f;
			}
		}

		D3DXVECTOR3 v3Velocity = _v3Velocity;
		if (mc_pmatLocal && !m_pParticleProperty->m_bAttachFlag)
		{
			D3DXVec3TransformNormal(&v3Velocity, &v3Velocity, mc_pmatLocal);
		}

		pInstance->m_v3Velocity += v3Velocity;
		if (m_pEmitterProperty->m_v3EmittingDirection.x > 0.0f)
			pInstance->m_v3Velocity.x += frandom(-m_pEmitterProperty->m_v3EmittingDirection.x/2.0f, m_pEmitterProperty->m_v3EmittingDirection.x/2.0f) * 1000.0f;
		if (m_pEmitterProperty->m_v3EmittingDirection.y > 0.0f)
			pInstance->m_v3Velocity.y += frandom(-m_pEmitterProperty->m_v3EmittingDirection.y/2.0f, m_pEmitterProperty->m_v3EmittingDirection.y/2.0f) * 1000.0f;
		if (m_pEmitterProperty->m_v3EmittingDirection.z > 0.0f)
			pInstance->m_v3Velocity.z += frandom(-m_pEmitterProperty->m_v3EmittingDirection.z/2.0f, m_pEmitterProperty->m_v3EmittingDirection.z/2.0f) * 1000.0f;

		pInstance->m_v3Velocity *= fVelocity;

		// Size
		pInstance->m_v2HalfSize = v2HalfSize;

		// Rotation
		pInstance->m_fRotation = m_pParticleProperty->m_wRotationRandomStartingBegin;
		pInstance->m_fRotation = frandom(m_pParticleProperty->m_wRotationRandomStartingBegin,m_pParticleProperty->m_wRotationRandomStartingEnd);
		// Rotation - Lie 일 경우 LocalMatrix 의 Rotation 값을 Random 에 적용한다.
		//            매번 할 필요는 없을듯. 어느 정도의 최적화가 필요. - [levites]
		if (BILLBOARD_TYPE_LIE == m_pParticleProperty->m_byBillboardType && mc_pmatLocal)
		{
			pInstance->m_fRotation += fLieRotation;
		}

		// Texture Animation
		pInstance->m_byFrameIndex = 0;
		pInstance->m_byTextureAnimationType = m_pParticleProperty->GetTextureAnimationType();

		if (m_pParticleProperty->GetTextureAnimationFrameCount() > 1)
		{
			if (CParticleProperty::TEXTURE_ANIMATION_TYPE_RANDOM_DIRECTION == m_pParticleProperty->GetTextureAnimationType())
			{
				if (random() & 1)
				{
					pInstance->m_byFrameIndex = 0;
					pInstance->m_byTextureAnimationType = CParticleProperty::TEXTURE_ANIMATION_TYPE_CW;
				}
				else
				{
					pInstance->m_byFrameIndex = m_pParticleProperty->GetTextureAnimationFrameCount() - 1;
					pInstance->m_byTextureAnimationType = CParticleProperty::TEXTURE_ANIMATION_TYPE_CCW;
				}
			}
			if (m_pParticleProperty->m_bTexAniRandomStartFrameFlag)
			{
				pInstance->m_byFrameIndex = random_range(0,m_pParticleProperty->GetTextureAnimationFrameCount()-1);
			}
		}

		// Simple Update
		{
			pInstance->m_v3LastPosition = pInstance->m_v3Position - (pInstance->m_v3Velocity * fElapsedTime);
			pInstance->m_v2Scale.x = m_pParticleProperty->m_TimeEventScaleX.front().m_Value;
			pInstance->m_v2Scale.y= m_pParticleProperty->m_TimeEventScaleY.front().m_Value;
			//pInstance->m_v2Scale = m_pParticleProperty->m_TimeEventScaleXY.front().m_Value;
			pInstance->m_Color = m_pParticleProperty->m_TimeEventColor.front().m_Value;
		}

		m_ParticleInstanceListVector[pInstance->m_byFrameIndex].push_back(pInstance);
		m_dwCurrentEmissionCount++;
	}
}

bool CParticleSystemInstance::OnUpdate(float fElapsedTime)
{
	bool bMakeParticle = true;

	/////

	if (m_fLocalTime >= m_pEmitterProperty->GetCycleLength())
	{
		if (m_pEmitterProperty->isCycleLoop() && --m_iLoopCount!=0)
		{
			if (m_iLoopCount<0)
				m_iLoopCount = 0;
			m_fLocalTime = m_fLocalTime - m_pEmitterProperty->GetCycleLength();
		}
		else
		{
			bMakeParticle = false;
			m_iLoopCount=1;
			if (GetEmissionCount()==0)
				return false;
		}
	}

	/////

	int dwFrameIndex;
	int dwFrameCount = m_pParticleProperty->GetTextureAnimationFrameCount();

	float fAngularVelocity;
	m_pEmitterProperty->GetEmittingAngularVelocity(m_fLocalTime,&fAngularVelocity);
	
	if (fAngularVelocity && !m_pParticleProperty->m_bAttachFlag)
	{
		auto d3dd = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
		D3DXVec3TransformNormal(&m_pParticleProperty->m_v3ZAxis,&d3dd,mc_pmatLocal);
	}

	for (dwFrameIndex = 0; dwFrameIndex < dwFrameCount; dwFrameIndex++)
	{
		TParticleInstanceList::iterator itor = m_ParticleInstanceListVector[dwFrameIndex].begin();
		for (; itor != m_ParticleInstanceListVector[dwFrameIndex].end();)
		{
			CParticleInstance * pInstance = *itor;

			if (!pInstance->Update(fElapsedTime,fAngularVelocity)) [[unlikely]] {
				pInstance->DeleteThis();

				itor = m_ParticleInstanceListVector[dwFrameIndex].erase(itor);
				m_dwCurrentEmissionCount--;
			}
			else [[likely]] {
				if (pInstance->m_byFrameIndex != dwFrameIndex)
				{
					m_ParticleInstanceListVector[dwFrameCount+pInstance->m_byFrameIndex].push_back(*itor);
					itor = m_ParticleInstanceListVector[dwFrameIndex].erase(itor);
				}
				else
					++itor;
			}
		}
	}
	if (isActive() && bMakeParticle)
		CreateParticles(fElapsedTime);

	for (dwFrameIndex = 0; dwFrameIndex < dwFrameCount; ++dwFrameIndex)
	{
		m_ParticleInstanceListVector[dwFrameIndex].splice(m_ParticleInstanceListVector[dwFrameIndex].end(),m_ParticleInstanceListVector[dwFrameIndex+dwFrameCount]);
		m_ParticleInstanceListVector[dwFrameIndex+dwFrameCount].clear();
	}

	return true;
}

void CParticleSystemInstance::OnRender()
{
	CEffectManager& rMgr = CEffectManager::Instance();
	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (rMgr.EnsureDX11EffectResourcesReady() && pGrpDevice && pGrpDevice->IsValid())
	{
		ID3D11DeviceContext* pContext = pGrpDevice->GetContext();
		if (pContext)
		{
			// Skip effect rendering during depth-only passes (no RTV bound).
			ID3D11RenderTargetView* pCurrentRTV = nullptr;
			ID3D11DepthStencilView* pCurrentDSV = nullptr;
			pContext->OMGetRenderTargets(1, &pCurrentRTV, &pCurrentDSV);
			const bool bHasRTV = (pCurrentRTV != nullptr);
			const bool bHasDSV = (pCurrentDSV != nullptr);
			if (pCurrentRTV)
				pCurrentRTV->Release();
			if (pCurrentDSV)
				pCurrentDSV->Release();
			if (!bHasRTV)
			{
				if (bHasDSV)
				{
					static DWORD s_dwDepthOnlySkipLogMS = 0u;
					const DWORD dwNow = ELTimer_GetMSec();
					if (0u == s_dwDepthOnlySkipLogMS || (dwNow - s_dwDepthOnlySkipLogMS) >= 5000u)
					{
						s_dwDepthOnlySkipLogMS = dwNow;
						TraceError("DX11_EFFECT_PARTICLE_SKIP reason=depth_only_pass");
					}
				}
				return;
			}

			ID3D11InputLayout* pInputLayout = rMgr.GetDX11EffectInputLayout();
			ID3D11VertexShader* pVS = rMgr.GetDX11EffectVertexShader();
			ID3D11PixelShader* pPS = rMgr.GetDX11EffectPixelShader();
			ID3D11Buffer* pCB = rMgr.GetDX11EffectConstantBuffer();
			ID3D11SamplerState* pSampler = rMgr.GetDX11EffectSamplerState();
			if (!pInputLayout || !pVS || !pPS || !pCB || !pSampler)
				return;

			// DX11 native parity for particles: no backface culling + no depth writes.
			ID3D11RasterizerState* pNoCullRS = rMgr.GetDX11EffectNoCullRasterizerState();
			ID3D11DepthStencilState* pDepthReadOnlyDSS = rMgr.GetDX11EffectDepthReadOnlyState();
			if (!pNoCullRS || !pDepthReadOnlyDSS)
			{
				static DWORD s_dwLastMissingPipelineStateLogMS = 0u;
				const DWORD dwNow = ELTimer_GetMSec();
				if (0u == s_dwLastMissingPipelineStateLogMS || (dwNow - s_dwLastMissingPipelineStateLogMS) >= 2000u)
				{
					s_dwLastMissingPipelineStateLogMS = dwNow;
					TraceError(
						"DX11_EFFECT_PARTICLE_SKIP reason=missing_pipeline_state no_cull=%u depth_readonly=%u",
						pNoCullRS ? 1u : 0u,
						pDepthReadOnlyDSS ? 1u : 0u);
				}
				return;
			}

			if (!rMgr.EnsureDX11EffectDynamicVB(4u))
				return;

			ID3D11Buffer* pDynamicVB = rMgr.GetDX11EffectDynamicVB();
			if (!pDynamicVB)
				return;

			ID3D11RasterizerState* pPrevRS = nullptr;
			ID3D11DepthStencilState* pPrevDSS = nullptr;
			UINT uPrevStencilRef = 0;
			pContext->RSGetState(&pPrevRS);
			pContext->OMGetDepthStencilState(&pPrevDSS, &uPrevStencilRef);
			pContext->RSSetState(pNoCullRS);
			pContext->OMSetDepthStencilState(pDepthReadOnlyDSS, 0u);

			pContext->IASetInputLayout(pInputLayout);
			pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			pContext->VSSetShader(pVS, nullptr, 0);
			pContext->PSSetShader(pPS, nullptr, 0);
			pContext->VSSetConstantBuffers(0, 1, &pCB);
			pContext->PSSetConstantBuffers(0, 1, &pCB);
			pContext->PSSetSamplers(0, 1, &pSampler);

			const BYTE bySrcBlend = m_pParticleProperty ? m_pParticleProperty->m_bySrcBlendType : static_cast<BYTE>(GRP_BLEND_SRCALPHA);
			const BYTE byDestBlend = m_pParticleProperty ? m_pParticleProperty->m_byDestBlendType : static_cast<BYTE>(GRP_BLEND_INVSRCALPHA);
			ID3D11BlendState* pBlendState = rMgr.ResolveDX11EffectBlendState(true, bySrcBlend, byDestBlend);

			const float afBlendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
			pContext->OMSetBlendState(pBlendState, afBlendFactor, 0xFFFFFFFFu);

			D3DXMATRIX matViewProj;
			D3DXMatrixMultiply(&matViewProj, &CGraphicBase::GetViewMatrix(), &CGraphicBase::GetProjMatrix());

			struct SEffectCBData
			{
				D3DXMATRIX matViewProj;
				D3DXVECTOR4 vColorFactor;
				D3DXVECTOR4 vEffectParams;
			};

			DWORD dwDX11DrawCount = 0;
			UINT uDX11PrimitiveCount = 0u;
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			int iInvalidGeometrySkips = 0;

			const auto DrawParticleDX11 = [&](CParticleInstance* pInstance, ID3D11ShaderResourceView* pSRV) -> bool
			{
				if (!pInstance || !pSRV)
					return false;

				const TPTVertex* pParticleVerts = pInstance->GetParticleMeshPointer();
				if (!pParticleVerts)
					return false;

				// Guard against malformed particle quads (NaN/Inf/absurd coords) that can explode into
				// screen-sized "fan" artifacts when the selection/target effect is hovered.
				for (UINT uVert = 0; uVert < 4u; ++uVert)
				{
					const D3DXVECTOR3& kPos = pParticleVerts[uVert].position;
					if (!std::isfinite(kPos.x) || !std::isfinite(kPos.y) || !std::isfinite(kPos.z) ||
						std::fabs(kPos.x) > 1000000.0f || std::fabs(kPos.y) > 1000000.0f || std::fabs(kPos.z) > 1000000.0f)
					{
						++iInvalidGeometrySkips;
						return false;
					}
				}

				SEffectCBData cbData = {};
				cbData.matViewProj = matViewProj;
				cbData.vColorFactor = D3DXVECTOR4(
					pInstance->m_Color.r,
					pInstance->m_Color.g,
					pInstance->m_Color.b,
					pInstance->m_Color.a);
				cbData.vEffectParams = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
				pContext->UpdateSubresource(pCB, 0, nullptr, &cbData, 0, 0);
				pContext->PSSetShaderResources(0, 1, &pSRV);

				HRESULT hr = pContext->Map(pDynamicVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
				if (FAILED(hr) || !mapped.pData)
					return false;

				memcpy(mapped.pData, pParticleVerts, sizeof(TPTVertex) * 4u);
				pContext->Unmap(pDynamicVB, 0);

				UINT uStride = sizeof(TPTVertex);
				UINT uOffset = 0;
				pContext->IASetVertexBuffers(0, 1, &pDynamicVB, &uStride, &uOffset);
				pContext->Draw(4, 0);
				++dwDX11DrawCount;
				uDX11PrimitiveCount += 2u;
				return true;
			};

			for (DWORD dwFrameIndex = 0; dwFrameIndex < m_kVct_pkImgInst.size(); ++dwFrameIndex)
			{
				CGraphicImageInstance* pImageInst = m_kVct_pkImgInst[dwFrameIndex];
				ID3D11ShaderResourceView* pSRV = rMgr.GetEffectTextureSRV(pImageInst);
				if (!pSRV)
					continue;

				TParticleInstanceList::iterator itor = m_ParticleInstanceListVector[dwFrameIndex].begin();
				for (; itor != m_ParticleInstanceListVector[dwFrameIndex].end(); ++itor)
				{
					CParticleInstance* pInstance = *itor;
					if (!pInstance || !InFrustum(pInstance))
						continue;

					if (m_pParticleProperty->m_byBillboardType < BILLBOARD_TYPE_2FACE)
					{
						if (!m_pParticleProperty->m_bAttachFlag)
							pInstance->Transform();
						else
							pInstance->Transform(mc_pmatLocal);
						DrawParticleDX11(pInstance, pSRV);
					}
					else if (m_pParticleProperty->m_byBillboardType == BILLBOARD_TYPE_2FACE)
					{
						if (!m_pParticleProperty->m_bAttachFlag)
						{
							pInstance->Transform(nullptr, D3DXToRadian(-30.0f));
							DrawParticleDX11(pInstance, pSRV);
							pInstance->Transform(nullptr, D3DXToRadian(+30.0f));
							DrawParticleDX11(pInstance, pSRV);
						}
						else
						{
							pInstance->Transform(mc_pmatLocal, D3DXToRadian(-30.0f));
							DrawParticleDX11(pInstance, pSRV);
							pInstance->Transform(mc_pmatLocal, D3DXToRadian(+30.0f));
							DrawParticleDX11(pInstance, pSRV);
						}
					}
					else if (m_pParticleProperty->m_byBillboardType == BILLBOARD_TYPE_3FACE)
					{
						if (!m_pParticleProperty->m_bAttachFlag)
						{
							pInstance->Transform();
							DrawParticleDX11(pInstance, pSRV);
							pInstance->Transform(nullptr, D3DXToRadian(-60.0f));
							DrawParticleDX11(pInstance, pSRV);
							pInstance->Transform(nullptr, D3DXToRadian(+60.0f));
							DrawParticleDX11(pInstance, pSRV);
						}
						else
						{
							pInstance->Transform(mc_pmatLocal);
							DrawParticleDX11(pInstance, pSRV);
							pInstance->Transform(mc_pmatLocal, D3DXToRadian(-60.0f));
							DrawParticleDX11(pInstance, pSRV);
							pInstance->Transform(mc_pmatLocal, D3DXToRadian(+60.0f));
							DrawParticleDX11(pInstance, pSRV);
						}
					}
				}
			}

			ID3D11ShaderResourceView* pNullSRV = nullptr;
			pContext->PSSetShaderResources(0, 1, &pNullSRV);
			pContext->OMSetBlendState(nullptr, afBlendFactor, 0xFFFFFFFFu);
			pContext->RSSetState(pPrevRS);
			pContext->OMSetDepthStencilState(pPrevDSS, uPrevStencilRef);
			if (pPrevRS)
				pPrevRS->Release();
			if (pPrevDSS)
				pPrevDSS->Release();

			if (dwDX11DrawCount > 0)
			{
				pGrpDevice->IncrementFrameDrawCalls(dwDX11DrawCount, uDX11PrimitiveCount);

#ifdef BUILD_DEBUG_UI
				extern void ReportImGuiEffectsDrawCalls(UINT32 draws, UINT64 prims);
				ReportImGuiEffectsDrawCalls(dwDX11DrawCount, static_cast<UINT64>(uDX11PrimitiveCount));
#endif

				rMgr.AddDX11SubmittedEffectCount(dwDX11DrawCount);
				rMgr.AddDX11SubmittedParticleCount(dwDX11DrawCount);  // W4.2: telemetry split
			}

			if (iInvalidGeometrySkips > 0)
			{
				static DWORD s_dwInvalidParticleGeomTick = 0u;
				const DWORD dwNow = ELTimer_GetMSec();
				if (0u == s_dwInvalidParticleGeomTick || (dwNow - s_dwInvalidParticleGeomTick) >= 2000u)
				{
					s_dwInvalidParticleGeomTick = dwNow;
					TraceError("DX11_EFFECT_PARTICLE_SKIP reason=invalid_geometry skipped=%d", iInvalidGeometrySkips);
				}
			}
			return;
		}
	}

	if (pGrpDevice && pGrpDevice->IsValid())
	{
		static DWORD s_dwLastStrictSkipLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastStrictSkipLogMS || (dwNow - s_dwLastStrictSkipLogMS) >= 2000u)
		{
			s_dwLastStrictSkipLogMS = dwNow;
			TraceError("DX11_PARTICLE_SKIP reason=dx11_resources_not_ready_or_context_missing");
		}
		return;
	}

	// Full-DX11 migration policy: no DX9 fallback path in particle renderer.
	static DWORD s_dwLastRuntimeInactiveLogMS = 0u;
	const DWORD dwNowRuntime = ELTimer_GetMSec();
	if (0u == s_dwLastRuntimeInactiveLogMS || (dwNowRuntime - s_dwLastRuntimeInactiveLogMS) >= 3000u)
	{
		s_dwLastRuntimeInactiveLogMS = dwNowRuntime;
		TraceError("DX11_PARTICLE_SKIP reason=dx11_runtime_inactive");
	}
	return;
}

void CParticleSystemInstance::OnSetDataPointer(CEffectElementBase * pElement)
{
	m_pData = (CParticleSystemData *)pElement;

	m_dwCurrentEmissionCount = 0;
	m_pParticleProperty = m_pData->GetParticlePropertyPointer();
	m_pEmitterProperty = m_pData->GetEmitterPropertyPointer();
	m_iLoopCount = m_pEmitterProperty->GetLoopCount();
	m_ParticleInstanceListVector.resize(m_pParticleProperty->GetTextureAnimationFrameCount()*2+2);

	/////

	assert(m_kVct_pkImgInst.empty());
	m_kVct_pkImgInst.reserve(m_pParticleProperty->m_ImageVector.size());
	for (DWORD i = 0; i < m_pParticleProperty->m_ImageVector.size(); ++i)
	{
		CGraphicImage * pImage = m_pParticleProperty->m_ImageVector[i];

		CGraphicImageInstance* pkImgInstNew = CGraphicImageInstance::New();
		pkImgInstNew->SetImagePointer(pImage);
		m_kVct_pkImgInst.push_back(pkImgInstNew);
	}
}

void CParticleSystemInstance::OnInitialize()
{
	m_dwCurrentEmissionCount = 0;
	m_iLoopCount = 0;
	m_fEmissionResidue = 0.0f;
}

void CParticleSystemInstance::OnDestroy()
{
	// 2004. 3. 1. myevan. 파티클 제거 루틴
	TParticleInstanceListVector::iterator i;
	for(i = m_ParticleInstanceListVector.begin(); i!=m_ParticleInstanceListVector.end(); ++i)
	{
		TParticleInstanceList& rkLst_kParticleInst=*i;

		TParticleInstanceList::iterator j;
		for(j = rkLst_kParticleInst.begin(); j!=rkLst_kParticleInst.end(); ++j)
		{
			CParticleInstance* pkParticleInst=*j;
			pkParticleInst->DeleteThis();
		}

		rkLst_kParticleInst.clear();	
	}
	m_ParticleInstanceListVector.clear();

	std::for_each(m_kVct_pkImgInst.begin(), m_kVct_pkImgInst.end(), CGraphicImageInstance::Delete);
	m_kVct_pkImgInst.clear();
}

CParticleSystemInstance::CParticleSystemInstance()
{
	Initialize();
}

CParticleSystemInstance::~CParticleSystemInstance()
{
	assert(m_ParticleInstanceListVector.empty());
	assert(m_kVct_pkImgInst.empty());
}
