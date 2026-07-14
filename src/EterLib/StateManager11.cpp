/******************************************************************************
  StateManager11.cpp - DX11 State Management Layer Implementation

  DX11 Model Sync: M2-STATECORE-CUT-58 Phase 2
  Purpose: DX11-compatible replacement for CStateManager (DX9)

  Implementation Notes:
  - Translates DX9 state management calls to DX11 state objects
  - Caches state objects to reduce creation overhead
  - Implements save/restore pattern for atomic state changes
  - Uses constant buffers for transforms, materials, and lighting

  Author: MODEL2 (2026-03-22)
******************************************************************************/

#include "StdAfx.h"
#include "StateManager11.h"
// Legacy render-state token types are provided by GrpBase.h.

namespace
{
	bool ShouldLogRasterOwnerConflictOnce()
	{
		static bool s_bLogged = false;
		if (s_bLogged)
			return false;

		s_bLogged = true;
		return true;
	}

	D3D11_BLEND_DESC MakeSafeDefaultBlendDesc()
	{
		D3D11_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = FALSE;
		desc.IndependentBlendEnable = FALSE;
		for (UINT i = 0; i < 8; ++i)
		{
			desc.RenderTarget[i].BlendEnable = FALSE;
			desc.RenderTarget[i].SrcBlend = D3D11_BLEND_ONE;
			desc.RenderTarget[i].DestBlend = D3D11_BLEND_ZERO;
			desc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
			desc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
			desc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
			desc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			desc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		return desc;
	}

	UINT PrimitiveCountToVertexCount(D3D11_PRIMITIVE_TOPOLOGY topology, UINT primitiveCount)
	{
		switch (topology)
		{
			case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
				return primitiveCount;
			case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
				return primitiveCount * 2u;
			case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
				return primitiveCount + 1u;
			case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
				return primitiveCount * 3u;
			case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
				return primitiveCount + 2u;
			default:
				return primitiveCount;
		}
	}

	UINT PrimitiveCountToIndexCount(D3D11_PRIMITIVE_TOPOLOGY topology, UINT primitiveCount)
	{
		return PrimitiveCountToVertexCount(topology, primitiveCount);
	}

	DXGI_FORMAT ResolveIndexFormat(int legacyOrDxgiFormat)
	{
		switch (legacyOrDxgiFormat)
		{
		case static_cast<int>(GRP_FMT_INDEX16):
			return DXGI_FORMAT_R16_UINT;
		case static_cast<int>(GRP_FMT_INDEX32):
			return DXGI_FORMAT_R32_UINT;
		default:
			return static_cast<DXGI_FORMAT>(legacyOrDxgiFormat);
		}
	}
}

// ============================================================================
// Construction / Destruction
// ============================================================================

CStateManager11::CStateManager11()
	: m_pDevice(nullptr)
	, m_pContext(nullptr)
	, m_pD3DDevice(nullptr)
	, m_ScissorRect{}
{
#ifdef _DEBUG
	m_iDrawCallCount = 0;
#endif

	// Initialize render state cache to defaults
	for (DWORD i = 0; i < STATEMANAGER11_MAX_RENDERSTATES; i++)
	{
		m_RenderStateCache[i] = 0;
	}
	m_RenderStateCache[GRP_RS_LIGHTING] = TRUE;
	m_RenderStateCache[GRP_RS_FOGVERTEXMODE] = GRP_FOG_NONE;
	m_RenderStateCache[GRP_RS_RANGEFOGENABLE] = FALSE;

	// Initialize lighting
	ZeroMemory(&m_LightingCBData, sizeof(m_LightingCBData));
	m_LightingCBData.Ambient = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_LightingCBData.LightCount = 0;
	m_afLegacyVSConstants.fill(0.0f);
	m_afLegacyPSConstants.fill(0.0f);
	m_kDebugDrawDiagnostics = SDebugDrawDiagnostics();
	SyncFogDiagnosticsFromRenderStateCache();
	for (DWORD i = 0; i < STATEMANAGER11_MAX_STAGES; ++i)
	{
		InitializeSamplerStateCache(i);
		for (DWORD t = 0; t < STATEMANAGER11_MAX_TEXTURESTATES; ++t)
		{
			m_TextureStageStateCache[i][t] = 0u;
			m_TextureStageStateValid[i][t] = false;
		}
	}
	m_dwCurrentFVF = 0u;
}

CStateManager11::~CStateManager11()
{
	Release();
}

void CStateManager11::Initialize(CGraphicDeviceDX11* pDevice)
{
	if (!pDevice || !pDevice->IsValid())
	{
		assert(!"StateManager11: Invalid device");
		return;
	}

	m_pDevice = pDevice;
	m_pContext = pDevice->GetContext();
	m_pD3DDevice = pDevice->GetDevice();

	if (!m_pContext || !m_pD3DDevice)
	{
		assert(!"StateManager11: Null device context");
		return;
	}

	// Create constant buffers
	HRESULT hr;

	// Transform constant buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(CBTransform);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDesc.MiscFlags = 0;
	cbDesc.StructureByteStride = 0;

	hr = m_pD3DDevice->CreateBuffer(&cbDesc, nullptr, &m_pTransformCB);
	if (FAILED(hr))
	{
		assert(!"StateManager11: Failed to create transform constant buffer");
		return;
	}

	// Material constant buffer
	cbDesc.ByteWidth = sizeof(CBMaterial);
	hr = m_pD3DDevice->CreateBuffer(&cbDesc, nullptr, &m_pMaterialCB);
	if (FAILED(hr))
	{
		assert(!"StateManager11: Failed to create material constant buffer");
		return;
	}

	// Lighting constant buffer
	cbDesc.ByteWidth = sizeof(CBLighting);
	hr = m_pD3DDevice->CreateBuffer(&cbDesc, nullptr, &m_pLightingCB);
	if (FAILED(hr))
	{
		assert(!"StateManager11: Failed to create lighting constant buffer");
		return;
	}

	// Legacy shader constants (DX9 Set*ShaderConstant compatibility buffer layout)
	D3D11_BUFFER_DESC legacyCBDesc = {};
	legacyCBDesc.ByteWidth = sizeof(float) * STATEMANAGER11_MAX_VCONSTANTS * 4;
	legacyCBDesc.Usage = D3D11_USAGE_DEFAULT;
	legacyCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	legacyCBDesc.CPUAccessFlags = 0;
	legacyCBDesc.MiscFlags = 0;
	legacyCBDesc.StructureByteStride = 0;

	hr = m_pD3DDevice->CreateBuffer(&legacyCBDesc, nullptr, &m_pLegacyVSConstantsCB);
	if (FAILED(hr))
	{
		assert(!"StateManager11: Failed to create legacy VS constants buffer");
		return;
	}

	legacyCBDesc.ByteWidth = sizeof(float) * STATEMANAGER11_MAX_PCONSTANTS * 4;
	hr = m_pD3DDevice->CreateBuffer(&legacyCBDesc, nullptr, &m_pLegacyPSConstantsCB);
	if (FAILED(hr))
	{
		assert(!"StateManager11: Failed to create legacy PS constants buffer");
		return;
	}

	// Initialize transform data to identity
	DirectX::XMStoreFloat4x4(&m_TransformCBData.matWorld, DirectX::XMMatrixIdentity());
	DirectX::XMStoreFloat4x4(&m_TransformCBData.matView, DirectX::XMMatrixIdentity());
	DirectX::XMStoreFloat4x4(&m_TransformCBData.matProj, DirectX::XMMatrixIdentity());
	for (int i = 0; i < 8; i++)
	{
		DirectX::XMStoreFloat4x4(&m_TransformCBData.matTexture[i], DirectX::XMMatrixIdentity());
	}

	// Initialize material data to default white
	m_MaterialCBData.Diffuse = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_MaterialCBData.Ambient = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_MaterialCBData.Specular = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	m_MaterialCBData.Emissive = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	m_MaterialCBData.Power = 0.0f;
	m_bLightingEnabled = true;
	m_bLightingBufferDirty = false;
	m_uLightingBatchDepth = 0u;
	m_bLegacyVSConstantsDirty = false;
	m_bLegacyPSConstantsDirty = false;
	m_dwLegacyVSDirtyStartRegister = 0u;
	m_dwLegacyVSDirtyEndRegister = 0u;
	m_dwLegacyPSDirtyStartRegister = 0u;
	m_dwLegacyPSDirtyEndRegister = 0u;
	m_kDebugDrawDiagnostics = SDebugDrawDiagnostics();
	SyncFogDiagnosticsFromRenderStateCache();

	m_pContext->UpdateSubresource(m_pLegacyVSConstantsCB.Get(), 0, nullptr, m_afLegacyVSConstants.data(), 0, 0);
	m_pContext->UpdateSubresource(m_pLegacyPSConstantsCB.Get(), 0, nullptr, m_afLegacyPSConstants.data(), 0, 0);

	// Bind constant buffers to pipeline
	m_pContext->VSSetConstantBuffers(0, 1, m_pTransformCB.GetAddressOf());
	m_pContext->PSSetConstantBuffers(0, 1, m_pMaterialCB.GetAddressOf());
	m_pContext->PSSetConstantBuffers(1, 1, m_pLightingCB.GetAddressOf());
	m_pContext->VSSetConstantBuffers(3, 1, m_pLegacyVSConstantsCB.GetAddressOf());
	m_pContext->PSSetConstantBuffers(3, 1, m_pLegacyPSConstantsCB.GetAddressOf());

	// Ensure deterministic startup: initialize lighting CB content before first draw.
	m_bLightingBufferDirty = true;
	UpdateLightingBuffer();
}

void CStateManager11::Release()
{
	m_pTransformCB.Reset();
	m_pMaterialCB.Reset();
	m_pLightingCB.Reset();
	m_pLegacyVSConstantsCB.Reset();
	m_pLegacyPSConstantsCB.Reset();
	m_bLightingBufferDirty = false;
	m_uLightingBatchDepth = 0u;
	m_bLegacyVSConstantsDirty = false;
	m_bLegacyPSConstantsDirty = false;
	m_dwLegacyVSDirtyStartRegister = 0u;
	m_dwLegacyVSDirtyEndRegister = 0u;
	m_dwLegacyPSDirtyStartRegister = 0u;
	m_dwLegacyPSDirtyEndRegister = 0u;
	m_kDebugDrawDiagnostics = SDebugDrawDiagnostics();

	m_pContext = nullptr;
	m_pD3DDevice = nullptr;
	m_pDevice = nullptr;
}

// ============================================================================
// Scene Management
// ============================================================================

bool CStateManager11::BeginScene()
{
	// DX11 doesn't need explicit BeginScene, provided for compatibility
	ResetFrameDiagnostics();
	return true;
}

void CStateManager11::EndScene()
{
	// DX11 doesn't need explicit EndScene, provided for compatibility
}

void CStateManager11::ResetFrameDiagnostics()
{
	m_kDebugDrawDiagnostics = SDebugDrawDiagnostics();
	SyncFogDiagnosticsFromRenderStateCache();
}

void CStateManager11::NoteNoRTVWithPSDraw(bool bIndexedDraw, D3D11_PRIMITIVE_TOPOLOGY eTopology, UINT uElementCount, bool bDepthBound)
{
	++m_kDebugDrawDiagnostics.uNoRTVWithPSCount;
	if (bIndexedDraw)
		++m_kDebugDrawDiagnostics.uNoRTVWithPSIndexedCount;
	else
		++m_kDebugDrawDiagnostics.uNoRTVWithPSNonIndexedCount;
	m_kDebugDrawDiagnostics.uNoRTVWithPSLastTopology = static_cast<uint32_t>(eTopology);
	m_kDebugDrawDiagnostics.uNoRTVWithPSLastElements = static_cast<uint32_t>(uElementCount);
	m_kDebugDrawDiagnostics.uNoRTVWithPSLastDepthBound = bDepthBound ? 1u : 0u;
}

void CStateManager11::NoteUnsupportedRenderState(GrpRenderStateType Type, DWORD Value)
{
	++m_kDebugDrawDiagnostics.uUnsupportedRenderStateCount;
	m_kDebugDrawDiagnostics.uUnsupportedRenderStateLastType = static_cast<uint32_t>(Type);
	m_kDebugDrawDiagnostics.uUnsupportedRenderStateLastValue = static_cast<uint32_t>(Value);

	static DWORD s_dwLastUnsupportedRSLogMS = 0u;
	const DWORD dwNow = GetTickCount();
	if (0u == s_dwLastUnsupportedRSLogMS || (dwNow - s_dwLastUnsupportedRSLogMS) >= 2000u)
	{
		s_dwLastUnsupportedRSLogMS = dwNow;
		TraceError(
			"DX11_STATE_RS_UNSUPPORTED type=%u value=%u frame_count=%u",
			static_cast<unsigned int>(Type),
			static_cast<unsigned int>(Value),
			static_cast<unsigned int>(m_kDebugDrawDiagnostics.uUnsupportedRenderStateCount));
	}
}

void CStateManager11::SyncFogDiagnosticsFromRenderStateCache()
{
	m_kDebugDrawDiagnostics.uFogEnable = static_cast<uint32_t>(m_RenderStateCache[GRP_RS_FOGENABLE]);
	m_kDebugDrawDiagnostics.uFogMode = static_cast<uint32_t>(m_RenderStateCache[GRP_RS_FOGVERTEXMODE]);
	m_kDebugDrawDiagnostics.uFogRangeEnable = static_cast<uint32_t>(m_RenderStateCache[GRP_RS_RANGEFOGENABLE]);
	m_kDebugDrawDiagnostics.uFogColor = static_cast<uint32_t>(m_RenderStateCache[GRP_RS_FOGCOLOR]);
	m_kDebugDrawDiagnostics.uFogDensity = static_cast<uint32_t>(m_RenderStateCache[GRP_RS_FOGDENSITY]);
	m_kDebugDrawDiagnostics.uFogStart = static_cast<uint32_t>(m_RenderStateCache[GRP_RS_FOGSTART]);
	m_kDebugDrawDiagnostics.uFogEnd = static_cast<uint32_t>(m_RenderStateCache[GRP_RS_FOGEND]);
}

void CStateManager11::SetDefaultState()
{
	// Keep a deterministic DX11-native baseline close to legacy behavior.
	SetLightingEnabled(true);
	SetBlendMode(EBlendMode::AlphaBlend);
	SetDepthMode(EDepthMode::ReadWrite);
	SetCullMode(ECullMode::CounterClockwise);
	ApplyState();
}

// ============================================================================
// Material
// ============================================================================

void CStateManager11::SetMaterial(const GrpMaterial* pMaterial)
{
	if (!pMaterial)
		return;

	m_MaterialCBData.Diffuse = DirectX::XMFLOAT4(
		pMaterial->Diffuse.r, pMaterial->Diffuse.g,
		pMaterial->Diffuse.b, pMaterial->Diffuse.a
	);
	m_MaterialCBData.Ambient = DirectX::XMFLOAT4(
		pMaterial->Ambient.r, pMaterial->Ambient.g,
		pMaterial->Ambient.b, pMaterial->Ambient.a
	);
	m_MaterialCBData.Specular = DirectX::XMFLOAT4(
		pMaterial->Specular.r, pMaterial->Specular.g,
		pMaterial->Specular.b, pMaterial->Specular.a
	);
	m_MaterialCBData.Emissive = DirectX::XMFLOAT4(
		pMaterial->Emissive.r, pMaterial->Emissive.g,
		pMaterial->Emissive.b, pMaterial->Emissive.a
	);
	m_MaterialCBData.Power = pMaterial->Power;

	UpdateMaterialBuffer();
}

void CStateManager11::GetMaterial(GrpMaterial* pMaterial)
{
	if (!pMaterial)
		return;

	pMaterial->Diffuse.r = m_MaterialCBData.Diffuse.x;
	pMaterial->Diffuse.g = m_MaterialCBData.Diffuse.y;
	pMaterial->Diffuse.b = m_MaterialCBData.Diffuse.z;
	pMaterial->Diffuse.a = m_MaterialCBData.Diffuse.w;

	pMaterial->Ambient.r = m_MaterialCBData.Ambient.x;
	pMaterial->Ambient.g = m_MaterialCBData.Ambient.y;
	pMaterial->Ambient.b = m_MaterialCBData.Ambient.z;
	pMaterial->Ambient.a = m_MaterialCBData.Ambient.w;

	pMaterial->Specular.r = m_MaterialCBData.Specular.x;
	pMaterial->Specular.g = m_MaterialCBData.Specular.y;
	pMaterial->Specular.b = m_MaterialCBData.Specular.z;
	pMaterial->Specular.a = m_MaterialCBData.Specular.w;

	pMaterial->Emissive.r = m_MaterialCBData.Emissive.x;
	pMaterial->Emissive.g = m_MaterialCBData.Emissive.y;
	pMaterial->Emissive.b = m_MaterialCBData.Emissive.z;
	pMaterial->Emissive.a = m_MaterialCBData.Emissive.w;

	pMaterial->Power = m_MaterialCBData.Power;
}

void CStateManager11::SaveMaterial()
{
	GrpMaterial mat;
	GetMaterial(&mat);
	m_MaterialStack.push(mat);
}

void CStateManager11::SaveMaterial(const GrpMaterial* pMaterial)
{
	SaveMaterial();
	if (pMaterial)
		SetMaterial(pMaterial);
}

void CStateManager11::RestoreMaterial()
{
	if (m_MaterialStack.empty())
		return;

	GrpMaterial mat = m_MaterialStack.top();
	m_MaterialStack.pop();
	SetMaterial(&mat);
}

// ============================================================================
// Lights
// ============================================================================

void CStateManager11::SetLight(DWORD index, const SLightDesc* pLight)
{
	if (!pLight || index >= MAX_LIGHTS)
		return;

	CBLight& light = m_LightingCBData.Lights[index];

	light.Position = DirectX::XMFLOAT4(
		pLight->Position.x, pLight->Position.y,
		pLight->Position.z, 1.0f
	);

	light.Direction = DirectX::XMFLOAT4(
		pLight->Direction.x, pLight->Direction.y,
		pLight->Direction.z, 0.0f
	);

	light.Diffuse = DirectX::XMFLOAT4(
		pLight->Diffuse.r, pLight->Diffuse.g,
		pLight->Diffuse.b, pLight->Diffuse.a
	);

	light.Specular = DirectX::XMFLOAT4(
		pLight->Specular.r, pLight->Specular.g,
		pLight->Specular.b, pLight->Specular.a
	);

	light.Ambient = DirectX::XMFLOAT4(
		pLight->Ambient.r, pLight->Ambient.g,
		pLight->Ambient.b, pLight->Ambient.a
	);

	light.Type = static_cast<float>(pLight->Type);
	light.Range = pLight->Range;
	light.Falloff = pLight->Falloff;
	light.Attenuation0 = pLight->Attenuation0;
	light.Attenuation1 = pLight->Attenuation1;
	light.Attenuation2 = pLight->Attenuation2;
	light.Theta = pLight->Theta;
	light.Phi = pLight->Phi;
	light.Enabled = 1;

	UpdateLightingActiveCount();
	QueueLightingBufferUpdate();
}

void CStateManager11::SetLightEnable(DWORD index, BOOL bEnable)
{
	if (index >= MAX_LIGHTS)
		return;

	m_LightingCBData.Lights[index].Enabled = bEnable ? 1 : 0;
	UpdateLightingActiveCount();
	QueueLightingBufferUpdate();
}

void CStateManager11::GetLight(DWORD index, SLightDesc* pLight)
{
	if (!pLight || index >= MAX_LIGHTS)
		return;

	const CBLight& light = m_LightingCBData.Lights[index];

	pLight->Position.x = light.Position.x;
	pLight->Position.y = light.Position.y;
	pLight->Position.z = light.Position.z;

	pLight->Direction.x = light.Direction.x;
	pLight->Direction.y = light.Direction.y;
	pLight->Direction.z = light.Direction.z;

	pLight->Diffuse.r = light.Diffuse.x;
	pLight->Diffuse.g = light.Diffuse.y;
	pLight->Diffuse.b = light.Diffuse.z;
	pLight->Diffuse.a = light.Diffuse.w;

	pLight->Specular.r = light.Specular.x;
	pLight->Specular.g = light.Specular.y;
	pLight->Specular.b = light.Specular.z;
	pLight->Specular.a = light.Specular.w;

	pLight->Ambient.r = light.Ambient.x;
	pLight->Ambient.g = light.Ambient.y;
	pLight->Ambient.b = light.Ambient.z;
	pLight->Ambient.a = light.Ambient.w;

	pLight->Type = static_cast<ELightDescType>(static_cast<int>(light.Type));
	pLight->Range = light.Range;
	pLight->Falloff = light.Falloff;
	pLight->Attenuation0 = light.Attenuation0;
	pLight->Attenuation1 = light.Attenuation1;
	pLight->Attenuation2 = light.Attenuation2;
	pLight->Theta = light.Theta;
	pLight->Phi = light.Phi;
}

// ============================================================================
// Scissor Rect
// ============================================================================

void CStateManager11::SetScissorRect(const RECT& c_rRect)
{
	m_ScissorRect.left = c_rRect.left;
	m_ScissorRect.top = c_rRect.top;
	m_ScissorRect.right = c_rRect.right;
	m_ScissorRect.bottom = c_rRect.bottom;

	m_pContext->RSSetScissorRects(1, &m_ScissorRect);

	// Enable scissor test in rasterizer state
	m_CurrentState.RasterizerDesc.ScissorEnable = TRUE;
	m_CurrentState.bRasterizerDirty = true;
}

void CStateManager11::GetScissorRect(RECT* pRect)
{
	if (!pRect)
		return;

	pRect->left = m_ScissorRect.left;
	pRect->top = m_ScissorRect.top;
	pRect->right = m_ScissorRect.right;
	pRect->bottom = m_ScissorRect.bottom;
}

// ============================================================================
// M3-ETERLIB-STATECORE-69: Semantic DX11 State API Implementation
// ============================================================================

void CStateManager11::SetBlendMode(EBlendMode mode)
{
	switch (mode)
	{
	case EBlendMode::Opaque:
		m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable = FALSE;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;

	case EBlendMode::AlphaBlend:
		m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable = TRUE;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;

	case EBlendMode::Additive:
		m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable = TRUE;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;

	case EBlendMode::Screen:
		m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable = TRUE;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_COLOR;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;

	case EBlendMode::Modulate:
		m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable = TRUE;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;

	case EBlendMode::ColorDodge:
		m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable = TRUE;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		m_CurrentState.BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		m_CurrentState.BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		m_CurrentState.BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		break;

	case EBlendMode::Custom:
		// Custom blend factors must be set via SetCustomBlendFactors()
		m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable = TRUE;
		break;
	}

	m_CurrentState.bBlendDirty = true;
}

void CStateManager11::SetCustomBlendFactors(DWORD dwSrcBlend, DWORD dwDestBlend, DWORD dwBlendOp)
{
	m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable = TRUE;
	m_CurrentState.BlendDesc.RenderTarget[0].SrcBlend = BlendToD3D11Blend(static_cast<GrpBlendType>(dwSrcBlend));
	m_CurrentState.BlendDesc.RenderTarget[0].DestBlend = BlendToD3D11Blend(static_cast<GrpBlendType>(dwDestBlend));
	m_CurrentState.BlendDesc.RenderTarget[0].BlendOp = BlendOpToD3D11BlendOp(static_cast<GrpBlendOpType>(dwBlendOp));
	m_CurrentState.bBlendDirty = true;
}

void CStateManager11::SetDepthMode(EDepthMode mode)
{
	switch (mode)
	{
	case EDepthMode::Disabled:
		m_CurrentState.DepthStencilDesc.DepthEnable = FALSE;
		m_CurrentState.DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		break;

	case EDepthMode::ReadOnly:
		m_CurrentState.DepthStencilDesc.DepthEnable = TRUE;
		m_CurrentState.DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		m_CurrentState.DepthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
		break;

	case EDepthMode::ReadWrite:
		m_CurrentState.DepthStencilDesc.DepthEnable = TRUE;
		m_CurrentState.DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		m_CurrentState.DepthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
		break;
	}

	m_CurrentState.bDepthStencilDirty = true;
}

void CStateManager11::SetDepthComparisonFunc(GrpCmpFuncType func)
{
	m_CurrentState.DepthStencilDesc.DepthFunc = CompareToD3D11Comparison(func);
	m_CurrentState.bDepthStencilDirty = true;
}

void CStateManager11::SetColorWriteEnable(bool bRed, bool bGreen, bool bBlue, bool bAlpha)
{
	UINT8 mask = 0;
	if (bRed) mask |= D3D11_COLOR_WRITE_ENABLE_RED;
	if (bGreen) mask |= D3D11_COLOR_WRITE_ENABLE_GREEN;
	if (bBlue) mask |= D3D11_COLOR_WRITE_ENABLE_BLUE;
	if (bAlpha) mask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;

	m_CurrentState.BlendDesc.RenderTarget[0].RenderTargetWriteMask = mask;
	m_CurrentState.bBlendDirty = true;
}

void CStateManager11::SetColorWriteEnableAll()
{
	m_CurrentState.BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	m_CurrentState.bBlendDirty = true;
}

void CStateManager11::SetLightingEnabled(bool bEnable)
{
	m_bLightingEnabled = bEnable;
	m_RenderStateCache[GRP_RS_LIGHTING] = bEnable ? TRUE : FALSE;
	UpdateLightingActiveCount();
	QueueLightingBufferUpdate();
}

void CStateManager11::SetFillMode(EFillMode mode)
{
	m_CurrentState.RasterizerDesc.FillMode = (mode == EFillMode::Wireframe) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
	m_CurrentState.bRasterizerDirty = true;
}

void CStateManager11::SetCullMode(ECullMode mode)
{
	switch (mode)
	{
	case ECullMode::None:
		m_CurrentState.RasterizerDesc.CullMode = D3D11_CULL_NONE;
		break;
	case ECullMode::Clockwise:
		m_CurrentState.RasterizerDesc.CullMode = D3D11_CULL_FRONT;
		break;
	case ECullMode::CounterClockwise:
		m_CurrentState.RasterizerDesc.CullMode = D3D11_CULL_BACK;
		break;
	}

	m_CurrentState.bRasterizerDirty = true;
}

// ============================================================================
// Render States
// ============================================================================

void CStateManager11::SetRenderState(GrpRenderStateType Type, DWORD Value)
{
	if (Type >= STATEMANAGER11_MAX_RENDERSTATES)
		return;

	if (Type == GRP_RS_CULLMODE && m_pContext && !m_CurrentState.bRasterizerDirty)
	{
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> pCurrentRasterState;
		m_pContext->RSGetState(pCurrentRasterState.GetAddressOf());

		ID3D11RasterizerState* const pCachedRasterState = m_CurrentState.pRasterizerState.Get();
		if (pCurrentRasterState.Get() != pCachedRasterState && ShouldLogRasterOwnerConflictOnce())
		{
			TraceError("DX11_RS_OWNER_CONFLICT pass=statemanager_cull_cache current_rs=%p cached_rs=%p requested_cull=%u",
				pCurrentRasterState.Get(),
				pCachedRasterState,
				static_cast<unsigned int>(Value));
		}
	}

	m_RenderStateCache[Type] = Value;

	switch (Type)
	{
		// Blend states
		case GRP_RS_ALPHABLENDENABLE:
			m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable = Value ? TRUE : FALSE;
			m_CurrentState.bBlendDirty = true;
			break;

		case GRP_RS_SRCBLEND:
			m_CurrentState.BlendDesc.RenderTarget[0].SrcBlend = BlendToD3D11Blend(static_cast<GrpBlendType>(Value));
			m_CurrentState.bBlendDirty = true;
			break;

		case GRP_RS_DESTBLEND:
			m_CurrentState.BlendDesc.RenderTarget[0].DestBlend = BlendToD3D11Blend(static_cast<GrpBlendType>(Value));
			m_CurrentState.bBlendDirty = true;
			break;

		case GRP_RS_BLENDOP:
			m_CurrentState.BlendDesc.RenderTarget[0].BlendOp = BlendOpToD3D11BlendOp(static_cast<GrpBlendOpType>(Value));
			m_CurrentState.bBlendDirty = true;
			break;

		// Depth-stencil states
		case GRP_RS_ZENABLE:
			m_CurrentState.DepthStencilDesc.DepthEnable = Value ? TRUE : FALSE;
			m_CurrentState.bDepthStencilDirty = true;
			break;

		case GRP_RS_ZWRITEENABLE:
			m_CurrentState.DepthStencilDesc.DepthWriteMask =
				Value ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
			m_CurrentState.bDepthStencilDirty = true;
			break;

		case GRP_RS_ZFUNC:
			m_CurrentState.DepthStencilDesc.DepthFunc = CompareToD3D11Comparison(static_cast<GrpCmpFuncType>(Value));
			m_CurrentState.bDepthStencilDirty = true;
			break;

		// Rasterizer states
		case GRP_RS_CULLMODE:
			m_CurrentState.RasterizerDesc.CullMode = CullToD3D11Cull(static_cast<GrpCullType>(Value));
			m_CurrentState.bRasterizerDirty = true;
			break;

		case GRP_RS_FILLMODE:
			m_CurrentState.RasterizerDesc.FillMode = FillToD3D11Fill(static_cast<GrpFillModeType>(Value));
			m_CurrentState.bRasterizerDirty = true;
			break;

		case GRP_RS_SCISSORTESTENABLE:
			m_CurrentState.RasterizerDesc.ScissorEnable = Value ? TRUE : FALSE;
			m_CurrentState.bRasterizerDirty = true;
			break;

		case GRP_RS_COLORWRITEENABLE:
			// M3-ETERLIB-TEXT-66: Color write mask for LCD subpixel text rendering
			m_CurrentState.BlendDesc.RenderTarget[0].RenderTargetWriteMask = (UINT8)Value;
			m_CurrentState.bBlendDirty = true;
			break;

		case GRP_RS_LIGHTING:
			m_bLightingEnabled = (Value != FALSE);
			UpdateLightingActiveCount();
			QueueLightingBufferUpdate();
			break;

		// Shader-owned/non-fixed-function states. Values are cached and consumed by higher-level shader systems.
		case GRP_RS_FOGENABLE:
		case GRP_RS_FOGCOLOR:
		case GRP_RS_FOGDENSITY:
		case GRP_RS_FOGSTART:
		case GRP_RS_FOGEND:
		case GRP_RS_RANGEFOGENABLE:
		case GRP_RS_TEXTUREFACTOR:
			break;

		case GRP_RS_FOGVERTEXMODE:
		{
			const bool bValidMode =
				(Value == GRP_FOG_NONE) ||
				(Value == GRP_FOG_LINEAR) ||
				(Value == GRP_FOG_EXP) ||
				(Value == GRP_FOG_EXP2);
			if (!bValidMode)
			{
				m_RenderStateCache[Type] = GRP_FOG_LINEAR;
				static DWORD s_dwLastFogModeClampLogMS = 0u;
				const DWORD dwNow = GetTickCount();
				if (0u == s_dwLastFogModeClampLogMS || (dwNow - s_dwLastFogModeClampLogMS) >= 2000u)
				{
					s_dwLastFogModeClampLogMS = dwNow;
					TraceError(
						"DX11_STATE_RS_FOGMODE_CLAMP requested=%u fallback=%u",
						static_cast<unsigned int>(Value),
						static_cast<unsigned int>(GRP_FOG_LINEAR));
				}
			}
			break;
		}

		// M3-ETERLIB-TEXT-66: legacy render-state support status
		// ================================================
		// Implemented (mapped to DX11 state objects):
		//   GRP_RS_ALPHABLENDENABLE, GRP_RS_SRCBLEND, GRP_RS_DESTBLEND, GRP_RS_BLENDOP
		//   GRP_RS_ZENABLE, GRP_RS_ZWRITEENABLE, GRP_RS_ZFUNC
		//   GRP_RS_CULLMODE, GRP_RS_FILLMODE, GRP_RS_SCISSORTESTENABLE
		//   GRP_RS_COLORWRITEENABLE, GRP_RS_LIGHTING
		//
		// Shader-owned (cached here, consumed by shader systems):
		//   GRP_RS_FOGENABLE, GRP_RS_FOGCOLOR, GRP_RS_FOGDENSITY,
		//   GRP_RS_FOGSTART, GRP_RS_FOGEND, GRP_RS_FOGVERTEXMODE,
		//   GRP_RS_RANGEFOGENABLE, GRP_RS_TEXTUREFACTOR
		//
		// Not supported (legacy fixed-function):
		//   GRP_RS_AMBIENT, GRP_RS_SPECULARENABLE, GRP_RS_SHADEMODE

		default:
			NoteUnsupportedRenderState(Type, Value);
			break;
	}

	if (Type == GRP_RS_FOGENABLE ||
		Type == GRP_RS_FOGVERTEXMODE ||
		Type == GRP_RS_RANGEFOGENABLE ||
		Type == GRP_RS_FOGCOLOR ||
		Type == GRP_RS_FOGDENSITY ||
		Type == GRP_RS_FOGSTART ||
		Type == GRP_RS_FOGEND)
	{
		SyncFogDiagnosticsFromRenderStateCache();
	}
}

void CStateManager11::GetRenderState(GrpRenderStateType Type, DWORD* pdwValue)
{
	if (!pdwValue || Type >= STATEMANAGER11_MAX_RENDERSTATES)
		return;

	*pdwValue = m_RenderStateCache[Type];
}

DWORD CStateManager11::GetRenderState(GrpRenderStateType Type)
{
	if (Type >= STATEMANAGER11_MAX_RENDERSTATES)
		return 0;

	return m_RenderStateCache[Type];
}

void CStateManager11::SaveRenderState(GrpRenderStateType Type, DWORD dwValue)
{
	if (Type >= STATEMANAGER11_MAX_RENDERSTATES)
	{
		static DWORD s_dwLastSaveInvalidTypeLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastSaveInvalidTypeLogMS || (dwNow - s_dwLastSaveInvalidTypeLogMS) >= 3000u)
		{
			s_dwLastSaveInvalidTypeLogMS = dwNow;
			TraceError("DX11_STATE_MANAGER_SAVE_RS_SKIP reason=type_out_of_range type=%u max=%u",
				static_cast<unsigned int>(Type),
				static_cast<unsigned int>(STATEMANAGER11_MAX_RENDERSTATES));
		}
		return;
	}

