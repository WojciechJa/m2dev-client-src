#pragma once

// DirectXMath Helper Library for Pure DX11 Migration
// DX11 Model Sync M3-D3DX-CORE-CUT-58: Pure DirectXMath Implementation
// Replaces D3DXCompatMath.cpp with zero D3DX dependencies
// Uses DirectX::XMFLOAT* storage types and DirectX::XMVECTOR computation types

#include <DirectXMath.h>
#include "../../../extern/third_party/DirectXTK/Inc/SimpleMath.h"

namespace DXMath {
	using namespace DirectX;

	// ========================================================================
	// Vector2 Operations
	// ========================================================================

	inline void Vec2Normalize(XMFLOAT2* pOut, const XMFLOAT2* pV)
	{
		XMVECTOR v = XMLoadFloat2(pV);
		v = XMVector2Normalize(v);
		XMStoreFloat2(pOut, v);
	}

	inline float Vec2Length(const XMFLOAT2* pV)
	{
		XMVECTOR v = XMLoadFloat2(pV);
		return XMVectorGetX(XMVector2Length(v));
	}

	inline float Vec2Dot(const XMFLOAT2* pV1, const XMFLOAT2* pV2)
	{
		XMVECTOR v1 = XMLoadFloat2(pV1);
		XMVECTOR v2 = XMLoadFloat2(pV2);
		return XMVectorGetX(XMVector2Dot(v1, v2));
	}

	// ========================================================================
	// Vector3 Operations
	// ========================================================================

	inline void Vec3Normalize(XMFLOAT3* pOut, const XMFLOAT3* pV)
	{
		XMVECTOR v = XMLoadFloat3(pV);
		v = XMVector3Normalize(v);
		XMStoreFloat3(pOut, v);
	}

	inline void Vec3Cross(XMFLOAT3* pOut, const XMFLOAT3* pV1, const XMFLOAT3* pV2)
	{
		XMVECTOR v1 = XMLoadFloat3(pV1);
		XMVECTOR v2 = XMLoadFloat3(pV2);
		XMVECTOR result = XMVector3Cross(v1, v2);
		XMStoreFloat3(pOut, result);
	}

	inline float Vec3Dot(const XMFLOAT3* pV1, const XMFLOAT3* pV2)
	{
		XMVECTOR v1 = XMLoadFloat3(pV1);
		XMVECTOR v2 = XMLoadFloat3(pV2);
		return XMVectorGetX(XMVector3Dot(v1, v2));
	}

	inline float Vec3Length(const XMFLOAT3* pV)
	{
		XMVECTOR v = XMLoadFloat3(pV);
		return XMVectorGetX(XMVector3Length(v));
	}

	inline float Vec3LengthSq(const XMFLOAT3* pV)
	{
		XMVECTOR v = XMLoadFloat3(pV);
		return XMVectorGetX(XMVector3LengthSq(v));
	}

	inline void Vec3Add(XMFLOAT3* pOut, const XMFLOAT3* pV1, const XMFLOAT3* pV2)
	{
		pOut->x = pV1->x + pV2->x;
		pOut->y = pV1->y + pV2->y;
		pOut->z = pV1->z + pV2->z;
	}

	inline void Vec3Subtract(XMFLOAT3* pOut, const XMFLOAT3* pV1, const XMFLOAT3* pV2)
	{
		pOut->x = pV1->x - pV2->x;
		pOut->y = pV1->y - pV2->y;
		pOut->z = pV1->z - pV2->z;
	}

	inline void Vec3Scale(XMFLOAT3* pOut, const XMFLOAT3* pV, float s)
	{
		pOut->x = pV->x * s;
		pOut->y = pV->y * s;
		pOut->z = pV->z * s;
	}

	inline void Vec3Lerp(XMFLOAT3* pOut, const XMFLOAT3* pV1, const XMFLOAT3* pV2, float t)
	{
		XMVECTOR v1 = XMLoadFloat3(pV1);
		XMVECTOR v2 = XMLoadFloat3(pV2);
		XMVECTOR result = XMVectorLerp(v1, v2, t);
		XMStoreFloat3(pOut, result);
	}

	inline void Vec3TransformCoord(XMFLOAT3* pOut, const XMFLOAT3* pV, const XMFLOAT4X4* pM)
	{
		XMVECTOR v = XMLoadFloat3(pV);
		XMMATRIX m = XMLoadFloat4x4(pM);
		XMVECTOR result = XMVector3TransformCoord(v, m);
		XMStoreFloat3(pOut, result);
	}

	inline void Vec3TransformNormal(XMFLOAT3* pOut, const XMFLOAT3* pV, const XMFLOAT4X4* pM)
	{
		XMVECTOR v = XMLoadFloat3(pV);
		XMMATRIX m = XMLoadFloat4x4(pM);
		XMVECTOR result = XMVector3TransformNormal(v, m);
		XMStoreFloat3(pOut, result);
	}

	// ========================================================================
	// Vector4 Operations
	// ========================================================================

