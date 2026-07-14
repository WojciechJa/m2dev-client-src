#include "StdAfx.h"
#include "PythonApplication.h"
#include "PythonPlayer.h"
#include "Eterlib/Camera.h"

#include <winuser.h>

static int gs_nMouseCaptureRef = 0;
static bool gs_bImGuiMouseCapturedPrev = false;
#ifdef BUILD_DEBUG_UI
static DWORD gs_dwLastInputArbKeyLogMS = 0;
static DWORD gs_dwLastInputArbMouseLogMS = 0;
static const DWORD gs_dwInputArbLogThrottleMS = 1000;
#endif

void CPythonApplication::SafeSetCapture()
{
	SetCapture(m_hWnd);
	gs_nMouseCaptureRef++;
}

void CPythonApplication::SafeReleaseCapture()
{
	gs_nMouseCaptureRef--;
	if (gs_nMouseCaptureRef==0)
		ReleaseCapture();
}

void CPythonApplication::__SetFullScreenWindow(HWND hWnd, DWORD dwWidth, DWORD dwHeight, DWORD dwBPP)
{
	DEVMODE DevMode;
	DevMode.dmSize = sizeof(DevMode);
	DevMode.dmBitsPerPel = dwBPP;
	DevMode.dmPelsWidth = dwWidth;
	DevMode.dmPelsHeight = dwHeight;
	DevMode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

	LONG Error = ChangeDisplaySettings(&DevMode, CDS_FULLSCREEN);
	if(Error == DISP_CHANGE_RESTART)
	{
		ChangeDisplaySettings(0,0);
	}
}

void CPythonApplication::__MinimizeFullScreenWindow(HWND hWnd, DWORD dwWidth, DWORD dwHeight)
{
	ChangeDisplaySettings(0, 0);
	SetWindowPos(hWnd, 0, 0, 0,
				 dwWidth,
				 dwHeight,
				 SWP_SHOWWINDOW);
	ShowWindow(hWnd, SW_MINIMIZE);
}

