#include "StdAfx.h"
#include "EterLib/JpegFile.h"
#include "EterLib/GrpDeviceDX11.h"
#include "EterLib/StateManager11.h"
#include "EterLib/LightDesc.h"
#include "PythonGraphic.h"
#include <utf8.h>
#include <cmath>

bool g_isScreenShotKey = false;

void CPythonGraphic::Destroy()
{	
}

ID3D11Device* CPythonGraphic::GetDX11Device()
{
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
		return pDX11Device->GetDevice();

	return nullptr;
}

float CPythonGraphic::GetOrthoDepth()
{
	return m_fOrthoDepth;
}

void CPythonGraphic::SetInterfaceRenderState()
{
	// DX11 strict path: derive ortho dimensions from active backbuffer.
	UINT uBackBufferWidth = 0;
	UINT uBackBufferHeight = 0;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (uBackBufferWidth > 0 && uBackBufferHeight > 0)
	{
		ms_iWidth = static_cast<int>(uBackBufferWidth);
		ms_iHeight = static_cast<int>(uBackBufferHeight);
	}

	float fSafeWidth = static_cast<float>(ms_iWidth > 0 ? ms_iWidth : 1);
	float fSafeHeight = static_cast<float>(ms_iHeight > 0 ? ms_iHeight : 1);
	float fSafeDepth = GetOrthoDepth();
	if (!std::isfinite(fSafeDepth) || fSafeDepth <= 0.0f)
		fSafeDepth = 1000.0f;

	// Match legacy UI transform contract:
	// UI draws expect identity view/world with orthographic projection.
	ms_matView = DirectX::SimpleMath::Matrix::Identity;
	ms_matWorld = DirectX::SimpleMath::Matrix::Identity;
	ms_matWorldView = DirectX::SimpleMath::Matrix::Identity;
	ms_matInverseView = DirectX::SimpleMath::Matrix::Identity;
	ms_matInverseViewYAxis = DirectX::SimpleMath::Matrix::Identity;

	CPythonGraphic::Instance().SetBlendOperation();
	CPythonGraphic::Instance().SetOrtho2D(fSafeWidth, fSafeHeight, fSafeDepth);

	// DX11 strict path:
	// Do not force global UI depth-disable state here because world draws can still
	// be triggered from UI python flow. Forcing depth-off at this level causes
	// "see-through walls" regressions in world actors/trees.
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
	{
		if (pDX11Device->IsValid())
		{
			pDX11Device->BindMainRenderTargets();
			if (ID3D11DeviceContext* pContext = pDX11Device->GetContext())
			{
				const FLOAT afBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
				pContext->OMSetBlendState(pDX11Device->GetBootstrapUIAlphaBlendState(), afBlendFactor, 0xFFFFFFFFu);
				pContext->OMSetDepthStencilState(pDX11Device->GetBootstrapUIDepthDisableState(), 0u);
				pContext->RSSetState(pDX11Device->GetBootstrapRasterizerState());
			}
		}
	}

	static bool s_bLoggedInterfaceStateDX11 = false;
	if (!s_bLoggedInterfaceStateDX11)
	{
		s_bLoggedInterfaceStateDX11 = true;
		TraceError("DX11_UI_STATE_GUARD interface_render_state_dx11_only w=%d h=%d depth=%.3f",
			ms_iWidth, ms_iHeight, fSafeDepth);
	}
}

void CPythonGraphic::SetGameRenderState()
{
	// DX11 strict path baseline for world rendering.
	// Keep depth test/write enabled to prevent world geometry leaking through walls.
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
	{
		if (pDX11Device->IsValid())
		{
			pDX11Device->BindMainRenderTargets();
			if (ID3D11DeviceContext* pContext = pDX11Device->GetContext())
			{
				const FLOAT afBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
				ID3D11RasterizerState* pRaster = pDX11Device->GetBootstrapRasterizerState();

				// nullptr depth state = D3D11 default (DepthEnable=TRUE, WriteMask=ALL, Func=LESS)
				pContext->OMSetBlendState(nullptr, afBlendFactor, 0xFFFFFFFFu);
				pContext->OMSetDepthStencilState(nullptr, 0u);
				pContext->RSSetState(pRaster);
			}
		}
	}
}

void CPythonGraphic::SetCursorPosition(int x, int y)
{
	CScreen::SetCursorPosition(x, y, ms_iWidth, ms_iHeight);
}

