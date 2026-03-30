#include "StdAfx.h"
#include "MapOutdoor.h"
#include "TerrainPatch.h"
#include "AreaTerrain.h"
#include "TerrainQuadtree.h"

#include "EterLib/Camera.h"
#include "EterLib/StateManager11.h"
#include "EterLib/GrpDeviceDX11.h"
#include "EterLib/GrpTextureDX11.h"
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

	inline D3D11_PRIMITIVE_TOPOLOGY ToDX11Topology(D3DPRIMITIVETYPE ePrimitiveType)
	{
		return (D3DPT_TRIANGLESTRIP == ePrimitiveType) ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}

	inline UINT GetIndexCountFromPrimitiveCount(WORD wPrimitiveCount, D3DPRIMITIVETYPE ePrimitiveType)
	{
		if (D3DPT_TRIANGLESTRIP == ePrimitiveType)
			return static_cast<UINT>(wPrimitiveCount + 2u);
		return static_cast<UINT>(wPrimitiveCount * 3u);
	}

	HRESULT CompileDX11WorldShader(
		const char* szSource,
		const char* szEntry,
		const char* szTarget,
		ID3DBlob** ppBlobOut)
	{
		if (!szSource || !szEntry || !szTarget || !ppBlobOut)
			return E_INVALIDARG;

		*ppBlobOut = nullptr;
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
			if (pErrorBlob && pErrorBlob->GetBufferPointer())
				TraceError("DX11_OBJECT_SHADER_COMPILE_FAIL entry=%s target=%s hr=0x%08X error=%s", szEntry, szTarget, static_cast<unsigned int>(hr), static_cast<const char*>(pErrorBlob->GetBufferPointer()));
			else
				TraceError("DX11_OBJECT_SHADER_COMPILE_FAIL entry=%s target=%s hr=0x%08X", szEntry, szTarget, static_cast<unsigned int>(hr));
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
		if (rkTexture.pTextureSRV)
			return rkTexture.pTextureSRV;

		CGraphicTexture* pGraphicTexture = rkTexture.ImageInstance.GetTexturePointer();
		if (pGraphicTexture)
		{
			rkTexture.pTextureSRV = pGraphicTexture->GetD3D11TextureSRV();
			if (rkTexture.pTextureSRV)
				return rkTexture.pTextureSRV;
		}

		if (!szFilename || !*szFilename)
			szFilename = rkTexture.stFilename.c_str();
	}

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
};
VS_OUT main(VS_IN i)
{
	VS_OUT o;
	o.Pos = mul(float4(i.Pos, 1.0f), g_mWorldViewProj);
	o.Normal = normalize(i.Normal);
	o.UV = i.UV * g_vUVTransform.xy + g_vUVTransform.zw;
	o.AlphaUV = i.Pos.xy * g_vAlphaUVTransform.xy + g_vAlphaUVTransform.zw;
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
Texture2D g_txBase : register(t0);
SamplerState g_smBase : register(s0);
struct VS_OUT
{
	float4 Pos : SV_POSITION;
	float3 Normal : TEXCOORD0;
	float2 UV : TEXCOORD1;
	float2 AlphaUV : TEXCOORD2;
};
float4 main(VS_OUT i) : SV_Target
{
	float3 N = normalize(i.Normal);
	float3 L = normalize(-g_vLightDir.xyz);
	float ndl = saturate(dot(N, L));
	float4 baseCol = g_txBase.Sample(g_smBase, i.UV);
	float3 lit = baseCol.rgb * (g_vAmbient.rgb + ndl);
	return float4(lit, 1.0f);
}
)";

	ID3DBlob* pVSBlob = nullptr;
	ID3DBlob* pPSBlob = nullptr;

	HRESULT hr = CompileDX11WorldShader(kTerrainVS, "main", "vs_4_0", &pVSBlob);
	if (FAILED(hr) || !pVSBlob)
		return false;

	hr = CompileDX11WorldShader(kTerrainPS, "main", "ps_4_0", &pPSBlob);
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
	safe_release(pVSBlob);
	safe_release(pPSBlob);
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
};
VS_OUT main(VS_IN i)
{
	VS_OUT o;
	o.Pos = mul(float4(i.Pos, 1.0f), g_mWorldViewProj);
	o.Normal = normalize(i.Normal);
	o.UV = i.UV * g_vUVTransform.xy + g_vUVTransform.zw;
	o.AlphaUV = i.Pos.xy * g_vAlphaUVTransform.xy + g_vAlphaUVTransform.zw;
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
Texture2D g_txBase : register(t0);
Texture2D g_txAlpha : register(t1);
SamplerState g_smBase : register(s0);
SamplerState g_smAlpha : register(s1);
struct VS_OUT
{
	float4 Pos : SV_POSITION;
	float3 Normal : TEXCOORD0;
	float2 UV : TEXCOORD1;
	float2 AlphaUV : TEXCOORD2;
};
float4 main(VS_OUT i) : SV_Target
{
	float4 baseCol = g_txBase.Sample(g_smBase, i.UV);
	float alpha = g_txAlpha.Sample(g_smAlpha, i.AlphaUV).r;
	return float4(baseCol.rgb, alpha);
}
)";

	ID3DBlob* pVSBlob = nullptr;
	ID3DBlob* pPSBlob = nullptr;
	HRESULT hr = CompileDX11WorldShader(kSplatVS, "main", "vs_4_0", &pVSBlob);
	if (FAILED(hr) || !pVSBlob)
		return false;

	hr = CompileDX11WorldShader(kSplatPS, "main", "ps_4_0", &pPSBlob);
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
	safe_release(pVSBlob);
	safe_release(pPSBlob);
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

	float fLODLevel1Distance = __GetNoFogDistance();
	float fLODLevel2Distance = __GetFogDistance();
	BYTE byCurrentLOD = 0u;
	WORD wPrimitiveCount = 0u;
	D3DPRIMITIVETYPE ePrimitiveType = D3DPT_TRIANGLELIST;
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

		ID3D11Buffer* pIB = m_IndexBuffer[byCurrentLOD].GetD3D11Buffer();
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
			ID3D11ShaderResourceView* pTextureSRV = __GetSplatTextureSRV(nullptr, rkTexture.stFilename.c_str(), pTerrain, dwTextureIndex);
			if (!pTextureSRV)
				continue;

			ID3D11ShaderResourceView* pAlphaSRV = __GetOrCreateDX11SplatAlphaSRV(pTerrain, dwTextureIndex);
			kLayers.push_back({dwTextureIndex, pTextureSRV, pAlphaSRV, &rkTexture});
		}

		if (kLayers.empty())
		{
			SLayer kFallback = {};
			kFallback.dwTextureIndex = 0u;
			kFallback.pTextureSRV = m_pDX11TerrainDefaultTextureSRV ? m_pDX11TerrainDefaultTextureSRV : m_pDX11TerrainMissingTextureSRV;
			kFallback.pAlphaSRV = nullptr;
			kFallback.pTextureMeta = nullptr;
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
					pCB->vUVTransform = D3DXVECTOR4(rkLayer.pTextureMeta->UScale, rkLayer.pTextureMeta->VScale, rkLayer.pTextureMeta->UOffset, rkLayer.pTextureMeta->VOffset);
				else
					pCB->vUVTransform = D3DXVECTOR4(1.0f, 1.0f, 0.0f, 0.0f);

				const float fPatchWorldSize = static_cast<float>(CTerrainImpl::PATCH_XSIZE * CTerrainImpl::CELLSCALE);
				const float fInvPatchWorldSize = (fPatchWorldSize > 0.0f) ? (1.0f / fPatchWorldSize) : 0.0f;
				pCB->vAlphaUVTransform = D3DXVECTOR4(
					fInvPatchWorldSize,
					fInvPatchWorldSize,
					-pPatchProxy->GetMinX() * fInvPatchWorldSize,
					-pPatchProxy->GetMinY() * fInvPatchWorldSize);
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
	if (0u == s_dwTerrainVBLogTick || (dwNow - s_dwTerrainVBLogTick) >= 5000u)
	{
		s_dwTerrainVBLogTick = dwNow;
		TraceError("DX11_TERRAIN_VB_BUILD built=%u failed=%u patch_count=%u",
			dwBuilt,
			dwFailed,
			static_cast<unsigned int>(m_wPatchCount));
	}

	return true;
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

bool CMapOutdoor::InitializeDX11WaterResources(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	m_bDX11WaterResourcesReady = true;
	return true;
}

void CMapOutdoor::DestroyDX11WaterResources()
{
	m_bDX11WaterResourcesReady = false;

	safe_release(m_pDX11WaterVertexShader);
	safe_release(m_pDX11WaterPixelShader);
	safe_release(m_pDX11WaterInputLayout);
	safe_release(m_pDX11WaterConstantBuffer);
	safe_release(m_pDX11WaterBlendState);
	safe_release(m_pDX11WaterSamplerState);
	for (int i = 0; i < 30; ++i)
		safe_release(m_apDX11WaterTextureSRV[i]);

	m_iDX11LastRenderedWaterPatchCount = 0;
}

void CMapOutdoor::__RenderWater_DX11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	(void)pDevice;
	(void)pContext;

	if (!IsVisiblePart(PART_WATER) || !m_pTerrainPatchProxyList)
	{
		m_iDX11LastRenderedWaterPatchCount = 0;
		return;
	}

	int iWaterPatchCount = 0;
	for (WORD wPatchIndex = 0; wPatchIndex < m_wPatchCount; ++wPatchIndex)
	{
		CTerrainPatchProxy& rkProxy = m_pTerrainPatchProxyList[wPatchIndex];
		if (!rkProxy.isUsed() || !rkProxy.isWaterExists())
			continue;
		++iWaterPatchCount;
	}

	m_iDX11LastRenderedWaterPatchCount = iWaterPatchCount;

	static DWORD s_dwWaterRenderLogTick = 0u;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == s_dwWaterRenderLogTick || (dwNow - s_dwWaterRenderLogTick) >= 2000u)
	{
		s_dwWaterRenderLogTick = dwNow;
		TraceError("DX11_WATER_RENDER patches=%d", m_iDX11LastRenderedWaterPatchCount);
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

	CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
	DWORD dwOldCullMode = D3DCULL_CCW;
	if (pStateManager11)
	{
		dwOldCullMode = pStateManager11->GetRenderState(D3DRS_CULLMODE);
		// DX9 parity for world meshes.
		pStateManager11->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
		pStateManager11->ApplyState();
	}

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

	if (pStateManager11)
	{
		pStateManager11->SetRenderState(D3DRS_CULLMODE, dwOldCullMode);
		pStateManager11->ApplyState();
	}
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
};
VSOut main(VSIn i)
{
	VSOut o;
	float4 wpos = mul(float4(i.pos, 1.0f), gWorld);
	o.pos = mul(wpos, gViewProj);
	o.nrmW = normalize(mul(float4(i.nrm, 0.0f), gWorld).xyz);
	o.uv = i.uv;
	return o;
}
)";

	static const char* c_szObjectPS = R"(