	m_RenderStateStack[Type].push(GetRenderState(Type));
	SetRenderState(Type, dwValue);
}

void CStateManager11::RestoreRenderState(GrpRenderStateType Type)
{
	if (Type >= STATEMANAGER11_MAX_RENDERSTATES)
	{
		static DWORD s_dwLastRestoreInvalidTypeLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastRestoreInvalidTypeLogMS || (dwNow - s_dwLastRestoreInvalidTypeLogMS) >= 3000u)
		{
			s_dwLastRestoreInvalidTypeLogMS = dwNow;
			TraceError("DX11_STATE_MANAGER_RESTORE_RS_SKIP reason=type_out_of_range type=%u max=%u",
				static_cast<unsigned int>(Type),
				static_cast<unsigned int>(STATEMANAGER11_MAX_RENDERSTATES));
		}
		return;
	}

	if (m_RenderStateStack[Type].empty())
		return;

	DWORD dwValue = m_RenderStateStack[Type].top();
	m_RenderStateStack[Type].pop();
	SetRenderState(Type, dwValue);
}

void CStateManager11::SaveLightingEnabled(BOOL bEnable)
{
	SaveRenderState(GRP_RS_LIGHTING, bEnable ? TRUE : FALSE);
}

void CStateManager11::RestoreLightingEnabled()
{
	RestoreRenderState(GRP_RS_LIGHTING);
}

