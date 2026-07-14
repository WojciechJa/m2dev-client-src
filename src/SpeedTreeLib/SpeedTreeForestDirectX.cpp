///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeForestDirectX Class
//
//	(c) 2003 IDV, Inc.
//
//	This class is provided to illustrate one way to incorporate
//	SpeedTreeRT into an OpenGL application.  All of the SpeedTreeRT
//	calls that must be made on a per tree basis are done by this class.
//	Calls that apply to all trees (i.e. static SpeedTreeRT functions)
//	are made in the functions in main.cpp.
//
//
//	*** INTERACTIVE DATA VISUALIZATION (IDV) PROPRIETARY INFORMATION ***
//
//	This software is supplied under the terms of a license agreement or
//	nondisclosure agreement with Interactive Data Visualization and may
//	not be copied or disclosed except in accordance with the terms of
//	that agreement.
//
//      Copyright (c) 2001-2003 IDV, Inc.
//      All Rights Reserved.
//
//		IDV, Inc.
//		1233 Washington St. Suite 610
//		Columbia, SC 29201
//		Voice: (803) 799-1699
//		Fax:   (803) 931-0320
//		Web:   http://www.idvinc.com

#include "StdAfx.h"

#include "Constants.h"  // Must be included early for USE_SPEEDGRASS definition

#include <stdio.h>
#include <set>
#include <vector>

#include "EterBase/Timer.h"
#include "EterLib/Camera.h"

#include "SpeedTreeForestDirectX.h"
#include "SpeedTreeConfig.h"
#include "UserInterface/config.h"

// DX11: ImGui metrics for draw call tracking
#include "../DebugUI/ImGuiGraphicsMetrics.h"

// Iteration 1: Grass rendering includes
#include "GrassVertex.h"

// Grass shader source (inline HLSL)
static const char* GrassShadersHLSL = R"(
// DX11 Grass Shaders
// Billboard grass rendering with wind animation
// Iteration 3 - Wind Animation

// Grass Constant Buffer
cbuffer GrassCB : register(b0)
{
	row_major float4x4 gWorldViewProj;     // World-view-projection matrix
	float4 gCameraPosAndGrassSize;         // Camera position (xyz) and grass size (w)
	float4 gTimeAndWind;                   // Time (x), wind strength (y), padding (zw)
	row_major float4x4 gWindMatrix;        // Wind rotation matrix
}

// Vertex Input
struct VS_IN
{
	float3 pos : POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
};

// Pixel Input
struct PS_IN
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
};

// Grass Vertex Shader - Billboard with Wind Animation
PS_IN main(VS_IN input)
{
	PS_IN output;

	// Extract values from constant buffer
	float3 gCameraPos = gCameraPosAndGrassSize.xyz;
	float gGrassSize = gCameraPosAndGrassSize.w;
	float gTime = gTimeAndWind.x;
	float gWindStrength = gTimeAndWind.y;

	// Get world position
	float3 worldPos = input.pos;

	// Apply wind animation to grass blade tip
	// Grass bends more at the top (higher UV.y = more bend)
	float bendFactor = input.uv.y * gWindStrength;

	// Calculate wind offset using sinusoidal motion for natural movement
	float windPhase = gTime * 2.0f + worldPos.x * 0.1f + worldPos.z * 0.1f;
	float windOffset = sin(windPhase) * bendFactor * gGrassSize * 0.5f;

	// Apply wind matrix rotation for more complex movement
	float3 windPos = worldPos;
	windPos.y += bendFactor * gGrassSize;  // Lift the tip
	windPos.x += windOffset;  // Simple wind along X axis

	// Blend between original and wind-affected position based on height
	worldPos = lerp(worldPos, windPos, bendFactor);

	// Billboard transformation: always face camera
	float3 toCamera = gCameraPos - worldPos;
	toCamera.y = 0.0f;  // Keep grass upright
	toCamera = normalize(toCamera);

	// Calculate billboard right and up vectors
	float3 right = float3(1.0f, 0.0f, 0.0f);
	float3 up = float3(0.0f, 1.0f, 0.0f);

	// Expand single vertex to quad
	float2 offset = input.uv * gGrassSize;
	worldPos += right * offset.x + up * offset.y;

	// Transform to clip space
	output.pos = mul(float4(worldPos, 1.0f), gWorldViewProj);
	output.uv = input.uv;
	output.color = input.color;

	return output;
}

// Grass Pixel Shader - With Texture Sampling
Texture2D g_txGrass : register(t0);
SamplerState g_smGrass : register(s0);
cbuffer GrassAlphaCB : register(b1)
{
	float g_fGrassAlphaRef;
	float3 g_padGrassAlpha;
};

float4 mainPS(PS_IN input) : SV_TARGET
{
	float4 baseColor = input.color;

	// Sample grass texture if available
	#ifdef GRASS_USE_TEXTURE
		float4 texColor = g_txGrass.Sample(g_smGrass, input.uv);
		baseColor *= texColor;

		// Alpha test
		if (baseColor.a < g_fGrassAlphaRef)
			discard;
	#endif

	return baseColor;
}

// Grass Shadow Pixel Shader - Depth-only with alpha clip
// Used during shadow map rendering to cast grass shadows
#ifdef GRASS_USE_TEXTURE
void mainShadowPS(PS_IN input)
{
	// Sample texture for alpha test
	float4 texColor = g_txGrass.Sample(g_smGrass, input.uv);

	// Alpha test - discard transparent pixels
	clip(texColor.a - g_fGrassAlphaRef);
}
#else
void mainShadowPS(PS_IN input)
{
	// When texture is disabled, use vertex color alpha
	clip(input.color.a - g_fGrassAlphaRef);
}
#endif
)";

// W3 includes (DX11)
#include <d3d11.h>
#include <d3dcompiler.h>
#include "EterLib/GrpDeviceDX11.h"
#include "EterLib/GrpTextureDX11.h"
#include "EterLib/GrpImageInstance.h"
#include "EterLib/ResourceManager.h"
#include "SpeedTreeWrapper.h"
#include "SpeedGrassWrapper.h"
#include "SpeedGrassRT.h"

// W4.3: Static per-tree-type DX11 buffer cache (avoids creating buffers per-instance per-frame)
// Key: CSpeedTreeWrapper* (main tree = tree type)
// Value: DX11 vertex/index buffers and strip metadata for that tree type
static std::map<CSpeedTreeWrapper*, ID3D11Buffer*> s_mapBranchVB;
static std::map<CSpeedTreeWrapper*, ID3D11Buffer*> s_mapBranchIB;
static std::map<CSpeedTreeWrapper*, std::vector<std::pair<UINT, UINT>>> s_mapBranchStrips; // <offset, count>
static std::map<CSpeedTreeWrapper*, UINT> s_mapBranchIndexCount;
struct SSpeedTreeIndexedCacheSignature
{
	int lod = -1;
	unsigned short vertexCount = 0;
	unsigned short stripCount = 0;
	uint32_t stripLengthHash = 0;
	uint32_t stripEdgeIndexHash = 0;
};
static std::map<CSpeedTreeWrapper*, SSpeedTreeIndexedCacheSignature> s_mapBranchCacheSig;

static std::map<CSpeedTreeWrapper*, ID3D11Buffer*> s_mapFrondVB;
static std::map<CSpeedTreeWrapper*, ID3D11Buffer*> s_mapFrondIB;
static std::map<CSpeedTreeWrapper*, std::vector<std::pair<UINT, UINT>>> s_mapFrondStrips;
static std::map<CSpeedTreeWrapper*, UINT> s_mapFrondIndexCount;
static std::map<CSpeedTreeWrapper*, SSpeedTreeIndexedCacheSignature> s_mapFrondCacheSig;

static std::map<CSpeedTreeWrapper*, ID3D11Buffer*> s_mapLeafVB;
struct SSpeedTreeLeafDrawCall
{
	UINT vertexCount = 0;
	UINT startVertex = 0;
	float alphaRef = 0.01f;
};
static std::map<CSpeedTreeWrapper*, std::vector<SSpeedTreeLeafDrawCall>> s_mapLeafStrips; // Leaves use triangle list
struct SSpeedTreeLeafCacheSignature
{
	int lod0 = -1;
	int lod1 = -1;
	unsigned short leafCount0 = 0;
	unsigned short leafCount1 = 0;
	bool active0 = false;
	bool active1 = false;
	uint32_t leafTableEntryCount = 0;
};

inline uint32_t HashSpeedTreeStrips(const unsigned short* pStripLengths, unsigned short usNumStrips)
{
	if (!pStripLengths || usNumStrips == 0)
		return 0u;

	uint32_t uHash = 2166136261u; // FNV-1a base
	for (unsigned short i = 0; i < usNumStrips; ++i)
	{
		uHash ^= static_cast<uint32_t>(pStripLengths[i]);
		uHash *= 16777619u;
	}
	return uHash;
}

inline uint32_t HashSpeedTreeStripEdges(const uint16_t* const* ppStrips, const unsigned short* pStripLengths, unsigned short usNumStrips)
{
	if (!ppStrips || !pStripLengths || usNumStrips == 0)
		return 0u;

	uint32_t uHash = 2166136261u; // FNV-1a base
	for (unsigned short i = 0; i < usNumStrips; ++i)
	{
		const uint16_t* pStrip = ppStrips[i];
		const unsigned short usStripCount = pStripLengths[i];
		if (!pStrip || usStripCount == 0)
			continue;

		uHash ^= static_cast<uint32_t>(pStrip[0]);
		uHash *= 16777619u;
		uHash ^= static_cast<uint32_t>(pStrip[usStripCount - 1]);
		uHash *= 16777619u;
	}
	return uHash;
}
static std::map<CSpeedTreeWrapper*, SSpeedTreeLeafCacheSignature> s_mapLeafCacheSig;

static ID3D11RasterizerState* __GetDX11SpeedTreeCullNoneState(ID3D11Device* pDevice)
{
	static ID3D11RasterizerState* s_pCullNoneState = nullptr;
	if (!pDevice)
		return nullptr;

	if (!s_pCullNoneState)
	{
		D3D11_RASTERIZER_DESC kDesc = {};
		kDesc.FillMode = D3D11_FILL_SOLID;
		kDesc.CullMode = D3D11_CULL_NONE;
		kDesc.FrontCounterClockwise = FALSE;
		kDesc.DepthClipEnable = TRUE;
		kDesc.MultisampleEnable = FALSE;
		kDesc.ScissorEnable = FALSE;
		if (FAILED(pDevice->CreateRasterizerState(&kDesc, &s_pCullNoneState)))
			s_pCullNoneState = nullptr;
	}

	return s_pCullNoneState;
}

namespace
{
// DX11: Legacy state manager removed

template <typename TValue>
inline void PurgeStalePlainCache(std::map<CSpeedTreeWrapper*, TValue>& cache, const std::set<CSpeedTreeWrapper*>& liveKeys)
{
	for (auto it = cache.begin(); it != cache.end();)
	{
		if (liveKeys.find(it->first) == liveKeys.end())
			it = cache.erase(it);
		else
			++it;
	}
}

inline void PurgeStaleBufferCache(std::map<CSpeedTreeWrapper*, ID3D11Buffer*>& cache, const std::set<CSpeedTreeWrapper*>& liveKeys)
{
	for (auto it = cache.begin(); it != cache.end();)
	{
		if (liveKeys.find(it->first) == liveKeys.end())
		{
			if (it->second)
				it->second->Release();
			it = cache.erase(it);
		}
		else
		{
			++it;
		}
	}
}

inline void PurgeStaleSpeedTreeGeometryCaches(const CSpeedTreeForest::TTreeMap& mainTreeMap)
{
	std::set<CSpeedTreeWrapper*> liveKeys;
	for (const auto& pair : mainTreeMap)
	{
		if (pair.second)
			liveKeys.insert(pair.second.get());
	}

	PurgeStaleBufferCache(s_mapBranchVB, liveKeys);
	PurgeStaleBufferCache(s_mapBranchIB, liveKeys);
	PurgeStalePlainCache(s_mapBranchStrips, liveKeys);
	PurgeStalePlainCache(s_mapBranchIndexCount, liveKeys);
	PurgeStalePlainCache(s_mapBranchCacheSig, liveKeys);

	PurgeStaleBufferCache(s_mapFrondVB, liveKeys);
	PurgeStaleBufferCache(s_mapFrondIB, liveKeys);
	PurgeStalePlainCache(s_mapFrondStrips, liveKeys);
	PurgeStalePlainCache(s_mapFrondIndexCount, liveKeys);
	PurgeStalePlainCache(s_mapFrondCacheSig, liveKeys);

	PurgeStaleBufferCache(s_mapLeafVB, liveKeys);
	PurgeStalePlainCache(s_mapLeafStrips, liveKeys);
	PurgeStalePlainCache(s_mapLeafCacheSig, liveKeys);
}

struct SSpeedTreeAlphaClipCB
{
	float alphaRef;
	float padding[3];
};

static const UINT kDX11SpeedTreeMaxLeafTableEntries = 1024u; // float4 entries (fits in 64KB VS cbuffer)
struct SSpeedTreeLeafPlacementCB
{
	DirectX::SimpleMath::Vector4 entries[kDX11SpeedTreeMaxLeafTableEntries];
};

inline float NormalizeAlphaRef(float legacyAlphaRef)
{
	if (legacyAlphaRef <= 0.0f)
		return 0.0f;

	// SpeedTree legacy path uses GRP_RS_ALPHAREF (0..255).
	if (legacyAlphaRef > 1.0f)
		legacyAlphaRef /= 255.0f;

	if (legacyAlphaRef < 0.0f)
		legacyAlphaRef = 0.0f;
	if (legacyAlphaRef > 1.0f)
		legacyAlphaRef = 1.0f;
	return legacyAlphaRef;
}

inline void UpdateDX11SpeedTreeAlphaRef(ID3D11DeviceContext* pContext, ID3D11Buffer* pAlphaRefBuffer, float alphaRef)
{
	if (!pContext || !pAlphaRefBuffer)
		return;

	SSpeedTreeAlphaClipCB cbData = {};
	cbData.alphaRef = NormalizeAlphaRef(alphaRef);
	pContext->UpdateSubresource(pAlphaRefBuffer, 0, nullptr, &cbData, 0, 0);
	pContext->PSSetConstantBuffers(1, 1, &pAlphaRefBuffer);
}

inline void UpdateDX11SpeedTreeLeafPlacementTable(ID3D11DeviceContext* pContext, ID3D11Buffer* pLeafPlacementBuffer, const float* pTable, UINT uiFloatCount)
{
	if (!pContext || !pLeafPlacementBuffer || !pTable || uiFloatCount < 4u)
		return;

	const UINT uiEntryCount = uiFloatCount / 4u;
	if (uiEntryCount == 0u)
		return;

	SSpeedTreeLeafPlacementCB cbData = {};
	const UINT uiCopyCount = (uiEntryCount < kDX11SpeedTreeMaxLeafTableEntries) ? uiEntryCount : kDX11SpeedTreeMaxLeafTableEntries;
	for (UINT i = 0; i < uiCopyCount; ++i)
	{
		const float* pSrc = pTable + (i * 4u);
		cbData.entries[i] = DirectX::SimpleMath::Vector4(pSrc[0], pSrc[1], pSrc[2], pSrc[3]);
	}

	pContext->UpdateSubresource(pLeafPlacementBuffer, 0, nullptr, &cbData, 0, 0);
	pContext->VSSetConstantBuffers(2, 1, &pLeafPlacementBuffer);
}
}

///////////////////////////////////////////////////////////////////////
//	CSpeedTreeForestDirectX::CSpeedTreeForestDirectX

