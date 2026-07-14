#include <dxgi1_4.h>

#include "StdAfx.h"
#include "EterBase/Utils.h"
#include "EterBase/Timer.h"
#include "GrpBase.h"
#include "Camera.h"
#include "StateManager.h"
#include "GrpDeviceDX11.h"

#include <cmath>

void PixelPositionToD3DXVECTOR3(const D3DXVECTOR3& c_rkPPosSrc, D3DXVECTOR3* pv3Dst)
{
	pv3Dst->x=+c_rkPPosSrc.x;
	pv3Dst->y=-c_rkPPosSrc.y;
	pv3Dst->z=+c_rkPPosSrc.z;
}

void D3DXVECTOR3ToPixelPosition(const D3DXVECTOR3& c_rv3Src, D3DXVECTOR3* pv3Dst)
{
	pv3Dst->x=+c_rv3Src.x;
	pv3Dst->y=-c_rv3Src.y;
	pv3Dst->z=+c_rv3Src.z;
}

HWND CGraphicBase::ms_hWnd;
HDC CGraphicBase::ms_hDC;

GrpPresentParameters	CGraphicBase::ms_d3dPresentParameter = {};
GrpViewport				CGraphicBase::ms_Viewport;

HRESULT					CGraphicBase::ms_hLastResult = NULL;

int						CGraphicBase::ms_iWidth;
int						CGraphicBase::ms_iHeight;

DWORD					CGraphicBase::ms_faceCount = 0;

GrpCaps					CGraphicBase::ms_d3dCaps;

DWORD					CGraphicBase::ms_dwD3DBehavior = 0;


D3DXMATRIX				CGraphicBase::ms_matIdentity;

D3DXMATRIX				CGraphicBase::ms_matView;
D3DXMATRIX				CGraphicBase::ms_matProj;
D3DXMATRIX				CGraphicBase::ms_matInverseView;
D3DXMATRIX				CGraphicBase::ms_matInverseViewYAxis;

D3DXMATRIX				CGraphicBase::ms_matWorld;
D3DXMATRIX				CGraphicBase::ms_matWorldView;
D3DXMATRIX				CGraphicBase::ms_matDX11WorldViewSnapshot;
D3DXMATRIX				CGraphicBase::ms_matDX11WorldProjSnapshot;
bool					CGraphicBase::ms_bDX11WorldProjectionSnapshotValid = false;

D3DXMATRIX				CGraphicBase::ms_matScreen0;
D3DXMATRIX				CGraphicBase::ms_matScreen1;
D3DXMATRIX				CGraphicBase::ms_matScreen2;

D3DXVECTOR3				CGraphicBase::ms_vtPickRayOrig;
D3DXVECTOR3				CGraphicBase::ms_vtPickRayDir;

float					CGraphicBase::ms_fFieldOfView;
float					CGraphicBase::ms_fNearY;
float					CGraphicBase::ms_fFarY;
float					CGraphicBase::ms_fAspect;

DWORD					CGraphicBase::ms_dwWavingEndTime;
int						CGraphicBase::ms_iWavingPower;
DWORD					CGraphicBase::ms_dwFlashingEndTime;
D3DXCOLOR				CGraphicBase::ms_FlashingColor;

// Terrain picking용 Ray... CCamera 이용하는 버전.. 기존의 Ray와 통합 필요...
CRay					CGraphicBase::ms_Ray;
bool					CGraphicBase::ms_bSupportDXT = true;
bool					CGraphicBase::ms_isLowTextureMemory = false;
bool					CGraphicBase::ms_isHighTextureMemory = false;

// 2004.11.18.myevan.DynamicVertexBuffer로 교체
/*
std::vector<TIndex>		CGraphicBase::ms_lineIdxVector;
std::vector<TIndex>		CGraphicBase::ms_lineTriIdxVector;
std::vector<TIndex>		CGraphicBase::ms_lineRectIdxVector;
std::vector<TIndex>		CGraphicBase::ms_lineCubeIdxVector;

std::vector<TIndex>		CGraphicBase::ms_fillTriIdxVector;
std::vector<TIndex>		CGraphicBase::ms_fillRectIdxVector;
std::vector<TIndex>		CGraphicBase::ms_fillCubeIdxVector;
*/

