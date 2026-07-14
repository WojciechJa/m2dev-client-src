#include "StdAfx.h"
#include "MapOutdoor.h"
#include "TerrainPatch.h"
#include "AreaTerrain.h"
#include "TerrainQuadtree.h"

#include "EterLib/Camera.h"
#include "EterLib/GrpDeviceDX11.h"
#include "EterLib/GrpTextureDX11.h"
#include "EterLib/GrpShaderCacheDX11.h"
#include "EterLib/ResourceManager.h"
#include "EterGrnLib/ModelInstance.h"
#include "UserInterface/config.h"
#include <d3dcompiler.h>
#include <array>


#define MAX_RENDER_SPALT 150


namespace
{
	struct CMapOutdoor_LessThingInstancePtrRenderOrder
	{
		bool operator() (CGraphicThingInstance* pkLeft, CGraphicThingInstance* pkRight)
		{
			CCamera* pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
			const DirectX::SimpleMath::Vector3& c_rv3CameraPos = pCurrentCamera->GetEye();
			const DirectX::SimpleMath::Vector3& c_v3LeftPos = pkLeft->GetPosition();
			const DirectX::SimpleMath::Vector3& c_v3RightPos = pkRight->GetPosition();
			const DirectX::SimpleMath::Vector3 vv = c_rv3CameraPos - c_v3RightPos;
			const DirectX::SimpleMath::Vector3 vv2 = c_rv3CameraPos - c_v3LeftPos;
			return vv2.LengthSquared() < vv.LengthSquared();
		}
	};

	struct DX11ObjectShaderCB
	{
		D3DXMATRIX matWorld;
		D3DXMATRIX matViewProj;
		D3DXVECTOR4 vLightDir;
		D3DXVECTOR4 vAmbient;
		D3DXVECTOR4 vViewPosAndSpecPower;
		D3DXVECTOR4 vSpecularColorAndEnable;
	};

	struct DX11TerrainShaderCB
	{
		D3DXMATRIX matWorldViewProj;
		D3DXVECTOR4 vUVTransform;
		D3DXVECTOR4 vAlphaUVTransform;
		D3DXVECTOR4 vLightDir;
		D3DXVECTOR4 vAmbient;
	};

	struct DX11WaterShaderCB
	{
		D3DXMATRIX matWorldViewProj;
		D3DXVECTOR4 vWaterParams;
		D3DXVECTOR4 vTint;
		D3DXVECTOR4 vDebugParams;
	};

	struct DX11ShadowFrameCB
	{
		D3DXMATRIX matLightViewProj[3];
		D3DXVECTOR4 vCascadeSplits;
		D3DXVECTOR4 vLightDir;
	};

	struct DX11ShadowCasterStateScope
	{
		ID3D11DeviceContext* pContext;
		ID3D11RenderTargetView* pPrevRTV;
		ID3D11DepthStencilView* pPrevDSV;
		ID3D11RasterizerState* pPrevRaster;
		ID3D11DepthStencilState* pPrevDepthState;
		ID3D11BlendState* pPrevBlendState;
		ID3D11VertexShader* pPrevVS;
		ID3D11PixelShader* pPrevPS;
		ID3D11Buffer* pPrevVSCB0;
		ID3D11Buffer* pPrevPSCB0;
		ID3D11Buffer* pPrevPSCB3;
		ID3D11SamplerState* pPrevPSSampler6;
		ID3D11ShaderResourceView* pPrevPSSRV6;
		D3D11_VIEWPORT kPrevViewport;
		bool bHasViewport;
		FLOAT afPrevBlendFactor[4];
		UINT uPrevSampleMask;
		UINT uPrevStencilRef;

		explicit DX11ShadowCasterStateScope(ID3D11DeviceContext* pInContext)
			: pContext(pInContext)
			, pPrevRTV(nullptr)
			, pPrevDSV(nullptr)
			, pPrevRaster(nullptr)
			, pPrevDepthState(nullptr)
			, pPrevBlendState(nullptr)
			, pPrevVS(nullptr)
			, pPrevPS(nullptr)
			, pPrevVSCB0(nullptr)
			, pPrevPSCB0(nullptr)
			, pPrevPSCB3(nullptr)
			, pPrevPSSampler6(nullptr)
			, pPrevPSSRV6(nullptr)
			, bHasViewport(false)
			, uPrevSampleMask(0u)
			, uPrevStencilRef(0u)
		{
			afPrevBlendFactor[0] = 0.0f;
			afPrevBlendFactor[1] = 0.0f;
			afPrevBlendFactor[2] = 0.0f;
			afPrevBlendFactor[3] = 0.0f;

			if (!pContext)
				return;

			pContext->OMGetRenderTargets(1u, &pPrevRTV, &pPrevDSV);
			pContext->OMGetDepthStencilState(&pPrevDepthState, &uPrevStencilRef);
			pContext->OMGetBlendState(&pPrevBlendState, afPrevBlendFactor, &uPrevSampleMask);
			pContext->RSGetState(&pPrevRaster);
			pContext->VSGetShader(&pPrevVS, nullptr, nullptr);
			pContext->PSGetShader(&pPrevPS, nullptr, nullptr);
			pContext->VSGetConstantBuffers(0u, 1u, &pPrevVSCB0);
			pContext->PSGetConstantBuffers(0u, 1u, &pPrevPSCB0);
			pContext->PSGetConstantBuffers(3u, 1u, &pPrevPSCB3);
			pContext->PSGetSamplers(6u, 1u, &pPrevPSSampler6);
			pContext->PSGetShaderResources(6u, 1u, &pPrevPSSRV6);

			UINT uViewportCount = 1u;
			pContext->RSGetViewports(&uViewportCount, &kPrevViewport);
			if (uViewportCount > 0u)
				bHasViewport = true;
		}

		~DX11ShadowCasterStateScope()
		{
			if (!pContext)
				return;

			pContext->OMSetRenderTargets(1u, &pPrevRTV, pPrevDSV);
			pContext->OMSetDepthStencilState(pPrevDepthState, uPrevStencilRef);
			pContext->OMSetBlendState(pPrevBlendState, afPrevBlendFactor, uPrevSampleMask);
			pContext->RSSetState(pPrevRaster);
			if (bHasViewport)
				pContext->RSSetViewports(1u, &kPrevViewport);
			pContext->VSSetShader(pPrevVS, nullptr, 0u);
			pContext->PSSetShader(pPrevPS, nullptr, 0u);
			pContext->VSSetConstantBuffers(0u, 1u, &pPrevVSCB0);
			pContext->PSSetConstantBuffers(0u, 1u, &pPrevPSCB0);
			pContext->PSSetConstantBuffers(3u, 1u, &pPrevPSCB3);
			pContext->PSSetSamplers(6u, 1u, &pPrevPSSampler6);
			pContext->PSSetShaderResources(6u, 1u, &pPrevPSSRV6);

			safe_release(pPrevRTV);
			safe_release(pPrevDSV);
			safe_release(pPrevRaster);
			safe_release(pPrevDepthState);
			safe_release(pPrevBlendState);
			safe_release(pPrevVS);
			safe_release(pPrevPS);
			safe_release(pPrevVSCB0);
			safe_release(pPrevPSCB0);
			safe_release(pPrevPSCB3);
			safe_release(pPrevPSSampler6);
			safe_release(pPrevPSSRV6);
		}
	};

	struct DX11WaterPassStateScope
	{
		ID3D11DeviceContext* pContext;
		ID3D11BlendState* pPrevBlendState;
		ID3D11DepthStencilState* pPrevDepthState;
		ID3D11RasterizerState* pPrevRasterState;
		ID3D11InputLayout* pPrevInputLayout;
		ID3D11VertexShader* pPrevVS;
		ID3D11PixelShader* pPrevPS;
		ID3D11Buffer* pPrevVSConst;
		ID3D11Buffer* pPrevPSConst;
		ID3D11SamplerState* pPrevSampler;
		ID3D11ShaderResourceView* pPrevSRV;
		UINT uPrevSampleMask;
		UINT uPrevStencilRef;
		D3D11_PRIMITIVE_TOPOLOGY ePrevTopology;
		UINT uPrevVBStride;
		UINT uPrevVBOffset;
		ID3D11Buffer* pPrevVB;

		explicit DX11WaterPassStateScope(ID3D11DeviceContext* pInContext)
			: pContext(pInContext)
			, pPrevBlendState(nullptr)
			, pPrevDepthState(nullptr)
			, pPrevRasterState(nullptr)
			, pPrevInputLayout(nullptr)
			, pPrevVS(nullptr)
			, pPrevPS(nullptr)
			, pPrevVSConst(nullptr)
			, pPrevPSConst(nullptr)
			, pPrevSampler(nullptr)
			, pPrevSRV(nullptr)
			, uPrevSampleMask(0u)
			, uPrevStencilRef(0u)
			, ePrevTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
			, uPrevVBStride(0u)
			, uPrevVBOffset(0u)
			, pPrevVB(nullptr)
		{
			if (!pContext)
				return;

			float afBlend[4] = {0.0f, 0.0f, 0.0f, 0.0f};
			pContext->OMGetBlendState(&pPrevBlendState, afBlend, &uPrevSampleMask);
			pContext->OMGetDepthStencilState(&pPrevDepthState, &uPrevStencilRef);
			pContext->RSGetState(&pPrevRasterState);
			pContext->IAGetInputLayout(&pPrevInputLayout);
			pContext->IAGetPrimitiveTopology(&ePrevTopology);
			pContext->IAGetVertexBuffers(0u, 1u, &pPrevVB, &uPrevVBStride, &uPrevVBOffset);
			pContext->VSGetShader(&pPrevVS, nullptr, nullptr);
			pContext->PSGetShader(&pPrevPS, nullptr, nullptr);
			pContext->VSGetConstantBuffers(0u, 1u, &pPrevVSConst);
			pContext->PSGetConstantBuffers(0u, 1u, &pPrevPSConst);
			pContext->PSGetSamplers(0u, 1u, &pPrevSampler);
			pContext->PSGetShaderResources(0u, 1u, &pPrevSRV);
		}

		~DX11WaterPassStateScope()
		{
			if (!pContext)
				return;

			pContext->PSSetShaderResources(0u, 1u, &pPrevSRV);
			pContext->PSSetSamplers(0u, 1u, &pPrevSampler);
			pContext->VSSetConstantBuffers(0u, 1u, &pPrevVSConst);
			pContext->PSSetConstantBuffers(0u, 1u, &pPrevPSConst);
			pContext->VSSetShader(pPrevVS, nullptr, 0u);
			pContext->PSSetShader(pPrevPS, nullptr, 0u);
			pContext->IASetInputLayout(pPrevInputLayout);
			pContext->IASetPrimitiveTopology(ePrevTopology);
			pContext->IASetVertexBuffers(0u, 1u, &pPrevVB, &uPrevVBStride, &uPrevVBOffset);
			pContext->OMSetBlendState(pPrevBlendState, nullptr, uPrevSampleMask);
			pContext->OMSetDepthStencilState(pPrevDepthState, uPrevStencilRef);
			pContext->RSSetState(pPrevRasterState);

			safe_release(pPrevBlendState);
			safe_release(pPrevDepthState);
			safe_release(pPrevRasterState);
			safe_release(pPrevInputLayout);
			safe_release(pPrevVS);
			safe_release(pPrevPS);
			safe_release(pPrevVSConst);
			safe_release(pPrevPSConst);
			safe_release(pPrevSampler);
			safe_release(pPrevSRV);
			safe_release(pPrevVB);
		}
	};

	inline uint64_t MakeSplatAlphaCacheKey(const CTerrain* pTerrain, DWORD dwSplatIndex)
	{
		const uint64_t uTerrainPart = (reinterpret_cast<uint64_t>(pTerrain) >> 4);
		return (uTerrainPart << 8) | static_cast<uint64_t>(dwSplatIndex & 0xFFu);
	}

	inline D3D11_PRIMITIVE_TOPOLOGY ToDX11Topology(GrpPrimitiveType ePrimitiveType)
	{
	return (GRP_PT_TRIANGLESTRIP == ePrimitiveType) ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}

	inline UINT GetIndexCountFromPrimitiveCount(WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType)
	{
	if (GRP_PT_TRIANGLESTRIP == ePrimitiveType)
			return static_cast<UINT>(wPrimitiveCount + 2u);
		return static_cast<UINT>(wPrimitiveCount * 3u);
	}

	HRESULT CompileDX11WorldShader(
		const char* szSource,
		const char* szEntry,
		const char* szTarget,
		ID3DBlob** ppBlobOut,
		const char* szShaderTag = nullptr)
	{
		if (!szSource || !szEntry || !szTarget || !ppBlobOut)
			return E_INVALIDARG;

		*ppBlobOut = nullptr;

#ifdef DX11_SHADER_CACHE_ENABLED
		// Try shader cache first
		if (g_pkShaderCacheDX11)
		{
			HRESULT hr = g_pkShaderCacheDX11->GetShaderBytecode(
				szSource, szEntry, szTarget, ppBlobOut, szShaderTag);

			if (SUCCEEDED(hr))
			{
				return S_OK;  // Cache hit or compile successful
			}

			// Cache failed, fall through to direct compilation below
		}
#endif

		// Fallback to direct compilation
		ID3DBlob* pErrorBlob = nullptr;
		const UINT uFlags = D3DCOMPILE_ENABLE_STRICTNESS;
		const HRESULT hr = D3DCompile(
			szSource,
			strlen(szSource),
			nullptr,
			nullptr,
			nullptr,
			szEntry,
			szTarget,
			uFlags,
			0u,
			ppBlobOut,
			&pErrorBlob);

		if (FAILED(hr))
		{
			const char* szTag = (szShaderTag && szShaderTag[0]) ? szShaderTag : "unknown";
			if (pErrorBlob && pErrorBlob->GetBufferPointer())
				TraceError("DX11_SHADER_COMPILE_FAIL shader=%s entry=%s target=%s hr=0x%08X error=%s", szTag, szEntry, szTarget, static_cast<unsigned int>(hr), static_cast<const char*>(pErrorBlob->GetBufferPointer()));
			else
				TraceError("DX11_SHADER_COMPILE_FAIL shader=%s entry=%s target=%s hr=0x%08X", szTag, szEntry, szTarget, static_cast<unsigned int>(hr));
		}
		safe_release(pErrorBlob);
		return hr;
	}
}

void CMapOutdoor::__ClearDX11TerrainTextureSRVCache()
{
	for (auto& rkPair : m_mapDX11TerrainTextureSRVCache)
		safe_release(rkPair.second);
	m_mapDX11TerrainTextureSRVCache.clear();

	for (auto& rkPair : m_mapDX11SplatAlphaSRVCache)
		safe_release(rkPair.second);
	m_mapDX11SplatAlphaSRVCache.clear();
}

ID3D11ShaderResourceView* CMapOutdoor::__LoadTerrainTextureDDS(const char* szFilename, ID3D11Device* pDevice)
{
	(void)pDevice;
	if (!szFilename || !*szFilename)
		return nullptr;

	CGraphicImage* pImage = static_cast<CGraphicImage*>(CResourceManager::Instance().GetResourcePointer(szFilename));
	if (!pImage)
		return nullptr;

	CGraphicTexture* pTexture = pImage->GetTexturePointer();
	if (!pTexture)
		return nullptr;

	ID3D11ShaderResourceView* pSRV = pTexture->GetD3D11TextureSRV();
	if (!pSRV)
		return nullptr;

	pSRV->AddRef();
	return pSRV;
}

ID3D11ShaderResourceView* CMapOutdoor::__LoadTerrainTextureWIC(const char* szFilename, ID3D11Device* pDevice)
{
	return __LoadTerrainTextureDDS(szFilename, pDevice);
}

