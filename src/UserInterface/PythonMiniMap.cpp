#include "StdAfx.h"
#include "EterLib/StateManager.h"
#include "EterLib/GrpTexture.h"
#include "EterLib/GrpSubImage.h"
#include "EterLib/Camera.h"
#include "PackLib/PackManager.h"
#include "EterLib/GrpDeviceDX11.h"
#include <d3dcompiler.h>

#include "PythonMiniMap.h"
#include "PythonBackground.h"
#include "PythonCharacterManager.h"
#include "PythonNonPlayer.h"
#include "PythonGuild.h"

#include "AbstractPlayer.h"

#include "EterPythonLib/PythonWindowManager.h"

namespace
{
	struct SDX11MiniMapMaskConstants
	{
		float center_x;
		float center_y;
		float radius;
		float edge_softness;
	};

	int CountDX11MiniMapTilesWithSRV(CGraphicTexture* const* apTextures, size_t uCount)
	{
		int iCount = 0;
		if (!apTextures)
			return iCount;

		for (size_t i = 0; i < uCount; ++i)
		{
			CGraphicTexture* pTexture = apTextures[i];
			if (pTexture && pTexture->GetD3D11TextureSRV())
				++iCount;
		}
		return iCount;
	}
}

void CPythonMiniMap::AddObserver(DWORD dwVID, float fSrcX, float fSrcY)
{
	std::map<DWORD, SObserver>::iterator f=m_kMap_dwVID_kObserver.find(dwVID);
	if (m_kMap_dwVID_kObserver.end()==f)
	{
		SObserver kObserver;
		kObserver.dwSrcTime=ELTimer_GetMSec();
		kObserver.dwDstTime=kObserver.dwSrcTime+1000;
		kObserver.fSrcX=fSrcX;
		kObserver.fSrcY=fSrcY;
		kObserver.fDstX=fSrcX;
		kObserver.fDstY=fSrcY;
		kObserver.fCurX=fSrcX;
		kObserver.fCurY=fSrcY;
		m_kMap_dwVID_kObserver.insert(std::map<DWORD, SObserver>::value_type(dwVID, kObserver));
	}
	else
	{
		SObserver& rkObserver=f->second;
		rkObserver.dwSrcTime=ELTimer_GetMSec();
		rkObserver.dwDstTime=rkObserver.dwSrcTime+1000;
		rkObserver.fSrcX=fSrcX;
		rkObserver.fSrcY=fSrcY;
		rkObserver.fDstX=fSrcX;
		rkObserver.fDstY=fSrcY;
		rkObserver.fCurX=fSrcX;
		rkObserver.fCurY=fSrcY;		
	}
}

void CPythonMiniMap::MoveObserver(DWORD dwVID, float fDstX, float fDstY)
{
	std::map<DWORD, SObserver>::iterator f=m_kMap_dwVID_kObserver.find(dwVID);
	if (m_kMap_dwVID_kObserver.end()==f)
		return;

	SObserver& rkObserver=f->second;
	rkObserver.dwSrcTime=ELTimer_GetMSec();
	rkObserver.dwDstTime=rkObserver.dwSrcTime+1000;
	rkObserver.fSrcX=rkObserver.fCurX;
	rkObserver.fSrcY=rkObserver.fCurY;
	rkObserver.fDstX=fDstX;
	rkObserver.fDstY=fDstY;
}

void CPythonMiniMap::RemoveObserver(DWORD dwVID)
{
	m_kMap_dwVID_kObserver.erase(dwVID);
}

void CPythonMiniMap::SetCenterPosition(float fCenterX, float fCenterY)
{
	m_fCenterX = fCenterX;
	m_fCenterY = fCenterY;

	CMapOutdoor& rkMap = CPythonBackground::Instance().GetMapOutdoorRef();
	for (BYTE byTerrainNum = 0; byTerrainNum < AROUND_AREA_NUM; ++byTerrainNum)
	{
		m_apMiniMapTexture[byTerrainNum] = NULL;
		CTerrain * pTerrain;
		if (rkMap.GetTerrainPointer(byTerrainNum, &pTerrain))
			m_apMiniMapTexture[byTerrainNum] = pTerrain->GetMiniMapGraphicTexture();
	}

	const TOutdoorMapCoordinate & rOutdoorMapCoord = rkMap.GetCurCoordinate();

	m_fCenterCellX = (m_fCenterX - (float)(rOutdoorMapCoord.m_sTerrainCoordX * CTerrainImpl::TERRAIN_XSIZE)) / (float)(CTerrainImpl::CELLSCALE);
	m_fCenterCellY = (m_fCenterY - (float)(rOutdoorMapCoord.m_sTerrainCoordY * CTerrainImpl::TERRAIN_YSIZE)) / (float)(CTerrainImpl::CELLSCALE);

	__SetPosition();
}

void CPythonMiniMap::Update(float fCenterX, float fCenterY)
{
	CPythonBackground& rkBG=CPythonBackground::Instance();
	if (!rkBG.IsMapOutdoor())
		return;
	
	// 미니맵 그림 갱신
	if (m_fCenterX != fCenterX || m_fCenterY != fCenterY )
		SetCenterPosition(fCenterX, fCenterY);

	// 캐릭터 리스트 갱신
	m_OtherPCPositionVector.clear();
	m_PartyPCPositionVector.clear();
	m_NPCPositionVector.clear();
	m_MonsterPositionVector.clear();
	m_WarpPositionVector.clear();

	float fooCellScale = 1.0f / ((float) CTerrainImpl::CELLSCALE);

	CPythonCharacterManager& rkChrMgr=CPythonCharacterManager::Instance();

	CInstanceBase* pkInstMain=rkChrMgr.GetMainInstancePtr();
	if (!pkInstMain)
		return;

	CPythonCharacterManager::CharacterIterator i;
	for(i = rkChrMgr.CharacterInstanceBegin(); i!=rkChrMgr.CharacterInstanceEnd(); ++i)
	{
		CInstanceBase* pkInstEach=*i;

		TPixelPosition kInstancePosition;
		pkInstEach->NEW_GetPixelPosition(&kInstancePosition);
		float fDistanceFromCenterX = (kInstancePosition.x - m_fCenterX) * fooCellScale * m_fScale;
		float fDistanceFromCenterY = (kInstancePosition.y - m_fCenterY) * fooCellScale * m_fScale;
		if (fabs(fDistanceFromCenterX) >= m_fMiniMapRadius || fabs(fDistanceFromCenterY) >= m_fMiniMapRadius)
			continue;

		float fDistanceFromCenter = sqrtf(fDistanceFromCenterX * fDistanceFromCenterX + fDistanceFromCenterY * fDistanceFromCenterY );
		if ( fDistanceFromCenter >= m_fMiniMapRadius )
			continue;

		TMarkPosition aMarkPosition;

		if (pkInstEach->IsPC() && !pkInstEach->IsInvisibility())
		{
			if (pkInstEach == CPythonCharacterManager::Instance().GetMainInstancePtr())
				continue;

			aMarkPosition.m_fX = ( m_fWidth - (float)m_WhiteMark.GetWidth() ) / 2.0f + fDistanceFromCenterX + m_fScreenX;
			aMarkPosition.m_fY = ( m_fHeight - (float)m_WhiteMark.GetHeight() ) / 2.0f + fDistanceFromCenterY + m_fScreenY;
			aMarkPosition.m_eNameColor=pkInstEach->GetNameColorIndex();
			if (aMarkPosition.m_eNameColor==CInstanceBase::NAMECOLOR_PARTY)
				m_PartyPCPositionVector.push_back(aMarkPosition);
			else
				m_OtherPCPositionVector.push_back(aMarkPosition);
		}
		else if (pkInstEach->IsNPC())
		{
			aMarkPosition.m_fX = ( m_fWidth - (float)m_WhiteMark.GetWidth() ) / 2.0f + fDistanceFromCenterX + m_fScreenX;
			aMarkPosition.m_fY = ( m_fHeight - (float)m_WhiteMark.GetHeight() ) / 2.0f + fDistanceFromCenterY + m_fScreenY;

			m_NPCPositionVector.push_back(aMarkPosition);
		}
		else if (pkInstEach->IsEnemy())
		{
			aMarkPosition.m_fX = ( m_fWidth - (float)m_WhiteMark.GetWidth() ) / 2.0f + fDistanceFromCenterX + m_fScreenX;
			aMarkPosition.m_fY = ( m_fHeight - (float)m_WhiteMark.GetHeight() ) / 2.0f + fDistanceFromCenterY + m_fScreenY;

			m_MonsterPositionVector.push_back(aMarkPosition);
		}
		else if (pkInstEach->IsWarp())
		{
			aMarkPosition.m_fX = ( m_fWidth - (float)m_WhiteMark.GetWidth() ) / 2.0f + fDistanceFromCenterX + m_fScreenX;
			aMarkPosition.m_fY = ( m_fHeight - (float)m_WhiteMark.GetHeight() ) / 2.0f + fDistanceFromCenterY + m_fScreenY;

			m_WarpPositionVector.push_back(aMarkPosition);
		}
	}

	{
		DWORD dwCurTime=ELTimer_GetMSec();

		std::map<DWORD, SObserver>::iterator i;
		for (i=m_kMap_dwVID_kObserver.begin(); i!=m_kMap_dwVID_kObserver.end(); ++i)
		{
			SObserver& rkObserver=i->second;

			float fPos=float(dwCurTime-rkObserver.dwSrcTime)/float(rkObserver.dwDstTime-rkObserver.dwSrcTime);			
			if (fPos<0.0f) fPos=0.0f;
			else if (fPos>1.0f) fPos=1.0f;

			rkObserver.fCurX=(rkObserver.fDstX-rkObserver.fSrcX)*fPos+rkObserver.fSrcX;
			rkObserver.fCurY=(rkObserver.fDstY-rkObserver.fSrcY)*fPos+rkObserver.fSrcY;

			TPixelPosition kInstancePosition;
			kInstancePosition.x=rkObserver.fCurX;
			kInstancePosition.y=rkObserver.fCurY;
			kInstancePosition.z=0.0f;

			float fDistanceFromCenterX = (kInstancePosition.x - m_fCenterX) * fooCellScale * m_fScale;
			float fDistanceFromCenterY = (kInstancePosition.y - m_fCenterY) * fooCellScale * m_fScale;
			if (fabs(fDistanceFromCenterX) >= m_fMiniMapRadius || fabs(fDistanceFromCenterY) >= m_fMiniMapRadius)
				continue;

			float fDistanceFromCenter = sqrtf(fDistanceFromCenterX * fDistanceFromCenterX + fDistanceFromCenterY * fDistanceFromCenterY );
			if ( fDistanceFromCenter >= m_fMiniMapRadius )
				continue;

			TMarkPosition aMarkPosition;
			aMarkPosition.m_fX = ( m_fWidth - (float)m_WhiteMark.GetWidth() ) / 2.0f + fDistanceFromCenterX + m_fScreenX;
			aMarkPosition.m_fY = ( m_fHeight - (float)m_WhiteMark.GetHeight() ) / 2.0f + fDistanceFromCenterY + m_fScreenY;
			aMarkPosition.m_eNameColor=CInstanceBase::NAMECOLOR_PARTY;
			m_PartyPCPositionVector.push_back(aMarkPosition);
		}
	}

	{
		TAtlasMarkInfoVector::iterator itor = m_AtlasWayPointInfoVector.begin();
		for (; itor != m_AtlasWayPointInfoVector.end(); ++itor)
		{
			TAtlasMarkInfo & rAtlasMarkInfo = *itor;

			if (TYPE_TARGET != rAtlasMarkInfo.m_byType)
				continue;

			if (0 != rAtlasMarkInfo.m_dwChrVID)
			{
				CInstanceBase * pInstance = CPythonCharacterManager::Instance().GetInstancePtr(rAtlasMarkInfo.m_dwChrVID);
				if (pInstance)
				{
					TPixelPosition kPixelPosition;
					pInstance->NEW_GetPixelPosition(&kPixelPosition);
					__UpdateWayPoint(&rAtlasMarkInfo, kPixelPosition.x, kPixelPosition.y);
				}
			}

			const float c_fMiniMapWindowRadius = m_fMiniMapRadius;

			float fDistanceFromCenterX = (rAtlasMarkInfo.m_fX - m_fCenterX) * fooCellScale * m_fScale;
			float fDistanceFromCenterY = (rAtlasMarkInfo.m_fY - m_fCenterY) * fooCellScale * m_fScale;
			float fDistanceFromCenter = sqrtf(fDistanceFromCenterX * fDistanceFromCenterX + fDistanceFromCenterY * fDistanceFromCenterY );

			if (fDistanceFromCenter >= c_fMiniMapWindowRadius)
			{
				float fRadian = atan2f(fDistanceFromCenterY, fDistanceFromCenterX);
				fDistanceFromCenterX = c_fMiniMapWindowRadius * cosf(fRadian);
				fDistanceFromCenterY = c_fMiniMapWindowRadius * sinf(fRadian);
				rAtlasMarkInfo.m_fMiniMapX = ( m_fWidth - (float)m_WhiteMark.GetWidth() ) / 2.0f + fDistanceFromCenterX + m_fScreenX;
				rAtlasMarkInfo.m_fMiniMapY = ( m_fHeight - (float)m_WhiteMark.GetHeight() ) / 2.0f + fDistanceFromCenterY + m_fScreenY;
			}
			else
			{
				rAtlasMarkInfo.m_fMiniMapX = ( m_fWidth - (float)m_WhiteMark.GetWidth() ) / 2.0f + fDistanceFromCenterX + m_fScreenX;
				rAtlasMarkInfo.m_fMiniMapY = ( m_fHeight - (float)m_WhiteMark.GetHeight() ) / 2.0f + fDistanceFromCenterY + m_fScreenY;
			}
		}
	}
}