LPD3DXMESH				CGraphicBase::ms_lpSphereMesh = NULL;
LPD3DXMESH				CGraphicBase::ms_lpCylinderMesh = NULL;

std::vector<D3DXMATRIX> CGraphicBase::ms_matStack = { D3DXMATRIX::Identity };



bool CGraphicBase::IsLowTextureMemory()
{
	return ms_isLowTextureMemory;
}

bool CGraphicBase::IsHighTextureMemory()
{
	return ms_isHighTextureMemory;
}

bool CGraphicBase::IsFastTNL()
{ 
	if (ms_dwD3DBehavior & GRP_CREATE_HARDWARE_VERTEXPROCESSING ||
		ms_dwD3DBehavior & GRP_CREATE_MIXED_VERTEXPROCESSING)
	{
	if (ms_d3dCaps.VertexShaderVersion>GRP_VS_VERSION(1,0))
			return true;
	}
	return false;
}

bool CGraphicBase::IsTLVertexClipping()
{
	if (ms_d3dCaps.PrimitiveMiscCaps & GRP_PMISCCAPS_CLIPTLVERTS)
		return true;

	return false;
}

void CGraphicBase::GetBackBufferSize(UINT* puWidth, UINT* puHeight)
{
	UINT uWidth = ms_d3dPresentParameter.BackBufferWidth;
	UINT uHeight = ms_d3dPresentParameter.BackBufferHeight;

	// DX11-native fallback: some paths query CGraphicBase size before legacy
	// present params are populated. Recover from active DX11 device dimensions.
	if ((0u == uWidth || 0u == uHeight))
	{
		if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
		{
			if (pDX11Device->IsValid())
			{
				uWidth = pDX11Device->GetWidth();
				uHeight = pDX11Device->GetHeight();
				ms_d3dPresentParameter.BackBufferWidth = uWidth;
				ms_d3dPresentParameter.BackBufferHeight = uHeight;
			}
		}
	}

	*puWidth = uWidth;
	*puHeight = uHeight;
}

void CGraphicBase::SetBackBufferSize(UINT uWidth, UINT uHeight)
{
	ms_d3dPresentParameter.BackBufferWidth = uWidth;
	ms_d3dPresentParameter.BackBufferHeight = uHeight;
}

void CGraphicBase::SetDefaultIndexBuffer(UINT eDefIB)
{
	(void)eDefIB;
}

bool CGraphicBase::SetPDTStream(SPDTVertex* pVertices, UINT uVtxCount)
{
	return SetPDTStream((SPDTVertexRaw*)pVertices, uVtxCount);
}

bool CGraphicBase::SetPDTStream(SPDTVertexRaw* pSrcVertices, UINT uVtxCount)
{
	(void)pSrcVertices;
	(void)uVtxCount;
	return false;
}

uint64_t CGraphicBase::GetAvailableTextureMemory()
{
	static DWORD    s_dwNextUpdateTime = 0;
	static uint64_t s_uRemainVRAM = 0;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return s_uRemainVRAM;

	DWORD dwCurTime = ELTimer_GetMSec();
	if (s_dwNextUpdateTime < dwCurTime)
	{
		s_dwNextUpdateTime = dwCurTime + 5000;
		//current->used->available
		IDXGIFactory4* factory = nullptr;
		IDXGIAdapter1* adapter1 = nullptr;
		IDXGIAdapter3* adapter3 = nullptr;

		if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->EnumAdapters1(0, &adapter1)) &&
			SUCCEEDED(adapter1->QueryInterface(IID_PPV_ARGS(&adapter3))))
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
			if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(
				0,
				DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
				&info)))
			{
				if (info.Budget > info.CurrentUsage)
					s_uRemainVRAM = info.Budget - info.CurrentUsage;
				else
					s_uRemainVRAM = 0;
			}
		}
		SAFE_RELEASE(adapter3);
		SAFE_RELEASE(adapter1);
		SAFE_RELEASE(factory);
	}

	return s_uRemainVRAM;
}

const D3DXMATRIX& CGraphicBase::GetViewMatrix()
{
	return ms_matView;
}

const D3DXMATRIX& CGraphicBase::GetProjMatrix()
{
	return ms_matProj;
}

const D3DXMATRIX & CGraphicBase::GetIdentityMatrix()
{
	return ms_matIdentity;
}