void CStateManager11::SaveFogEnabled(BOOL bEnable)
{
	SaveRenderState(GRP_RS_FOGENABLE, bEnable ? TRUE : FALSE);
}

void CStateManager11::RestoreFogEnabled()
{
	RestoreRenderState(GRP_RS_FOGENABLE);
}

void CStateManager11::SetFogColorValue(DWORD dwFogColor)
{
	SetRenderState(GRP_RS_FOGCOLOR, dwFogColor);
}

void CStateManager11::SetFogExpDensity(float fDensity)
{
	DWORD dwDensity = 0u;
	static_assert(sizeof(dwDensity) == sizeof(fDensity), "Fog density packing mismatch");
	memcpy(&dwDensity, &fDensity, sizeof(dwDensity));
	SetRenderState(GRP_RS_FOGDENSITY, dwDensity);
}

void CStateManager11::SetFogLinearRange(float fFogNear, float fFogFar)
{
	DWORD dwFogNear = 0u;
	DWORD dwFogFar = 0u;
	static_assert(sizeof(dwFogNear) == sizeof(fFogNear), "Fog near packing mismatch");
	static_assert(sizeof(dwFogFar) == sizeof(fFogFar), "Fog far packing mismatch");
	memcpy(&dwFogNear, &fFogNear, sizeof(dwFogNear));
	memcpy(&dwFogFar, &fFogFar, sizeof(dwFogFar));
	SetRenderState(GRP_RS_FOGSTART, dwFogNear);
	SetRenderState(GRP_RS_FOGEND, dwFogFar);
}

