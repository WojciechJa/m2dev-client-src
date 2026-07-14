#include "StdAfx.h"
#include "SnowEnvironment.h"

#include "EterLib/Camera.h"
#include "EterLib/ResourceManager.h"
#include "EterLib/GrpDeviceDX11.h"
#include "SnowParticle.h"

void CSnowEnvironment::Enable()
{
	if (!m_bSnowEnable)
	{
		Create();
	}

	m_bSnowEnable = TRUE;
}

void CSnowEnvironment::Disable()
{
	m_bSnowEnable = FALSE;
}

void CSnowEnvironment::Update(const DirectX::SimpleMath::Vector3 & c_rv3Pos)
{
	if (!m_bSnowEnable)
	{
		if (m_kVct_pkParticleSnow.empty())
			return;
	}

	m_v3Center=c_rv3Pos;
}

void CSnowEnvironment::Deform()
{
	if (!m_bSnowEnable)
	{
		if (m_kVct_pkParticleSnow.empty())
			return;
	}

	const DirectX::SimpleMath::Vector3 & c_rv3Pos=m_v3Center;

	static long s_lLastTime = CTimer::Instance().GetCurrentMillisecond();
	long lcurTime = CTimer::Instance().GetCurrentMillisecond();
	float fElapsedTime = float(lcurTime - s_lLastTime) / 1000.0f;
	s_lLastTime = lcurTime;

	CCamera * pCamera = CCameraManager::Instance().GetCurrentCamera();
	if (!pCamera)
		return;

	const DirectX::SimpleMath::Vector3 & c_rv3View = pCamera->GetView();

	DirectX::SimpleMath::Vector3 v3ChangedPos = c_rv3View * 3500.0f + c_rv3Pos;
	v3ChangedPos.z = c_rv3Pos.z;

	std::vector<CSnowParticle*>::iterator itor = m_kVct_pkParticleSnow.begin();
	for (; itor != m_kVct_pkParticleSnow.end();)
	{
		CSnowParticle * pSnow = *itor;
		pSnow->Update(fElapsedTime, v3ChangedPos);

		if (!pSnow->IsActivate())
		{
			CSnowParticle::Delete(pSnow);

			itor = m_kVct_pkParticleSnow.erase(itor);
		}
		else
		{
			++itor;
		}
	}

	if (m_bSnowEnable)
	{
		for (int p = 0; p < std::min(10ull, m_dwParticleMaxNum - m_kVct_pkParticleSnow.size()); ++p)
		{
			CSnowParticle * pSnowParticle = CSnowParticle::New();
			pSnowParticle->Init(v3ChangedPos, m_fFallSpeedMin, m_fFallSpeedMax, m_fParticleSize);
			m_kVct_pkParticleSnow.push_back(pSnowParticle);
		}
	}
}

void CSnowEnvironment::__BeginBlur()
{
	// Blur accumulation is disabled in native DX11 path.
}

void CSnowEnvironment::__ApplyBlur()
{
	// Blur accumulation is disabled in native DX11 path.
}

void CSnowEnvironment::Render()
{
	if (!m_bSnowEnable)
	{
		if (m_kVct_pkParticleSnow.empty())
			return;
	}

	DWORD dwParticleCount = std::min((size_t)m_dwParticleMaxNum, m_kVct_pkParticleSnow.size());
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
			const CGraphicTexture& rkSnowTexture = m_pImageInstance->GetGraphicImagePointer()->GetTextureReference();
			ID3D11ShaderResourceView* pSnowSRV = rkSnowTexture.GetD3D11TextureSRV();
			if (pSnowSRV)
			{
				struct SBootstrapVertex
				{
					float x, y, z;
					float r, g, b, a;
					float u, v;
				};

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
					pContext->PSSetShaderResources(0, 1, &pSnowSRV);
					pContext->OMSetBlendState(pBootstrapAlphaBlendState, afBlendFactor, 0xffffffffu);
					pContext->OMSetDepthStencilState(pBootstrapDepthDisableState, 0);
					pContext->RSSetState(nullptr);

					UINT uParticleCursor = 0u;
					while (uParticleCursor < dwParticleCount)
					{
						const UINT uBatchParticleCount = std::min<UINT>(kMaxParticlesPerDraw, dwParticleCount - uParticleCursor);
						std::vector<SBootstrapVertex> kVertices;
						kVertices.resize(uBatchParticleCount * kVerticesPerParticle);

						for (UINT i = 0; i < uBatchParticleCount; ++i)
						{
							CSnowParticle* pSnow = m_kVct_pkParticleSnow[uParticleCursor + i];
							if (!pSnow)
								continue;

							SParticleVertex aQuadVertices[4];
							pSnow->SetCameraVertex(c_rv3Up, c_rv3Cross);
							pSnow->GetVerticies(aQuadVertices[0], aQuadVertices[1], aQuadVertices[2], aQuadVertices[3]);

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

						D3D11_MAPPED_SUBRESOURCE kMappedResource;
						ZeroMemory(&kMappedResource, sizeof(kMappedResource));
						const HRESULT hMapResult = pContext->Map(pBootstrapVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
						if (FAILED(hMapResult) || !kMappedResource.pData)
						{
							TraceError("DX11_SNOW_MAP_FAIL hr=0x%08x", static_cast<unsigned int>(hMapResult));
							break;
						}

						memcpy(kMappedResource.pData, &kVertices[0], sizeof(SBootstrapVertex) * kVertices.size());
						pContext->Unmap(pBootstrapVB, 0);
						pContext->Draw(static_cast<UINT>(kVertices.size()), 0);

						uParticleCursor += uBatchParticleCount;
					}

					ID3D11ShaderResourceView* pNullSRV = nullptr;
					pContext->PSSetShaderResources(0, 1, &pNullSRV);

					static bool s_bLoggedDX11SnowNativePath = false;
					if (!s_bLoggedDX11SnowNativePath)
					{
						s_bLoggedDX11SnowNativePath = true;
						TraceError("DX11_SNOW_PATH mode=dx11_native_draw status=active");
					}
				}
				else
				{
					static bool s_bLoggedDX11SnowBootstrapMissing = false;
					if (!s_bLoggedDX11SnowBootstrapMissing)
					{
						s_bLoggedDX11SnowBootstrapMissing = true;
						TraceError("DX11_SNOW_PATH mode=dx11_native_draw status=deferred reason=bootstrap_resources_missing");
					}
				}

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

	static bool s_bLoggedDX11SnowNoFallback = false;
	if (!s_bLoggedDX11SnowNoFallback)
	{
		s_bLoggedDX11SnowNoFallback = true;
		TraceError("DX11_SNOW_PATH mode=dx11_native_draw status=deferred reason=native_resources_unavailable no_legacy_fallback=1");
	}
}

bool CSnowEnvironment::__CreateBlurTexture()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	return (pDX11Device && pDX11Device->IsValid());
}

bool CSnowEnvironment::__CreateGeometry()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	return (pDX11Device && pDX11Device->IsValid());
}