void CPythonGraphic::SetOmniLight()
{
	CStateManager11* pStateManager11 = CStateManager11::InstancePtr();
	if (!pStateManager11)
	{
		static bool s_bLoggedNoStateManager = false;
		if (!s_bLoggedNoStateManager)
		{
			s_bLoggedNoStateManager = true;
			TraceError("DX11_UI_STATE_GUARD omni_light_skip reason=state_manager11_unavailable");
		}
		return;
	}

	SLightDesc kLight = {};
	kLight.Type = LIGHT_DESC_TYPE_DIRECTIONAL;
	kLight.Direction = D3DXVECTOR3(-0.35f, -0.15f, -0.92f);
	kLight.Diffuse = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
	kLight.Specular = D3DXCOLOR(0.6f, 0.6f, 0.6f, 1.0f);
	kLight.Ambient = D3DXCOLOR(0.30f, 0.30f, 0.30f, 1.0f);
	kLight.Range = 50000.0f;
	kLight.Falloff = 1.0f;
	kLight.Attenuation0 = 1.0f;
	kLight.Attenuation1 = 0.0f;
	kLight.Attenuation2 = 0.0f;
	kLight.Theta = 0.0f;
	kLight.Phi = 0.0f;

	pStateManager11->SetLight(0, &kLight);
	pStateManager11->SetLightEnable(0, TRUE);
	pStateManager11->SetLightingEnabled(true);
	pStateManager11->ApplyState();
}

void CPythonGraphic::SetViewport(float fx, float fy, float fWidth, float fHeight)
{
	// M2-UIPY-27: DX11 strict-safe viewport path
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		ID3D11DeviceContext* pContext = pDX11Device->GetContext();
		if (pContext)
		{
			// Backup current viewport
			UINT uViewportCount = 1;
			pContext->RSGetViewports(&uViewportCount, &m_backupViewportDX11);

			// Set new viewport
			D3D11_VIEWPORT ViewPortDX11;
			ViewPortDX11.TopLeftX = fx;
			ViewPortDX11.TopLeftY = fy;
			ViewPortDX11.Width = fWidth;
			ViewPortDX11.Height = fHeight;
			ViewPortDX11.MinDepth = 0.0f;
			ViewPortDX11.MaxDepth = 1.0f;
			pContext->RSSetViewports(1, &ViewPortDX11);

			static bool s_bLoggedViewportSet = false;
			if (!s_bLoggedViewportSet)
			{
				s_bLoggedViewportSet = true;
				TraceError("DX11_PYGRAPHIC_VIEWPORT_SET x=%.3f y=%.3f width=%.3f height=%.3f",
					fx, fy, fWidth, fHeight);
			}
			return;
		}

		static bool s_bLoggedViewportFailNoContext = false;
		if (!s_bLoggedViewportFailNoContext)
		{
			s_bLoggedViewportFailNoContext = true;
			TraceError("DX11_PYGRAPHIC_VIEWPORT_SET reason=no_dx11_context x=%.3f y=%.3f width=%.3f height=%.3f",
				fx, fy, fWidth, fHeight);
		}
		return;
	}

}

void CPythonGraphic::RestoreViewport()
{
	// M2-UIPY-27: DX11 strict-safe viewport restore path
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		ID3D11DeviceContext* pContext = pDX11Device->GetContext();
		if (pContext)
		{
			// Restore backed-up viewport
			pContext->RSSetViewports(1, &m_backupViewportDX11);

			static bool s_bLoggedViewportRestore = false;
			if (!s_bLoggedViewportRestore)
			{
				s_bLoggedViewportRestore = true;
				TraceError("DX11_PYGRAPHIC_VIEWPORT_RESTORE top_left_x=%.3f top_left_y=%.3f width=%.3f height=%.3f",
					m_backupViewportDX11.TopLeftX, m_backupViewportDX11.TopLeftY,
					m_backupViewportDX11.Width, m_backupViewportDX11.Height);
			}
			return;
		}

		static bool s_bLoggedViewportRestoreFailNoContext = false;
		if (!s_bLoggedViewportRestoreFailNoContext)
		{
			s_bLoggedViewportRestoreFailNoContext = true;
			TraceError("DX11_PYGRAPHIC_VIEWPORT_RESTORE reason=no_dx11_context");
		}
		return;
	}

}

