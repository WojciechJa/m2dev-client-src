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


#pragma once
#include "SpeedGrassRT.h"
#ifdef USE_SPEEDGRASS

// DX11: Forward declarations and includes
#include "GrassVertex.h"
#include <vector>

// forward reference
//class CScene;
class CMapOutdoor;


///////////////////////////////////////////////////////////////////////  
//	class CSpeedGrassWrapper declaration

class CSpeedGrassWrapper : public CSpeedGrassRT
{
public:
		CSpeedGrassWrapper( );
		virtual ~CSpeedGrassWrapper( );

		void							SetMapOutdoor(CMapOutdoor* pMapOutdoor)	{ m_pMapOutdoor = pMapOutdoor; }
		int								Draw(float fDensity);
		bool							InitFromBsfFile(const char* pFilename,
														unsigned int nNumBlades,
														unsigned int uiRows,
														unsigned int uiCols,
														float fCollisionDistance);

		// DX11: Generate grass geometry for DX11 rendering
		bool							GenerateGrassVertices(std::vector<GrassVertex>& outVertices,
																UINT& outRegionCount,
																UINT& outBladeCount,
																const DirectX::SimpleMath::Vector3& cameraPos,
																float lodNearDistance,
																float lodFarDistance,
																float& outLodBlendFactor) const;

private:
virtual float							Color(float fX, float fY, const float* pNormal, float* pTopColor, float* pBottomColor) const;
virtual	float							Height(float fX, float fY, float* pNormal) const;
		void							InitGraphics(void);

		CMapOutdoor *					m_pMapOutdoor;

		void*							m_lpD3DTexure8;		// DX11: Legacy DX9 texture (unused)

		CGraphicImageInstance			m_GrassImageInstance;
};

#endif // USE_SPEEDGRASS
