#pragma once

#include "StdAfx.h"
#include "EffectInstance.h"
#include <unordered_map>

// Forward declarations for DX11 types
struct ID3D11Device;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11SamplerState;
struct ID3D11BlendState;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11ShaderResourceView;
class CGraphicImageInstance;

class CEffectManager : public CScreen, public CSingleton<CEffectManager>
{
	public:
		enum EEffectType
		{
			EFFECT_TYPE_NONE				= 0,
			EFFECT_TYPE_PARTICLE			= 1,
			EFFECT_TYPE_ANIMATION_TEXTURE	= 2,
			EFFECT_TYPE_MESH				= 3,
			EFFECT_TYPE_SIMPLE_LIGHT		= 4,

			EFFECT_TYPE_MAX_NUM				= 4,
		};

		typedef std::map<DWORD, CEffectData*> TEffectDataMap;
		typedef std::map<DWORD, CEffectInstance*> TEffectInstanceMap;

	public:
		CEffectManager();
		virtual ~CEffectManager();

		void Destroy();

		void UpdateSound();
		void Update();
		void Render();
		void SetPerformanceSettings(int iProfile, bool bAdaptiveFX, bool bOverBudgetReduced, int iStrideBias = 1);
		DWORD GetActiveEffectCount() const;
		DWORD GetActiveParticleCount() const;

		void GetInfo(std::string* pstInfo);

		bool IsAliveEffect(DWORD dwInstanceIndex);

		// Register
		BOOL RegisterEffect(const char * c_szFileName,bool isExistDelete=false,bool isNeedCache=false);
		BOOL RegisterEffect2(const char * c_szFileName, DWORD* pdwRetCRC, bool isNeedCache=false);

		void DeleteAllInstances();

		// Usage
		int CreateEffect(DWORD dwID, const D3DXVECTOR3 & c_rv3Position, const D3DXVECTOR3 & c_rv3Rotation);
		int CreateEffect(const char * c_szFileName, const D3DXVECTOR3 & c_rv3Position, const D3DXVECTOR3 & c_rv3Rotation);

		void CreateEffectInstance(DWORD dwInstanceIndex, DWORD dwID);
		BOOL SelectEffectInstance(DWORD dwInstanceIndex);
		bool DestroyEffectInstance(DWORD dwInstanceIndex);
		void DeactiveEffectInstance(DWORD dwInstanceIndex);

		void SetEffectTextures(DWORD dwID, std::vector<std::string> textures);
		void SetEffectInstancePosition(const D3DXVECTOR3 & c_rv3Position);
		void SetEffectInstanceRotation(const D3DXVECTOR3 & c_rv3Rotation);
		void SetEffectInstanceGlobalMatrix(const D3DXMATRIX & c_rmatGlobal);

		void ShowEffect();
		void HideEffect();

		void ApplyAlwaysHidden();
		void ReleaseAlwaysHidden();

		// Temporary function
		DWORD GetRandomEffect();
		int GetEmptyIndex();
		bool GetEffectData(DWORD dwID, CEffectData ** ppEffect);
		bool GetEffectData(DWORD dwID, const CEffectData ** c_ppEffect);

		// Area에 직접 찍는 Effect용 함수... EffectInstance의 Pointer를 반환한다.
		// EffectManager 내부 EffectInstanceMap을 이용하지 않는다.
		void CreateUnsafeEffectInstance(DWORD dwEffectDataID, CEffectInstance ** ppEffectInstance);
		bool DestroyUnsafeEffectInstance(CEffectInstance * pEffectInstance);

		int GetRenderingEffectCount();

		// Return the CRC of the effect-data for the selected effect instance.
		DWORD GetSelectedEffectDataCRC() const;