void CPythonGraphic::SetGamma(float fGammaFactor)
{
	(void)fGammaFactor;
	static bool s_bLoggedGammaSkip = false;
	if (!s_bLoggedGammaSkip)
	{
		s_bLoggedGammaSkip = true;
		TraceError("DX11_PYGRAPHIC_GAMMA_SKIP reason=strict_mode_no_dx9_gamma");
	}
	return;
}

void GenScreenShotTag(const char* src, DWORD crc32, char* leaf, size_t leafLen)
{
	const char* p = src;
	const char* n = p;
	while (n = strchr(p, '\\'))
		p = n + 1;

	_snprintf(leaf, leafLen, "YMIR_METIN2:%s:0x%.8x", p, crc32);
}

bool CPythonGraphic::SaveJPEG(const char * pszFileName, LPBYTE pbyBuffer, UINT uWidth, UINT uHeight)
{
	return jpeg_save(pbyBuffer, uWidth, uHeight, 100, pszFileName) != 0;
}

bool CPythonGraphic::SaveScreenShot(const char * c_pszFileName)
{
	// M2-PYGRAPHIC-29: DX11 screenshot path using swapchain backbuffer copy to staging texture
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		static bool s_bLoggedNoDevice = false;
		if (!s_bLoggedNoDevice)
		{
			s_bLoggedNoDevice = true;
			TraceError("DX11_PYGRAPHIC_SCREENSHOT_FAIL reason=no_dx11_device");
		}
		return false;
	}

	IDXGISwapChain* pSwapChain = pDX11Device->GetSwapChain();
	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	ID3D11Device* pDevice = pDX11Device->GetDevice();

	if (!pSwapChain || !pContext || !pDevice)
	{
		static bool s_bLoggedMissingInterfaces = false;
		if (!s_bLoggedMissingInterfaces)
		{
			s_bLoggedMissingInterfaces = true;
			TraceError("DX11_PYGRAPHIC_SCREENSHOT_FAIL reason=missing_dx11_interfaces");
		}
		return false;
	}

	// Step 1: Get swapchain backbuffer
	ID3D11Texture2D* pDX11BackBuffer = nullptr;
	HRESULT hDX11Result = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pDX11BackBuffer);
	if (FAILED(hDX11Result))
	{
		TraceError("DX11_PYGRAPHIC_SCREENSHOT_FAIL reason=getbuffer_failed hr=0x%08x", hDX11Result);
		return false;
	}

	// Step 2: Get backbuffer description
	D3D11_TEXTURE2D_DESC kBackbufferDesc;
	pDX11BackBuffer->GetDesc(&kBackbufferDesc);

	UINT uDX11Width = kBackbufferDesc.Width;
	UINT uDX11Height = kBackbufferDesc.Height;

	// Validate source format (current converter supports 8-bit RGBA/BGRA only).
	const DXGI_FORMAT eBackBufferFormat = kBackbufferDesc.Format;
	const bool bIsBGRA =
		(DXGI_FORMAT_B8G8R8A8_UNORM == eBackBufferFormat ||
		 DXGI_FORMAT_B8G8R8A8_UNORM_SRGB == eBackBufferFormat ||
		 DXGI_FORMAT_B8G8R8X8_UNORM == eBackBufferFormat ||
		 DXGI_FORMAT_B8G8R8X8_UNORM_SRGB == eBackBufferFormat);
	const bool bIsRGBA =
		(DXGI_FORMAT_R8G8B8A8_UNORM == eBackBufferFormat ||
		 DXGI_FORMAT_R8G8B8A8_UNORM_SRGB == eBackBufferFormat);
	if (!bIsBGRA && !bIsRGBA)
	{
		TraceError("DX11_PYGRAPHIC_SCREENSHOT_FAIL reason=unsupported_format format=%u", static_cast<unsigned int>(eBackBufferFormat));
		pDX11BackBuffer->Release();
		return false;
	}

	// For MSAA backbuffers, resolve to non-MS texture before staging readback.
	ID3D11Texture2D* pResolveTex = nullptr;
	ID3D11Texture2D* pReadbackSourceTex = pDX11BackBuffer;
	if (kBackbufferDesc.SampleDesc.Count > 1u)
	{
		D3D11_TEXTURE2D_DESC kResolveDesc = kBackbufferDesc;
		kResolveDesc.Usage = D3D11_USAGE_DEFAULT;
		kResolveDesc.BindFlags = 0;
		kResolveDesc.CPUAccessFlags = 0;
		kResolveDesc.MiscFlags = 0;
		kResolveDesc.SampleDesc.Count = 1u;
		kResolveDesc.SampleDesc.Quality = 0u;

		hDX11Result = pDevice->CreateTexture2D(&kResolveDesc, nullptr, &pResolveTex);
		if (FAILED(hDX11Result) || !pResolveTex)
		{
			TraceError("DX11_PYGRAPHIC_SCREENSHOT_FAIL reason=create_resolve_failed hr=0x%08x", hDX11Result);
			pDX11BackBuffer->Release();
			return false;
		}

		pContext->ResolveSubresource(pResolveTex, 0u, pDX11BackBuffer, 0u, eBackBufferFormat);
		pReadbackSourceTex = pResolveTex;
	}

	// Step 3: Create staging texture for CPU readback
	D3D11_TEXTURE2D_DESC kStagingDesc = kBackbufferDesc;
	kStagingDesc.Usage = D3D11_USAGE_STAGING;
	kStagingDesc.BindFlags = 0;
	kStagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	kStagingDesc.MiscFlags = 0;
	kStagingDesc.SampleDesc.Count = 1u;
	kStagingDesc.SampleDesc.Quality = 0u;

	ID3D11Texture2D* pStagingTex = nullptr;
	hDX11Result = pDevice->CreateTexture2D(&kStagingDesc, nullptr, &pStagingTex);
	if (FAILED(hDX11Result))
	{
		TraceError("DX11_PYGRAPHIC_SCREENSHOT_FAIL reason=create_staging_failed hr=0x%08x", hDX11Result);
		if (pResolveTex)
			pResolveTex->Release();
		pDX11BackBuffer->Release();
		return false;
	}

	// Step 4: Copy backbuffer to staging texture
	pContext->CopyResource(pStagingTex, pReadbackSourceTex);

	// Step 5: Map staging texture to get CPU access
	D3D11_MAPPED_SUBRESOURCE kMapped;
	hDX11Result = pContext->Map(pStagingTex, 0, D3D11_MAP_READ, 0, &kMapped);
	if (FAILED(hDX11Result))
	{
		TraceError("DX11_PYGRAPHIC_SCREENSHOT_FAIL reason=map_failed hr=0x%08x", hDX11Result);
		pStagingTex->Release();
		if (pResolveTex)
			pResolveTex->Release();
		pDX11BackBuffer->Release();
		return false;
	}

	// Step 6: Convert BGRA to RGB and copy to buffer
	uint8_t* pbyDX11Buffer = new uint8_t[uDX11Width * uDX11Height * 3];
	if (!pbyDX11Buffer)
	{
		TraceError("DX11_PYGRAPHIC_SCREENSHOT_FAIL reason=alloc_failed");
		pContext->Unmap(pStagingTex, 0);
		pStagingTex->Release();
		if (pResolveTex)
			pResolveTex->Release();
		pDX11BackBuffer->Release();
		return false;
	}

	const uint8_t* pSrcRow = (const uint8_t*)kMapped.pData;
	uint8_t* pDst = pbyDX11Buffer;

	for (UINT y = 0; y < uDX11Height; ++y)
	{
		const uint8_t* pSrcPixel = pSrcRow;
		for (UINT x = 0; x < uDX11Width; ++x)
		{
			if (bIsBGRA)
			{
				*pDst++ = pSrcPixel[2]; // R
				*pDst++ = pSrcPixel[1]; // G
				*pDst++ = pSrcPixel[0]; // B
			}
			else // RGBA
			{
				*pDst++ = pSrcPixel[0]; // R
				*pDst++ = pSrcPixel[1]; // G
				*pDst++ = pSrcPixel[2]; // B
			}
			pSrcPixel += 4; // Skip alpha
		}
		pSrcRow += kMapped.RowPitch; // Use RowPitch, not width * 4
	}

	// Step 7: Unmap and cleanup
	pContext->Unmap(pStagingTex, 0);
	pStagingTex->Release();
	if (pResolveTex)
		pResolveTex->Release();
	pDX11BackBuffer->Release();

	// Step 8: Save JPEG using existing function
	bool bDX11Saved = SaveJPEG(c_pszFileName, pbyDX11Buffer, uDX11Width, uDX11Height);

	if (pbyDX11Buffer)
	{
		delete[] pbyDX11Buffer;
		pbyDX11Buffer = nullptr;
	}

	if (!bDX11Saved)
	{
		TraceError("DX11_PYGRAPHIC_SCREENSHOT_FAIL reason=jpeg_save_failed file=%s", c_pszFileName);
		return false;
	}

	// Add EXIF tag if screenshot key is active (reuse existing code)
	if (g_isScreenShotKey)
	{
		// UTF-8 → UTF-16 conversion for Unicode path support
		std::wstring wFileName = Utf8ToWide(c_pszFileName);
		FILE* srcFilePtr = _wfopen(wFileName.c_str(), L"rb");
		if (srcFilePtr)
		{
			fseek(srcFilePtr, 0, SEEK_END);
			size_t fileSize = ftell(srcFilePtr);
			fseek(srcFilePtr, 0, SEEK_SET);

			char head[21];
			size_t tailSize = fileSize - sizeof(head);
			char* tail = (char*)malloc(tailSize);

			fread(head, sizeof(head), 1, srcFilePtr);
			fread(tail, tailSize, 1, srcFilePtr);
			fclose(srcFilePtr);

			char imgDesc[64];
			GenScreenShotTag(c_pszFileName, GetCRC32(tail, tailSize), imgDesc, sizeof(imgDesc));

			int imgDescLen = strlen(imgDesc) + 1;

			unsigned char exifHeader[] = {
				0xe1,
				0, // blockLen[1],
				0, // blockLen[0],
				0x45,
				0x78,
				0x69,
				0x66,
				0x0,
				0x0,
				0x49,
				0x49,
				0x2a,
				0x0,
				0x8,
				0x0,
				0x0,
				0x0,
				0x1,
				0x0,
				0xe,
				0x1,
				0x2,
				0x0,
				static_cast<unsigned char>(imgDescLen), // textLen[0],
				0, // textLen[1],
				0, // textLen[2],
				0, // textLen[3],
				0x1a,
				0x0,
				0x0,
				0x0,
				0x0,
				0x0,
				0x0,
				0x0,
			};

			exifHeader[2] = sizeof(exifHeader) + imgDescLen;

			FILE* dstFilePtr = _wfopen(wFileName.c_str(), L"wb");
			if (dstFilePtr)
			{
				fwrite(head, sizeof(head), 1, dstFilePtr);
				fwrite(exifHeader, sizeof(exifHeader), 1, dstFilePtr);
				fwrite(imgDesc, imgDescLen, 1, dstFilePtr);
				fputc(0x00, dstFilePtr);
				fputc(0xff, dstFilePtr);
				fwrite(tail, tailSize, 1, dstFilePtr);
				fclose(dstFilePtr);
			}

			free(tail);
		}
	}

	// Success telemetry
	TraceError("DX11_PYGRAPHIC_SCREENSHOT_OK width=%u height=%u file=%s", uDX11Width, uDX11Height, c_pszFileName);
	return true;
}

