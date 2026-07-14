#include "StdAfx.h"
#include "GrpDeviceDX11.h"
#include "GrpBase.h"
#include "EterBase/Utils.h"
#include "EterBase/Timer.h"
#include "DirectXHelpers.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#ifdef _DEBUG
#include <d3d11sdklayers.h>
#endif

#if defined(BUILD_DEBUG_UI)
#include "DebugUI/ImGuiManager.h"
#include "DebugUI/ImGuiGraphicsMetrics.h"
#endif
// M2-D3DX-CORE-CUT-58 Phase 2: StateManager11 for DX11 state management
#include "StateManager11.h"

CGraphicDeviceDX11* CGraphicDeviceDX11::ms_pActiveDevice = NULL;

namespace
{
	CStateManager11* __GetOrCreateStateManager11()
	{
		CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
		if (pStateManager11)
			return pStateManager11;

		// Function-local static keeps deterministic construction on first DX11 use.
		static CStateManager11 s_kStateManager11;
		return &s_kStateManager11;
	}

	bool __IsDX11SwapchainLegacyFallbackEnabled()
	{
		char szBuffer[16] = { 0 };
		const DWORD dwLen = GetEnvironmentVariableA("DX11_SWAPCHAIN_LEGACY_FALLBACK", szBuffer, static_cast<DWORD>(_countof(szBuffer)));
		if (0 == dwLen || dwLen >= _countof(szBuffer))
			return false;

		const char c = szBuffer[0];
		return ('1' == c || 'y' == c || 'Y' == c || 't' == c || 'T' == c);
	}

	void __LogDX11HResultFailure(const char* c_szOperation, HRESULT hResult)
	{
		LPSTR szMessageBuffer = NULL;
		const DWORD dwFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
		const DWORD dwMessageLen = FormatMessageA(
			dwFlags,
			NULL,
			static_cast<DWORD>(hResult),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPSTR>(&szMessageBuffer),
			0,
			NULL);

		if (dwMessageLen > 0 && szMessageBuffer)
		{
			// Trim trailing CR/LF/space from FormatMessage payload for compact one-line telemetry.
			size_t stTrimLen = strlen(szMessageBuffer);
			while (stTrimLen > 0)
			{
				const char cLast = szMessageBuffer[stTrimLen - 1];
				if ('\r' != cLast && '\n' != cLast && ' ' != cLast && '\t' != cLast)
					break;
				szMessageBuffer[stTrimLen - 1] = '\0';
				--stTrimLen;
			}

			TraceError("DX11_HRESULT_FAIL op=%s hr=0x%08x msg=%s", c_szOperation, static_cast<unsigned int>(hResult), szMessageBuffer);
			LocalFree(szMessageBuffer);
			return;
		}

		TraceError("DX11_HRESULT_FAIL op=%s hr=0x%08x", c_szOperation, static_cast<unsigned int>(hResult));
	}

#ifdef _DEBUG
	void __ConfigureDX11DebugInfoQueue(ID3D11Device* pDevice)
	{
		if (!pDevice)
			return;

		ID3D11InfoQueue* pInfoQueue = nullptr;
		if (FAILED(pDevice->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&pInfoQueue))) || !pInfoQueue)
			return;

		pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);

		// Debug-only stability: silence high-volume warning emitted by depth-only passes.
		static D3D11_MESSAGE_ID s_aeSuppressedMessageIds[] =
		{
			D3D11_MESSAGE_ID_DEVICE_DRAW_RENDERTARGETVIEW_NOT_SET,
		};
		D3D11_INFO_QUEUE_FILTER kFilter = {};
		kFilter.DenyList.NumIDs = static_cast<UINT>(_countof(s_aeSuppressedMessageIds));
		kFilter.DenyList.pIDList = s_aeSuppressedMessageIds;
		pInfoQueue->AddStorageFilterEntries(&kFilter);

		TraceError(
			"DX11_DEBUG_INFOQUEUE_CONFIG filter=applied suppressed_rtv_not_set=%u",
			static_cast<unsigned int>(_countof(s_aeSuppressedMessageIds)));

		pInfoQueue->Release();
	}
#endif

	std::string __FormatWorldMaskTokens(uint32_t dwMask)
	{
		std::string stTokens;
		auto __Append = [&](const char* c_szName)
		{
			if (!stTokens.empty())
				stTokens += "|";
			stTokens += c_szName;
		};

		if (0u != (dwMask & CGraphicDeviceDX11::WORLD_TERRAIN_DX11))
			__Append("terrain");
		if (0u != (dwMask & CGraphicDeviceDX11::WORLD_OBJECTS_DX11))
			__Append("objects");
		if (0u != (dwMask & CGraphicDeviceDX11::WORLD_EFFECTS_DX11))
			__Append("effects");
		if (0u != (dwMask & CGraphicDeviceDX11::WORLD_SPEEDTREE_DX11))
			__Append("speedtree");
		if (0u != (dwMask & CGraphicDeviceDX11::WORLD_WATER_DX11))
			__Append("water");

		if (stTokens.empty())
			stTokens = "none";
		return stTokens;
	}

	const char* __DX11TopologyToToken(D3D11_PRIMITIVE_TOPOLOGY eTopology)
	{
		switch (eTopology)
		{
		case D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED:
			return "undefined";
		case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
			return "pointlist";
		case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
			return "linelist";
		case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
			return "linestrip";
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
			return "trianglelist";
		case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
			return "trianglestrip";
		default:
			return "other";
		}
	}

	void __LogDX11PresentTripwireSnapshot(
		ID3D11DeviceContext* pContext,
		ID3D11RenderTargetView* pStoredRTV,
		ID3D11DepthStencilView* pStoredDSV,
		int iScenePatches,
		int iSceneTextures,
		int iSceneSplat,
		float fAvgR,
		float fAvgG,
		float fAvgB,
		float fVariance,
		float fMaxChannel,
		DWORD dwRepeatCount)
	{
		if (!pContext)
			return;

		ID3D11RenderTargetView* apBoundRTV[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { nullptr };
		ID3D11DepthStencilView* pBoundDSV = nullptr;
		pContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, apBoundRTV, &pBoundDSV);

		D3D11_VIEWPORT kViewport = {};
		UINT uViewportCount = 1;
		pContext->RSGetViewports(&uViewportCount, &kViewport);

		ID3D11BlendState* pBlendState = nullptr;
		FLOAT afBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
		UINT uSampleMask = 0u;
		pContext->OMGetBlendState(&pBlendState, afBlendFactor, &uSampleMask);

		ID3D11DepthStencilState* pDepthState = nullptr;
		UINT uStencilRef = 0u;
		pContext->OMGetDepthStencilState(&pDepthState, &uStencilRef);

		ID3D11RasterizerState* pRasterizerState = nullptr;
		pContext->RSGetState(&pRasterizerState);

		D3D11_PRIMITIVE_TOPOLOGY eTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		pContext->IAGetPrimitiveTopology(&eTopology);

		ID3D11InputLayout* pInputLayout = nullptr;
		pContext->IAGetInputLayout(&pInputLayout);

		ID3D11VertexShader* pVS = nullptr;
		ID3D11PixelShader* pPS = nullptr;
		ID3D11GeometryShader* pGS = nullptr;
		pContext->VSGetShader(&pVS, nullptr, nullptr);
		pContext->PSGetShader(&pPS, nullptr, nullptr);
		pContext->GSGetShader(&pGS, nullptr, nullptr);

		ID3D11ShaderResourceView* apPSSRV[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		ID3D11SamplerState* apPSSampler[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		pContext->PSGetShaderResources(0, 8, apPSSRV);
		pContext->PSGetSamplers(0, 8, apPSSampler);

		int iBoundPSResourceSlots = 0;
		int iFirstBoundPSResourceSlot = -1;
		int iBoundPSSamplerSlots = 0;
		int iFirstBoundPSSamplerSlot = -1;
		for (int i = 0; i < 8; ++i)
		{
			if (apPSSRV[i])
			{
				++iBoundPSResourceSlots;
				if (iFirstBoundPSResourceSlot < 0)
					iFirstBoundPSResourceSlot = i;
			}
			if (apPSSampler[i])
			{
				++iBoundPSSamplerSlots;
				if (iFirstBoundPSSamplerSlot < 0)
					iFirstBoundPSSamplerSlot = i;
			}
		}

		TraceError(
			"DX11_PRESENT_BLACK_FRAME_SNAPSHOT repeats=%u scene_patches=%d scene_textures=%d scene_splat=%d "
			"avg_r=%.3f avg_g=%.3f avg_b=%.3f variance=%.3f max_channel=%.3f "
			"bound_rtv0=%d bound_dsv=%d stored_rtv=%d stored_dsv=%d viewport_w=%.0f viewport_h=%.0f "
			"blend=%d depth=%d raster=%d il=%d topo=%s vs=%d ps=%d gs=%d ps_srv_slots=%d ps_srv_first=%d ps_sampler_slots=%d ps_sampler_first=%d",
			static_cast<unsigned int>(dwRepeatCount),
			iScenePatches,
			iSceneTextures,
			iSceneSplat,
			fAvgR,
			fAvgG,
			fAvgB,
			fVariance,
			fMaxChannel,
			apBoundRTV[0] ? 1 : 0,
			pBoundDSV ? 1 : 0,
			pStoredRTV ? 1 : 0,
			pStoredDSV ? 1 : 0,
			(uViewportCount > 0) ? kViewport.Width : 0.0f,
			(uViewportCount > 0) ? kViewport.Height : 0.0f,
			pBlendState ? 1 : 0,
			pDepthState ? 1 : 0,
			pRasterizerState ? 1 : 0,
			pInputLayout ? 1 : 0,
			__DX11TopologyToToken(eTopology),
			pVS ? 1 : 0,
			pPS ? 1 : 0,
			pGS ? 1 : 0,
			iBoundPSResourceSlots,
			iFirstBoundPSResourceSlot,
			iBoundPSSamplerSlots,
			iFirstBoundPSSamplerSlot);

		for (int i = 0; i < 8; ++i)
		{
			if (apPSSRV[i])
				apPSSRV[i]->Release();
			if (apPSSampler[i])
				apPSSampler[i]->Release();
		}
		if (pVS)
			pVS->Release();
		if (pPS)
			pPS->Release();
		if (pGS)
			pGS->Release();
		if (pInputLayout)
			pInputLayout->Release();
		if (pRasterizerState)
			pRasterizerState->Release();
		if (pDepthState)
			pDepthState->Release();
		if (pBlendState)
			pBlendState->Release();
		for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		{
			if (apBoundRTV[i])
				apBoundRTV[i]->Release();
		}
		if (pBoundDSV)
			pBoundDSV->Release();
	}
}

CGraphicDeviceDX11::CGraphicDeviceDX11()
	: m_hWnd(NULL)
	, m_uWidth(0)
	, m_uHeight(0)
	, m_isWindowed(true)
	, m_isVSyncEnabled(true)
	, m_pSwapChain(NULL)
	, m_pDevice(NULL)
	, m_pDeviceContext(NULL)
	, m_pRenderTargetView(NULL)
	, m_pDepthStencilBuffer(NULL)
	, m_pDepthStencilView(NULL)
	, m_pBootstrapVertexShader(NULL)
	, m_pBootstrapPixelShader(NULL)
	, m_pBootstrapUIPixelShader(NULL)
	, m_pBootstrapInputLayout(NULL)
	, m_pBootstrapVertexBuffer(NULL)
	, m_pBootstrapAlphaBlendState(NULL)
	, m_pBootstrapUICloudBlendState(NULL)
	, m_pBootstrapUIScreenBlendState(NULL)
	, m_pBootstrapUIModulateBlendState(NULL)
	, m_pBootstrapUIAdditiveBlendState(NULL)
	, m_pBootstrapAdditiveBlendState(NULL)
	, m_pBootstrapLCDPass1BlendState(NULL)
	, m_pBootstrapLCDPass2BlendState(NULL)
	, m_pBootstrapDepthEnableState(NULL)
	, m_pBootstrapDepthReadState(NULL)
	, m_pBootstrapDepthDisableState(NULL)
	, m_pBootstrapRasterizerState(NULL)
	, m_pBootstrapUITexture(NULL)
	, m_pBootstrapUITextureSRV(NULL)
	, m_pVisibleBridgeTexture(NULL)
	, m_pVisibleBridgeTextureSRV(NULL)
	, m_pBootstrapUISamplerState(NULL)
	, m_isBootstrapPipelineReady(false)
	, m_isBootstrapUITextureReady(false)
	, m_uVisibleBridgeWidth(0)
	, m_uVisibleBridgeHeight(0)
	, m_iNativeWorldScenePatchCount(0)
	, m_iNativeWorldSceneSplatCount(0)
	, m_fNativeWorldSceneSplatRatio(0.0f)
	, m_iNativeWorldSceneTextureCount(0)
	, m_dwNativeWorldSceneThingInstances(0)
	, m_dwNativeWorldSceneCRCCount(0)
	, m_dwNativeWorldObservedMask(0u)
	, m_dwNativeWorldSubmittedMask(0u)
	, m_dwNativeWorldApplicableMask(0u)
	, m_dwNativeWorldSubmittedSeenMask(0u)
	, m_dwNativeWorldCommittedMask(0u)
	, m_bUsingNativeWorldPresentPath(false)
	, m_uFrameDrawCalls(0)
	, m_uFramePrimitiveCount(0)
	, m_eDX11TexturePipelineMode(DX11_TEXTURE_PIPELINE_NATIVE)
	, m_eFeatureLevel(D3D_FEATURE_LEVEL_11_0)
{
	ZeroMemory(&m_kViewport, sizeof(m_kViewport));
	ZeroMemory(m_apBootstrapTextureStageSRV, sizeof(m_apBootstrapTextureStageSRV));
}

CGraphicDeviceDX11::~CGraphicDeviceDX11()
{
	Destroy();
}

void CGraphicDeviceDX11::IncrementFrameDrawCalls(UINT uDrawCalls, UINT uPrimitiveCount)
{
	if (0u == uDrawCalls)
		return;
	m_uFrameDrawCalls += uDrawCalls;
	m_uFramePrimitiveCount += uPrimitiveCount;
}

bool CGraphicDeviceDX11::Create(HWND hWnd, UINT uWidth, UINT uHeight, bool isWindowed, bool isVSyncEnabled)
{
	Destroy();

	m_hWnd = hWnd;
	m_uWidth = uWidth;
	m_uHeight = uHeight;
	m_isWindowed = isWindowed;
	m_isVSyncEnabled = isVSyncEnabled;
	CGraphicBase::SetBackBufferSize(m_uWidth, m_uHeight);

	DXGI_SWAP_CHAIN_DESC kSwapChainDesc;
	ZeroMemory(&kSwapChainDesc, sizeof(kSwapChainDesc));
	kSwapChainDesc.BufferCount = 2;
	kSwapChainDesc.BufferDesc.Width = m_uWidth;
	kSwapChainDesc.BufferDesc.Height = m_uHeight;
	kSwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	kSwapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
	kSwapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	kSwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	kSwapChainDesc.OutputWindow = m_hWnd;
	kSwapChainDesc.SampleDesc.Count = 1;
	kSwapChainDesc.SampleDesc.Quality = 0;
	kSwapChainDesc.Windowed = m_isWindowed ? TRUE : FALSE;
	kSwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	kSwapChainDesc.Flags = 0;

	D3D_FEATURE_LEVEL aeFeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};

	UINT uCreateFlags = 0;
#ifdef _DEBUG
	uCreateFlags |= D3D11_CREATE_DEVICE_DEBUG;
#else
	// Release build: no debug layer
#endif
	HRESULT hResult = D3D11CreateDeviceAndSwapChain(
		NULL,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		uCreateFlags,
		aeFeatureLevels,
		sizeof(aeFeatureLevels) / sizeof(aeFeatureLevels[0]),
		D3D11_SDK_VERSION,
		&kSwapChainDesc,
		&m_pSwapChain,
		&m_pDevice,
		&m_eFeatureLevel,
		&m_pDeviceContext);
	if (FAILED(hResult))
	{
		const HRESULT hFlipCreateResult = hResult;
		if (__IsDX11SwapchainLegacyFallbackEnabled())
		{
			// Explicit dev-only escape hatch. Default runtime stays flip-model only.
			kSwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
			hResult = D3D11CreateDeviceAndSwapChain(
				NULL,
				D3D_DRIVER_TYPE_HARDWARE,
				NULL,
				uCreateFlags,
				aeFeatureLevels,
				sizeof(aeFeatureLevels) / sizeof(aeFeatureLevels[0]),
				D3D11_SDK_VERSION,
				&kSwapChainDesc,
				&m_pSwapChain,
				&m_pDevice,
				&m_eFeatureLevel,
				&m_pDeviceContext);
			if (SUCCEEDED(hResult))
			{
				TraceError(
					"DX11_SWAPCHAIN_MODE mode=discard reason=flip_sequential_unavailable legacy_fallback=env_on flip_hr=0x%08x",
					static_cast<unsigned int>(hFlipCreateResult));
			}
		}
		else
		{
			TraceError(
				"DX11_SWAPCHAIN_MODE mode=flip_sequential_only reason=legacy_fallback_disabled flip_hr=0x%08x",
				static_cast<unsigned int>(hFlipCreateResult));
		}
	}
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("D3D11CreateDeviceAndSwapChain", hResult);
		return false;
	}

#ifdef _DEBUG
	__ConfigureDX11DebugInfoQueue(m_pDevice);
#endif

	if (!__CreateRenderTarget())
		return false;

	// M2-D3DX-CORE-CUT-58 Phase 2: Initialize StateManager11 with DX11 device
	// This must be done after device creation but before setting as active
	__GetOrCreateStateManager11()->Initialize(this);

	ms_pActiveDevice = this;
	return true;
}

void CGraphicDeviceDX11::Destroy()
{
	// M2-D3DX-CORE-CUT-58 Phase 2: Release StateManager11 before device destruction
	if (CStateManager11* pStateManager11 = CStateManager11::InstancePtr())
		pStateManager11->Release();

	__DestroyVisibleBridgeTexture();
	__DestroyBootstrapUITexture();
	__DestroyBootstrapPipeline();
	__DestroyRenderTarget();
	SAFE_RELEASE(m_pSwapChain);
	SAFE_RELEASE(m_pDeviceContext);
	SAFE_RELEASE(m_pDevice);

	m_hWnd = NULL;
	m_uWidth = 0;
	m_uHeight = 0;
	CGraphicBase::SetBackBufferSize(0, 0);
	m_bUsingNativeWorldPresentPath = false;
	ZeroMemory(m_apBootstrapTextureStageSRV, sizeof(m_apBootstrapTextureStageSRV));

	if (ms_pActiveDevice == this)
		ms_pActiveDevice = NULL;
}

