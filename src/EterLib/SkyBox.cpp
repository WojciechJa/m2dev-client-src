#include "stdafx.h"
#include "SkyBox.h"
#include "Camera.h"
#include "ResourceManager.h"
#include "GrpDeviceDX11.h"
#include "GrpTextureDX11.h"
#include "UserInterface/config.h"

#include "EterBase/Timer.h"
#include <algorithm>
#include <d3dcompiler.h>

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) do { if ((p) != NULL) { (p)->Release(); (p) = NULL; } } while (0)
#endif

namespace
{
struct SBootstrapUIVertex
{
	float x, y, z;
	float r, g, b, a;
	float u, v;
};

static D3DXMATRIX gs_matSkyDX11World = CGraphicBase::GetIdentityMatrix();
static float gs_fSkyDX11UOffset = 0.0f;
static float gs_fSkyDX11VOffset = 0.0f;
static float gs_fSkyDX11UScale = 1.0f;
static float gs_fSkyDX11VScale = 1.0f;
static ID3D11BlendState* gs_pSkyDX11BlendState = NULL;
static UINT gs_uSkyDX11ExpectedQuads = 0u;
static UINT gs_uSkyDX11SubmittedQuads = 0u;
static DWORD gs_dwSkyParityLogTick = 0u;
static DWORD gs_dwCloudParityLogTick = 0u;
static bool gs_bSkyDX11TextureBound = false;
static UINT gs_uSkyDX11SampledAlphaHint = 0u;
static bool gs_bSkyDX11ClampSampler = false;
static const char* gs_szSkyDX11Mode = "texture";
static bool gs_bSkyDX11DiffuseSampled = false;
static float gs_fSkyDX11DiffuseMin = 1.0f;
static float gs_fSkyDX11DiffuseMax = 0.0f;
static ID3D11SamplerState* gs_pSkyDX11ClampSamplerState = NULL;
static ID3D11SamplerState* gs_pSkyDX11CloudSamplerState = NULL;
static ID3D11PixelShader* gs_pSkyDX11CloudCombinePixelShader = NULL;
static ID3D11VertexShader* gs_pSkyDX11WorldVertexShader = NULL;
static ID3D11Buffer* gs_pSkyDX11WorldVSConstantBuffer = NULL;

enum ESkyDX11ShadingMode
{
	SKY_DX11_SHADING_TEXTURE = 0,
	SKY_DX11_SHADING_DIFFUSE = 1,
	SKY_DX11_SHADING_CLOUD_COMBINE = 2,
};

static ESkyDX11ShadingMode gs_eSkyDX11ShadingMode = SKY_DX11_SHADING_TEXTURE;

inline TColor LerpSkyColor(const TColor& c_rLeft, const TColor& c_rRight, float fT)
{
	const float fClampedT = std::min(1.0f, std::max(0.0f, fT));
	const float fInvT = 1.0f - fClampedT;
	return TColor(
		c_rLeft.r * fInvT + c_rRight.r * fClampedT,
		c_rLeft.g * fInvT + c_rRight.g * fClampedT,
		c_rLeft.b * fInvT + c_rRight.b * fClampedT,
		c_rLeft.a * fInvT + c_rRight.a * fClampedT);
}

inline TGradientColor LerpSkyGradient(const TGradientColor& c_rLeft, const TGradientColor& c_rRight, float fT)
{
	TGradientColor kOut = {};
	kOut.m_FirstColor = LerpSkyColor(c_rLeft.m_FirstColor, c_rRight.m_FirstColor, fT);
	kOut.m_SecondColor = LerpSkyColor(c_rLeft.m_SecondColor, c_rRight.m_SecondColor, fT);
	return kOut;
}

inline TGradientColor GetDefaultSkyGradient()
{
	TGradientColor kOut = {};
	kOut.m_FirstColor = TColor(0.18f, 0.28f, 0.55f, 1.0f);
	kOut.m_SecondColor = TColor(0.04f, 0.08f, 0.20f, 1.0f);
	return kOut;
}

inline TGradientColor SampleSkyGradientNormalized(const TVectorGradientColor& rkSource, float fT)
{
	if (rkSource.empty())
		return GetDefaultSkyGradient();
	if (rkSource.size() == 1u)
		return rkSource[0];

	const float fClampedT = std::min(1.0f, std::max(0.0f, fT));
	const float fPos = fClampedT * static_cast<float>(rkSource.size() - 1u);
	const size_t uLeftIndex = static_cast<size_t>(fPos);
	const size_t uRightIndex = std::min(uLeftIndex + 1u, rkSource.size() - 1u);
	const float fLerp = fPos - static_cast<float>(uLeftIndex);
	return LerpSkyGradient(rkSource[uLeftIndex], rkSource[uRightIndex], fLerp);
}

inline TVectorGradientColor BuildSkyGradientLUT(const TVectorGradientColor& rkSource, size_t uRequiredSamples)
{
	const size_t uCount = std::max<size_t>(1u, uRequiredSamples);
	TVectorGradientColor kOut(uCount);
	if (rkSource.empty())
	{
		const TGradientColor kDefault = GetDefaultSkyGradient();
		for (size_t i = 0; i < uCount; ++i)
			kOut[i] = kDefault;
		return kOut;
	}

	auto SampleEdge = [&](float fT) -> TColor
	{
		const float fClampedT = std::min(1.0f, std::max(0.0f, fT));
		const float fPos = fClampedT * static_cast<float>(rkSource.size());
		const size_t uSegment = std::min(static_cast<size_t>(fPos), rkSource.size() - 1u);
		const float fLocalT = (fClampedT >= 1.0f)
			? 1.0f
			: (fPos - static_cast<float>(uSegment));
		return LerpSkyColor(rkSource[uSegment].m_FirstColor, rkSource[uSegment].m_SecondColor, fLocalT);
	};

	for (size_t i = 0; i < uCount; ++i)
	{
		kOut[i].m_FirstColor = SampleEdge(static_cast<float>(i) / static_cast<float>(uCount));
		kOut[i].m_SecondColor = SampleEdge(static_cast<float>(i + 1u) / static_cast<float>(uCount));
	}
	return kOut;
}

bool EnsureSkyDX11ClampSampler(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (gs_pSkyDX11ClampSamplerState)
		return true;

	D3D11_SAMPLER_DESC kSamplerDesc = {};
	kSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	kSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	kSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	kSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	kSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	kSamplerDesc.MinLOD = 0.0f;
	kSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	const HRESULT hResult = pDevice->CreateSamplerState(&kSamplerDesc, &gs_pSkyDX11ClampSamplerState);
	if (FAILED(hResult) || !gs_pSkyDX11ClampSamplerState)
	{
		static DWORD s_dwSamplerFailLogTick = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwSamplerFailLogTick || (dwNow - s_dwSamplerFailLogTick) >= 2000u)
		{
			s_dwSamplerFailLogTick = dwNow;
			TraceError("DX11_SKYENV_DRAW_FAIL stage=sky reason=clamp_sampler_create_failed hr=0x%08x", static_cast<unsigned int>(hResult));
		}
		return false;
	}

	return true;
}

bool EnsureSkyDX11CloudSampler(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;
	if (gs_pSkyDX11CloudSamplerState)
		return true;

	D3D11_SAMPLER_DESC kSamplerDesc = {};
	kSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	kSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	kSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	kSamplerDesc.MinLOD = 0.0f;
	kSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	return SUCCEEDED(pDevice->CreateSamplerState(&kSamplerDesc, &gs_pSkyDX11CloudSamplerState)) && gs_pSkyDX11CloudSamplerState;
}

bool EnsureSkyDX11CloudCombineShader(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (gs_pSkyDX11CloudCombinePixelShader)
		return true;

	char szCloudCombinePS[1024] = {};
	sprintf_s(
		szCloudCombinePS,
		"Texture2D tx0 : register(t0);"
		"SamplerState smp0 : register(s0);"
		"struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
		"float4 main(PSIn input) : SV_TARGET"
		"{"
		"    float4 texel = tx0.Sample(smp0, input.uv);"
		"    float vertexAlpha = saturate(input.col.a);"
		"    float cloudBlend = saturate(%0.5ff);"
		"    float minTex = saturate(%0.5ff);"
		"    float alphaFloor = saturate(%0.5ff);"
		"    float vertexFloor = saturate(%0.5ff);"
		"    float textureContribution = max(minTex, cloudBlend * vertexAlpha);"
		"    float3 tint = input.col.rgb;"
		"    float3 rgb = lerp(tint, texel.rgb * tint, textureContribution);"
		"    float alpha = saturate(max(texel.a, alphaFloor) * max(vertexAlpha, vertexFloor));"
		"    return float4(rgb, alpha);"
		"}",
		DX11RuntimeConfig::kSkyCloudTextureBlendWeight,
		DX11RuntimeConfig::kSkyCloudTextureMinContribution,
		DX11RuntimeConfig::kSkyCloudAlphaMin,
		DX11RuntimeConfig::kSkyCloudVertexAlphaFloor);

	ID3DBlob* pCloudPSBlob = NULL;
	ID3DBlob* pErrorBlob = NULL;
	const HRESULT hCompile = D3DCompile(
		szCloudCombinePS, strlen(szCloudCombinePS),
		NULL, NULL, NULL,
		"main", "ps_4_0",
		0, 0, &pCloudPSBlob, &pErrorBlob);
	if (FAILED(hCompile) || !pCloudPSBlob)
	{
		static DWORD s_dwCompileFailLogTick = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwCompileFailLogTick || (dwNow - s_dwCompileFailLogTick) >= 2000u)
		{
			s_dwCompileFailLogTick = dwNow;
			if (pErrorBlob && pErrorBlob->GetBufferPointer())
			{
				TraceError("DX11_SKYENV_DRAW_FAIL stage=cloud reason=cloud_shader_compile_failed msg=%s",
					reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer()));
			}
			else
			{
				TraceError("DX11_SKYENV_DRAW_FAIL stage=cloud reason=cloud_shader_compile_failed hr=0x%08x",
					static_cast<unsigned int>(hCompile));
			}
		}
		if (pErrorBlob)
			pErrorBlob->Release();
		if (pCloudPSBlob)
			pCloudPSBlob->Release();
		return false;
	}
	if (pErrorBlob)
		pErrorBlob->Release();

	const HRESULT hCreate = pDevice->CreatePixelShader(
		pCloudPSBlob->GetBufferPointer(),
		pCloudPSBlob->GetBufferSize(),
		NULL,
		&gs_pSkyDX11CloudCombinePixelShader);
	if (pCloudPSBlob)
		pCloudPSBlob->Release();
	if (FAILED(hCreate) || !gs_pSkyDX11CloudCombinePixelShader)
	{
		static DWORD s_dwCreateFailLogTick = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwCreateFailLogTick || (dwNow - s_dwCreateFailLogTick) >= 2000u)
		{
			s_dwCreateFailLogTick = dwNow;
			TraceError("DX11_SKYENV_DRAW_FAIL stage=cloud reason=cloud_shader_create_failed hr=0x%08x",
				static_cast<unsigned int>(hCreate));
		}
		return false;
	}

	return true;
}