bool CGraphicBase::ProjectPositionDX11World(float x, float y, float z, float* pfX, float* pfY, float* pfZ)
{
	if (!pfX || !pfY || !pfZ)
		return false;

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid() || !ms_bDX11WorldProjectionSnapshotValid)
		return false;

	UINT uBackBufferWidth = 0u;
	UINT uBackBufferHeight = 0u;
	GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (0u == uBackBufferWidth || 0u == uBackBufferHeight)
		return false;

	GrpViewport kViewport = ms_Viewport;
	if (kViewport.Width != uBackBufferWidth || kViewport.Height != uBackBufferHeight || kViewport.MaxZ <= kViewport.MinZ)
	{
		kViewport.X = 0.0f;
		kViewport.Y = 0.0f;
		kViewport.Width = static_cast<float>(uBackBufferWidth);
		kViewport.Height = static_cast<float>(uBackBufferHeight);
		kViewport.MinZ = 0.0f;
		kViewport.MaxZ = 1.0f;
	}

	D3DXVECTOR3 kInput(x, y, z);
	D3DXVECTOR3 kOutput;
	D3DXVec3Project(&kOutput, &kInput, &kViewport, &ms_matDX11WorldProjSnapshot, &ms_matDX11WorldViewSnapshot, &ms_matIdentity);
	if (!std::isfinite(kOutput.x) || !std::isfinite(kOutput.y) || !std::isfinite(kOutput.z))
		return false;

	*pfX = kOutput.x;
	*pfY = kOutput.y;
	*pfZ = kOutput.z;
	return true;
}

void CGraphicBase::SetEyeCamera(float xEye, float yEye, float zEye,
								float xCenter, float yCenter, float zCenter,
								float xUp, float yUp, float zUp)
{
	D3DXVECTOR3 vectorEye(xEye, yEye, zEye);
	D3DXVECTOR3 vectorCenter(xCenter, yCenter, zCenter);
	D3DXVECTOR3 vectorUp(xUp, yUp, zUp);

//	CCameraManager::Instance().SetCurrentCamera(CCameraManager::DEFAULT_PERSPECTIVE_CAMERA);
	CCameraManager::Instance().GetCurrentCamera()->SetViewParams(vectorEye, vectorCenter, vectorUp);
	UpdateViewMatrix();
}

void CGraphicBase::SetSimpleCamera(float x, float y, float z, float pitch, float roll)
{
	CCamera * pCamera = CCameraManager::Instance().GetCurrentCamera();
	D3DXVECTOR3 vectorEye(x, y, z);

	pCamera->SetViewParams(D3DXVECTOR3(0.0f, y, 0.0f), D3DXVECTOR3(0.0f, 0.0f, 0.0f), D3DXVECTOR3(0.0f, 0.0f, 1.0f));
	pCamera->RotateEyeAroundTarget(pitch, roll);
	pCamera->Move(vectorEye);

	UpdateViewMatrix();

	STATEMANAGER.GetTransform(GRP_TS_WORLD, &ms_matWorld);
	ms_matWorldView = ms_matWorld * ms_matView;
}

void CGraphicBase::SetAroundCamera(float distance, float pitch, float roll, float lookAtZ)
{
	CCamera * pCamera = CCameraManager::Instance().GetCurrentCamera();
	pCamera->SetViewParams(D3DXVECTOR3(0.0f, -distance, 0.0f), D3DXVECTOR3(0.0f, 0.0f, 0.0f), D3DXVECTOR3(0.0f, 0.0f, 1.0f));
	pCamera->RotateEyeAroundTarget(pitch, roll);
	D3DXVECTOR3 v3Target = pCamera->GetTarget();
	v3Target.z = lookAtZ;
	pCamera->SetTarget(v3Target);
// 	pCamera->Move(v3Target);

	UpdateViewMatrix();

	STATEMANAGER.GetTransform(GRP_TS_WORLD, &ms_matWorld);
	ms_matWorldView = ms_matWorld * ms_matView;
}