ID3D11ShaderResourceView* CMapOutdoor::__GetOrCreateDX11TerrainTextureSRV(const char* szFilename)
{
	if (!szFilename || !*szFilename)
		return nullptr;

	auto it = m_mapDX11TerrainTextureSRVCache.find(szFilename);
	if (it != m_mapDX11TerrainTextureSRVCache.end() && it->second)
		return it->second;

	ID3D11ShaderResourceView* pLoaded = __LoadTerrainTextureDDS(szFilename, m_pDX11Device);
	if (!pLoaded)
		pLoaded = __LoadTerrainTextureWIC(szFilename, m_pDX11Device);

	if (!pLoaded)
		return nullptr;

	m_mapDX11TerrainTextureSRVCache.emplace(szFilename, pLoaded);
	return pLoaded;
}

ID3D11ShaderResourceView* CMapOutdoor::__GetTerrainTextureSRV(bool* pbWasFallbackWhite, const char* szFilename)
{
	if (pbWasFallbackWhite)
		*pbWasFallbackWhite = false;

	if (!szFilename || !*szFilename)
	{
		if (pbWasFallbackWhite)
			*pbWasFallbackWhite = true;
		return m_pDX11TerrainDefaultTextureSRV ? m_pDX11TerrainDefaultTextureSRV : m_pDX11TerrainMissingTextureSRV;
	}

	ID3D11ShaderResourceView* pLoaded = __GetOrCreateDX11TerrainTextureSRV(szFilename);
	if (pLoaded)
		return pLoaded;

	if (pbWasFallbackWhite)
		*pbWasFallbackWhite = true;
	return m_pDX11TerrainMissingTextureSRV ? m_pDX11TerrainMissingTextureSRV : m_pDX11TerrainDefaultTextureSRV;
}

ID3D11ShaderResourceView* CMapOutdoor::__GetSplatTextureSRV(
	bool* pbWasFallbackWhite,
	const char* szFilename,
	CTerrain* pTerrain,
	DWORD dwSplatIndex)
{
	if (pbWasFallbackWhite)
		*pbWasFallbackWhite = false;

	if (pTerrain && dwSplatIndex < pTerrain->GetNumTextures())
	{
		TTerrainTexture& rkTexture = pTerrain->GetTexture(dwSplatIndex);
		if (!szFilename || !*szFilename)
			szFilename = rkTexture.stFilename.c_str();
	}

	// Deterministic strict path:
	// always resolve terrain splat textures by filename through terrain cache.
	// This avoids stale/mismatched SRV pointers leaking from unrelated image instances.
	return __GetTerrainTextureSRV(pbWasFallbackWhite, szFilename);
}

ID3D11ShaderResourceView* CMapOutdoor::__GetOrCreateDX11SplatAlphaSRV(CTerrain* pTerrain, DWORD dwSplatIndex)
{
	if (!pTerrain || dwSplatIndex >= MAXTERRAINTEXTURES || !m_pDX11Device)
		return nullptr;

	TTerrainSplatPatch& rkSplatPatch = pTerrain->GetTerrainSplatPatch();
	if (rkSplatPatch.Splats[dwSplatIndex].pTextureSRV)
		return rkSplatPatch.Splats[dwSplatIndex].pTextureSRV;

	const uint64_t uCacheKey = MakeSplatAlphaCacheKey(pTerrain, dwSplatIndex);
	auto it = m_mapDX11SplatAlphaSRVCache.find(uCacheKey);
	if (it != m_mapDX11SplatAlphaSRVCache.end() && it->second)
	{
		rkSplatPatch.Splats[dwSplatIndex].pTextureSRV = it->second;
		return it->second;
	}

	const BYTE* pAlphaData = nullptr;
	UINT uWidth = 0u;
	UINT uHeight = 0u;
	if (!pTerrain->GetDX11SplatAlphaCache(static_cast<BYTE>(dwSplatIndex), &pAlphaData, &uWidth, &uHeight) || !pAlphaData || !uWidth || !uHeight)
		return nullptr;

	D3D11_TEXTURE2D_DESC kDesc = {};
	kDesc.Width = uWidth;
	kDesc.Height = uHeight;
	kDesc.MipLevels = 1u;
	kDesc.ArraySize = 1u;
	kDesc.Format = DXGI_FORMAT_R8_UNORM;
	kDesc.SampleDesc.Count = 1u;
	kDesc.Usage = D3D11_USAGE_DEFAULT;
	kDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA kInitData = {};
	kInitData.pSysMem = pAlphaData;
	kInitData.SysMemPitch = uWidth * sizeof(BYTE);

	ID3D11Texture2D* pTexture = nullptr;
	HRESULT hr = m_pDX11Device->CreateTexture2D(&kDesc, &kInitData, &pTexture);
	if (FAILED(hr) || !pTexture)
	{
		TraceError("DX11_SPLAT_ALPHA_SRV_FAIL reason=create_texture hr=0x%08X splat=%u", static_cast<unsigned int>(hr), static_cast<unsigned int>(dwSplatIndex));
		return nullptr;
	}

	ID3D11ShaderResourceView* pSRV = nullptr;
	hr = m_pDX11Device->CreateShaderResourceView(pTexture, nullptr, &pSRV);
	pTexture->Release();
	if (FAILED(hr) || !pSRV)
	{
		TraceError("DX11_SPLAT_ALPHA_SRV_FAIL reason=create_srv hr=0x%08X splat=%u", static_cast<unsigned int>(hr), static_cast<unsigned int>(dwSplatIndex));
		return nullptr;
	}

	rkSplatPatch.Splats[dwSplatIndex].pTextureSRV = pSRV;
	m_mapDX11SplatAlphaSRVCache.emplace(uCacheKey, pSRV);
	return pSRV;
}

bool CMapOutdoor::__CreateDX11TerrainShaders(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11TerrainShaders();

	static const char* kTerrainVS = R"(
cbuffer TerrainCB : register(b0)
{
	row_major float4x4 g_mWorldViewProj;
	float4 g_vUVTransform;
	float4 g_vAlphaUVTransform;
	float4 g_vLightDir;
	float4 g_vAmbient;
};
struct VS_IN
{
	float3 Pos : POSITION;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD0;
};
struct VS_OUT
{
	float4 Pos : SV_POSITION;
	float3 Normal : TEXCOORD0;
	float2 UV : TEXCOORD1;
	float2 AlphaUV : TEXCOORD2;
	float3 WorldPos : TEXCOORD3;
};
VS_OUT main(VS_IN i)
{
	VS_OUT o;
	o.Pos = mul(float4(i.Pos, 1.0f), g_mWorldViewProj);
	o.Normal = normalize(i.Normal);
	o.UV = i.UV * g_vUVTransform.xy + g_vUVTransform.zw;
	o.AlphaUV = i.Pos.xy * g_vAlphaUVTransform.xy + g_vAlphaUVTransform.zw;
	o.WorldPos = i.Pos;
	return o;
}
)";

	static const char* kTerrainPS = R"(
cbuffer TerrainCB : register(b0)
{
	row_major float4x4 g_mWorldViewProj;
	float4 g_vUVTransform;
	float4 g_vAlphaUVTransform;
	float4 g_vLightDir;
	float4 g_vAmbient;
};
cbuffer ShadowFrameCB : register(b3)
{
	row_major float4x4 g_mShadowLightViewProj0;
	row_major float4x4 g_mShadowLightViewProj1;
	row_major float4x4 g_mShadowLightViewProj2;
	float4 g_vShadowCascadeSplits;
	float4 g_vShadowLightDir;
};
Texture2D g_txBase : register(t0);
Texture2DArray g_txShadowMap : register(t6);
SamplerState g_smBase : register(s0);
SamplerState g_smShadow : register(s6);
struct VS_OUT
{
	float4 Pos : SV_POSITION;
	float3 Normal : TEXCOORD0;
	float2 UV : TEXCOORD1;
	float2 AlphaUV : TEXCOORD2;
	float3 WorldPos : TEXCOORD3;
};
float SampleShadowCascade(row_major float4x4 matLightViewProj, float fCascadeIndex, float3 vWorldPos, out bool bValid)
{
	float4 vShadowPos = mul(float4(vWorldPos, 1.0f), matLightViewProj);
	float fInvW = rcp(max(abs(vShadowPos.w), 1e-4f));
	vShadowPos.xyz *= fInvW;
	float2 vShadowUV = vShadowPos.xy * float2(0.5f, -0.5f) + 0.5f;
	bValid =
		(vShadowUV.x >= 0.0f && vShadowUV.x <= 1.0f) &&
		(vShadowUV.y >= 0.0f && vShadowUV.y <= 1.0f) &&
		(vShadowPos.z >= 0.0f && vShadowPos.z <= 1.0f);
	if (!bValid)
		return 1.0f;

	// PCF 3x3 filtering (9 samples) for soft shadow edges
	const float2 texelSize = 1.0f / float2(2048.0, 2048.0);  // SHADOW_MAP_SIZE = 2048
	const float fShadowBias = 0.00025f;
	float fShadow = 0.0f;

	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			float2 offset = float2(x, y) * texelSize;
			float fMapDepth = g_txShadowMap.SampleLevel(
				g_smShadow,
				float3(vShadowUV + offset, fCascadeIndex),
				0.0f).r;
			fShadow += (fMapDepth >= (vShadowPos.z - fShadowBias)) ? 1.0f : 0.0f;
		}
	}
	return fShadow / 9.0f;
}
float SampleShadow(float3 vWorldPos)
{
	if (g_vShadowLightDir.w < 0.5f)
		return 1.0f;

	bool bValid = false;
	float fShadow = SampleShadowCascade(g_mShadowLightViewProj0, 0.0f, vWorldPos, bValid);
	if (bValid)
		return fShadow;

	fShadow = SampleShadowCascade(g_mShadowLightViewProj1, 1.0f, vWorldPos, bValid);
	if (bValid)
		return fShadow;

	fShadow = SampleShadowCascade(g_mShadowLightViewProj2, 2.0f, vWorldPos, bValid);
	if (bValid)
		return fShadow;

	return 1.0f;
}
float4 main(VS_OUT i) : SV_Target
{
	float3 N = normalize(i.Normal);
	float3 L = normalize(-g_vLightDir.xyz);
	float ndl = saturate(dot(N, L));
	float shadow = SampleShadow(i.WorldPos);
	float4 baseCol = g_txBase.Sample(g_smBase, i.UV);
	float3 lit = baseCol.rgb * (g_vAmbient.rgb + (ndl * shadow));
	return float4(lit, 1.0f);
}
)";

	ID3DBlob* pVSBlob = nullptr;
	ID3DBlob* pPSBlob = nullptr;

	HRESULT hr = CompileDX11WorldShader(kTerrainVS, "main", "vs_4_0", &pVSBlob, "terrain_vs");
	if (FAILED(hr) || !pVSBlob)
		return false;

	hr = CompileDX11WorldShader(kTerrainPS, "main", "ps_4_0", &pPSBlob, "terrain_ps");
	if (FAILED(hr) || !pPSBlob)
	{
		safe_release(pVSBlob);
		return false;
	}

	hr = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pDX11TerrainVertexShader);
	if (FAILED(hr))
	{
		safe_release(pVSBlob);
		safe_release(pPSBlob);
		return false;
	}

	hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pDX11TerrainPixelShader);
	if (FAILED(hr))
	{
		safe_release(pVSBlob);
		safe_release(pPSBlob);
		__DestroyDX11TerrainShaders();
		return false;
	}

	const D3D11_INPUT_ELEMENT_DESC kLayout[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DX11TerrainVertex, kPosition), D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DX11TerrainVertex, kNormal), D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(DX11TerrainVertex, kTexCoord), D3D11_INPUT_PER_VERTEX_DATA, 0},
	};

	hr = pDevice->CreateInputLayout(kLayout, _countof(kLayout), pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pDX11TerrainInputLayout);
	if (pVSBlob)
	{
		pVSBlob->Release();
		pVSBlob = nullptr;
	}
	if (pPSBlob)
	{
		pPSBlob->Release();
		pPSBlob = nullptr;
	}
	if (FAILED(hr))
	{
		__DestroyDX11TerrainShaders();
		return false;
	}

	D3D11_BUFFER_DESC kCBDesc = {};
	kCBDesc.ByteWidth = sizeof(DX11TerrainShaderCB);
	kCBDesc.Usage = D3D11_USAGE_DYNAMIC;
	kCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	kCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	hr = pDevice->CreateBuffer(&kCBDesc, nullptr, &m_pDX11TerrainConstantBuffer);
	if (FAILED(hr))
	{
		__DestroyDX11TerrainShaders();
		return false;
	}

	return true;
}

void CMapOutdoor::__DestroyDX11TerrainShaders()
{
	safe_release(m_pDX11TerrainConstantBuffer);
	safe_release(m_pDX11TerrainInputLayout);
	safe_release(m_pDX11TerrainPixelShader);
	safe_release(m_pDX11TerrainVertexShader);
}

bool CMapOutdoor::__CreateDX11TerrainPipelineStates(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11TerrainPipelineStates();

	D3D11_SAMPLER_DESC kSamplerDesc = {};
	kSamplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
	kSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.MaxAnisotropy = 8u;
	kSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	HRESULT hr = pDevice->CreateSamplerState(&kSamplerDesc, &m_pDX11TerrainSamplerState);
	if (FAILED(hr))
		return false;

	const uint32_t uWhite = 0xFFFFFFFFu;
	D3D11_TEXTURE2D_DESC kTexDesc = {};
	kTexDesc.Width = 1u;
	kTexDesc.Height = 1u;
	kTexDesc.MipLevels = 1u;
	kTexDesc.ArraySize = 1u;
	kTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	kTexDesc.SampleDesc.Count = 1u;
	kTexDesc.Usage = D3D11_USAGE_IMMUTABLE;
	kTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA kInit = {};
	kInit.pSysMem = &uWhite;
	kInit.SysMemPitch = sizeof(uint32_t);

	hr = pDevice->CreateTexture2D(&kTexDesc, &kInit, &m_pDX11TerrainDefaultTexture);
	if (FAILED(hr))
		return false;

	hr = pDevice->CreateShaderResourceView(m_pDX11TerrainDefaultTexture, nullptr, &m_pDX11TerrainDefaultTextureSRV);
	if (FAILED(hr))
		return false;

	const uint32_t uMissing = 0xFFFF00FFu;
	kInit.pSysMem = &uMissing;
	hr = pDevice->CreateTexture2D(&kTexDesc, &kInit, &m_pDX11TerrainMissingTexture);
	if (FAILED(hr))
		return false;

	hr = pDevice->CreateShaderResourceView(m_pDX11TerrainMissingTexture, nullptr, &m_pDX11TerrainMissingTextureSRV);
	if (FAILED(hr))
		return false;

	return true;
}

void CMapOutdoor::__DestroyDX11TerrainPipelineStates()
{
	safe_release(m_pDX11TerrainMissingTextureSRV);
	safe_release(m_pDX11TerrainMissingTexture);
	safe_release(m_pDX11TerrainDefaultTextureSRV);
	safe_release(m_pDX11TerrainDefaultTexture);
	safe_release(m_pDX11TerrainSamplerState);
}