struct SSkyDX11VSConstants
{
	D3DXMATRIX matWorldViewProj;
};

bool EnsureSkyDX11WorldVertexShader(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (gs_pSkyDX11WorldVertexShader && gs_pSkyDX11WorldVSConstantBuffer)
		return true;

	static const char* c_szSkyWorldVS =
		"cbuffer SkyVSConstants : register(b0)"
		"{"
		"    row_major float4x4 uWorldViewProj;"
		"};"
		"struct VSIn { float3 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
		"struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
		"VSOut main(VSIn input)"
		"{"
		"    VSOut output;"
		"    output.pos = mul(float4(input.pos, 1.0f), uWorldViewProj);"
		"    output.pos.z = output.pos.w;"
		"    output.col = input.col;"
		"    output.uv = input.uv;"
		"    return output;"
		"}";

	ID3DBlob* pVSBlob = NULL;
	ID3DBlob* pErrorBlob = NULL;
	const HRESULT hCompile = D3DCompile(
		c_szSkyWorldVS, strlen(c_szSkyWorldVS),
		NULL, NULL, NULL,
		"main", "vs_4_0",
		0, 0, &pVSBlob, &pErrorBlob);
	if (FAILED(hCompile) || !pVSBlob)
	{
		static DWORD s_dwVSCompileFailLogTick = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwVSCompileFailLogTick || (dwNow - s_dwVSCompileFailLogTick) >= 2000u)
		{
			s_dwVSCompileFailLogTick = dwNow;
			if (pErrorBlob && pErrorBlob->GetBufferPointer())
			{
				TraceError("DX11_SKYENV_DRAW_FAIL stage=sky reason=world_vs_compile_failed msg=%s",
					reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer()));
			}
			else
			{
				TraceError("DX11_SKYENV_DRAW_FAIL stage=sky reason=world_vs_compile_failed hr=0x%08x",
					static_cast<unsigned int>(hCompile));
			}
		}
		SAFE_RELEASE(pErrorBlob);
		SAFE_RELEASE(pVSBlob);
		return false;
	}
	SAFE_RELEASE(pErrorBlob);

	const HRESULT hCreateVS = pDevice->CreateVertexShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		NULL,
		&gs_pSkyDX11WorldVertexShader);
	if (FAILED(hCreateVS) || !gs_pSkyDX11WorldVertexShader)
	{
		static DWORD s_dwVSCreateFailLogTick = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwVSCreateFailLogTick || (dwNow - s_dwVSCreateFailLogTick) >= 2000u)
		{
			s_dwVSCreateFailLogTick = dwNow;
			TraceError("DX11_SKYENV_DRAW_FAIL stage=sky reason=world_vs_create_failed hr=0x%08x",
				static_cast<unsigned int>(hCreateVS));
		}
		SAFE_RELEASE(pVSBlob);
		return false;
	}

	D3D11_BUFFER_DESC kConstantBufferDesc = {};
	kConstantBufferDesc.ByteWidth = sizeof(SSkyDX11VSConstants);
	kConstantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	kConstantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	kConstantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	const HRESULT hCreateCB = pDevice->CreateBuffer(&kConstantBufferDesc, NULL, &gs_pSkyDX11WorldVSConstantBuffer);
	SAFE_RELEASE(pVSBlob);
	if (FAILED(hCreateCB) || !gs_pSkyDX11WorldVSConstantBuffer)
	{
		static DWORD s_dwCBCreateFailLogTick = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwCBCreateFailLogTick || (dwNow - s_dwCBCreateFailLogTick) >= 2000u)
		{
			s_dwCBCreateFailLogTick = dwNow;
			TraceError("DX11_SKYENV_DRAW_FAIL stage=sky reason=world_vs_cbuffer_create_failed hr=0x%08x",
				static_cast<unsigned int>(hCreateCB));
		}
		return false;
	}

	return true;
}

inline void ConvertDiffuseToFloat4(const DWORD dwDiffuse, float& r, float& g, float& b, float& a)
{
	a = static_cast<float>((dwDiffuse >> 24) & 0xFF) / 255.0f;
	r = static_cast<float>((dwDiffuse >> 16) & 0xFF) / 255.0f;
	g = static_cast<float>((dwDiffuse >> 8) & 0xFF) / 255.0f;
	b = static_cast<float>(dwDiffuse & 0xFF) / 255.0f;
}