Texture2D gDiffuseTex : register(t0);
Texture2D gOpacityTex : register(t1);
SamplerState gSamp : register(s0);
cbuffer ObjectCB : register(b0)
{
	row_major float4x4 gWorld;
	row_major float4x4 gViewProj;
	float4 gLightDir;
	float4 gAmbient;
};
struct PSIn
{
	float4 pos  : SV_POSITION;
	float3 nrmW : TEXCOORD0;
	float2 uv   : TEXCOORD1;
};
float4 main(PSIn i) : SV_TARGET
{
	float3 L = normalize(-gLightDir.xyz);
	float NdotL = saturate(dot(normalize(i.nrmW), L));
	float lit = saturate(gAmbient.x + (1.0f - gAmbient.x) * NdotL);
	float4 base = gDiffuseTex.Sample(gSamp, i.uv);
	if (gAmbient.w > 0.5f)
	{
		float4 op = gOpacityTex.Sample(gSamp, i.uv);
		base.a *= op.r;
	}
	if (base.a <= 0.001f)
		discard;
	base.rgb *= lit;
	return base;
}
)";

	ID3DBlob* pVSBlob = nullptr;
	ID3DBlob* pPSBlob = nullptr;
	if (FAILED(CompileDX11WorldShader(c_szObjectVS, "main", "vs_4_0", &pVSBlob)) || !pVSBlob)
	{
		safe_release(pVSBlob);
		return false;
	}

	if (FAILED(CompileDX11WorldShader(c_szObjectPS, "main", "ps_4_0", &pPSBlob)) || !pPSBlob)
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

	safe_release(pVSBlob);
	safe_release(pPSBlob);

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
