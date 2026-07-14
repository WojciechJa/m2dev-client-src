// TerrainPatch.cpp: implementation of the CTerrainPatch class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "TerrainPatch.h"
#include <d3d11.h>
#include "BufferHelpers.h"  // Phase 3: DirectXTK buffer helpers

//////////////////////////////////////////////////////////////////////
// DX11 Terrain Patch
//////////////////////////////////////////////////////////////////////

CTerrainPatch::SDX11TerrainPatch::SDX11TerrainPatch()
{
	__Initialize();
}

CTerrainPatch::SDX11TerrainPatch::~SDX11TerrainPatch()
{
	Destroy();
}

void CTerrainPatch::SDX11TerrainPatch::CacheSourceData(HardwareTransformPatch_SSourceVertex* akSrcVertex)
{
	if (!akSrcVertex)
		return;

	// Free existing cache
	if (m_akCachedSourceVertex)
	{
		delete[] m_akCachedSourceVertex;
		m_akCachedSourceVertex = nullptr;
	}

	// Allocate and cache source vertex data
	m_akCachedSourceVertex = new HardwareTransformPatch_SSourceVertex[CTerrainPatch::TERRAIN_VERTEX_COUNT];
	memcpy(m_akCachedSourceVertex, akSrcVertex, sizeof(HardwareTransformPatch_SSourceVertex) * CTerrainPatch::TERRAIN_VERTEX_COUNT);
}

void CTerrainPatch::SDX11TerrainPatch::Create(HardwareTransformPatch_SSourceVertex* akSrcVertex, ID3D11Device* pDevice)
{
	if (!pDevice || !akSrcVertex)
		return;

	// IMPORTANT:
	// Do not call Destroy() here. Destroy() also frees m_akCachedSourceVertex.
	// Rebuild-from-cache path passes m_akCachedSourceVertex as akSrcVertex,
	// so Destroy() would invalidate the source pointer before conversion.
	if (m_pVertexBuffer)
	{
		m_pVertexBuffer->Release();
		m_pVertexBuffer = nullptr;
	}

	// Convert DX9 vertex format to DX11 format with UVs
	DX11TerrainVertex akDX11Vertices[CTerrainPatch::TERRAIN_VERTEX_COUNT];
	for (UINT uIndex = 0; uIndex < CTerrainPatch::TERRAIN_VERTEX_COUNT; ++uIndex)
	{
		akDX11Vertices[uIndex].kPosition = akSrcVertex[uIndex].kPosition;
		akDX11Vertices[uIndex].kNormal = akSrcVertex[uIndex].kNormal;

		// Keep texture-coordinate source in world space for legacy fixed-function parity.
		// Per-layer scale/offset is applied later from TTerrainTexture::m_matTransform.
		akDX11Vertices[uIndex].kTexCoord.x = akSrcVertex[uIndex].kPosition.x;
		akDX11Vertices[uIndex].kTexCoord.y = akSrcVertex[uIndex].kPosition.y;
	}

	// Phase 3: Use BufferHelpers for deterministic VB creation
	HRESULT hr = DirectX::CreateStaticBuffer(
		pDevice,
		akDX11Vertices,
		CTerrainPatch::TERRAIN_VERTEX_COUNT,
		D3D11_BIND_VERTEX_BUFFER,
		&m_pVertexBuffer);

	if (FAILED(hr) || !m_pVertexBuffer)
	{
		m_pVertexBuffer = nullptr;
		// B6.2: Always log VB creation failures (critical diagnostic)
		TraceError("DX11_TERRAIN_VB_CREATE_FAILED hr=0x%08X count=%u size=%u device=%p",
			static_cast<unsigned int>(hr),
			CTerrainPatch::TERRAIN_VERTEX_COUNT,
			static_cast<unsigned int>(sizeof(DX11TerrainVertex) * CTerrainPatch::TERRAIN_VERTEX_COUNT),
			pDevice);
	}
}