	inline void Vec4Normalize(XMFLOAT4* pOut, const XMFLOAT4* pV)
	{
		XMVECTOR v = XMLoadFloat4(pV);
		v = XMVector4Normalize(v);
		XMStoreFloat4(pOut, v);
	}

	inline float Vec4Dot(const XMFLOAT4* pV1, const XMFLOAT4* pV2)
	{
		XMVECTOR v1 = XMLoadFloat4(pV1);
		XMVECTOR v2 = XMLoadFloat4(pV2);
		return XMVectorGetX(XMVector4Dot(v1, v2));
	}

	inline void Vec4Transform(XMFLOAT4* pOut, const XMFLOAT4* pV, const XMFLOAT4X4* pM)
	{
		XMVECTOR v = XMLoadFloat4(pV);
		XMMATRIX m = XMLoadFloat4x4(pM);
		XMVECTOR result = XMVector4Transform(v, m);
		XMStoreFloat4(pOut, result);
	}

	// ========================================================================
	// Matrix Operations
	// ========================================================================

	inline void MatrixIdentity(XMFLOAT4X4* pOut)
	{
		XMStoreFloat4x4(pOut, XMMatrixIdentity());
	}

	inline void MatrixMultiply(XMFLOAT4X4* pOut, const XMFLOAT4X4* pM1, const XMFLOAT4X4* pM2)
	{
		XMMATRIX m1 = XMLoadFloat4x4(pM1);
		XMMATRIX m2 = XMLoadFloat4x4(pM2);
		XMMATRIX result = XMMatrixMultiply(m1, m2);
		XMStoreFloat4x4(pOut, result);
	}

	inline void MatrixTranspose(XMFLOAT4X4* pOut, const XMFLOAT4X4* pM)
	{
		XMMATRIX m = XMLoadFloat4x4(pM);
		XMMATRIX result = XMMatrixTranspose(m);
		XMStoreFloat4x4(pOut, result);
	}

	inline void MatrixInverse(XMFLOAT4X4* pOut, const XMFLOAT4X4* pM)
	{
		XMMATRIX m = XMLoadFloat4x4(pM);
		XMVECTOR det;
		XMMATRIX result = XMMatrixInverse(&det, m);
		XMStoreFloat4x4(pOut, result);
	}

	inline void MatrixRotationX(XMFLOAT4X4* pOut, float angle)
	{
		XMStoreFloat4x4(pOut, XMMatrixRotationX(angle));
	}

	inline void MatrixRotationY(XMFLOAT4X4* pOut, float angle)
	{
		XMStoreFloat4x4(pOut, XMMatrixRotationY(angle));
	}

	inline void MatrixRotationZ(XMFLOAT4X4* pOut, float angle)
	{
		XMStoreFloat4x4(pOut, XMMatrixRotationZ(angle));
	}

	inline void MatrixRotationYawPitchRoll(XMFLOAT4X4* pOut, float yaw, float pitch, float roll)
	{
		XMStoreFloat4x4(pOut, XMMatrixRotationRollPitchYaw(pitch, yaw, roll));
	}

	inline void MatrixRotationAxis(XMFLOAT4X4* pOut, const XMFLOAT3* pAxis, float angle)
	{
		XMVECTOR axis = XMLoadFloat3(pAxis);
		XMStoreFloat4x4(pOut, XMMatrixRotationAxis(axis, angle));
	}

	inline void MatrixRotationQuaternion(XMFLOAT4X4* pOut, const XMFLOAT4* pQ)
	{
		XMVECTOR q = XMLoadFloat4(pQ);
		XMStoreFloat4x4(pOut, XMMatrixRotationQuaternion(q));
	}

	inline void MatrixTranslation(XMFLOAT4X4* pOut, float x, float y, float z)
	{
		XMStoreFloat4x4(pOut, XMMatrixTranslation(x, y, z));
	}

	inline void MatrixScaling(XMFLOAT4X4* pOut, float sx, float sy, float sz)
	{
		XMStoreFloat4x4(pOut, XMMatrixScaling(sx, sy, sz));
	}

	inline void MatrixLookAtLH(XMFLOAT4X4* pOut, const XMFLOAT3* pEye, const XMFLOAT3* pAt, const XMFLOAT3* pUp)
	{
		XMVECTOR eye = XMLoadFloat3(pEye);
		XMVECTOR at = XMLoadFloat3(pAt);
		XMVECTOR up = XMLoadFloat3(pUp);
		XMStoreFloat4x4(pOut, XMMatrixLookAtLH(eye, at, up));
	}

	inline void MatrixPerspectiveFovLH(XMFLOAT4X4* pOut, float fovy, float aspect, float zn, float zf)
	{
		XMStoreFloat4x4(pOut, XMMatrixPerspectiveFovLH(fovy, aspect, zn, zf));
	}

	inline void MatrixOrthoLH(XMFLOAT4X4* pOut, float w, float h, float zn, float zf)
	{
		XMStoreFloat4x4(pOut, XMMatrixOrthographicLH(w, h, zn, zf));
	}