bool CPythonMiniMap::__EnsureDX11MiniMapMaskResources()
{
	if (m_bDX11MiniMapMaskResourcesReady)
		return true;

	if (m_bDX11MiniMapMaskResourcesFailed)
		return false;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		m_bDX11MiniMapMaskResourcesFailed = true;
		TraceError("DX11_MINIMAP_SHADER_FAIL reason=device_unavailable");
		return false;
	}

	ID3D11Device* pDevice = pDX11Device->GetDevice();
	if (!pDevice)
	{
		m_bDX11MiniMapMaskResourcesFailed = true;
		TraceError("DX11_MINIMAP_SHADER_FAIL reason=d3d11_device_null");
		return false;
	}

	static const char* c_szMiniMapMaskPS =
		"Texture2D tx0 : register(t0);"
		"SamplerState smp0 : register(s0);"
		"cbuffer MiniMapMaskCB : register(b0)"
		"{"
		"    float2 gCenterPx;"
		"    float gRadiusPx;"
		"    float gEdgeSoftnessPx;"
		"};"
		"struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
		"float4 main(PSIn input) : SV_TARGET"
		"{"
		"    float2 delta = input.pos.xy - gCenterPx;"
		"    float dist = length(delta);"
		"    float softness = max(gEdgeSoftnessPx, 0.0001f);"
		"    float alphaMask = saturate((gRadiusPx - dist) / softness);"
		"    if (alphaMask <= 0.0f) discard;"
		"    return tx0.Sample(smp0, input.uv) * input.col * alphaMask;"
		"}";

	ID3DBlob* pShaderBlob = NULL;
	ID3DBlob* pErrorBlob = NULL;
	HRESULT hr = D3DCompile(
		c_szMiniMapMaskPS,
		strlen(c_szMiniMapMaskPS),
		NULL,
		NULL,
		NULL,
		"main",
		"ps_4_0",
		0,
		0,
		&pShaderBlob,
		&pErrorBlob);
	if (FAILED(hr))
	{
		m_bDX11MiniMapMaskResourcesFailed = true;
		if (pErrorBlob && pErrorBlob->GetBufferPointer())
		{
			const char* c_szCompilerMessage = reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer());
			TraceError("DX11_MINIMAP_SHADER_FAIL reason=compile_failed hr=0x%08x msg=%s", static_cast<unsigned int>(hr), c_szCompilerMessage);
		}
		else
		{
			TraceError("DX11_MINIMAP_SHADER_FAIL reason=compile_failed hr=0x%08x", static_cast<unsigned int>(hr));
		}
		SAFE_RELEASE(pErrorBlob);
		SAFE_RELEASE(pShaderBlob);
		return false;
	}
	SAFE_RELEASE(pErrorBlob);

	hr = pDevice->CreatePixelShader(
		pShaderBlob->GetBufferPointer(),
		pShaderBlob->GetBufferSize(),
		NULL,
		&m_pDX11MiniMapMaskPixelShader);
	if (FAILED(hr) || !m_pDX11MiniMapMaskPixelShader)
	{
		m_bDX11MiniMapMaskResourcesFailed = true;
		TraceError("DX11_MINIMAP_SHADER_FAIL reason=create_ps_failed hr=0x%08x", static_cast<unsigned int>(hr));
		SAFE_RELEASE(pShaderBlob);
		return false;
	}
	SAFE_RELEASE(pShaderBlob);

	D3D11_BUFFER_DESC kCBDesc = {};
	kCBDesc.Usage = D3D11_USAGE_DYNAMIC;
	kCBDesc.ByteWidth = sizeof(SDX11MiniMapMaskConstants);
	kCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	kCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = pDevice->CreateBuffer(&kCBDesc, NULL, &m_pDX11MiniMapMaskConstantBuffer);
	if (FAILED(hr) || !m_pDX11MiniMapMaskConstantBuffer)
	{
		m_bDX11MiniMapMaskResourcesFailed = true;
		TraceError("DX11_MINIMAP_SHADER_FAIL reason=create_cb_failed hr=0x%08x", static_cast<unsigned int>(hr));
		__DestroyDX11MiniMapMaskResources();
		return false;
	}
	m_bDX11MiniMapMaskResourcesReady = true;
	m_bDX11MiniMapMaskResourcesFailed = false;
	TraceError("DX11_MINIMAP_MASK_SHADER_OK");
	return true;
}

void CPythonMiniMap::__DestroyDX11MiniMapMaskResources()
{
	SAFE_RELEASE(m_pDX11MiniMapMaskConstantBuffer);
	SAFE_RELEASE(m_pDX11MiniMapMaskPixelShader);
	m_bDX11MiniMapMaskResourcesReady = false;
}

bool CPythonMiniMap::__UpdateDX11MiniMapMaskCB(float fCenterX, float fCenterY, float fRadius, float fEdgeSoftness)
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !m_pDX11MiniMapMaskConstantBuffer)
		return false;

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pContext)
		return false;

	D3D11_MAPPED_SUBRESOURCE kMapped = {};
	HRESULT hr = pContext->Map(m_pDX11MiniMapMaskConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMapped);
	if (FAILED(hr) || !kMapped.pData)
	{
		static DWORD s_dwLastCBMapFailLog = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0 == s_dwLastCBMapFailLog || (dwNow - s_dwLastCBMapFailLog) >= 2000u)
		{
			TraceError("DX11_MINIMAP_SHADER_FAIL reason=cb_map_failed hr=0x%08x", static_cast<unsigned int>(hr));
			s_dwLastCBMapFailLog = dwNow;
		}
		return false;
	}

	SDX11MiniMapMaskConstants kConstants = {};
	kConstants.center_x = fCenterX;
	kConstants.center_y = fCenterY;
	kConstants.radius = fRadius;
	kConstants.edge_softness = fEdgeSoftness;
	memcpy(kMapped.pData, &kConstants, sizeof(kConstants));
	pContext->Unmap(m_pDX11MiniMapMaskConstantBuffer, 0);
	return true;
}

// W4: DX11 minimap tile rendering using bootstrap 2D pipeline
bool CPythonMiniMap::__RenderTilesDX11()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return false;

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pContext)
		return false;

	// Check if we have DX11 textures available
	const int iTilesWithSRV = CountDX11MiniMapTilesWithSRV(m_apMiniMapTexture, AROUND_AREA_NUM);
	if (iTilesWithSRV == 0)
		return false;

	// W4.6: Bootstrap resources should always be ready (initialized by Model 1)
	if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
		return false;

	UINT uBackBufferWidth = 0;
	UINT uBackBufferHeight = 0;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (0 == uBackBufferWidth || 0 == uBackBufferHeight)
		return false;

	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	if (!pInputLayout || !pVertexShader || !pVertexBuffer || !pAlphaBlendState || !pDepthDisableState || !pSamplerState)
		return false;

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const auto PixelToNDCX = [uBackBufferWidth](float fX) -> float
	{
		return (2.0f * fX / static_cast<float>(uBackBufferWidth)) - 1.0f;
	};
	const auto PixelToNDCY = [uBackBufferHeight](float fY) -> float
	{
		return 1.0f - (2.0f * fY / static_cast<float>(uBackBufferHeight));
	};
	const auto LocalToScreenX = [this](float fX) -> float
	{
		return (fX * m_matWorld._11) + m_matWorld._41;
	};
	const auto LocalToScreenY = [this](float fY) -> float
	{
		return (fY * m_matWorld._22) + m_matWorld._42;
	};
	const auto BuildTileVertices = [&](float fLocalX, float fLocalY, SBootstrapVertex* pOutVertices)
	{
		const float fX0 = LocalToScreenX(fLocalX);
		const float fY0 = LocalToScreenY(fLocalY);
		const float fX1 = LocalToScreenX(fLocalX + 1.0f);
		const float fY1 = LocalToScreenY(fLocalY + 1.0f);

		pOutVertices[0] = { PixelToNDCX(fX0), PixelToNDCY(fY0), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f };
		pOutVertices[1] = { PixelToNDCX(fX0), PixelToNDCY(fY1), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };
		pOutVertices[2] = { PixelToNDCX(fX1), PixelToNDCY(fY0), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f };
		pOutVertices[3] = { PixelToNDCX(fX1), PixelToNDCY(fY0), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f };
		pOutVertices[4] = { PixelToNDCX(fX0), PixelToNDCY(fY1), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };
		pOutVertices[5] = { PixelToNDCX(fX1), PixelToNDCY(fY1), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
	};

	if (!__EnsureDX11MiniMapMaskResources() || !m_pDX11MiniMapMaskPixelShader || !m_pDX11MiniMapMaskConstantBuffer)
		return false;

	const float fMaskCenterX = m_fScreenX + m_fWidth * 0.5f;
	const float fMaskCenterY = m_fScreenY + m_fHeight * 0.5f;
	const float fMaskRadius = m_fMiniMapRadius;
	const float fMaskEdgeSoftness = 1.5f;
	if (!__UpdateDX11MiniMapMaskCB(fMaskCenterX, fMaskCenterY, fMaskRadius, fMaskEdgeSoftness))
		return false;

	pDX11Device->BindMainRenderTargets();

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	const FLOAT afBlendFactor[4] = { 0, 0, 0, 0 };
	pContext->OMSetBlendState(pAlphaBlendState, afBlendFactor, 0xFFFFFFFFu);
	pContext->OMSetDepthStencilState(pDepthDisableState, 0);
	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(pVertexShader, NULL, 0);
	pContext->PSSetSamplers(0, 1, &pSamplerState);
	pContext->PSSetShader(m_pDX11MiniMapMaskPixelShader, NULL, 0);
	pContext->PSSetConstantBuffers(0, 1, &m_pDX11MiniMapMaskConstantBuffer);

	int iTilesDrawn = 0;
	for (BYTE byTerrainNum = 0; byTerrainNum < AROUND_AREA_NUM; ++byTerrainNum)
	{
		CGraphicTexture* pMiniMapTexture = m_apMiniMapTexture[byTerrainNum];
		ID3D11ShaderResourceView* pTileSRV = pMiniMapTexture ? pMiniMapTexture->GetD3D11TextureSRV() : NULL;
		if (!pTileSRV)
			continue;

		const int iRow = byTerrainNum / 3;
		const int iColumn = byTerrainNum % 3;
		const float fLocalX = -1.5f + static_cast<float>(iColumn);
		const float fLocalY = -1.5f + static_cast<float>(iRow);

		SBootstrapVertex akTileVertices[6];
		BuildTileVertices(fLocalX, fLocalY, akTileVertices);

		D3D11_MAPPED_SUBRESOURCE kMappedResource = {};
		if (FAILED(pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource)) || !kMappedResource.pData)
			continue;

		memcpy(kMappedResource.pData, akTileVertices, sizeof(akTileVertices));
		pContext->Unmap(pVertexBuffer, 0);

		pContext->PSSetShaderResources(0, 1, &pTileSRV);
		pContext->Draw(6, 0);
		++iTilesDrawn;
	}

	ID3D11ShaderResourceView* pNullSRV = NULL;
	ID3D11Buffer* pNullCB = NULL;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);
	pContext->PSSetConstantBuffers(0, 1, &pNullCB);
	pContext->OMSetDepthStencilState(NULL, 0);

	if (iTilesDrawn > 0)
	{
		static DWORD s_dwLastMiniMapDX11Heartbeat = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0 == s_dwLastMiniMapDX11Heartbeat || dwNow - s_dwLastMiniMapDX11Heartbeat >= 5000u)
		{
			TraceError("DX11_MINIMAP_DX11_ACTIVE tiles_drawn=%d tiles_with_srv=%d", iTilesDrawn, iTilesWithSRV);
			TraceError("DX11_MINIMAP_MASK_ACTIVE tiles_drawn=%d scale=%.2f radius=%.2f", iTilesDrawn, m_fScale, m_fMiniMapRadius);
			TraceError("DX11_MINIMAP_RADIUS_FIXED radius=%.2f scale=%.2f", m_fMiniMapRadius, m_fScale);
			s_dwLastMiniMapDX11Heartbeat = dwNow;
		}
		return true;
	}

	static bool s_bLoggedDX11TileFail = false;
	if (!s_bLoggedDX11TileFail)
	{
		s_bLoggedDX11TileFail = true;
		TraceError("DX11_MINIMAP_DX11_FAIL reason=no_tiles_drawn tiles_with_srv=%d", iTilesWithSRV);
	}
	return false;
}

