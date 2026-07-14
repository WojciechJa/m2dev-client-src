/******************************************************************************
  StateManager11.h - DX11 State Management Layer

  DX11 Model Sync: M2-STATECORE-CUT-58 Phase 2
  Purpose: DX11-compatible replacement for CStateManager (DX9)

  Architecture:
  - Maintains same API as CStateManager for compatibility
  - Translates DX9 state management calls to DX11 state objects
  - Provides save/restore pattern for atomic state changes
  - Caches DX11 state objects to reduce creation overhead

  Consumer Usage:
    // No code changes needed - uses same API as DX9 StateManager
    STATEMANAGER.SetRenderState(GRP_RS_ALPHABLENDENABLE, TRUE);
    STATEMANAGER.SetTexture(0, pTexture);
    STATEMANAGER.SetTransform(GRP_TS_WORLD, &matWorld);
    STATEMANAGER.DrawPrimitive(...);

  Implementation:
    - Runtime dispatch in StateManager.cpp selects DX9 or DX11 path
    - DX11 path: SetRenderState → Update blend descriptor → Create/Cache state object
    - DX11 path: SetTransform → Update constant buffer → VSSetConstantBuffers
    - DX11 path: SetTexture → PSSetShaderResources
    - DX11 path: SetTextureStageState → shader-owned stage behavior

  Author: MODEL2 (2026-03-22)
******************************************************************************/

#ifndef __STATEMANAGER11_H
#define __STATEMANAGER11_H

#include <d3d11.h>
#include <DirectXMath.h>
#include "DirectXMathHelpers.h"
#include <cstdint>
#include <vector>
#include <stack>
#include <array>
#include <wrl/client.h>
#include "EterBase/Singleton.h"
#include "GrpBase.h"
#include "LightDesc.h"
#include "GrpDeviceDX11.h"

// Forward declarations
class CGraphicDeviceDX11;

// ----------------------------------------------------------------------------
// Legacy render symbol ownership is centralized in GrpBase.h.
// StateManager11.h must not redeclare legacy enums/types to avoid header collisions.
// ----------------------------------------------------------------------------

// DX11 State Management Constants (matching DX9 limits)
static const DWORD STATEMANAGER11_MAX_RENDERSTATES = 256;
static const DWORD STATEMANAGER11_MAX_TEXTURESTATES = 128;
static const DWORD STATEMANAGER11_MAX_STAGES = 8;
static const DWORD STATEMANAGER11_MAX_VCONSTANTS = 96;
static const DWORD STATEMANAGER11_MAX_PCONSTANTS = 8;
static const DWORD STATEMANAGER11_MAX_TRANSFORMSTATES = 300;
static const DWORD STATEMANAGER11_MAX_STREAMS = 16;

// DX11 Constant Buffer Structures
struct CBTransform
{
	DirectX::XMFLOAT4X4 matWorld;
	DirectX::XMFLOAT4X4 matView;
	DirectX::XMFLOAT4X4 matProj;
	DirectX::XMFLOAT4X4 matTexture[8];
};

struct CBMaterial
{
	DirectX::XMFLOAT4 Diffuse;
	DirectX::XMFLOAT4 Ambient;
	DirectX::XMFLOAT4 Specular;
	DirectX::XMFLOAT4 Emissive;
	float Power;
	float _pad[3];
};

struct CBLight
{
	DirectX::XMFLOAT4 Position;
	DirectX::XMFLOAT4 Direction;
	DirectX::XMFLOAT4 Diffuse;
	DirectX::XMFLOAT4 Specular;
	DirectX::XMFLOAT4 Ambient;
	float Type;
	float Range;
	float Falloff;
	float Attenuation0;
	float Attenuation1;
	float Attenuation2;
	float Theta;
	float Phi;
	int Enabled;
	float _pad[3];
};

static const int MAX_LIGHTS = 8;

struct CBLighting
{
	CBLight Lights[MAX_LIGHTS];
	DirectX::XMFLOAT4 Ambient;
	int LightCount;
	float _pad[3];
};