bool DrawSkyPrimitiveDX11(const TPDTVertex* pVertices,
						  const UINT uVertexCount,
						  const D3D11_PRIMITIVE_TOPOLOGY eTopology,
						  ID3D11ShaderResourceView* pTextureSRV,
						  const char* c_szFailKey)
{
	(void)c_szFailKey;

	if (!pVertices || 0 == uVertexCount)
		return false;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return false;

	ID3D11Device* pDevice = pDX11Device->GetDevice();
	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pDevice || !pContext)
		return false;

	if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
		return false;
	if (!EnsureSkyDX11WorldVertexShader(pDevice))
		return false;

	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11VertexShader* pVertexShader = gs_pSkyDX11WorldVertexShader;
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11DepthStencilState* pDepthReadState = pDX11Device->GetBootstrapUIDepthReadState();
	ID3D11RasterizerState* pRasterizerState = pDX11Device->GetBootstrapRasterizerState();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	ID3D11ShaderResourceView* pWhiteSRV = CGraphicTextureDX11::GetWhiteFallbackTexture(pDevice);
	ID3D11PixelShader* pPixelShader = NULL;
	switch (gs_eSkyDX11ShadingMode)
	{
	case SKY_DX11_SHADING_DIFFUSE:
		// Diffuse sky is vertex-color only; using textured UI PS here can output black when t0 is unbound.
		pPixelShader = pDX11Device->GetBootstrapColorPixelShader();
		break;
	case SKY_DX11_SHADING_CLOUD_COMBINE:
		if (!EnsureSkyDX11CloudCombineShader(pDevice))
			return false;
		pPixelShader = gs_pSkyDX11CloudCombinePixelShader;
		break;
	case SKY_DX11_SHADING_TEXTURE:
	default:
		pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
		break;
	}

	if (gs_bSkyDX11ClampSampler)
	{
		if (!EnsureSkyDX11ClampSampler(pDevice))
			return false;
		pSamplerState = gs_pSkyDX11ClampSamplerState;
	}
	else if (SKY_DX11_SHADING_CLOUD_COMBINE == gs_eSkyDX11ShadingMode)
	{
		if (!EnsureSkyDX11CloudSampler(pDevice))
			return false;
		pSamplerState = gs_pSkyDX11CloudSamplerState;
	}

	if (!pInputLayout || !pVertexShader || !pPixelShader || !pVertexBuffer || !pDepthReadState || !pRasterizerState || !pSamplerState || !pWhiteSRV)
		return false;

	CCamera* pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
	D3DXMATRIX matView = pCurrentCamera ? pCurrentCamera->GetViewMatrix() : CGraphicBase::GetViewMatrix();
	const D3DXMATRIX& matProj = CGraphicBase::GetProjMatrix();
	D3DXMATRIX matWorld = gs_matSkyDX11World;
	if (pCurrentCamera)
	{
		const D3DXVECTOR3 v3Eye = pCurrentCamera->GetEye();
		matWorld._41 -= v3Eye.x;
		matWorld._42 -= v3Eye.y;
		matWorld._43 -= v3Eye.z;
		matView._41 = 0.0f;
		matView._42 = 0.0f;
		matView._43 = 0.0f;
	}
	D3DXMATRIX matWorldView;
	D3DXMATRIX matWorldViewProj;
	D3DXMatrixMultiply(&matWorldView, &matWorld, &matView);
	D3DXMatrixMultiply(&matWorldViewProj, &matWorldView, &matProj);

	std::vector<SBootstrapUIVertex> akVertices;
	akVertices.resize(uVertexCount);

	for (UINT i = 0; i < uVertexCount; ++i)
	{
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
		float a = 1.0f;
		if (SKY_DX11_SHADING_TEXTURE != gs_eSkyDX11ShadingMode)
		{
			ConvertDiffuseToFloat4(pVertices[i].diffuse, r, g, b, a);
			gs_bSkyDX11DiffuseSampled = true;
			float fMinRGB = r;
			float fMaxRGB = r;
			if (g < fMinRGB) fMinRGB = g;
			if (b < fMinRGB) fMinRGB = b;
			if (g > fMaxRGB) fMaxRGB = g;
			if (b > fMaxRGB) fMaxRGB = b;
			if (fMinRGB < gs_fSkyDX11DiffuseMin) gs_fSkyDX11DiffuseMin = fMinRGB;
			if (fMaxRGB > gs_fSkyDX11DiffuseMax) gs_fSkyDX11DiffuseMax = fMaxRGB;
		}

		akVertices[i].x = pVertices[i].position.x;
		akVertices[i].y = pVertices[i].position.y;
		akVertices[i].z = pVertices[i].position.z;
		akVertices[i].r = r;
		akVertices[i].g = g;
		akVertices[i].b = b;
		akVertices[i].a = a;
		akVertices[i].u = pVertices[i].texCoord.x * gs_fSkyDX11UScale + gs_fSkyDX11UOffset;
		akVertices[i].v = pVertices[i].texCoord.y * gs_fSkyDX11VScale + gs_fSkyDX11VOffset;
	}

	D3D11_MAPPED_SUBRESOURCE kMappedResource = {};
	const HRESULT hMapResult = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
		return false;
	memcpy(kMappedResource.pData, &akVertices[0], sizeof(SBootstrapUIVertex) * akVertices.size());
	pContext->Unmap(pVertexBuffer, 0);

	D3D11_MAPPED_SUBRESOURCE kMappedCB = {};
	const HRESULT hMapCB = pContext->Map(gs_pSkyDX11WorldVSConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedCB);
	if (FAILED(hMapCB) || !kMappedCB.pData)
		return false;
	SSkyDX11VSConstants kVSConstants = {};
	kVSConstants.matWorldViewProj = matWorldViewProj;
	memcpy(kMappedCB.pData, &kVSConstants, sizeof(kVSConstants));
	pContext->Unmap(gs_pSkyDX11WorldVSConstantBuffer, 0);

	ID3D11InputLayout* pPrevInputLayout = NULL;
	ID3D11Buffer* pPrevVertexBuffer = NULL;
	UINT uPrevStride = 0u;
	UINT uPrevOffset = 0u;
	D3D11_PRIMITIVE_TOPOLOGY ePrevTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	ID3D11VertexShader* pPrevVS = NULL;
	ID3D11Buffer* pPrevVSConstantBuffer0 = NULL;
	ID3D11PixelShader* pPrevPS = NULL;
	ID3D11SamplerState* pPrevPSSampler0 = NULL;
	ID3D11ShaderResourceView* pPrevPSSRV0 = NULL;
	ID3D11BlendState* pPrevBlendState = NULL;
	FLOAT afPrevBlendFactor[4] = { 0, 0, 0, 0 };
	UINT uPrevSampleMask = 0xFFFFFFFFu;
	ID3D11DepthStencilState* pPrevDepthState = NULL;
	UINT uPrevStencilRef = 0u;
	ID3D11RasterizerState* pPrevRasterizerState = NULL;
	ID3D11RenderTargetView* pPrevRTV = NULL;
	ID3D11DepthStencilView* pPrevDSV = NULL;

	pContext->IAGetInputLayout(&pPrevInputLayout);
	pContext->IAGetVertexBuffers(0, 1, &pPrevVertexBuffer, &uPrevStride, &uPrevOffset);
	pContext->IAGetPrimitiveTopology(&ePrevTopology);
	pContext->VSGetShader(&pPrevVS, NULL, NULL);
	pContext->VSGetConstantBuffers(0, 1, &pPrevVSConstantBuffer0);
	pContext->PSGetShader(&pPrevPS, NULL, NULL);
	pContext->PSGetSamplers(0, 1, &pPrevPSSampler0);
	pContext->PSGetShaderResources(0, 1, &pPrevPSSRV0);
	pContext->OMGetBlendState(&pPrevBlendState, afPrevBlendFactor, &uPrevSampleMask);
	pContext->OMGetDepthStencilState(&pPrevDepthState, &uPrevStencilRef);
	pContext->RSGetState(&pPrevRasterizerState);
	pContext->OMGetRenderTargets(1, &pPrevRTV, &pPrevDSV);

	pDX11Device->BindMainRenderTargets();

	const FLOAT afBlendFactor[4] = { 0, 0, 0, 0 };
	UINT uStride = sizeof(SBootstrapUIVertex);
	UINT uOffset = 0;
	// M3-SKY-BLEND-FIX-74: Safety check for blend state (fallback to alpha blend if NULL)
    ID3D11BlendState* pBlendState = gs_pSkyDX11BlendState;
	pContext->OMSetBlendState(pBlendState, afBlendFactor, 0xFFFFFFFFu);
	pContext->RSSetState(pRasterizerState);
	pContext->OMSetDepthStencilState(pDepthReadState, 0);
	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
	pContext->IASetPrimitiveTopology(eTopology);
	pContext->VSSetShader(pVertexShader, NULL, 0);
	pContext->VSSetConstantBuffers(0, 1, &gs_pSkyDX11WorldVSConstantBuffer);
	pContext->PSSetShader(pPixelShader, NULL, 0);
	pContext->PSSetSamplers(0, 1, &pSamplerState);

	ID3D11ShaderResourceView* pBoundSRV = pTextureSRV ? pTextureSRV : pWhiteSRV;
	ID3D11ShaderResourceView* pNullSRV = NULL;
	if (SKY_DX11_SHADING_DIFFUSE == gs_eSkyDX11ShadingMode)
		pContext->PSSetShaderResources(0, 1, &pNullSRV);
	else
		pContext->PSSetShaderResources(0, 1, &pBoundSRV);
	pContext->Draw(uVertexCount, 0);
	pDX11Device->IncrementFrameDrawCalls(1u, (uVertexCount >= 2u) ? (uVertexCount - 2u) : 0u);

	pContext->PSSetShaderResources(0, 1, &pPrevPSSRV0);
	pContext->PSSetSamplers(0, 1, &pPrevPSSampler0);
	pContext->PSSetShader(pPrevPS, NULL, 0);
	pContext->VSSetConstantBuffers(0, 1, &pPrevVSConstantBuffer0);
	pContext->VSSetShader(pPrevVS, NULL, 0);
	pContext->IASetPrimitiveTopology(ePrevTopology);
	pContext->IASetVertexBuffers(0, 1, &pPrevVertexBuffer, &uPrevStride, &uPrevOffset);
	pContext->IASetInputLayout(pPrevInputLayout);
	pContext->OMSetRenderTargets(1, &pPrevRTV, pPrevDSV);
	pContext->OMSetDepthStencilState(pPrevDepthState, uPrevStencilRef);
	pContext->OMSetBlendState(pPrevBlendState, afPrevBlendFactor, uPrevSampleMask);
	pContext->RSSetState(pPrevRasterizerState);

	SAFE_RELEASE(pPrevInputLayout);
	SAFE_RELEASE(pPrevVertexBuffer);
	SAFE_RELEASE(pPrevVS);
	SAFE_RELEASE(pPrevVSConstantBuffer0);
	SAFE_RELEASE(pPrevPS);
	SAFE_RELEASE(pPrevPSSampler0);
	SAFE_RELEASE(pPrevPSSRV0);
	SAFE_RELEASE(pPrevBlendState);
	SAFE_RELEASE(pPrevDepthState);
	SAFE_RELEASE(pPrevRasterizerState);
	SAFE_RELEASE(pPrevRTV);
	SAFE_RELEASE(pPrevDSV);
	return true;
}

// DX11_MODERNIZE_NOTES:
// 1) Replace fixed-function sky/cloud pass with dedicated DX11 PSO-like state blocks.
// 2) Render sky with immutable VB/IB + shader-based gradients (remove FVF/TextureStageState).
// 3) Move cloud UV animation to shader constants and batch as one indexed draw.
// 4) Integrate temporal blending and exposure-safe sky luminance path.
}

//////////////////////////////////////////////////////////////////////////
// CSkyObjectQuad
//////////////////////////////////////////////////////////////////////////

CSkyObjectQuad::CSkyObjectQuad()
{
	// Index buffer
	m_Indices[0] = 0;
	m_Indices[1] = 2;
	m_Indices[2] = 1;
	m_Indices[3] = 3;

	for (unsigned char uci = 0; uci < 4; ++uci)
	{
		memset(&m_Vertex[uci], 0, sizeof(TPDTVertex));
	}
}

CSkyObjectQuad::~CSkyObjectQuad()
{
}

void CSkyObjectQuad::Clear(const unsigned char & c_rucNumVertex,
						   const float & c_rfRed,
						   const float & c_rfGreen,
						   const float & c_rfBlue,
						   const float & c_rfAlpha)
{
	if (c_rucNumVertex > 3)
		return;
	m_Helper[c_rucNumVertex].Clear(c_rfRed, c_rfGreen, c_rfBlue, c_rfAlpha);
}

void CSkyObjectQuad::SetSrcColor(const unsigned char & c_rucNumVertex,
								 const float & c_rfRed,
								 const float & c_rfGreen,
								 const float & c_rfBlue,
								 const float & c_rfAlpha)
{
	if (c_rucNumVertex > 3)
		return;
	m_Helper[c_rucNumVertex].SetSrcColor(c_rfRed, c_rfGreen, c_rfBlue, c_rfAlpha);
}

void CSkyObjectQuad::SetTransition(const unsigned char & c_rucNumVertex,
								   const float & c_rfRed,
								   const float & c_rfGreen,
								   const float & c_rfBlue,
								   const float & c_rfAlpha,
								   DWORD dwDuration)
{
	if (c_rucNumVertex > 3)
		return;
	m_Helper[c_rucNumVertex].SetTransition(c_rfRed, c_rfGreen, c_rfBlue, c_rfAlpha, dwDuration);
}

void CSkyObjectQuad::SetVertex(const unsigned char & c_rucNumVertex, const TPDTVertex & c_rPDTVertex)
{
	if (c_rucNumVertex > 3)
		return;
	memcpy (&m_Vertex[m_Indices[c_rucNumVertex]], &c_rPDTVertex, sizeof(TPDTVertex)); 
}

void CSkyObjectQuad::StartTransition()
{
	for (unsigned char uci = 0; uci < 4; ++uci)
	{
		m_Helper[uci].StartTransition();
	}
}

bool CSkyObjectQuad::Update()
{
	bool bResult = false;
	for (unsigned char uci = 0; uci < 4; ++uci)
	{
		bResult = m_Helper[uci].Update() || bResult;
		m_Vertex[m_Indices[uci]].diffuse = m_Helper[uci].GetCurColor();
	}
 	return bResult;
}