// W4.4: DX11 minimap marks rendering (player/party/PC/NPC/monster/warp)
bool CPythonMiniMap::__RenderMarksDX11()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device)
		return false;

	ID3D11Device* pDevice = pDX11Device->GetDevice();
	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pDevice || !pContext)
		return false;

	// Check if bootstrap resources are ready
	if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
		return false;

	UINT uBackBufferWidth = 0;
	UINT uBackBufferHeight = 0;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (0 == uBackBufferWidth || 0 == uBackBufferHeight)
		return false;

	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	if (!pInputLayout || !pVertexShader || !pPixelShader || !pVertexBuffer || !pAlphaBlendState || !pDepthDisableState || !pSamplerState)
		return false;

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const auto PixelToNDCX = [uBackBufferWidth](float fX) -> float
	{
		return (2.0f * fX / static_cast<float>(uBackBufferWidth)) - 1.0f;
	};
	const auto PixelToNDCY = [uBackBufferHeight](float fY) -> float
	{
		return 1.0f - (2.0f * fY / static_cast<float>(uBackBufferHeight));
	};

	const auto ResolveMarkUV = [&](CGraphicImageInstance& rMark, const char* c_szMarkName, float& fSU, float& fSV, float& fEU, float& fEV) -> bool
	{
		CGraphicImage* pImage = rMark.GetGraphicImagePointer();
		CGraphicTexture* pTexture = rMark.GetTexturePointer();
		if (!pImage || !pTexture)
		{
			static bool s_bLoggedMissingImageOrTexture = false;
			if (!s_bLoggedMissingImageOrTexture)
			{
				s_bLoggedMissingImageOrTexture = true;
				TraceError("DX11_MINIMAP_MARK_UV_FAIL reason=missing_image_or_texture mark=%s", c_szMarkName ? c_szMarkName : "unknown");
			}
			return false;
		}

		const int iTextureWidth = pTexture->GetWidth();
		const int iTextureHeight = pTexture->GetHeight();
		if (iTextureWidth <= 0 || iTextureHeight <= 0)
		{
			static bool s_bLoggedInvalidTextureSize = false;
			if (!s_bLoggedInvalidTextureSize)
			{
				s_bLoggedInvalidTextureSize = true;
				TraceError(
					"DX11_MINIMAP_MARK_UV_FAIL reason=invalid_texture_size mark=%s width=%d height=%d",
					c_szMarkName ? c_szMarkName : "unknown",
					iTextureWidth,
					iTextureHeight);
			}
			return false;
		}

		const RECT& c_rRect = pImage->GetRectReference();
		const float fInvTexWidth = 1.0f / static_cast<float>(iTextureWidth);
		const float fInvTexHeight = 1.0f / static_cast<float>(iTextureHeight);
		fSU = static_cast<float>(c_rRect.left) * fInvTexWidth;
		fSV = static_cast<float>(c_rRect.top) * fInvTexHeight;
		fEU = static_cast<float>(c_rRect.right) * fInvTexWidth;
		fEV = static_cast<float>(c_rRect.bottom) * fInvTexHeight;

		static DWORD s_dwLastMarkUVHeartbeat = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0 == s_dwLastMarkUVHeartbeat || (dwNow - s_dwLastMarkUVHeartbeat) >= 5000u)
		{
			TraceError(
				"DX11_MINIMAP_MARK_UV mode=sub_rect mark=%s w=%d h=%d su=%.4f sv=%.4f eu=%.4f ev=%.4f",
				c_szMarkName ? c_szMarkName : "unknown",
				rMark.GetWidth(),
				rMark.GetHeight(),
				fSU,
				fSV,
				fEU,
				fEV);
			s_dwLastMarkUVHeartbeat = dwNow;
		}

		return true;
	};

	const auto RenderMark = [&](CGraphicImageInstance& rMark, const char* c_szMarkName, float fScreenX, float fScreenY, const D3DXCOLOR& c_rColor) -> bool
	{
		if (rMark.IsEmpty())
			return false;

		CGraphicTexture* pTexture = rMark.GetTexturePointer();
		ID3D11ShaderResourceView* pSRV = pTexture ? pTexture->GetD3D11TextureSRV() : NULL;
		if (!pSRV)
			return false;

		float fSU = 0.0f;
		float fSV = 0.0f;
		float fEU = 1.0f;
		float fEV = 1.0f;
		if (!ResolveMarkUV(rMark, c_szMarkName, fSU, fSV, fEU, fEV))
			return false;

		const int iWidth = rMark.GetWidth();
		const int iHeight = rMark.GetHeight();
		const float fX0 = fScreenX;
		const float fY0 = fScreenY;
		const float fX1 = fScreenX + static_cast<float>(iWidth);
		const float fY1 = fScreenY + static_cast<float>(iHeight);

		SBootstrapVertex aVertices[6];
		aVertices[0] = { PixelToNDCX(fX0), PixelToNDCY(fY0), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fSV };
		aVertices[1] = { PixelToNDCX(fX0), PixelToNDCY(fY1), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fEV };
		aVertices[2] = { PixelToNDCX(fX1), PixelToNDCY(fY0), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fSV };
		aVertices[3] = { PixelToNDCX(fX1), PixelToNDCY(fY0), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fSV };
		aVertices[4] = { PixelToNDCX(fX0), PixelToNDCY(fY1), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fEV };
		aVertices[5] = { PixelToNDCX(fX1), PixelToNDCY(fY1), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fEV };

		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT hr = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (FAILED(hr))
			return false;

		memcpy(mappedResource.pData, aVertices, sizeof(aVertices));
		pContext->Unmap(pVertexBuffer, 0);

		pContext->PSSetShaderResources(0, 1, &pSRV);
		pContext->Draw(6, 0);
		return true;
	};

	const auto RenderMarkWithRotation = [&](CGraphicImageInstance& rMark, const char* c_szMarkName, float fScreenX, float fScreenY, const D3DXCOLOR& c_rColor, float fRotationDegrees) -> bool
	{
		if (rMark.IsEmpty())
			return false;

		CGraphicTexture* pTexture = rMark.GetTexturePointer();
		ID3D11ShaderResourceView* pSRV = pTexture ? pTexture->GetD3D11TextureSRV() : NULL;
		if (!pSRV)
			return false;

		float fSU = 0.0f;
		float fSV = 0.0f;
		float fEU = 1.0f;
		float fEV = 1.0f;
		if (!ResolveMarkUV(rMark, c_szMarkName, fSU, fSV, fEU, fEV))
			return false;

		const int iWidth = rMark.GetWidth();
		const int iHeight = rMark.GetHeight();
		const float fCenterX = fScreenX + static_cast<float>(iWidth) / 2.0f;
		const float fCenterY = fScreenY + static_cast<float>(iHeight) / 2.0f;
		const float fHalfWidth = static_cast<float>(iWidth) / 2.0f;
		const float fHalfHeight = static_cast<float>(iHeight) / 2.0f;

		// Convert rotation to radians
		const float fRotationRad = DirectX::XMConvertToRadians(fRotationDegrees);
		const float fCos = cosf(fRotationRad);
		const float fSin = sinf(fRotationRad);

		// Define quad corners relative to center
		float aCorners[4][2] = {
			{ -fHalfWidth, -fHalfHeight },  // Top-left
			{ -fHalfWidth,  fHalfHeight },  // Bottom-left
			{  fHalfWidth, -fHalfHeight },  // Top-right
			{  fHalfWidth,  fHalfHeight }   // Bottom-right
		};

		// Rotate and translate corners
		float aRotatedCorners[4][2];
		for (int i = 0; i < 4; ++i)
		{
			const float fX = aCorners[i][0];
			const float fY = aCorners[i][1];
			aRotatedCorners[i][0] = fCenterX + (fX * fCos - fY * fSin);
			aRotatedCorners[i][1] = fCenterY + (fX * fSin + fY * fCos);
		}

		SBootstrapVertex aVertices[6];
		aVertices[0] = { PixelToNDCX(aRotatedCorners[0][0]), PixelToNDCY(aRotatedCorners[0][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fSV };
		aVertices[1] = { PixelToNDCX(aRotatedCorners[1][0]), PixelToNDCY(aRotatedCorners[1][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fEV };
		aVertices[2] = { PixelToNDCX(aRotatedCorners[2][0]), PixelToNDCY(aRotatedCorners[2][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fSV };
		aVertices[3] = { PixelToNDCX(aRotatedCorners[2][0]), PixelToNDCY(aRotatedCorners[2][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fSV };
		aVertices[4] = { PixelToNDCX(aRotatedCorners[1][0]), PixelToNDCY(aRotatedCorners[1][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fEV };
		aVertices[5] = { PixelToNDCX(aRotatedCorners[3][0]), PixelToNDCY(aRotatedCorners[3][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fEV };

		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT hr = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (FAILED(hr))
			return false;

		memcpy(mappedResource.pData, aVertices, sizeof(aVertices));
		pContext->Unmap(pVertexBuffer, 0);

		pContext->PSSetShaderResources(0, 1, &pSRV);
		pContext->Draw(6, 0);
		return true;
	};

	// Set up rendering state
	pDX11Device->BindMainRenderTargets();

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	const FLOAT afBlendFactor[4] = { 0, 0, 0, 0 };
	pContext->OMSetBlendState(pAlphaBlendState, afBlendFactor, 0xFFFFFFFFu);
	pContext->OMSetDepthStencilState(pDepthDisableState, 0);
	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(pVertexShader, NULL, 0);
	pContext->PSSetShader(pPixelShader, NULL, 0);
	pContext->PSSetSamplers(0, 1, &pSamplerState);

	int iMarksDrawn = 0;

	// Render scale-dependent marks (monsters and other PCs) - only at scale >= 2.0
	if (m_fScale >= 2.0f)
	{
		// Monster marks
		const D3DXCOLOR& rMonsterColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_MOB);
		TInstancePositionVectorIterator aIterator = m_MonsterPositionVector.begin();
		while (aIterator != m_MonsterPositionVector.end())
		{
			TMarkPosition& rPosition = *aIterator;
			if (RenderMark(m_WhiteMark, "white_mark_monster", rPosition.m_fX, rPosition.m_fY, rMonsterColor))
				++iMarksDrawn;
			++aIterator;
		}

		// Other PC marks
		const D3DXCOLOR& rOtherPCColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_PC);
		aIterator = m_OtherPCPositionVector.begin();
		while (aIterator != m_OtherPCPositionVector.end())
		{
			TMarkPosition& rPosition = *aIterator;
			if (RenderMark(m_WhiteMark, "white_mark_other_pc", rPosition.m_fX, rPosition.m_fY, rOtherPCColor))
				++iMarksDrawn;
			++aIterator;
		}

		// Party PC marks with pulsing effect
		if (!m_PartyPCPositionVector.empty())
		{
			float v = (1.0f + sinf(CTimer::Instance().GetCurrentSecond() * 6.0f)) / 5.0f + 0.6f;
			D3DXCOLOR cPartyBase = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_PARTY);
			D3DXCOLOR cModulator(v, v, v, 1.0f);
			D3DXCOLOR cPartyModulated;
			D3DXColorModulate(&cPartyModulated, &cPartyBase, &cModulator);

			aIterator = m_PartyPCPositionVector.begin();
			while (aIterator != m_PartyPCPositionVector.end())
			{
				TMarkPosition& rPosition = *aIterator;
				if (RenderMark(m_WhiteMark, "white_mark_party", rPosition.m_fX, rPosition.m_fY, cPartyModulated))
					++iMarksDrawn;
				++aIterator;
			}
		}
	}

	// NPC marks (always rendered regardless of scale)
	const D3DXCOLOR& rNPCColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_NPC);
	TInstancePositionVectorIterator aIterator = m_NPCPositionVector.begin();
	while (aIterator != m_NPCPositionVector.end())
	{
		TMarkPosition& rPosition = *aIterator;
		if (RenderMark(m_WhiteMark, "white_mark_npc", rPosition.m_fX, rPosition.m_fY, rNPCColor))
			++iMarksDrawn;
		++aIterator;
	}

	// Warp marks (always rendered)
	const D3DXCOLOR& rWarpColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_WARP);
	aIterator = m_WarpPositionVector.begin();
	while (aIterator != m_WarpPositionVector.end())
	{
		TMarkPosition& rPosition = *aIterator;
		if (RenderMark(m_WhiteMark, "white_mark_warp", rPosition.m_fX, rPosition.m_fY, rWarpColor))
			++iMarksDrawn;
		++aIterator;
	}

	// Player mark with rotation (always rendered)
	CInstanceBase* pkInst = CPythonCharacterManager::Instance().GetMainInstancePtr();
	if (pkInst && !m_PlayerMark.IsEmpty())
	{
		// Calculate rotation (matching DX9 path)
		float fRotation = 540.0f - pkInst->GetRotation();
		while (fRotation > 360.0f)
			fRotation -= 360.0f;
		while (fRotation < 0.0f)
			fRotation += 360.0f;

		const D3DXCOLOR whiteColor(1.0f, 1.0f, 1.0f, 1.0f);
		const float fPlayerX = m_fScreenX + (m_fWidth - m_PlayerMark.GetWidth()) / 2.0f;
		const float fPlayerY = m_fScreenY + (m_fHeight - m_PlayerMark.GetHeight()) / 2.0f;
		if (RenderMarkWithRotation(m_PlayerMark, "player_mark", fPlayerX, fPlayerY, whiteColor, fRotation))
			++iMarksDrawn;
	}

	// Render waypoint marks (from waypoint system) - targets and regular waypoints
	TAtlasMarkInfoVectorIterator itorAtlasWayPoint = m_AtlasWayPointInfoVector.begin();
	while (itorAtlasWayPoint != m_AtlasWayPointInfoVector.end())
	{
		TAtlasMarkInfo& rAtlasMarkInfo = *itorAtlasWayPoint;

		if (rAtlasMarkInfo.m_fMiniMapX > 0.0f && rAtlasMarkInfo.m_fMiniMapY > 0.0f)
		{
			if (TYPE_TARGET == rAtlasMarkInfo.m_byType)
			{
				// __RenderTargetMark logic: animated sprite based on timer
				const int iNum = (ELTimer_GetMSec() / 80) % TARGET_MARK_IMAGE_COUNT;
				CGraphicImageInstance& rInstance = m_TargetMarkGraphicImageInstances[iNum];

				const float fCenterX = rAtlasMarkInfo.m_fMiniMapX + m_WhiteMark.GetWidth() / 2.0f;
				const float fCenterY = rAtlasMarkInfo.m_fMiniMapY + m_WhiteMark.GetHeight() / 2.0f;
				const float fRenderX = fCenterX - rInstance.GetWidth() / 2.0f;
				const float fRenderY = fCenterY - rInstance.GetHeight() / 2.0f;

				const D3DXCOLOR whiteColor(1.0f, 1.0f, 1.0f, 1.0f);
				if (RenderMark(rInstance, "target_mark", fRenderX, fRenderY, whiteColor))
					++iMarksDrawn;
			}
			else
			{
				// Regular waypoint marks: animated sprite based on timer
				const int iNum = (ELTimer_GetMSec() / 67) % WAYPOINT_IMAGE_COUNT;
				CGraphicImageInstance& rInstance = m_WayPointGraphicImageInstances[iNum];

				const float fCenterX = rAtlasMarkInfo.m_fMiniMapX + m_WhiteMark.GetWidth() / 2.0f;
				const float fCenterY = rAtlasMarkInfo.m_fMiniMapY + m_WhiteMark.GetHeight() / 2.0f;
				const float fRenderX = fCenterX - rInstance.GetWidth() / 2.0f;
				const float fRenderY = fCenterY - rInstance.GetHeight() / 2.0f;

				const D3DXCOLOR whiteColor(1.0f, 1.0f, 1.0f, 1.0f);
				if (RenderMark(rInstance, "waypoint_mark", fRenderX, fRenderY, whiteColor))
					++iMarksDrawn;
			}
		}
		++itorAtlasWayPoint;
	}

	// Camera direction indicator (rotated)
	CCamera* pkCmrCur = CCameraManager::Instance().GetCurrentCamera();
	if (pkCmrCur && !m_MiniMapCameraraphicImageInstance.IsEmpty())
	{
		const D3DXCOLOR whiteColor(1.0f, 1.0f, 1.0f, 1.0f);
		const float fCameraX = m_fScreenX + (m_fWidth - m_MiniMapCameraraphicImageInstance.GetWidth()) / 2.0f;
		const float fCameraY = m_fScreenY + (m_fHeight - m_MiniMapCameraraphicImageInstance.GetHeight()) / 2.0f;
		const float fCameraRotation = pkCmrCur->GetRoll();
		if (RenderMarkWithRotation(m_MiniMapCameraraphicImageInstance, "camera_mark", fCameraX, fCameraY, whiteColor, fCameraRotation))
			++iMarksDrawn;
	}

	ID3D11ShaderResourceView* pNullSRV = NULL;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);
	pContext->OMSetDepthStencilState(NULL, 0);

	// Throttled telemetry (every 5 seconds)
	static DWORD s_dwLastMarksDX11Heartbeat = 0;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0 == s_dwLastMarksDX11Heartbeat || (dwNow - s_dwLastMarksDX11Heartbeat) >= 5000)
	{
		TraceError("DX11_MINIMAP_MARKS_DX11 marks_drawn=%d scale=%.2f", iMarksDrawn, m_fScale);
		s_dwLastMarksDX11Heartbeat = dwNow;
	}

	return iMarksDrawn > 0;
}

void CPythonMiniMap::Render(float fScreenX, float fScreenY)
{
	CPythonBackground& rkBG=CPythonBackground::Instance();
	if (!rkBG.IsMapOutdoor())
		return;

	if (!m_bShow)
		return;

	if (!rkBG.IsMapReady())
		return;

	if (m_fScreenX != fScreenX || m_fScreenY != fScreenY)
	{
		m_fScreenX = fScreenX;
		m_fScreenY = fScreenY;
		__SetPosition();
	}

	// W4.6: DX11-only minimap rendering (strict mode enforced)
	if (__RenderTilesDX11())
	{
		// M2-USERINTERFACE-HALF-B: Throttled telemetry for minimap tiles parity
		static DWORD s_dwMiniMapTilesOK = 0;
		static DWORD s_dwMiniMapTilesFail = 0;
		static DWORD s_dwMiniMapTilesTelemetryTick = 0;
		++s_dwMiniMapTilesOK;

		const DWORD dwCurrentTick = ELTimer_GetMSec();
		if (0 == s_dwMiniMapTilesTelemetryTick || (dwCurrentTick - s_dwMiniMapTilesTelemetryTick) >= 15000)
		{
			TraceError("DX11_MINIMAP_TILES_PARITY tiles_ok=%u tiles_fail=%u interval_ms=15000",
				s_dwMiniMapTilesOK, s_dwMiniMapTilesFail);
			s_dwMiniMapTilesTelemetryTick = dwCurrentTick;
		}

		// DX11 minimap rendering succeeded; render marks and return
		__RenderMarksDX11();
		return;
	}
	else
	{
		static DWORD s_dwMiniMapTilesFail = 0;
		++s_dwMiniMapTilesFail;
	}

	// DX11 render failed - log blocker and return (no DX9 fallback)
	static bool s_bLoggedMinimapBlock = false;
	if (!s_bLoggedMinimapBlock)
	{
		const int iTilesWithSRV = CountDX11MiniMapTilesWithSRV(m_apMiniMapTexture, AROUND_AREA_NUM);
		TraceError(
			"DX11_MINIMAP_RENDER_FAILED reason=dx11_tile_draw_failed dx11_tiles_with_srv=%d",
			iTilesWithSRV);
		s_bLoggedMinimapBlock = true;
	}
	return;
}

// W4.6: DX9 code cleanup - removed 228 lines of dead DX9 rendering code


void CPythonMiniMap::SetScale(float fScale)
{
	if (fScale >= 4.0f)
		fScale = 4.0f;
	if (fScale <= 0.5f)
		fScale = 0.5f;
	m_fScale = fScale;

	__SetPosition();
}

void CPythonMiniMap::ScaleUp()
{
	SetScale(m_fScale + 0.10f);
}

void CPythonMiniMap::ScaleDown()
{
	SetScale(m_fScale - 0.10f);
}

void CPythonMiniMap::SetMiniMapSize(float fWidth, float fHeight)
{
	m_fWidth = fWidth;
	m_fHeight = fHeight;
}

bool CPythonMiniMap::GetPickedInstanceInfo(float fScreenX, float fScreenY, std::string & rReturnName, float * pReturnPosX, float * pReturnPosY, DWORD * pdwTextColor)
{
	float fDistanceFromMiniMapCenterX = fScreenX - m_fScreenX - m_fWidth * 0.5f;
	float fDistanceFromMiniMapCenterY = fScreenY - m_fScreenY - m_fHeight * 0.5f;

	if (sqrtf(fDistanceFromMiniMapCenterX * fDistanceFromMiniMapCenterX + fDistanceFromMiniMapCenterY * fDistanceFromMiniMapCenterY) > m_fMiniMapRadius )
		return false;

	float fRealX = m_fCenterX + fDistanceFromMiniMapCenterX / m_fScale * ((float) CTerrainImpl::CELLSCALE);
	float fRealY = m_fCenterY + fDistanceFromMiniMapCenterY / m_fScale * ((float) CTerrainImpl::CELLSCALE);

	CInstanceBase * pkInst = CPythonCharacterManager::Instance().GetMainInstancePtr();

	if (pkInst)
	{
		TPixelPosition kInstPos;
		pkInst->NEW_GetPixelPosition(&kInstPos);

		if (fabs(kInstPos.x - fRealX) < ((float) CTerrainImpl::CELLSCALE) * 6.0f / m_fScale &&
			fabs(kInstPos.y - fRealY) < ((float) CTerrainImpl::CELLSCALE) * 6.0f / m_fScale)
		{
			rReturnName = pkInst->GetNameString();
			*pReturnPosX = kInstPos.x;
			*pReturnPosY = kInstPos.y;
			*pdwTextColor = pkInst->GetNameColor();
			return true;
		}
	}

	if (m_fScale < 1.0f)
		return false;

	CPythonCharacterManager& rkChrMgr=CPythonCharacterManager::Instance();
	CPythonCharacterManager::CharacterIterator i;
	for(i = rkChrMgr.CharacterInstanceBegin(); i!=rkChrMgr.CharacterInstanceEnd(); ++i)
	{
		CInstanceBase* pkInstEach=*i;
		if (pkInstEach->IsInvisibility())
			continue;
		if (m_fScale < 2.0f && (pkInstEach->IsEnemy() || pkInstEach->IsPC()))
			continue;
		TPixelPosition kInstancePosition;
		pkInstEach->NEW_GetPixelPosition(&kInstancePosition);

		if (fabs(kInstancePosition.x - fRealX) < ((float) CTerrainImpl::CELLSCALE) * 3.0f / m_fScale &&
			fabs(kInstancePosition.y - fRealY) < ((float) CTerrainImpl::CELLSCALE) * 3.0f / m_fScale)
		{
			rReturnName = pkInstEach->GetNameString();
			*pReturnPosX = kInstancePosition.x;
			*pReturnPosY = kInstancePosition.y;
			*pdwTextColor = pkInstEach->GetNameColor();
			return true;
		}
	}
	return false;
}

#pragma pack(push)
#pragma pack(1)
typedef struct _MINIMAPVERTEX
{
    float x, y, z;          // position
    float u, v;       // normal
} MINIMAPVERTEX, *LPMINIMAPVERTEX;
#pragma pack(pop)

bool CPythonMiniMap::Create()
{
	const std::string strImageRoot = "D:/ymir work/ui/";
	const std::string strImageFilter = strImageRoot + "minimap_image_filter.dds";
	const std::string strImageCamera = strImageRoot + "minimap_camera.dds";
	const std::string strPlayerMark = strImageRoot + "minimap/playermark.sub";
	const std::string strWhiteMark = strImageRoot + "minimap/whitemark.sub";

	// 미니맵 커버
	CGraphicImage * pImage = (CGraphicImage *) CResourceManager::Instance().GetResourcePointer(strImageFilter.c_str());
	m_MiniMapFilterGraphicImageInstance.SetImagePointer(pImage);
	pImage = (CGraphicImage *) CResourceManager::Instance().GetResourcePointer(strImageCamera.c_str());
	m_MiniMapCameraraphicImageInstance.SetImagePointer(pImage);

	m_matMiniMapCover._11 = 1.0f / ((float)m_MiniMapFilterGraphicImageInstance.GetWidth());
	m_matMiniMapCover._22 = 1.0f / ((float)m_MiniMapFilterGraphicImageInstance.GetHeight());
	m_matMiniMapCover._33 = 0.0f;

	// 캐릭터 마크
	CGraphicSubImage * pSubImage = (CGraphicSubImage *) CResourceManager::Instance().GetResourcePointer(strPlayerMark.c_str());
	m_PlayerMark.SetImagePointer(pSubImage);

	pSubImage = (CGraphicSubImage *) CResourceManager::Instance().GetResourcePointer(strWhiteMark.c_str());
	m_WhiteMark.SetImagePointer(pSubImage);

	char buf[256];
	for (int i = 0; i < MINI_WAYPOINT_IMAGE_COUNT; ++i)
	{
		sprintf(buf, "%sminimap/mini_waypoint%02d.sub", strImageRoot.c_str(), i+1);
		m_MiniWayPointGraphicImageInstances[i].SetImagePointer((CGraphicSubImage *) CResourceManager::Instance().GetResourcePointer(buf));
		m_MiniWayPointGraphicImageInstances[i].SetRenderingMode(CGraphicExpandedImageInstance::RENDERING_MODE_SCREEN);
	}
	for (int j = 0; j < WAYPOINT_IMAGE_COUNT; ++j)
	{
		sprintf(buf, "%sminimap/waypoint%02d.sub", strImageRoot.c_str(), j+1);
		m_WayPointGraphicImageInstances[j].SetImagePointer((CGraphicSubImage *) CResourceManager::Instance().GetResourcePointer(buf));
		m_WayPointGraphicImageInstances[j].SetRenderingMode(CGraphicExpandedImageInstance::RENDERING_MODE_SCREEN);
	}
	for (int k = 0; k < TARGET_MARK_IMAGE_COUNT; ++k)
	{
		sprintf(buf, "%sminimap/targetmark%02d.sub", strImageRoot.c_str(), k+1);
		m_TargetMarkGraphicImageInstances[k].SetImagePointer((CGraphicSubImage *) CResourceManager::Instance().GetResourcePointer(buf));
		m_TargetMarkGraphicImageInstances[k].SetRenderingMode(CGraphicExpandedImageInstance::RENDERING_MODE_SCREEN);
	}

	m_GuildAreaFlagImageInstance.SetImagePointer((CGraphicSubImage *) CResourceManager::Instance().GetResourcePointer("d:/ymir work/ui/minimap/GuildArea01.sub"));

	// 그려질 폴리곤 세팅
#if !defined(DX11_STRICT_ONLY)
#pragma pack(push)
#pragma pack(1)
	LPMINIMAPVERTEX		lpMiniMapVertex;
	LPMINIMAPVERTEX		lpOrigMiniMapVertex;
#pragma pack(pop)

	if (!m_VertexBuffer.Create(36, FVF_XYZ | FVF_TEX1, GRP_USAGE_DYNAMIC, GRP_POOL_DEFAULT) )
	{
		return false;
	}

	if (m_VertexBuffer.Lock((void **) &lpOrigMiniMapVertex))
	{		
		char * pchMiniMapVertex = (char *)lpOrigMiniMapVertex;
		memset(pchMiniMapVertex, 0, sizeof(char) * 720);
		lpMiniMapVertex = (LPMINIMAPVERTEX) pchMiniMapVertex;

		for (int iY = -3; iY <= 1; ++iY)
		{
			if (0 == iY%2)
				continue;
			float fY = 0.5f * ((float)iY);
			for (int iX = -3; iX <= 1; ++iX)
			{
				if (0 == iX%2)
					continue;
				float fX = 0.5f * ((float)iX);
				lpMiniMapVertex = (LPMINIMAPVERTEX) pchMiniMapVertex;
				lpMiniMapVertex->x = fX;
				lpMiniMapVertex->y = fY;
				lpMiniMapVertex->z = 0.0f;
				lpMiniMapVertex->u = 0.0f;
				lpMiniMapVertex->v = 0.0f;
				pchMiniMapVertex += 20;
				lpMiniMapVertex = (LPMINIMAPVERTEX) pchMiniMapVertex;
				lpMiniMapVertex->x = fX;
				lpMiniMapVertex->y = fY + 1.0f;
				lpMiniMapVertex->z = 0.0f;
				lpMiniMapVertex->u = 0.0f;
				lpMiniMapVertex->v = 1.0f;
				pchMiniMapVertex += 20;
				lpMiniMapVertex = (LPMINIMAPVERTEX) pchMiniMapVertex;
				lpMiniMapVertex->x = fX + 1.0f;
				lpMiniMapVertex->y = fY;
				lpMiniMapVertex->z = 0.0f;
				lpMiniMapVertex->u = 1.0f;
				lpMiniMapVertex->v = 0.0f;
				pchMiniMapVertex += 20;
				lpMiniMapVertex = (LPMINIMAPVERTEX) pchMiniMapVertex;
				lpMiniMapVertex->x = fX + 1.0f;
				lpMiniMapVertex->y = fY + 1.0f;
				lpMiniMapVertex->z = 0.0f;
				lpMiniMapVertex->u = 1.0f;
				lpMiniMapVertex->v = 1.0f;
				pchMiniMapVertex += 20;
			}
		}

		m_VertexBuffer.Unlock();
	}
	
	if (!m_IndexBuffer.Create(54, GRP_FMT_INDEX16))
	{
		return false;
	}

	WORD pwIndices[54] = 
	{
		0, 1, 2, 2, 1, 3,
		4, 5, 6, 6, 5, 7,
		8, 9, 10, 10, 9, 11,
		
		12, 13, 14, 14, 13, 15,
		16, 17, 18, 18, 17, 19,
		20, 21, 22, 22, 21, 23,
		
		24, 25, 26, 26, 25, 27,
		28, 29, 30, 30, 29, 31,
		32, 33, 34, 34, 33, 35
	};

	void * pIndices;
		
	if (m_IndexBuffer.Lock(&pIndices))
	{
		memcpy(pIndices, pwIndices, 54 * sizeof(WORD));
		m_IndexBuffer.Unlock();
	}
#endif

	return true;
}

void CPythonMiniMap::__SetPosition()
{
	float fWindowRadius = (m_fWidth < m_fHeight ? m_fWidth : m_fHeight) * 0.5f - 5.0f;
	if (fWindowRadius < 1.0f)
		fWindowRadius = 1.0f;
	m_fMiniMapRadius = fWindowRadius;

	m_matWorld._11 = m_fWidth * m_fScale;
	m_matWorld._22 = m_fHeight * m_fScale;
	m_matWorld._41 = (1.0f + m_fScale) * m_fWidth * 0.5f - m_fCenterCellX * m_fScale + m_fScreenX;
	m_matWorld._42 = (1.0f + m_fScale) * m_fHeight * 0.5f - m_fCenterCellY * m_fScale + m_fScreenY;

	if (!m_MiniMapFilterGraphicImageInstance.IsEmpty())
	{
		m_matMiniMapCover._41 = -(m_fScreenX) / ((float)m_MiniMapFilterGraphicImageInstance.GetWidth());
		m_matMiniMapCover._42 = -(m_fScreenY) / ((float)m_MiniMapFilterGraphicImageInstance.GetHeight());
	}

	if (!m_PlayerMark.IsEmpty())
		m_PlayerMark.SetPosition( ( m_fWidth - (float)m_PlayerMark.GetWidth() ) / 2.0f + m_fScreenX,
		( m_fHeight - (float)m_PlayerMark.GetHeight() ) / 2.0f  + m_fScreenY );

	if (!m_MiniMapCameraraphicImageInstance.IsEmpty())
		m_MiniMapCameraraphicImageInstance.SetPosition( ( m_fWidth - (float)m_MiniMapCameraraphicImageInstance.GetWidth() ) / 2.0f + m_fScreenX,
		( m_fHeight - (float)m_MiniMapCameraraphicImageInstance.GetHeight() ) / 2.0f  + m_fScreenY );
}

//////////////////////////////////////////////////////////////////////////
// Atlas

void CPythonMiniMap::ClearAtlasMarkInfo()
{
	m_AtlasNPCInfoVector.clear();
	m_AtlasWarpInfoVector.clear();
}

void CPythonMiniMap::RegisterAtlasMark(BYTE byType, const char * c_szName, long lx, long ly)
{
	TAtlasMarkInfo aAtlasMarkInfo;

	aAtlasMarkInfo.m_fX = float(lx);
	aAtlasMarkInfo.m_fY = float(ly);
	aAtlasMarkInfo.m_strText = c_szName;

	__GlobalPositionToAtlasPosition(lx, ly, &aAtlasMarkInfo.m_fScreenX, &aAtlasMarkInfo.m_fScreenY);
	aAtlasMarkInfo.m_fScreenX -= (float)m_WhiteMark.GetWidth() / 2.0f;
	aAtlasMarkInfo.m_fScreenY -= (float)m_WhiteMark.GetHeight() / 2.0f;

	switch(byType)
	{
		case CActorInstance::TYPE_NPC:
			aAtlasMarkInfo.m_byType = TYPE_NPC;
			m_AtlasNPCInfoVector.push_back(aAtlasMarkInfo);
			break;
		case CActorInstance::TYPE_WARP:
			aAtlasMarkInfo.m_byType = TYPE_WARP;
			{
				int iPos = aAtlasMarkInfo.m_strText.find(" ");
				if (iPos >= 0)
					aAtlasMarkInfo.m_strText[iPos]=0;
				
			}
			m_AtlasWarpInfoVector.push_back(aAtlasMarkInfo);
			break;
	}
}

void CPythonMiniMap::ClearGuildArea()
{
	m_GuildAreaInfoVector.clear();
}

void CPythonMiniMap::RegisterGuildArea(DWORD dwID, DWORD dwGuildID, long x, long y, long width, long height)
{
	TGuildAreaInfo kGuildAreaInfo;
	kGuildAreaInfo.dwGuildID = dwGuildID;
	kGuildAreaInfo.lx = x;
	kGuildAreaInfo.ly = y;
	kGuildAreaInfo.lwidth = width;
	kGuildAreaInfo.lheight = height;
	m_GuildAreaInfoVector.push_back(kGuildAreaInfo);
}

DWORD CPythonMiniMap::GetGuildAreaID(DWORD x, DWORD y)
{
	TGuildAreaInfoVectorIterator itor = m_GuildAreaInfoVector.begin();
	for (; itor != m_GuildAreaInfoVector.end(); ++itor)
	{
		TGuildAreaInfo & rAreaInfo = *itor;

		if (x >= rAreaInfo.lx)
		if (y >= rAreaInfo.ly)
		if (x <= rAreaInfo.lx + rAreaInfo.lwidth)
		if (y <= rAreaInfo.ly + rAreaInfo.lheight)
		{
			return rAreaInfo.dwGuildID;
		}
	}

	return 0xffffffff;
}

void CPythonMiniMap::CreateTarget(int iID, const char * c_szName)
{
	AddWayPoint(TYPE_TARGET, iID, 0.0f, 0.0f, c_szName);
}

void CPythonMiniMap::UpdateTarget(int iID, int ix, int iy)
{
	TAtlasMarkInfo * pkInfo;
	if (!__GetWayPoint(iID, &pkInfo))
		return;

	if (0 != pkInfo->m_dwChrVID)
	{
		if (CPythonCharacterManager::Instance().GetInstancePtr(pkInfo->m_dwChrVID))
			return;
	}

	if (ix < m_dwAtlasBaseX)
		return;
	if (iy < m_dwAtlasBaseY)
		return;
	if (ix > m_dwAtlasBaseX+DWORD(m_fAtlasMaxX))
		return;
	if (iy > m_dwAtlasBaseY+DWORD(m_fAtlasMaxY))
		return;

	__UpdateWayPoint(pkInfo, ix-int(m_dwAtlasBaseX), iy-int(m_dwAtlasBaseY));
}

void CPythonMiniMap::CreateTarget(int iID, const char * c_szName, DWORD dwVID)
{
	AddWayPoint(TYPE_TARGET, iID, 0.0f, 0.0f, c_szName, dwVID);
}

void CPythonMiniMap::DeleteTarget(int iID)
{
	RemoveWayPoint(iID);
}

void CPythonMiniMap::SetAtlasScale(float fx, float fy)
{
	m_AtlasImageInstance.SetScale(fx, fy);

	m_fAtlasImageSizeX = float(m_AtlasImageInstance.GetWidth()) * fx;
	m_fAtlasImageSizeY = float(m_AtlasImageInstance.GetHeight()) * fy;
	
	ComputeAtlasCenteringOffsets();
}

void CPythonMiniMap::ClearAtlasMarks()
{
	ClearAtlasMarkInfo();
	ClearGuildArea();
}

bool CPythonMiniMap::LoadAtlas()
{
	CPythonBackground& rkBG=CPythonBackground::Instance();
	if (!rkBG.IsMapOutdoor())
		return false;

	CMapOutdoor& rkMap=rkBG.GetMapOutdoorRef();

	const char* playerMarkFileName = "d:/ymir work/ui/minimap/playermark.sub";

	char atlasFileName[1024+1];
	snprintf(atlasFileName, sizeof(atlasFileName), "%s/atlas.sub", rkMap.GetName().c_str());	
	if (!CPackManager::Instance().IsExist(atlasFileName))		
	{
		snprintf(atlasFileName, sizeof(atlasFileName), "d:/ymir work/ui/atlas/%s/atlas.sub", rkMap.GetName().c_str());
	}
	
	m_AtlasImageInstance.Destroy();
	m_AtlasPlayerMark.Destroy();
	CGraphicImage* pkGrpImgAtlas = (CGraphicImage *) CResourceManager::Instance().GetResourcePointer(atlasFileName);
	if (pkGrpImgAtlas)
	{
		m_AtlasImageInstance.SetImagePointer(pkGrpImgAtlas);
		
		if (pkGrpImgAtlas->IsEmpty())
			m_bAtlas=false;
		else
			m_bAtlas=true;		
	}
	else
	{
	}
	m_AtlasPlayerMark.SetImagePointer((CGraphicSubImage *) CResourceManager::Instance().GetResourcePointer(playerMarkFileName));

	short sTerrainCountX, sTerrainCountY;
	rkMap.GetBaseXY(&m_dwAtlasBaseX, &m_dwAtlasBaseY);
	rkMap.GetTerrainCount(&sTerrainCountX, &sTerrainCountY);
	m_fAtlasMaxX = (float) sTerrainCountX * static_cast<float>(CTerrainImpl::TERRAIN_XSIZE);
	m_fAtlasMaxY = (float) sTerrainCountY * static_cast<float>(CTerrainImpl::TERRAIN_YSIZE);

	m_fAtlasImageSizeX = (float) m_AtlasImageInstance.GetWidth();
	m_fAtlasImageSizeY = (float) m_AtlasImageInstance.GetHeight();

	ComputeAtlasCenteringOffsets();
	
	ClearAtlasMarks();

	if (m_bShowAtlas)
		OpenAtlasWindow();

	return true;
}

void CPythonMiniMap::ComputeAtlasCenteringOffsets()
{
	float fScaleX = m_fAtlasImageSizeX / m_fAtlasMaxX;
	float fScaleY = m_fAtlasImageSizeY / m_fAtlasMaxY;
	float fUniformScale = std::min(fScaleX, fScaleY);
	
	float fScaledMapWidth = m_fAtlasMaxX * fUniformScale;
	float fScaledMapHeight = m_fAtlasMaxY * fUniformScale;
	
	m_fAtlasOffsetX = (m_fAtlasImageSizeX - fScaledMapWidth) * 0.5f;
	m_fAtlasOffsetY = (m_fAtlasImageSizeY - fScaledMapHeight) * 0.5f;
}

float CPythonMiniMap::GetAtlasUniformScale() const
{
	float fScaleX = m_fAtlasImageSizeX / m_fAtlasMaxX;
	float fScaleY = m_fAtlasImageSizeY / m_fAtlasMaxY;
	return std::min(fScaleX, fScaleY);
}

void CPythonMiniMap::__GlobalPositionToAtlasPosition(long lx, long ly, float * pfx, float * pfy)
{
	float fUniformScale = GetAtlasUniformScale();
	
	*pfx = lx * fUniformScale + m_fAtlasOffsetX;
	*pfy = ly * fUniformScale + m_fAtlasOffsetY;
}

void CPythonMiniMap::UpdateAtlas()
{
	CInstanceBase * pkInst = CPythonCharacterManager::Instance().GetMainInstancePtr();

	if (pkInst)
	{
		TPixelPosition kInstPos;
		pkInst->NEW_GetPixelPosition(&kInstPos);

		float fRotation;
		fRotation = (540.0f - pkInst->GetRotation());
		while(fRotation > 360.0f)
			fRotation -= 360.0f;
		while(fRotation < 0.0f)
			fRotation += 360.0f;

		float fPlayerX, fPlayerY;
		__GlobalPositionToAtlasPosition((long)kInstPos.x, (long)kInstPos.y, &fPlayerX, &fPlayerY);
		m_AtlasPlayerMark.SetPosition(fPlayerX - (float)m_AtlasPlayerMark.GetWidth() / 2.0f,
			fPlayerY - (float)m_AtlasPlayerMark.GetHeight() / 2.0f);
		m_AtlasPlayerMark.SetRotation(fRotation);
	}

	{
		TGuildAreaInfoVectorIterator itor = m_GuildAreaInfoVector.begin();
		for (; itor != m_GuildAreaInfoVector.end(); ++itor)
		{
			TGuildAreaInfo & rInfo = *itor;
			__GlobalPositionToAtlasPosition(rInfo.lx, rInfo.ly, &rInfo.fsxRender, &rInfo.fsyRender);
			__GlobalPositionToAtlasPosition(rInfo.lx+rInfo.lwidth, rInfo.ly+rInfo.lheight, &rInfo.fexRender, &rInfo.feyRender);
		}
	}
}

// W4.5: DX11 atlas rendering (base atlas + marks + waypoints + player + guild flags)
bool CPythonMiniMap::__RenderAtlasDX11()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device)
		return false;

	ID3D11Device* pDevice = pDX11Device->GetDevice();
	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pDevice || !pContext)
		return false;

	if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
		return false;

	UINT uBackBufferWidth = 0;
	UINT uBackBufferHeight = 0;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (0 == uBackBufferWidth || 0 == uBackBufferHeight)
		return false;

	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	if (!pInputLayout || !pVertexShader || !pPixelShader || !pVertexBuffer || !pAlphaBlendState || !pDepthDisableState || !pSamplerState)
		return false;

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const auto PixelToNDCX = [uBackBufferWidth](float fX) -> float
	{
		return (2.0f * fX / static_cast<float>(uBackBufferWidth)) - 1.0f;
	};
	const auto PixelToNDCY = [uBackBufferHeight](float fY) -> float
	{
		return 1.0f - (2.0f * fY / static_cast<float>(uBackBufferHeight));
	};

	const auto RenderQuad = [&](CGraphicImageInstance& rImage, const char* c_szQuadName, float fScreenX, float fScreenY, float fWidth, float fHeight, const D3DXCOLOR& c_rColor, float fRotationDegrees = 0.0f, bool bUseRotation = false) -> bool
	{
		if (rImage.IsEmpty() || fWidth <= 0.0f || fHeight <= 0.0f)
			return false;

		CGraphicImage* pImage = rImage.GetGraphicImagePointer();
		CGraphicTexture* pTexture = rImage.GetTexturePointer();
		if (!pImage || !pTexture)
		{
			static bool s_bLoggedAtlasMissingImageOrTexture = false;
			if (!s_bLoggedAtlasMissingImageOrTexture)
			{
				s_bLoggedAtlasMissingImageOrTexture = true;
				TraceError("DX11_ATLAS_MARK_UV_FAIL reason=missing_image_or_texture mark=%s", c_szQuadName ? c_szQuadName : "unknown");
			}
			return false;
		}

		ID3D11ShaderResourceView* pSRV = pTexture->GetD3D11TextureSRV();
		if (!pSRV)
			return false;

		const int iTextureWidth = pTexture->GetWidth();
		const int iTextureHeight = pTexture->GetHeight();
		if (iTextureWidth <= 0 || iTextureHeight <= 0)
		{
			static bool s_bLoggedAtlasInvalidTextureSize = false;
			if (!s_bLoggedAtlasInvalidTextureSize)
			{
				s_bLoggedAtlasInvalidTextureSize = true;
				TraceError(
					"DX11_ATLAS_MARK_UV_FAIL reason=invalid_texture_size mark=%s width=%d height=%d",
					c_szQuadName ? c_szQuadName : "unknown",
					iTextureWidth,
					iTextureHeight);
			}
			return false;
		}

		const RECT& c_rRect = pImage->GetRectReference();
		const float fInvTexWidth = 1.0f / static_cast<float>(iTextureWidth);
		const float fInvTexHeight = 1.0f / static_cast<float>(iTextureHeight);
		const float fSU = static_cast<float>(c_rRect.left) * fInvTexWidth;
		const float fSV = static_cast<float>(c_rRect.top) * fInvTexHeight;
		const float fEU = static_cast<float>(c_rRect.right) * fInvTexWidth;
		const float fEV = static_cast<float>(c_rRect.bottom) * fInvTexHeight;

		static DWORD s_dwLastAtlasUVHeartbeat = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0 == s_dwLastAtlasUVHeartbeat || (dwNow - s_dwLastAtlasUVHeartbeat) >= 5000u)
		{
			TraceError(
				"DX11_ATLAS_MARK_UV mode=sub_rect mark=%s w=%.1f h=%.1f su=%.4f sv=%.4f eu=%.4f ev=%.4f",
				c_szQuadName ? c_szQuadName : "unknown",
				fWidth,
				fHeight,
				fSU,
				fSV,
				fEU,
				fEV);
			s_dwLastAtlasUVHeartbeat = dwNow;
		}

		SBootstrapVertex aVertices[6];
		if (!bUseRotation)
		{
			const float fX0 = fScreenX;
			const float fY0 = fScreenY;
			const float fX1 = fScreenX + fWidth;
			const float fY1 = fScreenY + fHeight;
			aVertices[0] = { PixelToNDCX(fX0), PixelToNDCY(fY0), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fSV };
			aVertices[1] = { PixelToNDCX(fX0), PixelToNDCY(fY1), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fEV };
			aVertices[2] = { PixelToNDCX(fX1), PixelToNDCY(fY0), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fSV };
			aVertices[3] = { PixelToNDCX(fX1), PixelToNDCY(fY0), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fSV };
			aVertices[4] = { PixelToNDCX(fX0), PixelToNDCY(fY1), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fEV };
			aVertices[5] = { PixelToNDCX(fX1), PixelToNDCY(fY1), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fEV };
		}
		else
		{
			const float fCenterX = fScreenX + fWidth * 0.5f;
			const float fCenterY = fScreenY + fHeight * 0.5f;
			const float fHalfWidth = fWidth * 0.5f;
			const float fHalfHeight = fHeight * 0.5f;
			const float fRotationRad = DirectX::XMConvertToRadians(fRotationDegrees);
			const float fCos = cosf(fRotationRad);
			const float fSin = sinf(fRotationRad);

			const float aCorners[4][2] =
			{
				{ -fHalfWidth, -fHalfHeight },
				{ -fHalfWidth,  fHalfHeight },
				{  fHalfWidth, -fHalfHeight },
				{  fHalfWidth,  fHalfHeight }
			};
			float aRotatedCorners[4][2] = {};
			for (int i = 0; i < 4; ++i)
			{
				const float fX = aCorners[i][0];
				const float fY = aCorners[i][1];
				aRotatedCorners[i][0] = fCenterX + (fX * fCos - fY * fSin);
				aRotatedCorners[i][1] = fCenterY + (fX * fSin + fY * fCos);
			}

			aVertices[0] = { PixelToNDCX(aRotatedCorners[0][0]), PixelToNDCY(aRotatedCorners[0][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fSV };
			aVertices[1] = { PixelToNDCX(aRotatedCorners[1][0]), PixelToNDCY(aRotatedCorners[1][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fEV };
			aVertices[2] = { PixelToNDCX(aRotatedCorners[2][0]), PixelToNDCY(aRotatedCorners[2][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fSV };
			aVertices[3] = { PixelToNDCX(aRotatedCorners[2][0]), PixelToNDCY(aRotatedCorners[2][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fSV };
			aVertices[4] = { PixelToNDCX(aRotatedCorners[1][0]), PixelToNDCY(aRotatedCorners[1][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fSU, fEV };
			aVertices[5] = { PixelToNDCX(aRotatedCorners[3][0]), PixelToNDCY(aRotatedCorners[3][1]), 0.0f, c_rColor.r, c_rColor.g, c_rColor.b, c_rColor.a, fEU, fEV };
		}

		D3D11_MAPPED_SUBRESOURCE mappedResource = {};
		if (FAILED(pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)) || !mappedResource.pData)
			return false;

		memcpy(mappedResource.pData, aVertices, sizeof(aVertices));
		pContext->Unmap(pVertexBuffer, 0);
		pContext->PSSetShaderResources(0, 1, &pSRV);
		pContext->Draw(6, 0);
		return true;
	};

	pDX11Device->BindMainRenderTargets();
	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	const FLOAT afBlendFactor[4] = { 0, 0, 0, 0 };
	pContext->OMSetBlendState(pAlphaBlendState, afBlendFactor, 0xFFFFFFFFu);
	pContext->OMSetDepthStencilState(pDepthDisableState, 0);
	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(pVertexShader, NULL, 0);
	pContext->PSSetShader(pPixelShader, NULL, 0);
	pContext->PSSetSamplers(0, 1, &pSamplerState);

	int iDraws = 0;
	const D3DXCOLOR cWhite(1.0f, 1.0f, 1.0f, 1.0f);

	// Atlas base image
	if (!m_AtlasImageInstance.IsEmpty())
	{
		if (RenderQuad(
			m_AtlasImageInstance,
			"atlas_base",
			m_fAtlasScreenX,
			m_fAtlasScreenY,
			m_fAtlasImageSizeX,
			m_fAtlasImageSizeY,
			cWhite))
		{
			++iDraws;
		}
	}

	const float fWhiteMarkWidth = static_cast<float>(m_WhiteMark.GetWidth());
	const float fWhiteMarkHeight = static_cast<float>(m_WhiteMark.GetHeight());
	const D3DXCOLOR cNPCColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_NPC);
	const D3DXCOLOR cWarpColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_WARP);
	const D3DXCOLOR cWaypointColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_WAYPOINT);

	// NPC marks
	for (TAtlasMarkInfoVectorIterator it = m_AtlasNPCInfoVector.begin(); it != m_AtlasNPCInfoVector.end(); ++it)
	{
		TAtlasMarkInfo& rAtlasMarkInfo = *it;
		if (RenderQuad(
			m_WhiteMark,
			"atlas_npc",
			m_fAtlasScreenX + rAtlasMarkInfo.m_fScreenX,
			m_fAtlasScreenY + rAtlasMarkInfo.m_fScreenY,
			fWhiteMarkWidth,
			fWhiteMarkHeight,
			cNPCColor))
		{
			++iDraws;
		}
	}

	// Warp marks
	for (TAtlasMarkInfoVectorIterator it = m_AtlasWarpInfoVector.begin(); it != m_AtlasWarpInfoVector.end(); ++it)
	{
		TAtlasMarkInfo& rAtlasMarkInfo = *it;
		if (RenderQuad(
			m_WhiteMark,
			"atlas_warp",
			m_fAtlasScreenX + rAtlasMarkInfo.m_fScreenX,
			m_fAtlasScreenY + rAtlasMarkInfo.m_fScreenY,
			fWhiteMarkWidth,
			fWhiteMarkHeight,
			cWarpColor))
		{
			++iDraws;
		}
	}

	// Waypoint marks (target + normal)
	for (TAtlasMarkInfoVectorIterator it = m_AtlasWayPointInfoVector.begin(); it != m_AtlasWayPointInfoVector.end(); ++it)
	{
		TAtlasMarkInfo& rAtlasMarkInfo = *it;
		if (rAtlasMarkInfo.m_fScreenX <= 0.0f || rAtlasMarkInfo.m_fScreenY <= 0.0f)
			continue;

		CGraphicImageInstance* pWaypointInstance = nullptr;
		if (TYPE_TARGET == rAtlasMarkInfo.m_byType)
		{
			const int iNum = (ELTimer_GetMSec() / 67) % MINI_WAYPOINT_IMAGE_COUNT;
			pWaypointInstance = &m_MiniWayPointGraphicImageInstances[iNum];
		}
		else
		{
			const int iNum = (ELTimer_GetMSec() / 67) % WAYPOINT_IMAGE_COUNT;
			pWaypointInstance = &m_WayPointGraphicImageInstances[iNum];
		}

		if (!pWaypointInstance || pWaypointInstance->IsEmpty())
			continue;

		const float fCenterX = m_fAtlasScreenX + rAtlasMarkInfo.m_fScreenX + fWhiteMarkWidth * 0.5f;
		const float fCenterY = m_fAtlasScreenY + rAtlasMarkInfo.m_fScreenY + fWhiteMarkHeight * 0.5f;
		const float fWidth = static_cast<float>(pWaypointInstance->GetWidth());
		const float fHeight = static_cast<float>(pWaypointInstance->GetHeight());
		if (RenderQuad(
			*pWaypointInstance,
			TYPE_TARGET == rAtlasMarkInfo.m_byType ? "atlas_target_waypoint" : "atlas_waypoint",
			fCenterX - fWidth * 0.5f,
			fCenterY - fHeight * 0.5f,
			fWidth,
			fHeight,
			cWaypointColor))
		{
			++iDraws;
		}
	}

	// Player mark (blink + rotation)
	if ((ELTimer_GetMSec() / 500) % 2)
	{
		CInstanceBase* pkInst = CPythonCharacterManager::Instance().GetMainInstancePtr();
		if (pkInst && !m_AtlasPlayerMark.IsEmpty())
		{
			TPixelPosition kInstPos;
			pkInst->NEW_GetPixelPosition(&kInstPos);
			float fPlayerX = 0.0f;
			float fPlayerY = 0.0f;
			__GlobalPositionToAtlasPosition(static_cast<long>(kInstPos.x), static_cast<long>(kInstPos.y), &fPlayerX, &fPlayerY);

			float fRotation = 540.0f - pkInst->GetRotation();
			while (fRotation > 360.0f)
				fRotation -= 360.0f;
			while (fRotation < 0.0f)
				fRotation += 360.0f;

			const float fWidth = static_cast<float>(m_AtlasPlayerMark.GetWidth());
			const float fHeight = static_cast<float>(m_AtlasPlayerMark.GetHeight());
			if (RenderQuad(
				m_AtlasPlayerMark,
				"atlas_player",
				m_fAtlasScreenX + fPlayerX - fWidth * 0.5f,
				m_fAtlasScreenY + fPlayerY - fHeight * 0.5f,
				fWidth,
				fHeight,
				cWhite,
				fRotation,
				true))
			{
				++iDraws;
			}
		}
	}

	// Guild flags
	if (!m_GuildAreaFlagImageInstance.IsEmpty())
	{
		const float fWidth = static_cast<float>(m_GuildAreaFlagImageInstance.GetWidth());
		const float fHeight = static_cast<float>(m_GuildAreaFlagImageInstance.GetHeight());
		for (TGuildAreaInfoVectorIterator it = m_GuildAreaInfoVector.begin(); it != m_GuildAreaInfoVector.end(); ++it)
		{
			TGuildAreaInfo& rInfo = *it;
			const float fX = m_fAtlasScreenX + (rInfo.fsxRender + rInfo.fexRender) * 0.5f - fWidth * 0.5f;
			const float fY = m_fAtlasScreenY + (rInfo.fsyRender + rInfo.feyRender) * 0.5f - fHeight * 0.5f;
			if (RenderQuad(m_GuildAreaFlagImageInstance, "atlas_guild_flag", fX, fY, fWidth, fHeight, cWhite))
				++iDraws;
		}
	}

	ID3D11ShaderResourceView* pNullSRV = NULL;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);
	pContext->OMSetDepthStencilState(NULL, 0);

	if (iDraws > 0)
	{
		static DWORD s_dwLastAtlasDX11Heartbeat = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0 == s_dwLastAtlasDX11Heartbeat || dwNow - s_dwLastAtlasDX11Heartbeat >= 5000u)
		{
			TraceError("DX11_MINIMAP_ATLAS_DX11_ACTIVE draws=%d", iDraws);
			s_dwLastAtlasDX11Heartbeat = dwNow;
		}
		return true;
	}

	static bool s_bLoggedAtlasDX11Fail = false;
	if (!s_bLoggedAtlasDX11Fail)
	{
		s_bLoggedAtlasDX11Fail = true;
		TraceError("DX11_MINIMAP_ATLAS_DX11_FAIL reason=no_draws");
	}
	return false;
}

void CPythonMiniMap::RenderAtlas(float fScreenX, float fScreenY)
{
	if (!m_bShowAtlas)
		return;

	if (m_fAtlasScreenX != fScreenX || m_fAtlasScreenY != fScreenY)
	{
		m_matWorldAtlas._41 = fScreenX;
		m_matWorldAtlas._42 = fScreenY;
		m_fAtlasScreenX = fScreenX;
		m_fAtlasScreenY = fScreenY;
	}

	// W4.6: DX11-only atlas rendering (strict mode enforced)
	if (__RenderAtlasDX11())
	{
		// M2-USERINTERFACE-HALF-B: Throttled telemetry for minimap atlas parity
		static DWORD s_dwMiniMapAtlasOK = 0;
		static DWORD s_dwMiniMapAtlasFail = 0;
		static DWORD s_dwMiniMapAtlasTelemetryTick = 0;
		++s_dwMiniMapAtlasOK;

		const DWORD dwCurrentTick = ELTimer_GetMSec();
		if (0 == s_dwMiniMapAtlasTelemetryTick || (dwCurrentTick - s_dwMiniMapAtlasTelemetryTick) >= 15000)
		{
			TraceError("DX11_MINIMAP_ATLAS_PARITY atlas_ok=%u atlas_fail=%u interval_ms=15000",
				s_dwMiniMapAtlasOK, s_dwMiniMapAtlasFail);
			s_dwMiniMapAtlasTelemetryTick = dwCurrentTick;
		}
		return;
	}
	else
	{
		static DWORD s_dwMiniMapAtlasFail = 0;
		++s_dwMiniMapAtlasFail;
	}

	// DX11 atlas render failed - log blocker and return (no DX9 fallback)
	static bool s_bLoggedAtlasBlock = false;
	if (!s_bLoggedAtlasBlock)
	{
		TraceError("DX11_MINIMAP_ATLAS_RENDER_FAILED reason=dx11_atlas_draw_failed");
		s_bLoggedAtlasBlock = true;
	}
	return;
}

void CPythonMiniMap::__AtlasPositionToGlobalPosition(float fAtlasX, float fAtlasY, float* pfWorldX, float* pfWorldY) const
{
	float fReverseScale = 1.0f / GetAtlasUniformScale();
	
	*pfWorldX = (fAtlasX - m_fAtlasOffsetX) * fReverseScale;
	*pfWorldY = (fAtlasY - m_fAtlasOffsetY) * fReverseScale;
}

bool CPythonMiniMap::GetAtlasInfo(float fScreenX, float fScreenY, std::string & rReturnString, float * pReturnPosX, float * pReturnPosY, DWORD * pdwTextColor, DWORD * pdwGuildID)
{
	float fLocalX = fScreenX - m_fAtlasScreenX;
	float fLocalY = fScreenY - m_fAtlasScreenY;
	
	float fRealX, fRealY;
	__AtlasPositionToGlobalPosition(fLocalX, fLocalY, &fRealX, &fRealY);

#ifdef ENABLE_MINIMAP_TELEPORT_CLICK
	*pReturnPosX = fRealX;
	*pReturnPosY = fRealY;
#endif
	
	float fReverseScale = 1.0f / GetAtlasUniformScale();
	float fCheckWidth = fReverseScale * 5.0f;
	float fCheckHeight = fReverseScale * 5.0f;
	
	CInstanceBase * pkInst = CPythonCharacterManager::Instance().GetMainInstancePtr();

	if (pkInst)
	{
		TPixelPosition kInstPos;
		pkInst->NEW_GetPixelPosition(&kInstPos);

		if (kInstPos.x-fCheckWidth<fRealX && kInstPos.x+fCheckWidth>fRealX && 
			kInstPos.y-fCheckHeight<fRealY && kInstPos.y+fCheckHeight>fRealY)
		{
			rReturnString = pkInst->GetNameString();
			*pReturnPosX = kInstPos.x;
			*pReturnPosY = kInstPos.y;
			*pdwTextColor = pkInst->GetNameColor();
			return true;		
		}
	}

	m_AtlasMarkInfoVectorIterator = m_AtlasNPCInfoVector.begin();
	while (m_AtlasMarkInfoVectorIterator != m_AtlasNPCInfoVector.end())
	{
		TAtlasMarkInfo & rAtlasMarkInfo = *m_AtlasMarkInfoVectorIterator;

		if (rAtlasMarkInfo.m_fX-fCheckWidth/2<fRealX && rAtlasMarkInfo.m_fX+fCheckWidth>fRealX && 
			rAtlasMarkInfo.m_fY-fCheckWidth/2<fRealY && rAtlasMarkInfo.m_fY+fCheckHeight>fRealY)		
		{
			rReturnString = rAtlasMarkInfo.m_strText;
			*pReturnPosX = rAtlasMarkInfo.m_fX;
			*pReturnPosY = rAtlasMarkInfo.m_fY;
			*pdwTextColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_NPC);//m_MarkTypeToColorMap[rAtlasMarkInfo.m_byType];
			return true;
		}
		++m_AtlasMarkInfoVectorIterator;
	}

	m_AtlasMarkInfoVectorIterator = m_AtlasWarpInfoVector.begin();
	while (m_AtlasMarkInfoVectorIterator != m_AtlasWarpInfoVector.end())
	{
		TAtlasMarkInfo & rAtlasMarkInfo = *m_AtlasMarkInfoVectorIterator;
		if (rAtlasMarkInfo.m_fX-fCheckWidth/2<fRealX && rAtlasMarkInfo.m_fX+fCheckWidth>fRealX && 
			rAtlasMarkInfo.m_fY-fCheckWidth/2<fRealY && rAtlasMarkInfo.m_fY+fCheckHeight>fRealY)
		{
			rReturnString = rAtlasMarkInfo.m_strText;
			*pReturnPosX = rAtlasMarkInfo.m_fX;
			*pReturnPosY = rAtlasMarkInfo.m_fY;
			*pdwTextColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_WARP);//m_MarkTypeToColorMap[rAtlasMarkInfo.m_byType];
			return true;
		}
		++m_AtlasMarkInfoVectorIterator;
	}

	m_AtlasMarkInfoVectorIterator = m_AtlasWayPointInfoVector.begin();
	while (m_AtlasMarkInfoVectorIterator != m_AtlasWayPointInfoVector.end())
	{
		TAtlasMarkInfo & rAtlasMarkInfo = *m_AtlasMarkInfoVectorIterator;
		if (rAtlasMarkInfo.m_fScreenX > 0.0f)
		if (rAtlasMarkInfo.m_fScreenY > 0.0f)
		if (rAtlasMarkInfo.m_fX-fCheckWidth/2<fRealX && rAtlasMarkInfo.m_fX+fCheckWidth>fRealX && 
			rAtlasMarkInfo.m_fY-fCheckWidth/2<fRealY && rAtlasMarkInfo.m_fY+fCheckHeight>fRealY)		
		{
			rReturnString = rAtlasMarkInfo.m_strText;
			*pReturnPosX = rAtlasMarkInfo.m_fX;
			*pReturnPosY = rAtlasMarkInfo.m_fY;
			*pdwTextColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_WAYPOINT);//m_MarkTypeToColorMap[rAtlasMarkInfo.m_byType];
			return true;
		}
		++m_AtlasMarkInfoVectorIterator;
	}

	TGuildAreaInfoVector::iterator itor = m_GuildAreaInfoVector.begin();
	for (; itor!=m_GuildAreaInfoVector.end(); ++itor)
	{
		TGuildAreaInfo & rInfo = *itor;
		if (fScreenX - m_fAtlasScreenX >= rInfo.fsxRender)
		if (fScreenY - m_fAtlasScreenY >= rInfo.fsyRender)
		if (fScreenX - m_fAtlasScreenX <= rInfo.fexRender)
		if (fScreenY - m_fAtlasScreenY <= rInfo.feyRender)
		{
			if (CPythonGuild::Instance().GetGuildName(rInfo.dwGuildID, &rReturnString))
			{
				*pdwGuildID = rInfo.dwGuildID;
			}
			else
			{
				rReturnString = "empty_guild_area";
			}

			*pReturnPosX = rInfo.lx + rInfo.lwidth/2;
			*pReturnPosY = rInfo.ly + rInfo.lheight/2;
			*pdwTextColor = CInstanceBase::GetIndexedNameColor(CInstanceBase::NAMECOLOR_PARTY);
			return true;
		}
	}

	return false;
}

bool CPythonMiniMap::GetAtlasSize(float * pfSizeX, float * pfSizeY)
{
	CPythonBackground& rkBG=CPythonBackground::Instance();
	if (!rkBG.IsMapOutdoor())
		return false;

	*pfSizeX = m_fAtlasImageSizeX;
	*pfSizeY = m_fAtlasImageSizeY;

	return true;
}

// Atlas
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// WayPoint
void CPythonMiniMap::AddWayPoint(BYTE byType, DWORD dwID, float fX, float fY, std::string strText, DWORD dwChrVID)
{
	m_AtlasMarkInfoVectorIterator = m_AtlasWayPointInfoVector.begin();
	while (m_AtlasMarkInfoVectorIterator != m_AtlasWayPointInfoVector.end())
	{
		TAtlasMarkInfo & rAtlasMarkInfo = *m_AtlasMarkInfoVectorIterator;
		if (rAtlasMarkInfo.m_dwID == dwID)
			return;
		++m_AtlasMarkInfoVectorIterator;
	}

	TAtlasMarkInfo aAtlasMarkInfo;
	aAtlasMarkInfo.m_byType = byType;
	aAtlasMarkInfo.m_dwID = dwID;
	aAtlasMarkInfo.m_fX = fX;
	aAtlasMarkInfo.m_fY = fY;
	aAtlasMarkInfo.m_fScreenX = 0.0f;
	aAtlasMarkInfo.m_fScreenY = 0.0f;
	aAtlasMarkInfo.m_fMiniMapX = 0.0f;
	aAtlasMarkInfo.m_fMiniMapY = 0.0f;
	aAtlasMarkInfo.m_strText = strText;
	aAtlasMarkInfo.m_dwChrVID = dwChrVID;
	__UpdateWayPoint(&aAtlasMarkInfo, fX, fY);
	m_AtlasWayPointInfoVector.push_back(aAtlasMarkInfo);
	
}

void CPythonMiniMap::RemoveWayPoint(DWORD dwID)
{
	m_AtlasMarkInfoVectorIterator = m_AtlasWayPointInfoVector.begin();
	while (m_AtlasMarkInfoVectorIterator != m_AtlasWayPointInfoVector.end())
	{
		TAtlasMarkInfo & rAtlasMarkInfo = *m_AtlasMarkInfoVectorIterator;
		if (rAtlasMarkInfo.m_dwID == dwID)
		{
			m_AtlasMarkInfoVectorIterator = m_AtlasWayPointInfoVector.erase(m_AtlasMarkInfoVectorIterator);
			return;
		}
		++m_AtlasMarkInfoVectorIterator;
	}
}

bool CPythonMiniMap::__GetWayPoint(DWORD dwID, TAtlasMarkInfo ** ppkInfo)
{
	TAtlasMarkInfoVectorIterator itor = m_AtlasWayPointInfoVector.begin();
	for (; itor != m_AtlasWayPointInfoVector.end(); ++itor)
	{
		TAtlasMarkInfo & rInfo = *itor;
		if (dwID == rInfo.m_dwID)
		{
			*ppkInfo = &rInfo;
			return true;
		}
	}

	return false;
}

void CPythonMiniMap::__UpdateWayPoint(TAtlasMarkInfo * pkInfo, int ix, int iy)
{
	pkInfo->m_fX = float(ix);
	pkInfo->m_fY = float(iy);
	__GlobalPositionToAtlasPosition(ix, iy, &pkInfo->m_fScreenX, &pkInfo->m_fScreenY);
}

// WayPoint
//////////////////////////////////////////////////////////////////////////

void CPythonMiniMap::__RenderWayPointMark(int ixCenter, int iyCenter)
{
	int iNum = (ELTimer_GetMSec() / 67) % WAYPOINT_IMAGE_COUNT;

	CGraphicImageInstance & rInstance = m_WayPointGraphicImageInstances[iNum];
	rInstance.SetPosition(ixCenter - rInstance.GetWidth()/2, iyCenter - rInstance.GetHeight()/2);
	rInstance.Render();
}

void CPythonMiniMap::__RenderMiniWayPointMark(int ixCenter, int iyCenter)
{
	int iNum = (ELTimer_GetMSec() / 67) % MINI_WAYPOINT_IMAGE_COUNT;

	CGraphicImageInstance & rInstance = m_MiniWayPointGraphicImageInstances[iNum];
	rInstance.SetPosition(ixCenter - rInstance.GetWidth()/2, iyCenter - rInstance.GetHeight()/2);
	rInstance.Render();
}

void CPythonMiniMap::__RenderTargetMark(int ixCenter, int iyCenter)
{
	int iNum = (ELTimer_GetMSec() / 80) % TARGET_MARK_IMAGE_COUNT;

	CGraphicImageInstance & rInstance = m_TargetMarkGraphicImageInstances[iNum];
	rInstance.SetPosition(ixCenter - rInstance.GetWidth()/2, iyCenter - rInstance.GetHeight()/2);
	rInstance.Render();
}

void CPythonMiniMap::AddSignalPoint(float fX, float fY)
{
	static unsigned int g_id = 255;

	TSignalPoint sp;
	sp.id = g_id;
	sp.v2Pos.x = fX;
	sp.v2Pos.y = fY;

	m_SignalPointVector.push_back(sp);

	AddWayPoint(TYPE_WAYPOINT, g_id, fX, fY, "");

	g_id++;
}

void CPythonMiniMap::ClearAllSignalPoint()
{
	std::vector<TSignalPoint>::iterator it;
	for(it = m_SignalPointVector.begin();it!=m_SignalPointVector.end();++it)
	{
		RemoveWayPoint(it->id);
	}
	m_SignalPointVector.clear();
}

void CPythonMiniMap::RegisterAtlasWindow(PyObject* poHandler)
{
	m_poHandler = poHandler;
}

void CPythonMiniMap::UnregisterAtlasWindow()
{
	m_poHandler = 0;
}

void CPythonMiniMap::OpenAtlasWindow()
{
	if (m_poHandler)
	{
		PyCallClassMemberFunc(m_poHandler,"Show", Py_BuildValue("()"));
	}
}

void CPythonMiniMap::SetAtlasCenterPosition(int x, int y)
{
	if (m_poHandler)
	{
		//int sw = UI::CWindowManager::Instance().GetScreenWidth();
		//int sh = UI::CWindowManager::Instance().GetScreenHeight();
		//PyCallClassMemberFunc(m_poHandler,"SetPosition", Py_BuildValue("(ii)",sw/2+x,sh/2+y));
		PyCallClassMemberFunc(m_poHandler,"SetCenterPositionAdjust", Py_BuildValue("(ii)",x,y));
	}
}

bool CPythonMiniMap::IsAtlas()
{
	return m_bAtlas;
}

void CPythonMiniMap::ShowAtlas()
{
	m_bShowAtlas=true;
}

void CPythonMiniMap::HideAtlas()
{
	m_bShowAtlas=false;
}
		
bool CPythonMiniMap::CanShowAtlas()
{
	return m_bShowAtlas;
}

bool CPythonMiniMap::CanShow()
{
	return m_bShow;
}

void CPythonMiniMap::Show()
{
	m_bShow=true;	
}

void CPythonMiniMap::Hide()
{
	m_bShow=false;
}
		
void CPythonMiniMap::__Initialize()
{
	m_poHandler = 0;

	SetMiniMapSize(128.0f, 128.0f);

	m_fScale = 2.0f;

	m_fCenterX = m_fWidth * 0.5f;
	m_fCenterY = m_fHeight * 0.5f;

	m_fScreenX = 0.0f;
	m_fScreenY = 0.0f;

	m_fAtlasScreenX = 0.0f;
	m_fAtlasScreenY = 0.0f;
	
	m_fAtlasOffsetX = 0.0f;
	m_fAtlasOffsetY = 0.0f;

	m_dwAtlasBaseX = 0;
	m_dwAtlasBaseY = 0;

	m_fAtlasMaxX = 0.0f;
	m_fAtlasMaxY = 0.0f;
	
	m_fAtlasImageSizeX = 0.0f;
	m_fAtlasImageSizeY = 0.0f;

	m_bAtlas = false;
	
	m_bShow = false;
	m_bShowAtlas = false;
	for (BYTE byTerrainNum = 0; byTerrainNum < AROUND_AREA_NUM; ++byTerrainNum)
		m_apMiniMapTexture[byTerrainNum] = NULL;

	m_matIdentity = DirectX::SimpleMath::Matrix::Identity; // M2-USERINTERFACE-DX11-NATIVE-01: Migrated from D3DXMatrixIdentity
	m_matWorld = DirectX::SimpleMath::Matrix::Identity; // M2-USERINTERFACE-DX11-NATIVE-01: Migrated from D3DXMatrixIdentity
	m_matMiniMapCover = DirectX::SimpleMath::Matrix::Identity; // M2-USERINTERFACE-DX11-NATIVE-01: Migrated from D3DXMatrixIdentity
	m_matWorldAtlas = DirectX::SimpleMath::Matrix::Identity; // M2-USERINTERFACE-DX11-NATIVE-01: Migrated from D3DXMatrixIdentity

	m_pDX11MiniMapMaskPixelShader = NULL;
	m_pDX11MiniMapMaskConstantBuffer = NULL;
	m_bDX11MiniMapMaskResourcesReady = false;
	m_bDX11MiniMapMaskResourcesFailed = false;
}

void CPythonMiniMap::Destroy()
{
	__DestroyDX11MiniMapMaskResources();

	ClearAllSignalPoint();
	m_poHandler = 0;

	m_VertexBuffer.Destroy();
	m_IndexBuffer.Destroy();

	m_PlayerMark.Destroy();

	m_MiniMapFilterGraphicImageInstance.Destroy();
	m_MiniMapCameraraphicImageInstance.Destroy();

	m_AtlasWayPointInfoVector.clear();
	m_AtlasImageInstance.Destroy();
	m_AtlasPlayerMark.Destroy();
	m_WhiteMark.Destroy();

	for (int i = 0; i < MINI_WAYPOINT_IMAGE_COUNT; ++i)
		m_MiniWayPointGraphicImageInstances[i].Destroy();
	for (int j = 0; j < WAYPOINT_IMAGE_COUNT; ++j)
		m_WayPointGraphicImageInstances[j].Destroy();
	for (int k = 0; k < TARGET_MARK_IMAGE_COUNT; ++k)
		m_TargetMarkGraphicImageInstances[k].Destroy();

	m_GuildAreaFlagImageInstance.Destroy();

	__Initialize();
}

CPythonMiniMap::CPythonMiniMap()
{
	__Initialize();
}

CPythonMiniMap::~CPythonMiniMap()
{
	Destroy();
}
