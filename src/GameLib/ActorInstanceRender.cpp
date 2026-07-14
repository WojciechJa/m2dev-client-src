#include "StdAfx.h"
#include "EterLib/GrpDeviceDX11.h"
#include "EterLib/Camera.h"

#include "ActorInstance.h"

namespace
{
inline void AccumulateDX11ActorSubmitTelemetry(DWORD dwActorInstances, DWORD dwSubmittedDraws)
{
	static DWORD s_dwActorInstances = 0;
	static DWORD s_dwSubmittedDraws = 0;
	static DWORD s_dwLastLogTime = 0;

	s_dwActorInstances += dwActorInstances;
	s_dwSubmittedDraws += dwSubmittedDraws;

	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwLastLogTime < 5000)
		return;

	const float fDrawsPerActor = (s_dwActorInstances > 0)
		? (static_cast<float>(s_dwSubmittedDraws) / static_cast<float>(s_dwActorInstances))
		: 0.0f;
	TraceError("DX11_PIPELINE_SUBMIT_PARITY pass=actor_strict actors=%u submitted=%u draws_per_actor=%.2f",
		s_dwActorInstances,
		s_dwSubmittedDraws,
		fDrawsPerActor);
	s_dwActorInstances = 0;
	s_dwSubmittedDraws = 0;
	s_dwLastLogTime = dwNow;
}

inline void TraceDX11ActorStateParity(int iRenderMode, float fAlphaValue)
{
	static DWORD s_dwLastLogTime = 0;
	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwLastLogTime < 5000)
		return;

	const char* c_szBlendMode = "opaque";
	if (iRenderMode == CActorInstance::RENDER_MODE_BLEND)
		c_szBlendMode = (fAlphaValue < 1.0f) ? "alpha_blend" : "opaque_plus_alpha";
	else if (iRenderMode == CActorInstance::RENDER_MODE_ADD)
		c_szBlendMode = "additive";
	else if (iRenderMode == CActorInstance::RENDER_MODE_MODULATE)
		c_szBlendMode = "modulate";

	TraceError("DX11_PIPELINE_STATE_PARITY pass=actor_strict path=dx11_native render_mode=%d blend=%s depth=enabled", iRenderMode, c_szBlendMode);
	s_dwLastLogTime = dwNow;
}
}
bool CActorInstance::ms_isDirLine=false;

bool CActorInstance::IsDirLine()
{
	return ms_isDirLine;
}

void CActorInstance::ShowDirectionLine(bool isVisible)
{
	ms_isDirLine=isVisible;
}

void CActorInstance::SetMaterialColor(DWORD dwColor)
{
	if (m_pkHorse)
		m_pkHorse->SetMaterialColor(dwColor);

	m_dwMtrlColor&=0xff000000;
	m_dwMtrlColor|=(dwColor&0x00ffffff);
}

void CActorInstance::SetMaterialAlpha(DWORD dwAlpha)
{
	m_dwMtrlAlpha=dwAlpha;	
}