bool CGraphicDeviceDX11::Resize(UINT uWidth, UINT uHeight)
{
	if (!m_pSwapChain || !m_pDeviceContext)
		return false;

	if (0 == uWidth || 0 == uHeight)
		return true;

	m_uWidth = uWidth;
	m_uHeight = uHeight;
	CGraphicBase::SetBackBufferSize(m_uWidth, m_uHeight);

	__DestroyVisibleBridgeTexture();
	__DestroyRenderTarget();

	HRESULT hResult = m_pSwapChain->ResizeBuffers(0, m_uWidth, m_uHeight, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("IDXGISwapChain::ResizeBuffers", hResult);
		return false;
	}

	return __CreateRenderTarget();
}

bool CGraphicDeviceDX11::BeginFrame(float fR, float fG, float fB, float fA)
{
	if (!m_pDeviceContext)
		return false;

	if (!m_pRenderTargetView)
	{
		// Recover from transient RTV loss (resize/reset race) without forcing higher-level frame abort.
		// Strict DX11 mode depends on BeginFrame success to reach Present.
		if (!__CreateRenderTarget())
		{
			static DWORD s_dwBeginFrameMissingRTVLogTick = 0;
			const DWORD dwNow = GetTickCount();
			if (0 == s_dwBeginFrameMissingRTVLogTick || dwNow - s_dwBeginFrameMissingRTVLogTick >= 2000u)
			{
				s_dwBeginFrameMissingRTVLogTick = dwNow;
				TraceError(
					"DX11_BEGIN_FRAME_FAIL reason=missing_rtv_recreate_failed swapchain=%d device=%d context=%d width=%u height=%u",
					m_pSwapChain ? 1 : 0,
					m_pDevice ? 1 : 0,
					m_pDeviceContext ? 1 : 0,
					m_uWidth,
					m_uHeight);
			}
			return false;
		}
	}

	// Texture stage cache is frame-local; avoid stale SRV pointers across frames.
	ZeroMemory(m_apBootstrapTextureStageSRV, sizeof(m_apBootstrapTextureStageSRV));

	// DX11 Model Sync: Reset frame draw call counter
	m_uFrameDrawCalls = 0;
	m_uFramePrimitiveCount = 0;
	if (CStateManager11* pStateManager11 = CStateManager11::InstancePtr())
		pStateManager11->ResetFrameDiagnostics();

	#if defined(BUILD_DEBUG_UI)
	// Reset optional diagnostics stats at start of frame.
	extern void ResetImGuiSubsystemStats();
	ResetImGuiSubsystemStats();
	#endif

	const float afClearColor[4] = { fR, fG, fB, fA };
	m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);
	m_pDeviceContext->RSSetViewports(1, &m_kViewport);
	m_pDeviceContext->ClearRenderTargetView(m_pRenderTargetView, afClearColor);
	if (m_pDepthStencilView)
		m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	return true;
}

void CGraphicDeviceDX11::SetBootstrapTextureStageSRV(UINT uStage, ID3D11ShaderResourceView* pSRV)
{
	if (uStage >= _countof(m_apBootstrapTextureStageSRV))
		return;
	m_apBootstrapTextureStageSRV[uStage] = pSRV;
}

ID3D11ShaderResourceView* CGraphicDeviceDX11::GetBootstrapTextureStageSRV(UINT uStage) const
{
	if (uStage >= _countof(m_apBootstrapTextureStageSRV))
		return NULL;
	return m_apBootstrapTextureStageSRV[uStage];
}

bool CGraphicDeviceDX11::DrawBootstrapTriangle()
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;

	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const SBootstrapVertex akTriangleVertices[3] =
	{
		{  0.0f,  0.45f, 0.0f, 1.0f, 0.20f, 0.20f, 1.0f, 0.5f, 0.0f },
		{  0.35f, -0.25f, 0.0f, 0.20f, 1.0f, 0.20f, 1.0f, 1.0f, 1.0f },
		{ -0.35f, -0.25f, 0.0f, 0.20f, 0.45f, 1.0f, 1.0f, 0.0f, 1.0f },
	};

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hMapResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(bootstrap_vertex_buffer)", hMapResult);
		if (SUCCEEDED(hMapResult) && !kMappedResource.pData)
			TraceError("DX11_MAP_NULL_DATA op=ID3D11DeviceContext::Map(bootstrap_vertex_buffer)");
		return false;
	}

	memcpy(kMappedResource.pData, akTriangleVertices, sizeof(akTriangleVertices));
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapPixelShader, NULL, 0);
	m_pDeviceContext->Draw(3, 0);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((3) / 3);
	return true;
}

bool CGraphicDeviceDX11::DrawBootstrapUIOverlay(float fCursorX, float fCursorY)
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;
	if (0 == m_uWidth || 0 == m_uHeight)
		return false;

	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	auto PixelToNDCX = [this](float x) -> float
	{
		return (2.0f * x / static_cast<float>(m_uWidth)) - 1.0f;
	};
	auto PixelToNDCY = [this](float y) -> float
	{
		return 1.0f - (2.0f * y / static_cast<float>(m_uHeight));
	};

	const float fClampedCursorX = std::max(0.0f, std::min(fCursorX, static_cast<float>(m_uWidth - 1)));
	const float fClampedCursorY = std::max(0.0f, std::min(fCursorY, static_cast<float>(m_uHeight - 1)));

	const float fBarLeft = 24.0f;
	const float fBarTop = 24.0f;
	const float fBarRight = std::max(fBarLeft + 32.0f, static_cast<float>(m_uWidth) - 24.0f);
	const float fBarBottom = 56.0f;

	const float fCursorHalfSize = 8.0f;
	const float fCursorLeft = std::max(0.0f, fClampedCursorX - fCursorHalfSize);
	const float fCursorTop = std::max(0.0f, fClampedCursorY - fCursorHalfSize);
	const float fCursorRight = std::min(static_cast<float>(m_uWidth), fClampedCursorX + fCursorHalfSize);
	const float fCursorBottom = std::min(static_cast<float>(m_uHeight), fClampedCursorY + fCursorHalfSize);

	SBootstrapVertex akVertices[15];
	akVertices[0] = {  0.0f,  0.45f, 0.0f, 1.0f, 0.20f, 0.20f, 1.0f, 0.5f, 0.0f };
	akVertices[1] = {  0.35f, -0.25f, 0.0f, 0.20f, 1.0f, 0.20f, 1.0f, 1.0f, 1.0f };
	akVertices[2] = { -0.35f, -0.25f, 0.0f, 0.20f, 0.45f, 1.0f, 1.0f, 0.0f, 1.0f };

	akVertices[3] = { PixelToNDCX(fBarLeft),  PixelToNDCY(fBarTop),    0.0f, 0.14f, 0.60f, 1.00f, 1.0f, 0.0f, 0.0f };
	akVertices[4] = { PixelToNDCX(fBarRight), PixelToNDCY(fBarTop),    0.0f, 0.14f, 0.60f, 1.00f, 1.0f, 1.0f, 0.0f };
	akVertices[5] = { PixelToNDCX(fBarLeft),  PixelToNDCY(fBarBottom), 0.0f, 0.08f, 0.23f, 0.52f, 1.0f, 0.0f, 1.0f };
	akVertices[6] = { PixelToNDCX(fBarLeft),  PixelToNDCY(fBarBottom), 0.0f, 0.08f, 0.23f, 0.52f, 1.0f, 0.0f, 1.0f };
	akVertices[7] = { PixelToNDCX(fBarRight), PixelToNDCY(fBarTop),    0.0f, 0.14f, 0.60f, 1.00f, 1.0f, 1.0f, 0.0f };
	akVertices[8] = { PixelToNDCX(fBarRight), PixelToNDCY(fBarBottom), 0.0f, 0.08f, 0.23f, 0.52f, 1.0f, 1.0f, 1.0f };

	akVertices[9]  = { PixelToNDCX(fCursorLeft),  PixelToNDCY(fCursorTop),    0.0f, 1.00f, 0.92f, 0.20f, 1.0f, 0.0f, 0.0f };
	akVertices[10] = { PixelToNDCX(fCursorRight), PixelToNDCY(fCursorTop),    0.0f, 1.00f, 0.92f, 0.20f, 1.0f, 1.0f, 0.0f };
	akVertices[11] = { PixelToNDCX(fCursorLeft),  PixelToNDCY(fCursorBottom), 0.0f, 0.80f, 0.65f, 0.10f, 1.0f, 0.0f, 1.0f };
	akVertices[12] = { PixelToNDCX(fCursorLeft),  PixelToNDCY(fCursorBottom), 0.0f, 0.80f, 0.65f, 0.10f, 1.0f, 0.0f, 1.0f };
	akVertices[13] = { PixelToNDCX(fCursorRight), PixelToNDCY(fCursorTop),    0.0f, 1.00f, 0.92f, 0.20f, 1.0f, 1.0f, 0.0f };
	akVertices[14] = { PixelToNDCX(fCursorRight), PixelToNDCY(fCursorBottom), 0.0f, 0.80f, 0.65f, 0.10f, 1.0f, 1.0f, 1.0f };

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hMapResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(bootstrap_vertex_buffer)", hMapResult);
		if (SUCCEEDED(hMapResult) && !kMappedResource.pData)
			TraceError("DX11_MAP_NULL_DATA op=ID3D11DeviceContext::Map(bootstrap_vertex_buffer)");
		return false;
	}

	memcpy(kMappedResource.pData, akVertices, sizeof(akVertices));
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapPixelShader, NULL, 0);

	// Uses vertex range [3..14] from bootstrap VB (bar + cursor quad).
	m_pDeviceContext->Draw(12, 3);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((12) / 3);
	return true;
}

bool CGraphicDeviceDX11::DrawBootstrapUITextureOverlay(float fCursorX, float fCursorY)
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;
	if (0 == m_uWidth || 0 == m_uHeight)
		return false;

	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}
	if (!m_isBootstrapUITextureReady)
	{
		if (!__CreateBootstrapUITexture())
			return false;
	}

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	auto PixelToNDCX = [this](float x) -> float
	{
		return (2.0f * x / static_cast<float>(m_uWidth)) - 1.0f;
	};
	auto PixelToNDCY = [this](float y) -> float
	{
		return 1.0f - (2.0f * y / static_cast<float>(m_uHeight));
	};

	const float fClampedCursorX = std::max(0.0f, std::min(fCursorX, static_cast<float>(m_uWidth - 1)));
	const float fClampedCursorY = std::max(0.0f, std::min(fCursorY, static_cast<float>(m_uHeight - 1)));

	const float fBarLeft = 24.0f;
	const float fBarTop = 24.0f;
	const float fBarRight = std::max(fBarLeft + 32.0f, static_cast<float>(m_uWidth) - 24.0f);
	const float fBarBottom = 56.0f;

	const float fCursorHalfSize = 8.0f;
	const float fCursorLeft = std::max(0.0f, fClampedCursorX - fCursorHalfSize);
	const float fCursorTop = std::max(0.0f, fClampedCursorY - fCursorHalfSize);
	const float fCursorRight = std::min(static_cast<float>(m_uWidth), fClampedCursorX + fCursorHalfSize);
	const float fCursorBottom = std::min(static_cast<float>(m_uHeight), fClampedCursorY + fCursorHalfSize);

	SBootstrapVertex akVertices[15];
	akVertices[0] = {  0.0f,  0.45f, 0.0f, 1.0f, 0.20f, 0.20f, 1.0f, 0.5f, 0.0f };
	akVertices[1] = {  0.35f, -0.25f, 0.0f, 0.20f, 1.0f, 0.20f, 1.0f, 1.0f, 1.0f };
	akVertices[2] = { -0.35f, -0.25f, 0.0f, 0.20f, 0.45f, 1.0f, 1.0f, 0.0f, 1.0f };

	akVertices[3] = { PixelToNDCX(fBarLeft),  PixelToNDCY(fBarTop),    0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f };
	akVertices[4] = { PixelToNDCX(fBarRight), PixelToNDCY(fBarTop),    0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f };
	akVertices[5] = { PixelToNDCX(fBarLeft),  PixelToNDCY(fBarBottom), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	akVertices[6] = { PixelToNDCX(fBarLeft),  PixelToNDCY(fBarBottom), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	akVertices[7] = { PixelToNDCX(fBarRight), PixelToNDCY(fBarTop),    0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f };
	akVertices[8] = { PixelToNDCX(fBarRight), PixelToNDCY(fBarBottom), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

	akVertices[9]  = { PixelToNDCX(fCursorLeft),  PixelToNDCY(fCursorTop),    0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f };
	akVertices[10] = { PixelToNDCX(fCursorRight), PixelToNDCY(fCursorTop),    0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f };
	akVertices[11] = { PixelToNDCX(fCursorLeft),  PixelToNDCY(fCursorBottom), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	akVertices[12] = { PixelToNDCX(fCursorLeft),  PixelToNDCY(fCursorBottom), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	akVertices[13] = { PixelToNDCX(fCursorRight), PixelToNDCY(fCursorTop),    0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f };
	akVertices[14] = { PixelToNDCX(fCursorRight), PixelToNDCY(fCursorBottom), 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hMapResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(bootstrap_vertex_buffer)", hMapResult);
		if (SUCCEEDED(hMapResult) && !kMappedResource.pData)
			TraceError("DX11_MAP_NULL_DATA op=ID3D11DeviceContext::Map(bootstrap_vertex_buffer)");
		return false;
	}

	memcpy(kMappedResource.pData, akVertices, sizeof(akVertices));
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapUIPixelShader, NULL, 0);
	m_pDeviceContext->PSSetShaderResources(0, 1, &m_pBootstrapUITextureSRV);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pBootstrapUISamplerState);
	m_pDeviceContext->Draw(12, 3);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((12) / 3);
	ID3D11ShaderResourceView* pNullSRV = NULL;
	m_pDeviceContext->PSSetShaderResources(0, 1, &pNullSRV);

	return true;
}

bool CGraphicDeviceDX11::DrawBootstrapWorldDepthTest(float fTimeSec)
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;
	if (0 == m_uWidth || 0 == m_uHeight)
		return false;

	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const float fOffsetX = static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 1.4)) * 0.28f;
	const float fNearZ = 0.12f;
	const float fFarZ = 0.78f;

	SBootstrapVertex akVertices[15];
	akVertices[0] = {  0.0f,  0.45f, 0.0f, 1.0f, 0.20f, 0.20f, 1.0f, 0.5f, 0.0f };
	akVertices[1] = {  0.35f, -0.25f, 0.0f, 0.20f, 1.0f, 0.20f, 1.0f, 1.0f, 1.0f };
	akVertices[2] = { -0.35f, -0.25f, 0.0f, 0.20f, 0.45f, 1.0f, 1.0f, 0.0f, 1.0f };

	// Far quad (back layer).
	akVertices[3] = { -0.70f,  0.32f, fFarZ, 0.10f, 0.30f, 0.85f, 1.0f, 0.0f, 0.0f };
	akVertices[4] = {  0.40f,  0.32f, fFarZ, 0.10f, 0.30f, 0.85f, 1.0f, 1.0f, 0.0f };
	akVertices[5] = { -0.70f, -0.40f, fFarZ, 0.06f, 0.16f, 0.50f, 1.0f, 0.0f, 1.0f };
	akVertices[6] = { -0.70f, -0.40f, fFarZ, 0.06f, 0.16f, 0.50f, 1.0f, 0.0f, 1.0f };
	akVertices[7] = {  0.40f,  0.32f, fFarZ, 0.10f, 0.30f, 0.85f, 1.0f, 1.0f, 0.0f };
	akVertices[8] = {  0.40f, -0.40f, fFarZ, 0.06f, 0.16f, 0.50f, 1.0f, 1.0f, 1.0f };

	// Near quad (front layer, horizontally animated).
	akVertices[9]  = { -0.34f + fOffsetX,  0.52f, fNearZ, 0.95f, 0.65f, 0.15f, 1.0f, 0.0f, 0.0f };
	akVertices[10] = {  0.76f + fOffsetX,  0.52f, fNearZ, 0.95f, 0.65f, 0.15f, 1.0f, 1.0f, 0.0f };
	akVertices[11] = { -0.34f + fOffsetX, -0.20f, fNearZ, 0.72f, 0.41f, 0.08f, 1.0f, 0.0f, 1.0f };
	akVertices[12] = { -0.34f + fOffsetX, -0.20f, fNearZ, 0.72f, 0.41f, 0.08f, 1.0f, 0.0f, 1.0f };
	akVertices[13] = {  0.76f + fOffsetX,  0.52f, fNearZ, 0.95f, 0.65f, 0.15f, 1.0f, 1.0f, 0.0f };
	akVertices[14] = {  0.76f + fOffsetX, -0.20f, fNearZ, 0.72f, 0.41f, 0.08f, 1.0f, 1.0f, 1.0f };

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hMapResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(bootstrap_vertex_buffer)", hMapResult);
		if (SUCCEEDED(hMapResult) && !kMappedResource.pData)
			TraceError("DX11_MAP_NULL_DATA op=ID3D11DeviceContext::Map(bootstrap_vertex_buffer)");
		return false;
	}

	memcpy(kMappedResource.pData, akVertices, sizeof(akVertices));
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapPixelShader, NULL, 0);
	m_pDeviceContext->Draw(12, 3);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((12) / 3);
	return true;
}