bool CMapOutdoor::__CreateDX11TerrainSplatShaders(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11TerrainSplatShaders();

	static const char* kSplatVS = R"(
cbuffer TerrainCB : register(b0)
{
	row_major float4x4 g_mWorldViewProj;
	float4 g_vUVTransform;
	float4 g_vAlphaUVTransform;
	float4 g_vLightDir;
	float4 g_vAmbient;
};
struct VS_IN
{
	float3 Pos : POSITION;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD0;
};
struct VS_OUT
{
	float4 Pos : SV_POSITION;
	float3 Normal : TEXCOORD0;
	float2 UV : TEXCOORD1;
	float2 AlphaUV : TEXCOORD2;
	float3 WorldPos : TEXCOORD3;
};
VS_OUT main(VS_IN i)
{
	VS_OUT o;
	o.Pos = mul(float4(i.Pos, 1.0f), g_mWorldViewProj);
	o.Normal = normalize(i.Normal);
	o.UV = i.UV * g_vUVTransform.xy + g_vUVTransform.zw;
	o.AlphaUV = i.Pos.xy * g_vAlphaUVTransform.xy + g_vAlphaUVTransform.zw;
	o.WorldPos = i.Pos;
	return o;
}
)";

	static const char* kSplatPS = R"(
cbuffer TerrainCB : register(b0)
{
	row_major float4x4 g_mWorldViewProj;
	float4 g_vUVTransform;
	float4 g_vAlphaUVTransform;
	float4 g_vLightDir;
	float4 g_vAmbient;
};
cbuffer ShadowFrameCB : register(b3)
{
	row_major float4x4 g_mShadowLightViewProj0;
	row_major float4x4 g_mShadowLightViewProj1;
	row_major float4x4 g_mShadowLightViewProj2;
	float4 g_vShadowCascadeSplits;
	float4 g_vShadowLightDir;
};
Texture2D g_txBase : register(t0);
Texture2D g_txAlpha : register(t1);
Texture2DArray g_txShadowMap : register(t6);
SamplerState g_smBase : register(s0);
SamplerState g_smAlpha : register(s1);
SamplerState g_smShadow : register(s6);
struct VS_OUT
{
	float4 Pos : SV_POSITION;
	float3 Normal : TEXCOORD0;
	float2 UV : TEXCOORD1;
	float2 AlphaUV : TEXCOORD2;
	float3 WorldPos : TEXCOORD3;
};
float SampleShadowCascade(row_major float4x4 matLightViewProj, float fCascadeIndex, float3 vWorldPos, out bool bValid)
{
	float4 vShadowPos = mul(float4(vWorldPos, 1.0f), matLightViewProj);
	float fInvW = rcp(max(abs(vShadowPos.w), 1e-4f));
	vShadowPos.xyz *= fInvW;
	float2 vShadowUV = vShadowPos.xy * float2(0.5f, -0.5f) + 0.5f;
	bValid =
		(vShadowUV.x >= 0.0f && vShadowUV.x <= 1.0f) &&
		(vShadowUV.y >= 0.0f && vShadowUV.y <= 1.0f) &&
		(vShadowPos.z >= 0.0f && vShadowPos.z <= 1.0f);
	if (!bValid)
		return 1.0f;

	// PCF 3x3 filtering (9 samples) for soft shadow edges
	const float2 texelSize = 1.0f / float2(2048.0, 2048.0);  // SHADOW_MAP_SIZE = 2048
	const float fShadowBias = 0.00025f;
	float fShadow = 0.0f;

	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			float2 offset = float2(x, y) * texelSize;
			float fMapDepth = g_txShadowMap.SampleLevel(
				g_smShadow,
				float3(vShadowUV + offset, fCascadeIndex),
				0.0f).r;
			fShadow += (fMapDepth >= (vShadowPos.z - fShadowBias)) ? 1.0f : 0.0f;
		}
	}
	return fShadow / 9.0f;
}
float SampleShadow(float3 vWorldPos)
{
	if (g_vShadowLightDir.w < 0.5f)
		return 1.0f;

	bool bValid = false;
	float fShadow = SampleShadowCascade(g_mShadowLightViewProj0, 0.0f, vWorldPos, bValid);
	if (bValid)
		return fShadow;

	fShadow = SampleShadowCascade(g_mShadowLightViewProj1, 1.0f, vWorldPos, bValid);
	if (bValid)
		return fShadow;

	fShadow = SampleShadowCascade(g_mShadowLightViewProj2, 2.0f, vWorldPos, bValid);
	if (bValid)
		return fShadow;

	return 1.0f;
}
float4 main(VS_OUT i) : SV_Target
{
	float4 baseCol = g_txBase.Sample(g_smBase, i.UV);
	float alpha = g_txAlpha.Sample(g_smAlpha, i.AlphaUV).r;
	float3 N = normalize(i.Normal);
	float3 L = normalize(-g_vLightDir.xyz);
	float ndl = saturate(dot(N, L));
	float shadow = SampleShadow(i.WorldPos);
	float3 lit = baseCol.rgb * (g_vAmbient.rgb + (ndl * shadow));
	return float4(lit, alpha);
}
)";

	ID3DBlob* pVSBlob = nullptr;
	ID3DBlob* pPSBlob = nullptr;
	HRESULT hr = CompileDX11WorldShader(kSplatVS, "main", "vs_4_0", &pVSBlob, "terrain_splat_vs");
	if (FAILED(hr) || !pVSBlob)
		return false;

	hr = CompileDX11WorldShader(kSplatPS, "main", "ps_4_0", &pPSBlob, "terrain_splat_ps");
	if (FAILED(hr) || !pPSBlob)
	{
		safe_release(pVSBlob);
		return false;
	}

	hr = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pDX11TerrainSplatVertexShader);
	if (FAILED(hr))
	{
		safe_release(pVSBlob);
		safe_release(pPSBlob);
		return false;
	}

	hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pDX11TerrainSplatPixelShader);
	if (pVSBlob)
	{
		pVSBlob->Release();
		pVSBlob = nullptr;
	}
	if (pPSBlob)
	{
		pPSBlob->Release();
		pPSBlob = nullptr;
	}
	if (FAILED(hr))
	{
		__DestroyDX11TerrainSplatShaders();
		return false;
	}

	return true;
}

void CMapOutdoor::__DestroyDX11TerrainSplatShaders()
{
	safe_release(m_pDX11TerrainSplatPixelShader);
	safe_release(m_pDX11TerrainSplatVertexShader);
}

bool CMapOutdoor::__CreateDX11TerrainSplatBlendState(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11TerrainSplatBlendState();

	D3D11_BLEND_DESC kBlendDesc = {};
	kBlendDesc.RenderTarget[0].BlendEnable = TRUE;
	kBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	kBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	kBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	kBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	kBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	kBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	kBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	HRESULT hr = pDevice->CreateBlendState(&kBlendDesc, &m_pDX11TerrainSplatBlendState);
	if (FAILED(hr))
		return false;

	D3D11_SAMPLER_DESC kAlphaSampler = {};
	kAlphaSampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	kAlphaSampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	kAlphaSampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	kAlphaSampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	kAlphaSampler.MaxLOD = D3D11_FLOAT32_MAX;
	hr = pDevice->CreateSamplerState(&kAlphaSampler, &m_pDX11TerrainSplatAlphaSamplerState);
	if (FAILED(hr))
	{
		__DestroyDX11TerrainSplatBlendState();
		return false;
	}

	return true;
}

void CMapOutdoor::__DestroyDX11TerrainSplatBlendState()
{
	safe_release(m_pDX11TerrainSplatAlphaSamplerState);
	safe_release(m_pDX11TerrainSplatBlendState);
}

bool CMapOutdoor::__RenderTerrain_DX11HardwareTransformPatch(
	ID3D11Device* pDevice,
	ID3D11DeviceContext* pContext,
	const D3DXMATRIX& matTerrainViewProj)
{
	if (!pDevice || !pContext || !m_pDX11TerrainVertexShader || !m_pDX11TerrainPixelShader || !m_pDX11TerrainInputLayout || !m_pDX11TerrainConstantBuffer)
		return false;

	ID3D11RasterizerState* pOldRaster = nullptr;
	ID3D11DepthStencilState* pOldDepth = nullptr;
	UINT uOldStencilRef = 0u;
	ID3D11BlendState* pOldBlend = nullptr;
	FLOAT afOldBlendFactor[4] = {0.f, 0.f, 0.f, 0.f};
	UINT uOldSampleMask = 0u;
	pContext->RSGetState(&pOldRaster);
	pContext->OMGetDepthStencilState(&pOldDepth, &uOldStencilRef);
	pContext->OMGetBlendState(&pOldBlend, afOldBlendFactor, &uOldSampleMask);

	// Pass-local raster/depth ownership for terrain to avoid state leaks from object/character passes.
	static ID3D11Device* s_pTerrainStateDevice = nullptr;
	static ID3D11RasterizerState* s_pTerrainRasterState = nullptr;
	static ID3D11DepthStencilState* s_pTerrainDepthState = nullptr;
	if (s_pTerrainStateDevice != pDevice)
	{
		safe_release(s_pTerrainRasterState);
		safe_release(s_pTerrainDepthState);
		safe_release(s_pTerrainStateDevice);
		if (pDevice)
		{
			s_pTerrainStateDevice = pDevice;
			s_pTerrainStateDevice->AddRef();
		}
	}

	if (!s_pTerrainRasterState)
	{
		D3D11_RASTERIZER_DESC kRasterDesc = {};
		kRasterDesc.FillMode = D3D11_FILL_SOLID;
		kRasterDesc.CullMode = D3D11_CULL_NONE;
		kRasterDesc.FrontCounterClockwise = FALSE;
		kRasterDesc.DepthClipEnable = TRUE;
		pDevice->CreateRasterizerState(&kRasterDesc, &s_pTerrainRasterState);
	}

	if (!s_pTerrainDepthState)
	{
		D3D11_DEPTH_STENCIL_DESC kDepthDesc = {};
		kDepthDesc.DepthEnable = TRUE;
		kDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		kDepthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		pDevice->CreateDepthStencilState(&kDepthDesc, &s_pTerrainDepthState);
	}

	if (s_pTerrainRasterState)
		pContext->RSSetState(s_pTerrainRasterState);
	if (s_pTerrainDepthState)
		pContext->OMSetDepthStencilState(s_pTerrainDepthState, 0u);
	pContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);

    // Terrain shaders sample shadow map from t6/s6 and shadow frame data from b3.
    // Bind explicitly here so terrain draw never depends on external pass state.
    if (m_bDX11ShadowResourcesReady && m_pDX11ShadowFrameConstantBuffer && m_pDX11ShadowMapArraySRV)
    {
        pContext->PSSetConstantBuffers(3u, 1u, &m_pDX11ShadowFrameConstantBuffer);
        pContext->PSSetShaderResources(6u, 1u, &m_pDX11ShadowMapArraySRV);
    }
    else
    {
        ID3D11ShaderResourceView* pNullShadowSRV = nullptr;
        pContext->PSSetShaderResources(6u, 1u, &pNullShadowSRV);
    }

    ID3D11SamplerState* pShadowSampler = m_pDX11ShadowComparisonSampler ? m_pDX11ShadowComparisonSampler : m_pDX11TerrainSamplerState;
    if (pShadowSampler)
        pContext->PSSetSamplers(6u, 1u, &pShadowSampler);


	float fLODLevel1Distance = __GetNoFogDistance();
	float fLODLevel2Distance = __GetFogDistance();
	BYTE byCurrentLOD = 0u;
	WORD wPrimitiveCount = 0u;
	GrpPrimitiveType ePrimitiveType = GRP_PT_TRIANGLELIST;
	SelectIndexBuffer(0u, &wPrimitiveCount, &ePrimitiveType);

	m_iRenderedSplatNum = 0;
	m_iRenderedSplatNumSqSum = 0.0f;
	m_RenderedTextureNumVector.clear();
	std::array<bool, MAXTERRAINTEXTURES> akTextureSeen = {};
	bool bIssuedDraw = false;

	for (const TPatchDrawStruct& rkPatchDraw : m_PatchDrawStructVector)
	{
		if (!rkPatchDraw.pTerrainPatchProxy)
			continue;

		if (0u == byCurrentLOD && fLODLevel1Distance <= rkPatchDraw.fDistance)
		{
			byCurrentLOD = 1u;
			SelectIndexBuffer(byCurrentLOD, &wPrimitiveCount, &ePrimitiveType);
		}
		else if (1u == byCurrentLOD && fLODLevel2Distance <= rkPatchDraw.fDistance)
		{
			byCurrentLOD = 2u;
			SelectIndexBuffer(byCurrentLOD, &wPrimitiveCount, &ePrimitiveType);
		}

		CTerrainPatchProxy* pPatchProxy = rkPatchDraw.pTerrainPatchProxy;
		CTerrainPatch* pPatch = pPatchProxy->GetTerrainPatch();
		if (!pPatch)
			continue;

		ID3D11Buffer* pVB = pPatch->GetDX11VertexBuffer();
		if (!pVB)
			continue;

		ID3D11Buffer* pIB = m_IndexBuffer[byCurrentLOD].GetIndexBuffer();
		if (!pIB || 0u == wPrimitiveCount)
			continue;
		const UINT uIndexCount = GetIndexCountFromPrimitiveCount(wPrimitiveCount, ePrimitiveType);
		if (0u == uIndexCount)
			continue;

		CTerrain* pTerrain = nullptr;
		if (!GetTerrainPointer(rkPatchDraw.byTerrainNum, &pTerrain) || !pTerrain)
			continue;

		TTerrainSplatPatch& rkSplatPatch = pTerrain->GetTerrainSplatPatch();

		struct SLayer
		{
			DWORD dwTextureIndex;
			ID3D11ShaderResourceView* pTextureSRV;
			ID3D11ShaderResourceView* pAlphaSRV;
			TTerrainTexture* pTextureMeta;
			bool bFallbackWhite;
		};

		std::vector<SLayer> kLayers;
		kLayers.reserve(8);

		const DWORD dwTextureCount = pTerrain->GetNumTextures();
		for (DWORD dwTextureIndex = 1; dwTextureIndex < dwTextureCount; ++dwTextureIndex)
		{
			if (0u == rkSplatPatch.PatchTileCount[rkPatchDraw.lPatchNum][dwTextureIndex])
				continue;
			if (!rkSplatPatch.Splats[dwTextureIndex].Active)
				continue;

			TTerrainTexture& rkTexture = pTerrain->GetTexture(dwTextureIndex);
			bool bFallbackWhite = false;
			ID3D11ShaderResourceView* pTextureSRV = __GetSplatTextureSRV(&bFallbackWhite, rkTexture.stFilename.c_str(), pTerrain, dwTextureIndex);
			if (!pTextureSRV)
				continue;

			ID3D11ShaderResourceView* pAlphaSRV = __GetOrCreateDX11SplatAlphaSRV(pTerrain, dwTextureIndex);
			kLayers.push_back({dwTextureIndex, pTextureSRV, pAlphaSRV, &rkTexture, bFallbackWhite});
		}

		// Keep a deterministic base pass to avoid per-patch base texture drift (visible sector seams).
		// Legacy terrain defaults to texture slot 1 as baseline.
		if (dwTextureCount > 1u)
		{
			const DWORD dwBaseTextureIndex = 1u;
			bool bBaseFallbackWhite = false;
			TTerrainTexture& rkBaseTexture = pTerrain->GetTexture(dwBaseTextureIndex);
			ID3D11ShaderResourceView* pBaseTextureSRV =
				__GetSplatTextureSRV(&bBaseFallbackWhite, rkBaseTexture.stFilename.c_str(), pTerrain, dwBaseTextureIndex);

			if (pBaseTextureSRV)
			{
				size_t uExistingBaseLayer = static_cast<size_t>(-1);
				for (size_t iLayer = 0; iLayer < kLayers.size(); ++iLayer)
				{
					if (kLayers[iLayer].dwTextureIndex == dwBaseTextureIndex)
					{
						uExistingBaseLayer = iLayer;
						break;
					}
				}

				if (uExistingBaseLayer == static_cast<size_t>(-1))
				{
					kLayers.insert(kLayers.begin(), {dwBaseTextureIndex, pBaseTextureSRV, nullptr, &rkBaseTexture, bBaseFallbackWhite});
				}
				else if (uExistingBaseLayer != 0u)
				{
					std::swap(kLayers[0], kLayers[uExistingBaseLayer]);
				}
			}
		}

		if (kLayers.empty())
		{
			SLayer kFallback = {};
			kFallback.dwTextureIndex = 0u;
			kFallback.pTextureSRV = m_pDX11TerrainDefaultTextureSRV ? m_pDX11TerrainDefaultTextureSRV : m_pDX11TerrainMissingTextureSRV;
			kFallback.pAlphaSRV = nullptr;
			kFallback.pTextureMeta = nullptr;
			kFallback.bFallbackWhite = true;
			kLayers.push_back(kFallback);
		}

		const UINT uStride = sizeof(DX11TerrainVertex);
		const UINT uOffset = 0u;
		pContext->IASetInputLayout(m_pDX11TerrainInputLayout);
		pContext->IASetVertexBuffers(0u, 1u, &pVB, &uStride, &uOffset);
		pContext->IASetIndexBuffer(pIB, DXGI_FORMAT_R16_UINT, 0u);
		pContext->IASetPrimitiveTopology(ToDX11Topology(ePrimitiveType));

		pContext->VSSetConstantBuffers(0u, 1u, &m_pDX11TerrainConstantBuffer);
		pContext->PSSetConstantBuffers(0u, 1u, &m_pDX11TerrainConstantBuffer);
		pContext->VSSetShader(m_pDX11TerrainVertexShader, nullptr, 0u);
		pContext->PSSetShader(m_pDX11TerrainPixelShader, nullptr, 0u);
		pContext->PSSetSamplers(0u, 1u, &m_pDX11TerrainSamplerState);

		int iPatchLayerDraws = 0;
		for (size_t iLayer = 0; iLayer < kLayers.size(); ++iLayer)
		{
			const SLayer& rkLayer = kLayers[iLayer];

			D3D11_MAPPED_SUBRESOURCE kMapped = {};
			if (SUCCEEDED(pContext->Map(m_pDX11TerrainConstantBuffer, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &kMapped)))
			{
				DX11TerrainShaderCB* pCB = reinterpret_cast<DX11TerrainShaderCB*>(kMapped.pData);
				pCB->matWorldViewProj = matTerrainViewProj;
				if (rkLayer.pTextureMeta)
				{
					// Legacy fixed-function parity:
					// UV is sourced from world position and transformed by per-texture matrix.
					pCB->vUVTransform = D3DXVECTOR4(
						rkLayer.pTextureMeta->m_matTransform._11,
						rkLayer.pTextureMeta->m_matTransform._22,
						rkLayer.pTextureMeta->m_matTransform._41,
						rkLayer.pTextureMeta->m_matTransform._42);

				}
				else
					pCB->vUVTransform = D3DXVECTOR4(1.0f, 1.0f, 0.0f, 0.0f);

				WORD wTerrainCoordX = 0;
				WORD wTerrainCoordY = 0;
				pTerrain->GetCoordinate(&wTerrainCoordX, &wTerrainCoordY);

				// Preserve legacy STP alpha mapping with terrain-local origin.
				// alphaUV = (world + terrain_offset) * m_matSplatAlpha.scale + m_matSplatAlpha.bias
				const float fAlphaScaleX = m_matSplatAlpha._11;
				const float fAlphaScaleY = m_matSplatAlpha._22;
				const float fTerrainBaseX =
					-static_cast<float>(wTerrainCoordX) * static_cast<float>(CTerrainImpl::TERRAIN_XSIZE);
				const float fTerrainBaseY =
					+static_cast<float>(wTerrainCoordY) * static_cast<float>(CTerrainImpl::TERRAIN_YSIZE);
				const float fAlphaBiasX = fTerrainBaseX * fAlphaScaleX + m_matSplatAlpha._41;
				const float fAlphaBiasY = fTerrainBaseY * fAlphaScaleY + m_matSplatAlpha._42;
				pCB->vAlphaUVTransform = D3DXVECTOR4(fAlphaScaleX, fAlphaScaleY, fAlphaBiasX, fAlphaBiasY);

				static DWORD s_dwTerrainLayerDiagLogTick = 0u;
				const DWORD dwDiagNow = ELTimer_GetMSec();
				if ((0u == s_dwTerrainLayerDiagLogTick || (dwDiagNow - s_dwTerrainLayerDiagLogTick) >= 3000u) &&
					0u == iLayer &&
					rkPatchDraw.lPatchNum == m_PatchDrawStructVector.front().lPatchNum)
				{
					s_dwTerrainLayerDiagLogTick = dwDiagNow;
					TraceError(
						"DX11_TERRAIN_LAYER_DIAG patch=%ld terrain=(%u,%u) tex_idx=%u fallback=%d file=%s uv_scale=(%.6f,%.6f) uv_bias=(%.6f,%.6f) alpha_uv=(%.6f,%.6f,%.6f,%.6f)",
						rkPatchDraw.lPatchNum,
						static_cast<unsigned int>(wTerrainCoordX),
						static_cast<unsigned int>(wTerrainCoordY),
						static_cast<unsigned int>(rkLayer.dwTextureIndex),
						rkLayer.bFallbackWhite ? 1 : 0,
						rkLayer.pTextureMeta ? rkLayer.pTextureMeta->stFilename.c_str() : "<none>",
						pCB->vUVTransform.x,
						pCB->vUVTransform.y,
						pCB->vUVTransform.z,
						pCB->vUVTransform.w,
						pCB->vAlphaUVTransform.x,
						pCB->vAlphaUVTransform.y,
						pCB->vAlphaUVTransform.z,
						pCB->vAlphaUVTransform.w);
				}
				pCB->vLightDir = D3DXVECTOR4(0.0f, 0.0f, -1.0f, 0.0f);
				pCB->vAmbient = D3DXVECTOR4(0.65f, 0.65f, 0.65f, 0.0f);
				pContext->Unmap(m_pDX11TerrainConstantBuffer, 0u);
			}

			ID3D11ShaderResourceView* apSRV[2] = {rkLayer.pTextureSRV ? rkLayer.pTextureSRV : m_pDX11TerrainDefaultTextureSRV, nullptr};
			pContext->PSSetShaderResources(0u, 2u, apSRV);

			if (0u == iLayer || !rkLayer.pAlphaSRV || !m_pDX11TerrainSplatPixelShader || !m_pDX11TerrainSplatBlendState)
			{
				pContext->PSSetShader(m_pDX11TerrainPixelShader, nullptr, 0u);
				pContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
			}
			else
			{
				apSRV[1] = rkLayer.pAlphaSRV;
				pContext->PSSetShaderResources(0u, 2u, apSRV);
				pContext->PSSetShader(m_pDX11TerrainSplatPixelShader, nullptr, 0u);
				ID3D11SamplerState* apSamplers[2] = {m_pDX11TerrainSamplerState, m_pDX11TerrainSplatAlphaSamplerState};
				pContext->PSSetSamplers(0u, 2u, apSamplers);
				pContext->OMSetBlendState(m_pDX11TerrainSplatBlendState, nullptr, 0xFFFFFFFFu);
			}

			pContext->DrawIndexed(uIndexCount, 0u, 0u);
			bIssuedDraw = true;
			++iPatchLayerDraws;
			++m_iRenderedSplatNum;
			if (rkLayer.dwTextureIndex < MAXTERRAINTEXTURES && !akTextureSeen[rkLayer.dwTextureIndex])
			{
				akTextureSeen[rkLayer.dwTextureIndex] = true;
				m_RenderedTextureNumVector.push_back(static_cast<int>(rkLayer.dwTextureIndex));
			}
		}

		m_iRenderedSplatNumSqSum += static_cast<float>(iPatchLayerDraws * iPatchLayerDraws);
	}

	ID3D11ShaderResourceView* apNullSRV[2] = {nullptr, nullptr};
	pContext->PSSetShaderResources(0u, 2u, apNullSRV);
	pContext->OMSetBlendState(pOldBlend, afOldBlendFactor, uOldSampleMask);
	pContext->OMSetDepthStencilState(pOldDepth, uOldStencilRef);
	pContext->RSSetState(pOldRaster);

	safe_release(pOldBlend);
	safe_release(pOldDepth);
	safe_release(pOldRaster);
	return bIssuedDraw;
}