void CActorInstance::OnRender()
{
	if (!m_pkCurRaceData)
		return;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!(pDX11Device && pDX11Device->IsValid() && pDX11Device->GetContext()))
	{
		static bool s_bLoggedActorRenderNoBackend = false;
		if (!s_bLoggedActorRenderNoBackend)
		{
			s_bLoggedActorRenderNoBackend = true;
			TraceError("DX11_ACTOR_RENDER_FAIL reason=dx11_context_unavailable");
		}
		return;
	}

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	const DWORD dwSubmittedBefore = CGraphicThingInstance::GetDX11SubmittedDrawCount();
	CCamera* pCamera = CCameraManager::instance().GetCurrentCamera();
	DirectX::SimpleMath::Matrix matViewProj = (pCamera ? pCamera->GetViewMatrix() : ms_matView) * ms_matProj;

	// Keep actor pass in world depth-tested path; disabling depth here causes
	// unstable overdraw artifacts and incorrect scene compositing.
	if (ID3D11DepthStencilState* pDepthReadState = pDX11Device->GetBootstrapUIDepthReadState())
		pContext->OMSetDepthStencilState(pDepthReadState, 0u);
	else
		pContext->OMSetDepthStencilState(nullptr, 0u);
	TraceDX11ActorStateParity(m_iRenderMode, m_fAlphaValue);

	ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11BlendState* pAdditiveBlendState = pDX11Device->GetBootstrapUIAdditiveBlendState();
	ID3D11BlendState* pModulateBlendState = pDX11Device->GetBootstrapUIModulateBlendState();
	const FLOAT afOpaqueBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };

	switch(m_iRenderMode)
	{
	case RENDER_MODE_NORMAL:
		pContext->OMSetBlendState(nullptr, afOpaqueBlendFactor, 0xffffffffu);
		RenderWithOneTextureDX11(pContext, matViewProj);
		break;

	case RENDER_MODE_BLEND:
		if (m_fAlphaValue == 1.0f)
		{
			pContext->OMSetBlendState(nullptr, afOpaqueBlendFactor, 0xffffffffu);
			RenderWithOneTextureDX11(pContext, matViewProj);
		}
		else if (m_fAlphaValue > 0.0f)
		{
			if (pAlphaBlendState)
				pContext->OMSetBlendState(pAlphaBlendState, afOpaqueBlendFactor, 0xffffffffu);
			RenderWithOneTextureDX11(pContext, matViewProj);
		}
		break;

	case RENDER_MODE_ADD:
		if (pAdditiveBlendState)
			pContext->OMSetBlendState(pAdditiveBlendState, afOpaqueBlendFactor, 0xffffffffu);
		else if (pAlphaBlendState)
			pContext->OMSetBlendState(pAlphaBlendState, afOpaqueBlendFactor, 0xffffffffu);
		RenderWithOneTextureDX11(pContext, matViewProj);
		break;

	case RENDER_MODE_MODULATE:
		if (pModulateBlendState)
			pContext->OMSetBlendState(pModulateBlendState, afOpaqueBlendFactor, 0xffffffffu);
		else if (pAlphaBlendState)
			pContext->OMSetBlendState(pAlphaBlendState, afOpaqueBlendFactor, 0xffffffffu);
		RenderWithOneTextureDX11(pContext, matViewProj);
		break;
	}

	pContext->OMSetBlendState(nullptr, afOpaqueBlendFactor, 0xffffffffu);
	const DWORD dwSubmittedAfter = CGraphicThingInstance::GetDX11SubmittedDrawCount();
	const DWORD dwSubmittedDelta = (dwSubmittedAfter >= dwSubmittedBefore) ? (dwSubmittedAfter - dwSubmittedBefore) : 0;
	AccumulateDX11ActorSubmitTelemetry(1u, dwSubmittedDelta);

	if (ms_isDirLine)
	{
		D3DXVECTOR3 kD3DVt3Cur(m_x, m_y, m_z);

		D3DXVECTOR3 kD3DVt3LookDir(0.0f, -1.0f, 0.0f);
		D3DXMATRIX kD3DMatLook;
		D3DXMatrixRotationZ(&kD3DMatLook, D3DXToRadian(GetRotation()));
		D3DXVec3TransformCoord(&kD3DVt3LookDir, &kD3DVt3LookDir, &kD3DMatLook);
		D3DXVec3Scale(&kD3DVt3LookDir, &kD3DVt3LookDir, 200.0f);
		D3DXVec3Add(&kD3DVt3LookDir, &kD3DVt3LookDir, &kD3DVt3Cur);

		D3DXVECTOR3 kD3DVt3AdvDir(0.0f, -1.0f, 0.0f);
		D3DXMATRIX kD3DMatAdv;
		D3DXMatrixRotationZ(&kD3DMatAdv, D3DXToRadian(GetAdvancingRotation()));
		D3DXVec3TransformCoord(&kD3DVt3AdvDir, &kD3DVt3AdvDir, &kD3DMatAdv);
		D3DXVec3Scale(&kD3DVt3AdvDir, &kD3DVt3AdvDir, 200.0f);
		D3DXVec3Add(&kD3DVt3AdvDir, &kD3DVt3AdvDir, &kD3DVt3Cur);

		static CScreen s_kScreen;
		s_kScreen.SetDiffuseColor(1.0f, 1.0f, 0.0f);
		s_kScreen.RenderLine3d(kD3DVt3Cur.x, kD3DVt3Cur.y, kD3DVt3Cur.z, kD3DVt3AdvDir.x, kD3DVt3AdvDir.y, kD3DVt3AdvDir.z);

		s_kScreen.SetDiffuseColor(0.0f, 1.0f, 1.0f);
		s_kScreen.RenderLine3d(kD3DVt3Cur.x, kD3DVt3Cur.y, kD3DVt3Cur.z, kD3DVt3LookDir.x, kD3DVt3LookDir.y, kD3DVt3LookDir.z);
	}
}
void CActorInstance::BeginDiffuseRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::EndDiffuseRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::BeginOpacityRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::EndOpacityRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::BeginBlendRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::EndBlendRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::BeginAddRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::EndAddRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::RestoreRenderMode()
{
	// NOTE : This is temporary code. I wanna convert this code to that restore the mode to
	//        model's default setting which had has as like specular or normal. - [levites]
	m_iRenderMode = RENDER_MODE_NORMAL;
	if (m_kBlendAlpha.m_isBlending)
	{
		m_kBlendAlpha.m_iOldRenderMode = m_iRenderMode;
	}
}