// Stream data for DX11 vertex buffers
class CStreamData11
{
public:
	CStreamData11(ID3D11Buffer* pBuffer = nullptr, UINT Stride = 0)
	{
		m_pBuffer = pBuffer;
		m_Stride = Stride;
		m_uOffset = 0;
	}
	CStreamData11(ID3D11Buffer* pBuffer, UINT Stride, UINT Offset)
	{
		m_pBuffer = pBuffer;
		m_Stride = Stride;
		m_uOffset = Offset;
	}

	bool operator == (const CStreamData11& rhs) const
	{
		return ((m_pBuffer == rhs.m_pBuffer) && (m_Stride == rhs.m_Stride) && (m_uOffset == rhs.m_uOffset));
	}

	ID3D11Buffer* m_pBuffer;
	UINT m_Stride;
	UINT m_uOffset;
};

// Index data for DX11 index buffers
class CIndexData11
{
public:
	CIndexData11(ID3D11Buffer* pIndexData = nullptr, DXGI_FORMAT Format = DXGI_FORMAT_R16_UINT, UINT Offset = 0)
	{
		m_pIndexData = pIndexData;
		m_Format = Format;
		m_uOffset = Offset;
	}

	bool operator == (const CIndexData11& rhs) const
	{
		return ((m_pIndexData == rhs.m_pIndexData) && (m_Format == rhs.m_Format) && (m_uOffset == rhs.m_uOffset));
	}

	ID3D11Buffer* m_pIndexData;
	DXGI_FORMAT m_Format;
	UINT m_uOffset;
};

// DX11 State Cache (descriptors for state object creation)
struct StateCache
{
	// Blend state descriptor
	D3D11_BLEND_DESC BlendDesc;
	FLOAT BlendFactor[4];
	UINT SampleMask;

	// Depth-stencil state descriptor
	D3D11_DEPTH_STENCIL_DESC DepthStencilDesc;
	UINT StencilRef;

	// Rasterizer state descriptor
	D3D11_RASTERIZER_DESC RasterizerDesc;

	// Current state objects
	Microsoft::WRL::ComPtr<ID3D11BlendState> pBlendState;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> pDepthStencilState;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> pRasterizerState;

	// Texture bindings
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pTextures[STATEMANAGER11_MAX_STAGES];
	Microsoft::WRL::ComPtr<ID3D11SamplerState> pSamplers[STATEMANAGER11_MAX_STAGES];

	// Vertex/Index bindings
	CStreamData11 StreamData[STATEMANAGER11_MAX_STREAMS];
	CIndexData11 IndexData;

	// Shaders
	Microsoft::WRL::ComPtr<ID3D11VertexShader> pVertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pPixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> pInputLayout;

	// Primitive topology
	D3D11_PRIMITIVE_TOPOLOGY Topology;

	// Dirty flags
	bool bBlendDirty;
	bool bDepthStencilDirty;
	bool bRasterizerDirty;
	bool bTexturesDirty[STATEMANAGER11_MAX_STAGES];
	bool bSamplersDirty[STATEMANAGER11_MAX_STAGES];
	bool bTopologyDirty;