void CGraphicBase::SetPositionCamera(float fx, float fy, float fz, float distance, float pitch, float roll)
{
	// I wanna downward this code to the game control level. - [levites]
	if (ms_dwWavingEndTime > CTimer::Instance().GetCurrentMillisecond())
	{
		if (ms_iWavingPower>0)
		{
			fx += float(rand() % ms_iWavingPower) / 10.0f;
			fy += float(rand() % ms_iWavingPower) / 10.0f;
			fz += float(rand() % ms_iWavingPower) / 10.0f;
		}
	}

	CCamera * pCamera = CCameraManager::Instance().GetCurrentCamera();
	if (!pCamera)
		return;

	pCamera->SetViewParams(D3DXVECTOR3(0.0f, -distance, 0.0f), D3DXVECTOR3(0.0f, 0.0f, 0.0f), D3DXVECTOR3(0.0f, 0.0f, 1.0f));
	pitch = fMIN(80.0f, fMAX(-80.0f, pitch) );
//	Tracef("SetPosition Camera : %f, %f\n", pitch, roll);
	pCamera->RotateEyeAroundTarget(pitch, roll);
	pCamera->Move(D3DXVECTOR3(fx, fy, fz));

	UpdateViewMatrix();

	// This is levites's virtual(?) code which you should not trust.
	STATEMANAGER.GetTransform(GRP_TS_WORLD, &ms_matWorld);
	ms_matWorldView = ms_matWorld * ms_matView;
}

void CGraphicBase::SetOrtho2D(float hres, float vres, float zres)
{
	const DirectX::XMMATRIX kOrtho = DirectX::XMMatrixOrthographicOffCenterRH(0.0f, hres, vres, 0.0f, 0.0f, zres);
	DirectX::XMStoreFloat4x4(&ms_matProj, kOrtho);
	UpdateProjMatrix();
}

void CGraphicBase::SetOrtho3D(float hres, float vres, float zmin, float zmax)
{
	const DirectX::XMMATRIX kOrtho = DirectX::XMMatrixOrthographicRH(hres, vres, zmin, zmax);
	DirectX::XMStoreFloat4x4(&ms_matProj, kOrtho);
	UpdateProjMatrix();
}

void CGraphicBase::SetPerspective(float fov, float aspect, float nearz, float farz)
{
	ms_fFieldOfView = fov;


	//if (ms_d3dPresentParameter.BackBufferWidth>0 && ms_d3dPresentParameter.BackBufferHeight>0)
	//	ms_fAspect = float(ms_d3dPresentParameter.BackBufferWidth)/float(ms_d3dPresentParameter.BackBufferHeight);
	//else
	ms_fAspect = aspect;

	ms_fNearY = nearz;
	ms_fFarY = farz;

	//CCameraManager::Instance().SetCurrentCamera(CCameraManager::DEFAULT_PERSPECTIVE_CAMERA);
	const DirectX::XMMATRIX kPerspective = DirectX::XMMatrixPerspectiveFovRH(DirectX::XMConvertToRadians(fov), ms_fAspect, nearz, farz);
	DirectX::XMStoreFloat4x4(&ms_matProj, kPerspective);
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
	{
		if (pDX11Device->IsValid())
		{
			ms_matDX11WorldProjSnapshot = ms_matProj;
			ms_matDX11WorldViewSnapshot = ms_matView;
			ms_bDX11WorldProjectionSnapshotValid = true;
		}
	}
	UpdateProjMatrix();
}

void CGraphicBase::UpdateProjMatrix()
{
	STATEMANAGER.SetTransform(GRP_TS_PROJECTION, &ms_matProj);
}

void CGraphicBase::UpdateViewMatrix()
{
	CCamera* pkCamera=CCameraManager::Instance().GetCurrentCamera();
	if (!pkCamera)
		return;

	ms_matView = pkCamera->GetViewMatrix();
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
	{
		if (pDX11Device->IsValid() && ms_bDX11WorldProjectionSnapshotValid)
			ms_matDX11WorldViewSnapshot = ms_matView;
	}
	STATEMANAGER.SetTransform(GRP_TS_VIEW, &ms_matView);

	{
		const DirectX::XMMATRIX kInv = DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&ms_matView));
		DirectX::XMStoreFloat4x4(&ms_matInverseView, kInv);
	}
	ms_matInverseViewYAxis._11 = ms_matInverseView._11;
	ms_matInverseViewYAxis._12 = ms_matInverseView._12;
	ms_matInverseViewYAxis._21 = ms_matInverseView._21;
	ms_matInverseViewYAxis._22 = ms_matInverseView._22;
}

void CGraphicBase::UpdatePipeLineMatrix()
{
	UpdateProjMatrix();
	UpdateViewMatrix();
}

