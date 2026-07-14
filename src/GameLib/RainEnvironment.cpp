#include "StdAfx.h"
#include "RainEnvironment.h"

#include "EterLib/Camera.h"
#include "EterLib/ResourceManager.h"
#include "EterLib/GrpDeviceDX11.h"
#include "RainParticle.h"

// Enable rain environment
void CRainEnvironment::Enable()
{
	if (!m_bRainEnable)
	{
		Create();
	}

	m_bRainEnable = TRUE;
}

// Disable rain environment
void CRainEnvironment::Disable()
{
	m_bRainEnable = FALSE;
}

// Update spawn center position
void CRainEnvironment::Update(const DirectX::SimpleMath::Vector3 & c_rv3Pos)
{
	if (!m_bRainEnable)
	{
		if (m_kVct_pkParticleRain.empty())
			return;
	}

	m_v3Center = c_rv3Pos;
}

// Animate rain particles (called each frame)
void CRainEnvironment::Deform()
{
	if (!m_bRainEnable)
	{
		if (m_kVct_pkParticleRain.empty())
			return;
	}

	const DirectX::SimpleMath::Vector3 & c_rv3Pos = m_v3Center;

	// Calculate delta time
	static long s_lLastTime = CTimer::Instance().GetCurrentMillisecond();
	long lcurTime = CTimer::Instance().GetCurrentMillisecond();
	float fElapsedTime = float(lcurTime - s_lLastTime) / 1000.0f;
	s_lLastTime = lcurTime;

	// Get camera
	CCamera * pCamera = CCameraManager::Instance().GetCurrentCamera();
	if (!pCamera)
		return;

	const DirectX::SimpleMath::Vector3 & c_rv3View = pCamera->GetView();

	// Calculate spawn position (in front of camera, same as snow)
	DirectX::SimpleMath::Vector3 v3ChangedPos = c_rv3View * 3500.0f + c_rv3Pos;
	v3ChangedPos.z = c_rv3Pos.z;

	// Calculate wind velocity
	DirectX::SimpleMath::Vector3 v3WindVelocity = m_v3Wind * m_fWindInfluence;

	// Update all particles
	std::vector<CRainParticle*>::iterator itor = m_kVct_pkParticleRain.begin();
	for (; itor != m_kVct_pkParticleRain.end();)
	{
		CRainParticle * pRain = *itor;
		pRain->Update(fElapsedTime, v3ChangedPos, v3WindVelocity);

		if (!pRain->IsActivate())
		{
			CRainParticle::Delete(pRain);
			itor = m_kVct_pkParticleRain.erase(itor);
		}
		else
		{
			++itor;
		}
	}

	// Spawn new particles (higher rate than snow: 25 vs 10)
	if (m_bRainEnable)
	{
		for (int p = 0; p < std::min((size_t)m_iSpawnRate, m_dwParticleMaxNum - m_kVct_pkParticleRain.size()); ++p)
		{
			CRainParticle * pRainParticle = CRainParticle::New();
			pRainParticle->Init(v3ChangedPos, m_fFallSpeedMin, m_fFallSpeedMax, m_fParticleWidth, m_fParticleHeight, m_fSpawnRadius);
			m_kVct_pkParticleRain.push_back(pRainParticle);
		}
	}
}

// Blur stubs (disabled in DX11, same as snow)
void CRainEnvironment::__BeginBlur()
{
	// Blur accumulation is disabled in native DX11 path.
}

void CRainEnvironment::__ApplyBlur()
{
	// Blur accumulation is disabled in native DX11 path.
}