	// Default constructor
	StateCache()
	{
		// Initialize descriptors to DX11 defaults
		ZeroMemory(&BlendDesc, sizeof(BlendDesc));
		ZeroMemory(&DepthStencilDesc, sizeof(DepthStencilDesc));
		ZeroMemory(&RasterizerDesc, sizeof(RasterizerDesc));
		ZeroMemory(BlendFactor, sizeof(BlendFactor));

		SampleMask = 0xFFFFFFFF;
		StencilRef = 0;

		// Set default blend state (opaque)
		BlendDesc.AlphaToCoverageEnable = FALSE;
		BlendDesc.IndependentBlendEnable = FALSE;
		for (UINT i = 0; i < 8; i++)
		{
			BlendDesc.RenderTarget[i].BlendEnable = FALSE;
			BlendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_ONE;
			BlendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_ZERO;
			BlendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
			BlendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
			BlendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
			BlendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			BlendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}

		// Set default depth-stencil state
		DepthStencilDesc.DepthEnable = TRUE;
		DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		DepthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
		DepthStencilDesc.StencilEnable = FALSE;
		DepthStencilDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
		DepthStencilDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
		DepthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		DepthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		DepthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		DepthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		DepthStencilDesc.BackFace = DepthStencilDesc.FrontFace;

		// Set default rasterizer state
		RasterizerDesc.FillMode = D3D11_FILL_SOLID;
		RasterizerDesc.CullMode = D3D11_CULL_BACK;
		RasterizerDesc.FrontCounterClockwise = FALSE;
		RasterizerDesc.DepthBias = 0;
		RasterizerDesc.DepthBiasClamp = 0.0f;
		RasterizerDesc.SlopeScaledDepthBias = 0.0f;
		RasterizerDesc.DepthClipEnable = TRUE;
		RasterizerDesc.ScissorEnable = FALSE;
		RasterizerDesc.MultisampleEnable = FALSE;
		RasterizerDesc.AntialiasedLineEnable = FALSE;

		// Initialize other state
		Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		// Mark all as dirty
		bBlendDirty = true;
		bDepthStencilDirty = true;
		bRasterizerDirty = true;
		bTopologyDirty = true;
		for (UINT i = 0; i < STATEMANAGER11_MAX_STAGES; i++)
		{
			bTexturesDirty[i] = true;
			bSamplersDirty[i] = true;
		}
	}
};

// DX11 State Manager
class CStateManager11 : public CSingleton<CStateManager11>
{
public:
	struct SDebugDrawDiagnostics
	{
		uint32_t uNoRTVWithPSCount;
		uint32_t uNoRTVWithPSIndexedCount;
		uint32_t uNoRTVWithPSNonIndexedCount;
		uint32_t uNoRTVWithPSLastTopology;
		uint32_t uNoRTVWithPSLastElements;
		uint32_t uNoRTVWithPSLastDepthBound;
		uint32_t uUnsupportedRenderStateCount;
		uint32_t uUnsupportedRenderStateLastType;
		uint32_t uUnsupportedRenderStateLastValue;
		uint32_t uFogEnable;
		uint32_t uFogMode;
		uint32_t uFogRangeEnable;
		uint32_t uFogColor;
		uint32_t uFogDensity;
		uint32_t uFogStart;
		uint32_t uFogEnd;
		uint32_t uVSConstClampCount;
		uint32_t uVSConstClampLastRegister;
		uint32_t uVSConstClampLastRequested;
		uint32_t uVSConstClampLastApplied;
		uint32_t uVSConstSetCallCount;
		uint32_t uVSConstSetRegisterCount;
		uint32_t uVSConstSetLastRegister;
		uint32_t uVSConstSetLastCount;
		uint32_t uVSConstUploadCount;
		uint32_t uVSConstUploadBytes;
		uint32_t uVSConstUploadStartRegister;
		uint32_t uVSConstUploadEndRegister;
		uint32_t uPSConstClampCount;
		uint32_t uPSConstClampLastRegister;
		uint32_t uPSConstClampLastRequested;
		uint32_t uPSConstClampLastApplied;
		uint32_t uPSConstSetCallCount;
		uint32_t uPSConstSetRegisterCount;
		uint32_t uPSConstSetLastRegister;
		uint32_t uPSConstSetLastCount;
		uint32_t uPSConstUploadCount;
		uint32_t uPSConstUploadBytes;
		uint32_t uPSConstUploadStartRegister;
		uint32_t uPSConstUploadEndRegister;