void CPythonGraphic::PushState()
{
	TState curState;

	curState.matProj = ms_matProj;
	curState.matView = ms_matView;

	// M2-CHARSELECT-FIX-39: Save DX11 viewport state
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		ID3D11DeviceContext* pContext = pDX11Device->GetContext();
		if (pContext)
		{
			UINT uViewportCount = 1;
			pContext->RSGetViewports(&uViewportCount, &curState.viewportDX11);
		}
		else
		{
			// Initialize with default if no context
			ZeroMemory(&curState.viewportDX11, sizeof(D3D11_VIEWPORT));
			curState.viewportDX11.TopLeftX = 0.0f;
			curState.viewportDX11.TopLeftY = 0.0f;
			curState.viewportDX11.Width = static_cast<float>(ms_iWidth > 0 ? ms_iWidth : 1);
			curState.viewportDX11.Height = static_cast<float>(ms_iHeight > 0 ? ms_iHeight : 1);
			curState.viewportDX11.MinDepth = 0.0f;
			curState.viewportDX11.MaxDepth = 1.0f;
		}
	}
	else
	{
		ZeroMemory(&curState.viewportDX11, sizeof(D3D11_VIEWPORT));
		curState.viewportDX11.TopLeftX = 0.0f;
		curState.viewportDX11.TopLeftY = 0.0f;
		curState.viewportDX11.Width = static_cast<float>(ms_iWidth > 0 ? ms_iWidth : 1);
		curState.viewportDX11.Height = static_cast<float>(ms_iHeight > 0 ? ms_iHeight : 1);
		curState.viewportDX11.MinDepth = 0.0f;
		curState.viewportDX11.MaxDepth = 1.0f;
	}

