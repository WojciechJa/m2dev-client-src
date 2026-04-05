#pragma once

#include <d3d11.h>
#include "GrpBase.h"

// Collision Detection
typedef struct SSphereData
{
	TPosition v3Position;
	float		fRadius;
} TSphereData;

typedef struct SPlaneData
{
	TPosition v3Position;
	TPosition v3Normal;

	TPosition v3QuadPosition[4];
	TPosition v3InsideVector[4];
} TPlaneData;

typedef struct SAABBData
{
	TPosition v3Min;
	TPosition v3Max;

} TAABBData;

typedef struct SOBBData
{
	TPosition v3Min;
	TPosition v3Max;
	DirectX::SimpleMath::Matrix matRot;

} TOBBData;

typedef struct SCylinderData
{
	TPosition v3Position;
	float fRadius;
	float fHeight;
} TCylinderData;

enum ECollisionType
{
	COLLISION_TYPE_PLANE,
	COLLISION_TYPE_BOX,
	COLLISION_TYPE_SPHERE,
	COLLISION_TYPE_CYLINDER,
	COLLISION_TYPE_AABB,
	COLLISION_TYPE_OBB,
};

struct CDynamicSphereInstance
{
	TPosition v3Position;
	TPosition v3LastPosition;

	float fRadius;
};

class CStaticCollisionData
{
public:
	DWORD dwType;
	char szName[32+1];

	TPosition v3Position;
	float fDimensions[3];
	DirectX::SimpleMath::Quaternion quatRotation;
};

void DestroyCollisionInstanceSystem();

typedef std::vector<CStaticCollisionData> CStaticCollisionDataVector;

/////////////////////////////////////////////
// Base
class CBaseCollisionInstance
{
	public:
		virtual void Render(D3D11_FILL_MODE fillMode = D3D11_FILL_SOLID) = 0;

		bool MovementCollisionDynamicSphere(const CDynamicSphereInstance & s) const
		{
			return OnMovementCollisionDynamicSphere(s);
		}
		bool CollisionDynamicSphere(const CDynamicSphereInstance & s) const
		{
			return OnCollisionDynamicSphere(s);
		}
		

		TPosition GetCollisionMovementAdjust(const CDynamicSphereInstance & s) const
		{
			return OnGetCollisionMovementAdjust(s);
		}

		void Destroy();

		static CBaseCollisionInstance * BuildCollisionInstance(const CStaticCollisionData * c_pCollisionData, const DirectX::SimpleMath::Matrix * pMat);

	protected:
		virtual TPosition OnGetCollisionMovementAdjust(const CDynamicSphereInstance & s) const = 0;
		virtual bool OnMovementCollisionDynamicSphere(const CDynamicSphereInstance & s) const  = 0;
		virtual bool OnCollisionDynamicSphere(const CDynamicSphereInstance & s) const  = 0;
		virtual void OnDestroy() = 0;
};

/////////////////////////////////////////////
// Sphere
class CSphereCollisionInstance : public CBaseCollisionInstance
{
	public:
		TSphereData & GetAttribute();
		const TSphereData & GetAttribute() const;
		virtual void Render(D3D11_FILL_MODE fillMode = D3D11_FILL_SOLID);

	protected:
		void OnDestroy();
		bool OnMovementCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual bool OnCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual TPosition OnGetCollisionMovementAdjust(const CDynamicSphereInstance & s) const;

	protected:
		TSphereData m_attribute;
};

/////////////////////////////////////////////
// Plane
class CPlaneCollisionInstance : public CBaseCollisionInstance
{
	public:
		TPlaneData & GetAttribute();
		const TPlaneData & GetAttribute() const;
		virtual void Render(D3D11_FILL_MODE fillMode = D3D11_FILL_SOLID);

	protected:
		void OnDestroy();
		bool OnMovementCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual bool OnCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual TPosition OnGetCollisionMovementAdjust(const CDynamicSphereInstance & s) const;

	protected:
		TPlaneData m_attribute;
};

/////////////////////////////////////////////
// AABB (Aligned Axis Bounding Box)
class CAABBCollisionInstance : public CBaseCollisionInstance
{
	public:
		TAABBData & GetAttribute();
		const TAABBData & GetAttribute() const;
		virtual void Render(D3D11_FILL_MODE fillMode = D3D11_FILL_SOLID);

	protected:
		void OnDestroy();
		bool OnMovementCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual bool OnCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual TPosition OnGetCollisionMovementAdjust(const CDynamicSphereInstance & s) const;

	protected:
		TAABBData m_attribute;
};

/////////////////////////////////////////////
// OBB
class COBBCollisionInstance : public CBaseCollisionInstance
{
	public:
		TOBBData & GetAttribute();
		const TOBBData & GetAttribute() const;
		virtual void Render(D3D11_FILL_MODE fillMode = D3D11_FILL_SOLID);

	protected:
		void OnDestroy();
		bool OnMovementCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual bool OnCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual TPosition OnGetCollisionMovementAdjust(const CDynamicSphereInstance & s) const;

	protected:
		TOBBData m_attribute;
};

/////////////////////////////////////////////
// Cylinder
class CCylinderCollisionInstance : public CBaseCollisionInstance
{
	public:
		TCylinderData & GetAttribute();
		const TCylinderData & GetAttribute() const;
		virtual void Render(D3D11_FILL_MODE fillMode = D3D11_FILL_SOLID);

	protected:
		void OnDestroy();
		bool OnMovementCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual bool OnCollisionDynamicSphere(const CDynamicSphereInstance & s) const;
		virtual TPosition OnGetCollisionMovementAdjust(const CDynamicSphereInstance & s) const;

		bool CollideCylinderVSDynamicSphere(const TCylinderData & c_rattribute, const CDynamicSphereInstance & s) const;

	protected:
		TCylinderData m_attribute;
};

typedef std::vector<CSphereCollisionInstance> CSphereCollisionInstanceVector;
typedef std::vector<CDynamicSphereInstance> CDynamicSphereInstanceVector;
typedef std::vector<CBaseCollisionInstance*> CCollisionInstanceVector;