bool CMapOutdoor::InitializeDX11TerrainResources(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (m_pDX11Device && m_pDX11Device != pDevice)
	{
		m_pDX11Device->Release();
		m_pDX11Device = nullptr;
	}

	if (!m_pDX11Device)
	{
		m_pDX11Device = pDevice;
		m_pDX11Device->AddRef();
	}

	if (!m_pDX11ObjectVS || !m_pDX11ObjectPS || !m_pDX11ObjectInputLayout || !m_pDX11ObjectConstantBuffer)
	{
		if (!__CreateDX11ObjectShaders(pDevice))
		{
			TraceError("DX11_OBJECT_PIPELINE_INIT_FAIL reason=create_object_shaders");
			return false;
		}
	}

	if (!m_pDX11TerrainVertexShader || !m_pDX11TerrainPixelShader || !m_pDX11TerrainInputLayout || !m_pDX11TerrainConstantBuffer)
	{
		if (!__CreateDX11TerrainShaders(pDevice))
		{
			TraceError("DX11_TERRAIN_PIPELINE_INIT_FAIL reason=create_terrain_shaders");
			return false;
		}
	}

	if (!m_pDX11TerrainSamplerState || !m_pDX11TerrainDefaultTextureSRV)
	{
		if (!__CreateDX11TerrainPipelineStates(pDevice))
		{
			TraceError("DX11_TERRAIN_PIPELINE_INIT_FAIL reason=create_pipeline_states");
			return false;
		}
	}

	if (!m_pDX11TerrainSplatVertexShader || !m_pDX11TerrainSplatPixelShader)
	{
		if (!__CreateDX11TerrainSplatShaders(pDevice))
		{
			TraceError("DX11_TERRAIN_SPLAT_INIT_FAIL reason=create_splat_shaders");
			return false;
		}
	}

	if (!m_pDX11TerrainSplatBlendState || !m_pDX11TerrainSplatAlphaSamplerState)
	{
		if (!__CreateDX11TerrainSplatBlendState(pDevice))
		{
			TraceError("DX11_TERRAIN_SPLAT_INIT_FAIL reason=create_splat_blend_state");
			return false;
		}
	}

	m_bDX11TerrainResourcesReady = true;
	m_bDX11TerrainSplatResourcesReady = true;
	return true;
}

void CMapOutdoor::DestroyDX11TerrainResources()
{
	m_bDX11TerrainResourcesReady = false;
	m_bDX11TerrainSplatResourcesReady = false;

	__ClearDX11TerrainTextureSRVCache();
	__DestroyDX11TerrainSplatBlendState();
	__DestroyDX11TerrainSplatShaders();
	__DestroyDX11TerrainPipelineStates();
	__DestroyDX11TerrainShaders();
	safe_release(m_pDX11TerrainIndexBuffer);
	m_uDX11TerrainIndexCount = 0u;

	safe_release(m_pDX11ObjectVS);
	safe_release(m_pDX11ObjectPS);
	safe_release(m_pDX11ObjectInputLayout);
	safe_release(m_pDX11ObjectConstantBuffer);
	safe_release(m_pDX11ObjectSamplerState);

	if (m_pDX11Device)
	{
		m_pDX11Device->Release();
		m_pDX11Device = nullptr;
	}
}

bool CMapOutdoor::BuildDX11TerrainVertexBuffers(ID3D11Device* pDevice)
{
	if (!pDevice || !m_pTerrainPatchProxyList)
		return false;

	DWORD dwBuilt = 0;
	DWORD dwFailed = 0;

	const WORD wPatchTotalCount = static_cast<WORD>(m_wPatchCount * m_wPatchCount);
	for (WORD wPatchIndex = 0; wPatchIndex < wPatchTotalCount; ++wPatchIndex)
	{
		CTerrainPatchProxy& rkProxy = m_pTerrainPatchProxyList[wPatchIndex];
		if (!rkProxy.isUsed())
			continue;

		CTerrainPatch* pPatch = rkProxy.GetTerrainPatch();
		if (!pPatch)
			continue;

		if (pPatch->IsDX11VertexBufferReady())
			continue;

		if (pPatch->BuildDX11TerrainVertexBufferFromCache(pDevice))
			++dwBuilt;
		else
			++dwFailed;
	}

	static DWORD s_dwTerrainVBLogTick = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwTerrainVBLogTick || (dwNow - s_dwTerrainVBLogTick) >= 3000u)
	{
		s_dwTerrainVBLogTick = dwNow;
		TraceError(
			"DX11_TERRAIN_VB_BUILD built=%u failed=%u patch_count=%u",
			static_cast<unsigned int>(dwBuilt),
			static_cast<unsigned int>(dwFailed),
			static_cast<unsigned int>(wPatchTotalCount));
	}

	return (0 == dwFailed);
}

void CMapOutdoor::RenderTerrainDX11(
	ID3D11Device* pDevice,
	ID3D11DeviceContext* pContext,
	uint32_t* pdwOutObservedMask,
	uint32_t* pdwOutSubmittedMask,
	uint32_t* pdwOutApplicableMask)
{
	uint32_t dwObservedMask = CGraphicDeviceDX11::WORLD_TERRAIN_DX11;
	uint32_t dwSubmittedMask = 0u;
	uint32_t dwApplicableMask = CGraphicDeviceDX11::WORLD_TERRAIN_DX11;
	// Dungeon maps keep terrain only for collision/height queries. The legacy
	// world path skips its color pass; native DX11 must preserve that behavior
	// or terrain drawn after dungeon objects covers structural floor meshes.
	// Publish a handled logical submission so the native-present readiness gate
	// does not wait forever for an intentionally disabled color pass.
	if (m_bEnableTerrainOnlyForHeight)
	{
		m_PatchVector.clear();
		m_iRenderedPatchNum = 0;
		m_iRenderedSplatNum = 0;
		m_iRenderedSplatNumSqSum = 0.0f;
		m_RenderedTextureNumVector.clear();
		dwSubmittedMask = CGraphicDeviceDX11::WORLD_TERRAIN_DX11;
		if (pdwOutObservedMask)
			*pdwOutObservedMask = dwObservedMask;
		if (pdwOutSubmittedMask)
			*pdwOutSubmittedMask = dwSubmittedMask;
		if (pdwOutApplicableMask)
			*pdwOutApplicableMask = dwApplicableMask;
		return;
	}

	if (!pDevice || !pContext || !m_pTerrainPatchProxyList || !m_pRootNode)
	{
		if (pdwOutObservedMask)
			*pdwOutObservedMask = dwObservedMask;
		if (pdwOutSubmittedMask)
			*pdwOutSubmittedMask = dwSubmittedMask;
		if (pdwOutApplicableMask)
			*pdwOutApplicableMask = 0u;
		return;
	}

	if (!m_bDX11TerrainResourcesReady)
	{
		if (!InitializeDX11TerrainResources(pDevice))
		{
			if (pdwOutObservedMask)
				*pdwOutObservedMask = dwObservedMask;
			if (pdwOutSubmittedMask)
				*pdwOutSubmittedMask = 0u;
			if (pdwOutApplicableMask)
				*pdwOutApplicableMask = 0u;
			return;
		}
	}

	BuildDX11TerrainVertexBuffers(pDevice);
	if (m_bDX11ShadowResourcesReady)
	{
		RenderShadowCastersDX11(pContext);
		RenderShadowReceiversDX11(pContext);
	}
	else
	{
		m_bDX11ShadowReceiverActive = false;
	}

	m_PatchVector.clear();
	__RenderTerrain_RecurseRenderQuadTree(m_pRootNode, true);

	std::sort(m_PatchVector.begin(), m_PatchVector.end());
	SetPatchDrawVector();

	m_iRenderedPatchNum = static_cast<int>(m_PatchDrawStructVector.size());
	m_iRenderedSplatNum = 0;
	m_iRenderedSplatNumSqSum = 0.0f;
	m_RenderedTextureNumVector.clear();

	if (m_iRenderedPatchNum > 0)
	{
		CCamera* pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
		const D3DXMATRIX matView = pCurrentCamera ? pCurrentCamera->GetViewMatrix() : CGraphicBase::GetViewMatrix();
		const D3DXMATRIX matProj = CGraphicBase::GetProjMatrix();
		const D3DXMATRIX matViewProj = matView * matProj;

		if (__RenderTerrain_DX11HardwareTransformPatch(pDevice, pContext, matViewProj))
			dwSubmittedMask |= CGraphicDeviceDX11::WORLD_TERRAIN_DX11;
	}

	std::sort(m_RenderedTextureNumVector.begin(), m_RenderedTextureNumVector.end());

	static DWORD s_dwTerrainRenderLogTick = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwTerrainRenderLogTick || (dwNow - s_dwTerrainRenderLogTick) >= 2000u)
	{
		s_dwTerrainRenderLogTick = dwNow;
		TraceError("DX11_TERRAIN_RENDER patches=%d splat=%d textures=%u",
			m_iRenderedPatchNum,
			m_iRenderedSplatNum,
			static_cast<unsigned int>(m_RenderedTextureNumVector.size()));
	}

	if (pdwOutObservedMask)
		*pdwOutObservedMask = dwObservedMask;
	if (pdwOutSubmittedMask)
		*pdwOutSubmittedMask = dwSubmittedMask;
	if (pdwOutApplicableMask)
		*pdwOutApplicableMask = dwApplicableMask;
}

