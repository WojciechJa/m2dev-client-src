#include "StdAfx.h"
#include "Material.h"
#include "Mesh.h"
#include "Eterbase/Filename.h"
#include "Eterlib/ResourceManager.h"
#include "Eterlib/StateManager.h"
#include "Eterlib/GrpScreen.h"
#include "Eterlib/GrpDeviceDX11.h"

CGraphicImageInstance CGrannyMaterial::ms_akSphereMapInstance[SPHEREMAP_NUM];

D3DXVECTOR3	CGrannyMaterial::ms_v3SpecularTrans(0.0f, 0.0f, 0.0f);
D3DXMATRIX	CGrannyMaterial::ms_matSpecular;

D3DXCOLOR g_fSpecularColor = D3DXCOLOR(0.0f, 0.0f, 0.0f, 0.0f);

namespace
{
	ID3D11RasterizerState* g_pDX11MaterialPassDefaultRasterState = nullptr;

	ID3D11RasterizerState* CreateTwoSidedRasterStateFromCurrent(ID3D11DeviceContext* pContext, ID3D11RasterizerState* pCurrentState)
	{
		if (!pContext)
			return nullptr;

		D3D11_RASTERIZER_DESC kDesc;
		ZeroMemory(&kDesc, sizeof(kDesc));
		if (pCurrentState)
		{
			pCurrentState->GetDesc(&kDesc);
		}
		else
		{
			kDesc.FillMode = D3D11_FILL_SOLID;
			kDesc.CullMode = D3D11_CULL_BACK;
			kDesc.FrontCounterClockwise = FALSE;
			kDesc.DepthBias = 0;
			kDesc.DepthBiasClamp = 0.0f;
			kDesc.SlopeScaledDepthBias = 0.0f;
			kDesc.DepthClipEnable = TRUE;
			kDesc.ScissorEnable = FALSE;
			kDesc.MultisampleEnable = FALSE;
			kDesc.AntialiasedLineEnable = FALSE;
		}

		kDesc.CullMode = D3D11_CULL_NONE;

		ID3D11Device* pDevice = nullptr;
		pContext->GetDevice(&pDevice);
		if (!pDevice)
			return nullptr;

		ID3D11RasterizerState* pTwoSidedState = nullptr;
		const HRESULT hr = pDevice->CreateRasterizerState(&kDesc, &pTwoSidedState);
		pDevice->Release();
		if (FAILED(hr))
			return nullptr;
		return pTwoSidedState;
	}

	ID3D11RasterizerState* GetOrCreateDX11MaterialPassDefaultRasterState(ID3D11DeviceContext* pContext)
	{
		if (g_pDX11MaterialPassDefaultRasterState)
			return g_pDX11MaterialPassDefaultRasterState;

		if (!pContext)
			return nullptr;

		ID3D11Device* pDevice = nullptr;
		pContext->GetDevice(&pDevice);
		if (!pDevice)
			return nullptr;

		D3D11_RASTERIZER_DESC kDesc;
		ZeroMemory(&kDesc, sizeof(kDesc));
		kDesc.FillMode = D3D11_FILL_SOLID;
		// Pass default parity for object/character rendering (GRP_CULL_CW -> front cull in DX11 with FrontCCW=FALSE).
		kDesc.CullMode = D3D11_CULL_FRONT;
		kDesc.FrontCounterClockwise = FALSE;
		kDesc.DepthBias = 0;
		kDesc.DepthBiasClamp = 0.0f;
		kDesc.SlopeScaledDepthBias = 0.0f;
		kDesc.DepthClipEnable = TRUE;
		kDesc.ScissorEnable = FALSE;
		kDesc.MultisampleEnable = FALSE;
		kDesc.AntialiasedLineEnable = FALSE;

		const HRESULT hr = pDevice->CreateRasterizerState(&kDesc, &g_pDX11MaterialPassDefaultRasterState);
		pDevice->Release();
		if (FAILED(hr))
			return nullptr;

		return g_pDX11MaterialPassDefaultRasterState;
	}
}