bool CGraphicDeviceDX11::DrawBootstrapWorldBatchTest(float fTimeSec, int iInstanceCount)
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;
	if (0 == m_uWidth || 0 == m_uHeight)
		return false;

	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const int kMaxQuads = 256;
	int iQuadCount = iInstanceCount;
	if (iQuadCount < 1)
		iQuadCount = 1;
	else if (iQuadCount > kMaxQuads)
		iQuadCount = kMaxQuads;

	const int iVertexCount = iQuadCount * 6;
	std::vector<SBootstrapVertex> akVertices(3 + iVertexCount);

	akVertices[0] = {  0.0f,  0.45f, 0.0f, 1.0f, 0.20f, 0.20f, 1.0f, 0.5f, 0.0f };
	akVertices[1] = {  0.35f, -0.25f, 0.0f, 0.20f, 1.0f, 0.20f, 1.0f, 1.0f, 1.0f };
	akVertices[2] = { -0.35f, -0.25f, 0.0f, 0.20f, 0.45f, 1.0f, 1.0f, 0.0f, 1.0f };

	const int iCols = 16;
	const int iRows = (iQuadCount + iCols - 1) / iCols;
	const float fCellW = 1.8f / static_cast<float>(iCols);
	const float fCellH = 1.5f / static_cast<float>(std::max(1, iRows));
	const float fPadW = fCellW * 0.12f;
	const float fPadH = fCellH * 0.16f;

	for (int i = 0; i < iQuadCount; ++i)
	{
		const int iCol = i % iCols;
		const int iRow = i / iCols;
		const float fBaseX = -0.90f + fCellW * static_cast<float>(iCol);
		const float fBaseY =  0.72f - fCellH * static_cast<float>(iRow);

		const float fAnim = static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 1.6 + static_cast<double>(i) * 0.25)) * 0.012f;
		const float fX0 = fBaseX + fPadW + fAnim;
		const float fY0 = fBaseY - fPadH;
		const float fX1 = fBaseX + fCellW - fPadW + fAnim;
		const float fY1 = fBaseY - fCellH + fPadH;
		const float fZ = 0.10f + 0.70f * (static_cast<float>(iRow) / static_cast<float>(std::max(1, iRows)));

		const float fR = 0.15f + 0.75f * (static_cast<float>(iCol) / static_cast<float>(iCols));
		const float fG = 0.20f + 0.50f * (static_cast<float>((i + iRow) % iCols) / static_cast<float>(iCols));
		const float fB = 0.25f + 0.55f * (static_cast<float>(iRow) / static_cast<float>(std::max(1, iRows)));

		const int k = 3 + i * 6;
		akVertices[k + 0] = { fX0, fY0, fZ, fR,        fG,        fB,        1.0f, 0.0f, 0.0f };
		akVertices[k + 1] = { fX1, fY0, fZ, fR * 0.92f, fG * 0.92f, fB * 0.92f, 1.0f, 1.0f, 0.0f };
		akVertices[k + 2] = { fX0, fY1, fZ, fR * 0.70f, fG * 0.70f, fB * 0.70f, 1.0f, 0.0f, 1.0f };
		akVertices[k + 3] = { fX0, fY1, fZ, fR * 0.70f, fG * 0.70f, fB * 0.70f, 1.0f, 0.0f, 1.0f };
		akVertices[k + 4] = { fX1, fY0, fZ, fR * 0.92f, fG * 0.92f, fB * 0.92f, 1.0f, 1.0f, 0.0f };
		akVertices[k + 5] = { fX1, fY1, fZ, fR * 0.65f, fG * 0.65f, fB * 0.65f, 1.0f, 1.0f, 1.0f };
	}

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hMapResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(bootstrap_vertex_buffer)", hMapResult);
		if (SUCCEEDED(hMapResult) && !kMappedResource.pData)
			TraceError("DX11_MAP_NULL_DATA op=ID3D11DeviceContext::Map(bootstrap_vertex_buffer)");
		return false;
	}

	memcpy(kMappedResource.pData, &akVertices[0], sizeof(SBootstrapVertex) * akVertices.size());
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapPixelShader, NULL, 0);
	m_pDeviceContext->Draw(iVertexCount, 3);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((iVertexCount) / 3);
	return true;
}

bool CGraphicDeviceDX11::DrawBootstrapWorldSpriteTest(float fTimeSec, int iInstanceCount)
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;
	if (0 == m_uWidth || 0 == m_uHeight)
		return false;

	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}
	if (!m_isBootstrapUITextureReady)
	{
		if (!__CreateBootstrapUITexture())
			return false;
	}

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const int kMaxSprites = 256;
	int iSpriteCount = iInstanceCount;
	if (iSpriteCount < 1)
		iSpriteCount = 1;
	else if (iSpriteCount > kMaxSprites)
		iSpriteCount = kMaxSprites;

	const int iVertexCount = iSpriteCount * 6;
	std::vector<SBootstrapVertex> akVertices(3 + iVertexCount);

	akVertices[0] = {  0.0f,  0.45f, 0.0f, 1.0f, 0.20f, 0.20f, 1.0f, 0.5f, 0.0f };
	akVertices[1] = {  0.35f, -0.25f, 0.0f, 0.20f, 1.0f, 0.20f, 1.0f, 1.0f, 1.0f };
	akVertices[2] = { -0.35f, -0.25f, 0.0f, 0.20f, 0.45f, 1.0f, 1.0f, 0.0f, 1.0f };

	const int iCols = 20;
	const int iRows = (iSpriteCount + iCols - 1) / iCols;
	const float fCellW = 1.8f / static_cast<float>(iCols);
	const float fCellH = 1.5f / static_cast<float>(std::max(1, iRows));
	const float fSpriteW = fCellW * 0.82f;
	const float fSpriteH = fCellH * 0.82f;

	for (int i = 0; i < iSpriteCount; ++i)
	{
		const int iCol = i % iCols;
		const int iRow = i / iCols;

		const float fBaseX = -0.90f + fCellW * static_cast<float>(iCol) + (fCellW * 0.5f);
		const float fBaseY =  0.72f - fCellH * static_cast<float>(iRow) - (fCellH * 0.5f);

		const float fPhase = static_cast<float>(i) * 0.17f;
		const float fJitterX = static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 1.7 + fPhase)) * (fCellW * 0.08f);
		const float fJitterY = static_cast<float>(std::cos(static_cast<double>(fTimeSec) * 1.3 + fPhase)) * (fCellH * 0.08f);
		const float fHalfW = fSpriteW * 0.5f;
		const float fHalfH = fSpriteH * 0.5f;

		const float fX0 = fBaseX - fHalfW + fJitterX;
		const float fY0 = fBaseY + fHalfH + fJitterY;
		const float fX1 = fBaseX + fHalfW + fJitterX;
		const float fY1 = fBaseY - fHalfH + fJitterY;

		const float fZ = 0.12f + 0.70f * (static_cast<float>(iRow) / static_cast<float>(std::max(1, iRows)));
		const float fTint = 0.70f + 0.30f * static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 2.0 + static_cast<double>(i) * 0.11));
		const float fR = std::max(0.20f, std::min(1.0f, 0.55f + 0.35f * fTint));
		const float fG = std::max(0.20f, std::min(1.0f, 0.45f + 0.30f * fTint));
		const float fB = std::max(0.20f, std::min(1.0f, 0.60f + 0.25f * fTint));

		const int k = 3 + i * 6;
		akVertices[k + 0] = { fX0, fY0, fZ, fR, fG, fB, 1.0f, 0.0f, 0.0f };
		akVertices[k + 1] = { fX1, fY0, fZ, fR, fG, fB, 1.0f, 1.0f, 0.0f };
		akVertices[k + 2] = { fX0, fY1, fZ, fR, fG, fB, 1.0f, 0.0f, 1.0f };
		akVertices[k + 3] = { fX0, fY1, fZ, fR, fG, fB, 1.0f, 0.0f, 1.0f };
		akVertices[k + 4] = { fX1, fY0, fZ, fR, fG, fB, 1.0f, 1.0f, 0.0f };
		akVertices[k + 5] = { fX1, fY1, fZ, fR, fG, fB, 1.0f, 1.0f, 1.0f };
	}

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hMapResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(bootstrap_vertex_buffer)", hMapResult);
		if (SUCCEEDED(hMapResult) && !kMappedResource.pData)
			TraceError("DX11_MAP_NULL_DATA op=ID3D11DeviceContext::Map(bootstrap_vertex_buffer)");
		return false;
	}

	memcpy(kMappedResource.pData, &akVertices[0], sizeof(SBootstrapVertex) * akVertices.size());
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapUIPixelShader, NULL, 0);
	m_pDeviceContext->PSSetShaderResources(0, 1, &m_pBootstrapUITextureSRV);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pBootstrapUISamplerState);
	m_pDeviceContext->Draw(iVertexCount, 3);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((iVertexCount) / 3);
	ID3D11ShaderResourceView* pNullSRV = NULL;
	m_pDeviceContext->PSSetShaderResources(0, 1, &pNullSRV);
	return true;
}

bool CGraphicDeviceDX11::DrawBootstrapWorldStateTest(float fTimeSec, int iInstanceCount)
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;
	if (0 == m_uWidth || 0 == m_uHeight)
		return false;

	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}
	if (!m_isBootstrapUITextureReady)
	{
		if (!__CreateBootstrapUITexture())
			return false;
	}
	if (!m_pBootstrapAlphaBlendState || !m_pBootstrapDepthEnableState || !m_pBootstrapDepthDisableState)
		return false;

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const int kMaxSprites = 192;
	int iSpriteCount = iInstanceCount;
	if (iSpriteCount < 1)
		iSpriteCount = 1;
	else if (iSpriteCount > kMaxSprites)
		iSpriteCount = kMaxSprites;

	const int iOpaqueCount = 3 * 6;
	const int iSpriteVertexCount = iSpriteCount * 6;
	const int iOverlayCount = 6;

	const int iOpaqueOffset = 3;
	const int iSpriteOffset = iOpaqueOffset + iOpaqueCount;
	const int iOverlayOffset = iSpriteOffset + iSpriteVertexCount;
	const int iTotalVertexCount = iOverlayOffset + iOverlayCount;

	std::vector<SBootstrapVertex> akVertices(iTotalVertexCount);

	akVertices[0] = {  0.0f,  0.45f, 0.0f, 1.0f, 0.20f, 0.20f, 1.0f, 0.5f, 0.0f };
	akVertices[1] = {  0.35f, -0.25f, 0.0f, 0.20f, 1.0f, 0.20f, 1.0f, 1.0f, 1.0f };
	akVertices[2] = { -0.35f, -0.25f, 0.0f, 0.20f, 0.45f, 1.0f, 1.0f, 0.0f, 1.0f };

	// Opaque depth-tested layers (three quads).
	{
		const float fShift = static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 1.1)) * 0.08f;
		const float fZ0 = 0.78f;
		const float fZ1 = 0.48f;
		const float fZ2 = 0.20f;

		SBootstrapVertex kOpaque[18] =
		{
			{ -0.78f,  0.64f, fZ0, 0.08f, 0.16f, 0.44f, 1.0f, 0.0f, 0.0f },
			{  0.18f,  0.64f, fZ0, 0.08f, 0.16f, 0.44f, 1.0f, 1.0f, 0.0f },
			{ -0.78f, -0.08f, fZ0, 0.04f, 0.08f, 0.28f, 1.0f, 0.0f, 1.0f },
			{ -0.78f, -0.08f, fZ0, 0.04f, 0.08f, 0.28f, 1.0f, 0.0f, 1.0f },
			{  0.18f,  0.64f, fZ0, 0.08f, 0.16f, 0.44f, 1.0f, 1.0f, 0.0f },
			{  0.18f, -0.08f, fZ0, 0.04f, 0.08f, 0.28f, 1.0f, 1.0f, 1.0f },

			{ -0.44f + fShift,  0.54f, fZ1, 0.16f, 0.52f, 0.85f, 1.0f, 0.0f, 0.0f },
			{  0.56f + fShift,  0.54f, fZ1, 0.16f, 0.52f, 0.85f, 1.0f, 1.0f, 0.0f },
			{ -0.44f + fShift, -0.20f, fZ1, 0.08f, 0.28f, 0.58f, 1.0f, 0.0f, 1.0f },
			{ -0.44f + fShift, -0.20f, fZ1, 0.08f, 0.28f, 0.58f, 1.0f, 0.0f, 1.0f },
			{  0.56f + fShift,  0.54f, fZ1, 0.16f, 0.52f, 0.85f, 1.0f, 1.0f, 0.0f },
			{  0.56f + fShift, -0.20f, fZ1, 0.08f, 0.28f, 0.58f, 1.0f, 1.0f, 1.0f },

			{ -0.10f - fShift,  0.44f, fZ2, 0.82f, 0.38f, 0.10f, 1.0f, 0.0f, 0.0f },
			{  0.88f - fShift,  0.44f, fZ2, 0.82f, 0.38f, 0.10f, 1.0f, 1.0f, 0.0f },
			{ -0.10f - fShift, -0.30f, fZ2, 0.48f, 0.20f, 0.05f, 1.0f, 0.0f, 1.0f },
			{ -0.10f - fShift, -0.30f, fZ2, 0.48f, 0.20f, 0.05f, 1.0f, 0.0f, 1.0f },
			{  0.88f - fShift,  0.44f, fZ2, 0.82f, 0.38f, 0.10f, 1.0f, 1.0f, 0.0f },
			{  0.88f - fShift, -0.30f, fZ2, 0.48f, 0.20f, 0.05f, 1.0f, 1.0f, 1.0f },
		};

		for (int i = 0; i < 18; ++i)
			akVertices[iOpaqueOffset + i] = kOpaque[i];
	}

	// Alpha blended textured sprites with depth enabled.
	{
		const int iCols = 16;
		const int iRows = (iSpriteCount + iCols - 1) / iCols;
		const float fCellW = 1.8f / static_cast<float>(iCols);
		const float fCellH = 0.78f / static_cast<float>(std::max(1, iRows));
		const float fSpriteW = fCellW * 0.82f;
		const float fSpriteH = fCellH * 0.82f;

		for (int i = 0; i < iSpriteCount; ++i)
		{
			const int iCol = i % iCols;
			const int iRow = i / iCols;

			const float fBaseX = -0.90f + fCellW * static_cast<float>(iCol) + (fCellW * 0.5f);
			const float fBaseY =  0.66f - fCellH * static_cast<float>(iRow) - (fCellH * 0.5f);
			const float fPhase = static_cast<float>(i) * 0.13f;
			const float fJitterX = static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 1.9 + fPhase)) * (fCellW * 0.10f);
			const float fJitterY = static_cast<float>(std::cos(static_cast<double>(fTimeSec) * 1.5 + fPhase)) * (fCellH * 0.10f);
			const float fHalfW = fSpriteW * 0.5f;
			const float fHalfH = fSpriteH * 0.5f;

			const float fX0 = fBaseX - fHalfW + fJitterX;
			const float fY0 = fBaseY + fHalfH + fJitterY;
			const float fX1 = fBaseX + fHalfW + fJitterX;
			const float fY1 = fBaseY - fHalfH + fJitterY;
			const float fZ = 0.18f + 0.58f * (static_cast<float>(iRow) / static_cast<float>(std::max(1, iRows)));

			const float fA = 0.35f + 0.30f * static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 2.2 + static_cast<double>(i) * 0.17));
			const float fAlpha = std::max(0.12f, std::min(0.80f, fA));

			const float fR = 0.70f;
			const float fG = 0.80f;
			const float fB = 1.00f;

			const int k = iSpriteOffset + i * 6;
			akVertices[k + 0] = { fX0, fY0, fZ, fR, fG, fB, fAlpha, 0.0f, 0.0f };
			akVertices[k + 1] = { fX1, fY0, fZ, fR, fG, fB, fAlpha, 1.0f, 0.0f };
			akVertices[k + 2] = { fX0, fY1, fZ, fR, fG, fB, fAlpha, 0.0f, 1.0f };
			akVertices[k + 3] = { fX0, fY1, fZ, fR, fG, fB, fAlpha, 0.0f, 1.0f };
			akVertices[k + 4] = { fX1, fY0, fZ, fR, fG, fB, fAlpha, 1.0f, 0.0f };
			akVertices[k + 5] = { fX1, fY1, fZ, fR, fG, fB, fAlpha, 1.0f, 1.0f };
		}
	}

	// Overlay quad with depth disabled (HUD-like pass).
	{
		const float fAlphaPulse = 0.55f + 0.35f * static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 2.8));
		const float fOverlayAlpha = std::max(0.25f, std::min(0.95f, fAlphaPulse));
		const int k = iOverlayOffset;
		akVertices[k + 0] = { -0.86f,  0.92f, 0.0f, 0.95f, 0.95f, 0.22f, fOverlayAlpha, 0.0f, 0.0f };
		akVertices[k + 1] = {  0.86f,  0.92f, 0.0f, 0.95f, 0.95f, 0.22f, fOverlayAlpha, 1.0f, 0.0f };
		akVertices[k + 2] = { -0.86f,  0.80f, 0.0f, 0.60f, 0.60f, 0.10f, fOverlayAlpha, 0.0f, 1.0f };
		akVertices[k + 3] = { -0.86f,  0.80f, 0.0f, 0.60f, 0.60f, 0.10f, fOverlayAlpha, 0.0f, 1.0f };
		akVertices[k + 4] = {  0.86f,  0.92f, 0.0f, 0.95f, 0.95f, 0.22f, fOverlayAlpha, 1.0f, 0.0f };
		akVertices[k + 5] = {  0.86f,  0.80f, 0.0f, 0.60f, 0.60f, 0.10f, fOverlayAlpha, 1.0f, 1.0f };
	}

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hMapResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(bootstrap_vertex_buffer)", hMapResult);
		if (SUCCEEDED(hMapResult) && !kMappedResource.pData)
			TraceError("DX11_MAP_NULL_DATA op=ID3D11DeviceContext::Map(bootstrap_vertex_buffer)");
		return false;
	}

	memcpy(kMappedResource.pData, &akVertices[0], sizeof(SBootstrapVertex) * akVertices.size());
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);

	const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// Pass 1: opaque with depth.
	m_pDeviceContext->OMSetBlendState(NULL, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthEnableState, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapPixelShader, NULL, 0);
	m_pDeviceContext->Draw(iOpaqueCount, iOpaqueOffset);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((iOpaqueCount) / 3);
	// Pass 2: alpha blended textured sprites with depth.
	m_pDeviceContext->OMSetBlendState(m_pBootstrapAlphaBlendState, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthEnableState, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapUIPixelShader, NULL, 0);
	m_pDeviceContext->PSSetShaderResources(0, 1, &m_pBootstrapUITextureSRV);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pBootstrapUISamplerState);
	m_pDeviceContext->Draw(iSpriteVertexCount, iSpriteOffset);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((iSpriteVertexCount) / 3);
	// Pass 3: HUD overlay with depth disabled.
	m_pDeviceContext->OMSetBlendState(m_pBootstrapAlphaBlendState, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthDisableState, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapPixelShader, NULL, 0);
	m_pDeviceContext->Draw(iOverlayCount, iOverlayOffset);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((iOverlayCount) / 3);
	// Restore defaults.
	ID3D11ShaderResourceView* pNullSRV = NULL;
	m_pDeviceContext->PSSetShaderResources(0, 1, &pNullSRV);
	m_pDeviceContext->OMSetBlendState(NULL, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(NULL, 0);

	return true;
}

