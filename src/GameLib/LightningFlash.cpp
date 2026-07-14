#include "StdAfx.h"
#include "LightningFlash.h"

#include "EterLib/ResourceManager.h"
#include "EterLib/GrpDeviceDX11.h"

// Static member initialization
float CLightningFlash::s_fLastTime = 0.0f;

CLightningFlash::CLightningFlash()
{
	__Initialize();
}

CLightningFlash::~CLightningFlash()
{
	Destroy();
}

void CLightningFlash::__Initialize()
{
	m_bFlashActive = false;
	m_fCurrentIntensity = 0.0f;
	m_fFlashTimer = 0.0f;
	m_fFlashDuration = 0.2f;       // Default: 200ms flash
	m_fMaxIntensity = 0.8f;        // Default: 80% brightness

	m_bAutoTrigger = false;
	m_fTimeUntilNextStrike = 0.0f;
	m_fMinInterval = 5.0f;         // Default: min 5 seconds
	m_fMaxInterval = 15.0f;        // Default: max 15 seconds

	m_pLightningTexture = nullptr;
	m_bInitialized = false;
}

void CLightningFlash::Create()
{
	Destroy();

	// Load lightning texture
	CGraphicImage* pImage = (CGraphicImage*)CResourceManager::Instance().GetResourcePointer("d:/ymir work/special/lightning.dds");
	if (pImage)
	{
		m_pLightningTexture = CGraphicImageInstance::New();
		m_pLightningTexture->SetImagePointer(pImage);
	}

	m_bInitialized = true;
	m_bAutoTrigger = false;
	m_fTimeUntilNextStrike = m_fMinInterval;

	TraceError("LIGHTNING_FLASH status=initialized texture=%s",
		pImage ? "loaded" : "not_found (using fallback)");
}

void CLightningFlash::Destroy()
{
	if (m_pLightningTexture)
	{
		CGraphicImageInstance::Delete(m_pLightningTexture);
		m_pLightningTexture = nullptr;
	}

	m_bInitialized = false;
	__Initialize();
}

void CLightningFlash::SetMinInterval(float fSeconds)
{
	m_fMinInterval = std::max(0.5f, fSeconds);
}

void CLightningFlash::SetMaxInterval(float fSeconds)
{
	m_fMaxInterval = std::max(m_fMinInterval, fSeconds);
}

void CLightningFlash::SetFlashDuration(float fSeconds)
{
	m_fFlashDuration = std::max(0.05f, fSeconds);
}

void CLightningFlash::SetFlashIntensity(float fIntensity)
{
	m_fMaxIntensity = std::max(0.0f, std::min(1.0f, fIntensity));
}

void CLightningFlash::SetAutoTrigger(bool bEnable)
{
	m_bAutoTrigger = bEnable;
	if (bEnable && m_fTimeUntilNextStrike <= 0.0f)
	{
		m_fTimeUntilNextStrike = frandom(m_fMinInterval, m_fMaxInterval);
	}
}

void CLightningFlash::TriggerFlash(float fIntensity)
{
	m_bFlashActive = true;
	m_fFlashTimer = 0.0f;
	m_fCurrentIntensity = std::max(0.0f, std::min(1.0f, fIntensity));

	// Reset auto-trigger timer
	if (m_bAutoTrigger)
	{
		m_fTimeUntilNextStrike = frandom(m_fMinInterval, m_fMaxInterval);
	}
}

void CLightningFlash::__TriggerRandomFlash()
{
	// Random intensity between 0.5 and 1.0
	float fIntensity = frandom(0.5f, 1.0f);
	TriggerFlash(fIntensity);
}

void CLightningFlash::Update(float fElapsedTime)
{
	if (!m_bInitialized)
		return;

	// Update flash
	if (m_bFlashActive)
	{
		m_fFlashTimer += fElapsedTime;

		// Calculate intensity (fade out over duration)
		float fFadeProgress = m_fFlashTimer / m_fFlashDuration;
		if (fFadeProgress >= 1.0f)
		{
			m_bFlashActive = false;
			m_fCurrentIntensity = 0.0f;
		}
		else
		{
			// Exponential fade out for more dramatic effect
			m_fCurrentIntensity = m_fMaxIntensity * (1.0f - fFadeProgress);
			m_fCurrentIntensity *= m_fCurrentIntensity; // Square for sharper falloff
		}
	}

	// Auto-trigger logic
	if (m_bAutoTrigger)
	{
		m_fTimeUntilNextStrike -= fElapsedTime;
		if (m_fTimeUntilNextStrike <= 0.0f)
		{
			__TriggerRandomFlash();
		}
	}
}

