#include "StdAfx.h"
#include "EterBase/Stl.h"
#include "GrpPixelShader.h"
#include "GrpDeviceDX11.h"
#include "StateManager.h"

#include <utf8.h>
#include <d3dcompiler.h>

CPixelShader::CPixelShader()
{
	Initialize();
}

CPixelShader::~CPixelShader()
{
	Destroy();
}

void CPixelShader::Initialize()
{
	m_handle = nullptr;
}

void CPixelShader::Destroy()
{
	safe_release(m_handle);
}

bool CPixelShader::CreateFromDiskFile(const char* c_szFileName)
{
	Destroy();

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
		"ps_4_0",
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		&pShaderBlob,
		&pErrorBlob);

	if (FAILED(hr) || !pShaderBlob)
	{
		if (pErrorBlob)
		{
			const char* err = static_cast<const char*>(pErrorBlob->GetBufferPointer());
			TraceError("Pixel shader compile error: %s", err);
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

	hr = pDevice->GetDevice()->CreatePixelShader(
		pShaderBlob->GetBufferPointer(),
		pShaderBlob->GetBufferSize(),
		nullptr,
		&m_handle);

	safe_release(pErrorBlob);
	safe_release(pShaderBlob);

	return SUCCEEDED(hr) && (m_handle != nullptr);
}

void CPixelShader::Set()
{
	if (!m_handle)
		return;

	STATEMANAGER.SetPixelShader(m_handle);
}
