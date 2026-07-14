#include "StdAfx.h"
#include "EterBase/Stl.h"
#include "GrpVertexShader.h"
#include "GrpDeviceDX11.h"
#include "StateManager.h"

#include <utf8.h>
#include <d3dcompiler.h>

CVertexShader::CVertexShader()
{
	Initialize();
}

CVertexShader::~CVertexShader()
{
	Destroy();
}

void CVertexShader::Initialize()
{
	m_handle = nullptr;
}

void CVertexShader::Destroy()
{
	safe_release(m_handle);
}

bool CVertexShader::CreateFromDiskFile(const char* c_szFileName, const DWORD* c_pdwVertexDecl)
{
	Destroy();
	(void)c_pdwVertexDecl;

	if (!c_szFileName || !*c_szFileName)
		return false;

	std::wstring wFileName = Utf8ToWide(c_szFileName);

	ID3DBlob* pShaderBlob = nullptr;
	ID3DBlob* pErrorBlob = nullptr;

	HRESULT hr = D3DCompileFromFile(
		wFileName.c_str(),
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"main",
		"vs_4_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pShaderBlob,
		&pErrorBlob);

	if (FAILED(hr) || !pShaderBlob)
	{
		if (pErrorBlob)
		{
			const char* err = static_cast<const char*>(pErrorBlob->GetBufferPointer());
			TraceError("Vertex shader compile error: %s", err);
		}
		safe_release(pErrorBlob);
		safe_release(pShaderBlob);
		return false;
	}

	CGraphicDeviceDX11* pDevice = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDevice || !pDevice->IsValid() || !pDevice->GetDevice())
	{
		safe_release(pErrorBlob);
		safe_release(pShaderBlob);
		return false;
	}

	hr = pDevice->GetDevice()->CreateVertexShader(
		pShaderBlob->GetBufferPointer(),
		pShaderBlob->GetBufferSize(),
		nullptr,
		&m_handle);

	safe_release(pErrorBlob);
	safe_release(pShaderBlob);

	return SUCCEEDED(hr) && (m_handle != nullptr);
}

void CVertexShader::Set()
{
	if (!m_handle)
		return;

	STATEMANAGER.SetVertexShader(m_handle);
}