bool CMapOutdoor::__CreateDX11WaterShaders(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11WaterShaders();

	static const char* kWaterVS = R"(
cbuffer WaterCB : register(b0)
{
	row_major float4x4 g_mWorldViewProj;
	float4 g_vWaterParams;
	float4 g_vTint;
	float4 g_vDebugParams;
};
struct VS_IN
{
	float3 Pos : POSITION;
	float4 Color : COLOR0;
};
struct VS_OUT
{
	float4 Pos : SV_POSITION;
	float4 Color : COLOR0;
	float2 UV : TEXCOORD0;
};
VS_OUT main(VS_IN i)
{
	VS_OUT o;
	float3 pos = i.Pos;
	pos.z += g_vDebugParams.x;
	o.Pos = mul(float4(pos, 1.0f), g_mWorldViewProj);
	// Push water slightly toward camera in clip space to avoid depth fighting with terrain.
	o.Pos.z -= g_vDebugParams.w * o.Pos.w;
	o.Color = i.Color;
	o.UV = i.Pos.xy * g_vWaterParams.xy + g_vWaterParams.zw;
	return o;
}
)";

	static const char* kWaterPS = R"(
cbuffer WaterCB : register(b0)
{
	row_major float4x4 g_mWorldViewProj;
	float4 g_vWaterParams;
	float4 g_vTint;
	float4 g_vDebugParams;
};
Texture2D g_txWater : register(t0);
SamplerState g_smWater : register(s0);
struct VS_OUT
{
	float4 Pos : SV_POSITION;
	float4 Color : COLOR0;
	float2 UV : TEXCOORD0;
};
float4 main(VS_OUT i) : SV_Target
{
	float4 col;
	if (g_vDebugParams.z > 0.5f)
	{
		col = g_vTint;
	}
	else
	{
		float4 tex = g_txWater.Sample(g_smWater, i.UV);
		float3 texturedRgb = tex.rgb * i.Color.rgb * g_vTint.rgb;
		float luma = dot(texturedRgb, float3(0.299f, 0.587f, 0.114f));
		if (luma < 0.05f)
			texturedRgb = lerp(g_vTint.rgb, texturedRgb, 0.35f);
		col.rgb = texturedRgb;
		// Legacy water takes alpha from the per-vertex shoreline depth only.
		col.a = i.Color.a * g_vTint.a;
	}
	col.a = saturate(max(col.a, g_vDebugParams.y));
	return col;
}
)";

	ID3DBlob* pVSBlob = nullptr;
	ID3DBlob* pPSBlob = nullptr;

	HRESULT hr = CompileDX11WorldShader(kWaterVS, "main", "vs_4_0", &pVSBlob, "water_vs");
	if (FAILED(hr) || !pVSBlob)
		return false;

	hr = CompileDX11WorldShader(kWaterPS, "main", "ps_4_0", &pPSBlob, "water_ps");
	if (FAILED(hr) || !pPSBlob)
	{
		safe_release(pVSBlob);
		return false;
	}

	hr = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pDX11WaterVertexShader);
	if (FAILED(hr))
	{
		safe_release(pVSBlob);
		safe_release(pPSBlob);
		return false;
	}

	hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pDX11WaterPixelShader);
	if (FAILED(hr))
	{
		safe_release(pVSBlob);
		safe_release(pPSBlob);
		__DestroyDX11WaterShaders();
		return false;
	}

	const D3D11_INPUT_ELEMENT_DESC kLayout[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DX11WaterVertex, kPosition), D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(DX11WaterVertex, kColor), D3D11_INPUT_PER_VERTEX_DATA, 0},
	};

	hr = pDevice->CreateInputLayout(kLayout, _countof(kLayout), pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pDX11WaterInputLayout);
	safe_release(pVSBlob);
	safe_release(pPSBlob);
	if (FAILED(hr))
	{
		__DestroyDX11WaterShaders();
		return false;
	}

	D3D11_BUFFER_DESC kCBDesc = {};
	kCBDesc.ByteWidth = sizeof(DX11WaterShaderCB);
	kCBDesc.Usage = D3D11_USAGE_DYNAMIC;
	kCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	kCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	hr = pDevice->CreateBuffer(&kCBDesc, nullptr, &m_pDX11WaterConstantBuffer);
	if (FAILED(hr))
	{
		__DestroyDX11WaterShaders();
		return false;
	}

	return true;
}

void CMapOutdoor::__DestroyDX11WaterShaders()
{
	safe_release(m_pDX11WaterConstantBuffer);
	safe_release(m_pDX11WaterInputLayout);
	safe_release(m_pDX11WaterPixelShader);
	safe_release(m_pDX11WaterVertexShader);
}

bool CMapOutdoor::__CreateDX11WaterPipelineStates(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11WaterPipelineStates();

	D3D11_BLEND_DESC kBlendDesc = {};
	kBlendDesc.RenderTarget[0].BlendEnable = TRUE;
	kBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	kBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	kBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	kBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	kBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	kBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	kBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(pDevice->CreateBlendState(&kBlendDesc, &m_pDX11WaterBlendState)))
		return false;

	D3D11_DEPTH_STENCIL_DESC kDepthDesc = {};
	kDepthDesc.DepthEnable = TRUE;
	kDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	kDepthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;  // DX9 parity: depth test enabled, no depth write
	kDepthDesc.StencilEnable = FALSE;
	if (FAILED(pDevice->CreateDepthStencilState(&kDepthDesc, &m_pDX11WaterDepthState)))
		return false;

	D3D11_RASTERIZER_DESC kRasterDesc = {};
	kRasterDesc.FillMode = D3D11_FILL_SOLID;
	kRasterDesc.CullMode = D3D11_CULL_NONE;
	kRasterDesc.DepthClipEnable = TRUE;
	if (FAILED(pDevice->CreateRasterizerState(&kRasterDesc, &m_pDX11WaterRasterState)))
		return false;

	D3D11_SAMPLER_DESC kSamplerDesc = {};
	kSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	kSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(pDevice->CreateSamplerState(&kSamplerDesc, &m_pDX11WaterSamplerState)))
		return false;

	return true;
}

void CMapOutdoor::__DestroyDX11WaterPipelineStates()
{
	safe_release(m_pDX11WaterBlendState);
	safe_release(m_pDX11WaterDepthState);
	safe_release(m_pDX11WaterRasterState);
	safe_release(m_pDX11WaterSamplerState);
}

bool CMapOutdoor::InitializeDX11WaterResources(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (!__CreateDX11WaterShaders(pDevice))
		return false;

	if (!__CreateDX11WaterPipelineStates(pDevice))
	{
		__DestroyDX11WaterShaders();
		return false;
	}

	int iLoadedFrames = 0;
	int iSrvFrames = 0;
	for (int i = 0; i < 30; ++i)
	{
		safe_release(m_apDX11WaterTextureSRV[i]);

		CGraphicImage* pImage = m_WaterInstances[i].GetGraphicImagePointer();
		if (!pImage)
			continue;
		++iLoadedFrames;

		CGraphicTexture* pTexture = pImage->GetTexturePointer();
		if (!pTexture)
			continue;

		ID3D11ShaderResourceView* pSRV = pTexture->GetD3D11TextureSRV();
		if (!pSRV)
			continue;

		pSRV->AddRef();
		m_apDX11WaterTextureSRV[i] = pSRV;
		++iSrvFrames;
	}

	m_bDX11WaterResourcesReady = true;
	TraceError("DX11_WATER_RESOURCES loaded_frames=%d srv_frames=%d", iLoadedFrames, iSrvFrames);
	if (0 == iSrvFrames)
		TraceError("DX11_WATER_RESOURCES reason=no_srv_frames");
	return true;
}

void CMapOutdoor::DestroyDX11WaterResources()
{
	m_bDX11WaterResourcesReady = false;

	__DestroyDX11WaterPipelineStates();
	__DestroyDX11WaterShaders();
	for (int i = 0; i < 30; ++i)
		safe_release(m_apDX11WaterTextureSRV[i]);

	m_iDX11LastRenderedWaterPatchCount = 0;
	m_iDX11LastObservedWaterPatchCount = 0;
}

void CMapOutdoor::__RenderWater_DX11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	static bool s_bLoggedWaterEntry = false;
	if (!s_bLoggedWaterEntry)
	{
		TraceError("DX11_WATER_DIAG: Function called pDevice=%p pContext=%p", pDevice, pContext);
		s_bLoggedWaterEntry = true;
	}

	if (!pDevice || !pContext || !IsVisiblePart(PART_WATER) || !m_pTerrainPatchProxyList)
	{
		if (!s_bLoggedWaterEntry)
			TraceError("DX11_WATER_DIAG: Early return - pDevice=%p pContext=%p PART_WATER=%d proxies=%p",
				pDevice, pContext, IsVisiblePart(PART_WATER), m_pTerrainPatchProxyList);
		m_iDX11LastRenderedWaterPatchCount = 0;
		m_iDX11LastObservedWaterPatchCount = 0;
		return;
	}

	if (!m_pDX11WaterVertexShader || !m_pDX11WaterPixelShader || !m_pDX11WaterInputLayout || !m_pDX11WaterConstantBuffer || !m_pDX11WaterBlendState || !m_pDX11WaterDepthState || !m_pDX11WaterRasterState || !m_pDX11WaterSamplerState)
	{
		if (!s_bLoggedWaterEntry)
			TraceError("DX11_WATER_DIAG: Resources missing - VS=%p PS=%p Layout=%p CB=%p Blend=%p Depth=%p Rast=%p Sampler=%p",
				m_pDX11WaterVertexShader, m_pDX11WaterPixelShader, m_pDX11WaterInputLayout,
				m_pDX11WaterConstantBuffer, m_pDX11WaterBlendState, m_pDX11WaterDepthState,
				m_pDX11WaterRasterState, m_pDX11WaterSamplerState);
		m_iDX11LastRenderedWaterPatchCount = 0;
		m_iDX11LastObservedWaterPatchCount = 0;
		return;
	}

	DX11WaterPassStateScope kScope(pContext);

	const float afBlendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	pContext->OMSetBlendState(m_pDX11WaterBlendState, afBlendFactor, 0xFFFFFFFFu);
	pContext->OMSetDepthStencilState(m_pDX11WaterDepthState, 0u);
	pContext->RSSetState(m_pDX11WaterRasterState);
	pContext->IASetInputLayout(m_pDX11WaterInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(m_pDX11WaterVertexShader, nullptr, 0u);
	pContext->PSSetShader(m_pDX11WaterPixelShader, nullptr, 0u);
	pContext->PSSetSamplers(0u, 1u, &m_pDX11WaterSamplerState);

	const DWORD dwNow = ELTimer_GetMSec();
	const float fWaterZLift = DX11RuntimeConfig::kWaterSurfaceZLift;
	const float fWaterMinAlpha = DX11RuntimeConfig::kWaterMinAlpha;
	const float fWaterDepthBias = DX11RuntimeConfig::kWaterDepthBiasClip;
	const bool bWaterDebugSolidColor = DX11RuntimeConfig::kWaterDebugSolidColor;

	const int iWaterFrameIndex = static_cast<int>((dwNow / 80u) % 30u);
	ID3D11ShaderResourceView* pWaterSRV = m_apDX11WaterTextureSRV[iWaterFrameIndex];
	const char* szWaterSRVSource = "animated";
	if (!pWaterSRV)
	{
		for (int i = 0; i < 30; ++i)
		{
			if (m_apDX11WaterTextureSRV[i])
			{
				pWaterSRV = m_apDX11WaterTextureSRV[i];
				break;
			}
		}
	}
	if (!pWaterSRV)
	{
		pWaterSRV = m_pDX11TerrainDefaultTextureSRV;
		szWaterSRVSource = "terrain_default";
	}
	if (!pWaterSRV)
	{
		pWaterSRV = m_pDX11TerrainMissingTextureSRV;
		szWaterSRVSource = "missing";
	}
	pContext->PSSetShaderResources(0u, 1u, &pWaterSRV);

	CCamera* pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
	const D3DXMATRIX matView = pCurrentCamera ? pCurrentCamera->GetViewMatrix() : CGraphicBase::GetViewMatrix();
	const D3DXMATRIX matProj = CGraphicBase::GetProjMatrix();

	DX11WaterShaderCB kWaterCB = {};
	kWaterCB.matWorldViewProj = matView * matProj;
	// The 30 source textures already contain the complete animation. Legacy
	// applies only world-space UV scale and does not add a second UV scroll.
	kWaterCB.vWaterParams = D3DXVECTOR4(m_fWaterTexCoordBase, -m_fWaterTexCoordBase, 0.0f, 0.0f);
	kWaterCB.vTint = D3DXVECTOR4(0.72f, 0.86f, 1.00f, 1.0f);
	kWaterCB.vDebugParams = D3DXVECTOR4(fWaterZLift, fWaterMinAlpha, bWaterDebugSolidColor ? 1.0f : 0.0f, fWaterDepthBias);

	D3D11_MAPPED_SUBRESOURCE kMapped = {};
	if (SUCCEEDED(pContext->Map(m_pDX11WaterConstantBuffer, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &kMapped)))
	{
		memcpy(kMapped.pData, &kWaterCB, sizeof(kWaterCB));
		pContext->Unmap(m_pDX11WaterConstantBuffer, 0u);
	}
	pContext->VSSetConstantBuffers(0u, 1u, &m_pDX11WaterConstantBuffer);
	pContext->PSSetConstantBuffers(0u, 1u, &m_pDX11WaterConstantBuffer);

	UINT uVisiblePatchCount = 0u;
	UINT uWaterFlagCount = 0u;
	UINT uWaterGeomCount = 0u;
	UINT uVBReadyCount = 0u;
	UINT uCacheReadyCount = 0u;
	UINT uRenderedPatchCount = 0u;
	bool bFallbackFullScanUsed = false;

	auto RenderWaterProxy = [&](CTerrainPatchProxy* pProxy, bool bCountVisible)
	{
		if (!pProxy || !pProxy->isUsed())
			return;

		if (bCountVisible)
			++uVisiblePatchCount;

		const bool bWaterFlag = pProxy->isWaterExists();
		if (bWaterFlag)
			++uWaterFlagCount;

		CTerrainPatch* pTerrainPatch = pProxy->GetTerrainPatch();
		if (!pTerrainPatch)
			return;

		const UINT uWaterFaceCount = pTerrainPatch->GetWaterFaceCount();
		const bool bHasCachedSource = pTerrainPatch->HasDX11CachedWaterSourceVertex();
		const bool bHasWaterGeometry = bWaterFlag && (uWaterFaceCount > 0u);
		if (!bHasWaterGeometry)
			return;

		++uWaterGeomCount;

		ID3D11Buffer* pWaterVB = pTerrainPatch->GetDX11WaterVertexBuffer();
		if (pWaterVB)
			++uVBReadyCount;
		if (!pWaterVB && bHasCachedSource)
		{
			++uCacheReadyCount;
			pTerrainPatch->BuildDX11WaterVertexBufferFromCache(pDevice);
			pWaterVB = pTerrainPatch->GetDX11WaterVertexBuffer();
			if (pWaterVB)
				++uVBReadyCount;
		}
		if (!pWaterVB)
			return;

		UINT uVertexCount = pTerrainPatch->GetDX11WaterVertexCount();
		if (0u == uVertexCount)
			uVertexCount = uWaterFaceCount * 3u;
		if (0u == uVertexCount)
			return;

		// Log first few patches for diagnostics
		static int s_iWaterPatchLogCount = 0;
		if (s_iWaterPatchLogCount < 3)
		{
			TraceError("DX11_WATER_GEOM: patch=%d vertices=%u", s_iWaterPatchLogCount, uVertexCount);
			++s_iWaterPatchLogCount;
		}

		UINT uStride = sizeof(DX11WaterVertex);
		UINT uOffset = 0u;
		pContext->IASetVertexBuffers(0u, 1u, &pWaterVB, &uStride, &uOffset);
		pContext->Draw(uVertexCount, 0u);
		++uRenderedPatchCount;
	};

	for (const TPatchDrawStruct& rkPatchDraw : m_PatchDrawStructVector)
		RenderWaterProxy(rkPatchDraw.pTerrainPatchProxy, true);

	if (0u == uWaterGeomCount)
	{
		bFallbackFullScanUsed = true;
		for (WORD wPatchIndex = 0; wPatchIndex < m_wPatchCount; ++wPatchIndex)
			RenderWaterProxy(&m_pTerrainPatchProxyList[wPatchIndex], false);
	}

	m_iDX11LastObservedWaterPatchCount = static_cast<int>(uWaterGeomCount);
	m_iDX11LastRenderedWaterPatchCount = static_cast<int>(uRenderedPatchCount);

	static DWORD s_dwWaterRenderLogTick = 0u;
	if (0u == s_dwWaterRenderLogTick || (dwNow - s_dwWaterRenderLogTick) >= 2000u)
	{
		s_dwWaterRenderLogTick = dwNow;
		TraceError("DX11_WATER_RENDER patches=%d", m_iDX11LastRenderedWaterPatchCount);
		TraceError("DX11_WATER_MATRIX_PARITY view_src=%s z_lift=%.2f min_alpha=%.2f depth_bias=%.4f debug_solid=%d",
			pCurrentCamera ? "current_camera" : "graphic_base",
			fWaterZLift,
			fWaterMinAlpha,
			fWaterDepthBias,
			bWaterDebugSolidColor ? 1 : 0);
		TraceError("DX11_WATER_SRV_SOURCE selected=%s", szWaterSRVSource);
		TraceError("DX11_WATER_PATCH_DIAG visible=%u water_flag=%u water_geom=%u vb_ready=%u cache_rebuild=%u rendered=%u fallback_full_scan=%u",
			static_cast<unsigned int>(uVisiblePatchCount),
			static_cast<unsigned int>(uWaterFlagCount),
			static_cast<unsigned int>(uWaterGeomCount),
			static_cast<unsigned int>(uVBReadyCount),
			static_cast<unsigned int>(uCacheReadyCount),
			static_cast<unsigned int>(uRenderedPatchCount),
			bFallbackFullScanUsed ? 1u : 0u);
	}
}