CSpeedTreeForestDirectX::CSpeedTreeForestDirectX()
	: m_dwLastRenderedVisibleInstanceCount(0)
	, m_dwLastRenderedVisibleTickMS(0)
	, m_dwLastDX11SubmittedInstanceCount(0)
{
	// W3: Initialize DX11 resources
	m_pDX11SpeedTreeBillboardVS = nullptr;
	m_pDX11SpeedTreeBillboardPS = nullptr;
	m_pDX11SpeedTreeBillboardInputLayout = nullptr;
	m_pDX11SpeedTreeConstantBuffer = nullptr;
	m_pDX11SpeedTreeAlphaRefBuffer = nullptr;
	m_pDX11SpeedTreeLeafPlacementBuffer = nullptr;
	m_pDX11SpeedTreeSamplerState = nullptr;
	m_pDX11SpeedTreeBlendState = nullptr;
	m_pDX11SpeedTreeAlphaToCoverageBlendState = nullptr;
	m_pDX11SpeedTreeDynamicVB = nullptr;
	m_pDX11SpeedTreeDefaultTextureSRV = nullptr;

	// W3.2/W3.3: Initialize branch/frond/leaf resources
	m_pDX11SpeedTreeBranchVS = nullptr;
	m_pDX11SpeedTreeBranchPS = nullptr;
	m_pDX11SpeedTreeBranchOpaquePS = nullptr;
	m_pDX11SpeedTreeShadowAlphaPS = nullptr;
	m_pDX11SpeedTreeBranchInputLayout = nullptr;
	m_pDX11SpeedTreeLeafVS = nullptr;
	m_pDX11SpeedTreeLeafInputLayout = nullptr;

	m_bDX11SpeedTreeResourcesReady = false;
	m_bDX11ShadowViewProjOverrideActive = false;
	m_matDX11ShadowViewProjOverride = DirectX::SimpleMath::Matrix::Identity;

	// Iteration 1: Initialize grass resources
	m_pDX11GrassVS = nullptr;
	m_pDX11GrassPS = nullptr;
	m_pDX11GrassInputLayout = nullptr;
	m_pDX11GrassConstantBuffer = nullptr;
	m_pDX11GrassVertexBuffer = nullptr;
	m_pDX11GrassSamplerState = nullptr;
	m_pDX11GrassBlendState = nullptr;
	m_pDX11GrassTextureSRV = nullptr;
	m_pDX11GrassAlphaRefBuffer = nullptr;
	m_pDX11GrassShadowAlphaPS = nullptr;
	m_uiDX11GrassVertexCount = 0;
	m_bDX11GrassResourcesReady = false;

	// Iteration 2: Initialize grass wrapper
	m_pGrassWrapper = nullptr;
	m_pGrassMapOutdoor = nullptr;
	m_uiDX11GrassRegionCount = 0;
	m_uiDX11GrassBladeCount = 0;
	m_bMatricesCached = false;
	m_matCachedView = DirectX::SimpleMath::Matrix::Identity;
	m_matCachedProj = DirectX::SimpleMath::Matrix::Identity;
	m_vCachedCameraPos = DirectX::SimpleMath::Vector3::Zero;

	// Iteration 3: Initialize wind animation data
	m_matGrassWindMatrix = DirectX::SimpleMath::Matrix::Identity;
	m_fGrassWindTime = 0.0f;

	// Iteration 4: Initialize grass LOD configuration from config.h
	m_fGrassLodNearDistance = DX11RuntimeConfig::kGrassLodNearDistance;
	m_fGrassLodFarDistance = DX11RuntimeConfig::kGrassLodFarDistance;
	m_fGrassSize = DX11RuntimeConfig::kGrassSize;
	m_uiDX11GrassRenderedBlades = 0;
	m_fGrassLodBlendFactor = 0.0f;
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeForestDirectX::~CSpeedTreeForestDirectX

CSpeedTreeForestDirectX::~CSpeedTreeForestDirectX()
{
	DestroyDX11SpeedTreeResources();
	DestroyDX11GrassResources();
	if (m_pGrassWrapper)
	{
		delete m_pGrassWrapper;
		m_pGrassWrapper = nullptr;
	}
	m_pGrassMapOutdoor = nullptr;
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeForestDirectX::InitVertexShaders
bool CSpeedTreeForestDirectX::InitVertexShaders(void)
{
	// DX11: Shaders handled by DX11 pipeline
	return false;
}

bool CSpeedTreeForestDirectX::EnsureVertexShaders()
{
	return InitVertexShaders();
}

bool CSpeedTreeForestDirectX::SetRenderingDevice()
{
	// DX11: No legacy shader initialization needed

	const float c_afLightPosition[4] = { -0.707f, -0.300f, 0.707f, 0.0f };
	const float	c_afLightAmbient[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
	const float	c_afLightDiffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const float	c_afLightSpecular[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	float afLight1[] =
	{
		c_afLightPosition[0], c_afLightPosition[1], c_afLightPosition[2],	// pos
		c_afLightDiffuse[0], c_afLightDiffuse[1], c_afLightDiffuse[2],		// diffuse
		c_afLightAmbient[0], c_afLightAmbient[1], c_afLightAmbient[2],		// ambient
		c_afLightSpecular[0], c_afLightSpecular[1], c_afLightSpecular[2],	// specular
		c_afLightPosition[3],												// directional flag
		1.0f, 0.0f, 0.0f													// attenuation (constant, linear, quadratic)
	};

	CSpeedTreeRT::SetNumWindMatrices(c_nNumWindMatrices);

	CSpeedTreeRT::SetLightAttributes(0, afLight1);
	CSpeedTreeRT::SetLightState(0, true);
	return true;
}

///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeForestDirectX::UploadWindMatrix

void CSpeedTreeForestDirectX::UploadWindMatrix(UINT uiLocation, const float* pMatrix) const
{
	// DX11 wind data is updated through constant buffers in the native pass.
	// Iteration 3: Capture first wind matrix for grass animation
	// Wind matrix 0 is uploaded to register c_nVertexShader_WindMatrices + 0 * 4 = 41
	if (uiLocation == c_nVertexShader_WindMatrices && pMatrix)
	{
		// Store the first wind matrix for grass use (cast away const for this specific case)
		CSpeedTreeForestDirectX* pThis = const_cast<CSpeedTreeForestDirectX*>(this);
		pThis->m_matGrassWindMatrix = DirectX::SimpleMath::Matrix(
			pMatrix[0], pMatrix[1], pMatrix[2], pMatrix[3],
			pMatrix[4], pMatrix[5], pMatrix[6], pMatrix[7],
			pMatrix[8], pMatrix[9], pMatrix[10], pMatrix[11],
			pMatrix[12], pMatrix[13], pMatrix[14], pMatrix[15]
		);
	}
	(void)uiLocation; // Suppressed unused warning in release builds
}

void CSpeedTreeForestDirectX::UpdateCompundMatrix(const DirectX::SimpleMath::Vector3& c_rEyeVec, const DirectX::SimpleMath::Matrix& c_rmatView, const DirectX::SimpleMath::Matrix& c_rmatProj)
{
    // setup composite matrix for shader
	DirectX::SimpleMath::Matrix matBlend;
	matBlend = DirectX::SimpleMath::Matrix::Identity;

	DirectX::SimpleMath::Matrix matBlendShader;
	matBlendShader = c_rmatView * c_rmatProj;

	float afDirection[3];
	afDirection[0] = matBlendShader.m[0][2];
	afDirection[1] = matBlendShader.m[1][2];
	afDirection[2] = matBlendShader.m[2][2];
	CSpeedTreeRT::SetCamera((const float*)&c_rEyeVec, afDirection);

	// Iteration 2: Cache matrices and camera position for grass rendering
	m_matCachedView = c_rmatView;
	m_matCachedProj = c_rmatProj;
	m_vCachedCameraPos = c_rEyeVec;
	m_bMatricesCached = true;
}

///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeForestDirectX::Render

void CSpeedTreeForestDirectX::Render(unsigned long ulRenderBitVector)
{
	UpdateSystem(CTimer::Instance().GetCurrentSecond());
	 m_dwLastRenderedVisibleInstanceCount = 0;
	ResetDX11SubmittedInstanceCount();

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	const bool bDX11Runtime = (nullptr != pDX11Device) && pDX11Device->IsValid();
	if (bDX11Runtime && !IsDX11SpeedTreeResourcesReady())
		InitializeDX11SpeedTreeResources(pDX11Device->GetDevice());
	const bool bDX11AnyPassReady = bDX11Runtime && IsDX11SpeedTreeResourcesReady();

	// Iteration 5: Initialize grass resources if needed
	if (bDX11Runtime && !IsDX11GrassResourcesReady())
		InitializeDX11GrassResources(pDX11Device->GetDevice());
	const bool bDX11GrassReady = bDX11Runtime && IsDX11GrassResourcesReady();
	const bool bDX11VisibleColorPass =
		bDX11AnyPassReady &&
		!(ulRenderBitVector & Forest_RenderToShadow) &&
		!(ulRenderBitVector & Forest_RenderToMiniMap);

	// Safety latch: visible color pass must never inherit shadow override state.
	// If it does, branch/frond/leaf path would bind shadow alpha-clip PS and produce no color.
	if (bDX11VisibleColorPass && m_bDX11ShadowViewProjOverrideActive)
	{
		m_bDX11ShadowViewProjOverrideActive = false;
		static DWORD s_dwLastShadowLeakLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastShadowLeakLogMS || (dwNow - s_dwLastShadowLeakLogMS) >= 2000u)
		{
			s_dwLastShadowLeakLogMS = dwNow;
			TraceError("DX11_SPEEDTREE_STATE_FIXUP cleared_shadow_override_for_visible_pass");
		}
	}

	// DX11: Skip if resources not ready
	if (bDX11Runtime && !bDX11AnyPassReady)
	{
		static DWORD s_dwLastDX11SkipLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastDX11SkipLogMS || (dwNow - s_dwLastDX11SkipLogMS) >= 2000u)
		{
			s_dwLastDX11SkipLogMS = dwNow;
			TraceError("DX11_SPEEDTREE_SKIP reason=dx11_resources_not_ready");
		}
		return;
	}

	if (m_pMainTreeMap.empty())
		return;

	if (bDX11AnyPassReady)
		PurgeStaleSpeedTreeGeometryCaches(m_pMainTreeMap);

	// DX9 parity: keep SpeedTree camera frustum inputs updated before visibility tests.
	// In DX11 path we no longer rely on legacy shader constants, but SpeedTreeRT::isShow()
	// still depends on the camera vector set through UpdateCompundMatrix/SetCamera.
	if (!(ulRenderBitVector & Forest_RenderToShadow) && !(ulRenderBitVector & Forest_RenderToMiniMap))
	{
		CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
		if (pCamera)
		{
			UpdateCompundMatrix(pCamera->GetEye(), ms_matView, ms_matProj);
		}
	}

	TTreeMap::iterator itor;
	UINT uiCount;
	DWORD dwVisibleInstanceCount = 0;
	
	itor = m_pMainTreeMap.begin();

	while (itor != m_pMainTreeMap.end())
	{
		auto pMainTree = (itor++)->second;
		auto ppInstances = pMainTree->GetInstances(uiCount);

		for (auto it : ppInstances)
		{
			it->Advance();
			if (it->isShow())
				++dwVisibleInstanceCount;
		}
	}
	m_dwLastRenderedVisibleInstanceCount = dwVisibleInstanceCount;
	if (dwVisibleInstanceCount > 0)
		m_dwLastRenderedVisibleTickMS = ELTimer_GetMSec();
	m_setDX11ForceGeometryInstances.clear();

	// Native DX11 path: keep billboard rendering available in every runtime profile.
	const bool bForceGeometryLODForVisible = false;
	DWORD dwForcedGeometryInstances = 0u;
	if (bDX11AnyPassReady && (ulRenderBitVector & Forest_RenderBillboards))
	{
		itor = m_pMainTreeMap.begin();
		while (itor != m_pMainTreeMap.end())
		{
			auto pMainTree = (itor++)->second;
			auto ppInstances = pMainTree->GetInstances(uiCount);
			for (const auto& pTreeInst : ppInstances)
			{
				if (!pTreeInst || (!m_bDX11ShadowViewProjOverrideActive && !pTreeInst->isShow()))
					continue;

				bool bForceGeometryForInstance = bForceGeometryLODForVisible;
				if (!bForceGeometryForInstance)
				{
					ID3D11ShaderResourceView* pCompositeSRV = __GetTreeTextureSRV(pTreeInst.get());
					if (!pCompositeSRV || pCompositeSRV == m_pDX11SpeedTreeDefaultTextureSRV)
					{
						bForceGeometryForInstance = true;

						static DWORD s_dwBillboardSrcLogTick = 0u;
						const DWORD dwNowBillboard = ELTimer_GetMSec();
						if (0u == s_dwBillboardSrcLogTick || (dwNowBillboard - s_dwBillboardSrcLogTick) >= 5000u)
						{
							s_dwBillboardSrcLogTick = dwNowBillboard;
							CGraphicImage* pCompositeImage = pTreeInst->GetCompositeImageInstance().GetGraphicImagePointer();
							const char* c_szTree = (pCompositeImage && pCompositeImage->GetFileName()) ? pCompositeImage->GetFileName() : "unknown";
							TraceError("DX11_SPEEDTREE_BILLBOARD_SRC tree=%s composite_ok=0 fallback=keep_geo_lod", c_szTree);
						}
					}
				}

				if (!bForceGeometryForInstance)
					continue;

				CSpeedTreeRT* pSpeedTree = pTreeInst->GetSpeedTree();
				if (pSpeedTree)
					pSpeedTree->SetLodLevel(DX11RuntimeConfig::kSpeedTreeForcedLodLevel);
				m_setDX11ForceGeometryInstances.insert(pTreeInst.get());
				++dwForcedGeometryInstances;
			}
		}
	}

	static DWORD s_dwSpeedTreeLodPathLogTick = 0u;
	const DWORD dwNowLodPath = ELTimer_GetMSec();
	if (0u == s_dwSpeedTreeLodPathLogTick || (dwNowLodPath - s_dwSpeedTreeLodPathLogTick) >= 5000u)
	{
		s_dwSpeedTreeLodPathLogTick = dwNowLodPath;
		const DWORD dwBillboardEligible = (dwVisibleInstanceCount > dwForcedGeometryInstances)
			? (dwVisibleInstanceCount - dwForcedGeometryInstances)
			: 0u;
		TraceError("DX11_SPEEDTREE_LOD_PATH geo_instances=%u billboard_instances=%u",
			dwForcedGeometryInstances, dwBillboardEligible);
	}

	// Full-DX11 runtime routing (color/shadow/minimap):
	// once DX11 runtime is active and resources are ready, never execute legacy DX9 tree path.
	if (bDX11AnyPassReady)
	{
		ID3D11DeviceContext* pDX11Context = pDX11Device ? pDX11Device->GetContext() : nullptr;
		ID3D11RasterizerState* pOldRasterState = nullptr;
		ID3D11DepthStencilState* pOldDepthState = nullptr;
		UINT uOldStencilRef = 0u;
		if (pDX11Context)
		{
			pDX11Context->RSGetState(&pOldRasterState);
			pDX11Context->OMGetDepthStencilState(&pOldDepthState, &uOldStencilRef);
			// DX11 strict baseline for tree world pass: keep depth test/write enabled.
			pDX11Context->OMSetDepthStencilState(nullptr, 0u);
			if (ID3D11RasterizerState* pCullNoneState = __GetDX11SpeedTreeCullNoneState(pDX11Device->GetDevice()))
				pDX11Context->RSSetState(pCullNoneState);
		}

		if (ulRenderBitVector & Forest_RenderBranches)
			RenderBranchesDX11(ulRenderBitVector);

		if (ulRenderBitVector & Forest_RenderFronds)
			RenderFrondsDX11(ulRenderBitVector);

		if (ulRenderBitVector & Forest_RenderLeaves)
			RenderLeavesDX11(ulRenderBitVector);

#ifndef WRAPPER_NO_BILLBOARD_MODE
		if (ulRenderBitVector & Forest_RenderBillboards)
			RenderBillboardsDX11(ulRenderBitVector);
#endif

		// Iteration 2: Render grass
		// Iteration 5: Check grass resources are ready before rendering
		if (bDX11GrassReady && (ulRenderBitVector & Forest_RenderGrass))
			RenderGrassDX11(pDX11Context);

		if (pDX11Context)
			pDX11Context->RSSetState(pOldRasterState);
		if (pOldRasterState)
			pOldRasterState->Release();
		if (pDX11Context)
			pDX11Context->OMSetDepthStencilState(pOldDepthState, uOldStencilRef);
		if (pOldDepthState)
			pOldDepthState->Release();

		return;
	}

	// DX11: Native rendering pipeline - no legacy DX9 fallback
	// All rendering is handled by RenderBranchesDX11, RenderFrondsDX11,
	// RenderLeavesDX11, and RenderBillboardsDX11 methods

	// Render branches
	if (ulRenderBitVector & Forest_RenderBranches)
		RenderBranchesDX11(ulRenderBitVector);

	// Render fronds
	if (ulRenderBitVector & Forest_RenderFronds)
		RenderFrondsDX11(ulRenderBitVector);

	// Render leaves
	if (ulRenderBitVector & Forest_RenderLeaves)
		RenderLeavesDX11(ulRenderBitVector);

	// Render billboards
#ifndef WRAPPER_NO_BILLBOARD_MODE
	if (ulRenderBitVector & Forest_RenderBillboards)
		RenderBillboardsDX11(ulRenderBitVector);
#endif
}


bool CSpeedTreeForestDirectX::EmitDX11SubmitProbeDraw()
{
	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice || !pGrpDevice->IsValid())
		return false;

	if (!IsDX11SpeedTreeResourcesReady())
		return false;

	ID3D11DeviceContext* pContext = pGrpDevice->GetContext();
	if (!pContext || !m_pDX11SpeedTreeDynamicVB || !m_pDX11SpeedTreeConstantBuffer)
		return false;

	struct SBillboardVertexDX11
	{
		float x, y, z;
		float u, v;
	};
	struct SBillboardConstants
	{
		DirectX::SimpleMath::Matrix matViewProj;
		DirectX::SimpleMath::Vector4 vTreePosAndRotation;
		DirectX::SimpleMath::Vector4 vLightDir;
	};

	const DirectX::SimpleMath::Matrix& matView = CGraphicBase::GetViewMatrix();
	const DirectX::SimpleMath::Matrix& matProj = CGraphicBase::GetProjMatrix();
	DirectX::SimpleMath::Matrix matViewProj = matView * matProj;
	DirectX::SimpleMath::Matrix matViewProjShader;
	matViewProjShader = matViewProj.Transpose();

	SBillboardConstants kConstants;
	ZeroMemory(&kConstants, sizeof(kConstants));
	kConstants.matViewProj = matViewProjShader;
	kConstants.vTreePosAndRotation = DirectX::SimpleMath::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	kConstants.vLightDir = DirectX::SimpleMath::Vector4(m_afLighting[0], m_afLighting[1], m_afLighting[2], 0.0f);
	pContext->UpdateSubresource(m_pDX11SpeedTreeConstantBuffer, 0, nullptr, &kConstants, 0, 0);

	SBillboardVertexDX11 akVerts[3] =
	{
		{ 2.0f, 2.0f, 0.0f, 0.0f, 0.0f },
		{ 2.2f, 2.0f, 0.0f, 0.0f, 0.0f },
		{ 2.0f, 2.2f, 0.0f, 0.0f, 0.0f }
	};

	D3D11_MAPPED_SUBRESOURCE kMapped;
	ZeroMemory(&kMapped, sizeof(kMapped));
	HRESULT hrMap = pContext->Map(m_pDX11SpeedTreeDynamicVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMapped);
	if (FAILED(hrMap) || !kMapped.pData)
		return false;

	memcpy(kMapped.pData, akVerts, sizeof(akVerts));
	pContext->Unmap(m_pDX11SpeedTreeDynamicVB, 0);

	pContext->IASetInputLayout(m_pDX11SpeedTreeBillboardInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(m_pDX11SpeedTreeBillboardVS, nullptr, 0);
	if (m_bDX11ShadowViewProjOverrideActive && m_pDX11SpeedTreeShadowAlphaPS)
		pContext->PSSetShader(m_pDX11SpeedTreeShadowAlphaPS, nullptr, 0);
	else
		pContext->PSSetShader(m_pDX11SpeedTreeBillboardPS, nullptr, 0);
	pContext->VSSetConstantBuffers(0, 1, &m_pDX11SpeedTreeConstantBuffer);
	pContext->PSSetSamplers(0, 1, &m_pDX11SpeedTreeSamplerState);
	UpdateDX11SpeedTreeAlphaRef(pContext, m_pDX11SpeedTreeAlphaRefBuffer, 0.0f);

	ID3D11ShaderResourceView* pTreeSRV = m_pDX11SpeedTreeDefaultTextureSRV;
	pContext->PSSetShaderResources(0, 1, &pTreeSRV);

	const float afBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	pContext->OMSetBlendState(m_pDX11SpeedTreeBlendState, afBlendFactor, 0xffffffffu);

	UINT uStride = sizeof(SBillboardVertexDX11);
	UINT uOffset = 0;
	pContext->IASetVertexBuffers(0, 1, &m_pDX11SpeedTreeDynamicVB, &uStride, &uOffset);
	pContext->Draw(3, 0);
	pGrpDevice->IncrementFrameDrawCalls(1u, 1u);

	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);
	ID3D11Buffer* pNullCB = nullptr;
	pContext->PSSetConstantBuffers(1, 1, &pNullCB);
	pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);
	return true;
}

void CSpeedTreeForestDirectX::TrackDX11SubmitFromVisiblePass(DWORD dwVisiblePassCount)
{
	if (0u == dwVisiblePassCount)
		return;

	// PERF/DEBUGLAYER: avoid synthetic probe draws in normal visible pass.
	// Real geometry draws already happened, so only update telemetry counter.
	AddDX11SubmittedInstanceCount(dwVisiblePassCount);
}

void CSpeedTreeForestDirectX::ResetDX11SubmittedInstanceCount()
{
	m_dwLastDX11SubmittedInstanceCount = 0;
}