void CTerrainPatch::SDX11TerrainPatch::Destroy()
{
	if (m_pVertexBuffer)
	{
		m_pVertexBuffer->Release();
		m_pVertexBuffer = nullptr;
	}

	if (m_akCachedSourceVertex)
	{
		delete[] m_akCachedSourceVertex;
		m_akCachedSourceVertex = nullptr;
	}

	__Initialize();
}

void CTerrainPatch::SDX11TerrainPatch::__Initialize()
{
	m_pVertexBuffer = nullptr;
	m_akCachedSourceVertex = nullptr;
}

void CTerrainPatch::Clear()
{
	m_kHT.m_kVB.Destroy();
	m_kDX11.Destroy(); // DX11 terrain cleanup
	m_kDX11Water.DestroyIncludingCache(); // DX11 water cleanup (VB + source cache)

	m_WaterVertexBuffer.Destroy();
	ClearID();
	SetUse(false);

	m_bWaterExist = false;
	m_bNeedUpdate = true;

	m_dwWaterPriCount = 0;
	m_byType = PATCH_TYPE_PLAIN;

	m_fMinX = m_fMaxX = m_fMinY = m_fMaxY = m_fMinZ = m_fMaxZ = 0.0f;

	m_dwVersion=0;
}

void CTerrainPatch::BuildWaterVertexBuffer(SWaterVertex* akSrcVertex, UINT uWaterVertexCount)
{
	// Keep DX11 source cache independent from DX9 device/runtime availability.
	m_kDX11Water.CacheSourceData(akSrcVertex, uWaterVertexCount);
	m_dwWaterPriCount = uWaterVertexCount / 3;

	CGraphicVertexBuffer& rkVB=m_WaterVertexBuffer;

	if (!rkVB.Create(uWaterVertexCount, FVF_XYZ | FVF_DIFFUSE, GRP_USAGE_DYNAMIC, GRP_POOL_DEFAULT)) 
		return;
	
	SWaterVertex* akDstWaterVertex;
	if (rkVB.Lock((void **) &akDstWaterVertex))
	{
		UINT uVBSize=sizeof(SWaterVertex)*uWaterVertexCount;
		memcpy(akDstWaterVertex, akSrcVertex, uVBSize);

		rkVB.Unlock();		
	}	
}
		
void CTerrainPatch::BuildTerrainVertexBuffer(HardwareTransformPatch_SSourceVertex* akSrcVertex)
{
	__BuildHardwareTerrainVertexBuffer(akSrcVertex);

	// Cache source data for deferred DX11 VB creation
	m_kDX11.CacheSourceData(akSrcVertex);
}

void CTerrainPatch::__BuildHardwareTerrainVertexBuffer(HardwareTransformPatch_SSourceVertex* akSrcVertex)
{

	CGraphicVertexBuffer& rkVB=m_kHT.m_kVB;
	if (!rkVB.Create(TERRAIN_VERTEX_COUNT, FVF_XYZ | FVF_NORMAL, GRP_USAGE_DYNAMIC, GRP_POOL_DEFAULT))
		return;

	HardwareTransformPatch_SSourceVertex* akDstVertex;
	if (rkVB.Lock((void **) &akDstVertex))
	{
		UINT uVBSize=sizeof(HardwareTransformPatch_SSourceVertex)*TERRAIN_VERTEX_COUNT;

		memcpy(akDstVertex, akSrcVertex, uVBSize);
		rkVB.Unlock();
	}
}

//////////////////////////////////////////////////////////////////////
// DX11 Terrain Vertex Buffer Build
//////////////////////////////////////////////////////////////////////

void CTerrainPatch::BuildDX11TerrainVertexBuffer(HardwareTransformPatch_SSourceVertex* akSrcVertex, ID3D11Device* pDevice)
{
	if (!pDevice || !akSrcVertex)
		return;

	__BuildDX11TerrainVertexBuffer(akSrcVertex, pDevice);
}

void CTerrainPatch::__BuildDX11TerrainVertexBuffer(HardwareTransformPatch_SSourceVertex* akSrcVertex, ID3D11Device* pDevice)
{
	m_kDX11.Create(akSrcVertex, pDevice);
}

