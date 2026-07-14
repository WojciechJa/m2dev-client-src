#pragma once

#include "EterLib/GrpScreen.h"
#include <DirectXMath.h>

struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11Buffer;

class CRainParticle;

class CRainEnvironment : public CScreen
{
	public:
		CRainEnvironment();
		virtual ~CRainEnvironment();

		// Lifecycle
		bool Create();
		void Destroy();

		// Control
		void Enable();
		void Disable();
		bool IsEnabled() const { return m_bRainEnable; }

		// Update/Render
		void Update(const DirectX::SimpleMath::Vector3 & c_rv3Pos);
		void Deform();
		void Render();

		// Configuration methods (unlike snow - make configurable!)
		void SetParticleCount(DWORD dwCount);
		DWORD GetParticleCount() const { return m_dwParticleMaxNum; }

		void SetFallSpeedMin(float fSpeed) { m_fFallSpeedMin = fSpeed; }
		float GetFallSpeedMin() const { return m_fFallSpeedMin; }

		void SetFallSpeedMax(float fSpeed) { m_fFallSpeedMax = fSpeed; }
		float GetFallSpeedMax() const { return m_fFallSpeedMax; }

		void SetWindInfluence(float fInfluence) { m_fWindInfluence = fInfluence; }
		float GetWindInfluence() const { return m_fWindInfluence; }

		void SetSpawnRate(int iParticlesPerFrame) { m_iSpawnRate = iParticlesPerFrame; }
		int GetSpawnRate() const { return m_iSpawnRate; }

		void SetParticleSize(float fWidth, float fHeight);
		float GetParticleWidth() const { return m_fParticleWidth; }
		float GetParticleHeight() const { return m_fParticleHeight; }

		void SetSpawnRadius(float fRadius) { m_fSpawnRadius = fRadius; }
		float GetSpawnRadius() const { return m_fSpawnRadius; }

		// Wind vector (can be set from environment data)
		void SetWindVector(const DirectX::SimpleMath::Vector3 & v3Wind) { m_v3Wind = v3Wind; }
		const DirectX::SimpleMath::Vector3 & GetWindVector() const { return m_v3Wind; }

	protected:
		void __Initialize();
		bool __CreateBlurTexture();
		bool __CreateGeometry();
		void __BeginBlur();
		void __ApplyBlur();

	protected:
		// DX11 legacy blur system (disabled in DX11, kept for compatibility)
		ID3D11RenderTargetView* m_lpOldSurface;
		ID3D11DepthStencilView* m_lpOldDepthStencilSurface;
		ID3D11Texture2D* m_lpRainTexture;
		ID3D11RenderTargetView* m_lpRainRenderTargetSurface;
		ID3D11DepthStencilView* m_lpRainDepthSurface;
		ID3D11Texture2D* m_lpAccumTexture;
		ID3D11RenderTargetView* m_lpAccumRenderTargetSurface;
		ID3D11DepthStencilView* m_lpAccumDepthSurface;
		ID3D11Buffer* m_pVB;
		ID3D11Buffer* m_pIB;

		// Core data
		DirectX::SimpleMath::Vector3 m_v3Center;
		WORD m_wBlurTextureSize;
		CGraphicImageInstance * m_pImageInstance;
		std::vector<CRainParticle*> m_kVct_pkParticleRain;

		// Configuration (configurable, unlike snow!)
		DWORD m_dwParticleMaxNum;          // Default: 8000 (vs 3000 for snow)
		float m_fFallSpeedMin;             // Default: 500.0f (vs 50.0f for snow)
		float m_fFallSpeedMax;             // Default: 1500.0f (vs 200.0f for snow)
		float m_fWindInfluence;            // Default: 1.0f (no wind for snow)
		int m_iSpawnRate;                  // Default: 25 (vs 10 for snow)
		float m_fParticleWidth;            // Default: 2.0f (vs 4.0f for snow)
		float m_fParticleHeight;           // Default: 15.0f (stretched, vs square for snow)
		float m_fSpawnRadius;              // Default: 70000.0f (same as snow)
		DirectX::SimpleMath::Vector3 m_v3Wind;              // Wind vector (0,0,0 by default)

		// State flags
		BOOL m_bBlurEnable;                // Disabled in DX11
		BOOL m_bRainEnable;                // Rain enable flag
};