void CSpeedTreeForestDirectX::AddDX11SubmittedInstanceCount(DWORD dwCount, UINT64 ullPrimitiveCount)
{
	if (0 == dwCount)
		return;

	if (m_dwLastDX11SubmittedInstanceCount > 0xffffffffu - dwCount)
		m_dwLastDX11SubmittedInstanceCount = 0xffffffffu;
	else
		m_dwLastDX11SubmittedInstanceCount += dwCount;

	// Report to ImGui metrics system (Map/Tree draw calls)
#ifdef BUILD_DEBUG_UI
	extern void ReportImGuiMapTreeDrawCalls(UINT32 draws, UINT64 prims);
	ReportImGuiMapTreeDrawCalls(dwCount, ullPrimitiveCount);
#endif
}

bool CSpeedTreeForestDirectX::IsDX11SpeedTreeResourcesReady() const
{
	return m_bDX11SpeedTreeResourcesReady;
}

bool CSpeedTreeForestDirectX::RenderToShadowMapDX11(ID3D11DeviceContext* pContext, const DirectX::SimpleMath::Matrix& c_rmatLightViewProj)
{
	if (!pContext || !IsDX11SpeedTreeResourcesReady())
		return false;

	m_bDX11ShadowViewProjOverrideActive = true;
	m_matDX11ShadowViewProjOverride = c_rmatLightViewProj;
	// Full shadow path: keep geometry casters active in every runtime profile.
	RenderBranchesDX11(Forest_RenderBranches | Forest_RenderToShadow);
	RenderFrondsDX11(Forest_RenderFronds | Forest_RenderToShadow);
	RenderLeavesDX11(Forest_RenderLeaves | Forest_RenderToShadow);

	// Keep billboards for backward compatibility / LOD fallback
	RenderBillboardsDX11(Forest_RenderBillboards | Forest_RenderToShadow);

	// Render grass shadows if enabled and available
	if (m_bDX11GrassResourcesReady && DX11RuntimeConfig::kGrassEnableShadows)
	{
		RenderGrassDX11(pContext);
	}

	m_bDX11ShadowViewProjOverrideActive = false;
	return true;
}

// ============================================================================
// W3.2/W3.3 SpeedTree DX11 Branch/Frond/Leaf Shadow Rendering Infrastructure
// ============================================================================

// Branch/Frond Vertex Shader (same format: position, color, texcoord)
static const char* s_szDX11SpeedTreeBranchVS = R"(
cbuffer cbPerTree : register(b0)
{
	matrix g_matViewProj;
	float4 g_vTreePos;
	float4 g_vLightDir;
};

struct VS_INPUT
{
	float3 vPosition : POSITION;
	float4 vColor    : COLOR0;
	float2 vTexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
	float  fLighting : TEXCOORD1;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;

	// Translate to world position
	float3 vWorldPos = input.vPosition + g_vTreePos.xyz;

	// Transform to clip space
	output.vPosition = mul(float4(vWorldPos, 1.0f), g_matViewProj);
	output.vTexCoord = input.vTexCoord;

	// Use pre-baked lighting from legacy vertex color with a small ambient floor.
	output.fLighting = max(input.vColor.r, 0.35f);

	return output;
}
)";

// Branch/Frond/Leaf Pixel Shader (shared, handles alpha testing)
static const char* s_szDX11SpeedTreeBranchPS = R"(
Texture2D g_txComposite : register(t0);
SamplerState g_sampler : register(s0);
cbuffer cbAlphaClip : register(b1)
{
	float g_fAlphaRef;
	float3 g_paddingAlphaRef;
};

struct PS_INPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
	float  fLighting : TEXCOORD1;
};

float4 main(PS_INPUT input) : SV_Target
{
	float4 vColor = g_txComposite.Sample(g_sampler, input.vTexCoord);

	// Legacy parity: alpha reference is driven by per-part GRP_RS_ALPHAREF equivalent.
	if (vColor.a < g_fAlphaRef)
		discard;

	// Apply lighting (keep minimum ambient so foliage cannot collapse to near-black)
	vColor.rgb *= saturate(input.fLighting);

	return vColor;
}
)";

// Branch color pass parity: bark texture can have non-useful alpha channel, so avoid alpha clip here.
static const char* s_szDX11SpeedTreeBranchOpaquePS = R"(
Texture2D g_txComposite : register(t0);
SamplerState g_sampler : register(s0);

struct PS_INPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
	float  fLighting : TEXCOORD1;
};

float4 main(PS_INPUT input) : SV_Target
{
	float4 vColor = g_txComposite.Sample(g_sampler, input.vTexCoord);
	vColor.rgb *= saturate(input.fLighting);
	return vColor;
}
)";

// Shadow caster alpha-clip PS (no color output, depth-only pass friendly)
static const char* s_szDX11SpeedTreeShadowAlphaPS = R"(
Texture2D g_txComposite : register(t0);
SamplerState g_sampler : register(s0);
cbuffer cbAlphaClip : register(b1)
{
	float g_fAlphaRef;
	float3 g_paddingAlphaRef;
};

struct PS_INPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
	float  fLighting : TEXCOORD1;
};

void main(PS_INPUT input)
{
	float fAlpha = g_txComposite.Sample(g_sampler, input.vTexCoord).a;
	clip(fAlpha - g_fAlphaRef);
}
)";

// Leaf Vertex Shader (simpler format: position, color, texcoord)
static const char* s_szDX11SpeedTreeLeafVS = R"(
cbuffer cbPerTree : register(b0)
{
	matrix g_matViewProj;
	float4 g_vTreePos;
	float4 g_vLightDir;
};
cbuffer cbLeafPlacement : register(b2)
{
	float4 g_vLeafTable[1024];
};

struct VS_INPUT
{
	float3 vPosition : POSITION;
	float4 vColor    : COLOR0;
	float2 vTexCoord : TEXCOORD0;
	float2 vLeafPlacement : TEXCOORD1; // x = placement index, y = scalar
};

struct VS_OUTPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
	float  fLighting : TEXCOORD1;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;

	// DX9 parity: leaf cards are expanded from center using placement table.
	int iPlacementIndex = (int)input.vLeafPlacement.x;
	iPlacementIndex = clamp(iPlacementIndex, 0, 1023);
	float fLeafScalar = input.vLeafPlacement.y;
	float3 vLeafOffset = g_vLeafTable[iPlacementIndex].xyz * fLeafScalar;
	float3 vWorldPos = input.vPosition + vLeafOffset + g_vTreePos.xyz;

	// Transform to clip space
	output.vPosition = mul(float4(vWorldPos, 1.0f), g_matViewProj);
	output.vTexCoord = input.vTexCoord;

	// Use pre-baked lighting from legacy vertex color with a small ambient floor.
	output.fLighting = max(input.vColor.r, 0.35f);

	return output;
}
)";

// W3 SpeedTree DX11 Billboard Rendering Infrastructure
// ============================================================================

// Inline HLSL Billboard Shaders
static const char* s_szDX11SpeedTreeBillboardVS = R"(
cbuffer cbPerBillboard : register(b0)
{
	matrix g_matViewProj;
	float4 g_vTreePosAndRotation;  // xyz = position, w = rotation angle
	float4 g_vLightDir;
};

struct VS_INPUT
{
	float3 vPosition : POSITION;
	float2 vTexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
	float  fLighting : TEXCOORD1;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;

	// Apply billboard rotation around Y axis
	float fCos = cos(g_vTreePosAndRotation.w);
	float fSin = sin(g_vTreePosAndRotation.w);
	float3 vRotated;
	vRotated.x = input.vPosition.x * fCos - input.vPosition.z * fSin;
	vRotated.y = input.vPosition.y;
	vRotated.z = input.vPosition.x * fSin + input.vPosition.z * fCos;

	// Translate to tree position
	float3 vWorldPos = vRotated + g_vTreePosAndRotation.xyz;

	// Transform to clip space
	output.vPosition = mul(float4(vWorldPos, 1.0f), g_matViewProj);
	output.vTexCoord = input.vTexCoord;

	// Simple directional lighting
	output.fLighting = saturate(dot(float3(0.0f, 1.0f, 0.0f), g_vLightDir.xyz)) * 0.5f + 0.5f;

	return output;
}
)";

static const char* s_szDX11SpeedTreeBillboardPS = R"(
Texture2D g_txComposite : register(t0);
SamplerState g_sampler : register(s0);
cbuffer cbAlphaClip : register(b1)
{
	float g_fAlphaRef;
	float3 g_paddingAlphaRef;
};

struct PS_INPUT
{
	float4 vPosition : SV_POSITION;
	float2 vTexCoord : TEXCOORD0;
	float  fLighting : TEXCOORD1;
};

float4 main(PS_INPUT input) : SV_Target
{
	float4 vColor = g_txComposite.Sample(g_sampler, input.vTexCoord);
	if (vColor.a < g_fAlphaRef)
		discard;
	vColor.rgb *= input.fLighting;
	return vColor;
}
)";

bool CSpeedTreeForestDirectX::InitializeDX11SpeedTreeResources(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (m_bDX11SpeedTreeResourcesReady)
		return true;

	HRESULT hr;

	// Compile vertex shader
	ID3DBlob* pVSBlob = nullptr;
	ID3DBlob* pErrorBlob = nullptr;
	hr = D3DCompile(
		s_szDX11SpeedTreeBillboardVS,
		strlen(s_szDX11SpeedTreeBillboardVS),
		"SpeedTreeBillboardVS",
		nullptr,
		nullptr,
		"main",
		"vs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pVSBlob,
		&pErrorBlob);

	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			TraceError("DX11_SPEEDTREE_SHADER_COMPILE_ERROR VS: %s", (char*)pErrorBlob->GetBufferPointer());
			pErrorBlob->Release();
		}
		return false;
	}

	hr = pDevice->CreateVertexShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11SpeedTreeBillboardVS);

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_VS_FAILED hr=0x%08X", hr);
		pVSBlob->Release();
		return false;
	}

	// Create input layout (position + texcoord, same as effects)
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	hr = pDevice->CreateInputLayout(
		layout,
		ARRAYSIZE(layout),
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		&m_pDX11SpeedTreeBillboardInputLayout);

	pVSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_INPUT_LAYOUT_FAILED hr=0x%08X", hr);
		return false;
	}

	// Compile pixel shader
	hr = D3DCompile(
		s_szDX11SpeedTreeBillboardPS,
		strlen(s_szDX11SpeedTreeBillboardPS),
		"SpeedTreeBillboardPS",
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pVSBlob,
		&pErrorBlob);

	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			TraceError("DX11_SPEEDTREE_SHADER_COMPILE_ERROR PS: %s", (char*)pErrorBlob->GetBufferPointer());
			pErrorBlob->Release();
		}
		return false;
	}

	hr = pDevice->CreatePixelShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11SpeedTreeBillboardPS);

	pVSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_PS_FAILED hr=0x%08X", hr);
		return false;
	}

	// W3.2: Compile branch vertex shader
	hr = D3DCompile(
		s_szDX11SpeedTreeBranchVS,
		strlen(s_szDX11SpeedTreeBranchVS),
		"SpeedTreeBranchVS",
		nullptr,
		nullptr,
		"main",
		"vs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pVSBlob,
		&pErrorBlob);

	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			TraceError("DX11_SPEEDTREE_BRANCH_VS_COMPILE_ERROR: %s", (char*)pErrorBlob->GetBufferPointer());
			pErrorBlob->Release();
		}
		return false;
	}

	hr = pDevice->CreateVertexShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11SpeedTreeBranchVS);

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_BRANCH_VS_FAILED hr=0x%08X", hr);
		pVSBlob->Release();
		return false;
	}

	// Create branch input layout (position + color + texcoord)
	D3D11_INPUT_ELEMENT_DESC branchLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	hr = pDevice->CreateInputLayout(
		branchLayout,
		ARRAYSIZE(branchLayout),
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		&m_pDX11SpeedTreeBranchInputLayout);

	pVSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_BRANCH_INPUT_LAYOUT_FAILED hr=0x%08X", hr);
		return false;
	}

	// W3.2: Compile branch pixel shader
	hr = D3DCompile(
		s_szDX11SpeedTreeBranchPS,
		strlen(s_szDX11SpeedTreeBranchPS),
		"SpeedTreeBranchPS",
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pVSBlob,
		&pErrorBlob);

	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			TraceError("DX11_SPEEDTREE_BRANCH_PS_COMPILE_ERROR: %s", (char*)pErrorBlob->GetBufferPointer());
			pErrorBlob->Release();
		}
		return false;
	}

	hr = pDevice->CreatePixelShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11SpeedTreeBranchPS);

	pVSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_BRANCH_PS_FAILED hr=0x%08X", hr);
		return false;
	}

	// Compile branch opaque PS (color pass parity: no alpha clip on bark)
	hr = D3DCompile(
		s_szDX11SpeedTreeBranchOpaquePS,
		strlen(s_szDX11SpeedTreeBranchOpaquePS),
		"SpeedTreeBranchOpaquePS",
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pVSBlob,
		&pErrorBlob);

	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			TraceError("DX11_SPEEDTREE_BRANCH_OPAQUE_PS_COMPILE_ERROR: %s", (char*)pErrorBlob->GetBufferPointer());
			pErrorBlob->Release();
		}
		return false;
	}

	hr = pDevice->CreatePixelShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11SpeedTreeBranchOpaquePS);

	pVSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_BRANCH_OPAQUE_PS_FAILED hr=0x%08X", hr);
		return false;
	}

	// Compile shadow alpha-clip PS (no SV_Target to avoid RTV warnings in depth-only shadow pass)
	hr = D3DCompile(
		s_szDX11SpeedTreeShadowAlphaPS,
		strlen(s_szDX11SpeedTreeShadowAlphaPS),
		"SpeedTreeShadowAlphaPS",
		nullptr,
		nullptr,
		"main",
		"ps_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pVSBlob,
		&pErrorBlob);

	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			TraceError("DX11_SPEEDTREE_SHADOW_ALPHA_PS_COMPILE_ERROR: %s", (char*)pErrorBlob->GetBufferPointer());
			pErrorBlob->Release();
		}
		return false;
	}

	hr = pDevice->CreatePixelShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11SpeedTreeShadowAlphaPS);

	pVSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_SHADOW_ALPHA_PS_FAILED hr=0x%08X", hr);
		return false;
	}

	// W3.3: Compile leaf vertex shader
	hr = D3DCompile(
		s_szDX11SpeedTreeLeafVS,
		strlen(s_szDX11SpeedTreeLeafVS),
		"SpeedTreeLeafVS",
		nullptr,
		nullptr,
		"main",
		"vs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pVSBlob,
		&pErrorBlob);

	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			TraceError("DX11_SPEEDTREE_LEAF_VS_COMPILE_ERROR: %s", (char*)pErrorBlob->GetBufferPointer());
			pErrorBlob->Release();
		}
		return false;
	}

	hr = pDevice->CreateVertexShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		nullptr,
		&m_pDX11SpeedTreeLeafVS);

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_LEAF_VS_FAILED hr=0x%08X", hr);
		pVSBlob->Release();
		return false;
	}

	// Create leaf input layout (position + color + texcoord + placement data)
	D3D11_INPUT_ELEMENT_DESC leafLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	hr = pDevice->CreateInputLayout(
		leafLayout,
		ARRAYSIZE(leafLayout),
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		&m_pDX11SpeedTreeLeafInputLayout);

	pVSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_LEAF_INPUT_LAYOUT_FAILED hr=0x%08X", hr);
		return false;
	}

	// Create constant buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.Usage = D3D11_USAGE_DEFAULT;
	cbDesc.ByteWidth = sizeof(DirectX::SimpleMath::Matrix) + sizeof(DirectX::SimpleMath::Vector4) * 2;  // ViewProj + TreePosRotation + LightDir
	cbDesc.ByteWidth = (cbDesc.ByteWidth + 15) & ~15;  // 16-byte align
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	hr = pDevice->CreateBuffer(&cbDesc, nullptr, &m_pDX11SpeedTreeConstantBuffer);
	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_CB_FAILED hr=0x%08X", hr);
		return false;
	}

	D3D11_BUFFER_DESC alphaCBDesc = {};
	alphaCBDesc.Usage = D3D11_USAGE_DEFAULT;
	alphaCBDesc.ByteWidth = sizeof(SSpeedTreeAlphaClipCB);
	alphaCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hr = pDevice->CreateBuffer(&alphaCBDesc, nullptr, &m_pDX11SpeedTreeAlphaRefBuffer);
	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_ALPHA_CB_FAILED hr=0x%08X", hr);
		return false;
	}

	D3D11_BUFFER_DESC leafPlacementCBDesc = {};
	leafPlacementCBDesc.Usage = D3D11_USAGE_DEFAULT;
	leafPlacementCBDesc.ByteWidth = sizeof(SSpeedTreeLeafPlacementCB);
	leafPlacementCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hr = pDevice->CreateBuffer(&leafPlacementCBDesc, nullptr, &m_pDX11SpeedTreeLeafPlacementBuffer);
	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_LEAF_TABLE_CB_FAILED hr=0x%08X", hr);
		return false;
	}

	// Create sampler state
	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0.0f;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

	hr = pDevice->CreateSamplerState(&sampDesc, &m_pDX11SpeedTreeSamplerState);
	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_SAMPLER_FAILED hr=0x%08X", hr);
		return false;
	}
	TraceError("DX11_SPEEDTREE_SAMPLER_POLICY mode=mip_chain");

	// Create default white texture (used until tree texture conversion path is finalized)
	unsigned char aWhitePixel[4] = { 255, 255, 255, 255 };
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = 1;
	texDesc.Height = 1;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA texInit = {};
	texInit.pSysMem = aWhitePixel;
	texInit.SysMemPitch = 4;

	ID3D11Texture2D* pDefaultTexture = nullptr;
	hr = pDevice->CreateTexture2D(&texDesc, &texInit, &pDefaultTexture);
	if (FAILED(hr) || !pDefaultTexture)
	{
		TraceError("DX11_SPEEDTREE_CREATE_DEFAULT_TEXTURE_FAILED hr=0x%08X", hr);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	hr = pDevice->CreateShaderResourceView(pDefaultTexture, &srvDesc, &m_pDX11SpeedTreeDefaultTextureSRV);
	pDefaultTexture->Release();
	if (FAILED(hr) || !m_pDX11SpeedTreeDefaultTextureSRV)
	{
		TraceError("DX11_SPEEDTREE_CREATE_DEFAULT_SRV_FAILED hr=0x%08X", hr);
		return false;
	}

	// Create alpha blend state
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	hr = pDevice->CreateBlendState(&blendDesc, &m_pDX11SpeedTreeBlendState);
	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_BLEND_FAILED hr=0x%08X", hr);
		return false;
	}

	// Alpha-to-coverage state for foliage quality (leaf edge anti-aliasing on MSAA targets).
	// Blend itself stays disabled (DX9-like alpha-test behavior), coverage comes from alpha.
	D3D11_BLEND_DESC a2cBlendDesc = {};
	a2cBlendDesc.AlphaToCoverageEnable = TRUE;
	a2cBlendDesc.RenderTarget[0].BlendEnable = FALSE;
	a2cBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	hr = pDevice->CreateBlendState(&a2cBlendDesc, &m_pDX11SpeedTreeAlphaToCoverageBlendState);
	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_A2C_BLEND_FAILED hr=0x%08X", hr);
		// Non-fatal: keep foliage rendering with default no-blend state.
		m_pDX11SpeedTreeAlphaToCoverageBlendState = nullptr;
	}

	// Create dynamic VB for billboard vertices
	D3D11_BUFFER_DESC vbDesc = {};
	vbDesc.Usage = D3D11_USAGE_DYNAMIC;
	vbDesc.ByteWidth = sizeof(float) * 5 * 4 * 256;  // 5 floats (pos+uv) * 4 verts * 256 billboards max
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = pDevice->CreateBuffer(&vbDesc, nullptr, &m_pDX11SpeedTreeDynamicVB);
	if (FAILED(hr))
	{
		TraceError("DX11_SPEEDTREE_CREATE_DYNAMIC_VB_FAILED hr=0x%08X", hr);
		return false;
	}

	m_bDX11SpeedTreeResourcesReady = true;
	TraceError("DX11_SPEEDTREE_RESOURCES_INITIALIZED success");
	return true;
}