void CActorInstance::SetAddRenderMode()
{
	m_iRenderMode = RENDER_MODE_ADD;
	if (m_kBlendAlpha.m_isBlending)
	{
		m_kBlendAlpha.m_iOldRenderMode = m_iRenderMode;
	}
}

void CActorInstance::SetRenderMode(int iRenderMode)
{
	m_iRenderMode = iRenderMode;
	if (m_kBlendAlpha.m_isBlending)
	{
		m_kBlendAlpha.m_iOldRenderMode = iRenderMode;
	}
}

void CActorInstance::SetAddColor(const D3DXCOLOR & c_rColor)
{
	m_AddColor = c_rColor;
	m_AddColor.a = 1.0f;
}

void CActorInstance::BeginModulateRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::EndModulateRender()
{
	// Legacy DX9 fixed-function path removed.
}
void CActorInstance::SetModulateRenderMode()
{
	m_iRenderMode = RENDER_MODE_MODULATE;
	if (m_kBlendAlpha.m_isBlending)
	{
		m_kBlendAlpha.m_iOldRenderMode = m_iRenderMode;
	}
}

void CActorInstance::RenderCollisionData()
{
	static CScreen s_Screen;

	if (m_pAttributeInstance)
	{
		for (DWORD col=0; col < GetCollisionInstanceCount(); ++col)
		{
			CBaseCollisionInstance * pInstance = GetCollisionInstanceData(col);
			pInstance->Render();
		}
	}

	s_Screen.SetColorOperation();
	s_Screen.SetDiffuseColor(1.0f, 0.0f, 0.0f);
	TCollisionPointInstanceList::iterator itor;
	s_Screen.SetDiffuseColor(1.0f, (isShow())?1.0f:0.0f, 0.0f);
	D3DXVECTOR3 center;
	float r;
	GetBoundingSphere(center,r);
	s_Screen.RenderCircle3d(center.x,center.y,center.z,r);

	s_Screen.SetDiffuseColor(0.0f, 0.0f, 1.0f);
	itor = m_DefendingPointInstanceList.begin();
	for (; itor != m_DefendingPointInstanceList.end(); ++itor)
	{
		const TCollisionPointInstance & c_rInstance = *itor;
		for (DWORD i = 0; i < c_rInstance.SphereInstanceVector.size(); ++i)
		{
			const CDynamicSphereInstance & c_rSphereInstance = c_rInstance.SphereInstanceVector[i];
			s_Screen.RenderCircle3d(c_rSphereInstance.v3Position.x,
									c_rSphereInstance.v3Position.y,
									c_rSphereInstance.v3Position.z,
									c_rSphereInstance.fRadius);
		}
	}

	s_Screen.SetDiffuseColor(0.0f, 1.0f, 0.0f);
	itor = m_BodyPointInstanceList.begin();
	for (; itor != m_BodyPointInstanceList.end(); ++itor)
	{
		const TCollisionPointInstance & c_rInstance = *itor;
		for (DWORD i = 0; i < c_rInstance.SphereInstanceVector.size(); ++i)
		{
			const CDynamicSphereInstance & c_rSphereInstance = c_rInstance.SphereInstanceVector[i];
			s_Screen.RenderCircle3d(c_rSphereInstance.v3Position.x,
									c_rSphereInstance.v3Position.y,
									c_rSphereInstance.v3Position.z,
									c_rSphereInstance.fRadius);
		}
	}

	s_Screen.SetDiffuseColor(1.0f, 0.0f, 0.0f);
	{
		CDynamicSphereInstanceVector::iterator itor = m_kSplashArea.SphereInstanceVector.begin();
		for (; itor != m_kSplashArea.SphereInstanceVector.end(); ++itor)
		{
			const CDynamicSphereInstance & c_rInstance = *itor;
			s_Screen.RenderCircle3d(c_rInstance.v3Position.x,
									c_rInstance.v3Position.y,
									c_rInstance.v3Position.z,
									c_rInstance.fRadius);
		}
	}
}
void CActorInstance::RenderToShadowMap()
{
	if (RENDER_MODE_BLEND == m_iRenderMode)
	if (GetAlphaValue() < 0.5f)
		return;

	CGraphicThingInstance::RenderToShadowMap();

	if (m_pkHorse)
		m_pkHorse->RenderToShadowMap();
}

