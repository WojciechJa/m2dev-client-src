#include "StdAfx.h"
#include "GrpCollisionObject.h"

using Matrix = DirectX::SimpleMath::Matrix;

bool CGraphicCollisionObject::IntersectBoundBox(const Matrix* c_pmatWorld, const TBoundBox& c_rboundBox, float* pu, float* pv, float* pt)
{
	return IntersectCube(c_pmatWorld, c_rboundBox.sx, c_rboundBox.sy, c_rboundBox.sz, c_rboundBox.ex, c_rboundBox.ey, c_rboundBox.ez, ms_vtPickRayOrig, ms_vtPickRayDir, pu, pv, pt);
}

bool CGraphicCollisionObject::IntersectCube(const Matrix* c_pmatWorld, float sx, float sy, float sz, float ex, float ey, float ez,
	TPosition& rayOriginal, TPosition& rayDirection, float* pu, float* pv, float* pt)
{
	TPosition posVertices[8];

	posVertices[0] = TPosition(sx, sy, sz);
	posVertices[1] = TPosition(ex, sy, sz);
	posVertices[2] = TPosition(sx, ey, sz);
	posVertices[3] = TPosition(ex, ey, sz);
	posVertices[4] = TPosition(sx, sy, ez);
	posVertices[5] = TPosition(ex, sy, ez);
	posVertices[6] = TPosition(sx, ey, ez);
	posVertices[7] = TPosition(ex, ey, ez);

	static const WORD c_awFillCubeIndices[36] = {
		0, 1, 2, 1, 3, 2,
		2, 0, 6, 0, 4, 6,
		0, 1, 4, 1, 5, 4,
		1, 3, 5, 3, 7, 5,
		3, 2, 7, 2, 6, 7,
		4, 5, 6, 5, 7, 6,
	};

	return IntersectIndexedMesh(c_pmatWorld, posVertices, sizeof(TPosition), 8, c_awFillCubeIndices, 36, rayOriginal, rayDirection, pu, pv, pt);
}

const int c_iLimitVertexCount = 1024;

bool CGraphicCollisionObject::IntersectIndexedMesh(const Matrix* c_pmatWorld, const void* vertices, int step, int vtxCount, const void* indices, int idxCount,
	TPosition& rayOriginal, TPosition& rayDirection, float* pu, float* pv, float* pt)
{
	static TPosition s_v3PositionArray[c_iLimitVertexCount];
	static DWORD s_dwPositionCount;

	if (vtxCount > c_iLimitVertexCount)
	{
		Tracef("The vertex count of mesh which is worked collision detection is too much : %d / %d", vtxCount, c_iLimitVertexCount);
		return false;
	}

	s_dwPositionCount = 0;

	char* pcurVtx = (char*)vertices;
	while (vtxCount--)
	{
		float* pos = (float*)pcurVtx;
		const TPosition srcPos(pos[0], pos[1], pos[2]);
		DXMath::Vec3TransformCoord(&s_v3PositionArray[s_dwPositionCount++], &srcPos, c_pmatWorld);
		pcurVtx += step;
	}

	WORD* pcurIdx = (WORD*)indices;
	int triCount = idxCount / 3;
	while (triCount--)
	{
		if (IntersectTriangle(rayOriginal, rayDirection,
			s_v3PositionArray[pcurIdx[0]],
			s_v3PositionArray[pcurIdx[1]],
			s_v3PositionArray[pcurIdx[2]],
			pu, pv, pt))
		{
			return true;
		}
		pcurIdx += 3;
	}

	return false;
}

bool CGraphicCollisionObject::IntersectMesh(const Matrix* c_pmatWorld, const void* vertices, DWORD dwStep, DWORD dwvtxCount,
	TPosition& rayOriginal, TPosition& rayDirection, float* pu, float* pv, float* pt)
{
	char* pcurVtx = (char*)vertices;
	TPosition v3Vertex[3];

	for (DWORD i = 0; i < dwvtxCount; i += 3)
	{
		for (int v = 0; v < 3; ++v)
		{
			float* pos = (float*)pcurVtx;
			const TPosition srcPos(pos[0], pos[1], pos[2]);
			DXMath::Vec3TransformCoord(&v3Vertex[v], &srcPos, c_pmatWorld);
			pcurVtx += dwStep;
		}

		if (IntersectTriangle(rayOriginal, rayDirection, v3Vertex[0], v3Vertex[1], v3Vertex[2], pu, pv, pt))
		{
			return true;
		}
	}

	return false;
}

