// TerrainPatch.h: interface for the CTerrainPatch class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_TERRAINPATCH_H__CDD52438_D542_433C_8748_3A15C910A65E__INCLUDED_)
#define AFX_TERRAINPATCH_H__CDD52438_D542_433C_8748_3A15C910A65E__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "EterLib/GrpVertexBuffer.h"
#include "PRTerrainLib/Terrain.h"

// DX11 forward declarations
struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11DeviceContext;

#pragma pack(push)
#pragma pack(1)

struct HardwareTransformPatch_SSourceVertex
{
	D3DXVECTOR3 kPosition;
	D3DXVECTOR3 kNormal;
};

struct SWaterVertex
{
    float x, y, z;          // position
	DWORD dwDiffuse;
};

// DX11 terrain vertex format
struct DX11TerrainVertex
{
	D3DXVECTOR3 kPosition;   // POSITION
	D3DXVECTOR3 kNormal;     // NORMAL
	D3DXVECTOR2 kTexCoord;   // TEXCOORD0 (world-space XY for legacy texture transform parity)
};

// DX11 water vertex format
struct DX11WaterVertex
{
	D3DXVECTOR3 kPosition;   // POSITION
	D3DXVECTOR4 kColor;      // COLOR (RGBA with alpha for depth-based transparency)
};
#pragma pack(pop)

class CTerrainPatch
{
public:
	enum
	{
		PATCH_TYPE_PLAIN = 0,
		PATCH_TYPE_HILL,
		PATCH_TYPE_CLIFF,
	};

	enum
	{
		TERRAIN_VERTEX_COUNT = (CTerrainImpl::PATCH_XSIZE+1)*(CTerrainImpl::PATCH_YSIZE+1)
	};

public:
	CTerrainPatch()									{ Clear(); }
	~CTerrainPatch()									{ Clear(); }
	
	void Clear();

	void ClearID()											{ SetID(0xFFFFFFFF); }
	
	void SetMinX(float fMinX)								{ m_fMinX = fMinX; }
	float GetMinX()											{ return m_fMinX; }
	
	void SetMaxX(float fMaxX)								{ m_fMaxX = fMaxX; }
	float GetMaxX()											{ return m_fMaxX; }
	
	void SetMinY(float fMinY)								{ m_fMinY = fMinY; }
	float GetMinY()											{ return m_fMinY; }
	
	void SetMaxY(float fMaxY)								{ m_fMaxY = fMaxY; }
	float GetMaxY()											{ return m_fMaxY; }
	
	void SetMinZ(float fMinZ)								{ m_fMinZ = fMinZ; }
	float GetMinZ()											{ return m_fMinZ; }
	
	void SetMaxZ(float fMaxZ)								{ m_fMaxZ = fMaxZ; }
	float GetMaxZ()											{ return m_fMaxZ; }
	
	bool IsUse()											{ return m_bUse; }
	void SetUse(bool bUse)									{ m_bUse = bUse; }
	
	bool IsWaterExist()										{ return m_bWaterExist; }
	void SetWaterExist(bool bWaterExist)					{ m_bWaterExist = bWaterExist; }
	
	DWORD GetID()											{ return m_dwID; }
	void SetID(DWORD dwID)									{ m_dwID = dwID; }
	
	void SetType(BYTE byType)								{ m_byType = byType; }
	BYTE GetType()											{ return m_byType; }

	void NeedUpdate(bool bNeedUpdate)						{ m_bNeedUpdate = bNeedUpdate;}
	bool NeedUpdate()										{ return m_bNeedUpdate; }

	UINT GetWaterFaceCount();

	void BuildTerrainVertexBuffer(HardwareTransformPatch_SSourceVertex* akSrcVertex);
	void BuildWaterVertexBuffer(SWaterVertex* akSrcVertex, UINT uWaterVertexCount);