void CSpeedTreeForestDirectX::DestroyDX11SpeedTreeResources()
{
	if (m_pDX11SpeedTreeDynamicVB)
	{
		m_pDX11SpeedTreeDynamicVB->Release();
		m_pDX11SpeedTreeDynamicVB = nullptr;
	}

	if (m_pDX11SpeedTreeDefaultTextureSRV)
	{
		m_pDX11SpeedTreeDefaultTextureSRV->Release();
		m_pDX11SpeedTreeDefaultTextureSRV = nullptr;
	}

	if (m_pDX11SpeedTreeBlendState)
	{
		m_pDX11SpeedTreeBlendState->Release();
		m_pDX11SpeedTreeBlendState = nullptr;
	}

	if (m_pDX11SpeedTreeAlphaToCoverageBlendState)
	{
		m_pDX11SpeedTreeAlphaToCoverageBlendState->Release();
		m_pDX11SpeedTreeAlphaToCoverageBlendState = nullptr;
	}

	if (m_pDX11SpeedTreeSamplerState)
	{
		m_pDX11SpeedTreeSamplerState->Release();
		m_pDX11SpeedTreeSamplerState = nullptr;
	}

	if (m_pDX11SpeedTreeConstantBuffer)
	{
		m_pDX11SpeedTreeConstantBuffer->Release();
		m_pDX11SpeedTreeConstantBuffer = nullptr;
	}

	if (m_pDX11SpeedTreeAlphaRefBuffer)
	{
		m_pDX11SpeedTreeAlphaRefBuffer->Release();
		m_pDX11SpeedTreeAlphaRefBuffer = nullptr;
	}

	if (m_pDX11SpeedTreeLeafPlacementBuffer)
	{
		m_pDX11SpeedTreeLeafPlacementBuffer->Release();
		m_pDX11SpeedTreeLeafPlacementBuffer = nullptr;
	}

	if (m_pDX11SpeedTreeBillboardInputLayout)
	{
		m_pDX11SpeedTreeBillboardInputLayout->Release();
		m_pDX11SpeedTreeBillboardInputLayout = nullptr;
	}

	if (m_pDX11SpeedTreeBillboardPS)
	{
		m_pDX11SpeedTreeBillboardPS->Release();
		m_pDX11SpeedTreeBillboardPS = nullptr;
	}

	if (m_pDX11SpeedTreeBillboardVS)
	{
		m_pDX11SpeedTreeBillboardVS->Release();
		m_pDX11SpeedTreeBillboardVS = nullptr;
	}

	// W3.2/W3.3: Release branch/frond/leaf resources
	if (m_pDX11SpeedTreeLeafInputLayout)
	{
		m_pDX11SpeedTreeLeafInputLayout->Release();
		m_pDX11SpeedTreeLeafInputLayout = nullptr;
	}

	if (m_pDX11SpeedTreeLeafVS)
	{
		m_pDX11SpeedTreeLeafVS->Release();
		m_pDX11SpeedTreeLeafVS = nullptr;
	}

	if (m_pDX11SpeedTreeBranchInputLayout)
	{
		m_pDX11SpeedTreeBranchInputLayout->Release();
		m_pDX11SpeedTreeBranchInputLayout = nullptr;
	}

	if (m_pDX11SpeedTreeBranchPS)
	{
		m_pDX11SpeedTreeBranchPS->Release();
		m_pDX11SpeedTreeBranchPS = nullptr;
	}

	if (m_pDX11SpeedTreeBranchOpaquePS)
	{
		m_pDX11SpeedTreeBranchOpaquePS->Release();
		m_pDX11SpeedTreeBranchOpaquePS = nullptr;
	}

	if (m_pDX11SpeedTreeShadowAlphaPS)
	{
		m_pDX11SpeedTreeShadowAlphaPS->Release();
		m_pDX11SpeedTreeShadowAlphaPS = nullptr;
	}

	if (m_pDX11SpeedTreeBranchVS)
	{
		m_pDX11SpeedTreeBranchVS->Release();
		m_pDX11SpeedTreeBranchVS = nullptr;
	}

	// W4.3: Release cached per-tree-type geometry buffers
	for (auto& pair : s_mapBranchVB)
	{
		if (pair.second)
			pair.second->Release();
	}
	s_mapBranchVB.clear();

	for (auto& pair : s_mapBranchIB)
	{
		if (pair.second)
			pair.second->Release();
	}
	s_mapBranchIB.clear();
	s_mapBranchStrips.clear();
	s_mapBranchIndexCount.clear();
	s_mapBranchCacheSig.clear();

	for (auto& pair : s_mapFrondVB)
	{
		if (pair.second)
			pair.second->Release();
	}
	s_mapFrondVB.clear();

	for (auto& pair : s_mapFrondIB)
	{
		if (pair.second)
			pair.second->Release();
	}
	s_mapFrondIB.clear();
	s_mapFrondStrips.clear();
	s_mapFrondIndexCount.clear();
	s_mapFrondCacheSig.clear();

	for (auto& pair : s_mapLeafVB)
	{
		if (pair.second)
			pair.second->Release();
	}
	s_mapLeafVB.clear();
	s_mapLeafStrips.clear();
	s_mapLeafCacheSig.clear();
	m_setDX11ForceGeometryInstances.clear();

	m_bDX11SpeedTreeResourcesReady = false;
}


///////////////////////////////////////////////////////////////////////
//	Iteration 1: DX11 Grass Rendering Infrastructure

bool CSpeedTreeForestDirectX::InitializeDX11GrassResources(ID3D11Device* pDevice)
{
	if (!pDevice)
		return false;

	if (m_bDX11GrassResourcesReady)
		return true;  // Already initialized

	// Compile grass vertex shader
	ID3DBlob* pVSBlob = nullptr;
	HRESULT hr = D3DCompile(
		GrassShadersHLSL,
		strlen(GrassShadersHLSL),
		nullptr,
		nullptr,
		nullptr,
		"main",
		"vs_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pVSBlob,
		nullptr);

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Vertex shader compilation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	hr = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(), nullptr, &m_pDX11GrassVS);

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Vertex shader creation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		pVSBlob->Release();
		return false;
	}

	// Create input layout
	hr = pDevice->CreateInputLayout(GrassInputLayout, GrassInputLayoutElements,
		pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(),
		&m_pDX11GrassInputLayout);

	pVSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Input layout creation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	// Compile grass pixel shader
	ID3DBlob* pPSBlob = nullptr;

	// Add preprocessor define for texture sampling
	D3D_SHADER_MACRO shaderMacros[] =
	{
		{ "GRASS_USE_TEXTURE", "1" },
		{ nullptr, nullptr }
	};

	hr = D3DCompile(
		GrassShadersHLSL,
		strlen(GrassShadersHLSL),
		nullptr,
		shaderMacros,
		nullptr,
		"mainPS",
		"ps_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pPSBlob,
		nullptr);

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Pixel shader compilation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(),
		pPSBlob->GetBufferSize(), nullptr, &m_pDX11GrassPS);

	pPSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Pixel shader creation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	// Compile grass shadow pixel shader (for shadow map rendering)
	ID3DBlob* pShadowPSBlob = nullptr;
	hr = D3DCompile(
		GrassShadersHLSL,
		strlen(GrassShadersHLSL),
		nullptr,
		shaderMacros,  // Use same macros as main PS
		nullptr,
		"mainShadowPS",
		"ps_5_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pShadowPSBlob,
		nullptr);

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Shadow pixel shader compilation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	hr = pDevice->CreatePixelShader(pShadowPSBlob->GetBufferPointer(),
		pShadowPSBlob->GetBufferSize(), nullptr, &m_pDX11GrassShadowAlphaPS);

	pShadowPSBlob->Release();

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Shadow pixel shader creation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	// Create constant buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(GrassConstantBuffer);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDesc.MiscFlags = 0;
	cbDesc.StructureByteStride = 0;

	hr = pDevice->CreateBuffer(&cbDesc, nullptr, &m_pDX11GrassConstantBuffer);

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Constant buffer creation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	// Create alpha reference constant buffer for texture-based alpha testing
	D3D11_BUFFER_DESC alphaDesc = {};
	alphaDesc.ByteWidth = sizeof(float) * 4;  // 16-byte aligned (float + 3 float padding)
	alphaDesc.Usage = D3D11_USAGE_DYNAMIC;
	alphaDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	alphaDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	alphaDesc.MiscFlags = 0;
	alphaDesc.StructureByteStride = 0;

	hr = pDevice->CreateBuffer(&alphaDesc, nullptr, &m_pDX11GrassAlphaRefBuffer);
	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Alpha buffer creation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	// Create vertex buffer (empty for now, will be populated in Iteration 2)
	D3D11_BUFFER_DESC vbDesc = {};
	vbDesc.ByteWidth = 4 * sizeof(GrassVertex);  // Reserve space for 1 quad
	vbDesc.Usage = D3D11_USAGE_DYNAMIC;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbDesc.MiscFlags = 0;

	hr = pDevice->CreateBuffer(&vbDesc, nullptr, &m_pDX11GrassVertexBuffer);

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Vertex buffer creation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	// Create sampler state
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.BorderColor[0] = 0.0f;
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 0.0f;
	samplerDesc.BorderColor[3] = 0.0f;
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	hr = pDevice->CreateSamplerState(&samplerDesc, &m_pDX11GrassSamplerState);

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Sampler state creation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	// Create blend state (alpha blending)
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	hr = pDevice->CreateBlendState(&blendDesc, &m_pDX11GrassBlendState);

	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_INIT_FAIL: Blend state creation failed hr=0x%08X",
			static_cast<unsigned int>(hr));
		return false;
	}

	m_uiDX11GrassVertexCount = 0;  // Will be set in Iteration 2
	m_bDX11GrassResourcesReady = true;

	TraceError("DX11_GRASS_INIT_SUCCESS: Grass resources initialized successfully");
	return true;
}

void CSpeedTreeForestDirectX::DestroyDX11GrassResources()
{
	if (m_pDX11GrassVS)
	{
		m_pDX11GrassVS->Release();
		m_pDX11GrassVS = nullptr;
	}

	if (m_pDX11GrassPS)
	{
		m_pDX11GrassPS->Release();
		m_pDX11GrassPS = nullptr;
	}

	if (m_pDX11GrassShadowAlphaPS)
	{
		m_pDX11GrassShadowAlphaPS->Release();
		m_pDX11GrassShadowAlphaPS = nullptr;
	}

	if (m_pDX11GrassInputLayout)
	{
		m_pDX11GrassInputLayout->Release();
		m_pDX11GrassInputLayout = nullptr;
	}

	if (m_pDX11GrassConstantBuffer)
	{
		m_pDX11GrassConstantBuffer->Release();
		m_pDX11GrassConstantBuffer = nullptr;
	}

	if (m_pDX11GrassAlphaRefBuffer)
	{
		m_pDX11GrassAlphaRefBuffer->Release();
		m_pDX11GrassAlphaRefBuffer = nullptr;
	}

	if (m_pDX11GrassVertexBuffer)
	{
		m_pDX11GrassVertexBuffer->Release();
		m_pDX11GrassVertexBuffer = nullptr;
	}

	if (m_pDX11GrassSamplerState)
	{
		m_pDX11GrassSamplerState->Release();
		m_pDX11GrassSamplerState = nullptr;
	}

	if (m_pDX11GrassBlendState)
	{
		m_pDX11GrassBlendState->Release();
		m_pDX11GrassBlendState = nullptr;
	}

	if (m_pDX11GrassTextureSRV)
	{
		m_pDX11GrassTextureSRV->Release();
		m_pDX11GrassTextureSRV = nullptr;
	}

	m_uiDX11GrassVertexCount = 0;
	m_bDX11GrassResourcesReady = false;
}

bool CSpeedTreeForestDirectX::IsDX11GrassResourcesReady() const
{
	return m_bDX11GrassResourcesReady;
}


///////////////////////////////////////////////////////////////////////
//	Iteration 2: Grass Wrapper Management

void CSpeedTreeForestDirectX::SetGrassWrapper(CSpeedGrassWrapper* pGrassWrapper)
{
	if (m_pGrassWrapper == pGrassWrapper)
		return;

	if (m_pGrassWrapper)
		delete m_pGrassWrapper;

	m_pGrassWrapper = pGrassWrapper;
	if (m_pGrassWrapper)
		m_pGrassWrapper->SetMapOutdoor(m_pGrassMapOutdoor);

	m_uiDX11GrassVertexCount = 0u;
	m_uiDX11GrassBladeCount = 0u;
	m_uiDX11GrassRegionCount = 0u;
}

CSpeedGrassWrapper* CSpeedTreeForestDirectX::GetGrassWrapper() const
{
	return m_pGrassWrapper;
}

void CSpeedTreeForestDirectX::SetGrassMapOutdoor(CMapOutdoor* pMapOutdoor)
{
	m_pGrassMapOutdoor = pMapOutdoor;

	if (!m_pGrassWrapper && m_pGrassMapOutdoor)
	{
		m_pGrassWrapper = new CSpeedGrassWrapper();
	}

	if (m_pGrassWrapper)
		m_pGrassWrapper->SetMapOutdoor(m_pGrassMapOutdoor);

	// Force geometry regeneration after map bind/unbind.
	m_uiDX11GrassVertexCount = 0u;
	m_uiDX11GrassBladeCount = 0u;
	m_uiDX11GrassRegionCount = 0u;
}


///////////////////////////////////////////////////////////////////////
//	Iteration 5: Grass LOD Configuration and Statistics

void CSpeedTreeForestDirectX::SetGrassLodDistances(float fNearDistance, float fFarDistance)
{
	if (fNearDistance > 0.0f && fFarDistance > fNearDistance)
	{
		m_fGrassLodNearDistance = fNearDistance;
		m_fGrassLodFarDistance = fFarDistance;

		// Force grass geometry regeneration on next render with new LOD distances
		m_uiDX11GrassVertexCount = 0;
	}
}

void CSpeedTreeForestDirectX::GetGrassLodDistances(float& fNearDistance, float& fFarDistance) const
{
	fNearDistance = m_fGrassLodNearDistance;
	fFarDistance = m_fGrassLodFarDistance;
}

void CSpeedTreeForestDirectX::GetGrassStatistics(UINT& uiTotalBlades, UINT& uiRenderedBlades, float& fLodBlend) const
{
	uiTotalBlades = m_uiDX11GrassBladeCount;
	uiRenderedBlades = m_uiDX11GrassRenderedBlades;
	fLodBlend = m_fGrassLodBlendFactor;
}

void CSpeedTreeForestDirectX::SetGrassSize(float fSize)
{
	if (fSize > 0.0f)
	{
		m_fGrassSize = fSize;
	}
}

float CSpeedTreeForestDirectX::GetGrassSize() const
{
	return m_fGrassSize;
}


///////////////////////////////////////////////////////////////////////
//	Iteration 2: Grass Geometry Generation

