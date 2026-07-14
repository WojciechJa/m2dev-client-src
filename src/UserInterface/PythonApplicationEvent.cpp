#include "StdAfx.h"
#include "PythonApplication.h"
#include "EterLib/Camera.h"

void CPythonApplication::OnCameraUpdate()
{
	if ( m_pyBackground.IsMapReady() )
	{
		CCamera* pkCameraMgr = CCameraManager::Instance().GetCurrentCamera();
		if (pkCameraMgr)
			pkCameraMgr->Update();
	}
}

void CPythonApplication::OnUIUpdate()
{
	UI::CWindowManager& rkUIMgr=UI::CWindowManager::Instance();
	rkUIMgr.Update();
}

void CPythonApplication::OnUIRender()
{
	static bool s_bDX11StartupFirstUISubmitLogged = false;
	if (!s_bDX11StartupFirstUISubmitLogged &&
		m_eRenderBackend == RENDER_BACKEND_DX11 &&
		m_grpDeviceDX11.IsValid())
	{
		s_bDX11StartupFirstUISubmitLogged = true;
		TraceError("DX11_STARTUP_TIMELINE event=first_ui_submit");
	}

	if (m_eRenderBackend == RENDER_BACKEND_DX11 && m_grpDeviceDX11.IsValid())
	{
		// World->UI state hygiene: force UI pass to start from a known DX11 baseline.
		m_grpDeviceDX11.BindMainRenderTargets();
		if (ID3D11DeviceContext* pContext = m_grpDeviceDX11.GetContext())
		{
			const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			ID3D11ShaderResourceView* apNullSRV[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			ID3D11SamplerState* apNullSampler[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			ID3D11Buffer* apNullCB[4] = { nullptr, nullptr, nullptr, nullptr };
			ID3D11BlendState* pUIBlendState = m_grpDeviceDX11.GetBootstrapUIAlphaBlendState();
			ID3D11DepthStencilState* pUIDepthDisableState = m_grpDeviceDX11.GetBootstrapUIDepthDisableState();
			ID3D11RasterizerState* pUIRasterState = m_grpDeviceDX11.GetBootstrapRasterizerState();

			pContext->OMSetBlendState(pUIBlendState, afBlendFactor, 0xFFFFFFFFu);
			pContext->OMSetDepthStencilState(pUIDepthDisableState, 0u);
			pContext->RSSetState(pUIRasterState);
			pContext->PSSetShaderResources(0, 8, apNullSRV);
			pContext->VSSetShaderResources(0, 8, apNullSRV);
			pContext->PSSetSamplers(0, 8, apNullSampler);
			pContext->VSSetSamplers(0, 8, apNullSampler);
			pContext->VSSetConstantBuffers(0, 4, apNullCB);
			pContext->PSSetConstantBuffers(0, 4, apNullCB);

			static DWORD s_dwDX11UIPassStateLogTick = 0u;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0u == s_dwDX11UIPassStateLogTick || (dwNow - s_dwDX11UIPassStateLogTick) >= 5000u)
			{
				s_dwDX11UIPassStateLogTick = dwNow;
				ID3D11BlendState* pBoundBlend = nullptr;
				ID3D11DepthStencilState* pBoundDepth = nullptr;
				FLOAT afBoundBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
				UINT uBoundSampleMask = 0u;
				UINT uBoundStencilRef = 0u;
				D3D11_VIEWPORT kViewport = {};
				UINT uViewportCount = 1u;

				pContext->OMGetBlendState(&pBoundBlend, afBoundBlendFactor, &uBoundSampleMask);
				pContext->OMGetDepthStencilState(&pBoundDepth, &uBoundStencilRef);
				pContext->RSGetViewports(&uViewportCount, &kViewport);

				TraceError(
					"DX11_UI_PASS_STATE depth=%p blend=%p viewport_w=%.0f viewport_h=%.0f stencil_ref=%u sample_mask=0x%08X",
					pBoundDepth,
					pBoundBlend,
					(uViewportCount > 0u ? kViewport.Width : 0.0f),
					(uViewportCount > 0u ? kViewport.Height : 0.0f),
					uBoundStencilRef,
					uBoundSampleMask);

				if (pBoundBlend)
					pBoundBlend->Release();
				if (pBoundDepth)
					pBoundDepth->Release();
			}
		}
	}

	UI::CWindowManager& rkUIMgr=UI::CWindowManager::Instance();
	rkUIMgr.Render();
}

void CPythonApplication::OnSizeChange(int width, int height)
{	
}

void CPythonApplication::OnMouseMiddleButtonDown(int x, int y)
{
	CCameraManager& rkCmrMgr=CCameraManager::Instance();
	CCamera* pkCmrCur=rkCmrMgr.GetCurrentCamera();
	if (pkCmrCur)
		pkCmrCur->BeginDrag(x, y);

	if ( !m_pyBackground.IsMapReady() )
		return;

	SetCursorNum(CAMERA_ROTATE);
	if ( CURSOR_MODE_HARDWARE == GetCursorMode())
		SetCursorVisible(FALSE, true);
}

void CPythonApplication::OnMouseMiddleButtonUp(int x, int y)
{
	CCameraManager& rkCmrMgr=CCameraManager::Instance();
	CCamera* pkCmrCur=rkCmrMgr.GetCurrentCamera();
	if (pkCmrCur)
		pkCmrCur->EndDrag();

	if ( !m_pyBackground.IsMapReady() )
		return;

	SetCursorNum(NORMAL);
	if ( CURSOR_MODE_HARDWARE == GetCursorMode())
		SetCursorVisible(TRUE);
}

void CPythonApplication::OnMouseWheel(int nLen)
{
	CCameraManager& rkCmrMgr=CCameraManager::Instance();
	CCamera* pkCmrCur=rkCmrMgr.GetCurrentCamera();
	if (pkCmrCur)
		pkCmrCur->Wheel(nLen);
}


void CPythonApplication::OnMouseMove(int x, int y)
{
	CCameraManager& rkCmrMgr=CCameraManager::Instance();
	CCamera* pkCmrCur=rkCmrMgr.GetCurrentCamera();
	
	POINT Point;
	if (pkCmrCur)
	{
		if ( CPythonBackground::Instance().IsMapReady() && pkCmrCur->Drag(x, y, &Point) )
		{
			x = Point.x;
			y = Point.y;
			ClientToScreen(m_hWnd, &Point);

			SetCursorPos(Point.x, Point.y);

		}
	}
	
	RECT rcWnd;
	GetClientRect(&rcWnd);
	
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.SetResolution(rcWnd.right-rcWnd.left, rcWnd.bottom-rcWnd.top);

	rkWndMgr.RunMouseMove(x, y);
}

void CPythonApplication::OnMouseLeftButtonDown(int x, int y)
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunMouseLeftButtonDown(x, y);
}