void CMapOutdoor::RenderWaterDX11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	if (!m_bDX11WaterResourcesReady)
		return;
	__RenderWater_DX11(pDevice, pContext);
}
bool CMapOutdoor::IsDynamicShadowCaster(float fHeight) const
{
	return fHeight >= 30.0f;
}

void CMapOutdoor::ClearCharacterShadowCasters()
{
	m_kVct_pkCharacterShadowCasters.clear();
	m_kVct_pkObjectShadowCasters.clear();
}

void CMapOutdoor::RegisterCharacterShadowCaster(CGraphicThingInstance* pInstance)
{
	if (!pInstance)
		return;
	m_kVct_pkCharacterShadowCasters.push_back(pInstance);
}

void CMapOutdoor::RegisterObjectShadowCaster(CGraphicThingInstance* pInstance)
{
	if (!pInstance)
		return;
	m_kVct_pkObjectShadowCasters.push_back(pInstance);
}

void CMapOutdoor::__RenderObjectsDX11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	if (!pDevice || !pContext)
		return;

	ID3D11RasterizerState* pPrevRasterState = nullptr;
	pContext->RSGetState(&pPrevRasterState);

	static ID3D11RasterizerState* s_pObjectRasterCullCW = nullptr;
	if (!s_pObjectRasterCullCW)
	{
		D3D11_RASTERIZER_DESC kRasterDesc = {};
		kRasterDesc.FillMode = D3D11_FILL_SOLID;
		// Keep front-face culling for parity with legacy object winding in this pass.
		kRasterDesc.CullMode = D3D11_CULL_FRONT;
		kRasterDesc.FrontCounterClockwise = FALSE;
		kRasterDesc.DepthClipEnable = TRUE;
		kRasterDesc.ScissorEnable = FALSE;
		kRasterDesc.MultisampleEnable = FALSE;
		kRasterDesc.AntialiasedLineEnable = FALSE;

		const HRESULT hRaster = pDevice->CreateRasterizerState(&kRasterDesc, &s_pObjectRasterCullCW);
		if (FAILED(hRaster) || !s_pObjectRasterCullCW)
		{
			static DWORD s_dwObjectRasterFailLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwObjectRasterFailLogTick || (dwNow - s_dwObjectRasterFailLogTick) >= 2000u)
			{
				s_dwObjectRasterFailLogTick = dwNow;
				TraceError("DX11_OBJECT_PIPELINE_BIND_FAIL reason=object_raster_create_failed hr=0x%08X", static_cast<unsigned int>(hRaster));
			}
			pContext->RSSetState(pPrevRasterState);
			safe_release(pPrevRasterState);
			return;
		}
	}

	pContext->RSSetState(s_pObjectRasterCullCW);

	if (!m_pDX11ObjectVS || !m_pDX11ObjectPS || !m_pDX11ObjectInputLayout || !m_pDX11ObjectConstantBuffer)
	{
		if (!__CreateDX11ObjectShaders(pDevice))
		{
			static DWORD s_dwObjectPipelineFailLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwObjectPipelineFailLogTick || (dwNow - s_dwObjectPipelineFailLogTick) >= 2000u)
			{
				s_dwObjectPipelineFailLogTick = dwNow;
				TraceError("DX11_OBJECT_PIPELINE_BIND_FAIL reason=resources_not_ready");
			}
			pContext->RSSetState(pPrevRasterState);
			safe_release(pPrevRasterState);
			return;
		}
	}

	CGrannyModelInstance::SetDX11ObjectShaders(
		m_pDX11ObjectVS,
		m_pDX11ObjectPS,
		m_pDX11ObjectInputLayout,
		m_pDX11ObjectConstantBuffer,
		m_pDX11ObjectSamplerState,
		D3DXVECTOR4(0.0f, 0.0f, -1.0f, 0.0f),
		D3DXVECTOR4(0.62f, 0.62f, 0.62f, 0.0f));

	std::vector<CGraphicThingInstance*> kOpaqueThings;
	std::vector<CGraphicThingInstance*> kBlendThings;
	std::vector<CDungeonBlock*> kDungeonBlocks;

	kOpaqueThings.reserve(1024);
	kBlendThings.reserve(512);
	kDungeonBlocks.reserve(128);

	for (int i = 0; i < AROUND_AREA_NUM; ++i)
	{
		CArea* pArea = nullptr;
		if (!GetAreaPointer(i, &pArea) || !pArea)
			continue;

		pArea->CollectRenderingObject(kOpaqueThings);
		pArea->CollectBlendRenderingObject(kBlendThings);
		pArea->CollectDungeonRenderingObject(kDungeonBlocks);
	}

	for (CGraphicThingInstance* pThing : kOpaqueThings)
	{
		if (pThing)
			pThing->Render();
	}

	for (CDungeonBlock* pDungeonBlock : kDungeonBlocks)
	{
		if (pDungeonBlock)
			pDungeonBlock->Render();
	}

	std::sort(kBlendThings.begin(), kBlendThings.end(), CMapOutdoor_LessThingInstancePtrRenderOrder());
	for (CGraphicThingInstance* pThing : kBlendThings)
	{
		if (pThing)
			pThing->BlendRender();
	}
	pContext->RSSetState(pPrevRasterState);
	safe_release(pPrevRasterState);
}

void CMapOutdoor::__RenderCharactersDX11(ID3D11DeviceContext* pContext)
{
	if (!pContext || m_kVct_pkCharacterShadowCasters.empty())
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

	CGrannyModelInstance::SetDX11ObjectShaders(
		m_pDX11ObjectVS,
		m_pDX11ObjectPS,
		m_pDX11ObjectInputLayout,
		m_pDX11ObjectConstantBuffer,
		m_pDX11ObjectSamplerState,
		D3DXVECTOR4(0.0f, 0.0f, -1.0f, 0.0f),
		D3DXVECTOR4(0.62f, 0.62f, 0.62f, 0.0f));

	CCamera* pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
	if (!pCurrentCamera)
		return;

	const D3DXMATRIX matView = pCurrentCamera->GetViewMatrix();
	const D3DXMATRIX matProj = CGraphicBase::GetProjMatrix();
	const D3DXMATRIX matViewProj = matView * matProj;

	std::set<CGraphicThingInstance*> kRendered;
	for (CGraphicThingInstance* pThingInstance : m_kVct_pkCharacterShadowCasters)
	{
		if (!pThingInstance)
			continue;
		if (!kRendered.insert(pThingInstance).second)
			continue;

		pThingInstance->RenderWithOneTextureDX11(pContext, matViewProj);
	}
}

void CMapOutdoor::DestroyDX11ShadowResources()
{
	m_bDX11ShadowResourcesReady = false;
	m_bDX11ShadowReceiverActive = false;
	m_bDX11ShadowFallbackActive = false;
	m_dwDX11ShadowLastFallbackLogMS = 0u;

	for (int i = 0; i < 3; ++i)
		safe_release(m_apDX11ShadowCascadeDSV[i]);

	safe_release(m_pDX11ShadowMapTextureArray);
	safe_release(m_pDX11ShadowMapArraySRV);
	safe_release(m_pDX11ShadowFrameConstantBuffer);
	safe_release(m_pDX11ShadowObjectConstantBuffer);
	safe_release(m_pDX11ShadowComparisonSampler);
	safe_release(m_pDX11ShadowRasterizerState);
	safe_release(m_pDX11ShadowDepthState);
	safe_release(m_pDX11ShadowCasterVertexShader);
	safe_release(m_pDX11ShadowCasterPixelShader);
	safe_release(m_pDX11ShadowReceiverVertexShader);
	safe_release(m_pDX11ShadowReceiverPixelShader);
	m_kVct_pkCharacterShadowCasters.clear();
	m_kVct_pkObjectShadowCasters.clear();
}

bool CMapOutdoor::InitializeDX11ShadowResources(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (m_bDX11ShadowResourcesReady)
		return true;

	if (!m_pDX11ObjectVS || !m_pDX11ObjectPS || !m_pDX11ObjectInputLayout || !m_pDX11ObjectConstantBuffer)
	{
		if (!__CreateDX11ObjectShaders(pDevice))
		{
			__LogDX11ShadowFallback("object_shader_unavailable");
			return false;
		}
	}

	if (!__CreateDX11ShadowShaders(pDevice))
	{
		DestroyDX11ShadowResources();
		__LogDX11ShadowFallback("create_shadow_shaders_failed");
		return false;
	}

	if (!__CreateDX11ShadowPipelineStates(pDevice))
	{
		DestroyDX11ShadowResources();
		__LogDX11ShadowFallback("create_shadow_states_failed");
		return false;
	}

	if (!__CreateDX11ShadowMapResources(pDevice))
	{
		DestroyDX11ShadowResources();
		__LogDX11ShadowFallback("create_shadow_map_failed");
		return false;
	}

	__UpdateDX11ShadowCascadeMatrices();
	m_bDX11ShadowResourcesReady = true;
	m_bDX11ShadowReceiverActive = false;
	m_bDX11ShadowFallbackActive = false;

	TraceError(
		"DX11_DYNAMIC_SHADOW_RUNTIME state=ready map_size=%u cascades=3",
		static_cast<unsigned int>(m_uDX11ShadowMapSize));
	return true;
}