//STATEMANAGER.SaveTransform(GRP_TS_WORLD, &m_SaveWorldMatrix);

	m_stateStack.push(curState);
	//CCamera::Instance().PushParams();
}

void CPythonGraphic::PopState()
{
	if (m_stateStack.empty())
	{
		assert(!"PythonGraphic::PopState StateStack is EMPTY");
		return;
	}
	
	TState & rState = m_stateStack.top();

//STATEMANAGER.RestoreTransform(GRP_TS_WORLD);
	ms_matProj = rState.matProj;
	ms_matView = rState.matView;

	// M2-CHARSELECT-FIX-39: Restore DX11 viewport state
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		ID3D11DeviceContext* pContext = pDX11Device->GetContext();
		if (pContext)
		{
			pContext->RSSetViewports(1, &rState.viewportDX11);

			static bool s_bLoggedViewportRestore = false;
			if (!s_bLoggedViewportRestore)
			{
				s_bLoggedViewportRestore = true;
				TraceError("CHARSELECT_POPSTATE_VIEWPORT_RESTORE top_left_x=%.3f top_left_y=%.3f width=%.3f height=%.3f",
					rState.viewportDX11.TopLeftX, rState.viewportDX11.TopLeftY,
					rState.viewportDX11.Width, rState.viewportDX11.Height);
			}
		}
	}

	UpdatePipeLineMatrix();

	m_stateStack.pop();
	//CCamera::Instance().PopParams();
}

