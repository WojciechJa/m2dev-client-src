#pragma once

#include "GrpBase.h"

struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;

class CGraphicDib;

class CBlockTexture : public CGraphicBase
{
	public:
		CBlockTexture();
		virtual ~CBlockTexture();

		bool Create(CGraphicDib * pDIB, const RECT & c_rRect, DWORD dwWidth, DWORD dwHeight);
		void SetClipRect(const RECT & c_rRect);
		void Render(int ix, int iy);
		void InvalidateRect(const RECT & c_rsrcRect);

	protected:
		CGraphicDib * m_pDIB;
		RECT m_rect;
		RECT m_clipRect;
		BOOL m_bClipEnable;
		DWORD m_dwWidth;
		DWORD m_dwHeight;
		ID3D11Texture2D* m_pDX11Texture; // M2-ETERLIB-NATIVE-58: Native DX11 texture
		ID3D11ShaderResourceView* m_pDX11TextureSRV; // M2-ETERLIB-NATIVE-58: DX11 shader resource view
};