LRESULT CPythonApplication::WindowProcedure(HWND hWnd, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
#ifdef BUILD_DEBUG_UI
	// DX11 Model Sync: Forward input to ImGui Developer Monitoring Tool
	if (CImGuiManager::Instance())
	{
		auto FlushGameplayMouseButtons = [&]()
		{
			POINT kPoint = { 0, 0 };
			if (uiMsg >= WM_MOUSEFIRST && uiMsg <= WM_MOUSELAST)
			{
				kPoint.x = static_cast<LONG>(short(LOWORD(lParam)));
				kPoint.y = static_cast<LONG>(short(HIWORD(lParam)));
			}
			else
			{
				GetCursorPos(&kPoint);
				ScreenToClient(hWnd, &kPoint);
			}

			while (gs_nMouseCaptureRef > 0)
				SafeReleaseCapture();
			if (hWnd == GetCapture())
				ReleaseCapture();

			OnMouseLeftButtonUp(static_cast<short>(kPoint.x), static_cast<short>(kPoint.y));
			OnMouseRightButtonUp(static_cast<short>(kPoint.x), static_cast<short>(kPoint.y));
			UI::CWindowManager::Instance().RunMouseMiddleButtonUp(static_cast<short>(kPoint.x), static_cast<short>(kPoint.y));
			OnMouseMiddleButtonUp(static_cast<short>(kPoint.x), static_cast<short>(kPoint.y));
			CPythonPlayer::Instance().DX11_ForceReleaseMouseHoldState();
		};

		const bool bImGuiToggleMsg = (uiMsg == WM_KEYDOWN && wParam == CImGuiManager::Instance()->GetToggleKey());
		const bool bImGuiConsumed = CImGuiManager::Instance()->HandleInput(hWnd, uiMsg, wParam, lParam);
		const bool bImGuiMouseCapturedNow = CImGuiManager::Instance()->WantsMouseCapture();
		const bool bImGuiKeyboardCapturedNow = CImGuiManager::Instance()->WantsKeyboardCapture();
		const bool bKeyboardMsg = (uiMsg >= WM_KEYFIRST && uiMsg <= WM_KEYLAST);
		const bool bMouseMsg = (uiMsg >= WM_MOUSEFIRST && uiMsg <= WM_MOUSELAST);
		const bool bMouseButtonMsg =
			(uiMsg == WM_LBUTTONDOWN || uiMsg == WM_LBUTTONUP || uiMsg == WM_LBUTTONDBLCLK ||
			 uiMsg == WM_RBUTTONDOWN || uiMsg == WM_RBUTTONUP || uiMsg == WM_RBUTTONDBLCLK ||
			 uiMsg == WM_MBUTTONDOWN || uiMsg == WM_MBUTTONUP || uiMsg == WM_MBUTTONDBLCLK ||
			 uiMsg == WM_XBUTTONDOWN || uiMsg == WM_XBUTTONUP || uiMsg == WM_XBUTTONDBLCLK);
		const DWORD dwInputArbNow = ELTimer_GetMSec();

		if (bKeyboardMsg &&
			(0 == gs_dwLastInputArbKeyLogMS || dwInputArbNow - gs_dwLastInputArbKeyLogMS >= gs_dwInputArbLogThrottleMS))
		{
			gs_dwLastInputArbKeyLogMS = dwInputArbNow;
			TraceError("DX11_INPUT_ARB key msg=%u imgui_capture=%u consumed=%u",
				uiMsg,
				bImGuiKeyboardCapturedNow ? 1u : 0u,
				bImGuiConsumed ? 1u : 0u);
		}
		if (bMouseMsg &&
			(0 == gs_dwLastInputArbMouseLogMS || dwInputArbNow - gs_dwLastInputArbMouseLogMS >= gs_dwInputArbLogThrottleMS))
		{
			gs_dwLastInputArbMouseLogMS = dwInputArbNow;
			TraceError("DX11_INPUT_ARB mouse msg=%u imgui_capture=%u consumed=%u",
				uiMsg,
				bImGuiMouseCapturedNow ? 1u : 0u,
				bImGuiConsumed ? 1u : 0u);
		}

		// F12 toggle hotkey can leave gameplay mouse buttons in held state while overlay takes over input.
		// Force-release gameplay buttons on every toggle edge (enable and disable).
		if (bImGuiToggleMsg)
		{
			FlushGameplayMouseButtons();
			gs_bImGuiMouseCapturedPrev = false;
		}
		// Deterministic arbitration: when ImGui takes mouse capture, flush gameplay mouse-down state once.
		else if (bImGuiMouseCapturedNow && !gs_bImGuiMouseCapturedPrev)
		{
			FlushGameplayMouseButtons();
		}

		// Safety net: if ImGui consumed mouse-button input while game capture is still active,
		// force-release gameplay button state to prevent stuck hold.
		if (bImGuiConsumed && bMouseButtonMsg && (gs_nMouseCaptureRef > 0 || hWnd == GetCapture()))
		{
			FlushGameplayMouseButtons();
		}

		// M3-IMGUI-INPUT-74: Flush gameplay mouse buttons when ImGui is first enabled
		// This prevents stuck button states when user toggles DebugUI during mouse hold
		static bool sbWasImGuiEnabled = false;
		bool bIsImGuiEnabled = CImGuiManager::Instance() && CImGuiManager::Instance()->IsEnabled();

		if (bIsImGuiEnabled && !sbWasImGuiEnabled)
		{
			FlushGameplayMouseButtons();
			TraceError("DX11_IMGUI_ENABLED flushing_game_mouse_state ImGui_enabled=1");
		}

		sbWasImGuiEnabled = bIsImGuiEnabled;

		// M3-IMGUI-INPUT-74: Flush stuck mouse state BEFORE any button-down when ImGui is enabled
		// This prevents the "hold" bug when clicking game UI while DebugUI is open
		if (bIsImGuiEnabled && !bImGuiConsumed && bMouseButtonMsg)
		{
			// Check if this is a button-down (not up) message
			const bool bButtonKeyDown =
				(uiMsg == WM_LBUTTONDOWN || uiMsg == WM_RBUTTONDOWN ||
				 uiMsg == WM_MBUTTONDOWN || uiMsg == WM_XBUTTONDOWN);

			if (bButtonKeyDown)
			{
				// Flush any stuck capture state before processing new click
				if (gs_nMouseCaptureRef > 0 || hWnd == GetCapture())
				{
					FlushGameplayMouseButtons();
					TraceError("DX11_IMGUI_BUTTONDOWN flushing_stuck_state msg=%u capture_ref=%d",
						uiMsg, gs_nMouseCaptureRef);
				}
			}
		}

		gs_bImGuiMouseCapturedPrev = bImGuiMouseCapturedNow;
		if (bImGuiConsumed)
			return 0;  // Input consumed by ImGui
	}
#endif

	const int c_DoubleClickTime = 300;
	const int c_DoubleClickBox = 5;
	static int s_xDownPosition = 0;
	static int s_yDownPosition = 0;

	switch (uiMsg)
	{
		case WM_ACTIVATEAPP:
			{
				m_isActivateWnd = (wParam == WA_ACTIVE) || (wParam == WA_CLICKACTIVE);

				if (m_isActivateWnd)
				{
					m_SoundEngine.RestoreVolume();

					//////////////////

					if (m_isWindowFullScreenEnable)
					{
						__SetFullScreenWindow(hWnd, m_dwWidth, m_dwHeight, m_pySystem.GetBPP());
					}
				}
				else
				{
					m_SoundEngine.SaveVolume(m_isMinimizedWnd);

					//////////////////

					if (m_isWindowFullScreenEnable)
					{
						__MinimizeFullScreenWindow(hWnd, m_dwWidth, m_dwHeight);
					}

					if (IsUserMovingMainWindow())
					{
						SetUserMovingMainWindow(false);
					}
				}
			}
			break;

		case WM_INPUTLANGCHANGE:
			return CPythonIME::Instance().WMInputLanguage(hWnd, uiMsg, wParam, lParam);
			break;

		case WM_IME_STARTCOMPOSITION:
			return CPythonIME::Instance().WMStartComposition(hWnd, uiMsg, wParam, lParam);
			break;

		case WM_IME_COMPOSITION:
			return CPythonIME::Instance().WMComposition(hWnd, uiMsg, wParam, lParam);
			break;

		case WM_IME_ENDCOMPOSITION:
			return CPythonIME::Instance().WMEndComposition(hWnd, uiMsg, wParam, lParam);
			break;

		case WM_IME_NOTIFY:
			return CPythonIME::Instance().WMNotify(hWnd, uiMsg, wParam, lParam);
			break;

		case WM_IME_SETCONTEXT:
			lParam &= ~(ISC_SHOWUICOMPOSITIONWINDOW | ISC_SHOWUIALLCANDIDATEWINDOW);
			break;

		case WM_CHAR:
			return CPythonIME::Instance().WMChar(hWnd, uiMsg, wParam, lParam);
			break;

		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE && IsUserMovingMainWindow())
				SetUserMovingMainWindow(false);
			OnIMEKeyDown(LOWORD(wParam));
			break;

		case WM_LBUTTONDOWN:
			SafeSetCapture();

			// M3-IMGUI-INPUT-74: Log button-down to diagnose stuck mouse issue
			TraceError("DX11_MOUSE_LBUTTONDOWN x=%d y=%d capture_ref=%d",
				LOWORD(lParam), HIWORD(lParam), gs_nMouseCaptureRef);

			if (ELTimer_GetMSec() - m_dwLButtonDownTime < c_DoubleClickTime &&
				abs(LOWORD(lParam) - s_xDownPosition) < c_DoubleClickBox &&
				abs(HIWORD(lParam) - s_yDownPosition) < c_DoubleClickBox)
			{
				m_dwLButtonDownTime = 0;

				OnMouseLeftButtonDoubleClick(short(LOWORD(lParam)), short(HIWORD(lParam)));
			}
			else
			{
				m_dwLButtonDownTime = ELTimer_GetMSec();

				OnMouseLeftButtonDown(short(LOWORD(lParam)), short(HIWORD(lParam)));
			}

			s_xDownPosition = LOWORD(lParam);
			s_yDownPosition = HIWORD(lParam);

			if (IsUserMovingMainWindow())
				SetUserMovingMainWindow(false);
			return 0;

		case WM_LBUTTONUP:
		{
			m_dwLButtonUpTime = ELTimer_GetMSec();

			// M3-IMGUI-INPUT-74: Log button-up to diagnose stuck mouse issue
			HWND hCaptured = GetCapture();
			TraceError("DX11_MOUSE_LBUTTONUP x=%d y=%d capture_ref=%d hWnd_is_capture=%d",
				LOWORD(lParam), HIWORD(lParam), gs_nMouseCaptureRef, (hWnd == hCaptured) ? 1 : 0);

			if (hWnd == GetCapture())
			{
				SafeReleaseCapture();
				OnMouseLeftButtonUp(short(LOWORD(lParam)), short(HIWORD(lParam)));
			}
			else
			{
				// M3-IMGUI-INPUT-74: Button up not processed because capture was lost!
				TraceError("DX11_MOUSE_LBUTTONUP_LOST_CAPTURE capture_ref=%d expected_hwnd=%p actual_capture=%p",
					gs_nMouseCaptureRef, hWnd, hCaptured);

				// Force cleanup to prevent stuck button state
				while (gs_nMouseCaptureRef > 0)
					SafeReleaseCapture();
				OnMouseLeftButtonUp(short(LOWORD(lParam)), short(HIWORD(lParam)));
			}
			return 0;
		}

		case WM_MBUTTONDOWN:
			SafeSetCapture();

			UI::CWindowManager::Instance().RunMouseMiddleButtonDown(short(LOWORD(lParam)), short(HIWORD(lParam)));
//			OnMouseMiddleButtonDown(short(LOWORD(lParam)), short(HIWORD(lParam)));
			break;

		case WM_MBUTTONUP:
			if (GetCapture() == hWnd)
			{
				SafeReleaseCapture();

				UI::CWindowManager::Instance().RunMouseMiddleButtonUp(short(LOWORD(lParam)), short(HIWORD(lParam)));
//				OnMouseMiddleButtonUp(short(LOWORD(lParam)), short(HIWORD(lParam)));
			}
			break;

		case WM_RBUTTONDOWN:
			SafeSetCapture();
			OnMouseRightButtonDown(short(LOWORD(lParam)), short(HIWORD(lParam)));
			return 0;

		case WM_RBUTTONUP:
			if (hWnd == GetCapture()) 
			{
				SafeReleaseCapture();

				OnMouseRightButtonUp(short(LOWORD(lParam)), short(HIWORD(lParam)));
			}
			return 0;

		case 0x20a:
			if (CPythonApplication::Instance().IsWebPageMode())
			{
				// 웹브라우저 상태일때는 휠 작동 안되도록 처리
			}
			else
			{
				OnMouseWheel(short(HIWORD(wParam)));
			}
			break;

		case WM_SIZE:
			switch (wParam)
			{
				case SIZE_RESTORED:
				case SIZE_MAXIMIZED:
					{
						RECT rcWnd; 
						GetClientRect(&rcWnd); 
				
						UINT uWidth=rcWnd.right-rcWnd.left; 
						UINT uHeight=rcWnd.bottom-rcWnd.top;
						__ResizeRenderBackend(uWidth, uHeight);
					}
					break;
			}

			if (wParam==SIZE_MINIMIZED)
				m_isMinimizedWnd=true;
			else
				m_isMinimizedWnd=false;

			OnSizeChange(short(LOWORD(lParam)), short(HIWORD(lParam)));

			break;

		case WM_EXITSIZEMOVE:    
			{
				RECT rcWnd; 
				GetClientRect(&rcWnd); 
				
				UINT uWidth=rcWnd.right-rcWnd.left; 
				UINT uHeight=rcWnd.bottom-rcWnd.top;
				__ResizeRenderBackend(uWidth, uHeight);
				OnSizeChange(short(LOWORD(lParam)), short(HIWORD(lParam)));
			}
			break; 
		case WM_NCLBUTTONDOWN:
			{
				switch (wParam)
				{
				case HTMAXBUTTON:
				case HTSYSMENU:
					return 0;
				case HTMINBUTTON:
					ShowWindow(hWnd, SW_MINIMIZE);
					return 0;
				case HTCLOSE:
					RunPressExitKey();
					return 0;
				case HTCAPTION:
					if (!IsUserMovingMainWindow())
						SetUserMovingMainWindow(true);
		
					return 0;
				}
		
				break;
			}
			
		case WM_NCLBUTTONUP:
			{
				if (IsUserMovingMainWindow())
					SetUserMovingMainWindow(false);
				
				break;
			}
		
		case WM_NCRBUTTONDOWN:
		case WM_NCRBUTTONUP:
		case WM_CONTEXTMENU:
			return 0;
		case WM_SYSCOMMAND:
			if (wParam == SC_KEYMENU)
				return 0;
			break;
		case WM_SYSKEYDOWN:
			switch (LOWORD(wParam))
			{
				case VK_F10:
					break;
			}
			break;

		case WM_SYSKEYUP:
			switch(LOWORD(wParam))
			{
				case 18:
					return FALSE;
					break;
				case VK_F10:
					break;
			}
			break;

		case WM_SETCURSOR:
			if (IsActive())
			{
				if (m_bCursorVisible && CURSOR_MODE_HARDWARE == m_iCursorMode)
				{
					SetCursor((HCURSOR) m_hCurrentCursor);
					return 0;
				}
				else
				{
					SetCursor(NULL);
					return 0;
				}
			}
			break;

		case WM_CLOSE:
#ifdef _DEBUG
			PostQuitMessage(0);
#else	
			RunPressExitKey();
#endif
			return 0;

		case WM_DESTROY:
			return 0;
		default:
			//Tracenf("%x msg %x", timeGetTime(), uiMsg);
			break;
	}	

	return CMSApplication::WindowProcedure(hWnd, uiMsg, wParam, lParam);
}
