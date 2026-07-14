#include "stdafx.h"
#include "EterLib/Camera.h"
#include "EterLib/GrpDeviceDX11.h"
#include "EterLib/GrpTextureDX11.h"

#include "FlyingData.h"
#include "FlyTrace.h"

CDynamicPool<CFlyTrace>		CFlyTrace::ms_kPool;		

struct TFlyVertex
{
	D3DXVECTOR3 p;
	DWORD c;
	D3DXVECTOR2 t;
	TFlyVertex(){};
	TFlyVertex(const D3DXVECTOR3& p, DWORD c, const D3DXVECTOR2 & t):p(p),c(c),t(t){}
};

struct TFlyVertexSet
{
	TFlyVertex v[6];
	TFlyVertexSet(TFlyVertex * pv)
	{
		memcpy(v,pv,sizeof(v));
	}
	bool operator < (const TFlyVertexSet& ) const
	{
		return false;
	}
	TFlyVertexSet & operator = ( const TFlyVertexSet& rhs )
	{
		memcpy(v,rhs.v,sizeof(v));
		return *this;
	}
};

typedef std::vector<std::pair<float, TFlyVertexSet> > TFlyVertexSetVector;

namespace
{
struct SDX11TraceVertex
{
	float x, y, z;
	float r, g, b, a;
	float u, v;
};

inline void UnpackColorARGB(DWORD dwColor, float& fR, float& fG, float& fB, float& fA)
{
	fA = ((dwColor >> 24) & 0xFF) / 255.0f;
	fR = ((dwColor >> 16) & 0xFF) / 255.0f;
	fG = ((dwColor >> 8) & 0xFF) / 255.0f;
	fB = (dwColor & 0xFF) / 255.0f;
}

inline bool RenderTraceStripsDX11(CGraphicDeviceDX11* pDX11Device, const TFlyVertexSetVector& rkStrips)
{
	if (!pDX11Device || !pDX11Device->IsValid())
		return false;

	if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
		return false;

	ID3D11Device* pDevice = pDX11Device->GetDevice();
	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pDevice || !pContext)
		return false;

	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11BlendState* pBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	ID3D11ShaderResourceView* pWhiteSRV = CGraphicTextureDX11::GetWhiteFallbackTexture(pDevice);
	if (!pInputLayout || !pVertexShader || !pPixelShader || !pVertexBuffer || !pBlendState || !pDepthDisableState || !pSamplerState || !pWhiteSRV)
		return false;

	ID3D11BlendState* pOldBlendState = nullptr;
	FLOAT afOldBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
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

	const D3DXMATRIX kMatViewProj = CGraphicBase::GetViewMatrix() * CGraphicBase::GetProjMatrix();
	const FLOAT afBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	UINT uStride = sizeof(SDX11TraceVertex);
	UINT uOffset = 0u;
	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	pContext->VSSetShader(pVertexShader, nullptr, 0);
	pContext->PSSetShader(pPixelShader, nullptr, 0);
	pContext->PSSetSamplers(0, 1, &pSamplerState);
	pContext->PSSetShaderResources(0, 1, &pWhiteSRV);
	pContext->OMSetBlendState(pBlendState, afBlendFactor, 0xffffffffu);
	pContext->OMSetDepthStencilState(pDepthDisableState, 0);
	pContext->RSSetState(nullptr);

	UINT uSubmitted = 0u;
	for (TFlyVertexSetVector::const_iterator it = rkStrips.begin(); it != rkStrips.end(); ++it)
	{
		const TFlyVertexSet& rkSet = it->second;
		SDX11TraceVertex akVertices[6];
		bool bValidStrip = true;
		for (UINT i = 0; i < 6; ++i)
		{
			const TFlyVertex& rkSrc = rkSet.v[i];
			D3DXVECTOR4 v4World(rkSrc.p.x, rkSrc.p.y, rkSrc.p.z, 1.0f);
			D3DXVECTOR4 v4Clip;
			D3DXVec4Transform(&v4Clip, &v4World, &kMatViewProj);
			if (fabsf(v4Clip.w) <= 1.0e-6f)
			{
				bValidStrip = false;
				break;
			}

			const float fInvW = 1.0f / v4Clip.w;
			SDX11TraceVertex& rkDst = akVertices[i];
			rkDst.x = v4Clip.x * fInvW;
			rkDst.y = v4Clip.y * fInvW;
			rkDst.z = v4Clip.z * fInvW;
			UnpackColorARGB(rkSrc.c, rkDst.r, rkDst.g, rkDst.b, rkDst.a);
			rkDst.u = rkSrc.t.x;
			rkDst.v = rkSrc.t.y;
		}

		if (!bValidStrip)
			continue;

		D3D11_MAPPED_SUBRESOURCE kMappedResource = {};
		const HRESULT hMapResult = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
		if (FAILED(hMapResult) || !kMappedResource.pData)
			break;

		memcpy(kMappedResource.pData, akVertices, sizeof(akVertices));
		pContext->Unmap(pVertexBuffer, 0);
		pContext->Draw(6u, 0u);
		++uSubmitted;
	}

