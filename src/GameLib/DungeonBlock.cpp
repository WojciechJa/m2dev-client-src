#include "StdAfx.h"
#include "DungeonBlock.h"

#include "EterLib/GrpBase.h"
#include "EterLib/GrpDeviceDX11.h"

namespace
{
inline void LogDungeonBlockDeferredOnce(const char* c_szPass, const char* c_szReason)
{
	static std::set<std::string> s_logged;
	if (!c_szPass)
		c_szPass = "unknown";

	if (s_logged.insert(c_szPass).second)
	{
		TraceError("DX11_DUNGEON_BLOCK_PATH pass=%s status=deferred reason=%s",
			c_szPass,
			c_szReason ? c_szReason : "unknown");
	}
}
}

class CDungeonModelInstance : public CGrannyModelInstance
{
	public:
		CDungeonModelInstance() = default;
		virtual ~CDungeonModelInstance() = default;

		void RenderDungeonBlock()
		{
			if (IsEmpty())
				return;

			CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
			if (!pDX11Device || !pDX11Device->IsValid())
				return;

			ID3D11DeviceContext* pContext = pDX11Device->GetContext();
			if (!pContext)
				return;

			const D3DXMATRIX matViewProj = CGraphicBase::GetViewMatrix() * CGraphicBase::GetProjMatrix();
			RenderWithTwoTextureDX11(pContext, matViewProj);
		}

		void RenderDungeonBlockShadow()
		{
			// Keep shadow submission functional in DX11 by reusing runtime model draw path.
			// Shadow-specific shader selection is handled by outer pass state.
			RenderDungeonBlock();
		}
};


struct FUpdate
{
	float fElapsedTime;
	D3DXMATRIX * pmatWorld;
	void operator() (CGrannyModelInstance * pInstance)
	{
		pInstance->Update(CGrannyModelInstance::ANIFPS_MIN);
		pInstance->UpdateLocalTime(fElapsedTime);
		pInstance->Deform(pmatWorld);
	}
};

void CDungeonBlock::Update()
{
	Transform();

	FUpdate Update;
	Update.fElapsedTime = 0.0f;
	Update.pmatWorld = &m_worldMatrix;
	for_each(m_ModelInstanceContainer.begin(), m_ModelInstanceContainer.end(), Update);
}

struct FRenderDX11
{
	ID3D11DeviceContext* pContext;
	const D3DXMATRIX& rmatViewProj;

	FRenderDX11(ID3D11DeviceContext* pCtx, const D3DXMATRIX& matViewProj)
		: pContext(pCtx)
		, rmatViewProj(matViewProj)
	{
	}

	void operator() (CDungeonModelInstance* pInstance)
	{
		if (!pInstance || !pContext)
			return;

		pInstance->RenderWithTwoTextureDX11(pContext, rmatViewProj);
	}
};

void CDungeonBlock::Render()
{
//	if (!isShow())
//		return;
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		LogDungeonBlockDeferredOnce("dungeon_block_render", "dx11_device_unavailable");
		return;
	}

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pContext)
	{
		LogDungeonBlockDeferredOnce("dungeon_block_render", "dx11_context_unavailable");
		return;
	}

	const D3DXMATRIX matViewProj = CGraphicBase::GetViewMatrix() * CGraphicBase::GetProjMatrix();
	for_each(m_ModelInstanceContainer.begin(), m_ModelInstanceContainer.end(), FRenderDX11(pContext, matViewProj));
}

void CDungeonBlock::RenderDX11(ID3D11DeviceContext* pContext, const D3DXMATRIX& matViewProj)
{
	if (!pContext)
		return;

	for_each(m_ModelInstanceContainer.begin(), m_ModelInstanceContainer.end(), FRenderDX11(pContext, matViewProj));
}

void CDungeonBlock::OnRender()
{
	Render();
}

void CDungeonBlock::OnBlendRender()
{
	Render();
}

void CDungeonBlock::OnRenderToShadowMap()
{
	OnRenderShadow();
}

void CDungeonBlock::OnRenderPCBlocker()
{
	Render();
}

struct FRenderShadow
{
	void operator() (CDungeonModelInstance * pInstance)
	{
		pInstance->RenderDungeonBlockShadow();
	}
};

void CDungeonBlock::OnRenderShadow()
{
	for_each(m_ModelInstanceContainer.begin(), m_ModelInstanceContainer.end(), FRenderShadow());
}

bool CDungeonBlock::GetBoundingSphere(D3DXVECTOR3 & v3Center, float & fRadius)
{
	v3Center = m_v3Center;
	fRadius = m_fRadius;
	D3DXVec3TransformCoord(&v3Center, &v3Center, &GetTransform());
	return true;
}

