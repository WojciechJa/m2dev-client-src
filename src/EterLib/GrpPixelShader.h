#pragma once

#include "GrpBase.h"
#include <d3d11.h>

class CPixelShader : public CGraphicBase
{
	public:
		CPixelShader();
		virtual ~CPixelShader();

		void Destroy();
		bool CreateFromDiskFile(const char* c_szFileName);

		void Set();

	protected:
		void Initialize();

	protected:
		ID3D11PixelShader* m_handle;
};