void CStateManager11::SetFogModeLinear()
{
	SetRenderState(GRP_RS_FOGVERTEXMODE, GRP_FOG_LINEAR);
}

void CStateManager11::SetFogModeExp()
{
	SetRenderState(GRP_RS_FOGVERTEXMODE, GRP_FOG_EXP);
}

void CStateManager11::SetFogRangeEnabled(BOOL bEnable)
{
	SetRenderState(GRP_RS_RANGEFOGENABLE, bEnable ? TRUE : FALSE);
}

// ============================================================================
// Textures
// ============================================================================

void CStateManager11::SetTexture(DWORD dwStage, ID3D11ShaderResourceView* pSRV)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES)
		return;

	m_CurrentState.pTextures[dwStage] = pSRV;
	m_CurrentState.bTexturesDirty[dwStage] = true;
}

void CStateManager11::GetTexture(DWORD dwStage, ID3D11ShaderResourceView** ppSRV)
{
	if (!ppSRV || dwStage >= STATEMANAGER11_MAX_STAGES)
		return;

	*ppSRV = m_CurrentState.pTextures[dwStage].Get();
	if (*ppSRV)
		(*ppSRV)->AddRef();
}

void CStateManager11::SaveTexture(DWORD dwStage, ID3D11ShaderResourceView* pSRV)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES)
		return;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pCurrent;
	GetTexture(dwStage, &pCurrent);
	m_TextureStack[dwStage].push(pCurrent);

	SetTexture(dwStage, pSRV);
}

void CStateManager11::RestoreTexture(DWORD dwStage)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES || m_TextureStack[dwStage].empty())
		return;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pSRV = m_TextureStack[dwStage].top();
	m_TextureStack[dwStage].pop();

	SetTexture(dwStage, pSRV.Get());
}

// ============================================================================
// Sampler States
// ============================================================================

void CStateManager11::SetSamplerState(DWORD dwStage, GrpSamplerStateType Type, DWORD dwValue)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES || Type >= STATEMANAGER11_MAX_TEXTURESTATES || !m_pD3DDevice)
		return;

	D3D11_SAMPLER_DESC desc = m_SamplerDescCache[dwStage];

	auto readSamplerValue = [&](GrpSamplerStateType queryType) -> DWORD
	{
		if (queryType >= STATEMANAGER11_MAX_TEXTURESTATES)
			return GetDefaultSamplerStateValue(queryType);
		if (m_SamplerStateValueValid[dwStage][queryType])
			return m_SamplerStateValueCache[dwStage][queryType];
		return GetDefaultSamplerStateValue(queryType);
	};

	switch (Type)
	{
		case GRP_SAMP_MINFILTER:
		case GRP_SAMP_MAGFILTER:
		case GRP_SAMP_MIPFILTER:
		{
			m_SamplerStateValueCache[dwStage][Type] = dwValue;
			m_SamplerStateValueValid[dwStage][Type] = true;

			const DWORD dwMin = readSamplerValue(GRP_SAMP_MINFILTER);
			const DWORD dwMag = readSamplerValue(GRP_SAMP_MAGFILTER);
			const DWORD dwMip = readSamplerValue(GRP_SAMP_MIPFILTER);

			const bool bAnisotropic =
				(dwMin == GRP_TEXF_ANISOTROPIC) ||
				(dwMag == GRP_TEXF_ANISOTROPIC) ||
				(dwMip == GRP_TEXF_ANISOTROPIC);

			if (bAnisotropic)
			{
				desc.Filter = D3D11_FILTER_ANISOTROPIC;
			}
			else
			{
				const bool bMinLinear = (dwMin == GRP_TEXF_LINEAR);
				const bool bMagLinear = (dwMag == GRP_TEXF_LINEAR);
				const bool bMipLinear = (dwMip == GRP_TEXF_LINEAR);

				if (bMinLinear && bMagLinear && bMipLinear)
					desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				else if (!bMinLinear && !bMagLinear && !bMipLinear)
					desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
				else if (bMinLinear && bMagLinear && !bMipLinear)
					desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
				else if (bMinLinear && !bMagLinear && bMipLinear)
					desc.Filter = D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
				else if (!bMinLinear && bMagLinear && bMipLinear)
					desc.Filter = D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
				else if (bMinLinear && !bMagLinear && !bMipLinear)
					desc.Filter = D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
				else if (!bMinLinear && bMagLinear && !bMipLinear)
					desc.Filter = D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
				else
					desc.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
			}
			break;
		}

		case GRP_SAMP_ADDRESSU:
			m_SamplerStateValueCache[dwStage][Type] = dwValue;
			m_SamplerStateValueValid[dwStage][Type] = true;
			desc.AddressU = AddressToD3D11Address(static_cast<GrpTextureAddressType>(dwValue));
			break;

		case GRP_SAMP_ADDRESSV:
			m_SamplerStateValueCache[dwStage][Type] = dwValue;
			m_SamplerStateValueValid[dwStage][Type] = true;
			desc.AddressV = AddressToD3D11Address(static_cast<GrpTextureAddressType>(dwValue));
			break;

		case GRP_SAMP_ADDRESSW:
			m_SamplerStateValueCache[dwStage][Type] = dwValue;
			m_SamplerStateValueValid[dwStage][Type] = true;
			desc.AddressW = AddressToD3D11Address(static_cast<GrpTextureAddressType>(dwValue));
			break;

		case GRP_SAMP_MAXANISOTROPY:
			m_SamplerStateValueCache[dwStage][Type] = dwValue;
			m_SamplerStateValueValid[dwStage][Type] = true;
			desc.MaxAnisotropy = (dwValue > 0u) ? dwValue : 1u;
			break;

		case GRP_SAMP_BORDERCOLOR:
			m_SamplerStateValueCache[dwStage][Type] = dwValue;
			m_SamplerStateValueValid[dwStage][Type] = true;
			desc.BorderColor[0] = static_cast<float>((dwValue >> 16) & 0xff) / 255.0f;
			desc.BorderColor[1] = static_cast<float>((dwValue >> 8) & 0xff) / 255.0f;
			desc.BorderColor[2] = static_cast<float>(dwValue & 0xff) / 255.0f;
			desc.BorderColor[3] = static_cast<float>((dwValue >> 24) & 0xff) / 255.0f;
			break;

		default:
			m_SamplerStateValueCache[dwStage][Type] = dwValue;
			m_SamplerStateValueValid[dwStage][Type] = true;
			break;
	}

	Microsoft::WRL::ComPtr<ID3D11SamplerState> pSampler;
	HRESULT hr = m_pD3DDevice->CreateSamplerState(&desc, &pSampler);
	if (SUCCEEDED(hr))
	{
		m_SamplerDescCache[dwStage] = desc;
		m_CurrentState.pSamplers[dwStage] = pSampler;
		m_CurrentState.bSamplersDirty[dwStage] = true;
	}
}

void CStateManager11::GetSamplerState(DWORD dwStage, GrpSamplerStateType Type, DWORD* pdwValue)
{
	if (!pdwValue)
		return;

	if (dwStage >= STATEMANAGER11_MAX_STAGES || Type >= STATEMANAGER11_MAX_TEXTURESTATES)
	{
		*pdwValue = 0;
		return;
	}

	if (m_SamplerStateValueValid[dwStage][Type])
	{
		*pdwValue = m_SamplerStateValueCache[dwStage][Type];
		return;
	}

	*pdwValue = GetDefaultSamplerStateValue(Type);
}

void CStateManager11::SaveSamplerState(DWORD dwStage, GrpSamplerStateType Type, DWORD dwValue)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES || Type >= STATEMANAGER11_MAX_TEXTURESTATES)
		return;

	DWORD dwCurrentValue = 0;
	GetSamplerState(dwStage, Type, &dwCurrentValue);
	m_SamplerStateStack[dwStage][Type].push(dwCurrentValue);
	SetSamplerState(dwStage, Type, dwValue);
}

void CStateManager11::RestoreSamplerState(DWORD dwStage, GrpSamplerStateType Type)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES || Type >= STATEMANAGER11_MAX_TEXTURESTATES)
		return;

	if (m_SamplerStateStack[dwStage][Type].empty())
		return;

	const DWORD dwPreviousValue = m_SamplerStateStack[dwStage][Type].top();
	m_SamplerStateStack[dwStage][Type].pop();
	SetSamplerState(dwStage, Type, dwPreviousValue);
}

// ============================================================================
// Helper Methods - DX9 to DX11 Translation
// ============================================================================

D3D11_BLEND CStateManager11::BlendToD3D11Blend(GrpBlendType value)
{
	switch (value)
	{
		case GRP_BLEND_ZERO: return D3D11_BLEND_ZERO;
		case GRP_BLEND_ONE: return D3D11_BLEND_ONE;
		case GRP_BLEND_SRCCOLOR: return D3D11_BLEND_SRC_COLOR;
		case GRP_BLEND_INVSRCCOLOR: return D3D11_BLEND_INV_SRC_COLOR;
		case GRP_BLEND_SRCALPHA: return D3D11_BLEND_SRC_ALPHA;
		case GRP_BLEND_INVSRCALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
		case GRP_BLEND_DESTALPHA: return D3D11_BLEND_DEST_ALPHA;
		case GRP_BLEND_INVDESTALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
		case GRP_BLEND_DESTCOLOR: return D3D11_BLEND_DEST_COLOR;
		case GRP_BLEND_INVDESTCOLOR: return D3D11_BLEND_INV_DEST_COLOR;
		case GRP_BLEND_SRCALPHASAT: return D3D11_BLEND_SRC_ALPHA_SAT;
		default: return D3D11_BLEND_ONE;
	}
}

