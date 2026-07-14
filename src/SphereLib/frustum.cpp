/* Copyright (C) John W. Ratcliff, 2001. 
 * All rights reserved worldwide.
 *
 * This software is provided "as is" without express or implied
 * warranties. You may freely copy and compile this source into
 * applications you distribute provided that the copyright text
 * below is included in the resulting source code, for example:
 * "Portions Copyright (C) John W. Ratcliff, 2001"
 */

#include "Stdafx.h"
#include "frustum.h"

//#include "frustum.h"

/*void Frustum::Set(int x1,int y1,int x2,int y2)
{
  mX1 = x1;
  mY1 = y1;
  mX2 = x2;
  mY2 = y2;
}

*/
ViewState Frustum::ViewVolumeTest(const Vector3d &c_v3Center,const float c_fRadius) const
{
	if (m_bUsingSphere)
	{
		DirectX::SimpleMath::Vector3 v(
				c_v3Center.x-m_v3Center.x,
				c_v3Center.y-m_v3Center.y,
				c_v3Center.z-m_v3Center.z);

		if ((c_fRadius + m_fRadius) * (c_fRadius + m_fRadius) < v.LengthSquared())
		{
			return VS_OUTSIDE;
		}
	}	
	
	const int count=6;

	DirectX::SimpleMath::Vector3 center = c_v3Center;
	//center.y *=-1;

	int i;

	float distance[count];
	for(i=0;i<count;i++)
	{
		distance[i] = m_plane[i].DotCoordinate(center);
		if (distance[i]<=-c_fRadius)
			return VS_OUTSIDE;
	}

	//return VS_INSIDE;

	for(i=0;i<count;i++)
	{
		if (distance[i]<=c_fRadius) 
			return VS_PARTIAL;
	}
	
	return VS_INSIDE;
}

void Frustum::BuildViewFrustum(DirectX::SimpleMath::Matrix & mat)
{
	m_bUsingSphere = false;
	m_plane[0] = DirectX::SimpleMath::Plane(          mat._13,           mat._23,           mat._33,           mat._43);
	m_plane[1] = DirectX::SimpleMath::Plane(mat._14 - mat._13, mat._24 - mat._23, mat._34 - mat._33, mat._44 - mat._43);
	//m_plane[0] = DirectX::SimpleMath::Plane(mat._14 + mat._13, mat._24 + mat._23, mat._34 + mat._33, mat._44 + mat._43);
	m_plane[2] = DirectX::SimpleMath::Plane(mat._14 + mat._11, mat._24 + mat._21, mat._34 + mat._31, mat._44 + mat._41);
	m_plane[3] = DirectX::SimpleMath::Plane(mat._14 - mat._11, mat._24 - mat._21, mat._34 - mat._31, mat._44 - mat._41);
	m_plane[4] = DirectX::SimpleMath::Plane(mat._14 + mat._12, mat._24 + mat._22, mat._34 + mat._32, mat._44 + mat._42);
	m_plane[5] = DirectX::SimpleMath::Plane(mat._14 - mat._12, mat._24 - mat._22, mat._34 - mat._32, mat._44 - mat._42);

	for(int i=0;i<6;i++)
		m_plane[i].Normalize();
}

void Frustum::BuildViewFrustum2(DirectX::SimpleMath::Matrix & mat, float fNear, float fFar, float fFov, float fAspect, const DirectX::SimpleMath::Vector3 & vCamera, const DirectX::SimpleMath::Vector3 & vLook)
{
	float fViewLen = fFar-fNear;
	float fH = fViewLen * tan(fFov*0.5f);
	float fW = fH*fAspect;
	DirectX::SimpleMath::Vector3 P(0.0f, 0.0f, fNear+fViewLen*0.5f);
	DirectX::SimpleMath::Vector3 Q(fW, fH, fViewLen);
	DirectX::SimpleMath::Vector3 PQ = P-Q;
	m_fRadius = PQ.Length();
	m_v3Center = vCamera + vLook * (fNear+fViewLen*0.5f);
	BuildViewFrustum(mat);
	m_bUsingSphere = true;
}