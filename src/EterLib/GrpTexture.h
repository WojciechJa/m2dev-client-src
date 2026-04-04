#pragma once

#include "GrpBase.h"

struct ID3D11ShaderResourceView;

class CGraphicTexture : public CGraphicBase
{
	public:
		virtual bool IsEmpty() const;

		int GetWidth() const;
		int GetHeight() const;

		void SetTextureStage(int stage) const;
		ID3D11ShaderResourceView* GetD3DTexture() const;
		virtual ID3D11ShaderResourceView* GetD3D11TextureSRV() const { return nullptr; }

		// M2-GRPBASE-TYPE-CUT-73: DX11-native API (replaces legacy GetD3DTexture)
		virtual ID3D11Buffer* GetD3D11Buffer() const { return nullptr; }

		void DestroyDeviceObjects();
		
	protected:
		CGraphicTexture();
		virtual	~CGraphicTexture();

		void Destroy();
		void Initialize();

	protected:
		bool m_bEmpty;

		int m_width;
		int m_height;

		ID3D11ShaderResourceView* m_lpd3dTexture;
};