void CPythonApplication::OnMouseLeftButtonUp(int x, int y)
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunMouseLeftButtonUp(x, y);
}

void CPythonApplication::OnMouseLeftButtonDoubleClick(int x, int y)
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunMouseLeftButtonDown(x, y);
	rkWndMgr.RunMouseLeftButtonDoubleClick(x, y);
}

void CPythonApplication::OnMouseRightButtonDown(int x, int y)
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunMouseRightButtonDown(x, y);
}

void CPythonApplication::OnMouseRightButtonUp(int x, int y)
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunMouseRightButtonUp(x, y);
}

void CPythonApplication::OnKeyDown(int iIndex)
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();

	if (DIK_ESCAPE == iIndex)
	{
		rkWndMgr.RunPressEscapeKey();
	}

	rkWndMgr.RunKeyDown(iIndex);
}

void CPythonApplication::OnKeyUp(int iIndex)
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunKeyUp(iIndex);
}

void CPythonApplication::RunIMEUpdate()
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunIMEUpdate();
}
void CPythonApplication::RunIMETabEvent()
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunIMETabEvent();
}
void CPythonApplication::RunIMEReturnEvent()
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunIMEReturnEvent();
}
void CPythonApplication::OnIMEKeyDown(int iIndex)
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunIMEKeyDown(iIndex);
}
/////////////////////////////

void CPythonApplication::RunIMEOpenCandidateListEvent()
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunOpenCandidate();
}
void CPythonApplication::RunIMECloseCandidateListEvent()
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunCloseCandidate();
}
void CPythonApplication::RunIMEOpenReadingWndEvent()
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunOpenReading();
}
void CPythonApplication::RunIMECloseReadingWndEvent()
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunCloseReading();
}

/////////////////////////////
void CPythonApplication::RunPressExitKey()
{
	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	rkWndMgr.RunPressExitKey();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPythonApplication::OnMouseUpdate()
{
#ifdef _DEBUG
	if (!m_poMouseHandler)
	{
		//assert(!" CPythonApplication::OnMouseUpdate - Mouse handler has not set!");
		return;
	}
#endif _DEBUG

	UI::CWindowManager& rkWndMgr=UI::CWindowManager::Instance();
	long lx, ly;
	rkWndMgr.GetMousePosition(lx, ly);
	PyCallClassMemberFunc(m_poMouseHandler, "Update", Py_BuildValue("(ii)", lx, ly));
}

void CPythonApplication::OnMouseRender()
{
#ifdef _DEBUG
	if (!m_poMouseHandler)
	{
		//assert(!" CPythonApplication::OnMouseRender - Mouse handler has not set!");
		return;
	}
#endif _DEBUG

	PyCallClassMemberFunc(m_poMouseHandler, "Render", Py_BuildValue("()"));
}