void CGraphicBase::SetViewport(DWORD dwX, DWORD dwY, DWORD dwWidth, DWORD dwHeight, float fMinZ, float fMaxZ)
{
	ms_Viewport.X = dwX;
	ms_Viewport.Y = dwY;
	ms_Viewport.Width = dwWidth;
	ms_Viewport.Height = dwHeight;
	ms_Viewport.MinZ = fMinZ;
	ms_Viewport.MaxZ = fMaxZ;
}

void CGraphicBase::GetViewport(DWORD* pdwX, DWORD* pdwY, DWORD* pdwWidth, DWORD* pdwHeight, float* pfMinZ, float* pfMaxZ)
{
	if (pdwX) *pdwX = static_cast<DWORD>(ms_Viewport.X);
	if (pdwY) *pdwY = static_cast<DWORD>(ms_Viewport.Y);
	if (pdwWidth) *pdwWidth = static_cast<DWORD>(ms_Viewport.Width);
	if (pdwHeight) *pdwHeight = static_cast<DWORD>(ms_Viewport.Height);
	if (pfMinZ) *pfMinZ = ms_Viewport.MinZ;
	if (pfMaxZ) *pfMaxZ = ms_Viewport.MaxZ;
}

void CGraphicBase::GetTargetPosition(float * px, float * py, float * pz)
{
	*px = CCameraManager::Instance().GetCurrentCamera()->GetTarget().x;
	*py = CCameraManager::Instance().GetCurrentCamera()->GetTarget().y;
	*pz = CCameraManager::Instance().GetCurrentCamera()->GetTarget().z;
}

void CGraphicBase::GetCameraPosition(float * px, float * py, float * pz)
{
	*px = CCameraManager::Instance().GetCurrentCamera()->GetEye().x;
	*py = CCameraManager::Instance().GetCurrentCamera()->GetEye().y;
	*pz = CCameraManager::Instance().GetCurrentCamera()->GetEye().z;
}

void CGraphicBase::GetMatrix(D3DXMATRIX* pRetMatrix) const
{
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	*pRetMatrix = ms_matStack.back();
}

const D3DXMATRIX* CGraphicBase::GetMatrixPointer() const
{
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	return &ms_matStack.back();
}

void CGraphicBase::GetSphereMatrix(D3DXMATRIX * pMatrix, float fValue)
{
	*pMatrix = D3DXMATRIX::Identity;
	pMatrix->_11 = fValue * ms_matWorldView._11;
	pMatrix->_21 = fValue * ms_matWorldView._21;
	pMatrix->_31 = fValue * ms_matWorldView._31;
	pMatrix->_41 = fValue;
	pMatrix->_12 = -fValue * ms_matWorldView._12;
	pMatrix->_22 = -fValue * ms_matWorldView._22;
	pMatrix->_32 = -fValue * ms_matWorldView._32;
	pMatrix->_42 = -fValue;
}

float CGraphicBase::GetFOV()
{
	return ms_fFieldOfView;
}

void CGraphicBase::PushMatrix()
{
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	ms_matStack.push_back(ms_matStack.back());
}

void CGraphicBase::Scale(float x, float y, float z)
{
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	ms_matStack.back() = ms_matStack.back() * D3DXMATRIX::CreateScale(x, y, z);
}

void CGraphicBase::Rotate(float degree, float x, float y, float z)
{
	const D3DXVECTOR3 vec(x, y, z);
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	ms_matStack.back() = ms_matStack.back() * D3DXMATRIX::CreateFromAxisAngle(vec, DirectX::XMConvertToRadians(degree));
}

void CGraphicBase::RotateLocal(float degree, float x, float y, float z)
{
	const D3DXVECTOR3 vec(x, y, z);
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	ms_matStack.back() = D3DXMATRIX::CreateFromAxisAngle(vec, DirectX::XMConvertToRadians(degree)) * ms_matStack.back();
}

void CGraphicBase::MultMatrix( const D3DXMATRIX* pMat)
{
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	ms_matStack.back() = ms_matStack.back() * (*pMat);
}

void CGraphicBase::MultMatrixLocal( const D3DXMATRIX* pMat)
{
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	ms_matStack.back() = (*pMat) * ms_matStack.back();
}