	ID3D11ShaderResourceView* pNullSRV = nullptr;
	pContext->PSSetShaderResources(0, 1, &pNullSRV);

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

	static DWORD s_dwLastParityLog = 0;
	const DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwLastParityLog >= 5000u)
	{
		s_dwLastParityLog = dwNow;
		TraceError("DX11_PIPELINE_STATE_PARITY pass=flytrace path=dx11_native");
		TraceError("DX11_PIPELINE_SUBMIT_PARITY pass=flytrace expected=%u submitted=%u", static_cast<unsigned int>(rkStrips.size()), static_cast<unsigned int>(uSubmitted));
	}

	return (uSubmitted > 0u);
}
}

void CFlyTrace::DestroySystem()
{
	ms_kPool.Destroy();
}

CFlyTrace* CFlyTrace::New()
{
	return ms_kPool.Alloc();
}

void CFlyTrace::Delete(CFlyTrace* pkInst)
{
	pkInst->Destroy();
	ms_kPool.Free(pkInst);
}

CFlyTrace::CFlyTrace()
{
	__Initialize();

	/*
	// Code for texture
	CGraphicImage * pImage = (CGraphicImage *)CResourceManager::Instance().GetResourcePointer("d:/ray.jpg");
	m_ImageInstance.SetImagePointer(pImage);
	
	CGraphicTexture * pTexture = m_ImageInstance.GetTexturePointer();
	m_lpTexture = pTexture->GetD3DTexture();
	*/
}

CFlyTrace::~CFlyTrace()
{
	Destroy();
}

				
void CFlyTrace::__Initialize()
{
	m_bRectShape=false;
	m_dwColor=0;
	m_fSize=0.0f;
	m_fTailLength=0.0f;	
}

void CFlyTrace::Destroy()
{
	m_TimePositionDeque.clear();

	__Initialize();
}

void CFlyTrace::UpdateNewPosition(const D3DXVECTOR3 & v3Position)
{
	m_TimePositionDeque.push_front(TTimePosition(CTimer::Instance().GetCurrentSecond(),v3Position));
	//Tracenf("%f %f",m_TimePositionDeque.back().first, CTimer::Instance().GetCurrentSecond());
	while(!m_TimePositionDeque.empty() && m_TimePositionDeque.back().first+m_fTailLength<CTimer::Instance().GetCurrentSecond())
	{
		m_TimePositionDeque.pop_back();
	}
}

void CFlyTrace::Create(const CFlyingData::TFlyingAttachData & rFlyingAttachData)
{
	//assert(rFlyingAttachData.bHasTail);
	m_dwColor = rFlyingAttachData.dwTailColor;
	m_fTailLength = rFlyingAttachData.fTailLength;
	m_fSize = rFlyingAttachData.fTailSize;
	m_bRectShape = rFlyingAttachData.bRectShape;
}


void CFlyTrace::Update()
{ 
	
}

//1. 알파를 쓰려면 색깔만 줄수있다.
//2. 텍스쳐를 쓰려면 알파 없다-_-


