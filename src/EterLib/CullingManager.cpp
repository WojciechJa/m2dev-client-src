#include "StdAfx.h"
#include "CullingManager.h"
#include "GrpObjectInstance.h"
#include "Camera.h"
#include "UserInterface/config.h"
#include <cmath>
#include <EterBase/Timer.h>

//#define COUNT_SHOWING_SPHERE

#ifdef COUNT_SHOWING_SPHERE
int showingcount = 0;
#endif

namespace
{
inline bool IsFiniteFloat(float v)
{
	return std::isfinite(v);
}

inline bool IsValidSphere(const Vector3d& center, float radius)
{
	return IsFiniteFloat(center.x) &&
		IsFiniteFloat(center.y) &&
		IsFiniteFloat(center.z) &&
		IsFiniteFloat(radius) &&
		radius > 0.0f;
}

inline bool IsReasonableSphere(const Vector3d& center, float radius)
{
	// Keep culling inputs safely inside SpherePack spatial envelope.
	// Root/leaf trees use large radii, but corrupted bounds can still produce
	// extreme centers that violate parent-child invariants during AddChild.
	const float kMaxAbsCenter = 1500000.0f;
	const float kMaxRadius = 200000.0f;
	return fabsf(center.x) <= kMaxAbsCenter &&
		fabsf(center.y) <= kMaxAbsCenter &&
		fabsf(center.z) <= kMaxAbsCenter &&
		radius <= kMaxRadius;
}

inline bool IsBuildingLikeObject(const CGraphicObjectInstance* pInstance)
{
	if (!pInstance)
		return false;

	const int iType = pInstance->GetType();
	return (iType == THING_OBJECT || iType == DUNGEON_OBJECT || iType == ACTOR_OBJECT);
}
}

void CCullingManager::RayTraceCallback(const Vector3d &/*p1*/,          // source pos of ray
							  const Vector3d &/*dir*/,          // dest pos of ray
							  float distance,
							  const Vector3d &/*sect*/,
							  SpherePack *sphere)
{
	//if (state!=VS_OUTSIDE)
	//{
	if (m_RayFarDistance<=0.0f || m_RayFarDistance>=distance)
	{
#ifdef SPHERELIB_STRICT
		if (sphere->IS_SPHERE)
			puts("CCullingManager::RayTraceCallback");
#endif		
		m_list.push_back((CGraphicObjectInstance *)sphere->GetUserData());
	}
		//f((CGraphicObjectInstance *)sphere->GetUserData());
	//}
}


void CCullingManager::VisibilityCallback(const Frustum &f,SpherePack *sphere,ViewState state)
{
#ifdef SPHERELIB_STRICT
		if (sphere->IS_SPHERE)
			puts("CCullingManager::VisibilityCallback");
#endif

	CGraphicObjectInstance * pInstance = (CGraphicObjectInstance*)sphere->GetUserData();
	bool bVisible = (state != VS_OUTSIDE);
	bool bYFlipRescued = false;
	bool bDistanceRescued = false;

	if (!bVisible &&
		DX11RuntimeConfig::kObjectCullingYFlipRescue &&
		IsBuildingLikeObject(pInstance))
	{
		D3DXVECTOR3 v3Center(0.0f, 0.0f, 0.0f);
		float fRadius = 0.0f;
		if (pInstance->GetBoundingSphere(v3Center, fRadius))
		{
			Vector3d kCenterYFlipped;
			kCenterYFlipped.Set(v3Center.x, -v3Center.y, v3Center.z);
			const ViewState eAltState = f.ViewVolumeTest(kCenterYFlipped, fRadius);
			if (eAltState != VS_OUTSIDE)
			{
				bVisible = true;
				bYFlipRescued = true;
			}
		}
	}

	if (!bVisible &&
		DX11RuntimeConfig::kObjectCullingDistanceRescue &&
		IsBuildingLikeObject(pInstance))
	{
		CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
		if (pCamera)
		{
			const D3DXVECTOR3& rv3Eye = pCamera->GetEye();
			const D3DXVECTOR3& rv3Pos = pInstance->GetPosition();
			const float dx = rv3Eye.x - rv3Pos.x;
			const float dy = rv3Eye.y - rv3Pos.y;
			const float dz = rv3Eye.z - rv3Pos.z;
			const float fDistSq = dx * dx + dy * dy + dz * dz;
			const float fMaxDist = DX11RuntimeConfig::kObjectCullingDistanceRescueRange;
			if (fDistSq <= (fMaxDist * fMaxDist))
			{
				bVisible = true;
				bDistanceRescued = true;
			}
		}
	}

	static DWORD s_dwLastCullingRescueLogMS = 0u;
	static DWORD s_dwYFlipRescueCount = 0u;
	static DWORD s_dwDistanceRescueCount = 0u;
	if (bYFlipRescued)
		++s_dwYFlipRescueCount;
	if (bDistanceRescued)
		++s_dwDistanceRescueCount;

	const DWORD dwNow = ELTimer_GetMSec();
	if ((s_dwYFlipRescueCount > 0u || s_dwDistanceRescueCount > 0u) &&
		(0u == s_dwLastCullingRescueLogMS || (dwNow - s_dwLastCullingRescueLogMS) >= 5000u))
	{
		s_dwLastCullingRescueLogMS = dwNow;
		TraceError("DX11_OBJECT_CULL_RESCUE y_flip=%u distance=%u",
			s_dwYFlipRescueCount,
			s_dwDistanceRescueCount);
		s_dwYFlipRescueCount = 0u;
		s_dwDistanceRescueCount = 0u;
	}

	if (!bVisible)
	{
#ifdef COUNT_SHOWING_SPHERE
		if (pInstance->isShow())
		{
			Tracef("SH : %p  ",sphere->GetUserData());
			showingcount--;
			Tracef("show size : %5d\n",showingcount);
		}
#endif
		pInstance->Hide();
	}
	else
	{
#ifdef COUNT_SHOWING_SPHERE
		if (!pInstance->isShow())
		{
			Tracef("HS : %p  ",sphere->GetUserData());
			showingcount++;
			Tracef("show size : %5d\n",showingcount);
		}
#endif
		pInstance->Show();
	}
}
void CCullingManager::RangeTestCallback(const Vector3d &/*p*/,float /*distance*/,SpherePack *sphere,ViewState state)
{
#ifdef SPHERELIB_STRICT
		if (sphere->IS_SPHERE)
			puts("CCullingManager::RangeTestCallback");
#endif
	if (state!=VS_OUTSIDE)
	{
		m_list.push_back((CGraphicObjectInstance *)sphere->GetUserData());
		//f((CGraphicObjectInstance *)sphere->GetUserData());
	}
	//assert(false && "NOT REACHED");
}