//////////////////////////////////////////////////////////////////////
// DX11 Water Patch
//////////////////////////////////////////////////////////////////////

CTerrainPatch::SDX11WaterPatch::SDX11WaterPatch()
{
	__Initialize();
}

CTerrainPatch::SDX11WaterPatch::~SDX11WaterPatch()
{
	// M2-WATER-DRAW-PARITY-74: Free both VB and cache in destructor
	DestroyIncludingCache();
}

void CTerrainPatch::SDX11WaterPatch::Create(SWaterVertex* akSrcWaterVertex, UINT uVertexCount, ID3D11Device* pDevice)
{
	if (!pDevice || !akSrcWaterVertex || uVertexCount == 0)
		return;

	// Keep cached water source vertices alive for deferred/lazy rebuild.
	if (m_pWaterVertexBuffer)
	{
		m_pWaterVertexBuffer->Release();
		m_pWaterVertexBuffer = nullptr;
	}
	m_uWaterVertexCount = 0u;

	// Convert DX9 water vertex format to DX11 format
	DX11WaterVertex* akDX11WaterVertices = new DX11WaterVertex[uVertexCount];
	for (UINT uIndex = 0; uIndex < uVertexCount; ++uIndex)
	{
		akDX11WaterVertices[uIndex].kPosition.x = akSrcWaterVertex[uIndex].x;
		akDX11WaterVertices[uIndex].kPosition.y = akSrcWaterVertex[uIndex].y;
		akDX11WaterVertices[uIndex].kPosition.z = akSrcWaterVertex[uIndex].z;

		// Legacy water diffuse keeps per-vertex alpha for depth blending, while RGB may contain
		// non-neutral tint markers. For DX11 parity we use texture RGB and take only alpha from diffuse.
		DWORD dwColor = akSrcWaterVertex[uIndex].dwDiffuse;
		akDX11WaterVertices[uIndex].kColor.x = 1.0f;                               // R
		akDX11WaterVertices[uIndex].kColor.y = 1.0f;                               // G
		akDX11WaterVertices[uIndex].kColor.z = 1.0f;                               // B
		akDX11WaterVertices[uIndex].kColor.w = ((dwColor >> 24) & 0xFF) / 255.0f; // A
	}

	// Create DX11 vertex buffer
	D3D11_BUFFER_DESC kBufferDesc;
	ZeroMemory(&kBufferDesc, sizeof(kBufferDesc));
	kBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	kBufferDesc.ByteWidth = sizeof(DX11WaterVertex) * uVertexCount;
	kBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	kBufferDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA kInitData;
	ZeroMemory(&kInitData, sizeof(kInitData));
	kInitData.pSysMem = akDX11WaterVertices;

	HRESULT hResult = pDevice->CreateBuffer(&kBufferDesc, &kInitData, &m_pWaterVertexBuffer);
	if (SUCCEEDED(hResult))
	{
		m_uWaterVertexCount = uVertexCount;
	}
	else
	{
		m_pWaterVertexBuffer = nullptr;
		m_uWaterVertexCount = 0;
	}

	delete[] akDX11WaterVertices;
}

void CTerrainPatch::SDX11WaterPatch::CacheSourceData(SWaterVertex* akSrcWaterVertex, UINT uVertexCount)
{
	if (!akSrcWaterVertex || uVertexCount == 0)
		return;

	if (m_akCachedSourceWaterVertex)
	{
		delete[] m_akCachedSourceWaterVertex;
		m_akCachedSourceWaterVertex = nullptr;
		m_uCachedSourceWaterVertexCount = 0u;
	}

	m_akCachedSourceWaterVertex = new SWaterVertex[uVertexCount];
	memcpy(m_akCachedSourceWaterVertex, akSrcWaterVertex, sizeof(SWaterVertex) * uVertexCount);
	m_uCachedSourceWaterVertexCount = uVertexCount;
}