D3D11_BLEND_OP CStateManager11::BlendOpToD3D11BlendOp(GrpBlendOpType value)
{
	switch (value)
	{
		case GRP_BLENDOP_ADD: return D3D11_BLEND_OP_ADD;
		case GRP_BLENDOP_SUBTRACT: return D3D11_BLEND_OP_SUBTRACT;
		case GRP_BLENDOP_REVSUBTRACT: return D3D11_BLEND_OP_REV_SUBTRACT;
		case GRP_BLENDOP_MIN: return D3D11_BLEND_OP_MIN;
		case GRP_BLENDOP_MAX: return D3D11_BLEND_OP_MAX;
		default: return D3D11_BLEND_OP_ADD;
	}
}

D3D11_COMPARISON_FUNC CStateManager11::CompareToD3D11Comparison(GrpCmpFuncType value)
{
	switch (value)
	{
		case GRP_CMP_NEVER: return D3D11_COMPARISON_NEVER;
		case GRP_CMP_LESS: return D3D11_COMPARISON_LESS;
		case GRP_CMP_EQUAL: return D3D11_COMPARISON_EQUAL;
		case GRP_CMP_LESSEQUAL: return D3D11_COMPARISON_LESS_EQUAL;
		case GRP_CMP_GREATER: return D3D11_COMPARISON_GREATER;
		case GRP_CMP_NOTEQUAL: return D3D11_COMPARISON_NOT_EQUAL;
		case GRP_CMP_GREATEREQUAL: return D3D11_COMPARISON_GREATER_EQUAL;
		case GRP_CMP_ALWAYS: return D3D11_COMPARISON_ALWAYS;
		default: return D3D11_COMPARISON_LESS;
	}
}

D3D11_CULL_MODE CStateManager11::CullToD3D11Cull(GrpCullType value)
{
	switch (value)
	{
		case GRP_CULL_NONE: return D3D11_CULL_NONE;
		case GRP_CULL_CW: return D3D11_CULL_FRONT;
		case GRP_CULL_CCW: return D3D11_CULL_BACK;
		default: return D3D11_CULL_BACK;
	}
}

D3D11_FILL_MODE CStateManager11::FillToD3D11Fill(GrpFillModeType value)
{
	switch (value)
	{
		case GRP_FILL_POINT: return D3D11_FILL_SOLID; // DX11 doesn't support point fill
		case GRP_FILL_WIREFRAME: return D3D11_FILL_WIREFRAME;
		case GRP_FILL_SOLID: return D3D11_FILL_SOLID;
		default: return D3D11_FILL_SOLID;
	}
}

D3D11_STENCIL_OP CStateManager11::StencilOpToD3D11StencilOp(GrpStencilOpType value)
{
	switch (value)
	{
		case GRP_STENCILOP_KEEP: return D3D11_STENCIL_OP_KEEP;
		case GRP_STENCILOP_ZERO: return D3D11_STENCIL_OP_ZERO;
		case GRP_STENCILOP_REPLACE: return D3D11_STENCIL_OP_REPLACE;
		case GRP_STENCILOP_INCRSAT: return D3D11_STENCIL_OP_INCR_SAT;
		case GRP_STENCILOP_DECRSAT: return D3D11_STENCIL_OP_DECR_SAT;
		case GRP_STENCILOP_INVERT: return D3D11_STENCIL_OP_INVERT;
		case GRP_STENCILOP_INCR: return D3D11_STENCIL_OP_INCR;
		case GRP_STENCILOP_DECR: return D3D11_STENCIL_OP_DECR;
		default: return D3D11_STENCIL_OP_KEEP;
	}
}

D3D11_FILTER CStateManager11::FilterToD3D11Filter(GrpTextureFilterType value)
{
	switch (value)
	{
		case GRP_TEXF_NONE: return D3D11_FILTER_MIN_MAG_MIP_POINT;
		case GRP_TEXF_POINT: return D3D11_FILTER_MIN_MAG_MIP_POINT;
		case GRP_TEXF_LINEAR: return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		case GRP_TEXF_ANISOTROPIC: return D3D11_FILTER_ANISOTROPIC;
		default: return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	}
}

D3D11_TEXTURE_ADDRESS_MODE CStateManager11::AddressToD3D11Address(GrpTextureAddressType value)
{
	switch (value)
	{
		case GRP_TADDRESS_WRAP: return D3D11_TEXTURE_ADDRESS_WRAP;
		case GRP_TADDRESS_MIRROR: return D3D11_TEXTURE_ADDRESS_MIRROR;
		case GRP_TADDRESS_CLAMP: return D3D11_TEXTURE_ADDRESS_CLAMP;
		case GRP_TADDRESS_BORDER: return D3D11_TEXTURE_ADDRESS_BORDER;
		case GRP_TADDRESS_MIRRORONCE: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
		default: return D3D11_TEXTURE_ADDRESS_WRAP;
	}
}

// ============================================================================
// Constant Buffer Updates
// ============================================================================

void CStateManager11::UpdateTransformBuffer()
{
	if (!m_pTransformCB || !m_pContext)
		return;

	D3D11_MAPPED_SUBRESOURCE ms;
	HRESULT hr = m_pContext->Map(m_pTransformCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
	if (SUCCEEDED(hr))
	{
		memcpy(ms.pData, &m_TransformCBData, sizeof(m_TransformCBData));
		m_pContext->Unmap(m_pTransformCB.Get(), 0);
	}
}

void CStateManager11::UpdateMaterialBuffer()
{
	if (!m_pMaterialCB || !m_pContext)
		return;

	D3D11_MAPPED_SUBRESOURCE ms;
	HRESULT hr = m_pContext->Map(m_pMaterialCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
	if (SUCCEEDED(hr))
	{
		memcpy(ms.pData, &m_MaterialCBData, sizeof(m_MaterialCBData));
		m_pContext->Unmap(m_pMaterialCB.Get(), 0);
	}
}

void CStateManager11::UpdateLightingBuffer()
{
	if (!m_pLightingCB || !m_pContext)
		return;

	D3D11_MAPPED_SUBRESOURCE ms;
	HRESULT hr = m_pContext->Map(m_pLightingCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
	if (SUCCEEDED(hr))
	{
		memcpy(ms.pData, &m_LightingCBData, sizeof(m_LightingCBData));
		m_pContext->Unmap(m_pLightingCB.Get(), 0);
		m_bLightingBufferDirty = false;
	}
}

void CStateManager11::UpdateLightingActiveCount()
{
	if (!m_bLightingEnabled)
	{
		m_LightingCBData.LightCount = 0;
		return;
	}

	int iActiveCount = 0;
	for (int i = 0; i < MAX_LIGHTS; ++i)
	{
		if (m_LightingCBData.Lights[i].Enabled != 0)
			++iActiveCount;
	}
	m_LightingCBData.LightCount = iActiveCount;
}

void CStateManager11::QueueLightingBufferUpdate()
{
	m_bLightingBufferDirty = true;
	if (0u == m_uLightingBatchDepth)
		UpdateLightingBuffer();
}

void CStateManager11::BeginLightBatch()
{
	if (m_uLightingBatchDepth < 0xffffffffu)
		++m_uLightingBatchDepth;
}

void CStateManager11::EndLightBatch()
{
	if (0u == m_uLightingBatchDepth)
		return;

	--m_uLightingBatchDepth;
	if (0u == m_uLightingBatchDepth && m_bLightingBufferDirty)
		UpdateLightingBuffer();
}

bool CStateManager11::EnsureDeviceReady(const char* c_szCaller)
{
	CGraphicDeviceDX11* pActiveDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pActiveDevice || !pActiveDevice->IsValid())
	{
		static DWORD s_dwLastNoDeviceLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastNoDeviceLogMS || (dwNow - s_dwLastNoDeviceLogMS) >= 3000u)
		{
			s_dwLastNoDeviceLogMS = dwNow;
			TraceError("DX11_STATE_MANAGER_RECOVERY_SKIP caller=%s reason=active_device_unavailable",
				(c_szCaller && c_szCaller[0]) ? c_szCaller : "unknown");
		}
		return false;
	}

	ID3D11Device* pActiveD3DDevice = pActiveDevice->GetDevice();
	ID3D11DeviceContext* pActiveContext = pActiveDevice->GetContext();
	if (!pActiveD3DDevice || !pActiveContext)
	{
		static DWORD s_dwLastNoActivePointersLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastNoActivePointersLogMS || (dwNow - s_dwLastNoActivePointersLogMS) >= 3000u)
		{
			s_dwLastNoActivePointersLogMS = dwNow;
			TraceError("DX11_STATE_MANAGER_RECOVERY_SKIP caller=%s reason=active_device_missing_pointers",
				(c_szCaller && c_szCaller[0]) ? c_szCaller : "unknown");
		}
		return false;
	}

	const bool bPointersMatchActive =
		(m_pDevice == pActiveDevice) &&
		(m_pD3DDevice == pActiveD3DDevice) &&
		(m_pContext == pActiveContext);
	const bool bHasRequiredBuffers =
		nullptr != m_pTransformCB.Get() &&
		nullptr != m_pMaterialCB.Get() &&
		nullptr != m_pLightingCB.Get() &&
		nullptr != m_pLegacyVSConstantsCB.Get() &&
		nullptr != m_pLegacyPSConstantsCB.Get();
	if (bPointersMatchActive && bHasRequiredBuffers)
		return true;

	Release();
	Initialize(pActiveDevice);

	if (m_pD3DDevice && m_pContext)
	{
		static DWORD s_dwLastRecoverLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastRecoverLogMS || (dwNow - s_dwLastRecoverLogMS) >= 3000u)
		{
			s_dwLastRecoverLogMS = dwNow;
			TraceError("DX11_STATE_MANAGER_RECOVERY_OK caller=%s",
				(c_szCaller && c_szCaller[0]) ? c_szCaller : "unknown");
		}
		return true;
	}

	static DWORD s_dwLastRecoverFailLogMS = 0u;
	const DWORD dwNow = GetTickCount();
	if (0u == s_dwLastRecoverFailLogMS || (dwNow - s_dwLastRecoverFailLogMS) >= 3000u)
	{
		s_dwLastRecoverFailLogMS = dwNow;
		TraceError("DX11_STATE_MANAGER_RECOVERY_FAIL caller=%s reason=post_init_device_missing",
			(c_szCaller && c_szCaller[0]) ? c_szCaller : "unknown");
	}
	return false;
}

void CStateManager11::FlushLegacyVSConstantsBuffer()
{
	if (!m_bLegacyVSConstantsDirty || !m_pLegacyVSConstantsCB || !m_pContext)
		return;

	if (m_dwLegacyVSDirtyEndRegister <= m_dwLegacyVSDirtyStartRegister)
	{
		m_bLegacyVSConstantsDirty = false;
		return;
	}

	const UINT uByteOffsetBegin = m_dwLegacyVSDirtyStartRegister * 16u;
	const UINT uByteOffsetEnd = m_dwLegacyVSDirtyEndRegister * 16u;
	const UINT uByteCount = uByteOffsetEnd - uByteOffsetBegin;
	const uint32_t uUploadRegisterStart = static_cast<uint32_t>(m_dwLegacyVSDirtyStartRegister);
	const uint32_t uUploadRegisterEnd = static_cast<uint32_t>(m_dwLegacyVSDirtyEndRegister);

	D3D11_BOX box = {};
	box.left = uByteOffsetBegin;
	box.right = uByteOffsetEnd;
	box.top = 0u;
	box.bottom = 1u;
	box.front = 0u;
	box.back = 1u;

	const float* pfSource = m_afLegacyVSConstants.data() + (m_dwLegacyVSDirtyStartRegister * 4u);
	m_pContext->UpdateSubresource(m_pLegacyVSConstantsCB.Get(), 0, &box, pfSource, uByteCount, uByteCount);
	m_pContext->VSSetConstantBuffers(3, 1, m_pLegacyVSConstantsCB.GetAddressOf());
	++m_kDebugDrawDiagnostics.uVSConstUploadCount;
	m_kDebugDrawDiagnostics.uVSConstUploadBytes += static_cast<uint32_t>(uByteCount);
	m_kDebugDrawDiagnostics.uVSConstUploadStartRegister = uUploadRegisterStart;
	m_kDebugDrawDiagnostics.uVSConstUploadEndRegister = uUploadRegisterEnd;

	m_bLegacyVSConstantsDirty = false;
	m_dwLegacyVSDirtyStartRegister = 0u;
	m_dwLegacyVSDirtyEndRegister = 0u;
}

