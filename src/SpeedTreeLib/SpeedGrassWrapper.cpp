///////////////////////////////////////////////////////////////////////  
//	CSpeedGrassWrapper Class
//
//	(c) 2003 IDV, Inc.
//
//	*** INTERACTIVE DATA VISUALIZATION (IDV) PROPRIETARY INFORMATION ***
//
//	This software is supplied under the terms of a license agreement or
//	nondisclosure agreement with Interactive Data Visualization and may
//	not be copied or disclosed except in accordance with the terms of
//	that agreement.
//
//      Copyright (c) 2001-2003 IDV, Inc.
//      All Rights Reserved.
//
//		IDV, Inc.
//		1233 Washington St. Suite 610
//		Columbia, SC 29201
//		Voice: (803) 799-1699
//		Fax:   (803) 931-0320
//		Web:   http://www.idvinc.com

#include "StdAfx.h"
#include "Constants.h"  // Must be included early for USE_SPEEDGRASS definition

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdlib.h>
#include <stdio.h>
#include <unordered_set>
#include <vector>

#include "SpeedGrassRT.h"       // Base class for stub implementations
#include "SpeedGrassWrapper.h"  // Include class definition with GrassVertex
#include "GrassVertex.h"         // Grass vertex structure

class CMapOutdoor
{
public:
	float GetHeight(float* pPos);
	bool GetBrushColor(float fX, float fY, float* pLowColor, float* pHighColor);
};

using namespace std;

#ifdef USE_SPEEDGRASS