bool CGraphicDeviceDX11::DrawBootstrapWorldPassesTest(float fTimeSec, int iTerrainTiles, int iActorCount, int iFXCount)
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;
	if (0 == m_uWidth || 0 == m_uHeight)
		return false;

	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}
	if (!m_isBootstrapUITextureReady)
	{
		if (!__CreateBootstrapUITexture())
			return false;
	}
	if (!m_pBootstrapAlphaBlendState || !m_pBootstrapAdditiveBlendState || !m_pBootstrapDepthEnableState || !m_pBootstrapDepthReadState || !m_pBootstrapDepthDisableState)
		return false;

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const int kMaxVBVertices = 4096;
	const int kReservedVertices = 3;
	const int kVerticesPerQuad = 6;

	int iTiles = iTerrainTiles;
	int iActors = iActorCount;
	int iFX = iFXCount;
	if (iTiles < 4) iTiles = 4;
	if (iActors < 4) iActors = 4;
	if (iFX < 4) iFX = 4;

	// Fit total within dynamic bootstrap VB budget.
	const int iMaxQuads = (kMaxVBVertices - kReservedVertices) / kVerticesPerQuad;
	while (iTiles + iActors + iFX > iMaxQuads)
	{
		if (iFX > 8) --iFX;
		else if (iActors > 8) --iActors;
		else if (iTiles > 8) --iTiles;
		else break;
	}

	const int iTilesVertices = iTiles * kVerticesPerQuad;
	const int iActorsVertices = iActors * kVerticesPerQuad;
	const int iFXVertices = iFX * kVerticesPerQuad;

	const int iTilesOffset = kReservedVertices;
	const int iActorsOffset = iTilesOffset + iTilesVertices;
	const int iFXOffset = iActorsOffset + iActorsVertices;
	const int iTotalVertices = iFXOffset + iFXVertices;

	std::vector<SBootstrapVertex> akVertices(iTotalVertices);

	akVertices[0] = {  0.0f,  0.45f, 0.0f, 1.0f, 0.20f, 0.20f, 1.0f, 0.5f, 0.0f };
	akVertices[1] = {  0.35f, -0.25f, 0.0f, 0.20f, 1.0f, 0.20f, 1.0f, 1.0f, 1.0f };
	akVertices[2] = { -0.35f, -0.25f, 0.0f, 0.20f, 0.45f, 1.0f, 1.0f, 0.0f, 1.0f };

	// Terrain pass: opaque textured tiles, far depth.
	{
		const int iCols = 20;
		const int iRows = (iTiles + iCols - 1) / iCols;
		const float fCellW = 1.8f / static_cast<float>(iCols);
		const float fCellH = 1.2f / static_cast<float>(std::max(1, iRows));

		for (int i = 0; i < iTiles; ++i)
		{
			const int iCol = i % iCols;
			const int iRow = i / iCols;
			const float fX0 = -0.90f + fCellW * static_cast<float>(iCol);
			const float fY0 =  0.74f - fCellH * static_cast<float>(iRow);
			const float fX1 = fX0 + fCellW;
			const float fY1 = fY0 - fCellH;
			const float fZ = 0.72f - 0.20f * (static_cast<float>(iRow) / static_cast<float>(std::max(1, iRows)));

			const float fR = 0.28f;
			const float fG = 0.34f + 0.18f * (static_cast<float>(iCol % 4) / 3.0f);
			const float fB = 0.20f;
			const float fA = 1.0f;

			const int k = iTilesOffset + i * 6;
			akVertices[k + 0] = { fX0, fY0, fZ, fR, fG, fB, fA, 0.0f, 0.0f };
			akVertices[k + 1] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 2] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 3] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 4] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 5] = { fX1, fY1, fZ, fR, fG, fB, fA, 1.0f, 1.0f };
		}
	}

	// Actor pass: alpha blended sprites, depth-tested.
	{
		const int iCols = 16;
		const int iRows = (iActors + iCols - 1) / iCols;
		const float fCellW = 1.72f / static_cast<float>(iCols);
		const float fCellH = 0.74f / static_cast<float>(std::max(1, iRows));
		const float fW = fCellW * 0.72f;
		const float fH = fCellH * 0.82f;

		for (int i = 0; i < iActors; ++i)
		{
			const int iCol = i % iCols;
			const int iRow = i / iCols;
			const float fBaseX = -0.86f + fCellW * static_cast<float>(iCol) + (fCellW * 0.5f);
			const float fBaseY =  0.64f - fCellH * static_cast<float>(iRow) - (fCellH * 0.5f);
			const float fPhase = static_cast<float>(i) * 0.11f;
			const float fOffsetX = static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 1.8 + fPhase)) * (fCellW * 0.12f);
			const float fOffsetY = static_cast<float>(std::cos(static_cast<double>(fTimeSec) * 1.3 + fPhase)) * (fCellH * 0.10f);

			const float fX0 = fBaseX - (fW * 0.5f) + fOffsetX;
			const float fY0 = fBaseY + (fH * 0.5f) + fOffsetY;
			const float fX1 = fBaseX + (fW * 0.5f) + fOffsetX;
			const float fY1 = fBaseY - (fH * 0.5f) + fOffsetY;
			const float fZ = 0.48f - 0.22f * (static_cast<float>(iRow) / static_cast<float>(std::max(1, iRows)));

			const float fR = 0.82f;
			const float fG = 0.84f;
			const float fB = 0.95f;
			const float fA = 0.62f;

			const int k = iActorsOffset + i * 6;
			akVertices[k + 0] = { fX0, fY0, fZ, fR, fG, fB, fA, 0.0f, 0.0f };
			akVertices[k + 1] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 2] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 3] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 4] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 5] = { fX1, fY1, fZ, fR, fG, fB, fA, 1.0f, 1.0f };
		}
	}

	// FX pass: additive sprites, depth test on, no depth write.
	{
		const int iCols = 14;
		const int iRows = (iFX + iCols - 1) / iCols;
		const float fCellW = 1.68f / static_cast<float>(iCols);
		const float fCellH = 0.68f / static_cast<float>(std::max(1, iRows));
		const float fW = fCellW * 0.86f;
		const float fH = fCellH * 0.86f;

		for (int i = 0; i < iFX; ++i)
		{
			const int iCol = i % iCols;
			const int iRow = i / iCols;
			const float fBaseX = -0.84f + fCellW * static_cast<float>(iCol) + (fCellW * 0.5f);
			const float fBaseY =  0.58f - fCellH * static_cast<float>(iRow) - (fCellH * 0.5f);
			const float fPhase = static_cast<float>(i) * 0.19f;
			const float fPulse = 0.65f + 0.35f * static_cast<float>(std::sin(static_cast<double>(fTimeSec) * 3.0 + static_cast<double>(fPhase)));

			const float fX0 = fBaseX - (fW * 0.5f);
			const float fY0 = fBaseY + (fH * 0.5f);
			const float fX1 = fBaseX + (fW * 0.5f);
			const float fY1 = fBaseY - (fH * 0.5f);
			const float fZ = 0.44f - 0.18f * (static_cast<float>(iRow) / static_cast<float>(std::max(1, iRows)));

			const float fR = 0.35f * fPulse;
			const float fG = 0.60f * fPulse;
			const float fB = 1.00f * fPulse;
			const float fA = 0.36f * fPulse;

			const int k = iFXOffset + i * 6;
			akVertices[k + 0] = { fX0, fY0, fZ, fR, fG, fB, fA, 0.0f, 0.0f };
			akVertices[k + 1] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 2] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 3] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 4] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 5] = { fX1, fY1, fZ, fR, fG, fB, fA, 1.0f, 1.0f };
		}
	}

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hMapResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(bootstrap_vertex_buffer)", hMapResult);
		if (SUCCEEDED(hMapResult) && !kMappedResource.pData)
			TraceError("DX11_MAP_NULL_DATA op=ID3D11DeviceContext::Map(bootstrap_vertex_buffer)");
		return false;
	}

	memcpy(kMappedResource.pData, &akVertices[0], sizeof(SBootstrapVertex) * akVertices.size());
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShaderResources(0, 1, &m_pBootstrapUITextureSRV);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pBootstrapUISamplerState);

	const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// Terrain pass.
	m_pDeviceContext->OMSetBlendState(NULL, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthEnableState, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapUIPixelShader, NULL, 0);
	m_pDeviceContext->Draw(iTilesVertices, iTilesOffset);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((iTilesVertices) / 3);
	// Actor pass.
	m_pDeviceContext->OMSetBlendState(m_pBootstrapAlphaBlendState, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthEnableState, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapUIPixelShader, NULL, 0);
	m_pDeviceContext->Draw(iActorsVertices, iActorsOffset);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((iActorsVertices) / 3);
	// FX pass.
	m_pDeviceContext->OMSetBlendState(m_pBootstrapAdditiveBlendState, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthReadState, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapUIPixelShader, NULL, 0);
	m_pDeviceContext->Draw(iFXVertices, iFXOffset);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((iFXVertices) / 3);
	ID3D11ShaderResourceView* pNullSRV = NULL;
	m_pDeviceContext->PSSetShaderResources(0, 1, &pNullSRV);
	m_pDeviceContext->OMSetBlendState(NULL, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(NULL, 0);

	return true;
}

bool CGraphicDeviceDX11::DrawNativeWorldMinimalDryRun(float fTimeSec, int iTerrainTiles, int iActorCount, int iFXCount)
{
	// World-minimal dry-run keeps runtime/present pipeline alive during warmup.
	(void)fTimeSec;
	if (IsNativeWorldTerrainPrototypeEnabled())
	{
		if (__RunNativeWorldTerrainPrototype(iTerrainTiles, iActorCount, iFXCount, false))
			return true;
	}
	return TickNativeWorldRuntime("warmup_external", iTerrainTiles, iActorCount, iFXCount);
}

bool CGraphicDeviceDX11::DrawNativeWorldRenderPasses(float fTimeSec, int iTerrainTiles, int iActorCount, int iFXCount)
{
	// Active world path: world draw calls are submitted by higher-level integration.
	(void)fTimeSec;
	if (IsNativeWorldTerrainPrototypeEnabled())
	{
		if (__RunNativeWorldTerrainPrototype(iTerrainTiles, iActorCount, iFXCount, true))
			return true;
	}
	return TickNativeWorldRuntime("active_external", iTerrainTiles, iActorCount, iFXCount);
}

bool CGraphicDeviceDX11::DrawNativeWorldTerrainPilot(float fTimeSec, int iTerrainTiles)
{
	// Stage 1 pilot: validate terrain-only DX11 world pass while keeping actor/fx disabled.
	return DrawNativeWorldMinimalDryRun(fTimeSec, iTerrainTiles, 0, 0);
}

bool CGraphicDeviceDX11::DrawNativeWorldShadowPasses(float fTimeSec, int iTerrainTiles, int iActorCount, int iFXCount)
{
	// Legacy compatibility entrypoint kept for higher layers.
	return DrawNativeWorldRenderPasses(fTimeSec, iTerrainTiles, iActorCount, iFXCount);
}

bool CGraphicDeviceDX11::TickNativeWorldRuntime(const char* c_szStage, int iTerrainTiles, int iActorCount, int iFXCount)
{
	if (!__ValidateNativeWorldRuntimePass())
		return false;

	static DWORD s_dwDX11NativeWorldRuntimeHeartbeatCounter = 0;
	const bool bLogNow =
		(0 == s_dwDX11NativeWorldRuntimeHeartbeatCounter) ||
		(0 == (s_dwDX11NativeWorldRuntimeHeartbeatCounter % 1800u));
	if (bLogNow)
	{
		TraceError(
			"DX11_WORLD_NATIVE_RUNTIME_HEARTBEAT stage=%s terrain=%d actors=%d fx=%d",
			c_szStage ? c_szStage : "unknown",
			iTerrainTiles,
			iActorCount,
			iFXCount);
	}
	if (s_dwDX11NativeWorldRuntimeHeartbeatCounter < 0xffffffffu)
		++s_dwDX11NativeWorldRuntimeHeartbeatCounter;

	static DWORD s_dwDX11WorldPortMaskLogTick = 0;
	const DWORD dwNow = GetTickCount();
	if (0 == s_dwDX11WorldPortMaskLogTick || dwNow - s_dwDX11WorldPortMaskLogTick >= 30000u)
	{
		s_dwDX11WorldPortMaskLogTick = dwNow;
		const uint32_t dwRequiredMask = WORLD_PORT_REQUIRED_MASK;
		const uint32_t dwRequiredEffectiveMask = GetNativeWorldRequiredEffectiveMask();
		const uint32_t dwMissingMaskFull = (dwRequiredMask & ~m_dwNativeWorldCommittedMask);
		const uint32_t dwMissingMaskEffective = (dwRequiredEffectiveMask & ~m_dwNativeWorldCommittedMask);
		const std::string stMissingMaskFullTokens = __FormatWorldMaskTokens(dwMissingMaskFull);
		const std::string stMissingMaskEffectiveTokens = __FormatWorldMaskTokens(dwMissingMaskEffective);
		TraceError(
			"DX11_WORLD_PORT_MASK mask=0x%02X observed=0x%02X submitted=0x%02X submitted_seen=0x%02X applicable=0x%02X required=0x%02X required_effective=0x%02X missing=0x%02X missing_effective=0x%02X missing_full_tokens=%s missing_effective_tokens=%s ready=%d ready_effective=%d texture_mode=%u",
			static_cast<unsigned int>(m_dwNativeWorldCommittedMask),
			static_cast<unsigned int>(m_dwNativeWorldObservedMask),
			static_cast<unsigned int>(m_dwNativeWorldSubmittedMask),
			static_cast<unsigned int>(m_dwNativeWorldSubmittedSeenMask),
			static_cast<unsigned int>(m_dwNativeWorldApplicableMask),
			static_cast<unsigned int>(dwRequiredMask),
			static_cast<unsigned int>(dwRequiredEffectiveMask),
			static_cast<unsigned int>(dwMissingMaskFull),
			static_cast<unsigned int>(dwMissingMaskEffective),
			stMissingMaskFullTokens.c_str(),
			stMissingMaskEffectiveTokens.c_str(),
			IsNativeWorldRendererPorted() ? 1 : 0,
			(0u == dwMissingMaskEffective) ? 1 : 0,
			static_cast<unsigned int>(m_eDX11TexturePipelineMode));
	}

	return true;
}

bool CGraphicDeviceDX11::__ValidateNativeWorldRuntimePass()
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;
	if (0 == m_uWidth || 0 == m_uHeight)
		return false;

	m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);
	m_pDeviceContext->RSSetViewports(1, &m_kViewport);
	return true;
}