bool CSpeedTreeForestDirectX::GenerateGrassGeometry()
{
	// Check if grass wrapper is available
	if (!m_pGrassWrapper)
	{
		static DWORD s_dwGrassWrapperMissingLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwGrassWrapperMissingLogMS || (dwNow - s_dwGrassWrapperMissingLogMS) >= 5000u)
		{
			s_dwGrassWrapperMissingLogMS = dwNow;
			TraceError("DX11_GRASS_GENERATE: No grass wrapper available - skipping grass generation");
		}
		return false;
	}

	// Iteration 4: Generate grass vertices using wrapper method with LOD parameters
	std::vector<GrassVertex> vertices;
	if (!m_pGrassWrapper->GenerateGrassVertices(vertices,
													m_uiDX11GrassRegionCount,
													m_uiDX11GrassBladeCount,
													m_vCachedCameraPos,
													m_fGrassLodNearDistance,
													m_fGrassLodFarDistance,
													m_fGrassLodBlendFactor))
	{
		static DWORD s_dwGrassGenerateFailLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwGrassGenerateFailLogMS || (dwNow - s_dwGrassGenerateFailLogMS) >= 5000u)
		{
			s_dwGrassGenerateFailLogMS = dwNow;
			TraceError("DX11_GRASS_GENERATE: Failed to generate grass vertices from wrapper");
		}
		return false;
	}

	if (m_uiDX11GrassBladeCount == 0)
	{
		static DWORD s_dwGrassNoBladeLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwGrassNoBladeLogMS || (dwNow - s_dwGrassNoBladeLogMS) >= 5000u)
		{
			s_dwGrassNoBladeLogMS = dwNow;
			TraceError("DX11_GRASS_GENERATE: No grass blades generated (LOD cull or empty regions)");
		}
		return false;
	}

	// Store actual rendered blade count for telemetry
	m_uiDX11GrassRenderedBlades = m_uiDX11GrassBladeCount;

	// Create or update vertex buffer
	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
	{
		TraceError("DX11_GRASS_GENERATE: No DX11 device available");
		return false;
	}

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	if (!pDevice)
	{
		TraceError("DX11_GRASS_GENERATE: No D3D11 device available");
		return false;
	}

	// Calculate vertex count
	m_uiDX11GrassVertexCount = static_cast<UINT>(vertices.size());

	// Create vertex buffer
	D3D11_BUFFER_DESC vbDesc = {};
	vbDesc.ByteWidth = m_uiDX11GrassVertexCount * sizeof(GrassVertex);
	vbDesc.Usage = D3D11_USAGE_DYNAMIC;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbDesc.MiscFlags = 0;
	vbDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA vbData = {};
	vbData.pSysMem = vertices.data();

	HRESULT hr = pDevice->CreateBuffer(&vbDesc, &vbData, &m_pDX11GrassVertexBuffer);
	if (FAILED(hr))
	{
		TraceError("DX11_GRASS_GENERATE: Failed to create vertex buffer hr=0x%08X", static_cast<unsigned int>(hr));
		m_uiDX11GrassVertexCount = 0;
		return false;
	}

	static DWORD s_dwGrassGeneratedLogMS = 0u;
	const DWORD dwNowGenerated = ELTimer_GetMSec();
	if (0u == s_dwGrassGeneratedLogMS || (dwNowGenerated - s_dwGrassGeneratedLogMS) >= 5000u)
	{
		s_dwGrassGeneratedLogMS = dwNowGenerated;
		TraceError("DX11_GRASS_GENERATE: Generated %u blades (%u vertices) across %u regions (LOD blend=%.2f)",
			m_uiDX11GrassBladeCount, m_uiDX11GrassVertexCount, m_uiDX11GrassRegionCount, m_fGrassLodBlendFactor);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////
//	Iteration 2: Grass Texture Loading

bool CSpeedTreeForestDirectX::LoadGrassTexture()
{
	if (!DX11RuntimeConfig::kGrassEnableTexture)
		return false;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return false;

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	if (!pDevice)
		return false;

	const char* szTexturePath = DX11RuntimeConfig::kGrassTexturePath;

	// M3-TEXTURE-ASYNC-10-RUNTIME: Load grass texture with PRIORITY_LOW (non-critical)
	// Grass can show white fallback for a few frames without impacting gameplay
	ID3D11ShaderResourceView* pGrassSRV = CGraphicTextureDX11::LoadTextureAsync(
		pDevice,
		szTexturePath,
		CGraphicTextureDX11::PRIORITY_LOW,
		nullptr,  // No callback - grass renders every frame, will pick up cached texture when ready
		true);

	if (!pGrassSRV)
	{
		TraceError("DX11_GRASS_TEXTURE_LOAD_FAILED: Failed to load %s", szTexturePath);
		return false;
	}

	if (m_pDX11GrassTextureSRV)
	{
		m_pDX11GrassTextureSRV->Release();
		m_pDX11GrassTextureSRV = nullptr;
	}

	m_pDX11GrassTextureSRV = pGrassSRV;
	TraceError("DX11_GRASS_TEXTURE_LOAD_SUCCESS: Loaded grass texture from %s", szTexturePath);

	return true;
}

void CSpeedTreeForestDirectX::UpdateGrassConstantBuffer(const DirectX::SimpleMath::Matrix& matViewProj, const DirectX::SimpleMath::Vector3& cameraPos)
{
	if (!m_pDX11GrassConstantBuffer)
		return;

	// Map constant buffer for writing
	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return;

	ID3D11DeviceContext* pContext = pGrpDevice->GetContext();
	if (!pContext)
		return;

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = pContext->Map(m_pDX11GrassConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(hr))
		return;

	GrassConstantBuffer* pBuffer = static_cast<GrassConstantBuffer*>(mapped.pData);

	// Fill constant buffer data
	pBuffer->worldViewProj = DirectX::XMFLOAT4X4(
		matViewProj._11, matViewProj._12, matViewProj._13, matViewProj._14,
		matViewProj._21, matViewProj._22, matViewProj._23, matViewProj._24,
		matViewProj._31, matViewProj._32, matViewProj._33, matViewProj._34,
		matViewProj._41, matViewProj._42, matViewProj._43, matViewProj._44
	);

	// Camera position and grass size
	pBuffer->cameraPosAndSize = DirectX::XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, m_fGrassSize);

	// Iteration 3: Wind animation parameters
	pBuffer->timeAndWind = DirectX::XMFLOAT4(m_fGrassWindTime, GetWindStrength(), 0.0f, 0.0f);

	// Wind matrix from SpeedTree wind system
	pBuffer->windMatrix = DirectX::XMFLOAT4X4(
		m_matGrassWindMatrix._11, m_matGrassWindMatrix._12, m_matGrassWindMatrix._13, m_matGrassWindMatrix._14,
		m_matGrassWindMatrix._21, m_matGrassWindMatrix._22, m_matGrassWindMatrix._23, m_matGrassWindMatrix._24,
		m_matGrassWindMatrix._31, m_matGrassWindMatrix._32, m_matGrassWindMatrix._33, m_matGrassWindMatrix._34,
		m_matGrassWindMatrix._41, m_matGrassWindMatrix._42, m_matGrassWindMatrix._43, m_matGrassWindMatrix._44
	);

	pContext->Unmap(m_pDX11GrassConstantBuffer, 0);
}

void CSpeedTreeForestDirectX::RenderGrassDX11(ID3D11DeviceContext* pContext)
{
	if (!pContext || !m_bDX11GrassResourcesReady)
		return;

	// Temporary migration safety gate:
	// NOT_FULLY_IMPLEMENTED: keep grass rendering disabled until SpeedGrass terrain/material mapping is fixed.
	if (DX11RuntimeConfig::kGrassTemporarilyDisableRendering)
	{
		static DWORD s_dwGrassDisabledLogMS = 0u;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwGrassDisabledLogMS || (dwNow - s_dwGrassDisabledLogMS) >= 5000u)
		{
			s_dwGrassDisabledLogMS = dwNow;
			TraceError("DX11_GRASS_RENDER_DISABLED NOT_FULLY_IMPLEMENTED: runtime rendering disabled by config");
		}
		m_uiDX11GrassRenderedBlades = 0u;
		m_uiDX11GrassBladeCount = 0u;
		m_uiDX11GrassRegionCount = 0u;
		return;
	}

	// Iteration 2: Generate grass geometry if needed
	if (m_uiDX11GrassVertexCount == 0 || m_uiDX11GrassBladeCount == 0)
	{
		if (!GenerateGrassGeometry())
		{
			// Failed to generate geometry, skip rendering
			return;
		}
	}

	// Load texture if not loaded
	if (!m_pDX11GrassTextureSRV)
	{
		LoadGrassTexture();
	}

	// Check if we have anything to render
	if (m_uiDX11GrassVertexCount == 0)
		return;

	// Use cached matrices and camera position
	if (!m_bMatricesCached)
		return;

	// Iteration 3: Update grass wind time from system time
	m_fGrassWindTime = m_fAccumTime;

	DirectX::SimpleMath::Matrix matViewProj = m_matCachedView * m_matCachedProj;

	// Update constant buffer with current frame data
	UpdateGrassConstantBuffer(matViewProj, m_vCachedCameraPos);

	// Set vertex buffer
	UINT uiStride = sizeof(GrassVertex);
	UINT uiOffset = 0;
	pContext->IASetVertexBuffers(0, 1, &m_pDX11GrassVertexBuffer, &uiStride, &uiOffset);

	// Set input layout
	pContext->IASetInputLayout(m_pDX11GrassInputLayout);

	// Set primitive topology (triangle list for quads)
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set shaders
	pContext->VSSetShader(m_pDX11GrassVS, nullptr, 0);

	// Select pixel shader based on rendering mode
	if (m_bDX11ShadowViewProjOverrideActive && m_pDX11GrassShadowAlphaPS)
	{
		// Shadow map rendering: use depth-only shadow pixel shader
		pContext->PSSetShader(m_pDX11GrassShadowAlphaPS, nullptr, 0);
	}
	else
	{
		// Color pass: use regular pixel shader
		pContext->PSSetShader(m_pDX11GrassPS, nullptr, 0);
	}

	// Set constant buffer
	pContext->VSSetConstantBuffers(0, 1, &m_pDX11GrassConstantBuffer);

	// Set texture and sampler if available (needed for both color and shadow passes)
	if (m_pDX11GrassTextureSRV && DX11RuntimeConfig::kGrassEnableTexture)
	{
		pContext->PSSetShaderResources(0, 1, &m_pDX11GrassTextureSRV);
		pContext->PSSetSamplers(0, 1, &m_pDX11GrassSamplerState);

		// Set alpha reference buffer (needed for alpha testing in both passes)
		D3D11_MAPPED_SUBRESOURCE mapped;
		HRESULT hr = pContext->Map(m_pDX11GrassAlphaRefBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			float* pAlphaRef = static_cast<float*>(mapped.pData);
			pAlphaRef[0] = DX11RuntimeConfig::kGrassAlphaTestRef;
			pAlphaRef[1] = 0.0f;  // Padding
			pAlphaRef[2] = 0.0f;
			pAlphaRef[3] = 0.0f;
			pContext->Unmap(m_pDX11GrassAlphaRefBuffer, 0);
			pContext->PSSetConstantBuffers(1, 1, &m_pDX11GrassAlphaRefBuffer);
		}
	}

	// Set blend state for alpha blending (only in color pass)
	FLOAT blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	if (m_bDX11ShadowViewProjOverrideActive)
	{
		// Shadow pass: no blending (depth-only write)
		pContext->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
	}
	else
	{
		// Color pass: enable alpha blending
		pContext->OMSetBlendState(m_pDX11GrassBlendState, blendFactor, 0xFFFFFFFF);
	}

	// Draw grass geometry
	// Each quad has 4 vertices and 2 triangles (6 indices, but we're using non-indexed rendering)
	// So we draw all vertices as triangles
	pContext->Draw(m_uiDX11GrassVertexCount, 0);

	// Restore default blend state
	pContext->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);

	// Unbind texture to prevent accidental use
	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);

	static DWORD s_dwGrassRenderLogMS = 0u;
	const DWORD dwNowGrassRender = ELTimer_GetMSec();
	if (0u == s_dwGrassRenderLogMS || (dwNowGrassRender - s_dwGrassRenderLogMS) >= 5000u)
	{
		s_dwGrassRenderLogMS = dwNowGrassRender;
		TraceError("DX11_GRASS_RENDER: Rendered %u grass vertices (%u blades) in %u regions",
			m_uiDX11GrassVertexCount, m_uiDX11GrassBladeCount, m_uiDX11GrassRegionCount);
	}
}

void CSpeedTreeForestDirectX::RenderBillboardsDX11(unsigned long ulRenderBitVector)
{
	if (!(ulRenderBitVector & Forest_RenderBillboards))
		return;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return;

	ID3D11DeviceContext* pContext = pGrpDevice->GetContext();
	if (!pContext || !m_pDX11SpeedTreeDynamicVB || !m_pDX11SpeedTreeConstantBuffer)
		return;

	struct SBillboardVertexDX11
	{
		float x, y, z;
		float u, v;
	};
	struct SBillboardConstants
	{
		DirectX::SimpleMath::Matrix matViewProj;
		DirectX::SimpleMath::Vector4 vTreePosAndRotation;
		DirectX::SimpleMath::Vector4 vLightDir;
	};

	auto EmitQuadTriangleList = [](const float* pCoords, const float* pTexCoords, SBillboardVertexDX11* pOutVerts)
	{
		pOutVerts[0] = { pCoords[0], pCoords[1], pCoords[2], pTexCoords[0], pTexCoords[1] };
		pOutVerts[1] = { pCoords[3], pCoords[4], pCoords[5], pTexCoords[2], pTexCoords[3] };
		pOutVerts[2] = { pCoords[6], pCoords[7], pCoords[8], pTexCoords[4], pTexCoords[5] };
		pOutVerts[3] = pOutVerts[0];
		pOutVerts[4] = pOutVerts[2];
		pOutVerts[5] = { pCoords[9], pCoords[10], pCoords[11], pTexCoords[6], pTexCoords[7] };
	};

	DirectX::SimpleMath::Matrix matViewProj;
	if (m_bDX11ShadowViewProjOverrideActive)
	{
		matViewProj = m_matDX11ShadowViewProjOverride;
	}
	else
	{
		const DirectX::SimpleMath::Matrix& matView = CGraphicBase::GetViewMatrix();
		const DirectX::SimpleMath::Matrix& matProj = CGraphicBase::GetProjMatrix();
		matViewProj = matView * matProj;
	}
	DirectX::SimpleMath::Matrix matViewProjShader;
	matViewProjShader = matViewProj.Transpose();

	pContext->IASetInputLayout(m_pDX11SpeedTreeBillboardInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(m_pDX11SpeedTreeBillboardVS, nullptr, 0);
	if (m_bDX11ShadowViewProjOverrideActive)
	{
		if (m_pDX11SpeedTreeShadowAlphaPS)
		{
			// Shadow pass is depth-only (no RTV bound), so use alpha-clip shadow PS.
			pContext->PSSetShader(m_pDX11SpeedTreeShadowAlphaPS, nullptr, 0);
		}
		else
		{
			// Fail-safe: avoid binding a color PS in depth-only pass, which triggers RTV warnings.
			pContext->PSSetShader(nullptr, nullptr, 0);
			static DWORD s_dwShadowBillboardPSNullLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwShadowBillboardPSNullLogTick || (dwNow - s_dwShadowBillboardPSNullLogTick) >= 5000u)
			{
				s_dwShadowBillboardPSNullLogTick = dwNow;
				TraceError("DX11_SPEEDTREE_SHADOW_BILLBOARD_PS_FALLBACK mode=null_ps reason=shadow_alpha_ps_missing");
			}
		}
	}
	else
	{
		pContext->PSSetShader(m_pDX11SpeedTreeBillboardPS, nullptr, 0);
	}
	pContext->VSSetConstantBuffers(0, 1, &m_pDX11SpeedTreeConstantBuffer);
	pContext->PSSetSamplers(0, 1, &m_pDX11SpeedTreeSamplerState);
	UpdateDX11SpeedTreeAlphaRef(pContext, m_pDX11SpeedTreeAlphaRefBuffer, static_cast<float>(c_nDefaultAlphaTestValue));

	const float afBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	if (m_bDX11ShadowViewProjOverrideActive)
		pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);
	else
		pContext->OMSetBlendState(m_pDX11SpeedTreeBlendState, afBlendFactor, 0xffffffffu);

	UINT uStride = sizeof(SBillboardVertexDX11);
	UINT uOffset = 0;
	pContext->IASetVertexBuffers(0, 1, &m_pDX11SpeedTreeDynamicVB, &uStride, &uOffset);

	DWORD dwSubmittedBillboards = 0;
	DWORD dwSkippedBillboardsByGeoFallback = 0;
	UINT64 ullSubmittedPrimitives = 0u;
	TTreeMap::iterator itor = m_pMainTreeMap.begin();
	while (itor != m_pMainTreeMap.end())
	{
		auto pMainTree = (itor++)->second;
		UINT uiCount = 0;
		auto ppInstances = pMainTree->GetInstances(uiCount);
		for (const auto& pTreeInst : ppInstances)
		{
			if (!pTreeInst || (!m_bDX11ShadowViewProjOverrideActive && !pTreeInst->isShow()))
				continue;

			if (m_setDX11ForceGeometryInstances.find(pTreeInst.get()) != m_setDX11ForceGeometryInstances.end())
			{
				++dwSkippedBillboardsByGeoFallback;
				continue;
			}

			CSpeedTreeRT* pSpeedTree = pTreeInst->GetSpeedTree();
			if (!pSpeedTree)
				continue;

			CSpeedTreeRT::SGeometry kGeometry;
			ZeroMemory(&kGeometry, sizeof(kGeometry));
			pSpeedTree->GetGeometry(kGeometry, SpeedTree_BillboardGeometry);

			// W3/T2: Load actual tree texture via DX11 helper
			ID3D11ShaderResourceView* pTreeSRV = __GetTreeTextureSRV(pTreeInst.get());
			if (!pTreeSRV || pTreeSRV == m_pDX11SpeedTreeDefaultTextureSRV)
			{
				if (pSpeedTree)
					pSpeedTree->SetLodLevel(DX11RuntimeConfig::kSpeedTreeForcedLodLevel);
				++dwSkippedBillboardsByGeoFallback;
				continue;
			}

			pContext->PSSetShaderResources(0, 1, &pTreeSRV);

			const float* pTreePos = pTreeInst->GetPosition();
			SBillboardConstants kConstants;
			ZeroMemory(&kConstants, sizeof(kConstants));
			kConstants.matViewProj = matViewProjShader;
			kConstants.vTreePosAndRotation = DirectX::SimpleMath::Vector4(
				pTreePos ? pTreePos[0] : 0.0f,
				pTreePos ? pTreePos[1] : 0.0f,
				pTreePos ? pTreePos[2] : 0.0f,
				0.0f);
			kConstants.vLightDir = DirectX::SimpleMath::Vector4(m_afLighting[0], m_afLighting[1], m_afLighting[2], 0.0f);
			pContext->UpdateSubresource(m_pDX11SpeedTreeConstantBuffer, 0, nullptr, &kConstants, 0, 0);

			SBillboardVertexDX11 akVerts[6];
			D3D11_MAPPED_SUBRESOURCE kMapped;
			ZeroMemory(&kMapped, sizeof(kMapped));
			bool bInstanceSubmitted = false;

			auto DrawBillboard = [&](const CSpeedTreeRT::SGeometry::SBillboard& rBillboard) -> void
			{
				if (!rBillboard.m_bIsActive || !rBillboard.m_pCoords || !rBillboard.m_pTexCoords)
					return;

				EmitQuadTriangleList(rBillboard.m_pCoords, rBillboard.m_pTexCoords, akVerts);

				HRESULT hrMap = pContext->Map(m_pDX11SpeedTreeDynamicVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMapped);
				if (FAILED(hrMap) || !kMapped.pData)
					return;

				memcpy(kMapped.pData, akVerts, sizeof(akVerts));
				pContext->Unmap(m_pDX11SpeedTreeDynamicVB, 0);
				pContext->Draw(6, 0);
				pGrpDevice->IncrementFrameDrawCalls(1u, 2u);
				++dwSubmittedBillboards;
				ullSubmittedPrimitives += 2u;
				bInstanceSubmitted = true;
			};

			DrawBillboard(kGeometry.m_sBillboard0);
			DrawBillboard(kGeometry.m_sBillboard1);
#ifdef WRAPPER_RENDER_HORIZONTAL_BILLBOARD
			DrawBillboard(kGeometry.m_sHorizontalBillboard);
#endif

			(void)bInstanceSubmitted;
		}
	}

	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);
	ID3D11Buffer* pNullCB = nullptr;
	pContext->PSSetConstantBuffers(1, 1, &pNullCB);
	pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);

	if (dwSubmittedBillboards > 0)
		AddDX11SubmittedInstanceCount(dwSubmittedBillboards, ullSubmittedPrimitives);

	static DWORD s_dwBillboardSummaryLogTick = 0u;
	const DWORD dwNowBillboard = ELTimer_GetMSec();
	if (0u == s_dwBillboardSummaryLogTick || (dwNowBillboard - s_dwBillboardSummaryLogTick) >= 5000u)
	{
		s_dwBillboardSummaryLogTick = dwNowBillboard;
		TraceError("DX11_SPEEDTREE_BILLBOARD_SUBMIT submitted=%u skipped_geo_fallback=%u",
			dwSubmittedBillboards, dwSkippedBillboardsByGeoFallback);
	}
}