	// DX11 terrain vertex buffer
	void BuildDX11TerrainVertexBuffer(HardwareTransformPatch_SSourceVertex* akSrcVertex, ID3D11Device* pDevice);
	bool BuildDX11TerrainVertexBufferFromCache(ID3D11Device* pDevice)
	{
		if (!m_kDX11.m_akCachedSourceVertex || !pDevice)
			return false;

		BuildDX11TerrainVertexBuffer(m_kDX11.m_akCachedSourceVertex, pDevice);
		return IsDX11VertexBufferReady();
	}
	ID3D11Buffer* GetDX11VertexBuffer() { return m_kDX11.m_pVertexBuffer; }
	bool IsDX11VertexBufferReady() const { return m_kDX11.m_pVertexBuffer != nullptr; }
	bool HasDX11CachedSourceVertex() const { return m_kDX11.m_akCachedSourceVertex != nullptr; } // B6.1: For telemetry
	const HardwareTransformPatch_SSourceVertex* GetDX11CachedSourceVertex() const { return m_kDX11.m_akCachedSourceVertex; }

	// DX11 water vertex buffer
	void BuildDX11WaterVertexBuffer(SWaterVertex* akSrcWaterVertex, UINT uVertexCount, ID3D11Device* pDevice);
	bool BuildDX11WaterVertexBufferFromCache(ID3D11Device* pDevice)
	{
		return m_kDX11Water.CreateFromCache(pDevice);
	}
	ID3D11Buffer* GetDX11WaterVertexBuffer() { return m_kDX11Water.m_pWaterVertexBuffer; }
	UINT GetDX11WaterVertexCount() const { return m_kDX11Water.m_uWaterVertexCount; }
	bool IsDX11WaterVertexBufferReady() const { return m_kDX11Water.m_pWaterVertexBuffer != nullptr; }
	bool HasDX11CachedWaterSourceVertex() const { return m_kDX11Water.m_akCachedSourceWaterVertex != nullptr && m_kDX11Water.m_uCachedSourceWaterVertexCount > 0u; }

protected:
	void __BuildHardwareTerrainVertexBuffer(HardwareTransformPatch_SSourceVertex* akSrcVertex);
	void __BuildDX11TerrainVertexBuffer(HardwareTransformPatch_SSourceVertex* akSrcVertex, ID3D11Device* pDevice);
	
private:
	float					m_fMinX;
	float					m_fMaxX;
	float					m_fMinY;
	float					m_fMaxY;
	float					m_fMinZ;
	float					m_fMaxZ;
	bool					m_bUse;
	bool					m_bWaterExist;
	DWORD					m_dwID;
	DWORD m_dwWaterPriCount;
	
	CGraphicVertexBuffer	m_WaterVertexBuffer;
	BYTE					m_byType;

	bool					m_bNeedUpdate;
	DWORD					m_dwVersion;

public:
	CGraphicVertexBuffer* GetWaterVertexBufferPointer()	{ return &m_WaterVertexBuffer;}

public:
	CGraphicVertexBuffer* HardwareTransformPatch_GetVertexBufferPtr() {return &m_kHT.m_kVB;}

protected:
	struct SHardwareTransformPatch
	{
		CGraphicVertexBuffer	m_kVB;
	} m_kHT;


protected:
	struct SDX11TerrainPatch
	{
		ID3D11Buffer* m_pVertexBuffer;
		HardwareTransformPatch_SSourceVertex* m_akCachedSourceVertex; // Cached for deferred VB creation

		SDX11TerrainPatch();
		~SDX11TerrainPatch();

		void CacheSourceData(HardwareTransformPatch_SSourceVertex* akSrcVertex);
		void Create(HardwareTransformPatch_SSourceVertex* akSrcVertex, ID3D11Device* pDevice);
		void Destroy();

		void __Initialize();
	} m_kDX11;

	struct SDX11WaterPatch
	{
		ID3D11Buffer* m_pWaterVertexBuffer;
		UINT m_uWaterVertexCount;
		SWaterVertex* m_akCachedSourceWaterVertex;
		UINT m_uCachedSourceWaterVertexCount;