void CStateManager11::FlushLegacyPSConstantsBuffer()
{
	if (!m_bLegacyPSConstantsDirty || !m_pLegacyPSConstantsCB || !m_pContext)
		return;

	if (m_dwLegacyPSDirtyEndRegister <= m_dwLegacyPSDirtyStartRegister)
	{
		m_bLegacyPSConstantsDirty = false;
		return;
	}

	const UINT uByteOffsetBegin = m_dwLegacyPSDirtyStartRegister * 16u;
	const UINT uByteOffsetEnd = m_dwLegacyPSDirtyEndRegister * 16u;
	const UINT uByteCount = uByteOffsetEnd - uByteOffsetBegin;
	const uint32_t uUploadRegisterStart = static_cast<uint32_t>(m_dwLegacyPSDirtyStartRegister);
	const uint32_t uUploadRegisterEnd = static_cast<uint32_t>(m_dwLegacyPSDirtyEndRegister);

	D3D11_BOX box = {};
	box.left = uByteOffsetBegin;
	box.right = uByteOffsetEnd;
	box.top = 0u;
	box.bottom = 1u;
	box.front = 0u;
	box.back = 1u;

	const float* pfSource = m_afLegacyPSConstants.data() + (m_dwLegacyPSDirtyStartRegister * 4u);
	m_pContext->UpdateSubresource(m_pLegacyPSConstantsCB.Get(), 0, &box, pfSource, uByteCount, uByteCount);
	m_pContext->PSSetConstantBuffers(3, 1, m_pLegacyPSConstantsCB.GetAddressOf());
	++m_kDebugDrawDiagnostics.uPSConstUploadCount;
	m_kDebugDrawDiagnostics.uPSConstUploadBytes += static_cast<uint32_t>(uByteCount);
	m_kDebugDrawDiagnostics.uPSConstUploadStartRegister = uUploadRegisterStart;
	m_kDebugDrawDiagnostics.uPSConstUploadEndRegister = uUploadRegisterEnd;

	m_bLegacyPSConstantsDirty = false;
	m_dwLegacyPSDirtyStartRegister = 0u;
	m_dwLegacyPSDirtyEndRegister = 0u;
}

void CStateManager11::InitializeSamplerStateCache(DWORD dwStage)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES)
		return;

	D3D11_SAMPLER_DESC& desc = m_SamplerDescCache[dwStage];
	ZeroMemory(&desc, sizeof(desc));
	desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.MipLODBias = 0.0f;
	desc.MaxAnisotropy = 1u;
	desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	desc.MinLOD = 0.0f;
	desc.MaxLOD = D3D11_FLOAT32_MAX;

	for (DWORD i = 0; i < STATEMANAGER11_MAX_TEXTURESTATES; ++i)
	{
		m_SamplerStateValueCache[dwStage][i] = 0u;
		m_SamplerStateValueValid[dwStage][i] = false;
	}

	auto setDefaultValue = [&](GrpSamplerStateType Type)
	{
		if (Type >= STATEMANAGER11_MAX_TEXTURESTATES)
			return;
		m_SamplerStateValueCache[dwStage][Type] = GetDefaultSamplerStateValue(Type);
		m_SamplerStateValueValid[dwStage][Type] = true;
	};

	setDefaultValue(GRP_SAMP_MINFILTER);
	setDefaultValue(GRP_SAMP_MAGFILTER);
	setDefaultValue(GRP_SAMP_MIPFILTER);
	setDefaultValue(GRP_SAMP_ADDRESSU);
	setDefaultValue(GRP_SAMP_ADDRESSV);
	setDefaultValue(GRP_SAMP_ADDRESSW);
	setDefaultValue(GRP_SAMP_MAXANISOTROPY);
	setDefaultValue(GRP_SAMP_BORDERCOLOR);
}

DWORD CStateManager11::GetDefaultSamplerStateValue(GrpSamplerStateType Type) const
{
	switch (Type)
	{
		case GRP_SAMP_MINFILTER:
		case GRP_SAMP_MAGFILTER:
		case GRP_SAMP_MIPFILTER:
			return GRP_TEXF_LINEAR;
		case GRP_SAMP_ADDRESSU:
		case GRP_SAMP_ADDRESSV:
		case GRP_SAMP_ADDRESSW:
			return GRP_TADDRESS_WRAP;
		case GRP_SAMP_MAXANISOTROPY:
			return 1u;
		case GRP_SAMP_BORDERCOLOR:
			return 0u;
		default:
			return 0u;
	}
}

// ============================================================================
// State Application (Flush cached state to DX11)
// ============================================================================

void CStateManager11::ApplyState()
{
	if (!EnsureDeviceReady("ApplyState"))
		return;

	if (m_bLightingBufferDirty)
		UpdateLightingBuffer();

	ApplyBlendState();
	ApplyDepthStencilState();
	ApplyRasterizerState();
	ApplyTextureBindings();
	ApplySamplerBindings();
	FlushLegacyVSConstantsBuffer();
	FlushLegacyPSConstantsBuffer();
	ApplyTopology();
}

void CStateManager11::ApplyBlendState()
{
	if (!m_CurrentState.bBlendDirty)
		return;

	HRESULT hr = CreateBlendState(m_CurrentState.BlendDesc, &m_CurrentState.pBlendState);
	if (FAILED(hr))
	{
		static DWORD s_dwLastBlendCreateFailLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastBlendCreateFailLogMS || (dwNow - s_dwLastBlendCreateFailLogMS) >= 2000u)
		{
			s_dwLastBlendCreateFailLogMS = dwNow;
			TraceError("DX11_STATE_BLEND_CREATE_FAIL hr=0x%08X src=%u dst=%u op=%u src_a=%u dst_a=%u op_a=%u mask=%u blend_enable=%u",
				static_cast<unsigned int>(hr),
				static_cast<unsigned int>(m_CurrentState.BlendDesc.RenderTarget[0].SrcBlend),
				static_cast<unsigned int>(m_CurrentState.BlendDesc.RenderTarget[0].DestBlend),
				static_cast<unsigned int>(m_CurrentState.BlendDesc.RenderTarget[0].BlendOp),
				static_cast<unsigned int>(m_CurrentState.BlendDesc.RenderTarget[0].SrcBlendAlpha),
				static_cast<unsigned int>(m_CurrentState.BlendDesc.RenderTarget[0].DestBlendAlpha),
				static_cast<unsigned int>(m_CurrentState.BlendDesc.RenderTarget[0].BlendOpAlpha),
				static_cast<unsigned int>(m_CurrentState.BlendDesc.RenderTarget[0].RenderTargetWriteMask),
				static_cast<unsigned int>(m_CurrentState.BlendDesc.RenderTarget[0].BlendEnable ? 1u : 0u));
		}

		// Runtime safety: recover to deterministic opaque blend state instead of keeping an invalid descriptor.
		m_CurrentState.BlendDesc = MakeSafeDefaultBlendDesc();
		hr = CreateBlendState(m_CurrentState.BlendDesc, &m_CurrentState.pBlendState);
		if (FAILED(hr))
			return;

		static DWORD s_dwLastBlendRecoverLogMS = 0u;
		if (0u == s_dwLastBlendRecoverLogMS || (dwNow - s_dwLastBlendRecoverLogMS) >= 2000u)
		{
			s_dwLastBlendRecoverLogMS = dwNow;
			TraceError("DX11_STATE_BLEND_RECOVERED fallback=opaque");
		}
	}

	if (SUCCEEDED(hr))
	{
		m_pContext->OMSetBlendState(
			m_CurrentState.pBlendState.Get(),
			m_CurrentState.BlendFactor,
			m_CurrentState.SampleMask
		);
		m_CurrentState.bBlendDirty = false;
	}
}

void CStateManager11::ApplyDepthStencilState()
{
	if (!m_CurrentState.bDepthStencilDirty)
		return;

	HRESULT hr = CreateDepthStencilState(m_CurrentState.DepthStencilDesc, &m_CurrentState.pDepthStencilState);
	if (SUCCEEDED(hr))
	{
		m_pContext->OMSetDepthStencilState(
			m_CurrentState.pDepthStencilState.Get(),
			m_CurrentState.StencilRef
		);
		m_CurrentState.bDepthStencilDirty = false;
	}
}

void CStateManager11::ApplyRasterizerState()
{
	if (!m_CurrentState.bRasterizerDirty)
		return;

	HRESULT hr = CreateRasterizerState(m_CurrentState.RasterizerDesc, &m_CurrentState.pRasterizerState);
	if (SUCCEEDED(hr))
	{
		m_pContext->RSSetState(m_CurrentState.pRasterizerState.Get());
		m_CurrentState.bRasterizerDirty = false;
	}
}

void CStateManager11::ApplyTextureBindings()
{
	for (DWORD i = 0; i < STATEMANAGER11_MAX_STAGES; i++)
	{
		if (m_CurrentState.bTexturesDirty[i])
		{
			m_pContext->PSSetShaderResources(i, 1, m_CurrentState.pTextures[i].GetAddressOf());
			m_CurrentState.bTexturesDirty[i] = false;
		}
	}
}

void CStateManager11::ApplySamplerBindings()
{
	for (DWORD i = 0; i < STATEMANAGER11_MAX_STAGES; i++)
	{
		if (m_CurrentState.bSamplersDirty[i])
		{
			m_pContext->PSSetSamplers(i, 1, m_CurrentState.pSamplers[i].GetAddressOf());
			m_CurrentState.bSamplersDirty[i] = false;
		}
	}
}

void CStateManager11::ApplyTopology()
{
	if (!m_CurrentState.bTopologyDirty)
		return;

	m_pContext->IASetPrimitiveTopology(m_CurrentState.Topology);
	m_CurrentState.bTopologyDirty = false;
}

void CStateManager11::ApplyInputLayout()
{
	// Input layout is applied immediately via SetInputLayout.
}

// ============================================================================
// State Object Creation (with caching)
// ============================================================================

HRESULT CStateManager11::CreateBlendState(const D3D11_BLEND_DESC& desc, ID3D11BlendState** ppState)
{
	if (!ppState)
		return E_POINTER;
	if (!EnsureDeviceReady("CreateBlendState"))
		return E_FAIL;

	// State object creation uses the current descriptor tracked by state cache.
	return m_pD3DDevice->CreateBlendState(&desc, ppState);
}

HRESULT CStateManager11::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& desc, ID3D11DepthStencilState** ppState)
{
	if (!ppState)
		return E_POINTER;
	if (!EnsureDeviceReady("CreateDepthStencilState"))
		return E_FAIL;

	return m_pD3DDevice->CreateDepthStencilState(&desc, ppState);
}

HRESULT CStateManager11::CreateRasterizerState(const D3D11_RASTERIZER_DESC& desc, ID3D11RasterizerState** ppState)
{
	if (!ppState)
		return E_POINTER;
	if (!EnsureDeviceReady("CreateRasterizerState"))
		return E_FAIL;

	return m_pD3DDevice->CreateRasterizerState(&desc, ppState);
}

HRESULT CStateManager11::CreateSamplerState(const D3D11_SAMPLER_DESC& desc, ID3D11SamplerState** ppState)
{
	if (!ppState)
		return E_POINTER;
	if (!EnsureDeviceReady("CreateSamplerState"))
		return E_FAIL;

	return m_pD3DDevice->CreateSamplerState(&desc, ppState);
}