		SDebugDrawDiagnostics()
			: uNoRTVWithPSCount(0u)
			, uNoRTVWithPSIndexedCount(0u)
			, uNoRTVWithPSNonIndexedCount(0u)
			, uNoRTVWithPSLastTopology(0u)
			, uNoRTVWithPSLastElements(0u)
			, uNoRTVWithPSLastDepthBound(0u)
			, uUnsupportedRenderStateCount(0u)
			, uUnsupportedRenderStateLastType(0u)
			, uUnsupportedRenderStateLastValue(0u)
			, uFogEnable(0u)
			, uFogMode(0u)
			, uFogRangeEnable(0u)
			, uFogColor(0u)
			, uFogDensity(0u)
			, uFogStart(0u)
			, uFogEnd(0u)
			, uVSConstClampCount(0u)
			, uVSConstClampLastRegister(0u)
			, uVSConstClampLastRequested(0u)
			, uVSConstClampLastApplied(0u)
			, uVSConstSetCallCount(0u)
			, uVSConstSetRegisterCount(0u)
			, uVSConstSetLastRegister(0u)
			, uVSConstSetLastCount(0u)
			, uVSConstUploadCount(0u)
			, uVSConstUploadBytes(0u)
			, uVSConstUploadStartRegister(0u)
			, uVSConstUploadEndRegister(0u)
			, uPSConstClampCount(0u)
			, uPSConstClampLastRegister(0u)
			, uPSConstClampLastRequested(0u)
			, uPSConstClampLastApplied(0u)
			, uPSConstSetCallCount(0u)
			, uPSConstSetRegisterCount(0u)
			, uPSConstSetLastRegister(0u)
			, uPSConstSetLastCount(0u)
			, uPSConstUploadCount(0u)
			, uPSConstUploadBytes(0u)
			, uPSConstUploadStartRegister(0u)
			, uPSConstUploadEndRegister(0u)
		{
		}
	};

	CStateManager11();
	virtual ~CStateManager11();

	// Initialization
	void Initialize(CGraphicDeviceDX11* pDevice);
	void Release();

	// Scene management (DX11 doesn't need explicit BeginScene/EndScene, but provided for compatibility)
	bool BeginScene();
	void EndScene();
	void SetDefaultState();

	// ========================================================================
	// Material (DX9: SetMaterial → DX11: Constant buffer)
	// ========================================================================
	void SaveMaterial();
	void SaveMaterial(const GrpMaterial* pMaterial);
	void RestoreMaterial();
	void SetMaterial(const GrpMaterial* pMaterial);
	void GetMaterial(GrpMaterial* pMaterial);

	// Lights (DX9: SetLight → DX11: Constant buffer)
	void SetLight(DWORD index, const SLightDesc* pLight);
	void SetLightEnable(DWORD index, BOOL bEnable);
	void GetLight(DWORD index, SLightDesc* pLight);
	void BeginLightBatch();
	void EndLightBatch();

	// Scissor Rect
	void SetScissorRect(const RECT& c_rRect);
	void GetScissorRect(RECT* pRect);

	// ========================================================================
	// M3-ETERLIB-STATECORE-69: Semantic DX11 State API
	// ========================================================================
	// High-level state setters that replace DX9 render states
	// These methods provide direct DX11 state management without GRP_RS_* constants

	// Blend modes (replaces GRP_RS_ALPHABLENDENABLE, GRP_RS_SRCBLEND, GRP_RS_DESTBLEND)
	enum class EBlendMode
	{
		Opaque,           // Alpha blend disabled
		AlphaBlend,       // SRCALPHA, INVSRCALPHA (standard transparency)
		Additive,         // ONE, ONE (additive blending)
		Screen,           // SRCALPHA, INVSRCCOLOR (screen blend)
		Modulate,         // DESTCOLOR, ZERO (modulate/multiply)
		ColorDodge,       // SRCALPHA, ONE (color dodge - special case)
		Custom            // Uses custom src/dest blend factors set via SetRenderState
	};

	void SetBlendMode(EBlendMode mode);
	void SetCustomBlendFactors(DWORD dwSrcBlend, DWORD dwDestBlend, DWORD dwBlendOp = GRP_BLENDOP_ADD);