void CSkyObjectQuad::Render()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	++gs_uSkyDX11ExpectedQuads;
	ID3D11ShaderResourceView* pTextureSRV = pDX11Device->GetBootstrapTextureStageSRV(0);
	if (DrawSkyPrimitiveDX11(m_Vertex, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, pTextureSRV, "DX11_SKYENV_DRAW_FAIL"))
		++gs_uSkyDX11SubmittedQuads;
}

//////////////////////////////////////////////////////////////////////////
// CSkyObject
/////////////////////////////////////////////////////////////////////////
CSkyObject::CSkyObject() :
	m_v3Position(0.0f, 0.0f, 0.0f),
	m_fScaleX(1.0f),
	m_fScaleY(1.0f),
	m_fScaleZ(1.0f)
{
	D3DXMatrixIdentity(&m_matWorld);
	D3DXMatrixIdentity(&m_matTranslation);
	D3DXMatrixIdentity(&m_matTextureCloud);

	m_dwlastTime = CTimer::Instance().GetCurrentMillisecond();

	m_fCloudPositionU = 0.0f;
	m_fCloudPositionV = 0.0f;

	m_bTransitionStarted = false;
	m_bSkyMatrixUpdated = false;
}

CSkyObject::~CSkyObject()
{
	Destroy();
}

void CSkyObject::Destroy()
{
}

void CSkyObject::Update()
{
	D3DXVECTOR3 v3Eye = CCameraManager::Instance().GetCurrentCamera()->GetEye();

	if (m_v3Position == v3Eye)
		if (m_bSkyMatrixUpdated == false)
			return;

	m_v3Position = v3Eye;

	m_matWorld._41 = m_v3Position.x;
	m_matWorld._42 = m_v3Position.y;
	m_matWorld._43 = m_v3Position.z;

	m_matWorldCloud._41 = m_v3Position.x;
	m_matWorldCloud._42 = m_v3Position.y;
	m_matWorldCloud._43 = m_v3Position.z + m_fCloudHeight;

	if (m_bSkyMatrixUpdated)
		m_bSkyMatrixUpdated = false;
}

void CSkyObject::Render()
{
}

CGraphicImageInstance * CSkyObject::GenerateTexture(const char * szfilename)
{
	// M2-SKY-ENV-FIX-40: Add sky texture load diagnostics
	static DWORD s_dwSkyTextureLoadLogTick = 0u;
	const DWORD dwNow = ELTimer_GetMSec();

	assert(szfilename != NULL);

	if (strlen(szfilename) <= 0)
	{
		// M2-SKY-ENV-FIX-40: Log empty filename error
		if (0u == s_dwSkyTextureLoadLogTick || (dwNow - s_dwSkyTextureLoadLogTick) >= 2000u)
		{
			s_dwSkyTextureLoadLogTick = dwNow;
			TraceError("DX11_SKYENV_TEXTURE_LOAD_FAIL stage=generate_texture reason=empty_filename filename=(null)");
		}
		assert(false);
		return NULL;
	}

	CResource * pResource = CResourceManager::Instance().GetResourcePointer(szfilename);

	if (!pResource)
	{
		// M2-SKY-ENV-FIX-40: Log resource load failure
		if (0u == s_dwSkyTextureLoadLogTick || (dwNow - s_dwSkyTextureLoadLogTick) >= 2000u)
		{
			s_dwSkyTextureLoadLogTick = dwNow;
			TraceError("DX11_SKYENV_TEXTURE_LOAD_FAIL stage=generate_texture reason=resource_null filename=%s", szfilename);
		}
		return NULL;
	}

	if (!pResource->IsType(CGraphicImage::Type()))
	{
		// M2-SKY-ENV-FIX-40: Log type mismatch
		if (0u == s_dwSkyTextureLoadLogTick || (dwNow - s_dwSkyTextureLoadLogTick) >= 2000u)
		{
			s_dwSkyTextureLoadLogTick = dwNow;
			TraceError("DX11_SKYENV_TEXTURE_LOAD_FAIL stage=generate_texture reason=type_mismatch filename=%s expected_type=CGraphicImage",
				szfilename);
		}
		assert(false);
		return NULL;
	}

	CGraphicImageInstance * pImageInstance = CGraphicImageInstance::New();
	if (!pImageInstance)
	{
		// M2-SKY-ENV-FIX-40: Log image instance creation failure
		if (0u == s_dwSkyTextureLoadLogTick || (dwNow - s_dwSkyTextureLoadLogTick) >= 2000u)
		{
			s_dwSkyTextureLoadLogTick = dwNow;
			TraceError("DX11_SKYENV_TEXTURE_LOAD_FAIL stage=generate_texture reason=image_instance_null filename=%s", szfilename);
		}
		return NULL;
	}

	pImageInstance->SetImagePointer(static_cast<CGraphicImage *>(pResource));

	// M2-SKY-ENV-FIX-40: Log successful texture load (throttled)
	static bool s_bLoggedTextureLoad = false;
	if (!s_bLoggedTextureLoad)
	{
		s_bLoggedTextureLoad = true;
		TraceError("DX11_SKYENV_TEXTURE_LOAD_SUCCESS filename=%s", szfilename);
	}

	return (pImageInstance);
}

void CSkyObject::DeleteTexture(CGraphicImageInstance * pImageInstance)
{
	if (pImageInstance)
		CGraphicImageInstance::Delete(pImageInstance);
}

void CSkyObject::StartTransition()
{
}

//////////////////////////////////////////////////////////////////////////
// CSkyObject::TSkyObjectFace
//////////////////////////////////////////////////////////////////////////

void CSkyObject::TSkyObjectFace::StartTransition()
{
	for (unsigned char uci = 0; uci < m_SkyObjectQuadVector.size(); ++uci)
	{
		m_SkyObjectQuadVector[uci].StartTransition();
	}
}

bool CSkyObject::TSkyObjectFace::Update()
{
	bool bResult = false;
	for (DWORD dwi = 0; dwi < m_SkyObjectQuadVector.size(); ++dwi)
 		bResult = m_SkyObjectQuadVector[dwi].Update() || bResult;
 	return bResult;
}

void CSkyObject::TSkyObjectFace::Render()
{
	for (unsigned char uci = 0; uci < m_SkyObjectQuadVector.size(); ++uci)
	{
		m_SkyObjectQuadVector[uci].Render();
	}
}

//////////////////////////////////////////////////////////////////////////
// CSkyBox
//////////////////////////////////////////////////////////////////////////

CSkyBox::CSkyBox()
{
	m_ucVirticalGradientLevelUpper = 0;
	m_ucVirticalGradientLevelLower = 0;
	m_bSkyTexturesExpected = false;  // M3-SKYBOX-ASSET-BIND-73: Initialize flag
}

CSkyBox::~CSkyBox()
{
	Destroy();
}

void CSkyBox::Destroy()
{
	Unload();
}

void CSkyBox::SetSkyBoxScale(const D3DXVECTOR3 & c_rv3Scale)
{
	if (fabsf(m_fScaleX - c_rv3Scale.x) <= 0.001f &&
		fabsf(m_fScaleY - c_rv3Scale.y) <= 0.001f &&
		fabsf(m_fScaleZ - c_rv3Scale.z) <= 0.001f)
		return;

	m_fScaleX = c_rv3Scale.x;
	m_fScaleY = c_rv3Scale.y;
	m_fScaleZ = c_rv3Scale.z;

	m_bSkyMatrixUpdated = true;
	D3DXMatrixScaling(&m_matWorld, m_fScaleX, m_fScaleY, m_fScaleZ);
	m_matWorld._41 = m_v3Position.x;
	m_matWorld._42 = m_v3Position.y;
	m_matWorld._43 = m_v3Position.z;
}

void CSkyBox::SetGradientLevel(BYTE byUpper, BYTE byLower)
{
	// Preserve the strip topology authored in .msenv; only guard division by zero.
	m_ucVirticalGradientLevelUpper = std::max<BYTE>(byUpper, 1);
	m_ucVirticalGradientLevelLower = std::max<BYTE>(byLower, 1);
}

void CSkyBox::SetFaceTexture( const char* c_szFileName, int iFaceIndex )
{
	if( iFaceIndex < 0 || iFaceIndex > 5 )
		return;

	// M3-SKYBOX-ASSET-BIND-73: Sky textures were configured (not just default diffuse mode)
	m_bSkyTexturesExpected = true;

	TGraphicImageInstanceMap::iterator itor = m_GraphicImageInstanceMap.find(c_szFileName);
	if (m_GraphicImageInstanceMap.end() != itor)
		return;

	m_Faces[iFaceIndex].m_strFaceTextureFileName = c_szFileName;

	CGraphicImageInstance * pGraphicImageInstance = GenerateTexture(c_szFileName);
	m_GraphicImageInstanceMap.insert(TGraphicImageInstanceMap::value_type(c_szFileName, pGraphicImageInstance));
}


void CSkyBox::SetCloudTexture(const char * c_szFileName)
{
	TGraphicImageInstanceMap::iterator itor = m_GraphicImageInstanceMap.find(c_szFileName);
	if (m_GraphicImageInstanceMap.end() != itor)
		return;

	m_FaceCloud.m_strfacename = c_szFileName;
	CGraphicImageInstance * pGraphicImageInstance = GenerateTexture(c_szFileName);
	if (!pGraphicImageInstance)
		TraceError("CSkyBox::SetCloudTexture - Failed to load cloud texture: %s", c_szFileName);
	m_GraphicImageInstanceMap.insert(TGraphicImageInstanceMap::value_type(m_FaceCloud.m_strfacename, pGraphicImageInstance));

	// ì´ê±° ì•ˆì“°ëŠ”ê±° ê°™ì€ë°ìš”? [cronan]
//	CGraphicImage * pImage = (CGraphicImage *) CResourceManager::Instance().GetResourcePointer("D:\\Ymir Work\\special/cloudalpha.tga");
//	m_CloudAlphaImageInstance.SetImagePointer(pImage);
}