void CGrannyMaterial::TranslateSpecularMatrix(float fAddX, float fAddY, float fAddZ)
{
	static float SPECULAR_TRANSLATE_MAX = 1000000.0f;

	ms_v3SpecularTrans.x+=fAddX;
	ms_v3SpecularTrans.y+=fAddY;
	ms_v3SpecularTrans.z+=fAddZ;

	if (ms_v3SpecularTrans.x>=SPECULAR_TRANSLATE_MAX)
		ms_v3SpecularTrans.x=0.0f;

	if (ms_v3SpecularTrans.y>=SPECULAR_TRANSLATE_MAX)
		ms_v3SpecularTrans.y=0.0f;

	if (ms_v3SpecularTrans.z>=SPECULAR_TRANSLATE_MAX)
		ms_v3SpecularTrans.z=0.0f;

	D3DXMatrixTranslation(&ms_matSpecular, 
		ms_v3SpecularTrans.x, 
		ms_v3SpecularTrans.y, 
		ms_v3SpecularTrans.z
	);
}

void CGrannyMaterial::ApplyRenderState()
{
	assert(m_pfnApplyRenderState!=NULL && "CGrannyMaterial::SaveRenderState");
	(this->*m_pfnApplyRenderState)();
}

void CGrannyMaterial::RestoreRenderState()
{
	assert(m_pfnRestoreRenderState!=NULL && "CGrannyMaterial::RestoreRenderState");
	(this->*m_pfnRestoreRenderState)();
}

void CGrannyMaterial::Copy(CGrannyMaterial& rkMtrl)
{
	m_pgrnMaterial = rkMtrl.m_pgrnMaterial;
	m_roImage[0] =  rkMtrl.m_roImage[0];
	m_roImage[1] =  rkMtrl.m_roImage[1];
    m_eType = rkMtrl.m_eType;
	m_bTwoSideRender = rkMtrl.m_bTwoSideRender;
	SetSpecularInfo(rkMtrl.m_bSpecularEnable, rkMtrl.m_fSpecularPower, rkMtrl.m_bSphereMapIndex);
}

CGrannyMaterial::CGrannyMaterial()
{
	m_bTwoSideRender = false;
	m_dwLastCullRenderStateForTwoSideRendering = GRP_CULL_CW;
	m_bDX11AppliedCullNone = false;
	m_pDX11TwoSidedRasterState = nullptr;

	Initialize();
}

CGrannyMaterial::~CGrannyMaterial()
{
	if (m_pDX11TwoSidedRasterState)
	{
		m_pDX11TwoSidedRasterState->Release();
		m_pDX11TwoSidedRasterState = nullptr;
	}
}

CGrannyMaterial::EType CGrannyMaterial::GetType() const
{
	return m_eType;
}

void CGrannyMaterial::SetImagePointer(int iStage, CGraphicImage* pImage)
{	
	assert(iStage<2 && "CGrannyMaterial::SetImagePointer");
	m_roImage[iStage]=pImage;
}

bool CGrannyMaterial::IsIn(const char* c_szImageName, int* piStage)
{
	std::string strImageName = c_szImageName;
	CFileNameHelper::StringPath(strImageName);

	granny_texture * pgrnDiffuseTexture = GrannyGetMaterialTextureByType(m_pgrnMaterial, GrannyDiffuseColorTexture);
	if (pgrnDiffuseTexture)
	{
		std::string strDiffuseFileName = pgrnDiffuseTexture->FromFileName;
		CFileNameHelper::StringPath(strDiffuseFileName);
		if (strDiffuseFileName == strImageName)
		{
			*piStage=0;
			return true;
		}
	}

    granny_texture * pgrnOpacityTexture = GrannyGetMaterialTextureByType(m_pgrnMaterial, GrannyOpacityTexture);
	if (pgrnOpacityTexture)
	{
		std::string strOpacityFileName = pgrnOpacityTexture->FromFileName;
		CFileNameHelper::StringPath(strOpacityFileName);
		if (strOpacityFileName == strImageName)
		{
			*piStage=1;
			return true;
		}
	}

	return false;
}

