#pragma once

#include <d3dcompiler.h>

class CDirect3DXBuffer
{
	public:
		CDirect3DXBuffer();
		CDirect3DXBuffer(ID3DBlob* pBlob);
		virtual ~CDirect3DXBuffer();

		void Destroy();
		void Create(ID3DBlob* pBlob);

		void*GetPointer();
		int  GetSize();

	protected:
		ID3DBlob* m_pBlob;
};
