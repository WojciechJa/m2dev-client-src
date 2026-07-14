#pragma once

#include "EterLib/GrpScreen.h"

struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11Buffer;

class CSnowParticle;

class CSnowEnvironment : public CScreen
{
	public:
		CSnowEnvironment();
		virtual ~CSnowEnvironment();

		bool Create();
		void Destroy();

		void Enable();
		void Disable();
		bool IsEnabled() const { return m_bSnowEnable; }

		void Update(const DirectX::SimpleMath::Vector3 & c_rv3Pos);
		void Deform();
		void Render();

		// Configuration methods (make snow configurable like rain)
		void SetParticleCount(DWORD dwCount);
		DWORD GetParticleCount() const { return m_dwParticleMaxNum; }

		void SetFallSpeedMin(float fSpeed);
		void SetFallSpeedMax(float fSpeed);
		float GetFallSpeedMin() const { return m_fFallSpeedMin; }
		float GetFallSpeedMax() const { return m_fFallSpeedMax; }

		void SetParticleSize(float fSize);

	protected:		
		void __Initialize();
		bool __CreateBlurTexture();
		bool __CreateGeometry();
		void __BeginBlur();
		void __ApplyBlur();

	protected:
		ID3D11RenderTargetView* m_lpOldSurface;
		ID3D11DepthStencilView* m_lpOldDepthStencilSurface;

		ID3D11Texture2D* m_lpSnowTexture;
		ID3D11RenderTargetView* m_lpSnowRenderTargetSurface;
		ID3D11DepthStencilView* m_lpSnowDepthSurface;

		ID3D11Texture2D* m_lpAccumTexture;
		ID3D11RenderTargetView* m_lpAccumRenderTargetSurface;
		ID3D11DepthStencilView* m_lpAccumDepthSurface;

		ID3D11Buffer* m_pVB;
		ID3D11Buffer* m_pIB;

		DirectX::SimpleMath::Vector3 m_v3Center;

		WORD m_wBlurTextureSize;
		CGraphicImageInstance * m_pImageInstance;
		std::vector<CSnowParticle*> m_kVct_pkParticleSnow;

		DWORD m_dwParticleMaxNum;
		float m_fFallSpeedMin;             // Added: Configurable fall speed
		float m_fFallSpeedMax;             // Added: Configurable fall speed
		float m_fParticleSize;             // Added: Configurable particle size
		BOOL m_bBlurEnable;

		BOOL m_bSnowEnable;
};