// W3/T2: Helper to get DX11 composite texture SRV from SpeedTree wrapper
ID3D11ShaderResourceView* CSpeedTreeForestDirectX::__GetTreeTextureSRV(CSpeedTreeWrapper* pTreeWrapper)
{
	static bool s_bLoggedNullImage = false;
	static bool s_bLoggedEmptyFilename = false;
	static std::set<std::string> s_setLoggedNativeLoadOk;
	static std::set<std::string> s_setLoggedNativeLoadFail;

	if (!pTreeWrapper)
		return m_pDX11SpeedTreeDefaultTextureSRV;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return m_pDX11SpeedTreeDefaultTextureSRV;

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	if (!pDevice)
		return m_pDX11SpeedTreeDefaultTextureSRV;

	// Get composite texture from wrapper
	CGraphicImage* pImage = pTreeWrapper->GetCompositeImageInstance().GetGraphicImagePointer();
	if (!pImage)
	{
		if (!s_bLoggedNullImage)
		{
			TraceError("DX11_TEXTURE_LOAD path=native_only_default domain=speedtree reason=image_pointer_null");
			s_bLoggedNullImage = true;
		}
		return m_pDX11SpeedTreeDefaultTextureSRV;
	}

	const char* szFilename = pImage->GetFileName();
	if (!szFilename || !szFilename[0])
	{
		if (!s_bLoggedEmptyFilename)
		{
			TraceError("DX11_TEXTURE_LOAD path=native_only_default domain=speedtree reason=filename_empty");
			s_bLoggedEmptyFilename = true;
		}
		return m_pDX11SpeedTreeDefaultTextureSRV;
	}

	// M3-TEXTURE-ASYNC-10-RUNTIME: Load via DX11 async helper with PRIORITY_NORMAL
	// Tree textures are moderately important - can show white fallback for 1-2 frames
	ID3D11ShaderResourceView* pSRV = CGraphicTextureDX11::LoadTextureAsync(
		pDevice,
		szFilename,
		CGraphicTextureDX11::PRIORITY_NORMAL,
		nullptr,  // No callback - renders every frame, will pick up cached texture when ready
		true);
	if (pSRV)
	{
		const std::string stFilename(szFilename);
		if (s_setLoggedNativeLoadOk.insert(stFilename).second)
		{
			TraceError("DX11_TEXTURE_LOAD path=native_only_async domain=speedtree file=%s", szFilename);
		}

		// M3-SPEEDTREE-IMAGE-48: Telemetry tracking for DX11 texture loading parity
		static DWORD s_dwLastCompositeParityLog = 0;
		static DWORD s_dwCompositeLoadSuccessCount = 0;
		++s_dwCompositeLoadSuccessCount;
		const DWORD dwNow = ELTimer_GetMSec();
		if (dwNow - s_dwLastCompositeParityLog >= 5000)
		{
			TraceError("DX11_SPEEDTREE_TEXTURE_PARITY type=composite result=success count=%u", s_dwCompositeLoadSuccessCount);
			s_dwLastCompositeParityLog = dwNow;
			s_dwCompositeLoadSuccessCount = 0;
		}

		return pSRV;
	}

	const std::string stFilename(szFilename);
	if (s_setLoggedNativeLoadFail.insert(stFilename).second)
	{
		TraceError("DX11_TEXTURE_LOAD path=native_only_default domain=speedtree file=%s reason=native_load_failed", szFilename);
	}

	// M3-SPEEDTREE-IMAGE-48: Telemetry for texture load failures
	static DWORD s_dwLastCompositeFailLog = 0;
	static DWORD s_dwCompositeLoadFailCount = 0;
	++s_dwCompositeLoadFailCount;
	const DWORD dwNow2 = ELTimer_GetMSec();
	if (dwNow2 - s_dwLastCompositeFailLog >= 5000)
	{
		TraceError("DX11_SPEEDTREE_TEXTURE_PARITY type=composite result=fail count=%u", s_dwCompositeLoadFailCount);
		s_dwLastCompositeFailLog = dwNow2;
		s_dwCompositeLoadFailCount = 0;
	}

	return m_pDX11SpeedTreeDefaultTextureSRV;
}

// Branch pass uses bark texture in DX9; keep the same mapping in DX11.
ID3D11ShaderResourceView* CSpeedTreeForestDirectX::__GetTreeBranchTextureSRV(CSpeedTreeWrapper* pTreeWrapper)
{
	static bool s_bLoggedNullImage = false;
	static bool s_bLoggedEmptyFilename = false;
	static bool s_bLoggedCompositeFallback = false;
	static std::set<std::string> s_setLoggedNativeLoadOk;
	static std::set<std::string> s_setLoggedNativeLoadFail;

	if (!pTreeWrapper)
		return m_pDX11SpeedTreeDefaultTextureSRV;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return m_pDX11SpeedTreeDefaultTextureSRV;

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	if (!pDevice)
		return m_pDX11SpeedTreeDefaultTextureSRV;

	CGraphicImage* pImage = pTreeWrapper->GetBranchImageInstance().GetGraphicImagePointer();
	if (!pImage)
	{
		if (!s_bLoggedNullImage)
		{
			TraceError("DX11_TEXTURE_LOAD path=native_only_default domain=speedtree_branch reason=image_pointer_null");
			s_bLoggedNullImage = true;
		}
		if (!s_bLoggedCompositeFallback)
		{
			TraceError("DX11_TEXTURE_LOAD path=native_only domain=speedtree_branch reason=using_composite_fallback");
			s_bLoggedCompositeFallback = true;
		}
		return __GetTreeTextureSRV(pTreeWrapper);
	}

	const char* szFilename = pImage->GetFileName();
	if (!szFilename || !szFilename[0])
	{
		if (!s_bLoggedEmptyFilename)
		{
			TraceError("DX11_TEXTURE_LOAD path=native_only_default domain=speedtree_branch reason=filename_empty");
			s_bLoggedEmptyFilename = true;
		}
		if (!s_bLoggedCompositeFallback)
		{
			TraceError("DX11_TEXTURE_LOAD path=native_only domain=speedtree_branch reason=using_composite_fallback");
			s_bLoggedCompositeFallback = true;
		}
		return __GetTreeTextureSRV(pTreeWrapper);
	}

	// M3-TEXTURE-ASYNC-10-RUNTIME: Load branch texture with PRIORITY_NORMAL
	ID3D11ShaderResourceView* pSRV = CGraphicTextureDX11::LoadTextureAsync(
		pDevice,
		szFilename,
		CGraphicTextureDX11::PRIORITY_NORMAL,
		nullptr,  // No callback - renders every frame, will pick up cached texture when ready
		true);
	if (pSRV)
	{
		const std::string stFilename(szFilename);
		if (s_setLoggedNativeLoadOk.insert(stFilename).second)
		{
			TraceError("DX11_TEXTURE_LOAD path=native_only_async domain=speedtree_branch file=%s", szFilename);
		}

		// M3-SPEEDTREE-IMAGE-48: Telemetry tracking for DX11 branch texture loading parity
		static DWORD s_dwLastBranchParityLog = 0;
		static DWORD s_dwBranchLoadSuccessCount = 0;
		++s_dwBranchLoadSuccessCount;
		const DWORD dwNow = ELTimer_GetMSec();
		if (dwNow - s_dwLastBranchParityLog >= 5000)
		{
			TraceError("DX11_SPEEDTREE_TEXTURE_PARITY type=branch result=success count=%u", s_dwBranchLoadSuccessCount);
			s_dwLastBranchParityLog = dwNow;
			s_dwBranchLoadSuccessCount = 0;
		}

		return pSRV;
	}

	const std::string stFilename(szFilename);
	if (s_setLoggedNativeLoadFail.insert(stFilename).second)
	{
		TraceError("DX11_TEXTURE_LOAD path=native_only_default domain=speedtree_branch file=%s reason=native_load_failed", szFilename);
	}

	// M3-SPEEDTREE-IMAGE-48: Telemetry for branch texture load failures
	static DWORD s_dwLastBranchFailLog = 0;
	static DWORD s_dwBranchLoadFailCount = 0;
	++s_dwBranchLoadFailCount;
	const DWORD dwNow2 = ELTimer_GetMSec();
	if (dwNow2 - s_dwLastBranchFailLog >= 5000)
	{
		TraceError("DX11_SPEEDTREE_TEXTURE_PARITY type=branch result=fail count=%u", s_dwBranchLoadFailCount);
		s_dwLastBranchFailLog = dwNow2;
		s_dwBranchLoadFailCount = 0;
	}

	if (!s_bLoggedCompositeFallback)
	{
		TraceError("DX11_TEXTURE_LOAD path=native_only domain=speedtree_branch reason=using_composite_fallback");
		s_bLoggedCompositeFallback = true;
	}
	return __GetTreeTextureSRV(pTreeWrapper);
}

// W3.2: Branch shadow rendering (simplified implementation)
// DX11-native branch rendering (shadow + color pass) - per-tree-type buffer caching
void CSpeedTreeForestDirectX::RenderBranchesDX11(unsigned long ulRenderBitVector)
{
	if (!(ulRenderBitVector & Forest_RenderBranches))
		return;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return;

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	ID3D11DeviceContext* pContext = pGrpDevice->GetContext();
	if (!pContext || !pDevice || !m_pDX11SpeedTreeConstantBuffer || !m_pDX11SpeedTreeLeafPlacementBuffer)
		return;

	struct SBranchVertexDX11
	{
		float x, y, z;
		DWORD color;
		float u, v;
	};
	struct SBranchConstants
	{
		DirectX::SimpleMath::Matrix matViewProj;
		DirectX::SimpleMath::Vector4 vTreePos;
		DirectX::SimpleMath::Vector4 vLightDir;
	};
	auto ConvertD3DColorToRGBA = [](DWORD dwColor) -> DWORD
	{
		const DWORD a = (dwColor >> 24) & 0xffu;
		const DWORD r = (dwColor >> 16) & 0xffu;
		const DWORD g = (dwColor >> 8) & 0xffu;
		const DWORD b = dwColor & 0xffu;
		return (a << 24) | (b << 16) | (g << 8) | r;
	};

	// Set pipeline state
	pContext->IASetInputLayout(m_pDX11SpeedTreeBranchInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	pContext->VSSetShader(m_pDX11SpeedTreeBranchVS, nullptr, 0);
	if (m_bDX11ShadowViewProjOverrideActive)
	{
		if (m_pDX11SpeedTreeShadowAlphaPS)
			pContext->PSSetShader(m_pDX11SpeedTreeShadowAlphaPS, nullptr, 0);
		else
		{
			pContext->PSSetShader(nullptr, nullptr, 0);
			static DWORD s_dwShadowBranchPSNullLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwShadowBranchPSNullLogTick || (dwNow - s_dwShadowBranchPSNullLogTick) >= 5000u)
			{
				s_dwShadowBranchPSNullLogTick = dwNow;
				TraceError("DX11_SPEEDTREE_SHADOW_BRANCH_PS_FALLBACK mode=null_ps reason=shadow_alpha_ps_missing");
			}
		}
	}
	else
	{
		pContext->PSSetShader(
			m_pDX11SpeedTreeBranchOpaquePS ? m_pDX11SpeedTreeBranchOpaquePS : m_pDX11SpeedTreeBranchPS,
			nullptr,
			0);
	}
	pContext->VSSetConstantBuffers(0, 1, &m_pDX11SpeedTreeConstantBuffer);
	pContext->PSSetSamplers(0, 1, &m_pDX11SpeedTreeSamplerState);
	UpdateDX11SpeedTreeAlphaRef(pContext, m_pDX11SpeedTreeAlphaRefBuffer, m_bDX11ShadowViewProjOverrideActive ? 0.01f : 0.0f);

	// Set blend state
	const float afBlendFactor[4] = {0, 0, 0, 0};
	// DX9 parity: branch/frond/leaf color passes used alpha test (opaque write), not translucent blending.
	pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);

	// Determine view/proj matrix
	DirectX::SimpleMath::Matrix matViewProj;
	if (m_bDX11ShadowViewProjOverrideActive)
		matViewProj = m_matDX11ShadowViewProjOverride;
	else
		matViewProj = CGraphicBase::GetViewMatrix() * CGraphicBase::GetProjMatrix();
	DirectX::SimpleMath::Matrix matViewProjShader;
	matViewProjShader = matViewProj.Transpose();

	DWORD dwSubmittedDrawCalls = 0;
	UINT64 ullSubmittedPrimitives = 0u;
	DWORD dwTextureBinds = 0;  // M3-SPEEDTREE-ATLAS-09: Track texture binds per frame

	// W4.3: Per-tree-TYPE loop (outer loop)
	TTreeMap::iterator itor = m_pMainTreeMap.begin();
	while (itor != m_pMainTreeMap.end())
	{
		auto pMainTree = (itor++)->second;
		if (!pMainTree)
			continue;
		CSpeedTreeWrapper* pMainTreeRaw = pMainTree.get();
		CSpeedTreeRT* pMainTreeRT = pMainTree->GetSpeedTree();
		if (!pMainTreeRaw || !pMainTreeRT)
			continue;

		// Pull current geometry first so cache follows active LOD and avoids "frozen thin trees".
		CSpeedTreeRT::SGeometry kGeometry;
		ZeroMemory(&kGeometry, sizeof(kGeometry));
		pMainTreeRT->GetGeometry(kGeometry, SpeedTree_BranchGeometry);

		const CSpeedTreeRT::SGeometry::SIndexed& rBranch = kGeometry.m_sBranches;
		if (rBranch.m_usVertexCount <= 1 || rBranch.m_usNumStrips == 0 ||
			!rBranch.m_pCoords || !rBranch.m_pColors || !rBranch.m_pTexCoords0 ||
			!rBranch.m_pStripLengths || !rBranch.m_pStrips)
			continue;

		const SSpeedTreeIndexedCacheSignature kCurrentSig = {
			rBranch.m_nDiscreteLodLevel,
			rBranch.m_usVertexCount,
			rBranch.m_usNumStrips,
			HashSpeedTreeStrips(rBranch.m_pStripLengths, rBranch.m_usNumStrips),
			HashSpeedTreeStripEdges(rBranch.m_pStrips, rBranch.m_pStripLengths, rBranch.m_usNumStrips)
		};

		// Check if buffers are cached for this tree type
		ID3D11Buffer* pVB = nullptr;
		ID3D11Buffer* pIB = nullptr;
		std::vector<std::pair<UINT, UINT>> vStrips;
		UINT uBranchIndexCount = 0u;

		auto itVB = s_mapBranchVB.find(pMainTreeRaw);
		auto itIB = s_mapBranchIB.find(pMainTreeRaw);
		auto itStrips = s_mapBranchStrips.find(pMainTreeRaw);
		auto itIndexCount = s_mapBranchIndexCount.find(pMainTreeRaw);

		auto itSig = s_mapBranchCacheSig.find(pMainTreeRaw);
		const bool bHaveCached =
			(itVB != s_mapBranchVB.end() &&
			 itIB != s_mapBranchIB.end() &&
			 itStrips != s_mapBranchStrips.end() &&
			 itIndexCount != s_mapBranchIndexCount.end() &&
			 itSig != s_mapBranchCacheSig.end());

		if (bHaveCached &&
			itSig->second.lod == kCurrentSig.lod &&
			itSig->second.vertexCount == kCurrentSig.vertexCount &&
			itSig->second.stripCount == kCurrentSig.stripCount &&
			itSig->second.stripLengthHash == kCurrentSig.stripLengthHash &&
			itSig->second.stripEdgeIndexHash == kCurrentSig.stripEdgeIndexHash)
		{
			// Use cached buffers
			pVB = itVB->second;
			pIB = itIB->second;
			vStrips = itStrips->second;
			uBranchIndexCount = itIndexCount->second;
		}
		else
		{
			if (bHaveCached)
			{
				static DWORD s_dwBranchCacheInvalidateLogTick = 0u;
				const DWORD dwNow = ELTimer_GetMSec();
				if (0u == s_dwBranchCacheInvalidateLogTick || (dwNow - s_dwBranchCacheInvalidateLogTick) >= 2000u)
				{
					s_dwBranchCacheInvalidateLogTick = dwNow;
					TraceError(
						"DX11_SPEEDTREE_CACHE_INVALIDATE type=branch old_lod=%d new_lod=%d old_hash_len=%u new_hash_len=%u old_hash_edge=%u new_hash_edge=%u",
						itSig->second.lod,
						kCurrentSig.lod,
						itSig->second.stripLengthHash,
						kCurrentSig.stripLengthHash,
						itSig->second.stripEdgeIndexHash,
						kCurrentSig.stripEdgeIndexHash);
				}
			}

			// LOD/signature changed: drop stale cache for this tree type.
			if (itVB != s_mapBranchVB.end())
			{
				if (itVB->second)
					itVB->second->Release();
				s_mapBranchVB.erase(itVB);
			}
			if (itIB != s_mapBranchIB.end())
			{
				if (itIB->second)
					itIB->second->Release();
				s_mapBranchIB.erase(itIB);
			}
			if (itStrips != s_mapBranchStrips.end())
				s_mapBranchStrips.erase(itStrips);
			if (itIndexCount != s_mapBranchIndexCount.end())
				s_mapBranchIndexCount.erase(itIndexCount);
			if (itSig != s_mapBranchCacheSig.end())
				s_mapBranchCacheSig.erase(itSig);

			std::vector<SBranchVertexDX11> vVertices;
			vVertices.resize(rBranch.m_usVertexCount);
			for (UINT i = 0; i < rBranch.m_usVertexCount; ++i)
			{
				SBranchVertexDX11& rv = vVertices[i];
				rv.x = rBranch.m_pCoords[i * 3 + 0];
				rv.y = rBranch.m_pCoords[i * 3 + 1];
				rv.z = rBranch.m_pCoords[i * 3 + 2];
				rv.color = ConvertD3DColorToRGBA(rBranch.m_pColors[i]);
				rv.u = rBranch.m_pTexCoords0[i * 2 + 0];
				rv.v = rBranch.m_pTexCoords0[i * 2 + 1];
			}

			std::vector<uint16_t> vIndices;
			for (UINT s = 0; s < rBranch.m_usNumStrips; ++s)
			{
				const UINT uStripCount = rBranch.m_pStripLengths[s];
				const uint16_t* pStrip = rBranch.m_pStrips[s];
				if (uStripCount <= 2 || !pStrip)
					continue;

				bool bStripValid = true;
				for (UINT iIdx = 0; iIdx < uStripCount; ++iIdx)
				{
					if (pStrip[iIdx] >= rBranch.m_usVertexCount)
					{
						bStripValid = false;
						static bool s_bLoggedInvalidBranchStrip = false;
						if (!s_bLoggedInvalidBranchStrip)
						{
							s_bLoggedInvalidBranchStrip = true;
							TraceError(
								"DX11_SPEEDTREE_STRIP_REJECT type=branch strip=%u idx=%u value=%u vertex_count=%u lod=%d",
								s,
								iIdx,
								static_cast<unsigned int>(pStrip[iIdx]),
								static_cast<unsigned int>(rBranch.m_usVertexCount),
								rBranch.m_nDiscreteLodLevel);
						}
						break;
					}
				}
				if (!bStripValid)
					continue;

				vStrips.push_back(std::make_pair(static_cast<UINT>(vIndices.size()), uStripCount));
				vIndices.insert(vIndices.end(), pStrip, pStrip + uStripCount);
			}

			if (vIndices.empty())
				continue;

			// Create IMMUTABLE buffers (per-tree-type, not per-instance)
			D3D11_BUFFER_DESC vbDesc = {};
			vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
			vbDesc.ByteWidth = static_cast<UINT>(vVertices.size() * sizeof(SBranchVertexDX11));
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vbData = {};
			vbData.pSysMem = vVertices.data();
			if (FAILED(pDevice->CreateBuffer(&vbDesc, &vbData, &pVB)) || !pVB)
				continue;

			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
			ibDesc.ByteWidth = static_cast<UINT>(vIndices.size() * sizeof(uint16_t));
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA ibData = {};
			ibData.pSysMem = vIndices.data();
			if (FAILED(pDevice->CreateBuffer(&ibDesc, &ibData, &pIB)) || !pIB)
			{
				pVB->Release();
				continue;
			}

			// Cache buffers for this tree type
			s_mapBranchVB[pMainTreeRaw] = pVB;
			s_mapBranchIB[pMainTreeRaw] = pIB;
			s_mapBranchStrips[pMainTreeRaw] = vStrips;
			s_mapBranchIndexCount[pMainTreeRaw] = static_cast<UINT>(vIndices.size());
			s_mapBranchCacheSig[pMainTreeRaw] = kCurrentSig;
			uBranchIndexCount = static_cast<UINT>(vIndices.size());
		}

		if (!pVB || !pIB || vStrips.empty() || uBranchIndexCount == 0u)
			continue;

		// Bind buffers once per tree type
		UINT uStride = sizeof(SBranchVertexDX11);
		UINT uOffset = 0;
		pContext->IASetVertexBuffers(0, 1, &pVB, &uStride, &uOffset);
		pContext->IASetIndexBuffer(pIB, DXGI_FORMAT_R16_UINT, 0);

		// M3-SPEEDTREE-ATLAS-09: Bind texture once per tree type (not per instance)
		// This optimization moves texture binding outside the inner loop
		ID3D11ShaderResourceView* pTreeSRV = __GetTreeBranchTextureSRV(pMainTree.get());
		if (!pTreeSRV)
			continue;  // Skip this tree type if texture unavailable
		pContext->PSSetShaderResources(0, 1, &pTreeSRV);
		++dwTextureBinds;

		// W4.3: Per-instance loop (inner loop) - draw with different tree positions
		UINT uiCount = 0;
		auto ppInstances = pMainTree->GetInstances(uiCount);
		for (const auto& pTreeInst : ppInstances)
		{
			if (!pTreeInst || (!m_bDX11ShadowViewProjOverrideActive && !pTreeInst->isShow()))
				continue;

			const float* pTreePos = pTreeInst->GetPosition();
			SBranchConstants kConstants;
			ZeroMemory(&kConstants, sizeof(kConstants));
			kConstants.matViewProj = matViewProjShader;
			kConstants.vTreePos = DirectX::SimpleMath::Vector4(
				pTreePos ? pTreePos[0] : 0.0f,
				pTreePos ? pTreePos[1] : 0.0f,
				pTreePos ? pTreePos[2] : 0.0f,
				0.0f);
			kConstants.vLightDir = DirectX::SimpleMath::Vector4(m_afLighting[0], m_afLighting[1], m_afLighting[2], 0.0f);
			pContext->UpdateSubresource(m_pDX11SpeedTreeConstantBuffer, 0, nullptr, &kConstants, 0, 0);

			// Draw all strips for this instance
			for (size_t s = 0; s < vStrips.size(); ++s)
			{
				const UINT uStart = vStrips[s].first;
				const UINT uCount = vStrips[s].second;
				if (uCount <= 2u || uStart >= uBranchIndexCount || uCount > (uBranchIndexCount - uStart))
				{
					static bool s_bLoggedBranchDrawRangeReject = false;
					if (!s_bLoggedBranchDrawRangeReject)
					{
						s_bLoggedBranchDrawRangeReject = true;
						TraceError(
							"DX11_SPEEDTREE_DRAW_REJECT type=branch strip=%u start=%u count=%u index_count=%u",
							static_cast<unsigned int>(s),
							static_cast<unsigned int>(uStart),
							static_cast<unsigned int>(uCount),
							static_cast<unsigned int>(uBranchIndexCount));
					}
					continue;
				}

				pContext->DrawIndexed(uCount, uStart, 0);
				pGrpDevice->IncrementFrameDrawCalls(1u, static_cast<UINT>(uCount / 3u));
				++dwSubmittedDrawCalls;
				ullSubmittedPrimitives += static_cast<UINT64>(uCount / 3u);
			}
		}
	}

	// Cleanup
	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);
	ID3D11Buffer* pNullCB = nullptr;
	pContext->PSSetConstantBuffers(1, 1, &pNullCB);
	pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);

	if (dwSubmittedDrawCalls > 0)
		AddDX11SubmittedInstanceCount(dwSubmittedDrawCalls, ullSubmittedPrimitives);

	// M3-SPEEDTREE-ATLAS-09: Log texture binding telemetry (periodic)
	static DWORD s_dwLastBranchBindLog = 0;
	static DWORD s_dwBranchBindSum = 0;
	static DWORD s_dwBranchBindFrames = 0;
	s_dwBranchBindSum += dwTextureBinds;
	++s_dwBranchBindFrames;
	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwLastBranchBindLog >= 5000)
	{
		TraceError("DX11_SPEEDTREE_TEXTURE_BINDS type=branch binds_per_frame=%u frames=%u",
			s_dwBranchBindFrames > 0 ? (s_dwBranchBindSum / s_dwBranchBindFrames) : 0,
			s_dwBranchBindFrames);
		s_dwLastBranchBindLog = dwNow;
		s_dwBranchBindSum = 0;
		s_dwBranchBindFrames = 0;
	}
}