void CGrannyMaterial::SetSpecularInfo(BOOL bFlag, float fPower, BYTE uSphereMapIndex)
{
	m_fSpecularPower = fPower;
	m_bSphereMapIndex = uSphereMapIndex;
	m_bSpecularEnable = bFlag;	

	if (bFlag)
	{
		m_pfnApplyRenderState = &CGrannyMaterial::__ApplySpecularRenderState;
		m_pfnRestoreRenderState = &CGrannyMaterial::__RestoreSpecularRenderState;
	}
	else
	{
		m_pfnApplyRenderState = &CGrannyMaterial::__ApplyDiffuseRenderState;
		m_pfnRestoreRenderState = &CGrannyMaterial::__RestoreDiffuseRenderState;
	}
}

bool CGrannyMaterial::IsEqual(granny_material* pgrnMaterial) const
{
	if (m_pgrnMaterial==pgrnMaterial)
		return true;

	return false;
}


CGraphicImage * CGrannyMaterial::GetImagePointer(int iStage) const
{
	const CGraphicImage::TRef & ratImage = m_roImage[iStage];

	if (ratImage.IsNull())
		return NULL;

	CGraphicImage * pImage = ratImage.GetPointer();
	return pImage;
}

const CGraphicTexture* CGrannyMaterial::GetDiffuseTexture() const
{
	if (m_roImage[0].IsNull())
		return NULL;

	return m_roImage[0].GetPointer()->GetTexturePointer();
}

const CGraphicTexture* CGrannyMaterial::GetOpacityTexture() const
{
	if (m_roImage[1].IsNull())
		return NULL;

	return m_roImage[1].GetPointer()->GetTexturePointer();
}

BOOL CGrannyMaterial::__IsSpecularEnable() const
{
	return m_bSpecularEnable;
}

// MR-12: Fix specular isolation issue
float CGrannyMaterial::GetSpecularPower() const
{
	return m_fSpecularPower;
}
// MR-12: -- END OF -- Fix specular isolation issue

extern const std::string& GetModelLocalPath();

CGraphicImage* CGrannyMaterial::__GetImagePointer(const char* fileName)
{
	assert(*fileName != '\0');

	CResourceManager& rkResMgr = CResourceManager::Instance();

	// SUPPORT_LOCAL_TEXTURE
	int fileName_len = strlen(fileName);
	if (fileName_len > 2 && fileName[1] != ':')
	{
		char localFileName[256];		
		const std::string& modelLocalPath = GetModelLocalPath();

		int localFileName_len = modelLocalPath.length() + 1 + fileName_len;
		if (localFileName_len < sizeof(localFileName) - 1)
		{
			_snprintf(localFileName, sizeof(localFileName), "%s%s", GetModelLocalPath().c_str(), fileName);
			CResource* pResource = rkResMgr.GetResourcePointer(localFileName);
			return static_cast<CGraphicImage*>(pResource);
		}		
	}
	// END_OF_SUPPORT_LOCAL_TEXTURE
	

	CResource* pResource = rkResMgr.GetResourcePointer(fileName);
	return static_cast<CGraphicImage*>(pResource);
}