	// ========================================================================
	// Quaternion Operations
	// ========================================================================

	inline void QuaternionIdentity(XMFLOAT4* pOut)
	{
		XMStoreFloat4(pOut, XMQuaternionIdentity());
	}

	inline void QuaternionRotationYawPitchRoll(XMFLOAT4* pOut, float yaw, float pitch, float roll)
	{
		XMStoreFloat4(pOut, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
	}

	inline void QuaternionRotationAxis(XMFLOAT4* pOut, const XMFLOAT3* pAxis, float angle)
	{
		XMVECTOR axis = XMLoadFloat3(pAxis);
		XMStoreFloat4(pOut, XMQuaternionRotationAxis(axis, angle));
	}

	inline void QuaternionMultiply(XMFLOAT4* pOut, const XMFLOAT4* pQ1, const XMFLOAT4* pQ2)
	{
		XMVECTOR q1 = XMLoadFloat4(pQ1);
		XMVECTOR q2 = XMLoadFloat4(pQ2);
		XMVECTOR result = XMQuaternionMultiply(q1, q2);
		XMStoreFloat4(pOut, result);
	}

	inline void QuaternionNormalize(XMFLOAT4* pOut, const XMFLOAT4* pQ)
	{
		XMVECTOR q = XMLoadFloat4(pQ);
		q = XMQuaternionNormalize(q);
		XMStoreFloat4(pOut, q);
	}

	inline void QuaternionSlerp(XMFLOAT4* pOut, const XMFLOAT4* pQ1, const XMFLOAT4* pQ2, float t)
	{
		XMVECTOR q1 = XMLoadFloat4(pQ1);
		XMVECTOR q2 = XMLoadFloat4(pQ2);
		XMVECTOR result = XMQuaternionSlerp(q1, q2, t);
		XMStoreFloat4(pOut, result);
	}

	// ========================================================================
	// Plane Operations
	// ========================================================================

	inline void PlaneNormalize(XMFLOAT4* pOut, const XMFLOAT4* pP)
	{
		XMVECTOR p = XMLoadFloat4(pP);
		XMVECTOR result = XMPlaneNormalize(p);
		XMStoreFloat4(pOut, result);
	}

	inline float PlaneDot(const XMFLOAT4* pP, const XMFLOAT4* pV)
	{
		XMVECTOR p = XMLoadFloat4(pP);
		XMVECTOR v = XMLoadFloat4(pV);
		return XMVectorGetX(XMPlaneDot(p, v));
	}

	inline float PlaneDotCoord(const XMFLOAT4* pP, const XMFLOAT3* pV)
	{
		XMVECTOR p = XMLoadFloat4(pP);
		XMVECTOR v = XMLoadFloat3(pV);
		return XMVectorGetX(XMPlaneDotCoord(p, v));
	}

	inline float PlaneDotNormal(const XMFLOAT4* pP, const XMFLOAT3* pV)
	{
		XMVECTOR p = XMLoadFloat4(pP);
		XMVECTOR v = XMLoadFloat3(pV);
		return XMVectorGetX(XMPlaneDotNormal(p, v));
	}

	inline void PlaneFromPointNormal(XMFLOAT4* pOut, const XMFLOAT3* pPoint, const XMFLOAT3* pNormal)
	{
		XMVECTOR point = XMLoadFloat3(pPoint);
		XMVECTOR normal = XMLoadFloat3(pNormal);
		XMVECTOR result = XMPlaneFromPointNormal(point, normal);
		XMStoreFloat4(pOut, result);
	}

	inline void PlaneFromPoints(XMFLOAT4* pOut, const XMFLOAT3* pV1, const XMFLOAT3* pV2, const XMFLOAT3* pV3)
	{
		XMVECTOR v1 = XMLoadFloat3(pV1);
		XMVECTOR v2 = XMLoadFloat3(pV2);
		XMVECTOR v3 = XMLoadFloat3(pV3);
		XMVECTOR result = XMPlaneFromPoints(v1, v2, v3);
		XMStoreFloat4(pOut, result);
	}

	// ========================================================================
	// Color Operations (XMFLOAT4 with r,g,b,a semantics)
	// ========================================================================

	inline void ColorModulate(XMFLOAT4* pOut, const XMFLOAT4* pC1, const XMFLOAT4* pC2)
	{
		XMVECTOR c1 = XMLoadFloat4(pC1);
		XMVECTOR c2 = XMLoadFloat4(pC2);
		XMVECTOR result = XMVectorMultiply(c1, c2);
		XMStoreFloat4(pOut, result);
	}

	inline void ColorLerp(XMFLOAT4* pOut, const XMFLOAT4* pC1, const XMFLOAT4* pC2, float t)
	{
		XMVECTOR c1 = XMLoadFloat4(pC1);
		XMVECTOR c2 = XMLoadFloat4(pC2);
		XMVECTOR result = XMVectorLerp(c1, c2, t);
		XMStoreFloat4(pOut, result);
	}

} // namespace DXMath