void CGraphicBase::RotateYawPitchRollLocal(float fYaw, float fPitch, float fRoll)
{
	const D3DXMATRIX kRot = D3DXMATRIX::CreateFromYawPitchRoll(
		DirectX::XMConvertToRadians(fYaw),
		DirectX::XMConvertToRadians(fPitch),
		DirectX::XMConvertToRadians(fRoll));
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	ms_matStack.back() = kRot * ms_matStack.back();
}

void CGraphicBase::Translate(float x, float y, float z)
{
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	ms_matStack.back() = ms_matStack.back() * D3DXMATRIX::CreateTranslation(x, y, z);
}

void CGraphicBase::LoadMatrix(const D3DXMATRIX& c_rSrcMatrix)
{
	if (ms_matStack.empty())
		ms_matStack.push_back(D3DXMATRIX::Identity);
	ms_matStack.back() = c_rSrcMatrix;
}

void CGraphicBase::PopMatrix()
{
	if (ms_matStack.size() > 1)
		ms_matStack.pop_back();
	else
	{
		if (ms_matStack.empty())
			ms_matStack.push_back(D3DXMATRIX::Identity);
		else
			ms_matStack.back() = D3DXMATRIX::Identity;
	}
}

DWORD CGraphicBase::GetColor(float r, float g, float b, float a)
{
	BYTE argb[4] =
	{
		(BYTE) (255.0f * b),
		(BYTE) (255.0f * g),
		(BYTE) (255.0f * r),
		(BYTE) (255.0f * a)
	};

	return *((DWORD *) argb);
}

void CGraphicBase::InitScreenEffect()
{
	ms_dwWavingEndTime = 0;
	ms_dwFlashingEndTime = 0;
	ms_iWavingPower = 0;
	ms_FlashingColor = D3DXCOLOR(0.0f, 0.0f, 0.0f, 0.0f);
}

void CGraphicBase::SetScreenEffectWaving(float fDuringTime, int iPower)
{
	ms_dwWavingEndTime = CTimer::Instance().GetCurrentMillisecond() + long(fDuringTime * 1000.0f);
	ms_iWavingPower = iPower;
}

void CGraphicBase::SetScreenEffectFlashing(float fDuringTime, const D3DXCOLOR & c_rColor)
{
	ms_dwFlashingEndTime = CTimer::Instance().GetCurrentMillisecond() + long(fDuringTime * 1000.0f);
	ms_FlashingColor = c_rColor;
}

DWORD CGraphicBase::GetFaceCount()
{
	return ms_faceCount;
}

void CGraphicBase::ResetFaceCount()
{
	ms_faceCount = 0;
}

HRESULT CGraphicBase::GetLastResult()
{
	return ms_hLastResult;
}

CGraphicBase::CGraphicBase()
{
}

CGraphicBase::~CGraphicBase()
{
}