	// Depth modes (replaces GRP_RS_ZENABLE, GRP_RS_ZWRITEENABLE, GRP_RS_ZFUNC)
	enum class EDepthMode
	{
		Disabled,         // Z test disabled
		ReadOnly,         // Z test enabled, Z write disabled
	ReadWrite          // Z test enabled, Z write enabled (default)
	};

	void SetDepthMode(EDepthMode mode);
	void SetDepthComparisonFunc(GrpCmpFuncType func); // Replaces depth compare render-state call sites

	// Color write mask (replaces GRP_RS_COLORWRITEENABLE)
	void SetColorWriteEnable(bool bRed, bool bGreen, bool bBlue, bool bAlpha);
	void SetColorWriteEnableAll();

	// Lighting toggle (replaces GRP_RS_LIGHTING call sites).
	void SetLightingEnabled(bool bEnable);

	// Fill mode (replaces GRP_RS_FILLMODE)
	enum class EFillMode
	{
		Solid,
		Wireframe
	};

	void SetFillMode(EFillMode mode);

	// Cull mode (replaces GRP_RS_CULLMODE)
	enum class ECullMode
	{
		None,
		Clockwise,
		CounterClockwise
	};

	void SetCullMode(ECullMode mode);

	// ========================================================================
	// Render States (DX9: SetRenderState �' DX11: Blend/Depth/Rasterizer states)
	// ========================================================================
	void SaveRenderState(GrpRenderStateType Type, DWORD dwValue);
	void RestoreRenderState(GrpRenderStateType Type);
	void SetRenderState(GrpRenderStateType Type, DWORD Value);
	void GetRenderState(GrpRenderStateType Type, DWORD* pdwValue);
	void SaveLightingEnabled(BOOL bEnable);
	void RestoreLightingEnabled();
	void SaveFogEnabled(BOOL bEnable);
	void RestoreFogEnabled();
	void SetFogColorValue(DWORD dwFogColor);
	void SetFogExpDensity(float fDensity);
	void SetFogLinearRange(float fFogNear, float fFogFar);
	void SetFogModeLinear();
	void SetFogModeExp();
	void SetFogRangeEnabled(BOOL bEnable);

	// ========================================================================
	// Textures (DX9: SetTexture → DX11: PSSetShaderResources)
	// ========================================================================
	void SaveTexture(DWORD dwStage, ID3D11ShaderResourceView* pSRV);
	void RestoreTexture(DWORD dwStage);
	void SetTexture(DWORD dwStage, ID3D11ShaderResourceView* pSRV);
	void GetTexture(DWORD dwStage, ID3D11ShaderResourceView** ppSRV);

	// ========================================================================
	// Texture Stage States (DX9: SetTextureStageState → DX11 shader-owned stage behavior)
	// ========================================================================
	void SaveTextureStageState(DWORD dwStage, GrpTextureStageStateType Type, DWORD dwValue);
	void RestoreTextureStageState(DWORD dwStage, GrpTextureStageStateType Type);
	void SetTextureStageCompatState(DWORD dwStage, GrpTextureStageStateType Type, DWORD dwValue);
	void GetTextureStageState(DWORD dwStage, GrpTextureStageStateType Type, DWORD* pdwValue);

	// ========================================================================
	// Sampler States (DX9: SetSamplerState → DX11: PSSetSamplers)
	// ========================================================================
	void SaveSamplerState(DWORD dwStage, GrpSamplerStateType Type, DWORD dwValue);
	void RestoreSamplerState(DWORD dwStage, GrpSamplerStateType Type);
	void SetSamplerState(DWORD dwStage, GrpSamplerStateType Type, DWORD dwValue);
	void GetSamplerState(DWORD dwStage, GrpSamplerStateType Type, DWORD* pdwValue);