bool CSnowEnvironment::Create()
{
	Destroy();

	if (!__CreateBlurTexture())
		return false;

	if (!__CreateGeometry())
		return false;

	CGraphicImage * pImage = (CGraphicImage *)CResourceManager::Instance().GetResourcePointer("d:/ymir work/special/snow.dds");
	m_pImageInstance = CGraphicImageInstance::New();
	m_pImageInstance->SetImagePointer(pImage);

	return true;
}

void CSnowEnvironment::Destroy()
{
	SAFE_RELEASE(m_lpOldSurface);
	SAFE_RELEASE(m_lpOldDepthStencilSurface);
	SAFE_RELEASE(m_lpSnowTexture);
	SAFE_RELEASE(m_lpSnowRenderTargetSurface);
	SAFE_RELEASE(m_lpSnowDepthSurface);
	SAFE_RELEASE(m_lpAccumTexture);
	SAFE_RELEASE(m_lpAccumRenderTargetSurface);
	SAFE_RELEASE(m_lpAccumDepthSurface);
	SAFE_RELEASE(m_pVB);
	SAFE_RELEASE(m_pIB);

	stl_wipe(m_kVct_pkParticleSnow);
	CSnowParticle::DestroyPool();

	if (m_pImageInstance)
	{
		CGraphicImageInstance::Delete(m_pImageInstance);
		m_pImageInstance = NULL;
	}

	__Initialize();
}

void CSnowEnvironment::__Initialize()
{
	m_bSnowEnable = FALSE;
	m_lpOldSurface = NULL;
	m_lpOldDepthStencilSurface = NULL;
	m_lpSnowTexture = NULL;
	m_lpSnowRenderTargetSurface = NULL;
	m_lpSnowDepthSurface = NULL;
	m_lpAccumTexture = NULL;
	m_lpAccumRenderTargetSurface = NULL;
	m_lpAccumDepthSurface = NULL;
	m_pVB = NULL;
	m_pIB = NULL;
	m_pImageInstance = NULL;

	m_kVct_pkParticleSnow.reserve(m_dwParticleMaxNum);
}

CSnowEnvironment::CSnowEnvironment()
{
	m_bBlurEnable = FALSE;

	// Default configuration (now configurable!)
	m_dwParticleMaxNum = 3000;
	m_fFallSpeedMin = 50.0f;      // Default: 50 units/sec
	m_fFallSpeedMax = 200.0f;     // Default: 200 units/sec
	m_fParticleSize = 7.0f;       // Default: 7 units (half-width, same as old default)
	m_wBlurTextureSize = 512;

	__Initialize();
}

// Set particle count
void CSnowEnvironment::SetParticleCount(DWORD dwCount)
{
	m_dwParticleMaxNum = dwCount;
	m_kVct_pkParticleSnow.reserve(m_dwParticleMaxNum);
}

// Set fall speed range
void CSnowEnvironment::SetFallSpeedMin(float fSpeed)
{
	m_fFallSpeedMin = fSpeed;
}

void CSnowEnvironment::SetFallSpeedMax(float fSpeed)
{
	m_fFallSpeedMax = fSpeed;
}

// Set particle size
void CSnowEnvironment::SetParticleSize(float fSize)
{
	m_fParticleSize = fSize;
}

CSnowEnvironment::~CSnowEnvironment()
{
	Destroy();
}
