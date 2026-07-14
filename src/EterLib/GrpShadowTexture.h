#pragma once

#include "GrpTexture.h"

class CGraphicShadowTexture : public CGraphicTexture
{
	public:
		CGraphicShadowTexture();
		virtual ~CGraphicShadowTexture();
		
		void Destroy();
		
		bool Create(int width, int height);

		void Begin();
		void End();
		void Set(int stage = 0) const;

		const DirectX::SimpleMath::Matrix& GetLightVPMatrixReference() const;
		ID3D11ShaderResourceView* GetD3DTexture() const;

	protected:
		void Initialize();
		
	protected:
		DirectX::SimpleMath::Matrix	m_lightViewProjMatrix;
		GrpViewport			m_oldViewport;
		
		ID3D11ShaderResourceView*	m_lpd3dShadowTexture;
};