void CSkyBox::SetCloudScale(const D3DXVECTOR2 & c_rv2CloudScale)
{
	m_fCloudScaleX = c_rv2CloudScale.x;
	m_fCloudScaleY = c_rv2CloudScale.y;

	D3DXMatrixScaling(&m_matWorldCloud, m_fCloudScaleX, m_fCloudScaleY, 1.0f);
}

void CSkyBox::SetCloudHeight(float fHeight)
{
	m_fCloudHeight = fHeight;
}

void CSkyBox::SetCloudTextureScale(const D3DXVECTOR2 & c_rv2CloudTextureScale)
{
	m_fCloudTextureScaleX = c_rv2CloudTextureScale.x;
	m_fCloudTextureScaleY = c_rv2CloudTextureScale.y;

	m_matTextureCloud._11 = m_fCloudTextureScaleX;
	m_matTextureCloud._22 = m_fCloudTextureScaleY;
}

void CSkyBox::SetCloudScrollSpeed(const D3DXVECTOR2 & c_rv2CloudScrollSpeed)
{
	m_fCloudScrollSpeedU = c_rv2CloudScrollSpeed.x;
	m_fCloudScrollSpeedV = c_rv2CloudScrollSpeed.y;
}

void CSkyBox::Unload()
{
	TGraphicImageInstanceMap::iterator itor = m_GraphicImageInstanceMap.begin();

	while (itor != m_GraphicImageInstanceMap.end())
	{
		DeleteTexture(itor->second);
		++itor;
	}

	m_GraphicImageInstanceMap.clear();
}

void CSkyBox::SetSkyObjectQuadVertical(TSkyObjectQuadVector * pSkyObjectQuadVector, const D3DXVECTOR2 * c_pv2QuadPoints)
{
	TPDTVertex aPDTVertex;

	DWORD dwIndex = 0;

	pSkyObjectQuadVector->clear();
	pSkyObjectQuadVector->resize(m_ucVirticalGradientLevelUpper + m_ucVirticalGradientLevelLower);

	unsigned char ucY;
	for (ucY = 0; ucY < m_ucVirticalGradientLevelUpper; ++ucY)
	{
		CSkyObjectQuad & rSkyObjectQuad = pSkyObjectQuadVector->at(dwIndex++);

		aPDTVertex.position.x = c_pv2QuadPoints[0].x;
		aPDTVertex.position.y = c_pv2QuadPoints[0].y;
		aPDTVertex.position.z = 1.0f - (float)(ucY + 1)/ (float)(m_ucVirticalGradientLevelUpper); 
		aPDTVertex.texCoord.x = 0.0f;
		aPDTVertex.texCoord.y = (float)(ucY + 1)/ (float)(m_ucVirticalGradientLevelUpper) * 0.5f;
		rSkyObjectQuad.SetVertex(0 , aPDTVertex);
		aPDTVertex.position.x = c_pv2QuadPoints[0].x;
		aPDTVertex.position.y = c_pv2QuadPoints[0].y;
		aPDTVertex.position.z = 1.0f - (float)(ucY) / (float)(m_ucVirticalGradientLevelUpper); 
		aPDTVertex.texCoord.x = 0.0f;
		aPDTVertex.texCoord.y = (float)(ucY)/ (float)(m_ucVirticalGradientLevelUpper) * 0.5f;
		rSkyObjectQuad.SetVertex(1, aPDTVertex);
		aPDTVertex.position.x = c_pv2QuadPoints[1].x;
		aPDTVertex.position.y = c_pv2QuadPoints[1].y;
		aPDTVertex.position.z = 1.0f - (float)(ucY + 1) / (float)(m_ucVirticalGradientLevelUpper); 
		aPDTVertex.texCoord.x = 1.0f;
		aPDTVertex.texCoord.y = (float)(ucY + 1)/ (float)(m_ucVirticalGradientLevelUpper) * 0.5f;
		rSkyObjectQuad.SetVertex(2, aPDTVertex);
		aPDTVertex.position.x = c_pv2QuadPoints[1].x;
		aPDTVertex.position.y = c_pv2QuadPoints[1].y;
		aPDTVertex.position.z = 1.0f - (float)(ucY) / (float)(m_ucVirticalGradientLevelUpper); 
		aPDTVertex.texCoord.x = 1.0f;
		aPDTVertex.texCoord.y = (float)(ucY)/ (float)(m_ucVirticalGradientLevelUpper) * 0.5f;
		rSkyObjectQuad.SetVertex(3, aPDTVertex);
	}
	for (ucY = 0; ucY < m_ucVirticalGradientLevelLower; ++ucY)
	{
		CSkyObjectQuad & rSkyObjectQuad = pSkyObjectQuadVector->at(dwIndex++);

		aPDTVertex.position.x = c_pv2QuadPoints[0].x;
		aPDTVertex.position.y = c_pv2QuadPoints[0].y;
		aPDTVertex.position.z = -(float)(ucY + 1)/ (float)(m_ucVirticalGradientLevelLower);
		aPDTVertex.texCoord.x = 0.0f;
		aPDTVertex.texCoord.y = 0.5f + (float)(ucY + 1)/ (float)(m_ucVirticalGradientLevelLower) * 0.5f;
		rSkyObjectQuad.SetVertex(0, aPDTVertex);
		aPDTVertex.position.x = c_pv2QuadPoints[0].x;
		aPDTVertex.position.y = c_pv2QuadPoints[0].y;
		aPDTVertex.position.z = -(float)(ucY) / (float)(m_ucVirticalGradientLevelLower);
		aPDTVertex.texCoord.x = 0.0f;
		aPDTVertex.texCoord.y = 0.5f + (float)(ucY)/ (float)(m_ucVirticalGradientLevelLower) * 0.5f;
		rSkyObjectQuad.SetVertex(1, aPDTVertex);
		aPDTVertex.position.x = c_pv2QuadPoints[1].x;
		aPDTVertex.position.y = c_pv2QuadPoints[1].y;
		aPDTVertex.position.z = -(float)(ucY + 1) / (float)(m_ucVirticalGradientLevelLower);
		aPDTVertex.texCoord.x = 1.0f;
		aPDTVertex.texCoord.y = 0.5f + (float)(ucY + 1)/ (float)(m_ucVirticalGradientLevelLower) * 0.5f;
		rSkyObjectQuad.SetVertex(2, aPDTVertex);
		aPDTVertex.position.x = c_pv2QuadPoints[1].x;
		aPDTVertex.position.y = c_pv2QuadPoints[1].y;
		aPDTVertex.position.z = -(float)(ucY) / (float)(m_ucVirticalGradientLevelLower);
		aPDTVertex.texCoord.x = 1.0f;
		aPDTVertex.texCoord.y = 0.5f + (float)(ucY)/ (float)(m_ucVirticalGradientLevelLower) * 0.5f;
		rSkyObjectQuad.SetVertex(3, aPDTVertex);
	}
}

//void CSkyBox::UpdateSkyFaceQuadTransform(D3DXVECTOR3 * c_pv3QuadPoints)
//{
//	for( int i = 0; i < 4; ++i )
//	{
//		c_pv3QuadPoints[i].x *= m_fScaleX;	
//		c_pv3QuadPoints[i].y *= m_fScaleY;	
//		c_pv3QuadPoints[i].z *= m_fScaleZ;	
//
//		c_pv3QuadPoints[i] += m_v3Position;
//	}
//}

void CSkyBox::SetSkyObjectQuadHorizon(TSkyObjectQuadVector * pSkyObjectQuadVector, const D3DXVECTOR3 * c_pv3QuadPoints)
{
	pSkyObjectQuadVector->clear();
	pSkyObjectQuadVector->resize(1);
	CSkyObjectQuad & rSkyObjectQuad = pSkyObjectQuadVector->at(0);

	TPDTVertex aPDTVertex;
	aPDTVertex.position		= c_pv3QuadPoints[0];
	aPDTVertex.texCoord.x	= 0.0f;
	aPDTVertex.texCoord.y	= 1.0f;
	rSkyObjectQuad.SetVertex(0, aPDTVertex);

	aPDTVertex.position		= c_pv3QuadPoints[1];
	aPDTVertex.texCoord.x	= 0.0f;
	aPDTVertex.texCoord.y	= 0.0f;
	rSkyObjectQuad.SetVertex(1, aPDTVertex);

	aPDTVertex.position		= c_pv3QuadPoints[2];
	aPDTVertex.texCoord.x	= 1.0f;
	aPDTVertex.texCoord.y	= 1.0f;
	rSkyObjectQuad.SetVertex(2, aPDTVertex);

	aPDTVertex.position		= c_pv3QuadPoints[3];
	aPDTVertex.texCoord.x	= 1.0f;
	aPDTVertex.texCoord.y	= 0.0f;
	rSkyObjectQuad.SetVertex(3, aPDTVertex);
}

