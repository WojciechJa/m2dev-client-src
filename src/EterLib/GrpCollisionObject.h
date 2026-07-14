#pragma once

#include "GrpBase.h"

class CGraphicCollisionObject : public CGraphicBase
{
public:
	CGraphicCollisionObject();
	virtual ~CGraphicCollisionObject();

protected:
	using Matrix = DirectX::SimpleMath::Matrix;

	bool IntersectTriangle(const TPosition& c_orig, const TPosition& c_dir, const TPosition& c_v0, const TPosition& c_v1, const TPosition& c_v2, float* pu, float* pv, float* pt);
	bool IntersectBoundBox(const Matrix* c_pmatWorld, const TBoundBox& c_rboundBox, float* pu, float* pv, float* pt);
	bool IntersectCube(const Matrix* c_pmatWorld, float sx, float sy, float sz, float ex, float ey, float ez, TPosition& rayOriginal, TPosition& rayDirection, float* pu, float* pv, float* pt);
	bool IntersectIndexedMesh(const Matrix* c_pmatWorld, const void* vertices, int step, int vtxCount, const void* indices, int idxCount, TPosition& rayOriginal, TPosition& rayDirection, float* pu, float* pv, float* pt);
	bool IntersectMesh(const Matrix* c_pmatWorld, const void* vertices, DWORD dwStep, DWORD dwvtxCount, TPosition& rayOriginal, TPosition& rayDirection, float* pu, float* pv, float* pt);

	bool IntersectSphere(const TPosition& c_rv3Position, float fRadius, const TPosition& c_rv3RayOriginal, const TPosition& c_rv3RayDirection);
	bool IntersectCylinder(const TPosition& c_rv3Position, float fRadius, float fHeight, const TPosition& c_rv3RayOriginal, const TPosition& c_rv3RayDirection);

	bool IntersectSphere(const TPosition& c_rv3Position, float fRadius);
	bool IntersectCylinder(const TPosition& c_rv3Position, float fRadius, float fHeight);
};