bool CGraphicDeviceDX11::__RunNativeWorldTerrainPrototype(int iTerrainTiles, int iActorCount, int iFXCount, bool bIncludeActorsAndFX)
{
	if (!__ValidateNativeWorldRuntimePass())
		return false;

	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}
	if (!m_pBootstrapDepthEnableState || !m_pBootstrapVertexBuffer || !m_pBootstrapInputLayout || !m_pBootstrapVertexShader || !m_pBootstrapPixelShader)
		return false;

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const int kVerticesPerQuad = 6;
	const int kMaxVertexBudget = 4096;
	int iTiles = std::max(8, iTerrainTiles);
	int iActors = bIncludeActorsAndFX ? std::max(4, iActorCount) : 0;
	int iFX = bIncludeActorsAndFX ? std::max(4, iFXCount) : 0;
	const int iScenePatchCount = std::max(iTiles, m_iNativeWorldScenePatchCount);
	const int iSceneTextureCount = std::max(1, m_iNativeWorldSceneTextureCount);
	const float fSceneSplatRatio = std::max(0.0f, m_fNativeWorldSceneSplatRatio);
	const float fSceneSplatScale = std::min(2.0f, std::max(0.4f, fSceneSplatRatio / 20.0f));
	const float fSceneInstanceScale = std::min(2.0f, 0.5f + static_cast<float>(m_dwNativeWorldSceneThingInstances % 400u) / 400.0f);
	const float fSceneCRCScale = std::min(2.0f, 0.5f + static_cast<float>(m_dwNativeWorldSceneCRCCount % 120u) / 120.0f);

	const int iMaxQuads = kMaxVertexBudget / kVerticesPerQuad;
	while (iTiles + iActors + iFX > iMaxQuads)
	{
		if (iFX > 4)
			--iFX;
		else if (iActors > 4)
			--iActors;
		else if (iTiles > 8)
			--iTiles;
		else
			break;
	}

	int iCols = 16 + std::min(20, iSceneTextureCount + (iScenePatchCount / 64));
	iCols = std::max(12, std::min(36, iCols));
	const int iRows = std::max(1, (iTiles + iCols - 1) / iCols);
	const float fCellW = 1.90f / static_cast<float>(iCols);
	const float fCellH = 1.30f / static_cast<float>(iRows);
	const float fStartX = -0.95f;
	const float fStartY = 0.85f;

	const int iTerrainVertices = iTiles * kVerticesPerQuad;
	const int iActorVertices = iActors * kVerticesPerQuad;
	const int iFXVertices = iFX * kVerticesPerQuad;
	const int iTerrainOffset = 0;
	const int iActorOffset = iTerrainOffset + iTerrainVertices;
	const int iFXOffset = iActorOffset + iActorVertices;

	std::vector<SBootstrapVertex> akVertices;
	akVertices.resize(iTerrainVertices + iActorVertices + iFXVertices);
	for (int i = 0; i < iTiles; ++i)
	{
		const int iCol = i % iCols;
		const int iRow = i / iCols;
		const float fX0 = fStartX + fCellW * static_cast<float>(iCol);
		const float fY0 = fStartY - fCellH * static_cast<float>(iRow);
		const float fX1 = fX0 + fCellW;
		const float fY1 = fY0 - fCellH;
		const float fDepthT = static_cast<float>(iRow) / static_cast<float>(iRows);
		const float fZ = 0.78f - (0.34f * fDepthT);

		const float fShade = 0.78f + (0.22f * static_cast<float>((iCol + iRow) % 3) / 2.0f);
		const float fTextureTint = 0.85f + (0.15f * static_cast<float>(iSceneTextureCount % 8) / 7.0f);
		const float fR = 0.22f * fShade * fTextureTint;
		const float fG = 0.30f * fShade * fSceneSplatScale;
		const float fB = 0.18f * fShade * fSceneCRCScale;
		const float fA = 1.0f;

		const int k = iTerrainOffset + i * kVerticesPerQuad;
		akVertices[k + 0] = { fX0, fY0, fZ, fR, fG, fB, fA, 0.0f, 0.0f };
		akVertices[k + 1] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
		akVertices[k + 2] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
		akVertices[k + 3] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
		akVertices[k + 4] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
		akVertices[k + 5] = { fX1, fY1, fZ, fR, fG, fB, fA, 1.0f, 1.0f };
	}

	if (iActors > 0)
	{
		const int iActorCols = 20;
		const int iActorRows = std::max(1, (iActors + iActorCols - 1) / iActorCols);
		const float fActorCellW = 1.70f / static_cast<float>(iActorCols);
		const float fActorCellH = 0.72f / static_cast<float>(iActorRows);
		const float fActorW = fActorCellW * 0.68f;
		const float fActorH = fActorCellH * 0.86f;
		const float fActorStartX = -0.86f;
		const float fActorStartY = 0.62f;
		for (int i = 0; i < iActors; ++i)
		{
			const int iCol = i % iActorCols;
			const int iRow = i / iActorCols;
			const float fCX = fActorStartX + fActorCellW * static_cast<float>(iCol) + (fActorCellW * 0.5f);
			const float fCY = fActorStartY - fActorCellH * static_cast<float>(iRow) - (fActorCellH * 0.5f);
			const float fX0 = fCX - (fActorW * 0.5f);
			const float fY0 = fCY + (fActorH * 0.5f);
			const float fX1 = fCX + (fActorW * 0.5f);
			const float fY1 = fCY - (fActorH * 0.5f);
			const float fDepthT = static_cast<float>(iRow) / static_cast<float>(iActorRows);
			const float fZ = 0.56f - (0.20f * fDepthT);
			const float fR = 0.62f * fSceneInstanceScale;
			const float fG = 0.66f * fSceneCRCScale;
			const float fB = 0.82f * fSceneSplatScale;
			const float fA = 0.64f;
			const int k = iActorOffset + i * kVerticesPerQuad;
			akVertices[k + 0] = { fX0, fY0, fZ, fR, fG, fB, fA, 0.0f, 0.0f };
			akVertices[k + 1] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 2] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 3] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 4] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 5] = { fX1, fY1, fZ, fR, fG, fB, fA, 1.0f, 1.0f };
		}
	}

	if (iFX > 0)
	{
		const int iFXCols = 18;
		const int iFXRows = std::max(1, (iFX + iFXCols - 1) / iFXCols);
		const float fFXCellW = 1.64f / static_cast<float>(iFXCols);
		const float fFXCellH = 0.66f / static_cast<float>(iFXRows);
		const float fFXW = fFXCellW * 0.92f;
		const float fFXH = fFXCellH * 0.92f;
		const float fFXStartX = -0.82f;
		const float fFXStartY = 0.56f;
		for (int i = 0; i < iFX; ++i)
		{
			const int iCol = i % iFXCols;
			const int iRow = i / iFXCols;
			const float fCX = fFXStartX + fFXCellW * static_cast<float>(iCol) + (fFXCellW * 0.5f);
			const float fCY = fFXStartY - fFXCellH * static_cast<float>(iRow) - (fFXCellH * 0.5f);
			const float fX0 = fCX - (fFXW * 0.5f);
			const float fY0 = fCY + (fFXH * 0.5f);
			const float fX1 = fCX + (fFXW * 0.5f);
			const float fY1 = fCY - (fFXH * 0.5f);
			const float fDepthT = static_cast<float>(iRow) / static_cast<float>(iFXRows);
			const float fZ = 0.50f - (0.18f * fDepthT);
			const float fTexturePulse = 0.75f + (0.25f * static_cast<float>(iSceneTextureCount % 10) / 9.0f);
			const float fR = 0.24f * fTexturePulse;
			const float fG = 0.44f * fSceneSplatScale;
			const float fB = 0.74f * fSceneInstanceScale;
			const float fA = 0.36f;
			const int k = iFXOffset + i * kVerticesPerQuad;
			akVertices[k + 0] = { fX0, fY0, fZ, fR, fG, fB, fA, 0.0f, 0.0f };
			akVertices[k + 1] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 2] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 3] = { fX0, fY1, fZ, fR, fG, fB, fA, 0.0f, 1.0f };
			akVertices[k + 4] = { fX1, fY0, fZ, fR, fG, fB, fA, 1.0f, 0.0f };
			akVertices[k + 5] = { fX1, fY1, fZ, fR, fG, fB, fA, 1.0f, 1.0f };
		}
	}

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hMapResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hMapResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(bootstrap_vertex_buffer)", hMapResult);
		if (SUCCEEDED(hMapResult) && !kMappedResource.pData)
			TraceError("DX11_MAP_NULL_DATA op=ID3D11DeviceContext::Map(bootstrap_vertex_buffer)");
		return false;
	}

	memcpy(kMappedResource.pData, &akVertices[0], sizeof(SBootstrapVertex) * akVertices.size());
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapPixelShader, NULL, 0);
	const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_pDeviceContext->OMSetBlendState(NULL, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthEnableState, 0);
	m_pDeviceContext->Draw(static_cast<UINT>(iTerrainVertices), static_cast<UINT>(iTerrainOffset));
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>(iTerrainVertices / 3);
	if (iActorVertices > 0)
	{
		m_pDeviceContext->OMSetBlendState(m_pBootstrapAlphaBlendState, afBlendFactor, 0xFFFFFFFFu);
		m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthEnableState, 0);
		m_pDeviceContext->Draw(static_cast<UINT>(iActorVertices), static_cast<UINT>(iActorOffset));
		++m_uFrameDrawCalls;
		m_uFramePrimitiveCount += static_cast<UINT>(iActorVertices / 3);
	}
	if (iFXVertices > 0)
	{
		m_pDeviceContext->OMSetBlendState(m_pBootstrapAdditiveBlendState, afBlendFactor, 0xFFFFFFFFu);
		m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthReadState, 0);
		m_pDeviceContext->Draw(static_cast<UINT>(iFXVertices), static_cast<UINT>(iFXOffset));
		++m_uFrameDrawCalls;
		m_uFramePrimitiveCount += static_cast<UINT>(iFXVertices / 3);
	}
	m_pDeviceContext->OMSetDepthStencilState(NULL, 0);
	m_pDeviceContext->OMSetBlendState(NULL, afBlendFactor, 0xFFFFFFFFu);

	static DWORD s_dwDX11TerrainProtoLogCounter = 0;
	if (0 == s_dwDX11TerrainProtoLogCounter || 0 == (s_dwDX11TerrainProtoLogCounter % 1800u))
	{
		TraceError(
			"DX11_WORLD_NATIVE_TERRAIN_PROTO draw_ok=1 mode=%s tiles=%d actors=%d fx=%d rows=%d cols=%d scene_patch=%d scene_splat=%d scene_ratio=%.3f scene_tex=%d scene_inst=%u scene_crc=%u",
			bIncludeActorsAndFX ? "active" : "warmup",
			iTiles,
			iActors,
			iFX,
			iRows,
			iCols,
			m_iNativeWorldScenePatchCount,
			m_iNativeWorldSceneSplatCount,
			m_fNativeWorldSceneSplatRatio,
			m_iNativeWorldSceneTextureCount,
			m_dwNativeWorldSceneThingInstances,
			m_dwNativeWorldSceneCRCCount);
	}
	if (s_dwDX11TerrainProtoLogCounter < 0xffffffffu)
		++s_dwDX11TerrainProtoLogCounter;

	return true;
}

bool CGraphicDeviceDX11::PresentNativeWorld(bool bDrawNativeCursorOverlay, float fCursorX, float fCursorY)
{
	m_bUsingNativeWorldPresentPath = true;

	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
	{
		static DWORD s_dwPresentNativeWorldInvalidStateLogTick = 0;
		const DWORD dwNow = GetTickCount();
		if (0 == s_dwPresentNativeWorldInvalidStateLogTick || dwNow - s_dwPresentNativeWorldInvalidStateLogTick >= 3000u)
		{
			TraceError(
				"DX11_PRESENT_NATIVE_WORLD_INVALID_STATE device=%d context=%d rtv=%d",
				m_pDevice ? 1 : 0,
				m_pDeviceContext ? 1 : 0,
				m_pRenderTargetView ? 1 : 0);
			s_dwPresentNativeWorldInvalidStateLogTick = dwNow;
		}
		return false;
	}

	// Instrumentation: log present path state (handoff task: diagnose black-screen)
	{
		static DWORD s_dwPresentNativeWorldStateLogTick = 0;
		const DWORD dwNow = GetTickCount();
		if (0 == s_dwPresentNativeWorldStateLogTick || dwNow - s_dwPresentNativeWorldStateLogTick >= 2500u)
		{
			// Query current render targets to verify state
			ID3D11RenderTargetView* apCurrentRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { NULL };
			ID3D11DepthStencilView* pCurrentDSV = NULL;
			m_pDeviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, apCurrentRTVs, &pCurrentDSV);

			// Query backbuffer dimensions
			UINT uBackbufferWidth = 0;
			UINT uBackbufferHeight = 0;
			if (m_pRenderTargetView)
			{
				ID3D11Resource* pBackbufferResource = NULL;
				m_pRenderTargetView->GetResource(&pBackbufferResource);
				if (pBackbufferResource)
				{
					ID3D11Texture2D* pBackbufferTex = NULL;
					HRESULT hQueryTex = pBackbufferResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pBackbufferTex);
					if (SUCCEEDED(hQueryTex) && pBackbufferTex)
					{
						D3D11_TEXTURE2D_DESC kBackbufferDesc;
						pBackbufferTex->GetDesc(&kBackbufferDesc);
						uBackbufferWidth = kBackbufferDesc.Width;
						uBackbufferHeight = kBackbufferDesc.Height;
						pBackbufferTex->Release();
					}
					pBackbufferResource->Release();
				}
			}

			// Query active viewport
			UINT uNumViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			D3D11_VIEWPORT akViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
			m_pDeviceContext->RSGetViewports(&uNumViewports, akViewports);

			const D3D11_VIEWPORT& kActiveViewport = (uNumViewports > 0) ? akViewports[0] : m_kViewport;

			TraceError(
				"DX11_PRESENT_NATIVE_WORLD_STATE backbuffer_w=%u backbuffer_h=%u viewport_w=%.0f viewport_h=%.0f "
				"active_rtv=%d active_dsv=%d stored_rtv=%d stored_dsv=%d cursor_overlay=%d scene_patches=%d scene_textures=%d",
				uBackbufferWidth,
				uBackbufferHeight,
				kActiveViewport.Width,
				kActiveViewport.Height,
				apCurrentRTVs[0] ? 1 : 0,
				pCurrentDSV ? 1 : 0,
				m_pRenderTargetView ? 1 : 0,
				m_pDepthStencilView ? 1 : 0,
				bDrawNativeCursorOverlay ? 1 : 0,
				m_iNativeWorldScenePatchCount,
				m_iNativeWorldSceneTextureCount);

			// Cleanup queried COM objects
			for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
			{
				if (apCurrentRTVs[i])
					apCurrentRTVs[i]->Release();
			}
			if (pCurrentDSV)
				pCurrentDSV->Release();

			s_dwPresentNativeWorldStateLogTick = dwNow;
		}
	}

	// Safety guard: explicit re-bind backbuffer RT + viewport before present
	// Reason: UI or other render passes may have changed active RT between world render and present
	// This ensures backbuffer (containing world content) is the active present target
	m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);
	m_pDeviceContext->RSSetViewports(1, &m_kViewport);

	// Deep diagnostic: backbuffer pixel sampling (emergency black-screen debug)
	// Purpose: Verify if backbuffer contains non-black pixels before present
	bool bBackbufferClearGuardShouldFailPresent = false;
	{
		static DWORD s_dwBackbufferSampleLogTick = 0;
		static DWORD s_dwBackbufferClearLikeStreak = 0;
		static DWORD s_dwBackbufferClearGuardLogTick = 0;
		static DWORD s_dwBackbufferSceneExpectedSinceTick = 0;
		static float s_fPrevSampleAvgR = -1.0f;
		static float s_fPrevSampleAvgG = -1.0f;
		static float s_fPrevSampleAvgB = -1.0f;
		static float s_fPrevSampleMaxChannel = -1.0f;
		const DWORD dwSampleNow = GetTickCount();
		if (0 == s_dwBackbufferSampleLogTick || dwSampleNow - s_dwBackbufferSampleLogTick >= 2500u)
		{
			s_dwBackbufferSampleLogTick = dwSampleNow;
			bool bBackbufferSampleValid = false;
			float fSampleAvgR = 0.0f;
			float fSampleAvgG = 0.0f;
			float fSampleAvgB = 0.0f;
			float fSampleVariance = 0.0f;
			float fSampleMaxChannel = 0.0f;

			// Get backbuffer texture from RTV
			ID3D11Resource* pBackbufferResource = NULL;
			m_pRenderTargetView->GetResource(&pBackbufferResource);

			if (pBackbufferResource)
			{
				ID3D11Texture2D* pBackbufferTex = NULL;
				HRESULT hQueryTex = pBackbufferResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pBackbufferTex);

				if (SUCCEEDED(hQueryTex) && pBackbufferTex)
				{
					D3D11_TEXTURE2D_DESC kBackbufferDesc;
					pBackbufferTex->GetDesc(&kBackbufferDesc);

					// Create small staging texture for sampling (8 pixels: 4x2)
					D3D11_TEXTURE2D_DESC kStagingDesc = kBackbufferDesc;
					kStagingDesc.Width = 4;
					kStagingDesc.Height = 2;
					kStagingDesc.MipLevels = 1;
					kStagingDesc.ArraySize = 1;
					kStagingDesc.SampleDesc.Count = 1;
					kStagingDesc.SampleDesc.Quality = 0;
					kStagingDesc.Usage = D3D11_USAGE_STAGING;
					kStagingDesc.BindFlags = 0;
					kStagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					kStagingDesc.MiscFlags = 0;

					ID3D11Texture2D* pStagingTex = NULL;
					HRESULT hCreateStaging = m_pDevice->CreateTexture2D(&kStagingDesc, NULL, &pStagingTex);

					if (SUCCEEDED(hCreateStaging) && pStagingTex)
					{
						// Sample 8 pixels from strategic locations in backbuffer
						// (top-left, top-mid, top-right, mid-left, mid-right, bottom-left, bottom-mid, bottom-right)
						const UINT uSamplePoints[8][2] = {
							{ kBackbufferDesc.Width / 4,     kBackbufferDesc.Height / 4 },     // top-left
							{ kBackbufferDesc.Width / 2,     kBackbufferDesc.Height / 4 },     // top-mid
							{ kBackbufferDesc.Width * 3 / 4, kBackbufferDesc.Height / 4 },     // top-right
							{ kBackbufferDesc.Width / 4,     kBackbufferDesc.Height / 2 },     // mid-left
							{ kBackbufferDesc.Width * 3 / 4, kBackbufferDesc.Height / 2 },     // mid-right
							{ kBackbufferDesc.Width / 4,     kBackbufferDesc.Height * 3 / 4 }, // bottom-left
							{ kBackbufferDesc.Width / 2,     kBackbufferDesc.Height * 3 / 4 }, // bottom-mid
							{ kBackbufferDesc.Width * 3 / 4, kBackbufferDesc.Height * 3 / 4 }  // bottom-right
						};

						// Copy sample pixels to staging (as 4x2 grid)
						for (UINT i = 0; i < 8; ++i)
						{
							D3D11_BOX kSrcBox;
							kSrcBox.left = uSamplePoints[i][0];
							kSrcBox.right = uSamplePoints[i][0] + 1;
							kSrcBox.top = uSamplePoints[i][1];
							kSrcBox.bottom = uSamplePoints[i][1] + 1;
							kSrcBox.front = 0;
							kSrcBox.back = 1;

							UINT uDstX = i % 4;
							UINT uDstY = i / 4;
							m_pDeviceContext->CopySubresourceRegion(pStagingTex, 0, uDstX, uDstY, 0, pBackbufferTex, 0, &kSrcBox);
						}

						// Map staging and read pixels
						D3D11_MAPPED_SUBRESOURCE kMapped;
						ZeroMemory(&kMapped, sizeof(kMapped));
						HRESULT hMap = m_pDeviceContext->Map(pStagingTex, 0, D3D11_MAP_READ, 0, &kMapped);

						if (SUCCEEDED(hMap) && kMapped.pData)
						{
							// Read 8 pixels and calculate color variance
							float fTotalR = 0.0f, fTotalG = 0.0f, fTotalB = 0.0f;
							float fMaxChannel = 0.0f;

							for (UINT y = 0; y < 2; ++y)
							{
								for (UINT x = 0; x < 4; ++x)
								{
									const BYTE* pPixel = (const BYTE*)kMapped.pData + (y * kMapped.RowPitch) + (x * 4);
									float r = pPixel[2] / 255.0f; // BGRA format
									float g = pPixel[1] / 255.0f;
									float b = pPixel[0] / 255.0f;

									fTotalR += r;
									fTotalG += g;
									fTotalB += b;

									fMaxChannel = std::max(fMaxChannel, std::max(r, std::max(g, b)));
								}
							}

							float fAvgR = fTotalR / 8.0f;
							float fAvgG = fTotalG / 8.0f;
							float fAvgB = fTotalB / 8.0f;
							float fVariance = (fAvgR + fAvgG + fAvgB) / 3.0f;
							bBackbufferSampleValid = true;
							fSampleAvgR = fAvgR;
							fSampleAvgG = fAvgG;
							fSampleAvgB = fAvgB;
							fSampleVariance = fVariance;
							fSampleMaxChannel = fMaxChannel;

							const bool bFirstSample = (s_fPrevSampleMaxChannel < 0.0f);
							const bool bSampleChanged =
								(std::fabs(fAvgR - s_fPrevSampleAvgR) >= 0.02f) ||
								(std::fabs(fAvgG - s_fPrevSampleAvgG) >= 0.02f) ||
								(std::fabs(fAvgB - s_fPrevSampleAvgB) >= 0.02f) ||
								(std::fabs(fMaxChannel - s_fPrevSampleMaxChannel) >= 0.03f);
							const bool bSampleHeartbeat = bFirstSample || (0 == s_dwBackbufferClearGuardLogTick) || (dwSampleNow - s_dwBackbufferClearGuardLogTick >= 15000u);
							if (bSampleChanged || bSampleHeartbeat)
							{
								TraceError(
									"DX11_PRESENT_BACKBUFFER_SAMPLE avg_r=%.3f avg_g=%.3f avg_b=%.3f variance=%.3f max_channel=%.3f backbuffer_w=%u backbuffer_h=%u patches=%d",
									fAvgR, fAvgG, fAvgB, fVariance, fMaxChannel,
									kBackbufferDesc.Width, kBackbufferDesc.Height, m_iNativeWorldScenePatchCount);
								s_dwBackbufferClearGuardLogTick = dwSampleNow;
							}
							s_fPrevSampleAvgR = fAvgR;
							s_fPrevSampleAvgG = fAvgG;
							s_fPrevSampleAvgB = fAvgB;
							s_fPrevSampleMaxChannel = fMaxChannel;

							m_pDeviceContext->Unmap(pStagingTex, 0);
						}

						pStagingTex->Release();
					}

					pBackbufferTex->Release();
				}

				pBackbufferResource->Release();
			}

			if (bBackbufferSampleValid)
			{
				static DWORD s_dwBackbufferTripwireLogTick = 0;
				static DWORD s_dwBackbufferTripwireRepeatCount = 0;
				// Guard should catch persistent clear-color regressions, not brief dark frames.
				// Use fast detection so runtime can recover from persistent clear-like presents.
				static const DWORD kBackbufferGuardArmDelayMS = 6000u;
				static const DWORD kBackbufferGuardFailStreak = 4u;
				static const float kBackbufferClearBaseline = 0.020f;
				static const float kBackbufferClearEpsilon = 0.004f;
				const bool bSceneExpected =
					(m_iNativeWorldScenePatchCount >= 32) &&
					(m_iNativeWorldSceneTextureCount >= 2) &&
					(m_iNativeWorldSceneSplatCount >= 96);
				if (bSceneExpected)
				{
					if (0 == s_dwBackbufferSceneExpectedSinceTick)
						s_dwBackbufferSceneExpectedSinceTick = dwSampleNow;
				}
				else
				{
					s_dwBackbufferSceneExpectedSinceTick = 0;
					s_dwBackbufferClearLikeStreak = 0;
				}
				const bool bBackbufferGuardArmed =
					(0 != s_dwBackbufferSceneExpectedSinceTick) &&
					(dwSampleNow >= s_dwBackbufferSceneExpectedSinceTick) &&
					((dwSampleNow - s_dwBackbufferSceneExpectedSinceTick) >= kBackbufferGuardArmDelayMS);
				const bool bClearLikeFrame =
					bSceneExpected &&
					(fSampleMaxChannel <= 0.022f) &&
					(fSampleVariance <= 0.022f);
				const bool bTripwireFrame =
					(m_iNativeWorldScenePatchCount > 0) &&
					(fSampleMaxChannel <= (kBackbufferClearBaseline + kBackbufferClearEpsilon));
				if (bClearLikeFrame)
				{
					if (s_dwBackbufferClearLikeStreak < 0xffffffffu)
						++s_dwBackbufferClearLikeStreak;
				}
				else
				{
					s_dwBackbufferClearLikeStreak = 0;
				}

				if (bTripwireFrame)
				{
					if (s_dwBackbufferTripwireRepeatCount < 0xffffffffu)
						++s_dwBackbufferTripwireRepeatCount;

					if (0 == s_dwBackbufferTripwireLogTick || (dwSampleNow - s_dwBackbufferTripwireLogTick) >= 2000u)
					{
						s_dwBackbufferTripwireLogTick = dwSampleNow;
						TraceError(
							"DX11_PRESENT_BLACK_FRAME_TRIPWIRE scene_ready=%d repeats=%u avg_r=%.3f avg_g=%.3f avg_b=%.3f variance=%.3f max_channel=%.3f baseline=%.3f epsilon=%.3f scene_patches=%d scene_textures=%d scene_splat=%d",
							bSceneExpected ? 1 : 0,
							static_cast<unsigned int>(s_dwBackbufferTripwireRepeatCount),
							fSampleAvgR,
							fSampleAvgG,
							fSampleAvgB,
							fSampleVariance,
							fSampleMaxChannel,
							kBackbufferClearBaseline,
							kBackbufferClearEpsilon,
							m_iNativeWorldScenePatchCount,
							m_iNativeWorldSceneTextureCount,
							m_iNativeWorldSceneSplatCount);
						__LogDX11PresentTripwireSnapshot(
							m_pDeviceContext,
							m_pRenderTargetView,
							m_pDepthStencilView,
							m_iNativeWorldScenePatchCount,
							m_iNativeWorldSceneTextureCount,
							m_iNativeWorldSceneSplatCount,
							fSampleAvgR,
							fSampleAvgG,
							fSampleAvgB,
							fSampleVariance,
							fSampleMaxChannel,
							s_dwBackbufferTripwireRepeatCount);
					}
				}
				else
				{
					s_dwBackbufferTripwireRepeatCount = 0;
				}

				// Guard native present path when scene is clearly being drawn but presented frame
				// repeatedly collapses to clear-color baseline.
				if (bBackbufferGuardArmed && bClearLikeFrame && s_dwBackbufferClearLikeStreak >= kBackbufferGuardFailStreak)
				{
					bBackbufferClearGuardShouldFailPresent = true;
					if (0 == s_dwBackbufferClearGuardLogTick || dwSampleNow - s_dwBackbufferClearGuardLogTick >= 3000u)
					{
						s_dwBackbufferClearGuardLogTick = dwSampleNow;
						TraceError(
							"DX11_PRESENT_BACKBUFFER_GUARD trigger=clear_like_frame streak=%u arm_delay_ms=%u avg_r=%.3f avg_g=%.3f avg_b=%.3f variance=%.3f max_channel=%.3f scene_patches=%d scene_textures=%d",
							s_dwBackbufferClearLikeStreak,
							kBackbufferGuardArmDelayMS,
							fSampleAvgR,
							fSampleAvgG,
							fSampleAvgB,
							fSampleVariance,
							fSampleMaxChannel,
							m_iNativeWorldScenePatchCount,
							m_iNativeWorldSceneTextureCount);
					}
				}
			}
		}
	}
	if (bBackbufferClearGuardShouldFailPresent)
	{
		// Keep this guard diagnostic-only for now.
		// In field runs we observed false positives on valid world frames (clear-like sample with active draw telemetry),
		// and fatal guard was forcing avoidable native-present backoff loops.
		static const bool kDX11BackbufferGuardFatal = false;
		static DWORD s_dwNativeWorldBackbufferGuardFailLogTick = 0;
		const DWORD dwNow = GetTickCount();
		const DWORD dwGuardLogIntervalMS = kDX11BackbufferGuardFatal ? 3000u : 30000u;
		if (0 == s_dwNativeWorldBackbufferGuardFailLogTick || dwNow - s_dwNativeWorldBackbufferGuardFailLogTick >= dwGuardLogIntervalMS)
		{
			if (kDX11BackbufferGuardFatal)
			{
				TraceError(
					"DX11_PRESENT_NATIVE_WORLD_FAIL reason=backbuffer_guard scene_patches=%d scene_textures=%d scene_splat=%d",
					m_iNativeWorldScenePatchCount,
					m_iNativeWorldSceneTextureCount,
					m_iNativeWorldSceneSplatCount);
			}
			else
			{
				TraceError(
					"DX11_PRESENT_BACKBUFFER_GUARD action=non_fatal scene_patches=%d scene_textures=%d scene_splat=%d",
					m_iNativeWorldScenePatchCount,
					m_iNativeWorldSceneTextureCount,
					m_iNativeWorldSceneSplatCount);
			}
			s_dwNativeWorldBackbufferGuardFailLogTick = dwNow;
		}
		if (kDX11BackbufferGuardFatal)
			return false;
	}

	// Reset baseline state before final present-side overlays.
	const FLOAT afBlendFactorReset[4] = { 0.f, 0.f, 0.f, 0.f };
	ID3D11ShaderResourceView* apNullSRV[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	ID3D11SamplerState* apNullSampler[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	ID3D11Buffer* apNullCB[4] = { nullptr, nullptr, nullptr, nullptr };
	ID3D11Buffer* apNullVB[1] = { nullptr };
	UINT uZero = 0u;
	m_pDeviceContext->VSSetShader(nullptr, nullptr, 0);
	m_pDeviceContext->PSSetShader(nullptr, nullptr, 0);
	m_pDeviceContext->GSSetShader(nullptr, nullptr, 0);
	m_pDeviceContext->HSSetShader(nullptr, nullptr, 0);
	m_pDeviceContext->DSSetShader(nullptr, nullptr, 0);
	m_pDeviceContext->CSSetShader(nullptr, nullptr, 0);
	m_pDeviceContext->IASetInputLayout(nullptr);
	m_pDeviceContext->IASetVertexBuffers(0, 1, apNullVB, &uZero, &uZero);
	m_pDeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
	m_pDeviceContext->PSSetShaderResources(0, 8, apNullSRV);
	m_pDeviceContext->VSSetShaderResources(0, 8, apNullSRV);
	m_pDeviceContext->PSSetSamplers(0, 8, apNullSampler);
	m_pDeviceContext->VSSetSamplers(0, 8, apNullSampler);
	m_pDeviceContext->VSSetConstantBuffers(0, 4, apNullCB);
	m_pDeviceContext->PSSetConstantBuffers(0, 4, apNullCB);
	m_pDeviceContext->OMSetBlendState(nullptr, afBlendFactorReset, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(nullptr, 0u);
	m_pDeviceContext->RSSetState(nullptr);

	if (bDrawNativeCursorOverlay)
	{
		if (!__DrawNativeCursorOverlay(fCursorX, fCursorY))
		{
			static DWORD s_dwNativeCursorOverlayFailLogTick = 0;
			const DWORD dwNow = GetTickCount();
			if (0 == s_dwNativeCursorOverlayFailLogTick || dwNow - s_dwNativeCursorOverlayFailLogTick >= 30000u)
			{
				TraceError("DX11_NATIVE_CURSOR_OVERLAY_FAIL path=native_world_present fallback=continue");
				s_dwNativeCursorOverlayFailLogTick = dwNow;
			}
		}
	}

	const bool bPresentOk = Present();
	if (!bPresentOk)
	{
		static DWORD s_dwNativeWorldPresentFailLogTick = 0;
		const DWORD dwNow = GetTickCount();
		if (0 == s_dwNativeWorldPresentFailLogTick || dwNow - s_dwNativeWorldPresentFailLogTick >= 1000u)
		{
			TraceError(
				"DX11_PRESENT_NATIVE_WORLD_FAIL reason=swapchain_present_failed vsync=%d scene_patches=%d scene_textures=%d",
				m_isVSyncEnabled ? 1 : 0,
				m_iNativeWorldScenePatchCount,
				m_iNativeWorldSceneTextureCount);
			s_dwNativeWorldPresentFailLogTick = dwNow;
		}
	}
	return bPresentOk;
}

bool CGraphicDeviceDX11::PresentNativeWorldDryRun(bool bDrawNativeCursorOverlay, float fCursorX, float fCursorY)
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;

	m_bUsingNativeWorldPresentPath = true;
	m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);
	m_pDeviceContext->RSSetViewports(1, &m_kViewport);

	if (bDrawNativeCursorOverlay)
	{
		if (!__DrawNativeCursorOverlay(fCursorX, fCursorY))
		{
			static DWORD s_dwNativeCursorOverlayDryRunFailLogTick = 0;
			const DWORD dwNow = GetTickCount();
			if (0 == s_dwNativeCursorOverlayDryRunFailLogTick || dwNow - s_dwNativeCursorOverlayDryRunFailLogTick >= 30000u)
			{
				TraceError("DX11_NATIVE_CURSOR_OVERLAY_FAIL path=native_world_dryrun fallback=continue");
				s_dwNativeCursorOverlayDryRunFailLogTick = dwNow;
			}
		}
	}

	return Present();
}