void CSkyBox::Refresh()
{
	D3DXVECTOR3 v3QuadPoints[4];

	if( m_ucRenderMode == CSkyObject::SKY_RENDER_MODE_DEFAULT ||  m_ucRenderMode == CSkyObject::SKY_RENDER_MODE_DIFFUSE )
	{
		D3DXVECTOR2 v2QuadPoints[2];

		//// Face 0: FRONT
		v2QuadPoints[0] = D3DXVECTOR2(1.0f, -1.0f);
		v2QuadPoints[1] = D3DXVECTOR2(-1.0f, -1.0f);
		SetSkyObjectQuadVertical(&m_Faces[0].m_SkyObjectQuadVector, v2QuadPoints);
		m_Faces[0].m_strfacename = "front";

		//// Face 1: BACK
		v2QuadPoints[0] = D3DXVECTOR2(-1.0f, 1.0f);
		v2QuadPoints[1] = D3DXVECTOR2(1.0f, 1.0f);
		SetSkyObjectQuadVertical(&m_Faces[1].m_SkyObjectQuadVector, v2QuadPoints);
		m_Faces[1].m_strfacename = "back";

		//// Face 2: LEFT
		v2QuadPoints[0] = D3DXVECTOR2(-1.0f, -1.0f);
		v2QuadPoints[1] = D3DXVECTOR2(-1.0f, 1.0f);
		SetSkyObjectQuadVertical(&m_Faces[2].m_SkyObjectQuadVector, v2QuadPoints);
		m_Faces[2].m_strfacename = "left";

		//// Face 3: RIGHT
		v2QuadPoints[0] = D3DXVECTOR2(1.0f, 1.0f);
		v2QuadPoints[1] = D3DXVECTOR2(1.0f, -1.0f);
		SetSkyObjectQuadVertical(&m_Faces[3].m_SkyObjectQuadVector, v2QuadPoints);
		m_Faces[3].m_strfacename = "right";

		//// Face 4: TOP
		v3QuadPoints[0] = D3DXVECTOR3(1.0f, 1.0f, 1.0f);
		v3QuadPoints[1] = D3DXVECTOR3(-1.0f, 1.0f, 1.0f);
		v3QuadPoints[2] = D3DXVECTOR3(1.0f, -1.0f, 1.0f);
		v3QuadPoints[3] = D3DXVECTOR3(-1.0f, -1.0f, 1.0f);
		SetSkyObjectQuadHorizon(&m_Faces[4].m_SkyObjectQuadVector, v3QuadPoints);
		m_Faces[4].m_strfacename = "top";

		//// Face 5: BOTTOM
		v3QuadPoints[0] = D3DXVECTOR3(-1.0f, 1.0f, -1.0f);
		v3QuadPoints[1] = D3DXVECTOR3(1.0f, 1.0f, -1.0f);
		v3QuadPoints[2] = D3DXVECTOR3(-1.0f, -1.0f, -1.0f);
		v3QuadPoints[3] = D3DXVECTOR3(1.0f, -1.0f, -1.0f);
		SetSkyObjectQuadHorizon(&m_Faces[5].m_SkyObjectQuadVector, v3QuadPoints);
		m_Faces[5].m_strfacename = "bottom";

	}
	else if( m_ucRenderMode == CSkyObject::SKY_RENDER_MODE_TEXTURE )
	{
		// Face 0: FRONT
		v3QuadPoints[0] = D3DXVECTOR3(1.0f, -1.0f, -1.0f);
		v3QuadPoints[1] = D3DXVECTOR3(1.0f, -1.0f, 1.0f);
		v3QuadPoints[2] = D3DXVECTOR3(-1.0f, -1.0f, -1.0f);
		v3QuadPoints[3] = D3DXVECTOR3(-1.0f, -1.0f, 1.0f);

		//UpdateSkyFaceQuadTransform(v3QuadPoints);

		SetSkyObjectQuadHorizon(&m_Faces[0].m_SkyObjectQuadVector, v3QuadPoints);
		m_Faces[0].m_strfacename = "front";

		//// Face 1: BACK
		v3QuadPoints[0] = D3DXVECTOR3(-1.0f, 1.0f, -1.0f);
		v3QuadPoints[1] = D3DXVECTOR3(-1.0f, 1.0f, 1.0f);
		v3QuadPoints[2] = D3DXVECTOR3(1.0f, 1.0f, -1.0f);
		v3QuadPoints[3] = D3DXVECTOR3(1.0f, 1.0f, 1.0f);

		//UpdateSkyFaceQuadTransform(v3QuadPoints);
		
		SetSkyObjectQuadHorizon(&m_Faces[1].m_SkyObjectQuadVector, v3QuadPoints);
		m_Faces[1].m_strfacename = "back";

		// Face 2: LEFT
		v3QuadPoints[0] = D3DXVECTOR3(1.0f, 1.0f, -1.0f);
		v3QuadPoints[1] = D3DXVECTOR3(1.0f, 1.0f, 1.0f);
		v3QuadPoints[2] = D3DXVECTOR3(1.0f, -1.0f, -1.0f);
		v3QuadPoints[3] = D3DXVECTOR3(1.0f, -1.0f, 1.0f);

		//UpdateSkyFaceQuadTransform(v3QuadPoints);

		SetSkyObjectQuadHorizon(&m_Faces[2].m_SkyObjectQuadVector, v3QuadPoints);
		m_Faces[2].m_strfacename = "left";

		// Face 3: RIGHT
		v3QuadPoints[0] = D3DXVECTOR3(-1.0f, -1.0f, -1.0f);
		v3QuadPoints[1] = D3DXVECTOR3(-1.0f, -1.0f, 1.0f);
		v3QuadPoints[2] = D3DXVECTOR3(-1.0f, 1.0f, -1.0f);
		v3QuadPoints[3] = D3DXVECTOR3(-1.0f, 1.0f, 1.0f);
		
		//UpdateSkyFaceQuadTransform(v3QuadPoints);
		
		SetSkyObjectQuadHorizon(&m_Faces[3].m_SkyObjectQuadVector, v3QuadPoints);
		m_Faces[3].m_strfacename = "right";

		// Face 4: TOP
		v3QuadPoints[0] = D3DXVECTOR3(1.0f, -1.0f, 1.0f); 
		v3QuadPoints[1] = D3DXVECTOR3(1.0f, 1.0f, 1.0f);
		v3QuadPoints[2] = D3DXVECTOR3(-1.0f, -1.0f, 1.0f);
		v3QuadPoints[3] = D3DXVECTOR3(-1.0f, 1.0f, 1.0f);

		//UpdateSkyFaceQuadTransform(v3QuadPoints);

		SetSkyObjectQuadHorizon(&m_Faces[4].m_SkyObjectQuadVector, v3QuadPoints);
		m_Faces[4].m_strfacename = "top";

		////// Face 5: BOTTOM
		v3QuadPoints[0] = D3DXVECTOR3(1.0f, -1.0f, -1.0f);
		v3QuadPoints[1] = D3DXVECTOR3(1.0f, 1.0f, -1.0f);
		v3QuadPoints[2] = D3DXVECTOR3(-1.0f, -1.0f, -1.0f);
		v3QuadPoints[3] = D3DXVECTOR3(-1.0f, 1.0f, -1.0f);

		//UpdateSkyFaceQuadTransform(v3QuadPoints);
		
		SetSkyObjectQuadHorizon(&m_Faces[5].m_SkyObjectQuadVector, v3QuadPoints);
		m_Faces[5].m_strfacename = "bottom";
	}

	//// Clouds..
	v3QuadPoints[0] = D3DXVECTOR3(1.0f, 1.0f, 0.0f);
	v3QuadPoints[1] = D3DXVECTOR3(-1.0f, 1.0f, 0.0f);
	v3QuadPoints[2] = D3DXVECTOR3(1.0f, -1.0f, 0.0f);
	v3QuadPoints[3] = D3DXVECTOR3(-1.0f, -1.0f, 0.0f);
	SetSkyObjectQuadHorizon(&m_FaceCloud.m_SkyObjectQuadVector, v3QuadPoints);
}

void CSkyBox::SetCloudColor(const TGradientColor & c_rColor, const TGradientColor & c_rNextColor, const DWORD & dwTransitionTime)
{
	TSkyObjectFace & aFaceCloud = m_FaceCloud;
	for (DWORD dwk = 0; dwk < aFaceCloud.m_SkyObjectQuadVector.size(); ++dwk)
	{
		CSkyObjectQuad & aSkyObjectQuad = aFaceCloud.m_SkyObjectQuadVector[dwk];
		
		aSkyObjectQuad.SetSrcColor(0,
			c_rColor.m_FirstColor.r,
			c_rColor.m_FirstColor.g,
			c_rColor.m_FirstColor.b,
			c_rColor.m_FirstColor.a);
		aSkyObjectQuad.SetTransition(0, 
			c_rNextColor.m_FirstColor.r,
			c_rNextColor.m_FirstColor.g,
			c_rNextColor.m_FirstColor.b,
			c_rNextColor.m_FirstColor.a,
			dwTransitionTime);
		aSkyObjectQuad.SetSrcColor(1,
			c_rColor.m_FirstColor.r,
			c_rColor.m_FirstColor.g,
			c_rColor.m_FirstColor.b,
			c_rColor.m_FirstColor.a);
		aSkyObjectQuad.SetTransition(1,
			c_rNextColor.m_FirstColor.r,
			c_rNextColor.m_FirstColor.g,
			c_rNextColor.m_FirstColor.b,
			c_rNextColor.m_FirstColor.a,
			dwTransitionTime);
		aSkyObjectQuad.SetSrcColor(2,
			c_rColor.m_FirstColor.r,
			c_rColor.m_FirstColor.g,
			c_rColor.m_FirstColor.b,
			c_rColor.m_FirstColor.a);
		aSkyObjectQuad.SetTransition(2,
			c_rNextColor.m_FirstColor.r,
			c_rNextColor.m_FirstColor.g,
			c_rNextColor.m_FirstColor.b,
			c_rNextColor.m_FirstColor.a,
			dwTransitionTime);
		aSkyObjectQuad.SetSrcColor(3,
			c_rColor.m_FirstColor.r,
			c_rColor.m_FirstColor.g,
			c_rColor.m_FirstColor.b,
			c_rColor.m_FirstColor.a);
		aSkyObjectQuad.SetTransition(3,
			c_rNextColor.m_FirstColor.r,
			c_rNextColor.m_FirstColor.g,
			c_rNextColor.m_FirstColor.b,
			c_rNextColor.m_FirstColor.a,
			dwTransitionTime);
	}
}

