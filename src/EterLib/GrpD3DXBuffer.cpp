#include "StdAfx.h"
#include "GrpD3DXBuffer.h"
#include "EterBase/Stl.h"

CDirect3DXBuffer::CDirect3DXBuffer()
{
	m_pBlob = nullptr;
}

CDirect3DXBuffer::CDirect3DXBuffer(ID3DBlob* pBlob)
{
	m_pBlob = pBlob;
}

CDirect3DXBuffer::~CDirect3DXBuffer()
{
	Destroy();
}

void CDirect3DXBuffer::Destroy()
{
	safe_release(m_pBlob);
}

void CDirect3DXBuffer::Create(ID3DBlob* pBlob)
{
	Destroy();
	m_pBlob = pBlob;
}

void*CDirect3DXBuffer::GetPointer()
{
	assert(m_pBlob != NULL);
	return m_pBlob->GetBufferPointer();
}

int  CDirect3DXBuffer::GetSize()
{
	assert(m_pBlob != NULL);
	return static_cast<int>(m_pBlob->GetBufferSize());
}