	// ========================================================================
	// Vertex Shader (DX9: SetVertexShader → DX11: VSSetShader)
	// ========================================================================
	void SaveVertexShader(ID3D11VertexShader* pShader);
	void RestoreVertexShader();
	void SetVertexShader(ID3D11VertexShader* pShader);
	void GetVertexShader(ID3D11VertexShader** ppShader);

	// ========================================================================
	// Input Layout (DX9: SetVertexDeclaration → DX11: IASetInputLayout)
	// ========================================================================
		void SaveInputLayout(ID3D11InputLayout* pInputLayout);
		void RestoreInputLayout();
		void SetInputLayout(ID3D11InputLayout* pInputLayout);
		void GetInputLayout(ID3D11InputLayout** ppInputLayout);
		void SetVertexDeclaration(ID3D11InputLayout* pInputLayout);

	// ========================================================================
	// FVF (DX9: SetFVF → DX11: InputLayout creation)
	// ========================================================================
	void SaveFVF(DWORD dwFVF);
	void RestoreFVF();
	void SetVertexFormatFlags(DWORD dwFVF);
	void GetFVF(DWORD* pdwFVF);

	// ========================================================================
	// Pixel Shader (DX9: SetPixelShader → DX11: PSSetShader)
	// ========================================================================
	void SavePixelShader(ID3D11PixelShader* pShader);
	void RestorePixelShader();
	void SetPixelShader(ID3D11PixelShader* pShader);
	void GetPixelShader(ID3D11PixelShader** ppShader);

	// ========================================================================
	// Transforms (DX9: SetTransform → DX11: Constant buffer)
	// ========================================================================
	void SaveTransform(GrpTransformStateType Transform, const DirectX::XMFLOAT4X4* pMatrix);
	void RestoreTransform(GrpTransformStateType Transform);
	void SetTransform(GrpTransformStateType Type, const DirectX::XMFLOAT4X4* pMatrix);
	void GetTransform(GrpTransformStateType Type, DirectX::XMFLOAT4X4* pMatrix);

	// ========================================================================
	// Shader Constants (DX9: Set*ShaderConstantF → DX11: UpdateSubresource)
	// ========================================================================
	void SetVertexShaderConstant(DWORD dwRegister, CONST void* pConstantData, DWORD dwConstantCount);
	void SetPixelShaderConstant(DWORD dwRegister, CONST void* pConstantData, DWORD dwConstantCount);

	// ========================================================================
	// Stream Sources (DX9: SetStreamSource → DX11: IASetVertexBuffers)
	// ========================================================================
	void SaveStreamSource(UINT StreamNumber, ID3D11Buffer* pStreamData, UINT Stride);
	void RestoreStreamSource(UINT StreamNumber);
	void SetStreamSource(UINT StreamNumber, ID3D11Buffer* pStreamData, UINT Stride);
	void SetStreamSource(UINT StreamNumber, ID3D11Buffer* pStreamData, UINT Stride, UINT Offset);

	// ========================================================================
	// Index Buffer (DX9: SetIndices → DX11: IASetIndexBuffer)
	// ========================================================================
		void SaveIndices(ID3D11Buffer* pIndexData, DXGI_FORMAT Format);
		void RestoreIndices();
		void SetIndices(ID3D11Buffer* pIndexData, DXGI_FORMAT Format);
		void SetIndices(ID3D11Buffer* pIndexData, DXGI_FORMAT Format, UINT Offset);
		void SetIndices(ID3D11Buffer* pIndexData, int Format);
		void SetIndices(ID3D11Buffer* pIndexData, int Format, UINT Offset);

	// ========================================================================
	// Drawing (DX9: DrawPrimitive* → DX11: Draw*)
	// ========================================================================
	HRESULT DrawPrimitive(GrpPrimitiveType PrimitiveType, UINT StartVertex, UINT PrimitiveCount);
	HRESULT DrawPrimitive(D3D11_PRIMITIVE_TOPOLOGY PrimitiveType, UINT StartVertex, UINT PrimitiveCount);
		HRESULT DrawIndexedPrimitive(GrpPrimitiveType PrimitiveType, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimCount);
		HRESULT DrawIndexedPrimitive(GrpPrimitiveType PrimitiveType, INT BaseVertexIndex, UINT NumVertices, UINT StartIndex, UINT PrimCount);
		HRESULT DrawIndexedPrimitive(D3D11_PRIMITIVE_TOPOLOGY PrimitiveType, INT BaseVertexIndex, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimCount);