		SDX11WaterPatch();
		~SDX11WaterPatch();

		void CacheSourceData(SWaterVertex* akSrcWaterVertex, UINT uVertexCount);
		bool CreateFromCache(ID3D11Device* pDevice);
		void Create(SWaterVertex* akSrcWaterVertex, UINT uVertexCount, ID3D11Device* pDevice);
		void Destroy();
		void DestroyIncludingCache();  // M2-WATER-DRAW-PARITY-74: Explicit full cleanup (frees cache)

		void __Initialize();
	} m_kDX11Water;

};

class CTerrainPatchProxy  
{
public:
	CTerrainPatchProxy();
	virtual ~CTerrainPatchProxy();

	void Clear();

	void SetCenterPosition(const D3DXVECTOR3& c_rv3Center);

	bool IsIn(const D3DXVECTOR3& c_rv3Target, float fRadius);

	bool isUsed()																	{ return m_bUsed; }
	void SetUsed(bool bUsed)														{ m_bUsed = bUsed; }

	short GetPatchNum()																{ return m_sPatchNum; }
	void SetPatchNum(short sPatchNum)												{ m_sPatchNum = sPatchNum; }

	BYTE GetTerrainNum()															{ return m_byTerrainNum; }
	void SetTerrainNum(BYTE byTerrainNum)											{ m_byTerrainNum = byTerrainNum; }

	void SetTerrainPatch(CTerrainPatch * pTerrainPatch)								{ m_pTerrainPatch = pTerrainPatch;}

	bool isWaterExists();

	UINT GetWaterFaceCount();

	float GetMinX();
	float GetMaxX();
	float GetMinY();
	float GetMaxY();
	float GetMinZ();
	float GetMaxZ();

	// Vertex Buffer
	CGraphicVertexBuffer * GetWaterVertexBufferPointer();
	CGraphicVertexBuffer* HardwareTransformPatch_GetVertexBufferPtr();

	// DX11 Vertex Buffer access
	ID3D11Buffer* GetDX11VertexBuffer();
	bool IsDX11VertexBufferReady() const;
	CTerrainPatch* GetTerrainPatch() const { return m_pTerrainPatch; }

protected:
	bool					m_bUsed;
	short					m_sPatchNum;	// Patch Number

	BYTE					m_byTerrainNum;	

	CTerrainPatch *			m_pTerrainPatch;

	D3DXVECTOR3				m_v3Center;
};

inline bool CTerrainPatchProxy::isWaterExists()
{
	return m_pTerrainPatch->IsWaterExist();
}

inline float CTerrainPatchProxy::GetMinX()
{
	return m_pTerrainPatch->GetMinX();
}

inline float CTerrainPatchProxy::GetMaxX()
{
	return m_pTerrainPatch->GetMaxX();
}

inline float CTerrainPatchProxy::GetMinY()
{
	return m_pTerrainPatch->GetMinY();
}

inline float CTerrainPatchProxy::GetMaxY()
{
	return m_pTerrainPatch->GetMaxY();
}

inline float CTerrainPatchProxy::GetMinZ()
{
	return m_pTerrainPatch->GetMinZ();
}

inline float CTerrainPatchProxy::GetMaxZ()
{
	return m_pTerrainPatch->GetMaxZ();
}

inline CGraphicVertexBuffer * CTerrainPatchProxy::GetWaterVertexBufferPointer()
{
	return m_pTerrainPatch->GetWaterVertexBufferPointer();
}

inline ID3D11Buffer* CTerrainPatchProxy::GetDX11VertexBuffer()
{
	return m_pTerrainPatch ? m_pTerrainPatch->GetDX11VertexBuffer() : nullptr;
}

inline bool CTerrainPatchProxy::IsDX11VertexBufferReady() const
{
	return m_pTerrainPatch ? m_pTerrainPatch->IsDX11VertexBufferReady() : false;
}

#endif // !defined(AFX_TERRAINPATCH_H__CDD52438_D542_433C_8748_3A15C910A65E__INCLUDED_)