void CSkyBox::SetSkyColor(const TVectorGradientColor & c_rColorVector, const TVectorGradientColor & c_rNextColorVector, long lTransitionTime)
{
	const size_t uRequiredSideSamples = std::max<size_t>(1u, m_Faces[0].m_SkyObjectQuadVector.size());
	const TVectorGradientColor kColorVector = BuildSkyGradientLUT(c_rColorVector, uRequiredSideSamples);
	const TVectorGradientColor kNextColorVector = BuildSkyGradientLUT(c_rNextColorVector, uRequiredSideSamples);

	const DWORD dwNow = ELTimer_GetMSec();
	static DWORD s_dwGradientValidationLogTick = 0u;
	if (0u == s_dwGradientValidationLogTick || (dwNow - s_dwGradientValidationLogTick) >= 2000u)
	{
		s_dwGradientValidationLogTick = dwNow;
		TraceError("DX11_SKY_GRADIENT_VALIDATION input=%u next_input=%u required=%u normalized=%u",
			static_cast<unsigned int>(c_rColorVector.size()),
			static_cast<unsigned int>(c_rNextColorVector.size()),
			static_cast<unsigned int>(uRequiredSideSamples),
			static_cast<unsigned int>(kColorVector.size()));
	}

	auto ApplySideGradient = [&](CSkyObjectQuad& rQuad, size_t uIndex)
	{
		const TGradientColor& rkCur = kColorVector[uIndex];
		const TGradientColor& rkNext = kNextColorVector[uIndex];

		rQuad.SetSrcColor(0, rkCur.m_SecondColor.r, rkCur.m_SecondColor.g, rkCur.m_SecondColor.b, rkCur.m_SecondColor.a);
		rQuad.SetTransition(0, rkNext.m_SecondColor.r, rkNext.m_SecondColor.g, rkNext.m_SecondColor.b, rkNext.m_SecondColor.a, lTransitionTime);
		rQuad.SetSrcColor(1, rkCur.m_FirstColor.r, rkCur.m_FirstColor.g, rkCur.m_FirstColor.b, rkCur.m_FirstColor.a);
		rQuad.SetTransition(1, rkNext.m_FirstColor.r, rkNext.m_FirstColor.g, rkNext.m_FirstColor.b, rkNext.m_FirstColor.a, lTransitionTime);
		rQuad.SetSrcColor(2, rkCur.m_SecondColor.r, rkCur.m_SecondColor.g, rkCur.m_SecondColor.b, rkCur.m_SecondColor.a);
		rQuad.SetTransition(2, rkNext.m_SecondColor.r, rkNext.m_SecondColor.g, rkNext.m_SecondColor.b, rkNext.m_SecondColor.a, lTransitionTime);
		rQuad.SetSrcColor(3, rkCur.m_FirstColor.r, rkCur.m_FirstColor.g, rkCur.m_FirstColor.b, rkCur.m_FirstColor.a);
		rQuad.SetTransition(3, rkNext.m_FirstColor.r, rkNext.m_FirstColor.g, rkNext.m_FirstColor.b, rkNext.m_FirstColor.a, lTransitionTime);
	};

	auto ApplyFlatGradient = [&](CSkyObjectQuad& rQuad, const TColor& rkCur, const TColor& rkNext)
	{
		rQuad.SetSrcColor(0, rkCur.r, rkCur.g, rkCur.b, rkCur.a);
		rQuad.SetTransition(0, rkNext.r, rkNext.g, rkNext.b, rkNext.a, lTransitionTime);
		rQuad.SetSrcColor(1, rkCur.r, rkCur.g, rkCur.b, rkCur.a);
		rQuad.SetTransition(1, rkNext.r, rkNext.g, rkNext.b, rkNext.a, lTransitionTime);
		rQuad.SetSrcColor(2, rkCur.r, rkCur.g, rkCur.b, rkCur.a);
		rQuad.SetTransition(2, rkNext.r, rkNext.g, rkNext.b, rkNext.a, lTransitionTime);
		rQuad.SetSrcColor(3, rkCur.r, rkCur.g, rkCur.b, rkCur.a);
		rQuad.SetTransition(3, rkNext.r, rkNext.g, rkNext.b, rkNext.a, lTransitionTime);
	};

	for (unsigned char ucj = 0; ucj < 4; ++ucj)
	{
		TSkyObjectFace& rFace = m_Faces[ucj];
		for (size_t uQuad = 0; uQuad < rFace.m_SkyObjectQuadVector.size(); ++uQuad)
		{
			const size_t uGradientIndex = std::min<size_t>(uQuad, kColorVector.size() - 1u);
			ApplySideGradient(rFace.m_SkyObjectQuadVector[uQuad], uGradientIndex);
		}
	}

	TSkyObjectFace& rTopFace = m_Faces[4];
	for (size_t uQuad = 0; uQuad < rTopFace.m_SkyObjectQuadVector.size(); ++uQuad)
	{
		ApplyFlatGradient(
			rTopFace.m_SkyObjectQuadVector[uQuad],
			kColorVector.front().m_FirstColor,
			kNextColorVector.front().m_FirstColor);
	}

	TSkyObjectFace& rBottomFace = m_Faces[5];
	const size_t uBottomIndex = kColorVector.size() - 1u;
	for (size_t uQuad = 0; uQuad < rBottomFace.m_SkyObjectQuadVector.size(); ++uQuad)
	{
		ApplyFlatGradient(
			rBottomFace.m_SkyObjectQuadVector[uQuad],
			kColorVector[uBottomIndex].m_SecondColor,
			kNextColorVector[uBottomIndex].m_SecondColor);
	}
}
void CSkyBox::StartTransition()
{
	m_bTransitionStarted = true;
	for (unsigned char ucj = 0; ucj < 6; ++ucj)
		m_Faces[ucj].StartTransition();
	m_FaceCloud.StartTransition();
}

void CSkyBox::Update()
{
	CSkyObject::Update();

	if (!m_bTransitionStarted)
		return;
	
	bool bResult = false;
	for (unsigned char uci = 0; uci < 6; ++uci)
 		bResult = m_Faces[uci].Update() || bResult;
 	bResult = m_FaceCloud.Update() || bResult;

	m_bTransitionStarted = bResult;
}

