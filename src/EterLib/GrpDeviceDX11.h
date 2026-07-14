#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>

class CGraphicDeviceDX11
{
public:
	enum ENativeWorldPortMask : uint32_t
	{
		WORLD_TERRAIN_DX11 = (1u << 0),
		WORLD_OBJECTS_DX11 = (1u << 1),
		WORLD_EFFECTS_DX11 = (1u << 2),
		WORLD_SPEEDTREE_DX11 = (1u << 3),
		WORLD_WATER_DX11 = (1u << 4),
	};
	enum EDX11TexturePipelineMode : uint32_t
	{
		DX11_TEXTURE_PIPELINE_NATIVE = 0u,
		DX11_TEXTURE_PIPELINE_HYBRID = 1u,
		DX11_TEXTURE_PIPELINE_LEGACY = 2u,
	};
	static constexpr uint32_t WORLD_PORT_REQUIRED_MASK =
		WORLD_TERRAIN_DX11 |
		WORLD_OBJECTS_DX11 |
		WORLD_EFFECTS_DX11 |
		WORLD_SPEEDTREE_DX11 |
		WORLD_WATER_DX11;

	CGraphicDeviceDX11();
	~CGraphicDeviceDX11();

	bool Create(HWND hWnd, UINT uWidth, UINT uHeight, bool isWindowed, bool isVSyncEnabled);
	void Destroy();
	bool Resize(UINT uWidth, UINT uHeight);
	bool BeginFrame(float fR, float fG, float fB, float fA);
	bool DrawBootstrapTriangle();
	bool DrawBootstrapUIOverlay(float fCursorX, float fCursorY);
	bool DrawBootstrapUITextureOverlay(float fCursorX, float fCursorY);
	bool DrawBootstrapWorldDepthTest(float fTimeSec);
	bool DrawBootstrapWorldBatchTest(float fTimeSec, int iInstanceCount);
	bool DrawBootstrapWorldSpriteTest(float fTimeSec, int iInstanceCount);
	bool DrawBootstrapWorldStateTest(float fTimeSec, int iInstanceCount);
	bool DrawBootstrapWorldPassesTest(float fTimeSec, int iTerrainTiles, int iActorCount, int iFXCount);
	bool DrawNativeWorldRenderPasses(float fTimeSec, int iTerrainTiles, int iActorCount, int iFXCount);
	bool DrawNativeWorldMinimalDryRun(float fTimeSec, int iTerrainTiles, int iActorCount, int iFXCount);
	bool DrawNativeWorldTerrainPilot(float fTimeSec, int iTerrainTiles);
	bool DrawNativeWorldShadowPasses(float fTimeSec, int iTerrainTiles, int iActorCount, int iFXCount);
	bool TickNativeWorldRuntime(const char* c_szStage, int iTerrainTiles, int iActorCount, int iFXCount);
	bool PresentNativeWorld(bool bDrawNativeCursorOverlay, float fCursorX, float fCursorY);
	bool PresentNativeWorldDryRun(bool bDrawNativeCursorOverlay, float fCursorX, float fCursorY);
	bool PresentVisibleBridgeTexture(bool bDrawNativeCursorOverlay, float fCursorX, float fCursorY);
	void BindMainRenderTargets();
	bool Present();
	bool PresentTest();
	bool SetVSyncEnabled(bool isEnabled);
	void SetNativeWorldSceneStats(
		int iPatchCount,
		int iSplatCount,
		float fSplatRatio,
		int iTextureCount,
		DWORD dwThingInstances,
		DWORD dwCRCCount);
	void SetNativeWorldObservedMask(uint32_t dwMask);
	void SetNativeWorldSubmittedMask(uint32_t dwMask);
	void SetNativeWorldApplicableMask(uint32_t dwMask);
	void SetNativeWorldSubmittedSeenMask(uint32_t dwMask);
	void SetNativeWorldCommittedMask(uint32_t dwMask);
	uint32_t GetNativeWorldObservedMask() const;
	uint32_t GetNativeWorldSubmittedMask() const;
	uint32_t GetNativeWorldApplicableMask() const;
	uint32_t GetNativeWorldSubmittedSeenMask() const;
	uint32_t GetNativeWorldCommittedMask() const;
	void SetNativeWorldPortMask(uint32_t dwMask);
	uint32_t GetNativeWorldPortMask() const;
	uint32_t GetNativeWorldMissingPortMask() const;
	uint32_t GetNativeWorldRequiredEffectiveMask() const;
	bool IsUsingNativeWorldPresentPath() const { return m_bUsingNativeWorldPresentPath; }
	bool IsNativeWorldTerrainPrototypeEnabled() const;
	bool IsNativeWorldRendererPorted() const;
	void SetDX11TexturePipelineMode(EDX11TexturePipelineMode eMode);
	EDX11TexturePipelineMode GetDX11TexturePipelineMode() const;
	bool EnsureBootstrapPipelineReady();
	bool EnsureBootstrapUISamplerReady();
	void SetBootstrapTextureStageSRV(UINT uStage, ID3D11ShaderResourceView* pSRV);
	ID3D11ShaderResourceView* GetBootstrapTextureStageSRV(UINT uStage) const;