// Render rain particles with DX11 bootstrap pipeline
void CRainEnvironment::Render()
{
	if (!m_bRainEnable)
	{
		if (m_kVct_pkParticleRain.empty())
			return;
	}

	DWORD dwParticleCount = std::min((size_t)m_dwParticleMaxNum, m_kVct_pkParticleRain.size());
	if (0 == dwParticleCount)
		return;

	CCamera * pCamera = CCameraManager::Instance().GetCurrentCamera();
	if (!pCamera)
		return;

	const DirectX::SimpleMath::Vector3 & c_rv3Up = pCamera->GetUp();
	const DirectX::SimpleMath::Vector3 & c_rv3Cross = pCamera->GetCross();

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	const bool bDX11RuntimeActive = (pDX11Device && pDX11Device->IsValid());
	if (bDX11RuntimeActive && pDX11Device->EnsureBootstrapPipelineReady() && pDX11Device->EnsureBootstrapUISamplerReady())
	{
		ID3D11DeviceContext* pContext = pDX11Device->GetContext();
		if (pContext && m_pImageInstance && m_pImageInstance->GetGraphicImagePointer())
		{
			const CGraphicTexture& rkRainTexture = m_pImageInstance->GetGraphicImagePointer()->GetTextureReference();
			ID3D11ShaderResourceView* pRainSRV = rkRainTexture.GetD3D11TextureSRV();
			if (pRainSRV)
			{
				// Bootstrap vertex structure
				struct SBootstrapVertex
				{
					float x, y, z;
					float r, g, b, a;
					float u, v;
				};

				// Save all DX11 state
				ID3D11BlendState* pOldBlendState = nullptr;
				FLOAT afOldBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				UINT uOldSampleMask = 0u;
				pContext->OMGetBlendState(&pOldBlendState, afOldBlendFactor, &uOldSampleMask);

				ID3D11DepthStencilState* pOldDepthState = nullptr;
				UINT uOldStencilRef = 0u;
				pContext->OMGetDepthStencilState(&pOldDepthState, &uOldStencilRef);

				ID3D11RasterizerState* pOldRasterState = nullptr;
				pContext->RSGetState(&pOldRasterState);

				ID3D11InputLayout* pOldInputLayout = nullptr;
				pContext->IAGetInputLayout(&pOldInputLayout);

				ID3D11Buffer* pOldVertexBuffer = nullptr;
				UINT uOldStride = 0u;
				UINT uOldOffset = 0u;
				pContext->IAGetVertexBuffers(0, 1, &pOldVertexBuffer, &uOldStride, &uOldOffset);

				D3D11_PRIMITIVE_TOPOLOGY eOldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
				pContext->IAGetPrimitiveTopology(&eOldTopology);

				ID3D11VertexShader* pOldVertexShader = nullptr;
				pContext->VSGetShader(&pOldVertexShader, nullptr, nullptr);

				ID3D11PixelShader* pOldPixelShader = nullptr;
				pContext->PSGetShader(&pOldPixelShader, nullptr, nullptr);

				ID3D11ShaderResourceView* pOldSRV0 = nullptr;
				pContext->PSGetShaderResources(0, 1, &pOldSRV0);

				ID3D11SamplerState* pOldSampler0 = nullptr;
				pContext->PSGetSamplers(0, 1, &pOldSampler0);

				// Get bootstrap resources
				ID3D11Buffer* pBootstrapVB = pDX11Device->GetBootstrapUIVertexBuffer();
				ID3D11InputLayout* pBootstrapIL = pDX11Device->GetBootstrapUIInputLayout();
				ID3D11VertexShader* pBootstrapVS = pDX11Device->GetBootstrapUIVertexShader();
				ID3D11PixelShader* pBootstrapTexturePS = pDX11Device->GetBootstrapUITexturePixelShader();
				ID3D11BlendState* pBootstrapAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
				ID3D11DepthStencilState* pBootstrapDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();
				ID3D11SamplerState* pBootstrapSampler = pDX11Device->GetBootstrapUISamplerState();

				if (pBootstrapVB && pBootstrapIL && pBootstrapVS && pBootstrapTexturePS && pBootstrapAlphaBlendState && pBootstrapDepthDisableState && pBootstrapSampler)
				{
					const UINT kVerticesPerParticle = 6u;
					const UINT kBootstrapVertexCapacity = 4096u;
					const UINT kMaxParticlesPerDraw = std::max<UINT>(1u, kBootstrapVertexCapacity / kVerticesPerParticle);
					const WORD c_awQuadIndices[kVerticesPerParticle] = { 0, 2, 1, 2, 3, 1 };
					const D3DXMATRIX kMatViewProj = ms_matView * ms_matProj;
					const FLOAT afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

					UINT uStride = sizeof(SBootstrapVertex);
					UINT uOffset = 0u;
					pContext->IASetInputLayout(pBootstrapIL);
					pContext->IASetVertexBuffers(0, 1, &pBootstrapVB, &uStride, &uOffset);
					pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					pContext->VSSetShader(pBootstrapVS, nullptr, 0);
					pContext->PSSetShader(pBootstrapTexturePS, nullptr, 0);
					pContext->PSSetSamplers(0, 1, &pBootstrapSampler);
					pContext->PSSetShaderResources(0, 1, &pRainSRV);
					pContext->OMSetBlendState(pBootstrapAlphaBlendState, afBlendFactor, 0xffffffffu);
					pContext->OMSetDepthStencilState(pBootstrapDepthDisableState, 0);
					pContext->RSSetState(nullptr);

					// Batch rendering (max 682 particles per draw call)
					UINT uParticleCursor = 0u;
					while (uParticleCursor < dwParticleCount)
					{
						const UINT uBatchParticleCount = std::min<UINT>(kMaxParticlesPerDraw, dwParticleCount - uParticleCursor);
						std::vector<SBootstrapVertex> kVertices;
						kVertices.resize(uBatchParticleCount * kVerticesPerParticle);

						// Build vertices for each particle
						for (UINT i = 0; i < uBatchParticleCount; ++i)
						{
							CRainParticle* pRain = m_kVct_pkParticleRain[uParticleCursor + i];
							if (!pRain)
								continue;

							// Get camera-facing quad vertices
							SParticleVertex aQuadVertices[4];
							pRain->SetCameraVertex(c_rv3Up, c_rv3Cross);
							pRain->GetVerticies(aQuadVertices[0], aQuadVertices[1], aQuadVertices[2], aQuadVertices[3]);

							// Transform to NDC and build bootstrap vertices
							for (UINT j = 0; j < kVerticesPerParticle; ++j)
							{
								const SParticleVertex& rkSourceVertex = aQuadVertices[c_awQuadIndices[j]];
								D3DXVECTOR4 v4World(rkSourceVertex.v3Pos.x, rkSourceVertex.v3Pos.y, rkSourceVertex.v3Pos.z, 1.0f);
								D3DXVECTOR4 v4Clip;
								D3DXVec4Transform(&v4Clip, &v4World, &kMatViewProj);

								float fNdcX = 0.0f;
								float fNdcY = 0.0f;
								float fNdcZ = 0.0f;
								if (fabsf(v4Clip.w) > 1.0e-6f)
								{
									const float fInvW = 1.0f / v4Clip.w;
									fNdcX = v4Clip.x * fInvW;
									fNdcY = v4Clip.y * fInvW;
									fNdcZ = v4Clip.z * fInvW;
								}

								SBootstrapVertex& rkDestVertex = kVertices[i * kVerticesPerParticle + j];
								rkDestVertex.x = fNdcX;
								rkDestVertex.y = fNdcY;
								rkDestVertex.z = fNdcZ;
								rkDestVertex.r = 1.0f;
								rkDestVertex.g = 1.0f;
								rkDestVertex.b = 1.0f;
								rkDestVertex.a = 1.0f;
								rkDestVertex.u = rkSourceVertex.u;
								rkDestVertex.v = rkSourceVertex.v;
							}
						}

						// Upload and draw
						D3D11_MAPPED_SUBRESOURCE kMappedResource;
						ZeroMemory(&kMappedResource, sizeof(kMappedResource));
						const HRESULT hMapResult = pContext->Map(pBootstrapVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
						if (FAILED(hMapResult) || !kMappedResource.pData)
						{
							TraceError("DX11_RAIN_MAP_FAIL hr=0x%08x", static_cast<unsigned int>(hMapResult));
							break;
						}

						memcpy(kMappedResource.pData, &kVertices[0], sizeof(SBootstrapVertex) * kVertices.size());
						pContext->Unmap(pBootstrapVB, 0);
						pContext->Draw(static_cast<UINT>(kVertices.size()), 0);

						uParticleCursor += uBatchParticleCount;
					}

					ID3D11ShaderResourceView* pNullSRV = nullptr;
					pContext->PSSetShaderResources(0, 1, &pNullSRV);

					static bool s_bLoggedDX11RainNativePath = false;
					if (!s_bLoggedDX11RainNativePath)
					{
						s_bLoggedDX11RainNativePath = true;
						TraceError("DX11_RAIN_PATH mode=dx11_native_draw status=active");
					}
				}
				else
				{
					static bool s_bLoggedDX11RainBootstrapMissing = false;
					if (!s_bLoggedDX11RainBootstrapMissing)
					{
						s_bLoggedDX11RainBootstrapMissing = true;
						TraceError("DX11_RAIN_PATH mode=dx11_native_draw status=deferred reason=bootstrap_resources_missing");
					}
				}

				// Restore all DX11 state
				pContext->RSSetState(pOldRasterState);
				pContext->OMSetDepthStencilState(pOldDepthState, uOldStencilRef);
				pContext->OMSetBlendState(pOldBlendState, afOldBlendFactor, uOldSampleMask);
				pContext->IASetInputLayout(pOldInputLayout);
				pContext->IASetVertexBuffers(0, 1, &pOldVertexBuffer, &uOldStride, &uOldOffset);
				pContext->IASetPrimitiveTopology(eOldTopology);
				pContext->VSSetShader(pOldVertexShader, nullptr, 0);
				pContext->PSSetShader(pOldPixelShader, nullptr, 0);
				pContext->PSSetShaderResources(0, 1, &pOldSRV0);
				pContext->PSSetSamplers(0, 1, &pOldSampler0);

				SAFE_RELEASE(pOldSampler0);
				SAFE_RELEASE(pOldSRV0);
				SAFE_RELEASE(pOldPixelShader);
				SAFE_RELEASE(pOldVertexShader);
				SAFE_RELEASE(pOldVertexBuffer);
				SAFE_RELEASE(pOldInputLayout);
				SAFE_RELEASE(pOldRasterState);
				SAFE_RELEASE(pOldDepthState);
				SAFE_RELEASE(pOldBlendState);

				return;
			}
		}
	}

	static bool s_bLoggedDX11RainNoFallback = false;
	if (!s_bLoggedDX11RainNoFallback)
	{
		s_bLoggedDX11RainNoFallback = true;
		TraceError("DX11_RAIN_PATH mode=dx11_native_draw status=deferred reason=native_resources_unavailable no_legacy_fallback=1");
	}
}

// Create rain environment resources
bool CRainEnvironment::Create()
{
	Destroy();

	if (!__CreateBlurTexture())
		return false;

	if (!__CreateGeometry())
		return false;

	// Load rain texture (create this file!)
	CGraphicImage * pImage = (CGraphicImage *)CResourceManager::Instance().GetResourcePointer("d:/ymir work/special/rain.dds");
	m_pImageInstance = CGraphicImageInstance::New();
	m_pImageInstance->SetImagePointer(pImage);

	return true;
}

// Destroy rain environment resources
void CRainEnvironment::Destroy()
{
	SAFE_RELEASE(m_lpOldSurface);
	SAFE_RELEASE(m_lpOldDepthStencilSurface);
	SAFE_RELEASE(m_lpRainTexture);
	SAFE_RELEASE(m_lpRainRenderTargetSurface);
	SAFE_RELEASE(m_lpRainDepthSurface);
	SAFE_RELEASE(m_lpAccumTexture);
	SAFE_RELEASE(m_lpAccumRenderTargetSurface);
	SAFE_RELEASE(m_lpAccumDepthSurface);
	SAFE_RELEASE(m_pVB);
	SAFE_RELEASE(m_pIB);

	stl_wipe(m_kVct_pkParticleRain);
	CRainParticle::DestroyPool();

	if (m_pImageInstance)
	{
		CGraphicImageInstance::Delete(m_pImageInstance);
		m_pImageInstance = NULL;
	}

	__Initialize();
}

// Initialize members
void CRainEnvironment::__Initialize()
{
	m_bRainEnable = FALSE;
	m_lpOldSurface = NULL;
	m_lpOldDepthStencilSurface = NULL;
	m_lpRainTexture = NULL;
	m_lpRainRenderTargetSurface = NULL;
	m_lpRainDepthSurface = NULL;
	m_lpAccumTexture = NULL;
	m_lpAccumRenderTargetSurface = NULL;
	m_lpAccumDepthSurface = NULL;
	m_pVB = NULL;
	m_pIB = NULL;
	m_pImageInstance = NULL;

	m_kVct_pkParticleRain.reserve(m_dwParticleMaxNum);
}

// Blur texture creation (stub in DX11, same as snow)
bool CRainEnvironment::__CreateBlurTexture()
{
	// Blur texture creation is disabled in native DX11 path
	return true;
}

// Geometry creation (stub in DX11, same as snow)
bool CRainEnvironment::__CreateGeometry()
{
	// Geometry creation is disabled in native DX11 path
	return true;
}

// Set particle size
void CRainEnvironment::SetParticleSize(float fWidth, float fHeight)
{
	m_fParticleWidth = fWidth;
	m_fParticleHeight = fHeight;
}

// Set particle count
void CRainEnvironment::SetParticleCount(DWORD dwCount)
{
	m_dwParticleMaxNum = dwCount;
	m_kVct_pkParticleRain.reserve(m_dwParticleMaxNum);
}

// Constructor
CRainEnvironment::CRainEnvironment()
{
	m_bBlurEnable = FALSE;

	// Default configuration (configurable, unlike snow!)
	m_dwParticleMaxNum = 8000;       // More particles than snow (3000)
	m_fFallSpeedMin = 500.0f;        // Much faster than snow (50.0f)
	m_fFallSpeedMax = 1500.0f;       // Much faster than snow (200.0f)
	m_fWindInfluence = 1.0f;         // Wind influence (snow has none)
	m_iSpawnRate = 25;               // Higher spawn rate than snow (10)
	m_fParticleWidth = 2.0f;         // Thinner than snow (4.0f)
	m_fParticleHeight = 15.0f;       // Stretched vertically (snow is square)
	m_fSpawnRadius = 70000.0f;       // Same as snow
	m_v3Wind = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);  // No wind by default
	m_wBlurTextureSize = 512;

	__Initialize();
}

// Destructor
CRainEnvironment::~CRainEnvironment()
{
	Destroy();
}
