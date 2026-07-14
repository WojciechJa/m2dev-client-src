// Decal.h: interface for the CDecal class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_DECAL_H__E3D27DFC_30CB_4995_B9B9_396B5E8A5F02__INCLUDED_)
#define AFX_DECAL_H__E3D27DFC_30CB_4995_B9B9_396B5E8A5F02__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "GrpBase.h"

class CDecal  
{
public:

	enum
	{
		MAX_DECAL_VERTICES	= 256,
	};

	CDecal();
	virtual ~CDecal();

	void Clear();

	virtual void Make(TPosition v3Center, TPosition v3Normal, TPosition v3Tangent, float fWidth, float fHeight, float fDepth) = 0;
// 	virtual void Update();
	virtual void Render();

protected:
	//
	TPosition		m_v3Center;
	TPosition		m_v3Normal;
	
	// Clip Plane
	DirectX::SimpleMath::Vector4		m_v4LeftPlane;
	DirectX::SimpleMath::Vector4		m_v4RightPlane;
	DirectX::SimpleMath::Vector4		m_v4BottomPlane;
	DirectX::SimpleMath::Vector4		m_v4TopPlane;
	DirectX::SimpleMath::Vector4		m_v4FrontPlane;
	DirectX::SimpleMath::Vector4		m_v4BackPlane;

	// ??????
	DWORD			m_dwVertexCount;
	DWORD			m_dwPrimitiveCount;

	// ????????? ????????? ????????? ??????
//	CGraphicVertexBuffer	m_GraphicVertexBuffer;
//	CGraphicIndexBuffer		m_GraphicIndexBuffer;

	// ????????? ????????? ????????? ?????? ????????? ?????? ????????? DrawIndexedPrimitiveUP??? ?????????.
	typedef struct 
	{
		WORD			m_wMinIndex;
		DWORD			m_dwVertexCount;
		DWORD			m_dwPrimitiveCount;
		DWORD			m_dwVBOffset;
	} TTRIANGLEFANSTRUCT;
	
	std::vector<TTRIANGLEFANSTRUCT> m_TriangleFanStructVector;

	TPDTVertex		m_Vertices[MAX_DECAL_VERTICES];
	WORD			m_Indices[MAX_DECAL_VERTICES];
	
	const float m_cfDecalEpsilon;

protected:
	bool AddPolygon(DWORD dwAddCount, const TPosition *c_pv3Vertex, const TPosition *c_pv3Normal);
	void ClipMesh(DWORD dwPrimitiveCount, const TPosition *c_pv3Vertex, const TPosition *c_pv3Normal);
	DWORD ClipPolygon(DWORD dwVertexCount, 
		const TPosition *c_pv3Vertex, 
		const TPosition *c_pv3Normal, 
		TPosition *c_pv3NewVertex, 
		TPosition *c_pv3NewNormal) const;
	static DWORD ClipPolygonAgainstPlane(const DirectX::SimpleMath::Vector4& v4Plane, 
		DWORD dwVertexCount,
		const TPosition *c_pv3Vertex, 
		const TPosition *c_pv3Normal, 
		TPosition *c_pv3NewVertex, 
		TPosition *c_pv3NewNormal);
};
/*

class CDecalManager : public CSingleton<CDecalManager>
{
public:
	CDecalManager();
	~CDecalManager();

	void Add(CDecal * pDecal);
	void Remove(CDecal * pDecal);
	void Update();
	void Render();
	
private:
	std::vector<CDecal *>	m_DecalPtrVector;
	
};

*/
#endif // !defined(AFX_DECAL_H__E3D27DFC_30CB_4995_B9B9_396B5E8A5F02__INCLUDED_)