	// ========================================================================
	// Utilities
	// ========================================================================
		DWORD GetRenderState(GrpRenderStateType Type); // For debug
		void ApplyState(); // Flush all cached state to DX11 context

		ID3D11DeviceContext* GetContext() { return m_pContext; }
		void* GetDevice() { return reinterpret_cast<void*>(m_pD3DDevice); }
		void ResetFrameDiagnostics();
		const SDebugDrawDiagnostics& GetDebugDrawDiagnostics() const { return m_kDebugDrawDiagnostics; }
	void NoteNoRTVWithPSDraw(bool bIndexedDraw, D3D11_PRIMITIVE_TOPOLOGY eTopology, UINT uElementCount, bool bDepthBound);
	void NoteUnsupportedRenderState(GrpRenderStateType Type, DWORD Value);
	void SyncFogDiagnosticsFromRenderStateCache();

#ifdef _DEBUG
	void ResetDrawCallCounter();
	int GetDrawCallCount() const { return m_iDrawCallCount; }
#endif

private:
	// DX11 Device
	CGraphicDeviceDX11* m_pDevice;
	ID3D11DeviceContext* m_pContext;
	ID3D11Device* m_pD3DDevice;

	// State cache
	StateCache m_CurrentState;
	StateCache m_CurrentState_Copy; // For save/restore

	// Constant buffers
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pTransformCB;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pMaterialCB;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pLightingCB;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pLegacyVSConstantsCB;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pLegacyPSConstantsCB;

	CBTransform m_TransformCBData;
	CBMaterial m_MaterialCBData;
	CBLighting m_LightingCBData;
	std::array<float, STATEMANAGER11_MAX_VCONSTANTS * 4> m_afLegacyVSConstants;
	std::array<float, STATEMANAGER11_MAX_PCONSTANTS * 4> m_afLegacyPSConstants;
	bool m_bLightingEnabled = true;
	bool m_bLightingBufferDirty = false;
	UINT m_uLightingBatchDepth = 0u;
	bool m_bLegacyVSConstantsDirty = false;
	bool m_bLegacyPSConstantsDirty = false;
	DWORD m_dwLegacyVSDirtyStartRegister = 0u; // inclusive register index
	DWORD m_dwLegacyVSDirtyEndRegister = 0u;   // exclusive register index
	DWORD m_dwLegacyPSDirtyStartRegister = 0u; // inclusive register index
	DWORD m_dwLegacyPSDirtyEndRegister = 0u;   // exclusive register index

	// Dynamic vertex buffer for DrawPrimitiveUP

	// Dynamic index buffer for DrawIndexedPrimitiveUP

	// Save/restore stacks
	std::stack<GrpMaterial> m_MaterialStack;
	std::stack<DWORD> m_RenderStateStack[STATEMANAGER11_MAX_RENDERSTATES];
	std::stack<DWORD> m_SamplerStateStack[STATEMANAGER11_MAX_STAGES][STATEMANAGER11_MAX_TEXTURESTATES];
	std::stack<DWORD> m_TextureStageStateStack[STATEMANAGER11_MAX_STAGES][STATEMANAGER11_MAX_TEXTURESTATES];
	std::stack<DWORD> m_FVFStack;
	std::stack<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_TextureStack[STATEMANAGER11_MAX_STAGES];
	std::stack<Microsoft::WRL::ComPtr<ID3D11VertexShader>> m_VertexShaderStack;
	std::stack<Microsoft::WRL::ComPtr<ID3D11PixelShader>> m_PixelShaderStack;
	std::stack<Microsoft::WRL::ComPtr<ID3D11InputLayout>> m_InputLayoutStack;
	std::stack<DirectX::XMFLOAT4X4> m_TransformStack[STATEMANAGER11_MAX_TRANSFORMSTATES];
	std::stack<CStreamData11> m_StreamSourceStack[STATEMANAGER11_MAX_STREAMS];
	std::stack<CIndexData11> m_IndicesStack;