// Helper function for random number generation (legacy grass support)
static inline float GetRandom(float fMin, float fMax)
{
	static bool bSeeded = false;
	if (!bSeeded)
	{
		srand(static_cast<unsigned int>(time(nullptr)));
		bSeeded = true;
	}
	float fRange = fMax - fMin;
	return fMin + (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * fRange;
}

static inline uint32_t HashGrassSample(int ix, int iy)
{
	uint32_t x = static_cast<uint32_t>(ix) * 0x9E3779B9u;
	uint32_t y = static_cast<uint32_t>(iy) * 0x85EBCA6Bu;
	uint32_t h = x ^ (y + 0xC2B2AE35u);
	h ^= (h >> 16);
	h *= 0x7FEB352Du;
	h ^= (h >> 15);
	h *= 0x846CA68Bu;
	h ^= (h >> 16);
	return h;
}

static inline float HashToUnitFloat(uint32_t h)
{
	return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

///////////////////////////////////////////////////////////////////////
//	Stubs for CSpeedGrassRT base class methods
// These are required for linking but not used in DX11 path

// Constructor stub
CSpeedGrassRT::CSpeedGrassRT() :
	m_nNumRegions(0),
	m_nNumRegionCols(0),
	m_nNumRegionRows(0),
	m_pRegions(nullptr),
	m_bAllRegionsCulled(false)
{
	m_afBoundingBox[0] = 0.0f;
	m_afBoundingBox[1] = 0.0f;
	m_afBoundingBox[2] = 0.0f;
	m_afBoundingBox[3] = 1.0f;
	m_afBoundingBox[4] = 1.0f;
	m_afBoundingBox[5] = 1.0f;
}

// Destructor stub
CSpeedGrassRT::~CSpeedGrassRT()
{
	delete[] m_pRegions;
	m_pRegions = nullptr;
	m_nNumRegions = 0;
	m_nNumRegionCols = 0;
	m_nNumRegionRows = 0;
	m_bAllRegionsCulled = true;
}

// ParseBsfFile stub - DX11 uses GenerateGrassVertices instead
bool CSpeedGrassRT::ParseBsfFile(const char* pFilename, unsigned int nNumBlades,
                                 unsigned int uiRows, unsigned int uiCols, float fCollisionDistance)
{
	// Stub - DX11 implementation doesn't use BSF file parsing
	// Grass data is loaded through the existing terrain system
	(void)pFilename;
	(void)nNumBlades;
	(void)fCollisionDistance;

	m_nNumRegionCols = static_cast<int>(uiCols);
	m_nNumRegionRows = static_cast<int>(uiRows);
	delete[] m_pRegions;
	m_pRegions = nullptr;
	m_nNumRegions = 0;
	m_bAllRegionsCulled = true;
	return false;
}

///////////////////////////////////////////////////////////////////////
//	CSpeedGrassWrapper::CSpeedGrassWrapper

CSpeedGrassWrapper::CSpeedGrassWrapper() : m_pMapOutdoor(NULL), m_lpD3DTexure8(NULL)//m_uiTexture(0)
{
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedGrassWrapper::~CSpeedGrassWrapper

CSpeedGrassWrapper::~CSpeedGrassWrapper( )
{
}


///////////////////////////////////////////////////////////////////////  
//	CSpeedGrassWrapper::Draw

int CSpeedGrassWrapper::Draw(float fDensity)
{
	int nTriangleCount = 0;

//	// determine which regions are visible
//	Cull( );
//
//	// setup opengl state
//	glPushAttrib(GL_ENABLE_BIT);
//	glDisable(GL_CULL_FACE);
//	glDisable(GL_BLEND);
//
//	glEnable(GL_TEXTURE_2D);
//	glBindTexture(GL_TEXTURE_2D, m_uiTexture);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
//
//	glEnable(GL_ALPHA_TEST);
//	glAlphaFunc(GL_GREATER, 0.4f);
//	glDisable(GL_LIGHTING);
//
//	unsigned int uiCount = 0;
//	unsigned int uiNumRegions = 0;
//	const SRegion* pRegions = GetRegions(uiNumRegions);
//
//	// setup for vertex buffer rendering (enable client buffers)
//	CIdvVertexBuffer::Enable(true);
//	if (uiNumRegions > 0)
//		pRegions[0].m_pVertexBuffer->EnableClientStates( );
//
//	// run through the regions and render those that aren't culled
//	for (unsigned int i = 0; i < uiNumRegions; ++i)
//	{
//		if (!pRegions[i].m_bCulled)
//		{
//			pRegions[i].m_pVertexBuffer->Bind( );
//			unsigned int uiNumBlades = int(fDensity * pRegions[i].m_vBlades.size( )); 
//			glDrawArrays(GL_QUADS, 0, uiNumBlades * 4);
//			nTriangleCount += uiNumBlades * 2;
//		}
//	}
//
//	// disable client buffers
//	if (uiNumRegions > 0)
//		pRegions[0].m_pVertexBuffer->DisableClientStates( );
//	CIdvVertexBuffer::Disable(true);
//
//	// restore opengl state
//	glPopAttrib( );

	return nTriangleCount;
}

///////////////////////////////////////////////////////////////////////  
//	CSpeedGrassWrapper::InitFromBsfFile

bool CSpeedGrassWrapper::InitFromBsfFile(const char* pFilename, 
										 unsigned int nNumBlades, 
										 unsigned int uiRows, 
										 unsigned int uiCols, 
										 float fCollisionDistance)
{
	bool bSuccess = false;

	if (pFilename)
	{
		// use SpeedGrass's built-in parse function
		if (ParseBsfFile(pFilename, nNumBlades, uiRows, uiCols, fCollisionDistance))
			bSuccess = true;
	}
	InitGraphics( );

	return bSuccess;
}


///////////////////////////////////////////////////////////////////////
//	CSpeedGrassWrapper::Color

float CSpeedGrassWrapper::Color(float fX, float fY, const float* pNormal, float* pTopColor, float* pBottomColor) const
{
	// Stub implementation for DX11 grass rendering
	// The DX11 path uses GenerateGrassVertices instead of this legacy method
	if (pTopColor)
	{
		pTopColor[0] = 0.5f;
		pTopColor[1] = 0.7f;
		pTopColor[2] = 0.3f;
	}
	if (pBottomColor)
	{
		pBottomColor[0] = 0.3f;
		pBottomColor[1] = 0.5f;
		pBottomColor[2] = 0.2f;
	}
	return 1.0f;
}


///////////////////////////////////////////////////////////////////////
//	CSpeedGrassWrapper::Height

float CSpeedGrassWrapper::Height(float fX, float fY, float* pNormal) const
{
	// Stub implementation for DX11 grass rendering
	// The DX11 path uses GenerateGrassVertices instead of this legacy method
	if (pNormal)
	{
		pNormal[0] = 0.0f;
		pNormal[1] = 0.0f;
		pNormal[2] = 1.0f;
	}
	return 0.0f;
}


///////////////////////////////////////////////////////////////////////
//	CSpeedGrassWrapper::InitGraphics

void CSpeedGrassWrapper::InitGraphics(void)
{
	// Stub implementation for DX11 grass rendering
	// The DX11 path uses GenerateGrassVertices and doesn't need legacy texture loading
	// Legacy initialization is handled by the DX11 renderer
}

///////////////////////////////////////////////////////////////////////
//	CSpeedGrassWrapper::GenerateGrassVertices
//	DX11: Generate grass geometry for DX11 rendering with LOD

bool CSpeedGrassWrapper::GenerateGrassVertices(std::vector<GrassVertex>& outVertices,
												UINT& outRegionCount,
												UINT& outBladeCount,
												const DirectX::SimpleMath::Vector3& cameraPos,
												float lodNearDistance,
												float lodFarDistance,
												float& outLodBlendFactor) const
{
	if (!m_pRegions)
	{
		if (!m_pMapOutdoor)
			return false;

		const float safeNearDistance = std::max(1.0f, lodNearDistance);
		const float safeFarDistance = std::max(safeNearDistance + 1.0f, lodFarDistance);
		const float lodRange = std::max(1.0f, safeFarDistance - safeNearDistance);
		const float sampleStep = std::max(120.0f, std::min(320.0f, safeFarDistance / 14.0f));
		const int sampleRadius = std::max(1, static_cast<int>(std::ceil(safeFarDistance / sampleStep)));
		const UINT maxBlades = 12000u;

		outVertices.clear();
		outVertices.reserve(static_cast<size_t>((sampleRadius * 2 + 1) * (sampleRadius * 2 + 1)) * 6u);
		outBladeCount = 0u;
		outRegionCount = 0u;
		outLodBlendFactor = 1.0f;

		float lodBlendAccum = 0.0f;
		UINT lodBlendSamples = 0u;
		std::unordered_set<uint32_t> regionKeys;
		regionKeys.reserve(256);

		for (int gy = -sampleRadius; gy <= sampleRadius; ++gy)
		{
			for (int gx = -sampleRadius; gx <= sampleRadius; ++gx)
			{
				const uint32_t h = HashGrassSample(gx, gy);
				const float jitterX = HashToUnitFloat(h) - 0.5f;
				const float jitterY = HashToUnitFloat(h ^ 0xA5A5A5A5u) - 0.5f;
				const float worldX = cameraPos.x + (static_cast<float>(gx) + jitterX * 0.6f) * sampleStep;
				const float worldY = cameraPos.y + (static_cast<float>(gy) + jitterY * 0.6f) * sampleStep;

				const float deltaX = worldX - cameraPos.x;
				const float deltaY = worldY - cameraPos.y;
				const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
				if (distance > safeFarDistance)
					continue;

				const float lodBlend = std::max(0.0f, std::min(1.0f, (distance - safeNearDistance) / lodRange));
				const uint32_t lodStride = (lodBlend < 0.33f) ? 1u : ((lodBlend < 0.66f) ? 2u : 4u);
				if ((h % lodStride) != 0u)
					continue;

				// Terrain-anchored placement for DX11 fallback path.
				float aPos[3] = { worldX, worldY, 0.0f };
				const float worldZ = m_pMapOutdoor->GetHeight(aPos);
				if (!std::isfinite(worldZ) || std::fabs(worldZ) > 1000000.0f)
					continue;

				// Use default grass colors (green gradient)
				float lowColor[4] = { 0.33f, 0.48f, 0.24f, 1.0f };
				float highColor[4] = { 0.54f, 0.70f, 0.35f, 1.0f };
				float brushLow[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				float brushHigh[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				if (m_pMapOutdoor->GetBrushColor(worldX, worldY, brushLow, brushHigh))
				{
					lowColor[0] = std::clamp(lowColor[0] * brushLow[0], 0.0f, 1.0f);
					lowColor[1] = std::clamp(lowColor[1] * brushLow[1], 0.0f, 1.0f);
					lowColor[2] = std::clamp(lowColor[2] * brushLow[2], 0.0f, 1.0f);
					highColor[0] = std::clamp(highColor[0] * brushHigh[0], 0.0f, 1.0f);
					highColor[1] = std::clamp(highColor[1] * brushHigh[1], 0.0f, 1.0f);
					highColor[2] = std::clamp(highColor[2] * brushHigh[2], 0.0f, 1.0f);
				}

				GrassVertex v0, v1, v2, v3;
				v0.position.x = worldX; v0.position.y = worldY; v0.position.z = worldZ;
				v1.position.x = worldX; v1.position.y = worldY; v1.position.z = worldZ;
				v2.position.x = worldX; v2.position.y = worldY; v2.position.z = worldZ;
				v3.position.x = worldX; v3.position.y = worldY; v3.position.z = worldZ;

				v0.uv.x = 1.0f; v0.uv.y = 0.0f;
				v1.uv.x = 0.0f; v1.uv.y = 0.0f;
				v2.uv.x = 0.0f; v2.uv.y = 1.0f;
				v3.uv.x = 1.0f; v3.uv.y = 1.0f;

				v0.color.x = lowColor[0]; v0.color.y = lowColor[1]; v0.color.z = lowColor[2]; v0.color.w = lowColor[3];
				v1.color.x = lowColor[0]; v1.color.y = lowColor[1]; v1.color.z = lowColor[2]; v1.color.w = lowColor[3];
				v2.color.x = highColor[0]; v2.color.y = highColor[1]; v2.color.z = highColor[2]; v2.color.w = highColor[3];
				v3.color.x = highColor[0]; v3.color.y = highColor[1]; v3.color.z = highColor[2]; v3.color.w = highColor[3];

				// Triangle list quad: (v0,v1,v2) + (v0,v2,v3)
				outVertices.push_back(v0);
				outVertices.push_back(v1);
				outVertices.push_back(v2);
				outVertices.push_back(v0);
				outVertices.push_back(v2);
				outVertices.push_back(v3);

				++outBladeCount;
				lodBlendAccum += lodBlend;
				++lodBlendSamples;
				regionKeys.insert(HashGrassSample(gx / 2, gy / 2));

				if (outBladeCount >= maxBlades)
					break;
			}

			if (outBladeCount >= maxBlades)
				break;
		}

		if (outBladeCount == 0u)
		{
			outVertices.clear();
			outRegionCount = 0u;
			outLodBlendFactor = 1.0f;
			return false;
		}

		outRegionCount = static_cast<UINT>(regionKeys.size());
		if (0u == outRegionCount)
			outRegionCount = 1u;

		outLodBlendFactor = (lodBlendSamples > 0u) ? (lodBlendAccum / static_cast<float>(lodBlendSamples)) : 0.0f;
		return true;
	}

	if (m_nNumRegions <= 0 || m_nNumRegions > 131072)
	{
		outVertices.clear();
		outRegionCount = 0u;
		outBladeCount = 0u;
		outLodBlendFactor = 1.0f;
		return false;
	}

	// Count total visible blades and calculate LOD factor
	outBladeCount = 0;
	outRegionCount = 0;
	float totalDistance = 0.0f;
	UINT lodRegionCount = 0;

	for (int i = 0; i < m_nNumRegions; ++i)
	{
		const SRegion& region = m_pRegions[i];
		if (!region.m_bCulled)
		{
			outBladeCount += static_cast<UINT>(region.m_vBlades.size());
			outRegionCount++;

			// Calculate distance from camera to region center
			DirectX::SimpleMath::Vector3 regionCenter(
				region.m_afCenter[0],
				region.m_afCenter[1],
				region.m_afCenter[2]
			);
			float distance = DirectX::SimpleMath::Vector3::Distance(regionCenter, cameraPos);
			totalDistance += distance;
			lodRegionCount++;
		}
	}

	if (outBladeCount == 0)
	{
		outLodBlendFactor = 1.0f;  // Far LOD (no grass)
		return false;
	}

	// Calculate average LOD blend factor
	float avgDistance = lodRegionCount > 0 ? totalDistance / lodRegionCount : 0.0f;
	float lodRange = lodFarDistance - lodNearDistance;
	outLodBlendFactor = (avgDistance - lodNearDistance) / lodRange;
	outLodBlendFactor = std::max(0.0f, std::min(1.0f, outLodBlendFactor));  // Clamp to [0,1]

	// Calculate blade skip factor based on LOD
	// LOD 0 (near): 100% blades (skipFactor = 1.0)
	// LOD 1 (medium): 50% blades (skipFactor = 2.0)
	// LOD 2 (far): 25% blades (skipFactor = 4.0)
	UINT skipFactor = 1;
	if (outLodBlendFactor < 0.33f)
	{
		skipFactor = 1;  // Near LOD: render all blades
	}
	else if (outLodBlendFactor < 0.66f)
	{
		skipFactor = 2;  // Medium LOD: render 50% of blades
	}
	else
	{
		skipFactor = 4;  // Far LOD: render 25% of blades
	}

	// Each blade becomes a quad (6 vertices in triangle-list topology)
	outVertices.clear();
	outVertices.reserve((outBladeCount / skipFactor) * 6);

	UINT bladeCounter = 0;

	// Iterate through all regions and blades
	for (int i = 0; i < m_nNumRegions; ++i)
	{
		const SRegion& region = m_pRegions[i];

		// Skip culled regions
		if (region.m_bCulled)
			continue;

		// Calculate distance-based LOD for this region
		DirectX::SimpleMath::Vector3 regionCenter(
			region.m_afCenter[0],
			region.m_afCenter[1],
			region.m_afCenter[2]
		);
		float regionDistance = DirectX::SimpleMath::Vector3::Distance(regionCenter, cameraPos);

		// Skip regions beyond far distance
		if (regionDistance > lodFarDistance)
			continue;

		// Calculate region-specific LOD blend factor
		float regionLodBlend = (regionDistance - lodNearDistance) / lodRange;
		regionLodBlend = std::max(0.0f, std::min(1.0f, regionLodBlend));

		// Determine skip factor for this region
		UINT regionSkipFactor = 1;
		if (regionLodBlend < 0.33f)
		{
			regionSkipFactor = 1;  // Near
		}
		else if (regionLodBlend < 0.66f)
		{
			regionSkipFactor = 2;  // Medium
		}
		else
		{
			regionSkipFactor = 4;  // Far
		}

		// Process each blade in the region with LOD-based skipping
		UINT bladeIndex = 0;
		for (const SBlade& blade : region.m_vBlades)
		{
			// Skip blades based on LOD factor (strided sampling for even distribution)
			if (bladeIndex % regionSkipFactor != 0)
			{
				bladeIndex++;
				continue;
			}
			bladeIndex++;

			GrassVertex v0, v1, v2, v3;

			// Position (same for all vertices - grass blade position)
			v0.position.x = blade.m_afPos[0];
			v0.position.y = blade.m_afPos[1];
			v0.position.z = blade.m_afPos[2];

			v1.position.x = blade.m_afPos[0];
			v1.position.y = blade.m_afPos[1];
			v1.position.z = blade.m_afPos[2];

			v2.position.x = blade.m_afPos[0];
			v2.position.y = blade.m_afPos[1];
			v2.position.z = blade.m_afPos[2];

			v3.position.x = blade.m_afPos[0];
			v3.position.y = blade.m_afPos[1];
			v3.position.z = blade.m_afPos[2];

			// UV coordinates (quad texture mapping)
			v0.uv.x = 1.0f;  v0.uv.y = 1.0f;  // Bottom-right
			v1.uv.x = 0.0f;  v1.uv.y = 1.0f;  // Bottom-left
			v2.uv.x = 0.0f;  v2.uv.y = 0.0f;  // Top-left
			v3.uv.x = 1.0f;  v3.uv.y = 0.0f;  // Top-right

			// Color - use bottom color for base, top color for tip
			// V0, V1: Bottom of blade (darker)
			v0.color.x = blade.m_afBottomColor[0];
			v0.color.y = blade.m_afBottomColor[1];
			v0.color.z = blade.m_afBottomColor[2];
			v0.color.w = 1.0f;

			v1.color.x = blade.m_afBottomColor[0];
			v1.color.y = blade.m_afBottomColor[1];
			v1.color.z = blade.m_afBottomColor[2];
			v1.color.w = 1.0f;

			// V2, V3: Top of blade (lighter)
			v2.color.x = blade.m_afTopColor[0];
			v2.color.y = blade.m_afTopColor[1];
			v2.color.z = blade.m_afTopColor[2];
			v2.color.w = 1.0f;

			v3.color.x = blade.m_afTopColor[0];
			v3.color.y = blade.m_afTopColor[1];
			v3.color.z = blade.m_afTopColor[2];
			v3.color.w = 1.0f;

			// Add vertices to form a quad (triangle list)
			outVertices.push_back(v0);
			outVertices.push_back(v1);
			outVertices.push_back(v2);
			outVertices.push_back(v0);
			outVertices.push_back(v2);
			outVertices.push_back(v3);
			bladeCounter++;
		}
	}

	// Update outBladeCount to reflect actual blades rendered after LOD
	outBladeCount = bladeCounter;

	return true;
}

#endif // USE_SPEEDGRASS