bool CGraphicDeviceDX11::PresentVisibleBridgeTexture(bool bDrawNativeCursorOverlay, float fCursorX, float fCursorY)
{
	m_bUsingNativeWorldPresentPath = false;

	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
	{
		static DWORD s_dwPresentBridgeInvalidStateLogTick = 0;
		const DWORD dwNow = GetTickCount();
		if (0 == s_dwPresentBridgeInvalidStateLogTick || dwNow - s_dwPresentBridgeInvalidStateLogTick >= 3000u)
		{
			TraceError(
				"DX11_PRESENT_BRIDGE_INVALID_STATE device=%d context=%d rtv=%d",
				m_pDevice ? 1 : 0,
				m_pDeviceContext ? 1 : 0,
				m_pRenderTargetView ? 1 : 0);
			s_dwPresentBridgeInvalidStateLogTick = dwNow;
		}
		return false;
	}
	if (!m_pVisibleBridgeTexture || !m_pVisibleBridgeTextureSRV)
	{
		static DWORD s_dwPresentBridgeMissingTextureLogTick = 0;
		const DWORD dwNow = GetTickCount();
		if (0 == s_dwPresentBridgeMissingTextureLogTick || dwNow - s_dwPresentBridgeMissingTextureLogTick >= 3000u)
		{
			TraceError(
				"DX11_PRESENT_BRIDGE_MISSING_TEXTURE tex=%d srv=%d width=%u height=%u",
				m_pVisibleBridgeTexture ? 1 : 0,
				m_pVisibleBridgeTextureSRV ? 1 : 0,
				m_uVisibleBridgeWidth,
				m_uVisibleBridgeHeight);
			s_dwPresentBridgeMissingTextureLogTick = dwNow;
		}
		return false;
	}
	if (!m_isBootstrapPipelineReady)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const SBootstrapVertex akFullscreenQuad[6] =
	{
		{ -1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f },
		{  1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f },
		{ -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f },
		{ -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f },
		{  1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f },
		{  1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
	};

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hResult = S_OK;
	hResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(visible_bridge_quad)", hResult);
		return false;
	}

	memcpy(kMappedResource.pData, akFullscreenQuad, sizeof(akFullscreenQuad));
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);
	m_pDeviceContext->RSSetViewports(1, &m_kViewport);
	m_pDeviceContext->OMSetBlendState(NULL, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthDisableState, 0);
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapUIPixelShader, NULL, 0);
	m_pDeviceContext->PSSetShaderResources(0, 1, &m_pVisibleBridgeTextureSRV);
	m_pDeviceContext->PSSetSamplers(0, 1, &m_pBootstrapUISamplerState);
	m_pDeviceContext->Draw(6, 0);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((6) / 3);
	ID3D11ShaderResourceView* pNullSRV = NULL;
	m_pDeviceContext->PSSetShaderResources(0, 1, &pNullSRV);
	if (bDrawNativeCursorOverlay)
	{
		if (!__DrawNativeCursorOverlay(fCursorX, fCursorY))
		{
			static DWORD s_dwBridgeCursorOverlayFailLogTick = 0;
			const DWORD dwNow = GetTickCount();
			if (0 == s_dwBridgeCursorOverlayFailLogTick || dwNow - s_dwBridgeCursorOverlayFailLogTick >= 30000u)
			{
				TraceError("DX11_NATIVE_CURSOR_OVERLAY_FAIL path=visible_bridge_present fallback=continue");
				s_dwBridgeCursorOverlayFailLogTick = dwNow;
			}
		}
	}
	m_pDeviceContext->OMSetDepthStencilState(NULL, 0);

	return Present();
}

void CGraphicDeviceDX11::BindMainRenderTargets()
{
	if (!m_pDeviceContext || !m_pRenderTargetView)
		return;

	m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);
	if (m_kViewport.Width > 0.0f && m_kViewport.Height > 0.0f)
		m_pDeviceContext->RSSetViewports(1, &m_kViewport);
}

// M2-ETERLIB-STATE-48: Explicit state baseline helpers for UI/text rendering
void CGraphicDeviceDX11::SetUI2DBaselineState()
{
	if (!m_pDeviceContext || !m_isBootstrapPipelineReady)
		return;

	// M2-ETERLIB-STATE-48: Set UI 2D baseline state (Blend: Alpha, Depth: Disable, Raster: Default, Sampler: Linear)
	const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_pDeviceContext->OMSetBlendState(m_pBootstrapAlphaBlendState, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthDisableState, 0);
	m_pDeviceContext->RSSetState(m_pBootstrapRasterizerState);
	if (m_pBootstrapUISamplerState)
		m_pDeviceContext->PSSetSamplers(0, 1, &m_pBootstrapUISamplerState);

	// M2-ETERLIB-STATE-48: Throttled telemetry for UI state baseline verification (30s cadence)
	static DWORD s_dwLastUIStateBaselineLog = 0;
	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwLastUIStateBaselineLog > 30000)
	{
		s_dwLastUIStateBaselineLog = dwNow;
		TraceError("DX11_UI_STATE_BASELINE blend=alpha_depth=disable_raster=default sampler=linear");
	}
}

void CGraphicDeviceDX11::SetUITextBaselineState()
{
	if (!m_pDeviceContext || !m_isBootstrapPipelineReady)
		return;

	// M2-ETERLIB-STATE-48: Set UI text baseline state (Blend: LCD Pass1, Depth: Disable, Raster: Default, Sampler: Point)
	const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_pDeviceContext->OMSetBlendState(m_pBootstrapLCDPass1BlendState, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthDisableState, 0);
	m_pDeviceContext->RSSetState(m_pBootstrapRasterizerState);
	// Note: Point sampler is set by text rendering code itself

	// M2-ETERLIB-STATE-48: Throttled telemetry for text state baseline verification (30s cadence)
	static DWORD s_dwLastTextStateBaselineLog = 0;
	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwLastTextStateBaselineLog > 30000)
	{
		s_dwLastTextStateBaselineLog = dwNow;
		TraceError("DX11_TEXT_STATE_BASELINE blend=lcd_pass1_depth=disable_raster=default sampler=point");
	}
}