		// DX11 Effect Infrastructure (Batch W2)
		bool InitializeDX11EffectResources(ID3D11Device* pDevice);
		bool EnsureDX11EffectResourcesReady();
		void DestroyDX11EffectResources();
		void ResetDX11SubmittedEffectCount();
		void BeginDX11WorldFrameTelemetry();
		bool IsDX11EffectResourcesReady() const { return m_bDX11EffectResourcesReady; }
		ID3D11VertexShader* GetDX11EffectVertexShader() const { return m_pDX11EffectVertexShader; }
		ID3D11PixelShader* GetDX11EffectPixelShader() const { return m_pDX11EffectPixelShader; }
		ID3D11InputLayout* GetDX11EffectInputLayout() const { return m_pDX11EffectInputLayout; }
		ID3D11Buffer* GetDX11EffectConstantBuffer() const { return m_pDX11EffectConstantBuffer; }
		ID3D11SamplerState* GetDX11EffectSamplerState() const { return m_pDX11EffectSamplerState; }
		ID3D11Buffer* GetDX11EffectDynamicVB() const { return m_pDX11EffectDynamicVB; }
		ID3D11RasterizerState* GetDX11EffectNoCullRasterizerState() const { return m_pDX11EffectNoCullRasterizerState; }
		ID3D11DepthStencilState* GetDX11EffectDepthReadOnlyState() const { return m_pDX11EffectDepthReadOnlyState; }
		bool EnsureDX11EffectDynamicVB(uint32_t uMinVertexCount);
		uint32_t GetDX11EffectDynamicVBCapacity() const { return m_uDX11EffectDynamicVBCapacity; }
		ID3D11ShaderResourceView* GetEffectTextureSRV(CGraphicImageInstance* pImageInstance);
		void AddDX11SubmittedEffectCount(uint32_t dwCount);
		void AddDX11SubmittedParticleCount(uint32_t dwCount);
		void AddDX11SubmittedMeshEffectCount(uint32_t dwCount);
		uint32_t GetDX11SubmittedEffectCount() const;
		uint32_t GetDX11SubmittedParticleCount() const { return m_dwDX11SubmittedParticleCount; }
		uint32_t GetDX11SubmittedMeshEffectCount() const { return m_dwDX11SubmittedMeshEffectCount; }

		// DX11 Blend State Accessors
		ID3D11BlendState* GetDX11EffectBlendStateAdditive() const { return m_pDX11EffectBlendStateAdditive; }
		ID3D11BlendState* GetDX11EffectBlendStateAlpha() const { return m_pDX11EffectBlendStateAlpha; }
		ID3D11BlendState* GetDX11EffectBlendStateScreen() const { return m_pDX11EffectBlendStateScreen; }
		ID3D11BlendState* ResolveDX11EffectBlendState(bool bBlendingEnable, BYTE bySrcBlend, BYTE byDestBlend);

	protected:
		bool IsHighPriorityFX(const CEffectInstance* pEffectInstance) const;
		static std::string NormalizeEffectPath(const char* cszFile);
		uint32_t LegacyBlendToDX11Blend(BYTE byBlendType) const;
		void NormalizeLegacyBlendPair(BYTE& bySrcBlend, BYTE& byDestBlend) const;
		ID3D11BlendState* GetOrCreateDX11EffectBlendState(BYTE bySrcBlend, BYTE byDestBlend);
		bool CreateDX11EffectDynamicVB(ID3D11Device* pDevice, uint32_t uVertexCapacity);
		void DestroyDX11EffectBlendCache();
		uint32_t MakeBlendCacheKey(BYTE bySrcBlend, BYTE byDestBlend) const;

		void __Initialize();

		void __DestroyEffectInstanceMap();
		void __DestroyEffectCacheMap();
		void __DestroyEffectDataMap();

	protected:
		bool m_isDisableSortRendering;
		int m_iPerfProfile;
		bool m_bAdaptiveFX;
		bool m_bOverBudgetReduced;
		int m_iFXStrideBias;
		DWORD m_dwUpdateFrame;
		DWORD m_dwRenderFrame;
		DWORD m_dwSortInterval;
		bool m_bSortDirty;
		std::vector<CEffectInstance*> m_kVctSortedCache;
		TEffectDataMap					m_kEftDataMap;
		TEffectInstanceMap				m_kEftInstMap;
		TEffectInstanceMap				m_kEftCacheMap;

		CEffectInstance *				m_pSelectedEffectInstance;

		// DX11 Effect Resources (Batch W2)
		ID3D11VertexShader*				m_pDX11EffectVertexShader;
		ID3D11PixelShader*				m_pDX11EffectPixelShader;
		ID3D11InputLayout*				m_pDX11EffectInputLayout;
		ID3D11Buffer*					m_pDX11EffectConstantBuffer;
		ID3D11SamplerState*				m_pDX11EffectSamplerState;
		ID3D11BlendState*				m_pDX11EffectBlendStateAdditive;
		ID3D11BlendState*				m_pDX11EffectBlendStateAlpha;
		ID3D11BlendState*				m_pDX11EffectBlendStateScreen;
		ID3D11RasterizerState*			m_pDX11EffectNoCullRasterizerState;
		ID3D11DepthStencilState*		m_pDX11EffectDepthReadOnlyState;
		std::unordered_map<uint32_t, ID3D11BlendState*> m_kDX11EffectBlendStateCache;
		ID3D11ShaderResourceView*		m_pDX11EffectDefaultTextureSRV;
		ID3D11Buffer*					m_pDX11EffectDynamicVB;
		uint32_t						m_uDX11EffectDynamicVBCapacity;
		bool							m_bDX11EffectResourcesReady;

		// DX11 Telemetry (W4.2)
		DWORD							m_dwDX11SubmittedEffectCount;
		DWORD							m_dwDX11SubmittedParticleCount;
		DWORD							m_dwDX11SubmittedMeshEffectCount;
};