// ============================================================================
// Transforms
// ============================================================================

void CStateManager11::SetTransform(GrpTransformStateType Type, const DirectX::XMFLOAT4X4* pMatrix)
{
	if (!pMatrix)
		return;

	switch (Type)
	{
		case GRP_TS_WORLD:
			m_TransformCBData.matWorld = *pMatrix;
			break;

		case GRP_TS_VIEW:
			m_TransformCBData.matView = *pMatrix;
			break;

		case GRP_TS_PROJECTION:
			m_TransformCBData.matProj = *pMatrix;
			break;

		case GRP_TS_TEXTURE0:
		case GRP_TS_TEXTURE1:
		case GRP_TS_TEXTURE2:
		case GRP_TS_TEXTURE3:
		case GRP_TS_TEXTURE4:
		case GRP_TS_TEXTURE5:
		case GRP_TS_TEXTURE6:
		case GRP_TS_TEXTURE7:
		{
			int index = Type - GRP_TS_TEXTURE0;
			if (index >= 0 && index < 8)
				m_TransformCBData.matTexture[index] = *pMatrix;
			break;
		}

		default:
			// Unsupported transform type
			break;
	}

	UpdateTransformBuffer();
}

void CStateManager11::GetTransform(GrpTransformStateType Type, DirectX::XMFLOAT4X4* pMatrix)
{
	if (!pMatrix)
		return;

	switch (Type)
	{
		case GRP_TS_WORLD:
			*pMatrix = m_TransformCBData.matWorld;
			break;

		case GRP_TS_VIEW:
			*pMatrix = m_TransformCBData.matView;
			break;

		case GRP_TS_PROJECTION:
			*pMatrix = m_TransformCBData.matProj;
			break;

		case GRP_TS_TEXTURE0:
		case GRP_TS_TEXTURE1:
		case GRP_TS_TEXTURE2:
		case GRP_TS_TEXTURE3:
		case GRP_TS_TEXTURE4:
		case GRP_TS_TEXTURE5:
		case GRP_TS_TEXTURE6:
		case GRP_TS_TEXTURE7:
		{
			int index = Type - GRP_TS_TEXTURE0;
			if (index >= 0 && index < 8)
				*pMatrix = m_TransformCBData.matTexture[index];
			break;
		}

		default:
			DirectX::XMStoreFloat4x4(pMatrix, DirectX::XMMatrixIdentity());
			break;
	}
}

void CStateManager11::SaveTransform(GrpTransformStateType Transform, const DirectX::XMFLOAT4X4* pMatrix)
{
	DirectX::XMFLOAT4X4 mat;
	GetTransform(Transform, &mat);
	m_TransformStack[Transform].push(mat);

	if (pMatrix)
		SetTransform(Transform, pMatrix);
}

void CStateManager11::RestoreTransform(GrpTransformStateType Transform)
{
	if (m_TransformStack[Transform].empty())
		return;

	DirectX::XMFLOAT4X4 mat = m_TransformStack[Transform].top();
	m_TransformStack[Transform].pop();
	SetTransform(Transform, &mat);
}

// ============================================================================
// Shader Constants
// ============================================================================

void CStateManager11::SetVertexShaderConstant(DWORD dwRegister, CONST void* pConstantData, DWORD dwConstantCount)
{
	if (!pConstantData || 0 == dwConstantCount || dwRegister >= STATEMANAGER11_MAX_VCONSTANTS)
		return;

	const DWORD dwClampedCount = (dwRegister + dwConstantCount > STATEMANAGER11_MAX_VCONSTANTS)
		? (STATEMANAGER11_MAX_VCONSTANTS - dwRegister)
		: dwConstantCount;
	++m_kDebugDrawDiagnostics.uVSConstSetCallCount;
	m_kDebugDrawDiagnostics.uVSConstSetRegisterCount += static_cast<uint32_t>(dwClampedCount);
	m_kDebugDrawDiagnostics.uVSConstSetLastRegister = static_cast<uint32_t>(dwRegister);
	m_kDebugDrawDiagnostics.uVSConstSetLastCount = static_cast<uint32_t>(dwClampedCount);
	if (dwClampedCount != dwConstantCount)
	{
		++m_kDebugDrawDiagnostics.uVSConstClampCount;
		m_kDebugDrawDiagnostics.uVSConstClampLastRegister = static_cast<uint32_t>(dwRegister);
		m_kDebugDrawDiagnostics.uVSConstClampLastRequested = static_cast<uint32_t>(dwConstantCount);
		m_kDebugDrawDiagnostics.uVSConstClampLastApplied = static_cast<uint32_t>(dwClampedCount);

		static DWORD s_dwLastVSConstClampLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastVSConstClampLogMS || (dwNow - s_dwLastVSConstClampLogMS) >= 2000u)
		{
			s_dwLastVSConstClampLogMS = dwNow;
			TraceError(
				"DX11_SHADERCONST_CLAMP stage=vs reg=%u requested=%u applied=%u max=%u frame_count=%u",
				static_cast<unsigned int>(dwRegister),
				static_cast<unsigned int>(dwConstantCount),
				static_cast<unsigned int>(dwClampedCount),
				static_cast<unsigned int>(STATEMANAGER11_MAX_VCONSTANTS),
				static_cast<unsigned int>(m_kDebugDrawDiagnostics.uVSConstClampCount));
		}
	}

	const size_t stDstOffset = static_cast<size_t>(dwRegister) * 4u;
	const size_t stCopyFloatCount = static_cast<size_t>(dwClampedCount) * 4u;
	const float* pfSource = reinterpret_cast<const float*>(pConstantData);
	memcpy(m_afLegacyVSConstants.data() + stDstOffset, pfSource, stCopyFloatCount * sizeof(float));

	const DWORD dwDirtyStart = dwRegister;
	const DWORD dwDirtyEnd = dwRegister + dwClampedCount; // exclusive
	if (!m_bLegacyVSConstantsDirty)
	{
		m_dwLegacyVSDirtyStartRegister = dwDirtyStart;
		m_dwLegacyVSDirtyEndRegister = dwDirtyEnd;
		m_bLegacyVSConstantsDirty = true;
	}
	else
	{
		if (dwDirtyStart < m_dwLegacyVSDirtyStartRegister)
			m_dwLegacyVSDirtyStartRegister = dwDirtyStart;
		if (dwDirtyEnd > m_dwLegacyVSDirtyEndRegister)
			m_dwLegacyVSDirtyEndRegister = dwDirtyEnd;
	}
}

void CStateManager11::SetPixelShaderConstant(DWORD dwRegister, CONST void* pConstantData, DWORD dwConstantCount)
{
	if (!pConstantData || 0 == dwConstantCount || dwRegister >= STATEMANAGER11_MAX_PCONSTANTS)
		return;

	const DWORD dwClampedCount = (dwRegister + dwConstantCount > STATEMANAGER11_MAX_PCONSTANTS)
		? (STATEMANAGER11_MAX_PCONSTANTS - dwRegister)
		: dwConstantCount;
	++m_kDebugDrawDiagnostics.uPSConstSetCallCount;
	m_kDebugDrawDiagnostics.uPSConstSetRegisterCount += static_cast<uint32_t>(dwClampedCount);
	m_kDebugDrawDiagnostics.uPSConstSetLastRegister = static_cast<uint32_t>(dwRegister);
	m_kDebugDrawDiagnostics.uPSConstSetLastCount = static_cast<uint32_t>(dwClampedCount);
	if (dwClampedCount != dwConstantCount)
	{
		++m_kDebugDrawDiagnostics.uPSConstClampCount;
		m_kDebugDrawDiagnostics.uPSConstClampLastRegister = static_cast<uint32_t>(dwRegister);
		m_kDebugDrawDiagnostics.uPSConstClampLastRequested = static_cast<uint32_t>(dwConstantCount);
		m_kDebugDrawDiagnostics.uPSConstClampLastApplied = static_cast<uint32_t>(dwClampedCount);

		static DWORD s_dwLastPSConstClampLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastPSConstClampLogMS || (dwNow - s_dwLastPSConstClampLogMS) >= 2000u)
		{
			s_dwLastPSConstClampLogMS = dwNow;
			TraceError(
				"DX11_SHADERCONST_CLAMP stage=ps reg=%u requested=%u applied=%u max=%u frame_count=%u",
				static_cast<unsigned int>(dwRegister),
				static_cast<unsigned int>(dwConstantCount),
				static_cast<unsigned int>(dwClampedCount),
				static_cast<unsigned int>(STATEMANAGER11_MAX_PCONSTANTS),
				static_cast<unsigned int>(m_kDebugDrawDiagnostics.uPSConstClampCount));
		}
	}

	const size_t stDstOffset = static_cast<size_t>(dwRegister) * 4u;
	const size_t stCopyFloatCount = static_cast<size_t>(dwClampedCount) * 4u;
	const float* pfSource = reinterpret_cast<const float*>(pConstantData);
	memcpy(m_afLegacyPSConstants.data() + stDstOffset, pfSource, stCopyFloatCount * sizeof(float));

	const DWORD dwDirtyStart = dwRegister;
	const DWORD dwDirtyEnd = dwRegister + dwClampedCount; // exclusive
	if (!m_bLegacyPSConstantsDirty)
	{
		m_dwLegacyPSDirtyStartRegister = dwDirtyStart;
		m_dwLegacyPSDirtyEndRegister = dwDirtyEnd;
		m_bLegacyPSConstantsDirty = true;
	}
	else
	{
		if (dwDirtyStart < m_dwLegacyPSDirtyStartRegister)
			m_dwLegacyPSDirtyStartRegister = dwDirtyStart;
		if (dwDirtyEnd > m_dwLegacyPSDirtyEndRegister)
			m_dwLegacyPSDirtyEndRegister = dwDirtyEnd;
	}
}

// ============================================================================
// Stream Sources
// ============================================================================

void CStateManager11::SetStreamSource(UINT StreamNumber, ID3D11Buffer* pStreamData, UINT Stride)
{
	SetStreamSource(StreamNumber, pStreamData, Stride, 0);
}

void CStateManager11::SetStreamSource(UINT StreamNumber, ID3D11Buffer* pStreamData, UINT Stride, UINT Offset)
{
	if (StreamNumber >= STATEMANAGER11_MAX_STREAMS)
		return;

	m_CurrentState.StreamData[StreamNumber] = CStreamData11(pStreamData, Stride, Offset);

	UINT offsets[] = { Offset };
	UINT strides[] = { Stride };
	m_pContext->IASetVertexBuffers(StreamNumber, 1, &pStreamData, strides, offsets);
}

void CStateManager11::SaveStreamSource(UINT StreamNumber, ID3D11Buffer* pStreamData, UINT Stride)
{
	if (StreamNumber >= STATEMANAGER11_MAX_STREAMS)
		return;

	m_StreamSourceStack[StreamNumber].push(m_CurrentState.StreamData[StreamNumber]);
	SetStreamSource(StreamNumber, pStreamData, Stride);
}

void CStateManager11::RestoreStreamSource(UINT StreamNumber)
{
	if (StreamNumber >= STATEMANAGER11_MAX_STREAMS || m_StreamSourceStack[StreamNumber].empty())
		return;

	CStreamData11 data = m_StreamSourceStack[StreamNumber].top();
	m_StreamSourceStack[StreamNumber].pop();
	SetStreamSource(StreamNumber, data.m_pBuffer, data.m_Stride, data.m_uOffset);
}

// ============================================================================
// Index Buffer
// ============================================================================

void CStateManager11::SetIndices(ID3D11Buffer* pIndexData, DXGI_FORMAT Format)
{
	SetIndices(pIndexData, Format, 0);
}

void CStateManager11::SetIndices(ID3D11Buffer* pIndexData, DXGI_FORMAT Format, UINT Offset)
{
	m_CurrentState.IndexData = CIndexData11(pIndexData, Format, Offset);
	m_pContext->IASetIndexBuffer(pIndexData, Format, Offset);
}