bool CGrannyMaterial::CreateFromGrannyMaterialPointer(granny_material * pgrnMaterial)
{
	m_pgrnMaterial = pgrnMaterial;

	granny_texture * pgrnDiffuseTexture = NULL;
	granny_texture * pgrnOpacityTexture = NULL;

	if (pgrnMaterial)
	{
		if (pgrnMaterial->MapCount > 1 && !_strnicmp(pgrnMaterial->Name, "Blend", 5))
		{
			pgrnDiffuseTexture = GrannyGetMaterialTextureByType(pgrnMaterial->Maps[0].Material, GrannyDiffuseColorTexture);
			pgrnOpacityTexture = GrannyGetMaterialTextureByType(pgrnMaterial->Maps[1].Material, GrannyDiffuseColorTexture);
		}
		else
		{
			pgrnDiffuseTexture = GrannyGetMaterialTextureByType(m_pgrnMaterial, GrannyDiffuseColorTexture);
			pgrnOpacityTexture = GrannyGetMaterialTextureByType(m_pgrnMaterial, GrannyOpacityTexture);
		}

		// Two-Side Ã«Â Å’Ã«Ââ€Ã«Â§ÂÃ¬ÂÂ´ Ã­â€¢â€žÃ¬Å¡â€Ã­â€¢Å“ Ã¬Â§â‚¬ ÃªÂ²â‚¬Ã¬â€šÂ¬
		{			
			granny_int32 twoSided = 0;
			granny_data_type_definition TwoSidedFieldType[] =
			{
				{GrannyInt32Member, "Two-sided"},
				{GrannyEndMember},
			};

			granny_variant twoSideResult;

			if (GrannyFindMatchingMember(pgrnMaterial->ExtendedData.Type, pgrnMaterial->ExtendedData.Object, "Two-sided", &twoSideResult)  && NULL != twoSideResult.Type)
				GrannyConvertSingleObject(twoSideResult.Type, twoSideResult.Object, TwoSidedFieldType, &twoSided, NULL);

			m_bTwoSideRender = 1 == twoSided;
		}
	}

	if (pgrnDiffuseTexture)
		m_roImage[0].SetPointer(__GetImagePointer(pgrnDiffuseTexture->FromFileName));

	if (pgrnOpacityTexture)
		m_roImage[1].SetPointer(__GetImagePointer(pgrnOpacityTexture->FromFileName));

	// Ã¬ËœÂ¤Ã­ÂÂ¼Ã¬â€¹Å“Ã­â€¹Â°ÃªÂ°â‚¬ Ã¬Å¾Ë†Ã¬Å“Â¼Ã«Â©Â´ Ã«Â¸â€Ã«Â Å’Ã«â€Â© Ã«Â©â€Ã¬â€°Â¬
	if (!m_roImage[1].IsNull())
		m_eType = TYPE_BLEND_PNT;
	else
		m_eType = TYPE_DIFFUSE_PNT;

	return true;
}

void CGrannyMaterial::Initialize()
{
	m_roImage[0] = NULL;
	m_roImage[1] = NULL;

	SetSpecularInfo(FALSE, 0.0f, 0);
}

void CGrannyMaterial::__ApplyDiffuseRenderState()
{
	if (m_bTwoSideRender)
	{
		CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
		ID3D11DeviceContext* pContext = pDX11Device ? pDX11Device->GetContext() : nullptr;
		if (pContext)
		{
			ID3D11RasterizerState* pCurrentRasterState = nullptr;
			pContext->RSGetState(&pCurrentRasterState);

			if (!m_pDX11TwoSidedRasterState)
				m_pDX11TwoSidedRasterState = CreateTwoSidedRasterStateFromCurrent(pContext, pCurrentRasterState);

			if (pCurrentRasterState)
				pCurrentRasterState->Release();

			if (m_pDX11TwoSidedRasterState)
			{
				pContext->RSSetState(m_pDX11TwoSidedRasterState);
				m_bDX11AppliedCullNone = true;
			}
			else
			{
				m_bDX11AppliedCullNone = false;
			}
		}
		else
		{
			m_bDX11AppliedCullNone = false;
		}
	}
	else
	{
		m_bDX11AppliedCullNone = false;
	}

	static DWORD s_dwLastTelemetryMS = 0;
	static DWORD s_dwDiffuseCount = 0;
	static DWORD s_dwTwoSidedCount = 0;
	++s_dwDiffuseCount;
	if (m_bTwoSideRender)
		++s_dwTwoSidedCount;

	const DWORD dwNow = GetTickCount();
	if (0u == s_dwLastTelemetryMS || (dwNow - s_dwLastTelemetryMS) >= 30000u)
	{
		s_dwLastTelemetryMS = dwNow;
		if (s_dwDiffuseCount > 0u)
		{
			TraceError("DX11_EGRN_MATERIAL_STRICT mode=dx11 active=diffuse total=%u two_sided=%u interval_ms=30000",
				s_dwDiffuseCount, s_dwTwoSidedCount);
			s_dwDiffuseCount = 0u;
			s_dwTwoSidedCount = 0u;
		}
	}
}