void CLightningFlash::Render()
{
	if (!m_bInitialized || !m_bFlashActive)
		return;

	if (m_fCurrentIntensity <= 0.01f)
		return;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pContext)
		return;

	// Save old state
	ID3D11BlendState* pOldBlendState = nullptr;
	FLOAT afOldBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	UINT uOldSampleMask = 0u;
	pContext->OMGetBlendState(&pOldBlendState, afOldBlendFactor, &uOldSampleMask);

	ID3D11DepthStencilState* pOldDepthState = nullptr;
	UINT uOldStencilRef = 0u;
	pContext->OMGetDepthStencilState(&pOldDepthState, &uOldStencilRef);

	ID3D11RasterizerState* pOldRasterState = nullptr;
	pContext->RSGetState(&pOldRasterState);

	ID3D11Buffer* pOldVertexBuffer = nullptr;
	UINT uOldStride = 0u;
	UINT uOldOffset = 0u;
	pContext->IAGetVertexBuffers(0, 1, &pOldVertexBuffer, &uOldStride, &uOldOffset);

	D3D11_PRIMITIVE_TOPOLOGY eOldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	pContext->IAGetPrimitiveTopology(&eOldTopology);

	// Get bootstrap resources
	ID3D11Buffer* pBootstrapVB = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11InputLayout* pBootstrapIL = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11VertexShader* pBootstrapVS = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pBootstrapColorPS = pDX11Device->GetBootstrapColorPixelShader();
	ID3D11BlendState* pBootstrapAdditiveBlendState = pDX11Device->GetBootstrapUIAdditiveBlendState();
	ID3D11DepthStencilState* pBootstrapDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();

	if (pBootstrapVB && pBootstrapIL && pBootstrapVS && pBootstrapColorPS &&
		pBootstrapAdditiveBlendState && pBootstrapDepthDisableState)
	{
		// Vertex structure for full-screen quad
		struct SFullScreenVertex
		{
			float x, y, z;
			float r, g, b, a;
		};

		// Full-screen quad in NDC (normalized device coordinates)
		SFullScreenVertex avQuadVertices[4] =
		{
			{ -1.0f,  1.0f, 0.5f, 1.0f, 1.0f, 1.0f, m_fCurrentIntensity }, // Top-left
			{  1.0f,  1.0f, 0.5f, 1.0f, 1.0f, 1.0f, m_fCurrentIntensity }, // Top-right
			{ -1.0f, -1.0f, 0.5f, 1.0f, 1.0f, 1.0f, m_fCurrentIntensity }, // Bottom-left
			{  1.0f, -1.0f, 0.5f, 1.0f, 1.0f, 1.0f, m_fCurrentIntensity }  // Bottom-right
		};

		const UINT uStride = sizeof(SFullScreenVertex);
		const UINT uOffset = 0u;
		const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		pContext->IASetInputLayout(pBootstrapIL);
		pContext->IASetVertexBuffers(0, 1, &pBootstrapVB, &uStride, &uOffset);
		pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		pContext->VSSetShader(pBootstrapVS, nullptr, 0);
		pContext->PSSetShader(pBootstrapColorPS, nullptr, 0);
		pContext->OMSetBlendState(pBootstrapAdditiveBlendState, afBlendFactor, 0xffffffffu);
		pContext->OMSetDepthStencilState(pBootstrapDepthDisableState, 0);
		pContext->RSSetState(nullptr);

		D3D11_MAPPED_SUBRESOURCE kMappedResource;
		ZeroMemory(&kMappedResource, sizeof(kMappedResource));
		HRESULT hMapResult = pContext->Map(pBootstrapVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
		if (SUCCEEDED(hMapResult) && kMappedResource.pData)
		{
			memcpy(kMappedResource.pData, avQuadVertices, sizeof(avQuadVertices));
			pContext->Unmap(pBootstrapVB, 0);
			pContext->Draw(4, 0);
		}
		else
		{
			TraceError("DX11_LIGHTNING_MAP_FAIL hr=0x%08x", static_cast<unsigned int>(hMapResult));
		}
	}
	else
	{
		static bool s_bLoggedBootstrapMissing = false;
		if (!s_bLoggedBootstrapMissing)
		{
			s_bLoggedBootstrapMissing = true;
			TraceError("DX11_LIGHTNING_PATH status=deferred reason=bootstrap_resources_missing");
		}
	}

	// Restore state
	pContext->RSSetState(pOldRasterState);
	pContext->OMSetDepthStencilState(pOldDepthState, uOldStencilRef);
	pContext->OMSetBlendState(pOldBlendState, afOldBlendFactor, uOldSampleMask);
	pContext->IASetVertexBuffers(0, 1, &pOldVertexBuffer, &uOldStride, &uOldOffset);
	pContext->IASetPrimitiveTopology(eOldTopology);

	SAFE_RELEASE(pOldVertexBuffer);
	SAFE_RELEASE(pOldRasterState);
	SAFE_RELEASE(pOldDepthState);
	SAFE_RELEASE(pOldBlendState);
}