	bool IsValid() const { return m_pDevice && m_pDeviceContext && m_pSwapChain; }
	bool IsVSyncEnabled() const { return m_isVSyncEnabled; }
	D3D_FEATURE_LEVEL GetFeatureLevel() const { return m_eFeatureLevel; }
	void IncrementFrameDrawCalls(UINT uDrawCalls = 1u, UINT uPrimitiveCount = 0u);
	UINT GetFrameDrawCalls() const { return m_uFrameDrawCalls; }
	UINT GetFramePrimitiveCount() const { return m_uFramePrimitiveCount; }

	static CGraphicDeviceDX11* GetActiveDevice() { return ms_pActiveDevice; }

	ID3D11Device* GetDevice() const { return m_pDevice; }
	ID3D11DeviceContext* GetContext() const { return m_pDeviceContext; }
	IDXGISwapChain* GetSwapChain() const { return m_pSwapChain; }
	UINT GetWidth() const { return m_uWidth; }
	UINT GetHeight() const { return m_uHeight; }
	ID3D11InputLayout* GetBootstrapUIInputLayout() const { return m_pBootstrapInputLayout; }
	ID3D11VertexShader* GetBootstrapUIVertexShader() const { return m_pBootstrapVertexShader; }
	ID3D11PixelShader* GetBootstrapColorPixelShader() const { return m_pBootstrapPixelShader; }
	ID3D11PixelShader* GetBootstrapUIPixelShader() const { return m_pBootstrapUIPixelShader; }
	ID3D11PixelShader* GetBootstrapUITexturePixelShader() const { return m_pBootstrapUIPixelShader; }
	ID3D11Buffer* GetBootstrapUIVertexBuffer() const { return m_pBootstrapVertexBuffer; }
	ID3D11BlendState* GetBootstrapUIAlphaBlendState() const { return m_pBootstrapAlphaBlendState; }
	ID3D11BlendState* GetBootstrapUICloudBlendState() const { return m_pBootstrapUICloudBlendState; }
	ID3D11BlendState* GetBootstrapUIScreenBlendState() const { return m_pBootstrapUIScreenBlendState; }
	ID3D11BlendState* GetBootstrapUIModulateBlendState() const { return m_pBootstrapUIModulateBlendState; }
	ID3D11BlendState* GetBootstrapUIAdditiveBlendState() const { return m_pBootstrapUIAdditiveBlendState; }
	ID3D11BlendState* GetBootstrapUILCDPass1BlendState() const { return m_pBootstrapLCDPass1BlendState; }
	ID3D11BlendState* GetBootstrapUILCDPass2BlendState() const { return m_pBootstrapLCDPass2BlendState; }
	ID3D11DepthStencilState* GetBootstrapUIDepthReadState() const { return m_pBootstrapDepthReadState; }
	ID3D11DepthStencilState* GetBootstrapUIDepthDisableState() const { return m_pBootstrapDepthDisableState; }
	ID3D11RasterizerState* GetBootstrapRasterizerState() const { return m_pBootstrapRasterizerState; }
	ID3D11SamplerState* GetBootstrapUISamplerState() const { return m_pBootstrapUISamplerState; }
	ID3D11RenderTargetView* GetMainRenderTargetView() const { return m_pRenderTargetView; }