namespace
{
	bool DX11ReadSRVToBGRA(ID3D11ShaderResourceView* pSRV, std::vector<std::uint8_t>& outPixels, UINT& outWidth, UINT& outHeight)
	{
		outPixels.clear();
		outWidth = 0u;
		outHeight = 0u;
		if (!pSRV)
			return false;

		CGraphicDeviceDX11* pDevice = CGraphicDeviceDX11::GetActiveDevice();
		if (!pDevice || !pDevice->IsValid())
			return false;

		ID3D11Resource* pResource = nullptr;
		pSRV->GetResource(&pResource);
		if (!pResource)
			return false;

		ID3D11Texture2D* pTex = nullptr;
		HRESULT hr = pResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pTex));
		pResource->Release();
		if (FAILED(hr) || !pTex)
			return false;

		D3D11_TEXTURE2D_DESC srcDesc = {};
		pTex->GetDesc(&srcDesc);
		if (!srcDesc.Width || !srcDesc.Height)
		{
			pTex->Release();
			return false;
		}

		D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
		stagingDesc.BindFlags = 0;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;

		ID3D11Texture2D* pStaging = nullptr;
		hr = pDevice->GetDevice()->CreateTexture2D(&stagingDesc, nullptr, &pStaging);
		if (FAILED(hr) || !pStaging)
		{
			pTex->Release();
			return false;
		}

		pDevice->GetContext()->CopyResource(pStaging, pTex);

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		hr = pDevice->GetContext()->Map(pStaging, 0u, D3D11_MAP_READ, 0u, &mapped);
		if (FAILED(hr))
		{
			pStaging->Release();
			pTex->Release();
			return false;
		}

		outWidth = srcDesc.Width;
		outHeight = srcDesc.Height;
		outPixels.resize(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4u);

		for (UINT y = 0; y < outHeight; ++y)
		{
			const std::uint8_t* srcLine = reinterpret_cast<const std::uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
			std::uint8_t* dstLine = outPixels.data() + static_cast<size_t>(y) * static_cast<size_t>(outWidth) * 4u;
			if (srcDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || srcDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || srcDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM || srcDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
			{
				memcpy(dstLine, srcLine, static_cast<size_t>(outWidth) * 4u);
				if (srcDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM || srcDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
				{
					for (UINT x = 0; x < outWidth; ++x)
						dstLine[x * 4 + 3] = 255u;
				}
			}
			else if (srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
			{
				for (UINT x = 0; x < outWidth; ++x)
				{
					dstLine[x * 4 + 0] = srcLine[x * 4 + 2];
					dstLine[x * 4 + 1] = srcLine[x * 4 + 1];
					dstLine[x * 4 + 2] = srcLine[x * 4 + 0];
					dstLine[x * 4 + 3] = srcLine[x * 4 + 3];
				}
			}
			else
			{
				pDevice->GetContext()->Unmap(pStaging, 0u);
				pStaging->Release();
				pTex->Release();
				return false;
			}
		}

		pDevice->GetContext()->Unmap(pStaging, 0u);
		pStaging->Release();
		pTex->Release();
		return true;
	}

	bool SaveBGRAAsTopDownBMP(const char* c_szFileName, const std::vector<std::uint8_t>& pixels, UINT w, UINT h)
	{
		if (!c_szFileName || pixels.empty() || !w || !h)
			return false;

		FILE* fp = fopen(c_szFileName, "wb");
		if (!fp)
			return false;

		BITMAPFILEHEADER fileHeader = {};
		BITMAPINFOHEADER infoHeader = {};
		const DWORD imageSize = static_cast<DWORD>(w * h * 4u);

		fileHeader.bfType = 0x4D42; // BM
		fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
		fileHeader.bfSize = fileHeader.bfOffBits + imageSize;

		infoHeader.biSize = sizeof(BITMAPINFOHEADER);
		infoHeader.biWidth = static_cast<LONG>(w);
		infoHeader.biHeight = -static_cast<LONG>(h); // top-down, parity with old D3DX output expectations
		infoHeader.biPlanes = 1;
		infoHeader.biBitCount = 32;
		infoHeader.biCompression = BI_RGB;
		infoHeader.biSizeImage = imageSize;

		fwrite(&fileHeader, sizeof(fileHeader), 1, fp);
		fwrite(&infoHeader, sizeof(infoHeader), 1, fp);
		fwrite(pixels.data(), imageSize, 1, fp);
		fclose(fp);
		return true;
	}
}

HRESULT D3DXSaveTextureToFileA(const char* pDestFile, D3DXIMAGE_FILEFORMAT DestFormat, ID3D11ShaderResourceView* pSrcTexture, const void*)
{
	if (!pDestFile || !pSrcTexture)
		return E_INVALIDARG;

	if (DestFormat == D3DXIFF_DDS)
		return E_NOTIMPL;

	std::vector<std::uint8_t> pixels;
	UINT w = 0u;
	UINT h = 0u;
	if (!DX11ReadSRVToBGRA(pSrcTexture, pixels, w, h))
		return E_FAIL;
	if (!SaveBGRAAsTopDownBMP(pDestFile, pixels, w, h))
		return E_FAIL;
	return S_OK;
}

HRESULT D3DXSaveTextureToFileW(const wchar_t* pDestFile, D3DXIMAGE_FILEFORMAT DestFormat, ID3D11ShaderResourceView* pSrcTexture, const void* pPalette)
{
	if (!pDestFile)
		return E_INVALIDARG;

	char szPath[MAX_PATH * 2] = {};
	int iLen = WideCharToMultiByte(CP_UTF8, 0, pDestFile, -1, szPath, static_cast<int>(sizeof(szPath)), nullptr, nullptr);
	if (iLen <= 0)
		return E_FAIL;

	return D3DXSaveTextureToFileA(szPath, DestFormat, pSrcTexture, pPalette);
}
