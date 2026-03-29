#include "StdAfx.h"
#include "GrpDevice.h"
#include "GrpDeviceDX11.h"

#include <memory>

// DX11 runtime capability globals kept as non-const symbols for legacy extern users.
bool GRAPHICS_CAPS_CAN_NOT_DRAW_SHADOW = false;
bool GRAPHICS_CAPS_CAN_NOT_DRAW_LINE = false;
bool GRAPHICS_CAPS_SOFTWARE_TILING = false;
#if defined(_M_X64) || (_M_IX86_FP >= 2)
bool CPU_HAS_SSE2 = true;
#else
bool CPU_HAS_SSE2 = false;
#endif

namespace
{
	std::unique_ptr<CGraphicDeviceDX11> gs_pGraphicDeviceDX11;

	CGraphicDeviceDX11* EnsureDX11Device()
	{
		if (!gs_pGraphicDeviceDX11)
			gs_pGraphicDeviceDX11 = std::make_unique<CGraphicDeviceDX11>();
		return gs_pGraphicDeviceDX11.get();
	}
}

CGraphicDevice::CGraphicDevice()
{
	__Initialize();
}

CGraphicDevice::~CGraphicDevice()
{
	Destroy();
}

void CGraphicDevice::__Initialize()
{
	m_uBackBufferCount = 2u;
	m_isVSyncEnabled = false;
	m_kMap_strWarningMessage.clear();
	m_pStateManager = CStateManager::InstancePtr();
}

void CGraphicDevice::RegisterWarningString(UINT uiMsg, const char* c_szString)
{
	m_kMap_strWarningMessage[uiMsg] = c_szString ? c_szString : "";
}

void CGraphicDevice::__WarningMessage(HWND hWnd, UINT uiMsg)
{
	auto it = m_kMap_strWarningMessage.find(uiMsg);
	if (it == m_kMap_strWarningMessage.end())
		return;

	MessageBoxA(hWnd, it->second.c_str(), "Graphic Device Warning", MB_ICONWARNING | MB_OK);
}

void CGraphicDevice::__UpdatePresentationInterval(D3DPRESENT_PARAMETERS& rkD3DPP)
{
	rkD3DPP.PresentationInterval = m_isVSyncEnabled ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
}

void CGraphicDevice::MoveWebBrowserRect(const RECT&)
{
}

void CGraphicDevice::EnableWebBrowserMode(const RECT&)
{
}

void CGraphicDevice::DisableWebBrowserMode()
{
}

bool CGraphicDevice::ResizeBackBuffer(UINT uWidth, UINT uHeight)
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return false;

	const bool bOk = pDX11Device->Resize(uWidth, uHeight);
	if (bOk)
	{
		ms_iWidth = static_cast<int>(uWidth);
		ms_iHeight = static_cast<int>(uHeight);
	}
	return bOk;
}

LPDIRECT3DVERTEXDECLARATION9 CGraphicDevice::CreatePTStreamVertexShader()
{
	return nullptr;
}

LPDIRECT3DVERTEXDECLARATION9 CGraphicDevice::CreatePNTStreamVertexShader()
{
	return nullptr;
}

LPDIRECT3DVERTEXDECLARATION9 CGraphicDevice::CreatePNT2StreamVertexShader()
{
	return nullptr;
}

LPDIRECT3DVERTEXDECLARATION9 CGraphicDevice::CreateDoublePNTStreamVertexShader()
{
	return nullptr;
}

CGraphicDevice::EDeviceState CGraphicDevice::GetDeviceState()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device)
		return DEVICESTATE_NULL;
	if (!pDX11Device->IsValid())
		return DEVICESTATE_BROKEN;
	return pDX11Device->PresentTest() ? DEVICESTATE_OK : DEVICESTATE_NEEDS_RESET;
}

bool CGraphicDevice::Reset()
{
	if (ms_iWidth <= 0 || ms_iHeight <= 0)
		return false;
	return ResizeBackBuffer(static_cast<UINT>(ms_iWidth), static_cast<UINT>(ms_iHeight));
}

bool CGraphicDevice::SetVSyncEnabled(bool isEnabled)
{
	m_isVSyncEnabled = isEnabled;
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return false;
	return pDX11Device->SetVSyncEnabled(isEnabled);
}

int CGraphicDevice::Create(HWND hWnd, int hres, int vres, bool Windowed, int, int)
{
	if (hres <= 0 || vres <= 0)
		return CREATE_DEVICE;

	ms_hWnd = hWnd;
	ms_iWidth = hres;
	ms_iHeight = vres;

	CGraphicDeviceDX11* pDX11Device = EnsureDX11Device();
	if (!pDX11Device)
		return CREATE_DEVICE;

	pDX11Device->Destroy();
	const bool bCreated = pDX11Device->Create(hWnd, static_cast<UINT>(hres), static_cast<UINT>(vres), Windowed, m_isVSyncEnabled);
	if (!bCreated)
		return CREATE_DEVICE;

	pDX11Device->SetVSyncEnabled(m_isVSyncEnabled);
	m_pStateManager = CStateManager::InstancePtr();

	__InitializeDefaultIndexBufferList();
	__CreateDefaultIndexBufferList();
	__InitializePDTVertexBufferList();
	__CreatePDTVertexBufferList();

	return CREATE_OK;
}

void CGraphicDevice::__InitializePDTVertexBufferList()
{
}

void CGraphicDevice::__DestroyPDTVertexBufferList()
{
}

bool CGraphicDevice::__CreatePDTVertexBufferList()
{
	return true;
}

void CGraphicDevice::__InitializeDefaultIndexBufferList()
{
}

void CGraphicDevice::__DestroyDefaultIndexBufferList()
{
}

bool CGraphicDevice::__CreateDefaultIndexBuffer(UINT, UINT, const WORD*)
{
	return true;
}

bool CGraphicDevice::__CreateDefaultIndexBufferList()
{
	return true;
}

void CGraphicDevice::InitBackBufferCount(UINT uBackBufferCount)
{
	m_uBackBufferCount = uBackBufferCount;
}

void CGraphicDevice::Destroy()
{
	__DestroyDefaultIndexBufferList();
	__DestroyPDTVertexBufferList();

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device)
		pDX11Device->Destroy();
}