bool CGraphicDeviceDX11::__DrawNativeCursorOverlay(float fCursorX, float fCursorY)
{
	if (!m_pDevice || !m_pDeviceContext || !m_pRenderTargetView)
		return false;
	if (0 == m_uWidth || 0 == m_uHeight)
		return false;
	if (!m_isBootstrapPipelineReady ||
		!m_pBootstrapVertexBuffer ||
		!m_pBootstrapInputLayout ||
		!m_pBootstrapVertexShader ||
		!m_pBootstrapPixelShader ||
		!m_pBootstrapAlphaBlendState ||
		!m_pBootstrapDepthDisableState)
	{
		if (!__CreateBootstrapPipeline())
			return false;
	}

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	auto PixelToNDCX = [this](float x) -> float
	{
		return (2.0f * x / static_cast<float>(m_uWidth)) - 1.0f;
	};
	auto PixelToNDCY = [this](float y) -> float
	{
		return 1.0f - (2.0f * y / static_cast<float>(m_uHeight));
	};

	const float fClampedCursorX = std::max(0.0f, std::min(fCursorX, static_cast<float>(m_uWidth - 1)));
	const float fClampedCursorY = std::max(0.0f, std::min(fCursorY, static_cast<float>(m_uHeight - 1)));
	const float fCursorSize = 14.0f;
	const float fCursorTipX = fClampedCursorX;
	const float fCursorTipY = fClampedCursorY;
	const float fCursorRightX = std::min(static_cast<float>(m_uWidth), fCursorTipX + fCursorSize);
	const float fCursorRightY = std::min(static_cast<float>(m_uHeight), fCursorTipY + (fCursorSize * 0.35f));
	const float fCursorBottomX = std::min(static_cast<float>(m_uWidth), fCursorTipX + (fCursorSize * 0.35f));
	const float fCursorBottomY = std::min(static_cast<float>(m_uHeight), fCursorTipY + fCursorSize);
	const float fCursorMidX = std::min(static_cast<float>(m_uWidth), fCursorTipX + (fCursorSize * 0.58f));
	const float fCursorMidY = std::min(static_cast<float>(m_uHeight), fCursorTipY + (fCursorSize * 0.72f));
	const float fCursorTailX = std::min(static_cast<float>(m_uWidth), fCursorTipX + (fCursorSize * 0.96f));
	const float fCursorTailY = std::min(static_cast<float>(m_uHeight), fCursorTipY + (fCursorSize * 1.14f));

	const SBootstrapVertex akCursorVertices[9] =
	{
		// Cursor head (arrow triangle)
		{ PixelToNDCX(fCursorTipX),    PixelToNDCY(fCursorTipY),    0.0f, 1.00f, 0.97f, 0.74f, 1.0f, 0.0f, 0.0f },
		{ PixelToNDCX(fCursorRightX),  PixelToNDCY(fCursorRightY),  0.0f, 0.98f, 0.90f, 0.42f, 1.0f, 1.0f, 0.0f },
		{ PixelToNDCX(fCursorBottomX), PixelToNDCY(fCursorBottomY), 0.0f, 0.90f, 0.78f, 0.28f, 1.0f, 0.0f, 1.0f },
		// Cursor stem (upper triangle)
		{ PixelToNDCX(fCursorBottomX), PixelToNDCY(fCursorBottomY), 0.0f, 0.88f, 0.72f, 0.24f, 1.0f, 0.0f, 1.0f },
		{ PixelToNDCX(fCursorRightX),  PixelToNDCY(fCursorRightY),  0.0f, 0.86f, 0.69f, 0.22f, 1.0f, 1.0f, 0.0f },
		{ PixelToNDCX(fCursorMidX),    PixelToNDCY(fCursorMidY),    0.0f, 0.82f, 0.63f, 0.20f, 1.0f, 1.0f, 1.0f },
		// Cursor stem (lower triangle)
		{ PixelToNDCX(fCursorBottomX), PixelToNDCY(fCursorBottomY), 0.0f, 0.80f, 0.60f, 0.19f, 1.0f, 0.0f, 1.0f },
		{ PixelToNDCX(fCursorMidX),    PixelToNDCY(fCursorMidY),    0.0f, 0.78f, 0.57f, 0.18f, 1.0f, 1.0f, 1.0f },
		{ PixelToNDCX(fCursorTailX),   PixelToNDCY(fCursorTailY),   0.0f, 0.72f, 0.50f, 0.16f, 1.0f, 1.0f, 1.0f },
	};

	D3D11_MAPPED_SUBRESOURCE kMappedResource;
	ZeroMemory(&kMappedResource, sizeof(kMappedResource));
	HRESULT hResult = m_pDeviceContext->Map(m_pBootstrapVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
	if (FAILED(hResult) || !kMappedResource.pData)
	{
		__LogDX11HResultFailure("ID3D11DeviceContext::Map(native_cursor_overlay)", hResult);
		return false;
	}

	memcpy(kMappedResource.pData, akCursorVertices, sizeof(akCursorVertices));
	m_pDeviceContext->Unmap(m_pBootstrapVertexBuffer, 0);

	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_pDeviceContext->OMSetBlendState(m_pBootstrapAlphaBlendState, afBlendFactor, 0xFFFFFFFFu);
	m_pDeviceContext->OMSetDepthStencilState(m_pBootstrapDepthDisableState, 0);
	m_pDeviceContext->IASetInputLayout(m_pBootstrapInputLayout);
	m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pBootstrapVertexBuffer, &uStride, &uOffset);
	m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pDeviceContext->VSSetShader(m_pBootstrapVertexShader, NULL, 0);
	m_pDeviceContext->PSSetShader(m_pBootstrapPixelShader, NULL, 0);
	m_pDeviceContext->Draw(9, 0);
	++m_uFrameDrawCalls;
	m_uFramePrimitiveCount += static_cast<UINT>((9) / 3);
	m_pDeviceContext->OMSetBlendState(NULL, afBlendFactor, 0xFFFFFFFFu);
	return true;
}

bool CGraphicDeviceDX11::Present()
{
	if (!m_pSwapChain)
		return false;
	#if defined(BUILD_DEBUG_UI)
	// Optional developer overlay and graphics metrics.
	if (CImGuiGraphicsMetrics::Instance())
	{
		CImGuiGraphicsMetrics::Instance()->EndFrame();
	}

	// DX11 Model Sync: Render ImGui Developer Monitoring Tool overlay
	if (CImGuiManager::Instance() && CImGuiManager::Instance()->IsEnabled())
	{
		CImGuiManager::Instance()->NewFrame();
		CImGuiManager::Instance()->RenderDX11Overlay();
	}

	// DX11 Model Sync: Begin frame for next graphics metrics collection
	if (CImGuiGraphicsMetrics::Instance())
	{
		CImGuiGraphicsMetrics::Instance()->BeginFrame();
	}
	#endif
	HRESULT hResult = m_pSwapChain->Present(m_isVSyncEnabled ? 1 : 0, 0);
	if (FAILED(hResult))
	{
		static DWORD s_dwPresentFailLastLog = 0;
		const DWORD dwNow = GetTickCount();
		if (0 == s_dwPresentFailLastLog || dwNow - s_dwPresentFailLastLog >= 1000u)
		{
			__LogDX11HResultFailure("IDXGISwapChain::Present", hResult);

			// If device was removed (0x887a0005 or 0x887a0006), get detailed reason
			if (hResult == DXGI_ERROR_DEVICE_REMOVED || hResult == DXGI_ERROR_DEVICE_HUNG)
			{
				if (m_pDevice)
				{
					HRESULT hrRemoved = m_pDevice->GetDeviceRemovedReason();
					__LogDX11HResultFailure("ID3D11Device::GetDeviceRemovedReason", hrRemoved);
				}
			}

			s_dwPresentFailLastLog = dwNow;
		}
	}
	else
	{
		static bool s_bDX11StartupFirstPresentSuccessLogged = false;
		if (!s_bDX11StartupFirstPresentSuccessLogged)
		{
			s_bDX11StartupFirstPresentSuccessLogged = true;
			TraceError(
				"DX11_STARTUP_TIMELINE event=first_present_success vsync=%d scene_patches=%d scene_textures=%d",
				m_isVSyncEnabled ? 1 : 0,
				m_iNativeWorldScenePatchCount,
				m_iNativeWorldSceneTextureCount);
		}
	}
	return SUCCEEDED(hResult);
}

bool CGraphicDeviceDX11::PresentTest()
{
	if (!m_pSwapChain)
		return false;

	HRESULT hResult = m_pSwapChain->Present(0, DXGI_PRESENT_TEST);
	if (FAILED(hResult))
	{
		static DWORD s_dwPresentTestFailLastLog = 0;
		const DWORD dwNow = GetTickCount();
		if (0 == s_dwPresentTestFailLastLog || dwNow - s_dwPresentTestFailLastLog >= 1000u)
		{
			__LogDX11HResultFailure("IDXGISwapChain::Present(TEST)", hResult);
			s_dwPresentTestFailLastLog = dwNow;
		}
	}
	return SUCCEEDED(hResult);
}

bool CGraphicDeviceDX11::SetVSyncEnabled(bool isEnabled)
{
	m_isVSyncEnabled = isEnabled ? true : false;
	return true;
}

void CGraphicDeviceDX11::SetNativeWorldSceneStats(
	int iPatchCount,
	int iSplatCount,
	float fSplatRatio,
	int iTextureCount,
	DWORD dwThingInstances,
	DWORD dwCRCCount)
{
	m_iNativeWorldScenePatchCount = std::max(0, iPatchCount);
	m_iNativeWorldSceneSplatCount = std::max(0, iSplatCount);
	m_fNativeWorldSceneSplatRatio = std::max(0.0f, fSplatRatio);
	m_iNativeWorldSceneTextureCount = std::max(0, iTextureCount);
	m_dwNativeWorldSceneThingInstances = dwThingInstances;
	m_dwNativeWorldSceneCRCCount = dwCRCCount;
}

void CGraphicDeviceDX11::SetNativeWorldPortMask(uint32_t dwMask)
{
	SetNativeWorldCommittedMask(dwMask);
}

uint32_t CGraphicDeviceDX11::GetNativeWorldPortMask() const
{
	return GetNativeWorldCommittedMask();
}

uint32_t CGraphicDeviceDX11::GetNativeWorldMissingPortMask() const
{
	return (WORLD_PORT_REQUIRED_MASK & ~m_dwNativeWorldCommittedMask);
}

uint32_t CGraphicDeviceDX11::GetNativeWorldRequiredEffectiveMask() const
{
	uint32_t dwRequiredEffectiveMask = (WORLD_PORT_REQUIRED_MASK & m_dwNativeWorldApplicableMask);
	// Terrain is always mandatory for world-ready evaluation.
	if (0u == (dwRequiredEffectiveMask & WORLD_TERRAIN_DX11))
		dwRequiredEffectiveMask |= WORLD_TERRAIN_DX11;
	return dwRequiredEffectiveMask;
}

bool CGraphicDeviceDX11::IsNativeWorldTerrainPrototypeEnabled() const
{
	// Old test/prototype world path is disabled to avoid fallback visuals.
	return false;
}

bool CGraphicDeviceDX11::IsNativeWorldRendererPorted() const
{
	const uint32_t dwRequiredEffectiveMask = GetNativeWorldRequiredEffectiveMask();
	return (0u == (dwRequiredEffectiveMask & ~m_dwNativeWorldCommittedMask));
}

void CGraphicDeviceDX11::SetNativeWorldObservedMask(uint32_t dwMask)
{
	m_dwNativeWorldObservedMask = (dwMask & WORLD_PORT_REQUIRED_MASK);
}

void CGraphicDeviceDX11::SetNativeWorldSubmittedMask(uint32_t dwMask)
{
	m_dwNativeWorldSubmittedMask = (dwMask & WORLD_PORT_REQUIRED_MASK);
}

void CGraphicDeviceDX11::SetNativeWorldApplicableMask(uint32_t dwMask)
{
	m_dwNativeWorldApplicableMask = (dwMask & WORLD_PORT_REQUIRED_MASK);
}

void CGraphicDeviceDX11::SetNativeWorldSubmittedSeenMask(uint32_t dwMask)
{
	m_dwNativeWorldSubmittedSeenMask = (dwMask & WORLD_PORT_REQUIRED_MASK);
}

void CGraphicDeviceDX11::SetNativeWorldCommittedMask(uint32_t dwMask)
{
	m_dwNativeWorldCommittedMask = (dwMask & WORLD_PORT_REQUIRED_MASK);
}

uint32_t CGraphicDeviceDX11::GetNativeWorldObservedMask() const
{
	return m_dwNativeWorldObservedMask;
}

uint32_t CGraphicDeviceDX11::GetNativeWorldSubmittedMask() const
{
	return m_dwNativeWorldSubmittedMask;
}

uint32_t CGraphicDeviceDX11::GetNativeWorldApplicableMask() const
{
	return m_dwNativeWorldApplicableMask;
}

uint32_t CGraphicDeviceDX11::GetNativeWorldSubmittedSeenMask() const
{
	return m_dwNativeWorldSubmittedSeenMask;
}

uint32_t CGraphicDeviceDX11::GetNativeWorldCommittedMask() const
{
	return m_dwNativeWorldCommittedMask;
}

void CGraphicDeviceDX11::SetDX11TexturePipelineMode(EDX11TexturePipelineMode eMode)
{
	switch (eMode)
	{
	case DX11_TEXTURE_PIPELINE_NATIVE:
	case DX11_TEXTURE_PIPELINE_HYBRID:
	case DX11_TEXTURE_PIPELINE_LEGACY:
		m_eDX11TexturePipelineMode = eMode;
		break;
	default:
		m_eDX11TexturePipelineMode = DX11_TEXTURE_PIPELINE_HYBRID;
		break;
	}
}

CGraphicDeviceDX11::EDX11TexturePipelineMode CGraphicDeviceDX11::GetDX11TexturePipelineMode() const
{
	return m_eDX11TexturePipelineMode;
}

bool CGraphicDeviceDX11::EnsureBootstrapPipelineReady()
{
	if (!m_isBootstrapPipelineReady)
		return __CreateBootstrapPipeline();

	return m_pBootstrapInputLayout &&
		m_pBootstrapVertexShader &&
		m_pBootstrapUIPixelShader &&
		m_pBootstrapVertexBuffer &&
		m_pBootstrapAlphaBlendState &&
		m_pBootstrapUICloudBlendState &&
		m_pBootstrapDepthDisableState;
}

bool CGraphicDeviceDX11::EnsureBootstrapUISamplerReady()
{
	if (!EnsureBootstrapPipelineReady())
		return false;

	if (!m_isBootstrapUITextureReady || !m_pBootstrapUISamplerState)
	{
		if (!__CreateBootstrapUITexture())
			return false;
	}

	return (m_pBootstrapUISamplerState != nullptr);
}

bool CGraphicDeviceDX11::__CreateRenderTarget()
{
	if (!m_pSwapChain || !m_pDevice || !m_pDeviceContext)
		return false;

	ID3D11Texture2D* pBackBuffer = NULL;
	HRESULT hResult = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("IDXGISwapChain::GetBuffer(backbuffer)", hResult);
		return false;
	}

	DirectX::SetDebugObjectName(pBackBuffer, "DX11.MainBackBuffer");

	hResult = m_pDevice->CreateRenderTargetView(pBackBuffer, NULL, &m_pRenderTargetView);
	SAFE_RELEASE(pBackBuffer);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateRenderTargetView(main)", hResult);
		return false;
	}

	DirectX::SetDebugObjectName(m_pRenderTargetView, "DX11.MainRenderTargetView");

	if (!__CreateDepthStencil())
		return false;

	m_kViewport.TopLeftX = 0.0f;
	m_kViewport.TopLeftY = 0.0f;
	m_kViewport.Width = static_cast<float>(m_uWidth);
	m_kViewport.Height = static_cast<float>(m_uHeight);
	m_kViewport.MinDepth = 0.0f;
	m_kViewport.MaxDepth = 1.0f;
	CGraphicBase::SetBackBufferSize(m_uWidth, m_uHeight);

	m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);
	m_pDeviceContext->RSSetViewports(1, &m_kViewport);
	return true;
}

void CGraphicDeviceDX11::__DestroyRenderTarget()
{
	__DestroyDepthStencil();
	SAFE_RELEASE(m_pRenderTargetView);
}

bool CGraphicDeviceDX11::__CreateDepthStencil()
{
	if (!m_pDevice)
		return false;

	D3D11_TEXTURE2D_DESC kDepthDesc;
	ZeroMemory(&kDepthDesc, sizeof(kDepthDesc));
	kDepthDesc.Width = m_uWidth;
	kDepthDesc.Height = m_uHeight;
	kDepthDesc.MipLevels = 1;
	kDepthDesc.ArraySize = 1;
	kDepthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	kDepthDesc.SampleDesc.Count = 1;
	kDepthDesc.SampleDesc.Quality = 0;
	kDepthDesc.Usage = D3D11_USAGE_DEFAULT;
	kDepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	HRESULT hResult = m_pDevice->CreateTexture2D(&kDepthDesc, NULL, &m_pDepthStencilBuffer);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateTexture2D(depth)", hResult);
		return false;
	}

	DirectX::SetDebugObjectName(m_pDepthStencilBuffer, "DX11.MainDepthStencilTexture");

	hResult = m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer, NULL, &m_pDepthStencilView);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateDepthStencilView(main)", hResult);
		SAFE_RELEASE(m_pDepthStencilBuffer);
		return false;
	}

	DirectX::SetDebugObjectName(m_pDepthStencilView, "DX11.MainDepthStencilView");

	return true;
}

void CGraphicDeviceDX11::__DestroyDepthStencil()
{
	SAFE_RELEASE(m_pDepthStencilView);
	SAFE_RELEASE(m_pDepthStencilBuffer);
}