void CDungeonBlock::OnUpdateCollisionData(const CStaticCollisionDataVector * pscdVector)
{
	assert(pscdVector);
	CStaticCollisionDataVector::const_iterator it;
	for(it = pscdVector->begin();it!=pscdVector->end();++it)
	{
		AddCollision(&(*it),&GetTransform());
	}
}

void CDungeonBlock::OnUpdateHeighInstance(CAttributeInstance * pAttributeInstance)
{
	assert(pAttributeInstance);
	SetHeightInstance(pAttributeInstance);	
}

bool CDungeonBlock::OnGetObjectHeight(float fX, float fY, float * pfHeight)
{
	if (m_pHeightAttributeInstance && m_pHeightAttributeInstance->GetHeight(fX, fY, pfHeight))
		return true;
	return false;
}

void CDungeonBlock::BuildBoundingSphere()
{
	if (m_ModelInstanceContainer.empty())
	{
		m_v3Center = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
		m_fRadius = 0.0f;
		return;
	}

	D3DXVECTOR3 v3Min, v3Max;
	GetBoundBox(&v3Min, &v3Max);

	m_v3Center = (v3Min + v3Max) * 0.5f;
	const auto vv = (v3Max - v3Min);
	m_fRadius = D3DXVec3Length(&vv) * 0.5f + 150.0f; // extra length for attached objects
}

bool CDungeonBlock::Intersect(float * pfu, float * pfv, float * pft)
{
	TModelInstanceContainer::iterator itor = m_ModelInstanceContainer.begin();
	for (; itor != m_ModelInstanceContainer.end(); ++itor)
	{
		CDungeonModelInstance * pInstance = *itor;
		if (pInstance->Intersect(&CGraphicObjectInstance::GetTransform(), pfu, pfv, pft))
			return true;
	}

	return false;
}

void CDungeonBlock::GetBoundBox(D3DXVECTOR3 * pv3Min, D3DXVECTOR3 * pv3Max)
{
	pv3Min->x = +10000000.0f;
	pv3Min->y = +10000000.0f;
	pv3Min->z = +10000000.0f;
	pv3Max->x = -10000000.0f;
	pv3Max->y = -10000000.0f;
	pv3Max->z = -10000000.0f;

	TModelInstanceContainer::iterator itor = m_ModelInstanceContainer.begin();
	for (; itor != m_ModelInstanceContainer.end(); ++itor)
	{
		CDungeonModelInstance * pInstance = *itor;

		D3DXVECTOR3 v3Min;
		D3DXVECTOR3 v3Max;
		pInstance->GetBoundBox(&v3Min, &v3Max);

		pv3Min->x = std::min(v3Min.x, pv3Min->x);
		pv3Min->y = std::min(v3Min.y, pv3Min->y);
		pv3Min->z = std::min(v3Min.z, pv3Min->z);
		pv3Max->x = std::max(v3Max.x, pv3Max->x);
		pv3Max->y = std::max(v3Max.y, pv3Max->y);
		pv3Max->z = std::max(v3Max.z, pv3Max->z);
	}
}

bool CDungeonBlock::Load(const char * c_szFileName)
{
	Destroy();

	m_pThing = (CGraphicThing *)CResourceManager::Instance().GetResourcePointer(c_szFileName);

	m_pThing->AddReference();
	if (m_pThing->GetModelCount() <= 0)
	{
		TraceError("CDungeonBlock::Load(filename=%s) - model count is %d\n", c_szFileName, m_pThing->GetModelCount());
		return false;
	}

	m_ModelInstanceContainer.reserve(m_pThing->GetModelCount());

	for (int i = 0; i < m_pThing->GetModelCount(); ++i)
	{
		CDungeonModelInstance * pModelInstance = new CDungeonModelInstance;
		pModelInstance->SetMainModelPointer(m_pThing->GetModelPointer(i), &m_kDeformableVertexBuffer);
		DWORD dwVertexCount = pModelInstance->GetVertexCount();
		m_kDeformableVertexBuffer.Destroy();
		m_kDeformableVertexBuffer.Create(
			dwVertexCount,
			FVF_XYZ|FVF_NORMAL|FVF_TEX1,
			GRP_USAGE_DYNAMIC,
			GRP_POOL_DEFAULT);	
		m_ModelInstanceContainer.push_back(pModelInstance);
	}

	return true;
}

void CDungeonBlock::__Initialize()
{
	m_v3Center = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	m_fRadius = 0.0f;

	m_pThing = NULL;
}

void CDungeonBlock::Destroy()
{
	if (m_pThing)
	{
		m_pThing->Release();
		m_pThing = NULL;
	}

	stl_wipe(m_ModelInstanceContainer);

	__Initialize();
}

CDungeonBlock::CDungeonBlock()
{
	__Initialize();
}
CDungeonBlock::~CDungeonBlock()
{
	Destroy();
}