void CMapOutdoor::RenderShadowCastersDX11(ID3D11DeviceContext* pContext)
{
	if (!m_bDX11ShadowResourcesReady || !pContext)
		return;

	if (!m_pDX11ShadowCasterVertexShader || !m_pDX11ShadowObjectConstantBuffer || !m_pDX11ObjectInputLayout || !m_pDX11ShadowDepthState || !m_pDX11ShadowRasterizerState)
	{
		__LogDX11ShadowFallback("shadow_caster_pipeline_missing");
		return;
	}

	if (!m_pDX11ShadowMapArraySRV || !m_apDX11ShadowCascadeDSV[0] || !m_apDX11ShadowCascadeDSV[1] || !m_apDX11ShadowCascadeDSV[2])
	{
		__LogDX11ShadowFallback("shadow_map_resources_missing");
		return;
	}

	__UpdateDX11ShadowCascadeMatrices();
	DX11ShadowCasterStateScope kStateScope(pContext);

	// Unbind receiver SRV before depth writes.
	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(6u, 1u, &pNullSRV);

	const D3D11_VIEWPORT kShadowViewport = {
		0.0f,
		0.0f,
		static_cast<FLOAT>(m_uDX11ShadowMapSize),
		static_cast<FLOAT>(m_uDX11ShadowMapSize),
		0.0f,
		1.0f
	};

	std::set<CGraphicThingInstance*> kUniqueCharacterCasters;
	for (CGraphicThingInstance* pInstance : m_kVct_pkCharacterShadowCasters)
	{
		if (pInstance && pInstance->isShow())
			kUniqueCharacterCasters.insert(pInstance);
	}

	std::set<CGraphicThingInstance*> kUniqueObjectCasters;
	for (CGraphicThingInstance* pInstance : m_kVct_pkObjectShadowCasters)
	{
		if (pInstance && pInstance->isShow())
			kUniqueObjectCasters.insert(pInstance);
	}

	m_dwDX11ShadowLastCasterActors = static_cast<DWORD>(kUniqueCharacterCasters.size());
	m_dwDX11ShadowLastCasterObjects = static_cast<DWORD>(kUniqueObjectCasters.size());
	m_dwDX11ShadowLastCasterSpeedTree = 0u;
	m_dwDX11ShadowLastSubmittedSpeedTreeCount = 0u;

	const D3DXVECTOR3 v3LightDir = __GetDX11ShadowLightDirection();
	const D3DXVECTOR4 v4LightDir(v3LightDir.x, v3LightDir.y, v3LightDir.z, 0.0f);
	const D3DXVECTOR4 v4Ambient(0.0f, 0.0f, 0.0f, 0.0f);

	for (UINT uCascade = 0u; uCascade < 3u; ++uCascade)
	{
		pContext->OMSetRenderTargets(0u, nullptr, m_apDX11ShadowCascadeDSV[uCascade]);
		// Depth-only shadow pass must not inherit a color PS from previous passes.
		// Individual caster paths (for example alpha-clip vegetation) can still bind their own shadow-safe PS explicitly.
		pContext->PSSetShader(nullptr, nullptr, 0u);
		pContext->ClearDepthStencilView(m_apDX11ShadowCascadeDSV[uCascade], D3D11_CLEAR_DEPTH, 1.0f, 0u);
		pContext->RSSetState(m_pDX11ShadowRasterizerState);
		pContext->RSSetViewports(1u, &kShadowViewport);
		pContext->OMSetDepthStencilState(m_pDX11ShadowDepthState, 0u);

		CGrannyModelInstance::SetDX11ObjectShaders(
			m_pDX11ShadowCasterVertexShader,
			nullptr,
			m_pDX11ObjectInputLayout,
			m_pDX11ShadowObjectConstantBuffer,
			nullptr,
			v4LightDir,
			v4Ambient);

		for (CGraphicThingInstance* pInstance : kUniqueCharacterCasters)
			pInstance->RenderToShadowMapDX11(pContext, m_akDX11ShadowLightViewProj[uCascade]);

		for (CGraphicThingInstance* pInstance : kUniqueObjectCasters)
			pInstance->RenderToShadowMapDX11(pContext, m_akDX11ShadowLightViewProj[uCascade]);

		CSpeedTreeForestDirectX& rkForest = CSpeedTreeForestDirectX::Instance();
		if (rkForest.IsDX11SpeedTreeResourcesReady())
		{
			DirectX::SimpleMath::Matrix matShadowViewProj;
			memcpy(&matShadowViewProj, &m_akDX11ShadowLightViewProj[uCascade], sizeof(matShadowViewProj));
			if (rkForest.RenderToShadowMapDX11(pContext, matShadowViewProj))
			{
				m_dwDX11ShadowLastCasterSpeedTree = std::max<DWORD>(
					m_dwDX11ShadowLastCasterSpeedTree,
					rkForest.GetLastDX11SubmittedInstanceCount());
			}
		}
	}

	// Restore default object shader bindings for subsequent world color passes.
	if (m_pDX11ObjectVS && m_pDX11ObjectPS && m_pDX11ObjectInputLayout && m_pDX11ObjectConstantBuffer)
	{
		CGrannyModelInstance::SetDX11ObjectShaders(
			m_pDX11ObjectVS,
			m_pDX11ObjectPS,
			m_pDX11ObjectInputLayout,
			m_pDX11ObjectConstantBuffer,
			m_pDX11ObjectSamplerState,
			D3DXVECTOR4(0.0f, 0.0f, -1.0f, 0.0f),
			D3DXVECTOR4(0.62f, 0.62f, 0.62f, 0.0f));
	}

	m_dwDX11ShadowLastSubmittedSpeedTreeCount = m_dwDX11ShadowLastCasterSpeedTree;
	m_bDX11ShadowFallbackActive = false;

	static DWORD s_dwShadowCasterLogTick = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwShadowCasterLogTick || (dwNow - s_dwShadowCasterLogTick) >= 2000u)
	{
		s_dwShadowCasterLogTick = dwNow;
		TraceError(
			"DX11_DYNAMIC_SHADOW_CASTERS characters=%u objects=%u speedtree=%u receiver=%d",
			static_cast<unsigned int>(m_dwDX11ShadowLastCasterActors),
			static_cast<unsigned int>(m_dwDX11ShadowLastCasterObjects),
			static_cast<unsigned int>(m_dwDX11ShadowLastSubmittedSpeedTreeCount),
			m_bDX11ShadowReceiverActive ? 1 : 0);
	}
}

void CMapOutdoor::RenderShadowReceiversDX11(ID3D11DeviceContext* pContext)
{
	if (!m_bDX11ShadowResourcesReady || !pContext)
		return;

	if (!m_pDX11ShadowMapArraySRV || !m_pDX11ShadowComparisonSampler || !m_pDX11ShadowFrameConstantBuffer)
	{
		m_bDX11ShadowReceiverActive = false;
		__LogDX11ShadowFallback("shadow_receiver_resources_missing");
		return;
	}

	DX11ShadowFrameCB kFrameCB = {};
	kFrameCB.matLightViewProj[0] = m_akDX11ShadowLightViewProj[0];
	kFrameCB.matLightViewProj[1] = m_akDX11ShadowLightViewProj[1];
	kFrameCB.matLightViewProj[2] = m_akDX11ShadowLightViewProj[2];
	kFrameCB.vCascadeSplits = D3DXVECTOR4(
		m_afDX11ShadowCascadeSplits[0],
		m_afDX11ShadowCascadeSplits[1],
		m_afDX11ShadowCascadeSplits[2],
		m_afDX11ShadowCascadeSplits[3]);
	const D3DXVECTOR3 v3LightDir = __GetDX11ShadowLightDirection();
	kFrameCB.vLightDir = D3DXVECTOR4(v3LightDir.x, v3LightDir.y, v3LightDir.z, 1.0f);
	pContext->UpdateSubresource(m_pDX11ShadowFrameConstantBuffer, 0u, nullptr, &kFrameCB, 0u, 0u);

	pContext->VSSetConstantBuffers(3u, 1u, &m_pDX11ShadowFrameConstantBuffer);
	pContext->PSSetConstantBuffers(3u, 1u, &m_pDX11ShadowFrameConstantBuffer);
	pContext->PSSetShaderResources(6u, 1u, &m_pDX11ShadowMapArraySRV);
	pContext->PSSetSamplers(6u, 1u, &m_pDX11ShadowComparisonSampler);
	m_bDX11ShadowReceiverActive = true;
	m_bDX11ShadowFallbackActive = false;
}

bool CMapOutdoor::__CreateDX11ShadowShaders(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11ShadowShaders();

	static const char* c_szShadowCasterVS = R"(
cbuffer ObjectCB : register(b0)
{
	row_major float4x4 gWorld;
	row_major float4x4 gViewProj;
	float4 gLightDir;
	float4 gAmbient;
};
struct VSIn
{
	float3 pos : POSITION;
	float3 nrm : NORMAL;
	float2 uv  : TEXCOORD0;
};
struct VSOut
{
	float4 pos : SV_POSITION;
};
VSOut main(VSIn i)
{
	VSOut o;
	float4 wpos = mul(float4(i.pos, 1.0f), gWorld);
	o.pos = mul(wpos, gViewProj);
	return o;
}
)";

	static const char* c_szShadowReceiverVS = R"(
struct VSIn
{
	float3 pos : POSITION;
	float3 nrm : NORMAL;
	float2 uv  : TEXCOORD0;
};
struct VSOut
{
	float4 pos : SV_POSITION;
};
VSOut main(VSIn i)
{
	VSOut o;
	o.pos = float4(i.pos, 1.0f);
	return o;
}
)";

	static const char* c_szShadowReceiverPS = R"(
float4 main() : SV_TARGET
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
)";

	ID3DBlob* pCasterVSBlob = nullptr;
	ID3DBlob* pReceiverVSBlob = nullptr;
	ID3DBlob* pReceiverPSBlob = nullptr;

	const bool bCompiled =
		SUCCEEDED(CompileDX11WorldShader(c_szShadowCasterVS, "main", "vs_4_0", &pCasterVSBlob, "shadow_caster_vs")) &&
		SUCCEEDED(CompileDX11WorldShader(c_szShadowReceiverVS, "main", "vs_4_0", &pReceiverVSBlob, "shadow_receiver_vs")) &&
		SUCCEEDED(CompileDX11WorldShader(c_szShadowReceiverPS, "main", "ps_4_0", &pReceiverPSBlob, "shadow_receiver_ps"));
	if (!bCompiled || !pCasterVSBlob || !pReceiverVSBlob || !pReceiverPSBlob)
	{
		safe_release(pCasterVSBlob);
		safe_release(pReceiverVSBlob);
		safe_release(pReceiverPSBlob);
		return false;
	}

	HRESULT hr = pDevice->CreateVertexShader(
		pCasterVSBlob->GetBufferPointer(),
		pCasterVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11ShadowCasterVertexShader);
	if (FAILED(hr))
	{
		TraceError("DX11_DYNAMIC_SHADOW_SHADER_FAIL stage=caster_vs hr=0x%08X", static_cast<unsigned int>(hr));
		safe_release(pCasterVSBlob);
		safe_release(pReceiverVSBlob);
		safe_release(pReceiverPSBlob);
		__DestroyDX11ShadowShaders();
		return false;
	}

	hr = pDevice->CreateVertexShader(
		pReceiverVSBlob->GetBufferPointer(),
		pReceiverVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11ShadowReceiverVertexShader);
	if (FAILED(hr))
	{
		TraceError("DX11_DYNAMIC_SHADOW_SHADER_FAIL stage=receiver_vs hr=0x%08X", static_cast<unsigned int>(hr));
		safe_release(pCasterVSBlob);
		safe_release(pReceiverVSBlob);
		safe_release(pReceiverPSBlob);
		__DestroyDX11ShadowShaders();
		return false;
	}

	hr = pDevice->CreatePixelShader(
		pReceiverPSBlob->GetBufferPointer(),
		pReceiverPSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11ShadowReceiverPixelShader);
	if (FAILED(hr))
	{
		TraceError("DX11_DYNAMIC_SHADOW_SHADER_FAIL stage=receiver_ps hr=0x%08X", static_cast<unsigned int>(hr));
		safe_release(pCasterVSBlob);
		safe_release(pReceiverVSBlob);
		safe_release(pReceiverPSBlob);
		__DestroyDX11ShadowShaders();
		return false;
	}

	safe_release(pCasterVSBlob);
	safe_release(pReceiverVSBlob);
	safe_release(pReceiverPSBlob);
	return true;
}

void CMapOutdoor::__DestroyDX11ShadowShaders()
{
	safe_release(m_pDX11ShadowCasterVertexShader);
	safe_release(m_pDX11ShadowCasterPixelShader);
	safe_release(m_pDX11ShadowReceiverVertexShader);
	safe_release(m_pDX11ShadowReceiverPixelShader);
}

bool CMapOutdoor::__CreateDX11ShadowPipelineStates(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11ShadowPipelineStates();

	D3D11_BUFFER_DESC kFrameCBDesc = {};
	kFrameCBDesc.ByteWidth = sizeof(DX11ShadowFrameCB);
	kFrameCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	kFrameCBDesc.Usage = D3D11_USAGE_DEFAULT;
	if (FAILED(pDevice->CreateBuffer(&kFrameCBDesc, nullptr, &m_pDX11ShadowFrameConstantBuffer)))
	{
		TraceError("DX11_DYNAMIC_SHADOW_STATE_FAIL resource=frame_cb");
		__DestroyDX11ShadowPipelineStates();
		return false;
	}

	D3D11_BUFFER_DESC kObjectCBDesc = {};
	kObjectCBDesc.ByteWidth = sizeof(DX11ObjectShaderCB);
	kObjectCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	kObjectCBDesc.Usage = D3D11_USAGE_DEFAULT;
	if (FAILED(pDevice->CreateBuffer(&kObjectCBDesc, nullptr, &m_pDX11ShadowObjectConstantBuffer)))
	{
		TraceError("DX11_DYNAMIC_SHADOW_STATE_FAIL resource=object_cb");
		__DestroyDX11ShadowPipelineStates();
		return false;
	}

	D3D11_SAMPLER_DESC kSamplerDesc = {};
	// Slot s6 is consumed by world shaders via regular SamplerState + SampleLevel.
	// Using a comparison sampler here triggers DEVICE_DRAW_SAMPLER_MISMATCH (#390).
	kSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	kSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	kSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	kSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	kSamplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	kSamplerDesc.MinLOD = 0.0f;
	kSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(pDevice->CreateSamplerState(&kSamplerDesc, &m_pDX11ShadowComparisonSampler)))
	{
		TraceError("DX11_DYNAMIC_SHADOW_STATE_FAIL resource=sampler");
		__DestroyDX11ShadowPipelineStates();
		return false;
	}

	D3D11_RASTERIZER_DESC kRasterDesc = {};
	kRasterDesc.FillMode = D3D11_FILL_SOLID;
	kRasterDesc.CullMode = D3D11_CULL_BACK;
	kRasterDesc.DepthBias = 64;
	kRasterDesc.SlopeScaledDepthBias = 2.5f;
	kRasterDesc.DepthBiasClamp = 0.0f;
	kRasterDesc.DepthClipEnable = TRUE;
	if (FAILED(pDevice->CreateRasterizerState(&kRasterDesc, &m_pDX11ShadowRasterizerState)))
	{
		TraceError("DX11_DYNAMIC_SHADOW_STATE_FAIL resource=rasterizer");
		__DestroyDX11ShadowPipelineStates();
		return false;
	}

	D3D11_DEPTH_STENCIL_DESC kDepthDesc = {};
	kDepthDesc.DepthEnable = TRUE;
	kDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	kDepthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	kDepthDesc.StencilEnable = FALSE;
	if (FAILED(pDevice->CreateDepthStencilState(&kDepthDesc, &m_pDX11ShadowDepthState)))
	{
		TraceError("DX11_DYNAMIC_SHADOW_STATE_FAIL resource=depth_state");
		__DestroyDX11ShadowPipelineStates();
		return false;
	}

	return true;
}

void CMapOutdoor::__DestroyDX11ShadowPipelineStates()
{
	safe_release(m_pDX11ShadowFrameConstantBuffer);
	safe_release(m_pDX11ShadowObjectConstantBuffer);
	safe_release(m_pDX11ShadowComparisonSampler);
	safe_release(m_pDX11ShadowRasterizerState);
	safe_release(m_pDX11ShadowDepthState);
}

bool CMapOutdoor::__CreateDX11ShadowMapResources(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11ShadowMapResources();

	D3D11_TEXTURE2D_DESC kTexDesc = {};
	kTexDesc.Width = m_uDX11ShadowMapSize;
	kTexDesc.Height = m_uDX11ShadowMapSize;
	kTexDesc.MipLevels = 1u;
	kTexDesc.ArraySize = 3u;
	kTexDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	kTexDesc.SampleDesc.Count = 1u;
	kTexDesc.Usage = D3D11_USAGE_DEFAULT;
	kTexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(pDevice->CreateTexture2D(&kTexDesc, nullptr, &m_pDX11ShadowMapTextureArray)))
	{
		TraceError("DX11_DYNAMIC_SHADOW_RESOURCE_FAIL resource=texture_array");
		__DestroyDX11ShadowMapResources();
		return false;
	}

	for (UINT uCascade = 0u; uCascade < 3u; ++uCascade)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC kDSVDesc = {};
		kDSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
		kDSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		kDSVDesc.Texture2DArray.ArraySize = 1u;
		kDSVDesc.Texture2DArray.FirstArraySlice = uCascade;
		kDSVDesc.Texture2DArray.MipSlice = 0u;

		if (FAILED(pDevice->CreateDepthStencilView(m_pDX11ShadowMapTextureArray, &kDSVDesc, &m_apDX11ShadowCascadeDSV[uCascade])))
		{
			TraceError("DX11_DYNAMIC_SHADOW_RESOURCE_FAIL resource=dsv cascade=%u", static_cast<unsigned int>(uCascade));
			__DestroyDX11ShadowMapResources();
			return false;
		}
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC kSRVDesc = {};
	kSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	kSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	kSRVDesc.Texture2DArray.ArraySize = 3u;
	kSRVDesc.Texture2DArray.FirstArraySlice = 0u;
	kSRVDesc.Texture2DArray.MipLevels = 1u;
	kSRVDesc.Texture2DArray.MostDetailedMip = 0u;

	if (FAILED(pDevice->CreateShaderResourceView(m_pDX11ShadowMapTextureArray, &kSRVDesc, &m_pDX11ShadowMapArraySRV)))
	{
		TraceError("DX11_DYNAMIC_SHADOW_RESOURCE_FAIL resource=srv");
		__DestroyDX11ShadowMapResources();
		return false;
	}

	return true;
}