bool CGraphicCollisionObject::IntersectTriangle(const TPosition& c_orig, const TPosition& c_dir, const TPosition& c_v0,
	const TPosition& c_v1, const TPosition& c_v2, float* pu, float* pv, float* pt)
{
	TPosition edge1 = c_v1 - c_v0;
	TPosition edge2 = c_v2 - c_v0;
	TPosition pvec;
	DXMath::Vec3Cross(&pvec, &c_dir, &edge2);

	float det = DXMath::Vec3Dot(&edge1, &pvec);
	TPosition tvec;

	if (det > 0.0f)
		tvec = c_orig - c_v0;
	else
	{
		tvec = c_v0 - c_orig;
		det = -det;
	}

	if (det < 0.0001f)
		return false;

	float u = DXMath::Vec3Dot(&tvec, &pvec);
	if (u < 0.0f || u > det)
		return false;

	TPosition qvec;
	DXMath::Vec3Cross(&qvec, &tvec, &edge1);

	float v = DXMath::Vec3Dot(&c_dir, &qvec);
	if (v < 0.0f || u + v > det)
		return false;

	float t = DXMath::Vec3Dot(&edge2, &qvec);
	const float fInvDet = 1.0f / det;
	t *= fInvDet;
	u *= fInvDet;
	v *= fInvDet;

	TPosition spot = edge1 * u + edge2 * v;
	spot += c_v0;

	*pu = spot.x;
	*pv = spot.y;
	*pt = t;
	return true;
}

bool CGraphicCollisionObject::IntersectSphere(const TPosition& c_rv3Position, float fRadius, const TPosition& c_rv3RayOriginal, const TPosition& c_rv3RayDirection)
{
	TPosition v3RayOriginal = c_rv3RayOriginal - c_rv3Position;

	const float a = DXMath::Vec3Dot(&c_rv3RayDirection, &c_rv3RayDirection);
	const float b = 2.0f * DXMath::Vec3Dot(&v3RayOriginal, &c_rv3RayDirection);
	const float c = DXMath::Vec3Dot(&v3RayOriginal, &v3RayOriginal) - fRadius * fRadius;
	const float D = b * b - 4.0f * a * c;

	return D >= 0.0f;
}

bool CGraphicCollisionObject::IntersectCylinder(const TPosition& c_rv3Position, float fRadius, float fHeight, const TPosition& c_rv3RayOriginal, const TPosition& c_rv3RayDirection)
{
	TPosition v3RayOriginal = c_rv3RayOriginal - c_rv3Position;

	const float a = c_rv3RayDirection.x * c_rv3RayDirection.x + c_rv3RayDirection.y * c_rv3RayDirection.y;
	const float b = 2.0f * (v3RayOriginal.x * c_rv3RayDirection.x + v3RayOriginal.y * c_rv3RayDirection.y);
	const float c = v3RayOriginal.x * v3RayOriginal.x + v3RayOriginal.y * v3RayOriginal.y - fRadius * fRadius;

	const float D = b * b - 4.0f * a * c;
	if (D > 0.0f && a != 0.0f)
	{
		const float tPlus = (-b + sqrtf(D)) / (2.0f * a);
		const float tMinus = (-b - sqrtf(D)) / (2.0f * a);
		const float fzPlus = v3RayOriginal.z + tPlus * c_rv3RayDirection.z;
		const float fzMinus = v3RayOriginal.z + tMinus * c_rv3RayDirection.z;

		if ((fzPlus > 0.0f && fzPlus <= fHeight) ||
			(fzMinus > 0.0f && fzMinus <= fHeight) ||
			(fzMinus * fzPlus < 0.0f))
		{
			return true;
		}
	}

	return false;
}

bool CGraphicCollisionObject::IntersectSphere(const TPosition& c_rv3Position, float fRadius)
{
	return CGraphicCollisionObject::IntersectSphere(c_rv3Position, fRadius, ms_vtPickRayOrig, ms_vtPickRayDir);
}

bool CGraphicCollisionObject::IntersectCylinder(const TPosition& c_rv3Position, float fRadius, float fHeight)
{
	return CGraphicCollisionObject::IntersectCylinder(c_rv3Position, fRadius, fHeight, ms_vtPickRayOrig, ms_vtPickRayDir);
}

CGraphicCollisionObject::CGraphicCollisionObject()
{
}

CGraphicCollisionObject::~CGraphicCollisionObject()
{
}