bool CGraphicDeviceDX11::__CreateBootstrapPipeline()
{
	if (!m_pDevice)
		return false;

	static const char* c_szBootstrapVS =
		"struct VSIn { float3 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
		"struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
		"VSOut main(VSIn input)"
		"{"
		"    VSOut output;"
		"    output.pos = float4(input.pos, 1.0f);"
		"    output.col = input.col;"
		"    output.uv = input.uv;"
		"    return output;"
		"}";

	static const char* c_szBootstrapPS =
		"struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
		"float4 main(PSIn input) : SV_TARGET"
		"{"
		"    return input.col;"
		"}";

	static const char* c_szBootstrapTexturePS =
		"Texture2D tx0 : register(t0);"
		"SamplerState smp0 : register(s0);"
		"struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
		"float4 main(PSIn input) : SV_TARGET"
		"{"
		"    return tx0.Sample(smp0, input.uv) * input.col;"
		"}";

	ID3DBlob* pVSBlob = NULL;
	ID3DBlob* pPSBlob = NULL;
	ID3DBlob* pTexturePSBlob = NULL;
	ID3DBlob* pErrorBlob = NULL;
	auto __LogCompileFailure = [&](const char* c_szStage, HRESULT hCompileResult)
	{
		if (pErrorBlob && pErrorBlob->GetBufferPointer())
		{
			const char* c_szCompilerMessage = reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer());
			TraceError("DX11_SHADER_COMPILE_FAIL stage=%s hr=0x%08x msg=%s", c_szStage, static_cast<unsigned int>(hCompileResult), c_szCompilerMessage);
			return;
		}
		__LogDX11HResultFailure(c_szStage, hCompileResult);
	};

	HRESULT hResult = D3DCompile(
		c_szBootstrapVS, strlen(c_szBootstrapVS),
		NULL, NULL, NULL,
		"main", "vs_4_0",
		0, 0, &pVSBlob, &pErrorBlob);
	if (FAILED(hResult))
	{
		__LogCompileFailure("D3DCompile(BootstrapVS)", hResult);
		SAFE_RELEASE(pErrorBlob);
		return false;
	}
	SAFE_RELEASE(pErrorBlob);

	hResult = D3DCompile(
		c_szBootstrapPS, strlen(c_szBootstrapPS),
		NULL, NULL, NULL,
		"main", "ps_4_0",
		0, 0, &pPSBlob, &pErrorBlob);
	if (FAILED(hResult))
	{
		__LogCompileFailure("D3DCompile(BootstrapPS)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pErrorBlob);
		return false;
	}
	SAFE_RELEASE(pErrorBlob);

	hResult = D3DCompile(
		c_szBootstrapTexturePS, strlen(c_szBootstrapTexturePS),
		NULL, NULL, NULL,
		"main", "ps_4_0",
		0, 0, &pTexturePSBlob, &pErrorBlob);
	if (FAILED(hResult))
	{
		__LogCompileFailure("D3DCompile(BootstrapTexturePS)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pErrorBlob);
		return false;
	}
	SAFE_RELEASE(pErrorBlob);

	hResult = m_pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), NULL, &m_pBootstrapVertexShader);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateVertexShader(bootstrap)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapVertexShader, "DX11.BootstrapVertexShader");

	hResult = m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &m_pBootstrapPixelShader);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreatePixelShader(bootstrap)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapPixelShader, "DX11.BootstrapPixelShader");

	hResult = m_pDevice->CreatePixelShader(pTexturePSBlob->GetBufferPointer(), pTexturePSBlob->GetBufferSize(), NULL, &m_pBootstrapUIPixelShader);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreatePixelShader(bootstrap_ui)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapUIPixelShader, "DX11.BootstrapUIPixelShader");

	D3D11_INPUT_ELEMENT_DESC akLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                     D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, sizeof(float) * 3,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, sizeof(float) * 7,     D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	hResult = m_pDevice->CreateInputLayout(
		akLayout, sizeof(akLayout) / sizeof(akLayout[0]),
		pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(),
		&m_pBootstrapInputLayout);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateInputLayout(bootstrap)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapInputLayout, "DX11.BootstrapInputLayout");

	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	const SBootstrapVertex akSeedVertices[] =
	{
		// Migration bootstrap triangle (indices 0..2)
		{  0.0f,  0.45f, 0.0f, 1.0f, 0.20f, 0.20f, 1.0f, 0.5f, 0.0f },
		{  0.35f, -0.25f, 0.0f, 0.20f, 1.0f, 0.20f, 1.0f, 1.0f, 1.0f },
		{ -0.35f, -0.25f, 0.0f, 0.20f, 0.45f, 1.0f, 1.0f, 0.0f, 1.0f },

		// Native DX11 UI test overlay bar (indices 3..8)
		{ -0.92f,  0.90f, 0.0f, 0.14f, 0.60f, 1.00f, 1.0f, 0.0f, 0.0f },
		{  0.92f,  0.90f, 0.0f, 0.14f, 0.60f, 1.00f, 1.0f, 1.0f, 0.0f },
		{ -0.92f,  0.78f, 0.0f, 0.08f, 0.23f, 0.52f, 1.0f, 0.0f, 1.0f },
		{ -0.92f,  0.78f, 0.0f, 0.08f, 0.23f, 0.52f, 1.0f, 0.0f, 1.0f },
		{  0.92f,  0.90f, 0.0f, 0.14f, 0.60f, 1.00f, 1.0f, 1.0f, 0.0f },
		{  0.92f,  0.78f, 0.0f, 0.08f, 0.23f, 0.52f, 1.0f, 1.0f, 1.0f },

		// Native DX11 UI test cursor quad (indices 9..14)
		{ -0.05f,  0.05f, 0.0f, 1.00f, 0.92f, 0.20f, 1.0f, 0.0f, 0.0f },
		{  0.05f,  0.05f, 0.0f, 1.00f, 0.92f, 0.20f, 1.0f, 1.0f, 0.0f },
		{ -0.05f, -0.05f, 0.0f, 0.80f, 0.65f, 0.10f, 1.0f, 0.0f, 1.0f },
		{ -0.05f, -0.05f, 0.0f, 0.80f, 0.65f, 0.10f, 1.0f, 0.0f, 1.0f },
		{  0.05f,  0.05f, 0.0f, 1.00f, 0.92f, 0.20f, 1.0f, 1.0f, 0.0f },
		{  0.05f, -0.05f, 0.0f, 0.80f, 0.65f, 0.10f, 1.0f, 1.0f, 1.0f },
	};

	static const UINT kBootstrapVBVertexCapacity = 4096;
	std::vector<SBootstrapVertex> akVBSeed(kBootstrapVBVertexCapacity);
	const size_t stSeedCount = sizeof(akSeedVertices) / sizeof(akSeedVertices[0]);
	for (size_t i = 0; i < stSeedCount; ++i)
		akVBSeed[i] = akSeedVertices[i];

	D3D11_BUFFER_DESC kVBDesc;
	ZeroMemory(&kVBDesc, sizeof(kVBDesc));
	kVBDesc.Usage = D3D11_USAGE_DYNAMIC;
	kVBDesc.ByteWidth = sizeof(SBootstrapVertex) * kBootstrapVBVertexCapacity;
	kVBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	kVBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	D3D11_SUBRESOURCE_DATA kVBInitData;
	ZeroMemory(&kVBInitData, sizeof(kVBInitData));
	kVBInitData.pSysMem = &akVBSeed[0];

	hResult = m_pDevice->CreateBuffer(&kVBDesc, &kVBInitData, &m_pBootstrapVertexBuffer);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateBuffer(bootstrap_vb)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapVertexBuffer, "DX11.BootstrapVertexBuffer");

	D3D11_BLEND_DESC kBlendDesc;
	ZeroMemory(&kBlendDesc, sizeof(kBlendDesc));
	kBlendDesc.RenderTarget[0].BlendEnable = TRUE;
	kBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	kBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	kBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	kBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	kBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	kBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	kBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	hResult = m_pDevice->CreateBlendState(&kBlendDesc, &m_pBootstrapAlphaBlendState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateBlendState(alpha)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapAlphaBlendState, "DX11.BootstrapAlphaBlendState");

	D3D11_BLEND_DESC kCloudBlendDesc = kBlendDesc;
	kCloudBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	kCloudBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_COLOR;
	kCloudBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	kCloudBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;

	hResult = m_pDevice->CreateBlendState(&kCloudBlendDesc, &m_pBootstrapUICloudBlendState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateBlendState(ui_cloud)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapUICloudBlendState, "DX11.BootstrapUICloudBlendState");

	D3D11_BLEND_DESC kScreenBlendDesc = kBlendDesc;
	kScreenBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_DEST_COLOR;
	kScreenBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	kScreenBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA;
	kScreenBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	hResult = m_pDevice->CreateBlendState(&kScreenBlendDesc, &m_pBootstrapUIScreenBlendState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateBlendState(ui_screen)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapUIScreenBlendState, "DX11.BootstrapUIScreenBlendState");

	D3D11_BLEND_DESC kModulateBlendDesc = kBlendDesc;
	kModulateBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	kModulateBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
	kModulateBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	kModulateBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC_ALPHA;

	hResult = m_pDevice->CreateBlendState(&kModulateBlendDesc, &m_pBootstrapUIModulateBlendState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateBlendState(ui_modulate)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapUIModulateBlendState, "DX11.BootstrapUIModulateBlendState");

	// M2-LENSFLARE-NATIVE-58: Additive blend state for lensflare elements
	D3D11_BLEND_DESC kAdditiveBlendDesc = kBlendDesc;
	kAdditiveBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	kAdditiveBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	kAdditiveBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	kAdditiveBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	hResult = m_pDevice->CreateBlendState(&kAdditiveBlendDesc, &m_pBootstrapUIAdditiveBlendState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateBlendState(ui_additive)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapUIAdditiveBlendState, "DX11.BootstrapUIAdditiveBlendState");

	// Reuse the same additive blend state object for both UI and generic additive paths.
	// Creating two states with identical desc can return the same COM object, and assigning two
	// different debug-name sizes to that object triggers debug-layer warning #55 (SetPrivateData size mismatch).
	m_pBootstrapAdditiveBlendState = m_pBootstrapUIAdditiveBlendState;
	if (m_pBootstrapAdditiveBlendState)
		m_pBootstrapAdditiveBlendState->AddRef();

	// LCD Pass 1: dest.rgb *= (1 - coverage.rgb) - ZERO, INVSRCCOLOR blend
	D3D11_BLEND_DESC kLCDPass1BlendDesc;
	ZeroMemory(&kLCDPass1BlendDesc, sizeof(kLCDPass1BlendDesc));
	kLCDPass1BlendDesc.RenderTarget[0].BlendEnable = TRUE;
	kLCDPass1BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	kLCDPass1BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	kLCDPass1BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_COLOR;
	kLCDPass1BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	kLCDPass1BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	kLCDPass1BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	kLCDPass1BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;

	hResult = m_pDevice->CreateBlendState(&kLCDPass1BlendDesc, &m_pBootstrapLCDPass1BlendState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateBlendState(lcd_pass1)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapLCDPass1BlendState, "DX11.BootstrapLCDPass1BlendState");

	// LCD Pass 2: dest.rgb += textColor.rgb * coverage.rgb - ONE, ONE blend
	D3D11_BLEND_DESC kLCDPass2BlendDesc;
	ZeroMemory(&kLCDPass2BlendDesc, sizeof(kLCDPass2BlendDesc));
	kLCDPass2BlendDesc.RenderTarget[0].BlendEnable = TRUE;
	kLCDPass2BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	kLCDPass2BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	kLCDPass2BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	kLCDPass2BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	kLCDPass2BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	kLCDPass2BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	kLCDPass2BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;

	hResult = m_pDevice->CreateBlendState(&kLCDPass2BlendDesc, &m_pBootstrapLCDPass2BlendState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateBlendState(lcd_pass2)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapLCDPass2BlendState, "DX11.BootstrapLCDPass2BlendState");

	D3D11_DEPTH_STENCIL_DESC kDepthEnableDesc;
	ZeroMemory(&kDepthEnableDesc, sizeof(kDepthEnableDesc));
	kDepthEnableDesc.DepthEnable = TRUE;
	kDepthEnableDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	kDepthEnableDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

	hResult = m_pDevice->CreateDepthStencilState(&kDepthEnableDesc, &m_pBootstrapDepthEnableState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateDepthStencilState(depth_enable)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapDepthEnableState, "DX11.BootstrapDepthEnableState");

	D3D11_DEPTH_STENCIL_DESC kDepthReadDesc = kDepthEnableDesc;
	kDepthReadDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

	hResult = m_pDevice->CreateDepthStencilState(&kDepthReadDesc, &m_pBootstrapDepthReadState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateDepthStencilState(depth_read)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapDepthReadState, "DX11.BootstrapDepthReadState");

	D3D11_DEPTH_STENCIL_DESC kDepthDisableDesc = kDepthEnableDesc;
	kDepthDisableDesc.DepthEnable = FALSE;
	kDepthDisableDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

	hResult = m_pDevice->CreateDepthStencilState(&kDepthDisableDesc, &m_pBootstrapDepthDisableState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateDepthStencilState(depth_disable)", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapDepthDisableState, "DX11.BootstrapDepthDisableState");

	// Create rasterizer state for UI rendering (no culling)
	D3D11_RASTERIZER_DESC kRasterizerDesc = {};
	kRasterizerDesc.FillMode = D3D11_FILL_SOLID;
	kRasterizerDesc.CullMode = D3D11_CULL_NONE;  // Disable culling for UI sprites
	kRasterizerDesc.FrontCounterClockwise = FALSE;
	kRasterizerDesc.DepthBias = 0;
	kRasterizerDesc.DepthBiasClamp = 0.0f;
	kRasterizerDesc.SlopeScaledDepthBias = 0.0f;
	kRasterizerDesc.DepthClipEnable = TRUE;
	kRasterizerDesc.ScissorEnable = FALSE;
	kRasterizerDesc.MultisampleEnable = FALSE;
	kRasterizerDesc.AntialiasedLineEnable = FALSE;

	hResult = m_pDevice->CreateRasterizerState(&kRasterizerDesc, &m_pBootstrapRasterizerState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateRasterizerState", hResult);
		SAFE_RELEASE(pVSBlob);
		SAFE_RELEASE(pPSBlob);
		SAFE_RELEASE(pTexturePSBlob);
		__DestroyBootstrapPipeline();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapRasterizerState, "DX11.BootstrapRasterizerState");

	SAFE_RELEASE(pVSBlob);
	SAFE_RELEASE(pPSBlob);
	SAFE_RELEASE(pTexturePSBlob);
	m_isBootstrapPipelineReady = true;
	return true;
}

void CGraphicDeviceDX11::__DestroyBootstrapPipeline()
{
	m_isBootstrapPipelineReady = false;
	SAFE_RELEASE(m_pBootstrapRasterizerState);
	SAFE_RELEASE(m_pBootstrapDepthDisableState);
	SAFE_RELEASE(m_pBootstrapDepthReadState);
	SAFE_RELEASE(m_pBootstrapDepthEnableState);
	SAFE_RELEASE(m_pBootstrapUIModulateBlendState);
	SAFE_RELEASE(m_pBootstrapUIAdditiveBlendState);
	SAFE_RELEASE(m_pBootstrapUICloudBlendState);
	SAFE_RELEASE(m_pBootstrapUIScreenBlendState);
	SAFE_RELEASE(m_pBootstrapAdditiveBlendState);
	SAFE_RELEASE(m_pBootstrapLCDPass2BlendState);
	SAFE_RELEASE(m_pBootstrapLCDPass1BlendState);
	SAFE_RELEASE(m_pBootstrapAlphaBlendState);
	SAFE_RELEASE(m_pBootstrapVertexBuffer);
	SAFE_RELEASE(m_pBootstrapInputLayout);
	SAFE_RELEASE(m_pBootstrapUIPixelShader);
	SAFE_RELEASE(m_pBootstrapPixelShader);
	SAFE_RELEASE(m_pBootstrapVertexShader);
}

bool CGraphicDeviceDX11::__CreateBootstrapUITexture()
{
	if (!m_pDevice)
		return false;
	if (m_isBootstrapUITextureReady)
		return true;

	static const UINT kTexSize = 32;
	std::vector<DWORD> akPixels(kTexSize * kTexSize, 0);
	for (UINT y = 0; y < kTexSize; ++y)
	{
		for (UINT x = 0; x < kTexSize; ++x)
		{
			const bool isDark = (((x / 4) + (y / 4)) % 2) == 0;
			const BYTE r = isDark ? 32 : 52;
			const BYTE g = isDark ? 114 : 152;
			const BYTE b = isDark ? 220 : 255;
			const BYTE a = 255;
			akPixels[y * kTexSize + x] = (static_cast<DWORD>(a) << 24) | (static_cast<DWORD>(b) << 16) | (static_cast<DWORD>(g) << 8) | static_cast<DWORD>(r);
		}
	}

	D3D11_TEXTURE2D_DESC kTexDesc;
	ZeroMemory(&kTexDesc, sizeof(kTexDesc));
	kTexDesc.Width = kTexSize;
	kTexDesc.Height = kTexSize;
	kTexDesc.MipLevels = 1;
	kTexDesc.ArraySize = 1;
	kTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	kTexDesc.SampleDesc.Count = 1;
	kTexDesc.Usage = D3D11_USAGE_IMMUTABLE;
	kTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA kInitData;
	ZeroMemory(&kInitData, sizeof(kInitData));
	kInitData.pSysMem = &akPixels[0];
	kInitData.SysMemPitch = sizeof(DWORD) * kTexSize;

	HRESULT hResult = m_pDevice->CreateTexture2D(&kTexDesc, &kInitData, &m_pBootstrapUITexture);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateTexture2D(bootstrap_ui)", hResult);
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapUITexture, "DX11.BootstrapUITexture");

	hResult = m_pDevice->CreateShaderResourceView(m_pBootstrapUITexture, NULL, &m_pBootstrapUITextureSRV);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateShaderResourceView(bootstrap_ui)", hResult);
		__DestroyBootstrapUITexture();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapUITextureSRV, "DX11.BootstrapUITextureSRV");

	D3D11_SAMPLER_DESC kSamplerDesc;
	ZeroMemory(&kSamplerDesc, sizeof(kSamplerDesc));
	// M3-UI-COMPOSITE-PARITY-41A: Use POINT filtering to prevent atlas bleeding on stretched UI slices.
	// Linear filtering caused vertical bands in center-slice composites (taskbar, options panel).
	// Point filtering matches legacy DX9 behavior and eliminates sampler bleeding at atlas seams.
	kSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	kSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	kSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	kSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	kSamplerDesc.MinLOD = 0.0f;
	kSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	hResult = m_pDevice->CreateSamplerState(&kSamplerDesc, &m_pBootstrapUISamplerState);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateSamplerState(bootstrap_ui)", hResult);
		__DestroyBootstrapUITexture();
		return false;
	}
	DirectX::SetDebugObjectName(m_pBootstrapUISamplerState, "DX11.BootstrapUISamplerState");

	m_isBootstrapUITextureReady = true;
	return true;
}

void CGraphicDeviceDX11::__DestroyBootstrapUITexture()
{
	m_isBootstrapUITextureReady = false;
	SAFE_RELEASE(m_pBootstrapUISamplerState);
	SAFE_RELEASE(m_pBootstrapUITextureSRV);
	SAFE_RELEASE(m_pBootstrapUITexture);
}

bool CGraphicDeviceDX11::__EnsureVisibleBridgeTexture(UINT uWidth, UINT uHeight)
{
	if (!m_pDevice)
		return false;
	if (0 == uWidth || 0 == uHeight)
		return false;

	if (m_pVisibleBridgeTexture &&
		m_pVisibleBridgeTextureSRV &&
		m_uVisibleBridgeWidth == uWidth &&
		m_uVisibleBridgeHeight == uHeight)
	{
		return true;
	}

	__DestroyVisibleBridgeTexture();

	D3D11_TEXTURE2D_DESC kTexDesc;
	ZeroMemory(&kTexDesc, sizeof(kTexDesc));
	kTexDesc.Width = uWidth;
	kTexDesc.Height = uHeight;
	kTexDesc.MipLevels = 1;
	kTexDesc.ArraySize = 1;
	kTexDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	kTexDesc.SampleDesc.Count = 1;
	kTexDesc.Usage = D3D11_USAGE_DEFAULT;
	kTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	HRESULT hResult = m_pDevice->CreateTexture2D(&kTexDesc, NULL, &m_pVisibleBridgeTexture);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateTexture2D(visible_bridge)", hResult);
		__DestroyVisibleBridgeTexture();
		return false;
	}
	DirectX::SetDebugObjectName(m_pVisibleBridgeTexture, "DX11.VisibleBridgeTexture");

	hResult = m_pDevice->CreateShaderResourceView(m_pVisibleBridgeTexture, NULL, &m_pVisibleBridgeTextureSRV);
	if (FAILED(hResult))
	{
		__LogDX11HResultFailure("ID3D11Device::CreateShaderResourceView(visible_bridge)", hResult);
		__DestroyVisibleBridgeTexture();
		return false;
	}
	DirectX::SetDebugObjectName(m_pVisibleBridgeTextureSRV, "DX11.VisibleBridgeTextureSRV");

	m_uVisibleBridgeWidth = uWidth;
	m_uVisibleBridgeHeight = uHeight;
	return true;
}

void CGraphicDeviceDX11::__DestroyVisibleBridgeTexture()
{
	m_uVisibleBridgeWidth = 0;
	m_uVisibleBridgeHeight = 0;
	SAFE_RELEASE(m_pVisibleBridgeTextureSRV);
	SAFE_RELEASE(m_pVisibleBridgeTexture);
}