void CGrannyMaterial::__RestoreDiffuseRenderState()
{
	if (m_bDX11AppliedCullNone)
	{
		CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
		ID3D11DeviceContext* pContext = pDX11Device ? pDX11Device->GetContext() : nullptr;
		if (pContext)
		{
			if (ID3D11RasterizerState* pPassDefaultRasterState = GetOrCreateDX11MaterialPassDefaultRasterState(pContext))
				pContext->RSSetState(pPassDefaultRasterState);
		}
	}

	m_bDX11AppliedCullNone = false;
}

void CGrannyMaterial::__ApplySpecularRenderState()
{
	// Reuse diffuse state path (including two-sided handling).
	// Specular contribution is applied in DX11 object shader via material parameters.
	__ApplyDiffuseRenderState();

	// Telemetry: throttled 30s heartbeat for specular material usage tracking
	static DWORD s_dwLastSpecularTelemetryMS = 0;
	static DWORD s_dwSpecularCount = 0;
	static float s_fMaxSpecularPower = 0.0f;
	++s_dwSpecularCount;
	if (m_fSpecularPower > s_fMaxSpecularPower)
		s_fMaxSpecularPower = m_fSpecularPower;

	const DWORD dwNow = GetTickCount();
	if (0u == s_dwLastSpecularTelemetryMS || (dwNow - s_dwLastSpecularTelemetryMS) >= 30000u)
	{
		s_dwLastSpecularTelemetryMS = dwNow;
		if (s_dwSpecularCount > 0u)
		{
			TraceError("DX11_EGRN_MATERIAL_STRICT mode=dx11 active=specular_shader total=%u max_power=%.2f sphere_idx=%d interval_ms=30000",
				s_dwSpecularCount, s_fMaxSpecularPower, (int)m_bSphereMapIndex);
			s_dwSpecularCount = 0u;
			s_fMaxSpecularPower = 0.0f;
		}
	}
}

void CGrannyMaterial::__RestoreSpecularRenderState()
{
	// M3-EGRN-40: Delegate to diffuse restore (clears state tracking)
	__RestoreDiffuseRenderState();
}

void CGrannyMaterial::CreateSphereMap(UINT uMapIndex, const char* c_szSphereMapImageFileName)
{
	CResourceManager& rkResMgr = CResourceManager::Instance();
	CGraphicImage * pImage = (CGraphicImage *)rkResMgr.GetResourcePointer(c_szSphereMapImageFileName);
	ms_akSphereMapInstance[uMapIndex].SetImagePointer(pImage);
}