void CPythonGraphic::RenderImage(CGraphicImageInstance* pImageInstance, float x, float y)
{
	assert(pImageInstance != NULL);

	//SetColorRenderState();
	const CGraphicTexture * c_pTexture = pImageInstance->GetTexturePointer();

	float width = (float) pImageInstance->GetWidth();
	float height = (float) pImageInstance->GetHeight();

	RenderTextureBox(x,
					 y,
					 x + width,
					 y + height,
					 c_pTexture,
					 0.0f,
					 0.5f / width, 
					 0.5f / height, 
					 (width + 0.5f) / width, 
					 (height + 0.5f) / height);
}

void CPythonGraphic::RenderAlphaImage(CGraphicImageInstance* pImageInstance, float x, float y, float aLeft, float aRight)
{
	assert(pImageInstance != NULL);

	const CGraphicTexture * c_pTexture = pImageInstance->GetTexturePointer();

	float width = (float) pImageInstance->GetWidth();
	float height = (float) pImageInstance->GetHeight();

	const DWORD dwLeftColor = GetColor(1.0f, 1.0f, 1.0f, aLeft);
	const DWORD dwRightColor = GetColor(1.0f, 1.0f, 1.0f, aRight);

	RenderTextureBox(x,
					 y,
					 x + width,
					 y + height,
					 c_pTexture,
					 dwLeftColor,
					 dwRightColor,
					 0.0f,
					 0.0f,
					 0.0f,
					 1.0f,
					 1.0f);
}