bool CTerrainPatch::SDX11WaterPatch::CreateFromCache(ID3D11Device* pDevice)
{
	if (!pDevice || !m_akCachedSourceWaterVertex || m_uCachedSourceWaterVertexCount == 0u)
		return false;

	Create(m_akCachedSourceWaterVertex, m_uCachedSourceWaterVertexCount, pDevice);
	return (m_pWaterVertexBuffer != nullptr && m_uWaterVertexCount > 0u);
}

void CTerrainPatch::SDX11WaterPatch::Destroy()
{
	// M2-WATER-DRAW-PARITY-74: Keep cached source vertices alive for deterministic rebuild
	// Only release the VB, not the source data. This allows rebuild after device reset/teleport.
	if (m_pWaterVertexBuffer)
	{
		m_pWaterVertexBuffer->Release();
		m_pWaterVertexBuffer = nullptr;
	}

	// M2-WATER-DRAW-PARITY-74: DO NOT free m_akCachedSourceWaterVertex here
	// The cache must survive device resets and map transitions for deterministic VB rebuild
	// Cache is only freed in destructor (see ~SDX11WaterPatch below)

	m_uWaterVertexCount = 0u;
}

// M2-WATER-DRAW-PARITY-74: Explicit cleanup for destructor (frees cache)
void CTerrainPatch::SDX11WaterPatch::DestroyIncludingCache()
{
	if (m_pWaterVertexBuffer)
	{
		m_pWaterVertexBuffer->Release();
		m_pWaterVertexBuffer = nullptr;
	}

	if (m_akCachedSourceWaterVertex)
	{
		delete[] m_akCachedSourceWaterVertex;
		m_akCachedSourceWaterVertex = nullptr;
		m_uCachedSourceWaterVertexCount = 0u;
	}

	m_uWaterVertexCount = 0u;
}

void CTerrainPatch::SDX11WaterPatch::__Initialize()
{
	m_pWaterVertexBuffer = nullptr;
	m_uWaterVertexCount = 0;
	m_akCachedSourceWaterVertex = nullptr;
	m_uCachedSourceWaterVertexCount = 0u;
}

//////////////////////////////////////////////////////////////////////
// DX11 Water Vertex Buffer Build
//////////////////////////////////////////////////////////////////////

void CTerrainPatch::BuildDX11WaterVertexBuffer(SWaterVertex* akSrcWaterVertex, UINT uVertexCount, ID3D11Device* pDevice)
{
	if (!pDevice || !akSrcWaterVertex || uVertexCount == 0)
		return;

	m_kDX11Water.Create(akSrcWaterVertex, uVertexCount, pDevice);
}

UINT CTerrainPatch::GetWaterFaceCount()
{
	return m_dwWaterPriCount;
}

CTerrainPatchProxy::CTerrainPatchProxy()
{
	Clear();
}

CTerrainPatchProxy::~CTerrainPatchProxy()
{
	Clear();
}

void CTerrainPatchProxy::SetCenterPosition(const D3DXVECTOR3& c_rv3Center)
{
	m_v3Center=c_rv3Center;
}

bool CTerrainPatchProxy::IsIn(const D3DXVECTOR3& c_rv3Target, float fRadius)
{
	float dx=m_v3Center.x-c_rv3Target.x;
	float dy=m_v3Center.y-c_rv3Target.y;
	float fDist=dx*dx+dy*dy;
	float fCheck=fRadius*fRadius;

	if (fDist<fCheck)
		return true;

	return false;
}

CGraphicVertexBuffer* CTerrainPatchProxy::HardwareTransformPatch_GetVertexBufferPtr()
{
	if (m_pTerrainPatch)
		return m_pTerrainPatch->HardwareTransformPatch_GetVertexBufferPtr();

	return NULL;
}

UINT CTerrainPatchProxy::GetWaterFaceCount()
{
	if (m_pTerrainPatch)
		return m_pTerrainPatch->GetWaterFaceCount();
	
	return 0;
}

void CTerrainPatchProxy::Clear()
{
	m_bUsed = false;
	m_sPatchNum = 0;
	m_byTerrainNum = 0xFF;

	m_pTerrainPatch = NULL;
}
