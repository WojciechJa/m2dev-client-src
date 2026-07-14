#pragma once

#include "GrpTexture.h"
#include <vector>

struct TDecodedImageData;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;

class CGraphicImageTexture : public CGraphicTexture
{
	public:
		CGraphicImageTexture();
		virtual ~CGraphicImageTexture();

		void		Destroy();

		bool		Create(UINT width, UINT height, GrpFormatType formatType, DWORD dwFilter = D3DX_FILTER_LINEAR);
		bool		CreateDeviceObjects();
		
		void		CreateFromTexturePointer(const CGraphicTexture* c_pSrcTexture);
		bool		CreateFromDiskFile(const char* c_szFileName, GrpFormatType formatType, DWORD dwFilter = D3DX_FILTER_LINEAR);
		bool		CreateFromMemoryFile(UINT bufSize, const void* c_pvBuf, GrpFormatType formatType, DWORD dwFilter = D3DX_FILTER_LINEAR);
		bool		CreateFromDDSTexture(UINT bufSize, const void* c_pvBuf);
		bool		CreateFromTGA(UINT bufSize, const void* c_pvBuf);
		bool		CreateFromSTB(UINT bufSize, const void* c_pvBuf);
		bool		CreateFromSOIL2(UINT bufSize, const void* c_pvBuf);
		bool		CreateFromDecodedData(const TDecodedImageData& decodedImage, GrpFormatType formatType, DWORD dwFilter);

		void		SetFileName(const char * c_szFileName);
		
		bool		Lock(int* pRetPitch, void** ppRetPixels, int level=0);
		void		Unlock(int level=0);
		ID3D11ShaderResourceView* GetD3D11TextureSRV() const override { return m_pDX11TextureSRV; }

	protected:
		void		Initialize();
		void		DestroyDX11Texture();
		bool		CreateDX11DynamicTexture(UINT width, UINT height);
		bool		BindDX11LoadedTexture(ID3D11ShaderResourceView* pSRV);
		
		GrpFormatType	m_formatType;
		DWORD		m_dwFilter;

		std::string m_stFileName;

		ID3D11Texture2D* m_pDX11Texture;
		ID3D11ShaderResourceView* m_pDX11TextureSRV;
		std::vector<uint8_t> m_kDX11LockedPixels;
		UINT m_uDX11LockPitch;
		bool m_bDX11LockActive;
		bool m_bDX11DynamicTexture;
};