void CGrannyMaterial::DestroySphereMap()
{
	for (UINT uMapIndex=0; uMapIndex<SPHEREMAP_NUM; ++uMapIndex)
		ms_akSphereMapInstance[uMapIndex].Destroy();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CGrannyMaterialPalette::CGrannyMaterialPalette()
{
}

CGrannyMaterialPalette::~CGrannyMaterialPalette()
{
	Clear();
}

void CGrannyMaterialPalette::Copy(const CGrannyMaterialPalette& rkMtrlPalSrc)
{
	m_mtrlVector=rkMtrlPalSrc.m_mtrlVector;
}

void CGrannyMaterialPalette::Clear()
{
	m_mtrlVector.clear();
}

CGrannyMaterial& CGrannyMaterialPalette::GetMaterialRef(DWORD mtrlIndex)
{
	assert(mtrlIndex<m_mtrlVector.size());
	return *m_mtrlVector[mtrlIndex].GetPointer();
}

void CGrannyMaterialPalette::SetMaterialImagePointer(const char* c_szImageName, CGraphicImage* pImage)
{
	DWORD size=m_mtrlVector.size();
	DWORD i;
	for (i=0; i<size; ++i)
	{
		CGrannyMaterial::TRef& roMtrl=m_mtrlVector[i];

		int iStage;
		if (roMtrl->IsIn(c_szImageName, &iStage))
		{
			CGrannyMaterial* pkNewMtrl=new CGrannyMaterial;
			pkNewMtrl->Copy(*roMtrl.GetPointer());
			pkNewMtrl->SetImagePointer(iStage, pImage);
			roMtrl=pkNewMtrl;

			return;
		}
	}
}

void CGrannyMaterialPalette::SetMaterialData(const char* c_szMtrlName, const SMaterialData& c_rkMaterialData)
{
	if (c_szMtrlName)
	{
		std::vector<CGrannyMaterial::TRef>::iterator i;
		for (i=m_mtrlVector.begin(); i!=m_mtrlVector.end(); ++i)
		{
			CGrannyMaterial::TRef& roMtrl=*i;

			int iStage;
			if (roMtrl->IsIn(c_szMtrlName, &iStage))
			{
				CGrannyMaterial* pkNewMtrl=new CGrannyMaterial;
				pkNewMtrl->Copy(*roMtrl.GetPointer());
				pkNewMtrl->SetImagePointer(iStage, c_rkMaterialData.pImage);
				pkNewMtrl->SetSpecularInfo(c_rkMaterialData.isSpecularEnable, c_rkMaterialData.fSpecularPower, c_rkMaterialData.bSphereMapIndex);
				roMtrl=pkNewMtrl;

				return;
			}
		}
	}
	else
	{
		std::vector<CGrannyMaterial::TRef>::iterator i;
		for (i=m_mtrlVector.begin(); i!=m_mtrlVector.end(); ++i)
		{
			CGrannyMaterial::TRef& roMtrl=*i;
			roMtrl->SetSpecularInfo(c_rkMaterialData.isSpecularEnable, c_rkMaterialData.fSpecularPower, c_rkMaterialData.bSphereMapIndex);
		}
	}
}

void CGrannyMaterialPalette::SetSpecularInfo(const char* c_szMtrlName, BOOL bEnable, float fPower)
{
	DWORD size=m_mtrlVector.size();
	DWORD i;
	if (c_szMtrlName)
	{
		for (i=0; i<size; ++i)
		{
			CGrannyMaterial::TRef& roMtrl=m_mtrlVector[i];

			int iStage;
			if (roMtrl->IsIn(c_szMtrlName, &iStage))
			{
				roMtrl->SetSpecularInfo(bEnable, fPower, 0);
				return;
			}
		}
	}
	else
	{
		for (i=0; i<size; ++i)
		{
			CGrannyMaterial::TRef& roMtrl=m_mtrlVector[i];
			roMtrl->SetSpecularInfo(bEnable, fPower, 0);
		}
	}
}

DWORD CGrannyMaterialPalette::RegisterMaterial(granny_material* pgrnMaterial)
{
	DWORD size=m_mtrlVector.size();
	DWORD i;
	for (i=0; i<size; ++i)
	{
		CGrannyMaterial::TRef& roMtrl=m_mtrlVector[i];
		if (roMtrl->IsEqual(pgrnMaterial))
			return i;
	}

	CGrannyMaterial* pkNewMtrl=new CGrannyMaterial;
	pkNewMtrl->CreateFromGrannyMaterialPointer(pgrnMaterial);
	m_mtrlVector.push_back(pkNewMtrl);
	
	return size;
}

DWORD CGrannyMaterialPalette::GetMaterialCount() const
{
	return m_mtrlVector.size();
}