void CFlyTrace::Render()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	const bool bDX11RuntimeActive = (pDX11Device && pDX11Device->IsValid());

	if (m_TimePositionDeque.size()<=1)
		return;
	TFlyVertexSetVector VSVector;
	
	D3DXMATRIX m;
	CScreen s;s.UpdateViewMatrix();
	CCamera * pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
	if (!pCurrentCamera)
		return;

	const D3DXMATRIX & M = pCurrentCamera->GetViewMatrix();
	D3DXMatrixIdentity(&m);
	D3DXVECTOR3 F(pCurrentCamera->GetView());
	m._31 = F.x;
	m._32 = F.y;
	m._33 = F.z;

	Frustum & frustum = s.GetFrustum();
	//frustum.BuildViewFrustum(ms_matView * ms_matProj);

	TTimePositionDeque::iterator it1, it2;
	it2 = it1 = m_TimePositionDeque.begin();
	++it2;
	for(;it2!=m_TimePositionDeque.end();++it2,++it1)
	{
		const D3DXVECTOR3& rkOld=it1->second;
		const D3DXVECTOR3& rkNew=it2->second;
		D3DXVECTOR3 B = rkNew - rkOld;
		
		float radius = std::max(fabs(B.x),std::max(fabs(B.y),fabs(B.z)))/2;
		Vector3d c(it1->second.x+B.x*0.5f,
			it1->second.y+B.y*0.5f,
			it1->second.z+B.z*0.5f
			);
		if (frustum.ViewVolumeTest(c, radius)==VS_OUTSIDE)
			continue;

		float rate1 = (1-(CTimer::Instance().GetCurrentSecond()-it1->first)/m_fTailLength);
		float rate2 = (1-(CTimer::Instance().GetCurrentSecond()-it2->first)/m_fTailLength);
		float size1 = m_fSize;
		float size2 = m_fSize;
		if (!m_bRectShape)
		{
			size1 *= rate1;
			size2 *= rate2;
		}
		TFlyVertex v[6] = 
		{
			TFlyVertex(D3DXVECTOR3(0.0f,size1,0.0f), m_dwColor,D3DXVECTOR2(0.0f,0.0f)),
			TFlyVertex(D3DXVECTOR3(-size1,0.0f,0.0f),m_dwColor,D3DXVECTOR2(0.0f,0.5f)),
			TFlyVertex(D3DXVECTOR3(size1,0.0f,0.0f), m_dwColor,D3DXVECTOR2(0.5f,0.0f)),
			TFlyVertex(D3DXVECTOR3(-size2,0.0f,0.0f),m_dwColor,D3DXVECTOR2(0.5f,1.0f)),
			TFlyVertex(D3DXVECTOR3(size2,0.0f,0.0f), m_dwColor,D3DXVECTOR2(1.0f,0.5f)),
			TFlyVertex(D3DXVECTOR3(0.0f,-size2,0.0f),m_dwColor,D3DXVECTOR2(1.0f,1.0f)),
	
			/*TVertex(D3DXVECTOR3(0.0f,size1,0.0f), ((DWORD)(0x40*rate1)<<24) + 0x0000ff,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(-size1,0.0f,0.0f),((DWORD)(0x40*rate1)<<24) + 0x0000ff,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(size1,0.0f,0.0f), ((DWORD)(0x40*rate1)<<24) + 0x0000ff,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(-size2,0.0f,0.0f),((DWORD)(0x40*rate2)<<24) + 0x0000ff,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(size2,0.0f,0.0f), ((DWORD)(0x40*rate2)<<24) + 0x0000ff,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(0.0f,-size2,0.0f),((DWORD)(0x40*rate2)<<24) + 0x0000ff,D3DXVECTOR2(0.0f,0.0f)),*/

			/*TVertex(D3DXVECTOR3(0.0f,size1,0.0f),0x20ff0000,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(-size1,0.0f,0.0f),0x20ff0000,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(size1,0.0f,0.0f),0x20ff0000,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(-size2,0.0f,0.0f),0x20ff0000,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(size2,0.0f,0.0f),0x20ff0000,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(0.0f,-size2,0.0f),0x20ff0000,D3DXVECTOR2(0.0f,0.0f)),*/

			/*TVertex(D3DXVECTOR3(0.0f,size1,0.0f),0xffff0000,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(-size1,0.0f,0.0f),0xffff0000,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(size1,0.0f,0.0f),0xffff0000,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(-size2,0.0f,0.0f),0xff0000ff,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(size2,0.0f,0.0f),0xff0000ff,D3DXVECTOR2(0.0f,0.0f)),
			TVertex(D3DXVECTOR3(0.0f,-size2,0.0f),0xff0000ff,D3DXVECTOR2(0.0f,0.0f)),*/
		};


		D3DXVECTOR3 E(M._41,M._42,M._43);
		E = pCurrentCamera->GetEye();
		E-=it1->second;

		D3DXVECTOR3 P;
		D3DXVec3Cross(&P, &B,&E);

		D3DXVECTOR3 U;
		D3DXVec3Cross(&U,&F,&P);
		D3DXVec3Normalize(&U,&U);
		D3DXVECTOR3 R;
		D3DXVec3Cross(&R,&F,&U);
		//D3DXMatrixIdentity(&m);
		m._21 = U.x;
		m._22 = U.y;
		m._23 = U.z;
		m._11 = R.x;
		m._12 = R.y;
		m._13 = R.z;
		int i;
		for(i=0;i<6;i++)
			D3DXVec3TransformNormal(&v[i].p,&v[i].p,&m);
		for(i=0;i<3;i++)
			v[i].p += it1->second;
		for(;i<6;i++)
			v[i].p += it2->second;
		//for(i=0;i<6;i++)
		//	Tracenf("#%d:%f %f %f", i, v[i].p.x,v[i].p.y,v[i].p.z);
		
		VSVector.push_back(std::make_pair(-D3DXVec3Dot(&E,&pCurrentCamera->GetView()),TFlyVertexSet(v)));
//OLD: STATEMANAGER.DrawPrimitiveUP(GRP_PT_TRIANGLESTRIP, 4, v, sizeof(TVertex));
//OLD: STATEMANAGER.DrawPrimitiveUP(GRP_PT_TRIANGLESTRIP, 2, v+1, sizeof(TVertex));
	}

	std::sort(VSVector.begin(),VSVector.end());

	if (!bDX11RuntimeActive)
	{
		static bool s_bLoggedDX11FlyTraceDeviceUnavailable = false;
		if (!s_bLoggedDX11FlyTraceDeviceUnavailable)
		{
			s_bLoggedDX11FlyTraceDeviceUnavailable = true;
			TraceError("DX11_FLYTRACE_PATH mode=dx11_native_draw status=skipped reason=dx11_device_unavailable");
		}
		return;
	}

	if (RenderTraceStripsDX11(pDX11Device, VSVector))
		return;

	static bool s_bLoggedDX11FlyTraceNativeFail = false;
	if (!s_bLoggedDX11FlyTraceNativeFail)
	{
		s_bLoggedDX11FlyTraceNativeFail = true;
		TraceError("DX11_FLYTRACE_PATH mode=dx11_native_draw status=deferred reason=bootstrap_or_submit_failed");
	}
	return;
}