void CPythonGraphic::RenderCoolTimeBox(float fxCenter, float fyCenter, float fRadius, float fTime)
{
	// M2-UIPY30.B: DX11 cooltime pie rendering using bootstrap UI pipeline
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		if (fTime >= 1.0f)
			return;

		fTime = std::max(0.0f, fTime);

		static DirectX::SimpleMath::Color color = DirectX::SimpleMath::Color(0.0f, 0.0f, 0.0f, 0.5f);
		static DirectX::SimpleMath::Vector2 s_v2BoxPos[8] =
		{
			DirectX::SimpleMath::Vector2(-1.0f, -1.0f),
			DirectX::SimpleMath::Vector2(-1.0f,  0.0f),
			DirectX::SimpleMath::Vector2(-1.0f, +1.0f),
			DirectX::SimpleMath::Vector2( 0.0f, +1.0f),
			DirectX::SimpleMath::Vector2(+1.0f, +1.0f),
			DirectX::SimpleMath::Vector2(+1.0f,  0.0f),
			DirectX::SimpleMath::Vector2(+1.0f, -1.0f),
			DirectX::SimpleMath::Vector2( 0.0f, -1.0f),
		};

		int iTriCount = int(8 - 8.0f * fTime);
		float fLastPercentage = (8 - 8.0f * fTime) - iTriCount;

		// Build CPU-side fan in pixel space first, then expand to triangle list in NDC.
		struct SBootstrapVertex
		{
			float x, y, z;
			float r, g, b, a;
			float u, v;
		};

		std::vector<SBootstrapVertex> fanVertices;
		fanVertices.reserve(11); // center + up to 10 rim points

		SBootstrapVertex centerVertex;
		centerVertex.x = fxCenter;
		centerVertex.y = fyCenter;
		centerVertex.z = 0.0f;
		centerVertex.r = color.x;
		centerVertex.g = color.y;
		centerVertex.b = color.z;
		centerVertex.a = color.w;
		centerVertex.u = 0.0f;
		centerVertex.v = 0.0f;
		fanVertices.push_back(centerVertex);

		SBootstrapVertex edgeVertex;
		edgeVertex.x = fxCenter;
		edgeVertex.y = fyCenter - fRadius;
		edgeVertex.z = 0.0f;
		edgeVertex.r = color.x;
		edgeVertex.g = color.y;
		edgeVertex.b = color.z;
		edgeVertex.a = color.w;
		edgeVertex.u = 0.0f;
		edgeVertex.v = 0.0f;
		fanVertices.push_back(edgeVertex);

		for (int j = 0; j < iTriCount; ++j)
		{
			edgeVertex.x = fxCenter + s_v2BoxPos[j].x * fRadius;
			edgeVertex.y = fyCenter + s_v2BoxPos[j].y * fRadius;
			fanVertices.push_back(edgeVertex);
		}

		if (fLastPercentage > 0.0f)
		{
			DirectX::SimpleMath::Vector2* pv2Pos;
			DirectX::SimpleMath::Vector2* pv2LastPos;

			assert((iTriCount - 1 + 8) % 8 >= 0 && (iTriCount - 1 + 8) % 8 < 8);
			assert((iTriCount + 8) % 8 >= 0 && (iTriCount + 8) % 8 < 8);
			pv2LastPos = &s_v2BoxPos[(iTriCount - 1 + 8) % 8];
			pv2Pos = &s_v2BoxPos[(iTriCount + 8) % 8];

			edgeVertex.x = fxCenter + ((pv2Pos->x - pv2LastPos->x) * fLastPercentage + pv2LastPos->x) * fRadius;
			edgeVertex.y = fyCenter + ((pv2Pos->y - pv2LastPos->y) * fLastPercentage + pv2LastPos->y) * fRadius;
			fanVertices.push_back(edgeVertex);
			++iTriCount;
		}

		if (fanVertices.size() < 3)
			return;

		UINT uBackBufferWidth = 0;
		UINT uBackBufferHeight = 0;
		CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
		if (0 == uBackBufferWidth || 0 == uBackBufferHeight)
		{
			uBackBufferWidth = pDX11Device->GetWidth();
			uBackBufferHeight = pDX11Device->GetHeight();
		}
		if (0 == uBackBufferWidth || 0 == uBackBufferHeight)
		{
			static bool s_bLoggedInvalidBackBuffer = false;
			if (!s_bLoggedInvalidBackBuffer)
			{
				s_bLoggedInvalidBackBuffer = true;
				TraceError("DX11_UIPY_COOLTIME_FAIL reason=invalid_backbuffer_size");
			}
			return;
		}

		const float fInvWidth = 1.0f / static_cast<float>(uBackBufferWidth);
		const float fInvHeight = 1.0f / static_cast<float>(uBackBufferHeight);
		auto ConvertPixelToNDC = [fInvWidth, fInvHeight](float fPixelX, float fPixelY, float* pfOutX, float* pfOutY)
		{
			*pfOutX = (2.0f * fPixelX * fInvWidth) - 1.0f;
			*pfOutY = 1.0f - (2.0f * fPixelY * fInvHeight);
		};

		// Expand fan to explicit triangle list for DX11.
		std::vector<SBootstrapVertex> drawVertices;
		drawVertices.reserve((fanVertices.size() - 2) * 3);
		for (size_t i = 1; i + 1 < fanVertices.size(); ++i)
		{
			SBootstrapVertex v0 = fanVertices[0];
			SBootstrapVertex v1 = fanVertices[i];
			SBootstrapVertex v2 = fanVertices[i + 1];
			ConvertPixelToNDC(v0.x, v0.y, &v0.x, &v0.y);
			ConvertPixelToNDC(v1.x, v1.y, &v1.x, &v1.y);
			ConvertPixelToNDC(v2.x, v2.y, &v2.x, &v2.y);
			drawVertices.push_back(v0);
			drawVertices.push_back(v1);
			drawVertices.push_back(v2);
		}
		if (drawVertices.empty())
			return;

		ID3D11DeviceContext* pContext = pDX11Device->GetContext();
		ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
		ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
		ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
		ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
		ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
		ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();
		ID3D11RasterizerState* pRasterizerState = pDX11Device->GetBootstrapRasterizerState();

		if (!pContext || !pVertexBuffer || !pVertexShader || !pPixelShader || !pInputLayout ||
			!pAlphaBlendState || !pDepthDisableState || !pRasterizerState)
		{
			static bool s_bLoggedResourceFail = false;
			if (!s_bLoggedResourceFail)
			{
				s_bLoggedResourceFail = true;
				TraceError("DX11_UIPY_COOLTIME_FAIL reason=bootstrap_resources_null");
			}
			return;
		}

		pDX11Device->BindMainRenderTargets();

		D3D11_MAPPED_SUBRESOURCE kMapped;
		HRESULT hr = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMapped);
		if (FAILED(hr))
		{
			static bool s_bLoggedMapFail = false;
			if (!s_bLoggedMapFail)
			{
				s_bLoggedMapFail = true;
				TraceError("DX11_UIPY_COOLTIME_FAIL reason=vertex_buffer_map_failed hr=0x%08x", hr);
			}
			return;
		}

		memcpy(kMapped.pData, drawVertices.data(), drawVertices.size() * sizeof(SBootstrapVertex));
		pContext->Unmap(pVertexBuffer, 0);

		UINT uStride = sizeof(SBootstrapVertex);
		UINT uOffset = 0;
		pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
		pContext->IASetInputLayout(pInputLayout);
		pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pContext->VSSetShader(pVertexShader, nullptr, 0);
		pContext->PSSetShader(pPixelShader, nullptr, 0);
		pContext->OMSetDepthStencilState(pDepthDisableState, 0);
		pContext->RSSetState(pRasterizerState);

		float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		pContext->OMSetBlendState(pAlphaBlendState, blendFactor, 0xFFFFFFFF);

		pContext->Draw(static_cast<UINT>(drawVertices.size()), 0);

		// Telemetry (throttled to 5 seconds)
		static DWORD s_dwLastCoolTimeLogTick = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastCoolTimeLogTick || (dwNow - s_dwLastCoolTimeLogTick) >= 5000u)
		{
			s_dwLastCoolTimeLogTick = dwNow;
			TraceError("DX11_UIPY_COOLTIME_DX11 rendered=%d tris=%u", 1, static_cast<unsigned int>(drawVertices.size() / 3));
		}

		return;
	}

	static bool s_bLoggedNoDX11Device = false;
	if (!s_bLoggedNoDX11Device)
	{
		s_bLoggedNoDX11Device = true;
		TraceError("DX11_UIPY_COOLTIME_FAIL reason=no_active_dx11_device");
	}
}