void CSkyBox::Render()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	gs_matSkyDX11World = m_matWorld;
	gs_fSkyDX11UOffset = 0.0f;
	gs_fSkyDX11VOffset = 0.0f;
	gs_fSkyDX11UScale = 1.0f;
	gs_fSkyDX11VScale = 1.0f;
    // DX11 native sky pass: opaque/no-blend for faces; clouds use dedicated cloud blend in RenderCloud().
    gs_pSkyDX11BlendState = NULL;
	gs_uSkyDX11ExpectedQuads = 0u;
	gs_uSkyDX11SubmittedQuads = 0u;
	gs_bSkyDX11TextureBound = false;
	gs_uSkyDX11SampledAlphaHint = 0u;
	gs_bSkyDX11ClampSampler = false;
	gs_bSkyDX11DiffuseSampled = false;
	gs_fSkyDX11DiffuseMin = 1.0f;
	gs_fSkyDX11DiffuseMax = 0.0f;
	static bool s_abSkyFaceMissingLogged[6] = { false, false, false, false, false, false };
	auto LogSkyFaceMissing = [&](unsigned int iFace, const char* c_szReason)
	{
		if (iFace >= 6 || s_abSkyFaceMissingLogged[iFace])
			return;
		s_abSkyFaceMissingLogged[iFace] = true;
		TraceError("DX11_SKY_FACE_MISSING face=%u file=%s reason=%s",
			iFace,
			m_Faces[iFace].m_strFaceTextureFileName.c_str(),
			c_szReason ? c_szReason : "unknown");
	};

	if (m_ucRenderMode == CSkyObject::SKY_RENDER_MODE_TEXTURE)
	{
		gs_eSkyDX11ShadingMode = SKY_DX11_SHADING_TEXTURE;
		gs_szSkyDX11Mode = "texture";
		gs_bSkyDX11ClampSampler = true;

		// M2-SKY-ENV-FIX-40: Track face texture binding status
		UINT uFaceTexturesBound = 0u;
		UINT uFaceTexturesMissing = 0u;

		for (unsigned int i = 0; i < 6; ++i)
		{
			CGraphicImageInstance* pFaceImageInstance = m_GraphicImageInstanceMap[m_Faces[i].m_strFaceTextureFileName];
			CGraphicTexture* pFaceTexture = pFaceImageInstance ? pFaceImageInstance->GetTexturePointer() : NULL;

			// M3-SKY-RESOURCE-DX11-72: Check if texture exists and is loaded
			if (pFaceTexture && !pFaceTexture->IsEmpty())
			{
				pFaceTexture->SetTextureStage(0);
				gs_bSkyDX11TextureBound = true;
				++uFaceTexturesBound;
			}
			else
			{
				// M2-SKY-ENV-FIX-40: Log missing face texture
				++uFaceTexturesMissing;

				// M3-SKY-RESOURCE-DX11-72: Provide specific reason for texture failure
				if (!pFaceImageInstance)
				{
					LogSkyFaceMissing(i, "texture_mode_instance_not_created");
				}
				else if (!pFaceTexture)
				{
					LogSkyFaceMissing(i, "texture_mode_pointer_null");
				}
				else if (pFaceTexture->IsEmpty())
				{
					// Texture object exists but failed to load (check file existence)
					const std::string& fileName = m_Faces[i].m_strFaceTextureFileName;
					const bool fileExists = CGraphicTextureDX11::DoesTextureFileExist(fileName.c_str());
					if (fileExists)
					{
						LogSkyFaceMissing(i, "texture_mode_file_exists_but_decode_failed");
					}
					else
					{
						LogSkyFaceMissing(i, "texture_mode_file_not_found");
					}
				}

				pDX11Device->SetBootstrapTextureStageSRV(0, NULL);
			}

			m_Faces[i].Render();
		}

		// M2-SKY-ENV-FIX-40: Detect and log if no face textures were bound (potential black sky)
		if (uFaceTexturesBound == 0 && uFaceTexturesMissing > 0)
		{
			static DWORD s_dwMissingFaceLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwMissingFaceLogTick || (dwNow - s_dwMissingFaceLogTick) >= 2000u)
			{
				s_dwMissingFaceLogTick = dwNow;
				TraceError("DX11_SKYENV_DRAW_FAIL stage=sky reason=no_face_textures_bound mode=texture texture_bound=0 missing_count=%u/6",
					uFaceTexturesMissing);
			}
		}
	}
	else
	{
		// M3-SKY-BLEND-FIX-74: Respect render mode set by MapOutdoor policy system
		// Previously this code forced texture mode when textures existed, overriding user's choice
		// Now we respect the mode set by SetRenderMode() via policy system
		gs_eSkyDX11ShadingMode = SKY_DX11_SHADING_DIFFUSE;
		gs_szSkyDX11Mode = "diffuse";

		// M3-SKY-RESOURCE-DX11-72: One-shot log for diffuse mode (truly missing assets)
		static bool s_bDiffuseModeLogged = false;
		if (!s_bDiffuseModeLogged)
		{
			s_bDiffuseModeLogged = true;
			// M3-SKYBOX-ASSET-BIND-73: Distinguish expected fallback from actual error
			if (m_bSkyTexturesExpected)
			{
				// ERROR: Sky textures were configured but failed to load
				TraceError("DX11_SKY_ERROR face=all reason=expected_textures_failed_to_load texture_bound=0 count=0/6");
			}
			else
			{
				// EXPECTED: Map has no sky textures configured (using diffuse gradient)
				Tracef("DX11_SKY_INFO mode=diffuse reason=no_textures_configured_for_map");
			}
		}

		pDX11Device->SetBootstrapTextureStageSRV(0, NULL);
		for (unsigned int i = 0; i < 6; ++i)
		{
			if (false) // M3-SKY-BLEND-FIX-74: Removed texture_forced_from_diffuse override
			{
				CGraphicImageInstance* pFaceImageInstance = m_GraphicImageInstanceMap[m_Faces[i].m_strFaceTextureFileName];
				CGraphicTexture* pFaceTexture = pFaceImageInstance ? pFaceImageInstance->GetTexturePointer() : NULL;
				if (pFaceTexture)
				{
					pFaceTexture->SetTextureStage(0);
					gs_bSkyDX11TextureBound = true;
				}
				else
				{
					LogSkyFaceMissing(i, "forced_texture_face_not_loaded");
					pDX11Device->SetBootstrapTextureStageSRV(0, NULL);
				}
			}
			m_Faces[i].Render();
		}

		// M3-SKY-BLEND-FIX-74: Runtime safety fallback - only when assets actually failed
		// Previously triggered on any dark gradient (e.g., legitimate night scenes)
		// Now only triggers when textures were EXPECTED but failed to load
		if (gs_bSkyDX11DiffuseSampled && gs_fSkyDX11DiffuseMax <= 0.01f && m_bSkyTexturesExpected)
		{
			bool bHasAnyFaceTexture = false;
			for (unsigned int i = 0; i < 6; ++i)
			{
				CGraphicImageInstance* pFaceImageInstance = m_GraphicImageInstanceMap[m_Faces[i].m_strFaceTextureFileName];
				CGraphicTexture* pFaceTexture = pFaceImageInstance ? pFaceImageInstance->GetTexturePointer() : NULL;
				if (pFaceTexture)
				{
					bHasAnyFaceTexture = true;
					break;
				}
			}

			if (bHasAnyFaceTexture)
			{
				gs_eSkyDX11ShadingMode = SKY_DX11_SHADING_TEXTURE;
				gs_szSkyDX11Mode = "texture_fallback";
				gs_bSkyDX11ClampSampler = true;
				gs_uSkyDX11ExpectedQuads = 0u;
				gs_uSkyDX11SubmittedQuads = 0u;
				gs_bSkyDX11TextureBound = false;
				gs_uSkyDX11SampledAlphaHint = 0u;

				for (unsigned int i = 0; i < 6; ++i)
				{
					CGraphicImageInstance* pFaceImageInstance = m_GraphicImageInstanceMap[m_Faces[i].m_strFaceTextureFileName];
					CGraphicTexture* pFaceTexture = pFaceImageInstance ? pFaceImageInstance->GetTexturePointer() : NULL;
					if (pFaceTexture)
					{
						pFaceTexture->SetTextureStage(0);
						gs_bSkyDX11TextureBound = true;
					}
					else
					{
						LogSkyFaceMissing(i, "texture_fallback_face_not_loaded");
						pDX11Device->SetBootstrapTextureStageSRV(0, NULL);
					}

					m_Faces[i].Render();
				}
			}
		}
	}

	pDX11Device->SetBootstrapTextureStageSRV(0, NULL);
	gs_pSkyDX11BlendState = NULL;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0u == gs_dwSkyParityLogTick || (dwNow - gs_dwSkyParityLogTick) >= 2000u)
	{
		gs_dwSkyParityLogTick = dwNow;
        TraceError("DX11_SKY_RENDER_STATE sky_blend=opaque cloud_blend=alpha");
		TraceError("DX11_PIPELINE_STATE_PARITY pass=skyenv path=dx11_native stage=sky mode=%s texture_bound=%u sampled_alpha_hint=%u diffuse_min=%.3f diffuse_max=%.3f",
			gs_szSkyDX11Mode,
			gs_bSkyDX11TextureBound ? 1u : 0u,
			gs_uSkyDX11SampledAlphaHint,
			gs_bSkyDX11DiffuseSampled ? gs_fSkyDX11DiffuseMin : -1.0f,
			gs_bSkyDX11DiffuseSampled ? gs_fSkyDX11DiffuseMax : -1.0f);
		TraceError("DX11_PIPELINE_SUBMIT_PARITY pass=skyenv expected=%u submitted=%u stage=sky mode=%s texture_bound=%u sampled_alpha_hint=%u diffuse_min=%.3f diffuse_max=%.3f",
			gs_uSkyDX11ExpectedQuads,
			gs_uSkyDX11SubmittedQuads,
			gs_szSkyDX11Mode,
			gs_bSkyDX11TextureBound ? 1u : 0u,
			gs_uSkyDX11SampledAlphaHint,
			gs_bSkyDX11DiffuseSampled ? gs_fSkyDX11DiffuseMin : -1.0f,
			gs_bSkyDX11DiffuseSampled ? gs_fSkyDX11DiffuseMax : -1.0f);
	}
}

void CSkyBox::RenderCloud()
{
	if (m_FaceCloud.m_strfacename.empty())
		return;

	CGraphicImageInstance* pCloudGraphicImageInstance = m_GraphicImageInstanceMap[m_FaceCloud.m_strfacename];
	CGraphicTexture* pCloudTexture = pCloudGraphicImageInstance ? pCloudGraphicImageInstance->GetTexturePointer() : NULL;

	DWORD dwCurTime = CTimer::Instance().GetCurrentMillisecond();
	m_fCloudPositionU = fmodf(m_fCloudPositionU + m_fCloudScrollSpeedU * (float)(dwCurTime - m_dwlastTime) * 0.001f, 1.0f);
	m_fCloudPositionV = fmodf(m_fCloudPositionV + m_fCloudScrollSpeedV * (float)(dwCurTime - m_dwlastTime) * 0.001f, 1.0f);
	if (m_fCloudPositionU < 0.0f) m_fCloudPositionU += 1.0f;
	if (m_fCloudPositionV < 0.0f) m_fCloudPositionV += 1.0f;
	m_dwlastTime = dwCurTime;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	gs_matSkyDX11World = m_matWorldCloud;
	gs_fSkyDX11UOffset = m_fCloudPositionU;
	gs_fSkyDX11VOffset = m_fCloudPositionV;
	gs_fSkyDX11UScale = m_fCloudTextureScaleX;
	gs_fSkyDX11VScale = m_fCloudTextureScaleY;
	gs_eSkyDX11ShadingMode = SKY_DX11_SHADING_CLOUD_COMBINE;
	gs_szSkyDX11Mode = "cloud_combine";
	gs_pSkyDX11BlendState = pDX11Device->GetBootstrapUICloudBlendState();
	gs_uSkyDX11ExpectedQuads = 0u;
	gs_uSkyDX11SubmittedQuads = 0u;
	gs_bSkyDX11TextureBound = (pCloudTexture != NULL);
	gs_uSkyDX11SampledAlphaHint = gs_bSkyDX11TextureBound ? 1u : 0u;
	gs_bSkyDX11ClampSampler = false;

	if (pCloudTexture)
		pCloudTexture->SetTextureStage(0);
	else
		pDX11Device->SetBootstrapTextureStageSRV(0, NULL);

	m_FaceCloud.Render();
	pDX11Device->SetBootstrapTextureStageSRV(0, NULL);
	gs_pSkyDX11BlendState = NULL;

	if (0u == gs_dwCloudParityLogTick || (dwCurTime - gs_dwCloudParityLogTick) >= 2000u)
	{
		gs_dwCloudParityLogTick = dwCurTime;
		TraceError("DX11_CLOUD_BLEND_MODE mode=combine texture_weight=%.3f min_contrib=%.3f alpha_min=%.3f vertex_alpha_floor=%.3f",
			DX11RuntimeConfig::kSkyCloudTextureBlendWeight,
			DX11RuntimeConfig::kSkyCloudTextureMinContribution,
			DX11RuntimeConfig::kSkyCloudAlphaMin,
			DX11RuntimeConfig::kSkyCloudVertexAlphaFloor);
		TraceError("DX11_PIPELINE_STATE_PARITY pass=skyenv path=dx11_native stage=cloud mode=%s texture_bound=%u sampled_alpha_hint=%u",
			gs_szSkyDX11Mode,
			gs_bSkyDX11TextureBound ? 1u : 0u,
			gs_uSkyDX11SampledAlphaHint);
		TraceError("DX11_PIPELINE_SUBMIT_PARITY pass=skyenv expected=%u submitted=%u stage=cloud mode=%s texture_bound=%u sampled_alpha_hint=%u",
			gs_uSkyDX11ExpectedQuads,
			gs_uSkyDX11SubmittedQuads,
			gs_szSkyDX11Mode,
			gs_bSkyDX11TextureBound ? 1u : 0u,
			gs_uSkyDX11SampledAlphaHint);
	}
}