	// Render state cache for GetRenderState
	DWORD m_RenderStateCache[STATEMANAGER11_MAX_RENDERSTATES];
	D3D11_SAMPLER_DESC m_SamplerDescCache[STATEMANAGER11_MAX_STAGES];
	DWORD m_SamplerStateValueCache[STATEMANAGER11_MAX_STAGES][STATEMANAGER11_MAX_TEXTURESTATES];
	bool m_SamplerStateValueValid[STATEMANAGER11_MAX_STAGES][STATEMANAGER11_MAX_TEXTURESTATES];
	DWORD m_TextureStageStateCache[STATEMANAGER11_MAX_STAGES][STATEMANAGER11_MAX_TEXTURESTATES];
	bool m_TextureStageStateValid[STATEMANAGER11_MAX_STAGES][STATEMANAGER11_MAX_TEXTURESTATES];
	DWORD m_dwCurrentFVF = 0u;

	// Scissor rect
	D3D11_RECT m_ScissorRect;

	// Draw call counter
#ifdef _DEBUG
	int m_iDrawCallCount;
#endif
	SDebugDrawDiagnostics m_kDebugDrawDiagnostics;

	// ========================================================================
	// Private Helpers
	// ========================================================================

	// State object creation and caching
	HRESULT CreateBlendState(const D3D11_BLEND_DESC& desc, ID3D11BlendState** ppState);
	HRESULT CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC& desc, ID3D11DepthStencilState** ppState);
	HRESULT CreateRasterizerState(const D3D11_RASTERIZER_DESC& desc, ID3D11RasterizerState** ppState);
	HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC& desc, ID3D11SamplerState** ppState);

	// State application
	void ApplyBlendState();
	void ApplyDepthStencilState();
	void ApplyRasterizerState();
	void ApplyTextureBindings();
	void ApplySamplerBindings();
	void ApplyTopology();
	void ApplyInputLayout();

	// Constant buffer updates
	void UpdateTransformBuffer();
	void UpdateMaterialBuffer();
	void UpdateLightingBuffer();
	void UpdateLightingActiveCount();
	void QueueLightingBufferUpdate();
	void FlushLegacyVSConstantsBuffer();
	void FlushLegacyPSConstantsBuffer();
	bool EnsureDeviceReady(const char* c_szCaller);
	void InitializeSamplerStateCache(DWORD dwStage);
	DWORD GetDefaultSamplerStateValue(GrpSamplerStateType Type) const;

	// Dynamic buffer management

	// FVF to InputLayout conversion (on-demand caching)

	// Render state translation helpers
	D3D11_BLEND BlendToD3D11Blend(GrpBlendType value);
	D3D11_BLEND_OP BlendOpToD3D11BlendOp(GrpBlendOpType value);
	D3D11_COMPARISON_FUNC CompareToD3D11Comparison(GrpCmpFuncType value);
	D3D11_CULL_MODE CullToD3D11Cull(GrpCullType value);
	D3D11_FILL_MODE FillToD3D11Fill(GrpFillModeType value);
	D3D11_STENCIL_OP StencilOpToD3D11StencilOp(GrpStencilOpType value);
	D3D11_FILTER FilterToD3D11Filter(GrpTextureFilterType value);
	D3D11_TEXTURE_ADDRESS_MODE AddressToD3D11Address(GrpTextureAddressType value);
};

// Legacy API compatibility aliases mapped to DX11-neutral method names.
#define SetTextureStageState SetTextureStageCompatState
#define SetFVF SetVertexFormatFlags

#endif // __STATEMANAGER11_H