// W4.3: Frond rendering (shadow + color pass) - per-tree-type buffer caching (fixed from per-instance per-frame overhead)
void CSpeedTreeForestDirectX::RenderFrondsDX11(unsigned long ulRenderBitVector)
{
	if (!(ulRenderBitVector & Forest_RenderFronds))
		return;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return;

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	ID3D11DeviceContext* pContext = pGrpDevice->GetContext();
	if (!pContext || !pDevice || !m_pDX11SpeedTreeConstantBuffer)
		return;

	struct SFrondVertexDX11
	{
		float x, y, z;
		DWORD color;
		float u, v;
	};
	struct SFrondConstants
	{
		DirectX::SimpleMath::Matrix matViewProj;
		DirectX::SimpleMath::Vector4 vTreePos;
		DirectX::SimpleMath::Vector4 vLightDir;
	};
	auto ConvertD3DColorToRGBA = [](DWORD dwColor) -> DWORD
	{
		const DWORD a = (dwColor >> 24) & 0xffu;
		const DWORD r = (dwColor >> 16) & 0xffu;
		const DWORD g = (dwColor >> 8) & 0xffu;
		const DWORD b = dwColor & 0xffu;
		return (a << 24) | (b << 16) | (g << 8) | r;
	};

	// Set pipeline state (same as branches - shared vertex format)
	pContext->IASetInputLayout(m_pDX11SpeedTreeBranchInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	pContext->VSSetShader(m_pDX11SpeedTreeBranchVS, nullptr, 0);
	if (m_bDX11ShadowViewProjOverrideActive)
	{
		if (m_pDX11SpeedTreeShadowAlphaPS)
			pContext->PSSetShader(m_pDX11SpeedTreeShadowAlphaPS, nullptr, 0);
		else
		{
			pContext->PSSetShader(nullptr, nullptr, 0);
			static DWORD s_dwShadowFrondPSNullLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwShadowFrondPSNullLogTick || (dwNow - s_dwShadowFrondPSNullLogTick) >= 5000u)
			{
				s_dwShadowFrondPSNullLogTick = dwNow;
				TraceError("DX11_SPEEDTREE_SHADOW_FROND_PS_FALLBACK mode=null_ps reason=shadow_alpha_ps_missing");
			}
		}
	}
	else
	{
		pContext->PSSetShader(m_pDX11SpeedTreeBranchPS, nullptr, 0);
	}
	pContext->VSSetConstantBuffers(0, 1, &m_pDX11SpeedTreeConstantBuffer);
	pContext->PSSetSamplers(0, 1, &m_pDX11SpeedTreeSamplerState);

	// Set blend state
	const float afBlendFactor[4] = {0, 0, 0, 0};
	if (m_bDX11ShadowViewProjOverrideActive)
	{
		pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);
	}
	else
	{
		ID3D11BlendState* pLeafBlendState =
			m_pDX11SpeedTreeAlphaToCoverageBlendState ? m_pDX11SpeedTreeAlphaToCoverageBlendState : nullptr;
		pContext->OMSetBlendState(pLeafBlendState, afBlendFactor, 0xffffffffu);
	}

	// Determine view/proj matrix
	DirectX::SimpleMath::Matrix matViewProj;
	if (m_bDX11ShadowViewProjOverrideActive)
		matViewProj = m_matDX11ShadowViewProjOverride;
	else
		matViewProj = CGraphicBase::GetViewMatrix() * CGraphicBase::GetProjMatrix();
	DirectX::SimpleMath::Matrix matViewProjShader;
	matViewProjShader = matViewProj.Transpose();

	DWORD dwSubmittedDrawCalls = 0;
	UINT64 ullSubmittedPrimitives = 0u;
	DWORD dwTextureBinds = 0;  // M3-SPEEDTREE-ATLAS-09: Track texture binds per frame

	// W4.3: Per-tree-TYPE loop (outer loop)
	TTreeMap::iterator itor = m_pMainTreeMap.begin();
	while (itor != m_pMainTreeMap.end())
	{
		auto pMainTree = (itor++)->second;
		if (!pMainTree)
			continue;
		CSpeedTreeWrapper* pMainTreeRaw = pMainTree.get();
		CSpeedTreeRT* pMainTreeRT = pMainTree->GetSpeedTree();
		if (!pMainTreeRaw || !pMainTreeRT)
			continue;

		// Pull current geometry first so cache follows active LOD and avoids stale frond strips.
		CSpeedTreeRT::SGeometry kGeometry;
		ZeroMemory(&kGeometry, sizeof(kGeometry));
		pMainTreeRT->GetGeometry(kGeometry, SpeedTree_FrondGeometry);

		const CSpeedTreeRT::SGeometry::SIndexed& rFrond = kGeometry.m_sFronds;
		if (rFrond.m_usVertexCount <= 1 || rFrond.m_usNumStrips == 0 ||
			!rFrond.m_pCoords || !rFrond.m_pColors || !rFrond.m_pTexCoords0 ||
			!rFrond.m_pStripLengths || !rFrond.m_pStrips)
			continue;

		const SSpeedTreeIndexedCacheSignature kCurrentSig = {
			rFrond.m_nDiscreteLodLevel,
			rFrond.m_usVertexCount,
			rFrond.m_usNumStrips,
			HashSpeedTreeStrips(rFrond.m_pStripLengths, rFrond.m_usNumStrips),
			HashSpeedTreeStripEdges(rFrond.m_pStrips, rFrond.m_pStripLengths, rFrond.m_usNumStrips)
		};

		// Check if buffers are cached for this tree type
		ID3D11Buffer* pVB = nullptr;
		ID3D11Buffer* pIB = nullptr;
		std::vector<std::pair<UINT, UINT>> vStrips;
		UINT uFrondIndexCount = 0u;

		auto itVB = s_mapFrondVB.find(pMainTreeRaw);
		auto itIB = s_mapFrondIB.find(pMainTreeRaw);
		auto itStrips = s_mapFrondStrips.find(pMainTreeRaw);
		auto itIndexCount = s_mapFrondIndexCount.find(pMainTreeRaw);

		auto itSig = s_mapFrondCacheSig.find(pMainTreeRaw);
		const bool bHaveCached =
			(itVB != s_mapFrondVB.end() &&
			 itIB != s_mapFrondIB.end() &&
			 itStrips != s_mapFrondStrips.end() &&
			 itIndexCount != s_mapFrondIndexCount.end() &&
			 itSig != s_mapFrondCacheSig.end());

		if (bHaveCached &&
			itSig->second.lod == kCurrentSig.lod &&
			itSig->second.vertexCount == kCurrentSig.vertexCount &&
			itSig->second.stripCount == kCurrentSig.stripCount &&
			itSig->second.stripLengthHash == kCurrentSig.stripLengthHash &&
			itSig->second.stripEdgeIndexHash == kCurrentSig.stripEdgeIndexHash)
		{
			// Use cached buffers
			pVB = itVB->second;
			pIB = itIB->second;
			vStrips = itStrips->second;
			uFrondIndexCount = itIndexCount->second;
		}
		else
		{
			if (bHaveCached)
			{
				static DWORD s_dwFrondCacheInvalidateLogTick = 0u;
				const DWORD dwNow = ELTimer_GetMSec();
				if (0u == s_dwFrondCacheInvalidateLogTick || (dwNow - s_dwFrondCacheInvalidateLogTick) >= 2000u)
				{
					s_dwFrondCacheInvalidateLogTick = dwNow;
					TraceError(
						"DX11_SPEEDTREE_CACHE_INVALIDATE type=frond old_lod=%d new_lod=%d old_hash_len=%u new_hash_len=%u old_hash_edge=%u new_hash_edge=%u",
						itSig->second.lod,
						kCurrentSig.lod,
						itSig->second.stripLengthHash,
						kCurrentSig.stripLengthHash,
						itSig->second.stripEdgeIndexHash,
						kCurrentSig.stripEdgeIndexHash);
				}
			}

			// LOD/signature changed: drop stale cache for this tree type.
			if (itVB != s_mapFrondVB.end())
			{
				if (itVB->second)
					itVB->second->Release();
				s_mapFrondVB.erase(itVB);
			}
			if (itIB != s_mapFrondIB.end())
			{
				if (itIB->second)
					itIB->second->Release();
				s_mapFrondIB.erase(itIB);
			}
			if (itStrips != s_mapFrondStrips.end())
				s_mapFrondStrips.erase(itStrips);
			if (itIndexCount != s_mapFrondIndexCount.end())
				s_mapFrondIndexCount.erase(itIndexCount);
			if (itSig != s_mapFrondCacheSig.end())
				s_mapFrondCacheSig.erase(itSig);

			std::vector<SFrondVertexDX11> vVertices;
			vVertices.resize(rFrond.m_usVertexCount);
			for (UINT i = 0; i < rFrond.m_usVertexCount; ++i)
			{
				SFrondVertexDX11& rv = vVertices[i];
				rv.x = rFrond.m_pCoords[i * 3 + 0];
				rv.y = rFrond.m_pCoords[i * 3 + 1];
				rv.z = rFrond.m_pCoords[i * 3 + 2];
				rv.color = ConvertD3DColorToRGBA(rFrond.m_pColors[i]);
				rv.u = rFrond.m_pTexCoords0[i * 2 + 0];
				rv.v = rFrond.m_pTexCoords0[i * 2 + 1];
			}

			std::vector<uint16_t> vIndices;
			for (UINT s = 0; s < rFrond.m_usNumStrips; ++s)
			{
				const UINT uStripCount = rFrond.m_pStripLengths[s];
				const uint16_t* pStrip = rFrond.m_pStrips[s];
				if (uStripCount <= 2 || !pStrip)
					continue;

				bool bStripValid = true;
				for (UINT iIdx = 0; iIdx < uStripCount; ++iIdx)
				{
					if (pStrip[iIdx] >= rFrond.m_usVertexCount)
					{
						bStripValid = false;
						static bool s_bLoggedInvalidFrondStrip = false;
						if (!s_bLoggedInvalidFrondStrip)
						{
							s_bLoggedInvalidFrondStrip = true;
							TraceError(
								"DX11_SPEEDTREE_STRIP_REJECT type=frond strip=%u idx=%u value=%u vertex_count=%u lod=%d",
								s,
								iIdx,
								static_cast<unsigned int>(pStrip[iIdx]),
								static_cast<unsigned int>(rFrond.m_usVertexCount),
								rFrond.m_nDiscreteLodLevel);
						}
						break;
					}
				}
				if (!bStripValid)
					continue;

				vStrips.push_back(std::make_pair(static_cast<UINT>(vIndices.size()), uStripCount));
				vIndices.insert(vIndices.end(), pStrip, pStrip + uStripCount);
			}

			if (vIndices.empty())
				continue;

			// Create IMMUTABLE buffers (per-tree-type, not per-instance)
			D3D11_BUFFER_DESC vbDesc = {};
			vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
			vbDesc.ByteWidth = static_cast<UINT>(vVertices.size() * sizeof(SFrondVertexDX11));
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vbData = {};
			vbData.pSysMem = vVertices.data();
			if (FAILED(pDevice->CreateBuffer(&vbDesc, &vbData, &pVB)) || !pVB)
				continue;

			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
			ibDesc.ByteWidth = static_cast<UINT>(vIndices.size() * sizeof(uint16_t));
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA ibData = {};
			ibData.pSysMem = vIndices.data();
			if (FAILED(pDevice->CreateBuffer(&ibDesc, &ibData, &pIB)) || !pIB)
			{
				pVB->Release();
				continue;
			}

			// Cache buffers for this tree type
			s_mapFrondVB[pMainTreeRaw] = pVB;
			s_mapFrondIB[pMainTreeRaw] = pIB;
			s_mapFrondStrips[pMainTreeRaw] = vStrips;
			s_mapFrondIndexCount[pMainTreeRaw] = static_cast<UINT>(vIndices.size());
			s_mapFrondCacheSig[pMainTreeRaw] = kCurrentSig;
			uFrondIndexCount = static_cast<UINT>(vIndices.size());
		}

		if (!pVB || !pIB || vStrips.empty() || uFrondIndexCount == 0u)
			continue;

		UpdateDX11SpeedTreeAlphaRef(pContext, m_pDX11SpeedTreeAlphaRefBuffer, kGeometry.m_fFrondAlphaTestValue);

		// Bind buffers once per tree type
		UINT uStride = sizeof(SFrondVertexDX11);
		UINT uOffset = 0;
		pContext->IASetVertexBuffers(0, 1, &pVB, &uStride, &uOffset);
		pContext->IASetIndexBuffer(pIB, DXGI_FORMAT_R16_UINT, 0);

		// M3-SPEEDTREE-ATLAS-09: Bind texture once per tree type (not per instance)
		// This optimization moves texture binding outside the inner loop
		ID3D11ShaderResourceView* pTreeSRV = __GetTreeTextureSRV(pMainTree.get());
		if (!pTreeSRV)
			continue;  // Skip this tree type if texture unavailable
		pContext->PSSetShaderResources(0, 1, &pTreeSRV);
		++dwTextureBinds;

		// W4.3: Per-instance loop (inner loop) - draw with different tree positions
		UINT uiCount = 0;
		auto ppInstances = pMainTree->GetInstances(uiCount);
		for (const auto& pTreeInst : ppInstances)
		{
			if (!pTreeInst || (!m_bDX11ShadowViewProjOverrideActive && !pTreeInst->isShow()))
				continue;

			const float* pTreePos = pTreeInst->GetPosition();
			SFrondConstants kConstants;
			ZeroMemory(&kConstants, sizeof(kConstants));
			kConstants.matViewProj = matViewProjShader;
			kConstants.vTreePos = DirectX::SimpleMath::Vector4(
				pTreePos ? pTreePos[0] : 0.0f,
				pTreePos ? pTreePos[1] : 0.0f,
				pTreePos ? pTreePos[2] : 0.0f,
				0.0f);
			kConstants.vLightDir = DirectX::SimpleMath::Vector4(m_afLighting[0], m_afLighting[1], m_afLighting[2], 0.0f);
			pContext->UpdateSubresource(m_pDX11SpeedTreeConstantBuffer, 0, nullptr, &kConstants, 0, 0);

			// Draw all strips for this instance
			for (size_t s = 0; s < vStrips.size(); ++s)
			{
				const UINT uStart = vStrips[s].first;
				const UINT uCount = vStrips[s].second;
				if (uCount <= 2u || uStart >= uFrondIndexCount || uCount > (uFrondIndexCount - uStart))
				{
					static bool s_bLoggedFrondDrawRangeReject = false;
					if (!s_bLoggedFrondDrawRangeReject)
					{
						s_bLoggedFrondDrawRangeReject = true;
						TraceError(
							"DX11_SPEEDTREE_DRAW_REJECT type=frond strip=%u start=%u count=%u index_count=%u",
							static_cast<unsigned int>(s),
							static_cast<unsigned int>(uStart),
							static_cast<unsigned int>(uCount),
							static_cast<unsigned int>(uFrondIndexCount));
					}
					continue;
				}

				pContext->DrawIndexed(uCount, uStart, 0);
				pGrpDevice->IncrementFrameDrawCalls(1u, static_cast<UINT>(uCount / 3u));
				++dwSubmittedDrawCalls;
				ullSubmittedPrimitives += static_cast<UINT64>(uCount / 3u);
			}
		}
	}

	// Cleanup
	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);
	ID3D11Buffer* pNullCB = nullptr;
	pContext->PSSetConstantBuffers(1, 1, &pNullCB);
	pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);

	if (dwSubmittedDrawCalls > 0)
		AddDX11SubmittedInstanceCount(dwSubmittedDrawCalls, ullSubmittedPrimitives);

	// M3-SPEEDTREE-ATLAS-09: Log texture binding telemetry (periodic)
	static DWORD s_dwLastFrondBindLog = 0;
	static DWORD s_dwFrondBindSum = 0;
	static DWORD s_dwFrondBindFrames = 0;
	s_dwFrondBindSum += dwTextureBinds;
	++s_dwFrondBindFrames;
	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwLastFrondBindLog >= 5000)
	{
		TraceError("DX11_SPEEDTREE_TEXTURE_BINDS type=frond binds_per_frame=%u frames=%u",
			s_dwFrondBindFrames > 0 ? (s_dwFrondBindSum / s_dwFrondBindFrames) : 0,
			s_dwFrondBindFrames);
		s_dwLastFrondBindLog = dwNow;
		s_dwFrondBindSum = 0;
		s_dwFrondBindFrames = 0;
	}
}

