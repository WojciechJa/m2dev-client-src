#include "StdAfx.h"
#include "PythonApplication.h"

int CPythonApplication::OnLogoOpen(char* /*szName*/)
{
	m_pLogoTex = nullptr;
	m_pCaptureBuffer = nullptr;
	m_lBufferSize = 0;
	m_bLogoError = false;
	m_bLogoPlay = false;

	m_pGraphBuilder = nullptr;
	m_pFilterSG = nullptr;
	m_pSampleGrabber = nullptr;
	m_pMediaCtrl = nullptr;
	m_pMediaEvent = nullptr;
	m_pVideoWnd = nullptr;
	m_pBasicVideo = nullptr;

	m_nLeft = 0;
	m_nRight = 0;
	m_nTop = 0;
	m_nBottom = 0;

	// DX11 strict migration: legacy DirectShow logo playback is disabled.
	return 0;
}

int CPythonApplication::OnLogoUpdate()
{
	return 0;
}

void CPythonApplication::OnLogoRender()
{
}

void CPythonApplication::OnLogoClose()
{
	if (m_pCaptureBuffer)
	{
		delete[] m_pCaptureBuffer;
		m_pCaptureBuffer = nullptr;
	}

	if (m_pLogoTex)
	{
		m_pLogoTex->Destroy();
		delete m_pLogoTex;
		m_pLogoTex = nullptr;
	}

	m_lBufferSize = 0;
	m_bLogoError = false;
	m_bLogoPlay = false;

	m_pGraphBuilder = nullptr;
	m_pFilterSG = nullptr;
	m_pSampleGrabber = nullptr;
	m_pMediaCtrl = nullptr;
	m_pMediaEvent = nullptr;
	m_pVideoWnd = nullptr;
	m_pBasicVideo = nullptr;
}