void CCullingManager::Reset()
{
	m_Factory->Reset();
}

void CCullingManager::Update()
{
	// NOTE: update each object
	// í•˜ì§€ë§ê³  ê°ìž í•˜ê²Œ í•´ë³´ìž

	//DWORD time = ELTimer_GetMSec();
	//Reset();

	m_Factory->Process();
	//Tracef("cull update : %3d  ",ELTimer_GetMSec()-time);
}

void CCullingManager::Process()
{
	//DWORD time = ELTimer_GetMSec();
	//Frustum f;
	UpdateViewMatrix();
	UpdateProjMatrix();
	BuildViewFrustum();
	m_Factory->FrustumTest(GetFrustum(), this);
	//Tracef("cull process : %3d  ",ELTimer_GetMSec()-time);
}

CCullingManager::CullingHandle CCullingManager::Register(CGraphicObjectInstance * obj)
{
	assert(obj);
#ifdef COUNT_SHOWING_SPHERE
	Tracef("CR : %p  ",obj);
	showingcount++;
	Tracef("show size : %5d\n",showingcount);
#endif
	Vector3d center;
	float radius = 0.0f;
	const bool bHasBoundingSphere = obj->GetBoundingSphere(center, radius);

	if (!bHasBoundingSphere || !IsValidSphere(center, radius) || !IsReasonableSphere(center, radius))
	{
		const D3DXVECTOR3& vMin = obj->GetTBBoxMin();
		const D3DXVECTOR3& vMax = obj->GetTBBoxMax();
		center.Set(
			(vMin.x + vMax.x) * 0.5f,
			(vMin.y + vMax.y) * 0.5f,
			(vMin.z + vMax.z) * 0.5f);

		const float dx = vMax.x - vMin.x;
		const float dy = vMax.y - vMin.y;
		const float dz = vMax.z - vMin.z;
		radius = sqrtf(dx * dx + dy * dy + dz * dz) * 0.5f;

		if (!IsValidSphere(center, radius) || !IsReasonableSphere(center, radius))
		{
			const D3DXVECTOR3& vPos = obj->GetPosition();
			center.Set(vPos.x, vPos.y, vPos.z);
			radius = 120.0f;
		}

		static bool s_bLoggedCullingSphereFallback = false;
		if (!s_bLoggedCullingSphereFallback)
		{
			s_bLoggedCullingSphereFallback = true;
			TraceError("DX11_CULLING_SPHERE_FALLBACK reason=invalid_object_bounding_sphere");
		}
	}

	// Defensive clamp for corrupted/extreme bounds data that can violate SpherePack hierarchy assumptions.
	const float kMaxReasonableCullingRadius = 200000.0f;
	if (radius > kMaxReasonableCullingRadius)
	{
		static bool s_bLoggedCullingSphereClamp = false;
		if (!s_bLoggedCullingSphereClamp)
		{
			s_bLoggedCullingSphereClamp = true;
			TraceError(
				"DX11_CULLING_SPHERE_CLAMP reason=radius_too_large old=%.2f new=%.2f",
				radius,
				kMaxReasonableCullingRadius);
		}
		radius = kMaxReasonableCullingRadius;
	}

	return m_Factory->AddSphere_(center,radius,obj, false);
}

void CCullingManager::Unregister(CullingHandle h)
{
#ifdef COUNT_SHOWING_SPHERE
	if (((CGraphicObjectInstance*)h->GetUserData())->isShow())
	{
		Tracef("DE : %p  ",h->GetUserData());
		showingcount--;
		Tracef("show size : %5d\n",showingcount);
	}
#endif
	m_Factory->Remove(h);
}

CCullingManager::CCullingManager()
{
	m_Factory = new SpherePackFactory(
		10000,	// maximum count
		6400,	// root radius
		1600,	// leaf radius
		400		// extra radius
		);
}

CCullingManager::~CCullingManager()
{
	delete m_Factory;
}

void CCullingManager::FindRange(const Vector3d &p, float radius)
{
	m_list.clear();
	m_Factory->RangeTest(p, radius, this);
}

void CCullingManager::FindRay(const Vector3d &p1, const Vector3d &dir)
{
	m_RayFarDistance = -1;
	m_list.clear();
	m_Factory->RayTrace(p1,dir,this);
}

void CCullingManager::FindRayDistance(const Vector3d &p1, const Vector3d &dir, float distance)
{
	m_RayFarDistance = distance;
	m_list.clear();
	m_Factory->RayTrace(p1,dir,this);
}


