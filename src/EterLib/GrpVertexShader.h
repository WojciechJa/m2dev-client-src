#pragma once

#include "GrpBase.h"
#include <d3d11.h>

class CVertexShader : public CGraphicBase
{
	public:
		CVertexShader();
		virtual ~CVertexShader();

		void Destroy();
		bool CreateFromDiskFile(const char* c_szFileName, const DWORD* c_pdwVertexDecl);

		void Set();

	protected:
		void Initialize();

	protected:
		ID3D11VertexShader* m_handle;
};