// W4.3: Leaf rendering (shadow + color pass) - per-tree-type buffer caching (fixed from per-instance per-frame overhead)
void CSpeedTreeForestDirectX::RenderLeavesDX11(unsigned long ulRenderBitVector)
{
	if (!(ulRenderBitVector & Forest_RenderLeaves))
		return;

	CGraphicDeviceDX11* pGrpDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pGrpDevice)
		return;

	ID3D11Device* pDevice = pGrpDevice->GetDevice();
	ID3D11DeviceContext* pContext = pGrpDevice->GetContext();
	if (!pContext || !pDevice || !m_pDX11SpeedTreeConstantBuffer)
		return;

	struct SLeafVertexDX11
	{
		float x, y, z;
		DWORD color;
		float u, v;
		float leafPlacementIndex;
		float leafScalar;
	};
	struct SLeafConstants
	{
		DirectX::SimpleMath::Matrix matViewProj;
		DirectX::SimpleMath::Vector4 vTreePos;
		DirectX::SimpleMath::Vector4 vLightDir;
	};
	auto ConvertD3DColorToRGBA = [](DWORD dwColor) -> DWORD
	{
		const DWORD a = (dwColor >> 24) & 0xffu;
		const DWORD r = (dwColor >> 16) & 0xffu;
		const DWORD g = (dwColor >> 8) & 0xffu;
		const DWORD b = dwColor & 0xffu;
		return (a << 24) | (b << 16) | (g << 8) | r;
	};
	static const int s_anLeafTriListIndices[6] = { 0, 1, 2, 0, 2, 3 };

	// Set pipeline state (leaf vertex shader and input layout)
	pContext->IASetInputLayout(m_pDX11SpeedTreeLeafInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(m_pDX11SpeedTreeLeafVS, nullptr, 0);
	if (m_bDX11ShadowViewProjOverrideActive)
	{
		if (m_pDX11SpeedTreeShadowAlphaPS)
			pContext->PSSetShader(m_pDX11SpeedTreeShadowAlphaPS, nullptr, 0);
		else
		{
			pContext->PSSetShader(nullptr, nullptr, 0);
			static DWORD s_dwShadowLeafPSNullLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwShadowLeafPSNullLogTick || (dwNow - s_dwShadowLeafPSNullLogTick) >= 5000u)
			{
				s_dwShadowLeafPSNullLogTick = dwNow;
				TraceError("DX11_SPEEDTREE_SHADOW_LEAF_PS_FALLBACK mode=null_ps reason=shadow_alpha_ps_missing");
			}
		}
	}
	else
	{
		pContext->PSSetShader(m_pDX11SpeedTreeBranchPS, nullptr, 0);
	}
	pContext->VSSetConstantBuffers(0, 1, &m_pDX11SpeedTreeConstantBuffer);
	pContext->PSSetSamplers(0, 1, &m_pDX11SpeedTreeSamplerState);

	// Set blend state
	const float afBlendFactor[4] = {0, 0, 0, 0};
	pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);

	// Determine view/proj matrix
	DirectX::SimpleMath::Matrix matViewProj;
	if (m_bDX11ShadowViewProjOverrideActive)
		matViewProj = m_matDX11ShadowViewProjOverride;
	else
		matViewProj = CGraphicBase::GetViewMatrix() * CGraphicBase::GetProjMatrix();
	DirectX::SimpleMath::Matrix matViewProjShader;
	matViewProjShader = matViewProj.Transpose();

	DWORD dwSubmittedDrawCalls = 0;
	UINT64 ullSubmittedPrimitives = 0u;
	DWORD dwTextureBinds = 0;  // M3-SPEEDTREE-ATLAS-09: Track texture binds per frame

	// W4.3: Per-tree-TYPE loop (outer loop)
	TTreeMap::iterator itor = m_pMainTreeMap.begin();
	while (itor != m_pMainTreeMap.end())
	{
		auto pMainTree = (itor++)->second;
		if (!pMainTree)
			continue;
		CSpeedTreeWrapper* pMainTreeRaw = pMainTree.get();
		CSpeedTreeRT* pMainTreeRT = pMainTree->GetSpeedTree();
		if (!pMainTreeRaw || !pMainTreeRT)
			continue;

		UINT uiLeafTableFloatCount = 0;
		const float* pLeafTable = pMainTreeRT->GetLeafBillboardTable(uiLeafTableFloatCount);
		const UINT uiLeafTableEntryCount = uiLeafTableFloatCount / 4u;
		const int iLeafLodCount = pMainTreeRT->GetNumLeafLodLevels();
		if (pLeafTable && uiLeafTableEntryCount > 0u)
			UpdateDX11SpeedTreeLeafPlacementTable(pContext, m_pDX11SpeedTreeLeafPlacementBuffer, pLeafTable, uiLeafTableFloatCount);

		// Pull current geometry first so cache follows active leaf LOD/signature.
		CSpeedTreeRT::SGeometry kGeometry;
		ZeroMemory(&kGeometry, sizeof(kGeometry));
		pMainTreeRT->GetGeometry(kGeometry, SpeedTree_LeafGeometry);

		const CSpeedTreeRT::SGeometry::SLeaf* apLeafGroups[2] =
		{
			&kGeometry.m_sLeaves0,
			&kGeometry.m_sLeaves1
		};
		const SSpeedTreeLeafCacheSignature kCurrentSig = {
			kGeometry.m_sLeaves0.m_nDiscreteLodLevel,
			kGeometry.m_sLeaves1.m_nDiscreteLodLevel,
			kGeometry.m_sLeaves0.m_usLeafCount,
			kGeometry.m_sLeaves1.m_usLeafCount,
			kGeometry.m_sLeaves0.m_bIsActive,
			kGeometry.m_sLeaves1.m_bIsActive,
			uiLeafTableEntryCount
		};

		// Check if buffers are cached for this tree type (leaf rendering uses VB only, no IB)
		std::vector<SSpeedTreeLeafDrawCall> vLeafDrawCalls;

		auto itVB = s_mapLeafVB.find(pMainTreeRaw);
		auto itStrips = s_mapLeafStrips.find(pMainTreeRaw);
		auto itSig = s_mapLeafCacheSig.find(pMainTreeRaw);

		ID3D11Buffer* pVB = nullptr;
		const bool bHaveCached = (itVB != s_mapLeafVB.end() && itStrips != s_mapLeafStrips.end() && itSig != s_mapLeafCacheSig.end());
		if (bHaveCached &&
			itSig->second.lod0 == kCurrentSig.lod0 &&
			itSig->second.lod1 == kCurrentSig.lod1 &&
			itSig->second.leafCount0 == kCurrentSig.leafCount0 &&
			itSig->second.leafCount1 == kCurrentSig.leafCount1 &&
			itSig->second.active0 == kCurrentSig.active0 &&
			itSig->second.active1 == kCurrentSig.active1 &&
			itSig->second.leafTableEntryCount == kCurrentSig.leafTableEntryCount)
		{
			// Use cached buffer
			pVB = itVB->second;
			vLeafDrawCalls = itStrips->second;
		}
		else
		{
			// LOD/signature changed: drop stale cache for this tree type.
			if (itVB != s_mapLeafVB.end())
			{
				if (itVB->second)
					itVB->second->Release();
				s_mapLeafVB.erase(itVB);
			}
			if (itStrips != s_mapLeafStrips.end())
				s_mapLeafStrips.erase(itStrips);
			if (itSig != s_mapLeafCacheSig.end())
				s_mapLeafCacheSig.erase(itSig);

			std::vector<SLeafVertexDX11> vAllVertices;

			for (int iLeafGroup = 0; iLeafGroup < 2; ++iLeafGroup)
			{
				const CSpeedTreeRT::SGeometry::SLeaf& rLeaf = *apLeafGroups[iLeafGroup];
				if (!rLeaf.m_bIsActive || rLeaf.m_usLeafCount == 0 ||
					!rLeaf.m_pCenterCoords || !rLeaf.m_pLeafMapCoords || !rLeaf.m_pLeafMapTexCoords || !rLeaf.m_pColors)
					continue;

				const UINT uStartVertex = static_cast<UINT>(vAllVertices.size());
				const UINT uLeafVertexCount = static_cast<UINT>(rLeaf.m_usLeafCount) * 6u;

				vAllVertices.resize(vAllVertices.size() + uLeafVertexCount);
				size_t uOut = uStartVertex;

				for (UINT iLeaf = 0; iLeaf < rLeaf.m_usLeafCount; ++iLeaf)
				{
					const float* pCenter = &(rLeaf.m_pCenterCoords[iLeaf * 3]);
					const float* pMapCoords = rLeaf.m_pLeafMapCoords[iLeaf];
					const float* pMapTex = rLeaf.m_pLeafMapTexCoords[iLeaf];
					const DWORD dwColor = ConvertD3DColorToRGBA(rLeaf.m_pColors[iLeaf]);
					const UINT uiLeafClusterIndex = rLeaf.m_pLeafClusterIndices ? rLeaf.m_pLeafClusterIndices[iLeaf] : 0u;
					const float* pLeafLodAdjustments = pMainTreeRT->GetLeafLodSizeAdjustments();
					float fLeafScalar = 1.0f;
					if (pLeafLodAdjustments && rLeaf.m_nDiscreteLodLevel >= 0 && rLeaf.m_nDiscreteLodLevel < iLeafLodCount)
						fLeafScalar = pLeafLodAdjustments[rLeaf.m_nDiscreteLodLevel];

					for (int iVert = 0; iVert < 6; ++iVert)
					{
						const int iLeafCorner = s_anLeafTriListIndices[iVert];
						const int iCoordBase = iLeafCorner * 4;
						const int iTexBase = iLeafCorner * 2;

						SLeafVertexDX11& rv = vAllVertices[uOut++];
						rv.x = pCenter[0];
						rv.y = pCenter[1];
						rv.z = pCenter[2];
						rv.color = dwColor;
						rv.u = pMapTex[iTexBase + 0];
						rv.v = pMapTex[iTexBase + 1];
						const UINT uiPlacementIndex = (uiLeafClusterIndex * 4u) + static_cast<UINT>(iLeafCorner);
						if (rLeaf.m_pLeafClusterIndices && uiPlacementIndex < uiLeafTableEntryCount)
						{
							rv.leafPlacementIndex = static_cast<float>(uiPlacementIndex);
							rv.leafScalar = fLeafScalar;
						}
						else
						{
							// Fallback to CPU-placed offset when placement table is unavailable/out-of-range.
							rv.x = pCenter[0] + pMapCoords[iCoordBase + 0];
							rv.y = pCenter[1] + pMapCoords[iCoordBase + 1];
							rv.z = pCenter[2] + pMapCoords[iCoordBase + 2];
							rv.leafPlacementIndex = 0.0f;
							rv.leafScalar = 0.0f;
						}
					}
				}

				SSpeedTreeLeafDrawCall drawCall;
				drawCall.vertexCount = uLeafVertexCount;
				drawCall.startVertex = uStartVertex;
				drawCall.alphaRef = rLeaf.m_fAlphaTestValue;
				vLeafDrawCalls.push_back(drawCall);
			}

			if (vAllVertices.empty())
				continue;

			// Create IMMUTABLE buffer (per-tree-type, not per-instance)
			D3D11_BUFFER_DESC vbDesc = {};
			vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
			vbDesc.ByteWidth = static_cast<UINT>(vAllVertices.size() * sizeof(SLeafVertexDX11));
			vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vbData = {};
			vbData.pSysMem = vAllVertices.data();
			if (FAILED(pDevice->CreateBuffer(&vbDesc, &vbData, &pVB)) || !pVB)
				continue;

			// Cache buffer for this tree type
			s_mapLeafVB[pMainTreeRaw] = pVB;
			s_mapLeafStrips[pMainTreeRaw] = vLeafDrawCalls;
			s_mapLeafCacheSig[pMainTreeRaw] = kCurrentSig;
		}

		if (!pVB || vLeafDrawCalls.empty())
			continue;

		// Bind buffer once per tree type
		UINT uStride = sizeof(SLeafVertexDX11);
		UINT uOffset = 0;
		pContext->IASetVertexBuffers(0, 1, &pVB, &uStride, &uOffset);

		// M3-SPEEDTREE-ATLAS-09: Bind texture once per tree type (not per instance)
		// This optimization moves texture binding outside the inner loop
		ID3D11ShaderResourceView* pTreeSRV = __GetTreeTextureSRV(pMainTree.get());
		if (!pTreeSRV)
			continue;  // Skip this tree type if texture unavailable
		pContext->PSSetShaderResources(0, 1, &pTreeSRV);
		++dwTextureBinds;

		// W4.3: Per-instance loop (inner loop) - draw with different tree positions
		UINT uiCount = 0;
		auto ppInstances = pMainTree->GetInstances(uiCount);
		for (const auto& pTreeInst : ppInstances)
		{
			if (!pTreeInst || (!m_bDX11ShadowViewProjOverrideActive && !pTreeInst->isShow()))
				continue;

			const float* pTreePos = pTreeInst->GetPosition();
			SLeafConstants kConstants;
			ZeroMemory(&kConstants, sizeof(kConstants));
			kConstants.matViewProj = matViewProjShader;
			kConstants.vTreePos = DirectX::SimpleMath::Vector4(
				pTreePos ? pTreePos[0] : 0.0f,
				pTreePos ? pTreePos[1] : 0.0f,
				pTreePos ? pTreePos[2] : 0.0f,
				0.0f);
			kConstants.vLightDir = DirectX::SimpleMath::Vector4(m_afLighting[0], m_afLighting[1], m_afLighting[2], 0.0f);
			pContext->UpdateSubresource(m_pDX11SpeedTreeConstantBuffer, 0, nullptr, &kConstants, 0, 0);

			// Draw all leaf groups for this instance
			for (size_t i = 0; i < vLeafDrawCalls.size(); ++i)
			{
				UpdateDX11SpeedTreeAlphaRef(pContext, m_pDX11SpeedTreeAlphaRefBuffer, vLeafDrawCalls[i].alphaRef);
				pContext->Draw(vLeafDrawCalls[i].vertexCount, vLeafDrawCalls[i].startVertex);
				pGrpDevice->IncrementFrameDrawCalls(1u, static_cast<UINT>(vLeafDrawCalls[i].vertexCount / 3u));
				++dwSubmittedDrawCalls;
				ullSubmittedPrimitives += static_cast<UINT64>(vLeafDrawCalls[i].vertexCount / 3u);
			}
		}
	}

	// Cleanup
	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);
	ID3D11Buffer* pNullVSCB = nullptr;
	pContext->VSSetConstantBuffers(2, 1, &pNullVSCB);
	ID3D11Buffer* pNullCB = nullptr;
	pContext->PSSetConstantBuffers(1, 1, &pNullCB);
	pContext->OMSetBlendState(nullptr, afBlendFactor, 0xffffffffu);

	if (dwSubmittedDrawCalls > 0)
		AddDX11SubmittedInstanceCount(dwSubmittedDrawCalls, ullSubmittedPrimitives);

	// M3-SPEEDTREE-ATLAS-09: Log texture binding telemetry (periodic)
	static DWORD s_dwLastLeafBindLog = 0;
	static DWORD s_dwLeafBindSum = 0;
	static DWORD s_dwLeafBindFrames = 0;
	s_dwLeafBindSum += dwTextureBinds;
	++s_dwLeafBindFrames;
	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwLastLeafBindLog >= 5000)
	{
		TraceError("DX11_SPEEDTREE_TEXTURE_BINDS type=leaf binds_per_frame=%u frames=%u",
			s_dwLeafBindFrames > 0 ? (s_dwLeafBindSum / s_dwLeafBindFrames) : 0,
			s_dwLeafBindFrames);
		s_dwLastLeafBindLog = dwNow;
		s_dwLeafBindSum = 0;
		s_dwLeafBindFrames = 0;
	}
}