	// M2-ETERLIB-STATE-48: Explicit state baseline helpers for UI/text rendering
	void SetUI2DBaselineState();
	void SetUITextBaselineState();

private:
	bool __ValidateNativeWorldRuntimePass();
	bool __RunNativeWorldTerrainPrototype(int iTerrainTiles, int iActorCount, int iFXCount, bool bIncludeActorsAndFX);
	bool __CreateRenderTarget();
	void __DestroyRenderTarget();
	bool __CreateDepthStencil();
	void __DestroyDepthStencil();
	bool __CreateBootstrapPipeline();
	void __DestroyBootstrapPipeline();
	bool __CreateBootstrapUITexture();
	void __DestroyBootstrapUITexture();
	bool __DrawNativeCursorOverlay(float fCursorX, float fCursorY);
	bool __EnsureVisibleBridgeTexture(UINT uWidth, UINT uHeight);
	void __DestroyVisibleBridgeTexture();

	static CGraphicDeviceDX11* ms_pActiveDevice;

	HWND m_hWnd;
	UINT m_uWidth;
	UINT m_uHeight;
	bool m_isWindowed;
	bool m_isVSyncEnabled;

	IDXGISwapChain* m_pSwapChain;
	ID3D11Device* m_pDevice;
	ID3D11DeviceContext* m_pDeviceContext;
	ID3D11RenderTargetView* m_pRenderTargetView;
	ID3D11Texture2D* m_pDepthStencilBuffer;
	ID3D11DepthStencilView* m_pDepthStencilView;
	D3D11_VIEWPORT m_kViewport;
	ID3D11VertexShader* m_pBootstrapVertexShader;
	ID3D11PixelShader* m_pBootstrapPixelShader;
	ID3D11PixelShader* m_pBootstrapUIPixelShader;
	ID3D11InputLayout* m_pBootstrapInputLayout;
	ID3D11Buffer* m_pBootstrapVertexBuffer;
	ID3D11BlendState* m_pBootstrapAlphaBlendState;
	ID3D11BlendState* m_pBootstrapUICloudBlendState;
	ID3D11BlendState* m_pBootstrapUIScreenBlendState;
	ID3D11BlendState* m_pBootstrapUIModulateBlendState;
	ID3D11BlendState* m_pBootstrapUIAdditiveBlendState;
	ID3D11BlendState* m_pBootstrapAdditiveBlendState;
	ID3D11BlendState* m_pBootstrapLCDPass1BlendState;
	ID3D11BlendState* m_pBootstrapLCDPass2BlendState;
	ID3D11DepthStencilState* m_pBootstrapDepthEnableState;
	ID3D11DepthStencilState* m_pBootstrapDepthReadState;
	ID3D11DepthStencilState* m_pBootstrapDepthDisableState;
	ID3D11RasterizerState* m_pBootstrapRasterizerState;
	ID3D11Texture2D* m_pBootstrapUITexture;
	ID3D11ShaderResourceView* m_pBootstrapUITextureSRV;
	ID3D11Texture2D* m_pVisibleBridgeTexture;
	ID3D11ShaderResourceView* m_pVisibleBridgeTextureSRV;
	ID3D11SamplerState* m_pBootstrapUISamplerState;
	ID3D11ShaderResourceView* m_apBootstrapTextureStageSRV[8];
	bool m_isBootstrapPipelineReady;
	bool m_isBootstrapUITextureReady;
	UINT m_uVisibleBridgeWidth;
	UINT m_uVisibleBridgeHeight;
	int m_iNativeWorldScenePatchCount;
	int m_iNativeWorldSceneSplatCount;
	float m_fNativeWorldSceneSplatRatio;
	int m_iNativeWorldSceneTextureCount;
	DWORD m_dwNativeWorldSceneThingInstances;
	DWORD m_dwNativeWorldSceneCRCCount;
	uint32_t m_dwNativeWorldObservedMask;
	uint32_t m_dwNativeWorldSubmittedMask;
	uint32_t m_dwNativeWorldApplicableMask;
	uint32_t m_dwNativeWorldSubmittedSeenMask;
	uint32_t m_dwNativeWorldCommittedMask;
	bool m_bUsingNativeWorldPresentPath;
	EDX11TexturePipelineMode m_eDX11TexturePipelineMode;
	D3D_FEATURE_LEVEL m_eFeatureLevel;
	// DX11 Model Sync: Simple frame counter for metrics
	UINT m_uFrameDrawCalls;  // Draw calls per frame
	UINT m_uFramePrimitiveCount;  // Primitive count per frame
};
