#include "StdAfx.h"
#include "Input.h"

void* CInputDevice::ms_lpDI = NULL;
HWND CInputDevice::ms_hWnd = NULL;
void* CInputKeyboard::ms_lpKeyboard = NULL;
bool CInputKeyboard::ms_bPressedKey[256];
char CInputKeyboard::ms_diks[256];

CInputDevice::CInputDevice()
{
}

CInputDevice::~CInputDevice()
{
}

HRESULT CInputDevice::CreateDevice(HWND hWnd)
{
	ms_hWnd = hWnd;
	// DX11 native path: keyboard is polled via Win32 state.
	return S_OK;
}

CInputKeyboard::CInputKeyboard()
{
	ResetKeyboard();
}

CInputKeyboard::~CInputKeyboard()
{
}

void CInputKeyboard::ResetKeyboard()
{
	memset(ms_diks, 0, sizeof(ms_diks));
	memset(ms_bPressedKey, 0, sizeof(ms_bPressedKey));
}

bool CInputKeyboard::InitializeKeyboard(HWND hWnd)
{
	if (FAILED(CreateDevice(hWnd)))
		return false;

	ResetKeyboard();
	return true;
}

void CInputKeyboard::UpdateKeyboard()
{
	// Ignore keyboard input when game window is not active.
	HWND hForeground = GetForegroundWindow();
	const bool bHasForeground =
		(hForeground != NULL) &&
		(hForeground == ms_hWnd || IsChild(ms_hWnd, hForeground));

	if (!ms_hWnd || !bHasForeground || !IsWindowVisible(ms_hWnd))
	{
		for (int i = 0; i < 256; ++i)
		{
			if (IsPressed(i))
				KeyUp(i);
		}
		return;
	}

	for (int i = 0; i < 256; ++i)
	{
		const UINT uVirtualKey = MapVirtualKeyExA(static_cast<UINT>(i), MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
		if (0u == uVirtualKey)
		{
			if (IsPressed(i))
				KeyUp(i);
			continue;
		}

		const SHORT sState = GetAsyncKeyState(static_cast<int>(uVirtualKey));
		const bool bDown = (sState & 0x8000) != 0;
		if (bDown)
		{
			if (!IsPressed(i))
				KeyDown(i);
		}
		else if (IsPressed(i))
		{
			KeyUp(i);
		}
	}
}

void CInputKeyboard::KeyDown(int iIndex)
{
	ms_bPressedKey[iIndex] = true;
	OnKeyDown(iIndex);
}

void CInputKeyboard::KeyUp(int iIndex)
{
	ms_bPressedKey[iIndex] = false;
	OnKeyUp(iIndex);
}

bool CInputKeyboard::IsPressed(int iIndex)
{
	return ms_bPressedKey[iIndex];
}