long CPythonGraphic::GenerateColor(float r, float g, float b, float a)
{
	return GetColor(r, g, b, a);
}

void CPythonGraphic::RenderDownButton(float sx, float sy, float ex, float ey)
{
	RenderBox2d(sx, sy, ex, ey);

	SetDiffuseColor(m_darkColor);
	RenderLine2d(sx, sy, ex, sy);
	RenderLine2d(sx, sy, sx, ey);

	SetDiffuseColor(m_lightColor);
	RenderLine2d(sx, ey, ex, ey);
	RenderLine2d(ex, sy, ex, ey);
}

void CPythonGraphic::RenderUpButton(float sx, float sy, float ex, float ey)
{
	RenderBox2d(sx, sy, ex, ey);

	SetDiffuseColor(m_lightColor);
	RenderLine2d(sx, sy, ex, sy);
	RenderLine2d(sx, sy, sx, ey);

	SetDiffuseColor(m_darkColor);
	RenderLine2d(sx, ey, ex, ey);
	RenderLine2d(ex, sy, ex, ey);
}

DWORD CPythonGraphic::GetAvailableMemory()
{
	// Route through shared graphics base helper, which is strict-safe in DX11.
	return CGraphicBase::GetAvailableTextureMemory();
}

CPythonGraphic::CPythonGraphic()
{
	m_lightColor = GetColor(1.0f, 1.0f, 1.0f);
	m_darkColor = GetColor(0.0f, 0.0f, 0.0f);
	
	memset(&m_backupViewport, 0, sizeof(GrpViewport));

	m_fOrthoDepth = 1000.0f;
}

CPythonGraphic::~CPythonGraphic()
{
	Tracef("Python Graphic Clear\n");
}