void CMapOutdoor::__DestroyDX11ShadowMapResources()
{
	for (int i = 0; i < 3; ++i)
		safe_release(m_apDX11ShadowCascadeDSV[i]);
	safe_release(m_pDX11ShadowMapArraySRV);
	safe_release(m_pDX11ShadowMapTextureArray);
}

void CMapOutdoor::__UpdateDX11ShadowCascadeMatrices()
{
	CCamera* pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
	if (!pCurrentCamera)
	{
		for (int i = 0; i < 3; ++i)
			D3DXMatrixIdentity(&m_akDX11ShadowLightViewProj[i]);
		return;
	}

	const DirectX::SimpleMath::Vector3& v3TargetSM = pCurrentCamera->GetTarget();
	const D3DXVECTOR3 v3Target(v3TargetSM.x, v3TargetSM.y, v3TargetSM.z);
	const D3DXVECTOR3 v3LightDir = __GetDX11ShadowLightDirection();
	const D3DXVECTOR3 v3UpDefault(0.0f, 0.0f, 1.0f);
	const D3DXVECTOR3 v3UpFallback(0.0f, 1.0f, 0.0f);
	D3DXVECTOR3 v3Up = v3UpDefault;
	if (fabsf(D3DXVec3Dot(&v3LightDir, &v3UpDefault)) > 0.95f)
		v3Up = v3UpFallback;

	for (UINT uCascade = 0u; uCascade < 3u; ++uCascade)
	{
		const float fCascadeFar = m_afDX11ShadowCascadeSplits[uCascade + 1u];

		// Metin2 uses an orbit camera. Anchoring the shadow volume in view direction
		// makes the same receiver enter and leave a cascade when camera yaw changes.
		// Keep every cascade centered on the camera target (normally the player).
		const D3DXVECTOR3 v3CascadeCenter = v3Target;
		const float fExtent = std::max(160.0f, fCascadeFar);
		const float fLightBackDistance = fExtent + 2000.0f;
		D3DXVECTOR3 v3LightEye(
			v3CascadeCenter.x - v3LightDir.x * fLightBackDistance,
			v3CascadeCenter.y - v3LightDir.y * fLightBackDistance,
			v3CascadeCenter.z - v3LightDir.z * fLightBackDistance);

		D3DXMATRIX matLightView;
		D3DXMATRIX matLightProj;
		D3DXMatrixLookAtRH(&matLightView, &v3LightEye, &v3CascadeCenter, &v3Up);
		D3DXMatrixOrthoRH(&matLightProj, fExtent * 2.0f, fExtent * 2.0f, 1.0f, fLightBackDistance * 2.0f);
		m_akDX11ShadowLightViewProj[uCascade] = matLightView * matLightProj;
	}
}
D3DXVECTOR3 CMapOutdoor::__GetDX11ShadowLightDirection() const
{
	D3DXVECTOR3 v3LightDir = m_v3DX11ShadowLightDir;
	if (m_kDX11EnvironmentBridgeState.bValid)
	{
		const D3DXVECTOR3& v3EnvDir = m_kDX11EnvironmentBridgeState.v3BackgroundLightDirection;
		const float fEnvDirLengthSq = (v3EnvDir.x * v3EnvDir.x) + (v3EnvDir.y * v3EnvDir.y) + (v3EnvDir.z * v3EnvDir.z);
		if (fEnvDirLengthSq > 0.0001f)
			v3LightDir = v3EnvDir;
	}

	const float fLengthSq = (v3LightDir.x * v3LightDir.x) + (v3LightDir.y * v3LightDir.y) + (v3LightDir.z * v3LightDir.z);
	if (fLengthSq <= 0.0001f)
		v3LightDir = D3DXVECTOR3(-0.577f, -0.577f, 0.577f);

	D3DXVec3Normalize(&v3LightDir, &v3LightDir);
	return v3LightDir;
}

void CMapOutdoor::__LogDX11ShadowFallback(const char* c_szReason)
{
	const DWORD dwNow = ELTimer_GetMSec();
	const DWORD dwInterval = 2000u;
	if (0u != m_dwDX11ShadowLastFallbackLogMS && (dwNow - m_dwDX11ShadowLastFallbackLogMS) < dwInterval)
		return;

	m_dwDX11ShadowLastFallbackLogMS = dwNow;
	m_bDX11ShadowFallbackActive = true;
	TraceError(
		"DX11_DYNAMIC_SHADOW_FALLBACK reason=%s resources_ready=%d receiver_active=%d",
		(c_szReason && c_szReason[0]) ? c_szReason : "unknown",
		m_bDX11ShadowResourcesReady ? 1 : 0,
		m_bDX11ShadowReceiverActive ? 1 : 0);
}

bool CMapOutdoor::__CreateDX11ObjectShaders(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	__DestroyDX11ObjectShaders();

static const char* c_szObjectVS = R"(
cbuffer ObjectCB : register(b0)
{
	row_major float4x4 gWorld;
	row_major float4x4 gViewProj;
	float4 gLightDir;
	float4 gAmbient;
	float4 gViewPosAndSpecPower;
	float4 gSpecularColorAndEnable;
};
cbuffer ShadowFrameCB : register(b3)
{
	row_major float4x4 g_mShadowLightViewProj0;
	row_major float4x4 g_mShadowLightViewProj1;
	row_major float4x4 g_mShadowLightViewProj2;
	float4 g_vShadowCascadeSplits;
	float4 g_vShadowLightDir;
};
struct VSIn
{
	float3 pos : POSITION;
	float3 nrm : NORMAL;
	float2 uv  : TEXCOORD0;
};
struct VSOut
{
	float4 pos  : SV_POSITION;
	float3 nrmW : TEXCOORD0;
	float2 uv   : TEXCOORD1;
	float4 shadowH : TEXCOORD2;
	float3 wpos : TEXCOORD3;
};
VSOut main(VSIn i)
{
	VSOut o;
	float4 wpos = mul(float4(i.pos, 1.0f), gWorld);
	o.pos = mul(wpos, gViewProj);
	o.nrmW = normalize(mul(float4(i.nrm, 0.0f), gWorld).xyz);
	o.uv = i.uv;
	o.shadowH = mul(wpos, g_mShadowLightViewProj0);
	o.wpos = wpos.xyz;
	return o;
}
)";

static const char* c_szObjectPS = R"(
Texture2D gDiffuseTex : register(t0);
Texture2D gOpacityTex : register(t1);
Texture2DArray gShadowMap : register(t6);
SamplerState gSamp : register(s0);
cbuffer ObjectCB : register(b0)
{
	row_major float4x4 gWorld;
	row_major float4x4 gViewProj;
	float4 gLightDir;
	float4 gAmbient;
	float4 gViewPosAndSpecPower;
	float4 gSpecularColorAndEnable;
};
cbuffer ShadowFrameCB : register(b3)
{
	row_major float4x4 g_mShadowLightViewProj0;
	row_major float4x4 g_mShadowLightViewProj1;
	row_major float4x4 g_mShadowLightViewProj2;
	float4 g_vShadowCascadeSplits;
	float4 g_vShadowLightDir;
};
struct PSIn
{
	float4 pos  : SV_POSITION;
	float3 nrmW : TEXCOORD0;
	float2 uv   : TEXCOORD1;
	float4 shadowH : TEXCOORD2;
	float3 wpos : TEXCOORD3;
};
float SampleShadow(float4 vShadowPos)
{
	if (g_vShadowLightDir.w < 0.5f)
		return 1.0f;

	float fInvW = rcp(max(vShadowPos.w, 1e-4f));
	vShadowPos.xyz *= fInvW;

	float2 vShadowUV = vShadowPos.xy * float2(0.5f, -0.5f) + 0.5f;
	float fShadowDepth = vShadowPos.z - 0.00030f;
	if (vShadowUV.x < 0.0f || vShadowUV.x > 1.0f ||
		vShadowUV.y < 0.0f || vShadowUV.y > 1.0f ||
		fShadowDepth < 0.0f || fShadowDepth > 1.0f)
	{
		return 1.0f;
	}

	// ps_4_0-safe shadow compare (manual compare instead of SampleCmp).
	float fMapDepth = gShadowMap.SampleLevel(gSamp, float3(vShadowUV, 0.0f), 0.0f).r;
	return (fMapDepth + 0.00015f >= fShadowDepth) ? 1.0f : 0.0f;
}
float4 main(PSIn i) : SV_TARGET
{
	float3 N = normalize(i.nrmW);
	float3 L = normalize(-gLightDir.xyz);
	float NdotL = saturate(dot(N, L));
	float shadow = SampleShadow(i.shadowH);
	float lit = saturate(gAmbient.x + (1.0f - gAmbient.x) * (NdotL * shadow));
	float3 V = normalize(gViewPosAndSpecPower.xyz - i.wpos);
	float3 H = normalize(L + V);
	float fSpec = pow(saturate(dot(N, H)), max(gViewPosAndSpecPower.w, 1.0f));
	fSpec *= (NdotL * gSpecularColorAndEnable.w);
	float4 base = gDiffuseTex.Sample(gSamp, i.uv);
	if (gAmbient.w > 0.5f)
	{
		float4 op = gOpacityTex.Sample(gSamp, i.uv);
		base.a *= op.r;
		if (base.a <= 0.001f)
			discard;
	}
	else
	{
		base.a = 1.0f;
	}
	base.rgb = saturate(base.rgb * lit + (gSpecularColorAndEnable.rgb * fSpec * shadow));
	return base;
}
)";

	ID3DBlob* pVSBlob = nullptr;
	ID3DBlob* pPSBlob = nullptr;
	if (FAILED(CompileDX11WorldShader(c_szObjectVS, "main", "vs_4_0", &pVSBlob, "object_vs")) || !pVSBlob)
	{
		safe_release(pVSBlob);
		return false;
	}

	D3D_FEATURE_LEVEL eFeatureLevel = D3D_FEATURE_LEVEL_11_0;
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
		eFeatureLevel = pDX11Device->GetFeatureLevel();

	const char* c_szPSTarget = "ps_4_0";
	if (FAILED(CompileDX11WorldShader(c_szObjectPS, "main", c_szPSTarget, &pPSBlob, "object_ps")) || !pPSBlob)
	{
		safe_release(pVSBlob);
		safe_release(pPSBlob);
		return false;
	}

	HRESULT hr = pDevice->CreateVertexShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11ObjectVS);
	if (FAILED(hr))
	{
		TraceError("DX11_OBJECT_SHADER_CREATE_FAIL stage=vs hr=0x%08X", static_cast<unsigned int>(hr));
		safe_release(pVSBlob);
		safe_release(pPSBlob);
		__DestroyDX11ObjectShaders();
		return false;
	}

	hr = pDevice->CreatePixelShader(
		pPSBlob->GetBufferPointer(),
		pPSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11ObjectPS);
	if (FAILED(hr))
	{
		TraceError("DX11_OBJECT_SHADER_CREATE_FAIL stage=ps hr=0x%08X", static_cast<unsigned int>(hr));
		safe_release(pVSBlob);
		safe_release(pPSBlob);
		__DestroyDX11ObjectShaders();
		return false;
	}

	static DWORD s_dwObjectShaderModelLogTick = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwObjectShaderModelLogTick || (dwNow - s_dwObjectShaderModelLogTick) >= 5000u)
	{
		s_dwObjectShaderModelLogTick = dwNow;
		TraceError(
			"DX11_OBJECT_SHADER_MODEL feature_level=0x%04X ps_target=%s shadow=%d",
			static_cast<unsigned int>(eFeatureLevel),
			c_szPSTarget,
			1);
	}

	D3D11_INPUT_ELEMENT_DESC akLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	hr = pDevice->CreateInputLayout(
		akLayout,
		ARRAYSIZE(akLayout),
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		&m_pDX11ObjectInputLayout);

	if (pVSBlob)
	{
		pVSBlob->Release();
		pVSBlob = nullptr;
	}
	if (pPSBlob)
	{
		pPSBlob->Release();
		pPSBlob = nullptr;
	}

	if (FAILED(hr))
	{
		TraceError("DX11_OBJECT_SHADER_CREATE_FAIL stage=layout hr=0x%08X", static_cast<unsigned int>(hr));
		__DestroyDX11ObjectShaders();
		return false;
	}

	D3D11_BUFFER_DESC kCBDesc;
	ZeroMemory(&kCBDesc, sizeof(kCBDesc));
	kCBDesc.ByteWidth = sizeof(DX11ObjectShaderCB);
	kCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	kCBDesc.Usage = D3D11_USAGE_DEFAULT;
	hr = pDevice->CreateBuffer(&kCBDesc, nullptr, &m_pDX11ObjectConstantBuffer);
	if (FAILED(hr))
	{
		TraceError("DX11_OBJECT_SHADER_CREATE_FAIL stage=cb hr=0x%08X", static_cast<unsigned int>(hr));
		__DestroyDX11ObjectShaders();
		return false;
	}

	D3D11_SAMPLER_DESC kSamplerDesc;
	ZeroMemory(&kSamplerDesc, sizeof(kSamplerDesc));
	kSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	kSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = pDevice->CreateSamplerState(&kSamplerDesc, &m_pDX11ObjectSamplerState);
	if (FAILED(hr))
	{
		TraceError("DX11_OBJECT_SHADER_CREATE_FAIL stage=sampler hr=0x%08X", static_cast<unsigned int>(hr));
		__DestroyDX11ObjectShaders();
		return false;
	}

	CGrannyModelInstance::SetDX11ObjectShaders(
		m_pDX11ObjectVS,
		m_pDX11ObjectPS,
		m_pDX11ObjectInputLayout,
		m_pDX11ObjectConstantBuffer,
		m_pDX11ObjectSamplerState,
		D3DXVECTOR4(0.0f, 0.0f, -1.0f, 0.0f),
		D3DXVECTOR4(0.62f, 0.62f, 0.62f, 0.0f));

	TraceError("DX11_OBJECT_PIPELINE_READY vs=%p ps=%p layout=%p cb=%p sampler=%p",
		m_pDX11ObjectVS, m_pDX11ObjectPS, m_pDX11ObjectInputLayout, m_pDX11ObjectConstantBuffer, m_pDX11ObjectSamplerState);
	return true;
}

void CMapOutdoor::__DestroyDX11ObjectShaders()
{
	safe_release(m_pDX11ObjectSamplerState);
	safe_release(m_pDX11ObjectConstantBuffer);
	safe_release(m_pDX11ObjectInputLayout);
	safe_release(m_pDX11ObjectPS);
	safe_release(m_pDX11ObjectVS);

	CGrannyModelInstance::SetDX11ObjectShaders(
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		D3DXVECTOR4(0.0f, 0.0f, -1.0f, 0.0f),
		D3DXVECTOR4(0.62f, 0.62f, 0.62f, 0.0f));
}


