void CStateManager11::SetIndices(ID3D11Buffer* pIndexData, int Format)
{
	SetIndices(pIndexData, ResolveIndexFormat(Format), 0);
}

void CStateManager11::SetIndices(ID3D11Buffer* pIndexData, int Format, UINT Offset)
{
	SetIndices(pIndexData, ResolveIndexFormat(Format), Offset);
}

void CStateManager11::SaveIndices(ID3D11Buffer* pIndexData, DXGI_FORMAT Format)
{
	m_IndicesStack.push(m_CurrentState.IndexData);
	SetIndices(pIndexData, Format);
}

void CStateManager11::RestoreIndices()
{
	if (m_IndicesStack.empty())
		return;

	CIndexData11 data = m_IndicesStack.top();
	m_IndicesStack.pop();
	SetIndices(data.m_pIndexData, data.m_Format, data.m_uOffset);
}

// ============================================================================
// Vertex Shader
// ============================================================================

void CStateManager11::SetVertexShader(ID3D11VertexShader* pShader)
{
	m_CurrentState.pVertexShader = pShader;
	m_pContext->VSSetShader(pShader, nullptr, 0);
}

void CStateManager11::GetVertexShader(ID3D11VertexShader** ppShader)
{
	if (!ppShader)
		return;

	*ppShader = m_CurrentState.pVertexShader.Get();
	if (*ppShader)
		(*ppShader)->AddRef();
}

void CStateManager11::SaveVertexShader(ID3D11VertexShader* pShader)
{
	m_VertexShaderStack.push(m_CurrentState.pVertexShader);
	SetVertexShader(pShader);
}

void CStateManager11::RestoreVertexShader()
{
	if (m_VertexShaderStack.empty())
		return;

	Microsoft::WRL::ComPtr<ID3D11VertexShader> pShader = m_VertexShaderStack.top();
	m_VertexShaderStack.pop();
	SetVertexShader(pShader.Get());
}

// ============================================================================
// Input Layout (FVF)
// ============================================================================

void CStateManager11::SetInputLayout(ID3D11InputLayout* pInputLayout)
{
	m_CurrentState.pInputLayout = pInputLayout;
	m_pContext->IASetInputLayout(pInputLayout);
}

void CStateManager11::SetVertexDeclaration(ID3D11InputLayout* pInputLayout)
{
	SetInputLayout(pInputLayout);
}

void CStateManager11::GetInputLayout(ID3D11InputLayout** ppInputLayout)
{
	if (!ppInputLayout)
		return;

	*ppInputLayout = m_CurrentState.pInputLayout.Get();
	if (*ppInputLayout)
		(*ppInputLayout)->AddRef();
}

void CStateManager11::SaveInputLayout(ID3D11InputLayout* pInputLayout)
{
	m_InputLayoutStack.push(m_CurrentState.pInputLayout);
	SetInputLayout(pInputLayout);
}

void CStateManager11::RestoreInputLayout()
{
	if (m_InputLayoutStack.empty())
		return;

	Microsoft::WRL::ComPtr<ID3D11InputLayout> pLayout = m_InputLayoutStack.top();
	m_InputLayoutStack.pop();
	SetInputLayout(pLayout.Get());
}

// ============================================================================
// Pixel Shader
// ============================================================================

void CStateManager11::SetPixelShader(ID3D11PixelShader* pShader)
{
	m_CurrentState.pPixelShader = pShader;
	m_pContext->PSSetShader(pShader, nullptr, 0);
}

void CStateManager11::GetPixelShader(ID3D11PixelShader** ppShader)
{
	if (!ppShader)
		return;

	*ppShader = m_CurrentState.pPixelShader.Get();
	if (*ppShader)
		(*ppShader)->AddRef();
}

void CStateManager11::SavePixelShader(ID3D11PixelShader* pShader)
{
	m_PixelShaderStack.push(m_CurrentState.pPixelShader);
	SetPixelShader(pShader);
}

void CStateManager11::RestorePixelShader()
{
	if (m_PixelShaderStack.empty())
		return;

	Microsoft::WRL::ComPtr<ID3D11PixelShader> pShader = m_PixelShaderStack.top();
	m_PixelShaderStack.pop();
	SetPixelShader(pShader.Get());
}

// ============================================================================
// Drawing
// ============================================================================

namespace
{
	D3D11_PRIMITIVE_TOPOLOGY ToD3D11Topology(GrpPrimitiveType primitiveType)
	{
		switch (primitiveType)
		{
		case GRP_PT_POINTLIST:
			return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
		case GRP_PT_LINELIST:
			return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
		case GRP_PT_LINESTRIP:
			return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case GRP_PT_TRIANGLELIST:
			return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case GRP_PT_TRIANGLESTRIP:
			return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		default:
			return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		}
	}

#ifdef _DEBUG
	void TraceRTVlessPixelShaderDraw(CStateManager11* pStateManager, ID3D11DeviceContext* pContext, D3D11_PRIMITIVE_TOPOLOGY eTopology, bool bIndexedDraw, UINT uElementCount)
	{
		if (!pContext)
			return;

		ID3D11RenderTargetView* pRTV = nullptr;
		ID3D11DepthStencilView* pDSV = nullptr;
		pContext->OMGetRenderTargets(1u, &pRTV, &pDSV);

		ID3D11PixelShader* pPS = nullptr;
		pContext->PSGetShader(&pPS, nullptr, nullptr);

		const bool bHasRTV = (nullptr != pRTV);
		const bool bHasPS = (nullptr != pPS);

		if (!bHasRTV && bHasPS)
		{
			if (pStateManager)
				pStateManager->NoteNoRTVWithPSDraw(bIndexedDraw, eTopology, uElementCount, nullptr != pDSV);

			static DWORD s_dwLastNoRTVWithPSLogMS = 0u;
			const DWORD dwNow = GetTickCount();
			if (0u == s_dwLastNoRTVWithPSLogMS || (dwNow - s_dwLastNoRTVWithPSLogMS) >= 2000u)
			{
				s_dwLastNoRTVWithPSLogMS = dwNow;
				TraceError(
					"DX11_DRAW_NO_RTV_WITH_PS indexed=%u topology=%u elements=%u depth_bound=%u",
					bIndexedDraw ? 1u : 0u,
					static_cast<unsigned int>(eTopology),
					static_cast<unsigned int>(uElementCount),
					nullptr != pDSV ? 1u : 0u);
			}
		}

		if (pPS)
			pPS->Release();
		if (pDSV)
			pDSV->Release();
		if (pRTV)
			pRTV->Release();
	}
#endif
}

HRESULT CStateManager11::DrawPrimitive(GrpPrimitiveType PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
	return DrawPrimitive(ToD3D11Topology(PrimitiveType), StartVertex, PrimitiveCount);
}

HRESULT CStateManager11::DrawPrimitive(D3D11_PRIMITIVE_TOPOLOGY PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
	ApplyState();

	m_CurrentState.Topology = PrimitiveType;
	m_pContext->IASetPrimitiveTopology(PrimitiveType);

	const UINT vertexCount = PrimitiveCountToVertexCount(PrimitiveType, PrimitiveCount);
	if (vertexCount == 0u)
		return S_OK;

#ifdef _DEBUG
	TraceRTVlessPixelShaderDraw(this, m_pContext, PrimitiveType, false, vertexCount);
#endif
	m_pContext->Draw(vertexCount, StartVertex);

#ifdef _DEBUG
	m_iDrawCallCount++;
#endif

	return S_OK;
}

HRESULT CStateManager11::DrawIndexedPrimitive(GrpPrimitiveType PrimitiveType, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimCount)
{
	return DrawIndexedPrimitive(ToD3D11Topology(PrimitiveType), BaseVertexIndex, MinIndex, NumVertices, StartIndex, PrimCount);
}

HRESULT CStateManager11::DrawIndexedPrimitive(GrpPrimitiveType PrimitiveType, INT BaseVertexIndex, UINT NumVertices, UINT StartIndex, UINT PrimCount)
{
	return DrawIndexedPrimitive(ToD3D11Topology(PrimitiveType), BaseVertexIndex, 0, NumVertices, StartIndex, PrimCount);
}

HRESULT CStateManager11::DrawIndexedPrimitive(D3D11_PRIMITIVE_TOPOLOGY PrimitiveType, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimCount)
{
	ApplyState();

	m_CurrentState.Topology = PrimitiveType;
	m_pContext->IASetPrimitiveTopology(PrimitiveType);

	const UINT indexCount = PrimitiveCountToIndexCount(PrimitiveType, PrimCount);
	if (indexCount == 0u)
		return S_OK;

#ifdef _DEBUG
	TraceRTVlessPixelShaderDraw(this, m_pContext, PrimitiveType, true, indexCount);
#endif
	m_pContext->DrawIndexed(indexCount, StartIndex, BaseVertexIndex);

#ifdef _DEBUG
	m_iDrawCallCount++;
#endif

	return S_OK;
}

// ============================================================================
// Debug
// ============================================================================

#ifdef _DEBUG
void CStateManager11::ResetDrawCallCounter()
{
	m_iDrawCallCount = 0;
}
#endif


void CStateManager11::SetTextureStageCompatState(DWORD dwStage, GrpTextureStageStateType Type, DWORD dwValue)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES || Type >= STATEMANAGER11_MAX_TEXTURESTATES)
		return;

	m_TextureStageStateCache[dwStage][Type] = dwValue;
	m_TextureStageStateValid[dwStage][Type] = true;

	// DX11 keeps texture-stage behavior in shader code. Cache for compatibility-only callers.
}

void CStateManager11::GetTextureStageState(DWORD dwStage, GrpTextureStageStateType Type, DWORD* pdwValue)
{
	if (!pdwValue)
		return;
	if (dwStage >= STATEMANAGER11_MAX_STAGES || Type >= STATEMANAGER11_MAX_TEXTURESTATES)
	{
		*pdwValue = 0u;
		return;
	}

	*pdwValue = m_TextureStageStateValid[dwStage][Type] ? m_TextureStageStateCache[dwStage][Type] : 0u;
}

void CStateManager11::SaveTextureStageState(DWORD dwStage, GrpTextureStageStateType Type, DWORD dwValue)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES || Type >= STATEMANAGER11_MAX_TEXTURESTATES)
		return;

	DWORD dwCurrent = 0u;
	GetTextureStageState(dwStage, Type, &dwCurrent);
	m_TextureStageStateStack[dwStage][Type].push(dwCurrent);
	SetTextureStageCompatState(dwStage, Type, dwValue);
}

void CStateManager11::RestoreTextureStageState(DWORD dwStage, GrpTextureStageStateType Type)
{
	if (dwStage >= STATEMANAGER11_MAX_STAGES || Type >= STATEMANAGER11_MAX_TEXTURESTATES)
		return;
	if (m_TextureStageStateStack[dwStage][Type].empty())
		return;

	const DWORD dwPrev = m_TextureStageStateStack[dwStage][Type].top();
	m_TextureStageStateStack[dwStage][Type].pop();
	SetTextureStageCompatState(dwStage, Type, dwPrev);
}

void CStateManager11::SetVertexFormatFlags(DWORD dwFVF)
{
	m_dwCurrentFVF = dwFVF;
}

void CStateManager11::GetFVF(DWORD* pdwFVF)
{
	if (!pdwFVF)
		return;
	*pdwFVF = m_dwCurrentFVF;
}

void CStateManager11::SaveFVF(DWORD dwFVF)
{
	m_FVFStack.push(m_dwCurrentFVF);
	SetVertexFormatFlags(dwFVF);
}

void CStateManager11::RestoreFVF()
{
	if (m_FVFStack.empty())
		return;
	m_dwCurrentFVF = m_FVFStack.top();
	m_FVFStack.pop();
}
