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

		const D3DXMATRIX& GetLightVPMatrixReference() const;
		ID3D11ShaderResourceView* GetD3DTexture() const;

	protected:
		void Initialize();
		
	protected:
		D3DXMATRIX			m_d3dLightVPMatrix;
		D3DVIEWPORT9		m_d3dOldViewport;
		
		ID3D11ShaderResourceView*	m_lpd3dShadowTexture;
};
