///////////////////////////////////////////////////////////////////////  
//	CSpeedTreeForestOpenGL Class
//
//	(c) 2003 IDV, Inc.
//
//	This class is provided to illustrate one way to incorporate
//	SpeedTreeRT into an OpenGL application.  All of the SpeedTreeRT
//	calls that must be made on a per tree basis are done by this class.
//	Calls that apply to all trees (i.e. static SpeedTreeRT functions)
//	are made in the functions in main.cpp.
//
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


///////////////////////////////////////////////////////////////////////
//	Include Files

//#include <map>
#define SPEEDTREE_DATA_FORMAT_DIRECTX

#include <unordered_set>
#include "SpeedTreeForest.h"
#include "SpeedTreeMaterial.h"

// W3 forward declarations (DX11)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11SamplerState;
struct ID3D11BlendState;
struct ID3D11ShaderResourceView;
class CSpeedTreeWrapper;
class CSpeedGrassWrapper;
class CMapOutdoor;

///////////////////////////////////////////////////////////////////////
//	class CSpeedTreeForestDirectX declaration
class CSpeedTreeForestDirectX : public CSpeedTreeForest, public CGraphicBase, public CSingleton<CSpeedTreeForestDirectX>
{
	public:
		CSpeedTreeForestDirectX();
		virtual ~CSpeedTreeForestDirectX();

		void			UploadWindMatrix(unsigned int uiLocation, const float* pMatrix) const;
		void			UpdateCompundMatrix(const DirectX::SimpleMath::Vector3 & c_rEyeVec, const DirectX::SimpleMath::Matrix & c_rmatView, const DirectX::SimpleMath::Matrix& c_rmatProj);

		void			Render(unsigned long ulRenderBitVector = Forest_RenderAll);
		bool			SetRenderingDevice();
		bool			EnsureVertexShaders();
		DWORD			GetLastRenderedVisibleInstanceCount() const { return m_dwLastRenderedVisibleInstanceCount; }
		DWORD			GetLastRenderedVisibleTickMS() const { return m_dwLastRenderedVisibleTickMS; }
		void			ResetDX11SubmittedInstanceCount();
		void			AddDX11SubmittedInstanceCount(DWORD dwCount, UINT64 ullPrimitiveCount = 0u);
		DWORD			GetLastDX11SubmittedInstanceCount() const { return m_dwLastDX11SubmittedInstanceCount; }

		// W3 SpeedTree DX11 billboard rendering infrastructure
		bool			InitializeDX11SpeedTreeResources(ID3D11Device* pDevice);
		void			DestroyDX11SpeedTreeResources();
		bool			IsDX11SpeedTreeResourcesReady() const;
		bool			RenderToShadowMapDX11(ID3D11DeviceContext* pContext, const DirectX::SimpleMath::Matrix& c_rmatLightViewProj);

		// Iteration 1: DX11 Grass Rendering Infrastructure
		bool			InitializeDX11GrassResources(ID3D11Device* pDevice);
		void			DestroyDX11GrassResources();
		bool			IsDX11GrassResourcesReady() const;
		void			RenderGrassDX11(ID3D11DeviceContext* pContext);

		// Iteration 2: Grass Wrapper Management
		void			SetGrassWrapper(CSpeedGrassWrapper* pGrassWrapper);
		CSpeedGrassWrapper*	GetGrassWrapper() const;
		void			SetGrassMapOutdoor(CMapOutdoor* pMapOutdoor);

		// Iteration 5: Grass LOD Configuration
		void			SetGrassLodDistances(float fNearDistance, float fFarDistance);
		void			GetGrassLodDistances(float& fNearDistance, float& fFarDistance) const;
		void			GetGrassStatistics(UINT& uiTotalBlades, UINT& uiRenderedBlades, float& fLodBlend) const;
		void			SetGrassSize(float fSize);
		float			GetGrassSize() const;

	private:
		bool			InitVertexShaders();
		void			RenderBillboardsDX11(unsigned long ulRenderBitVector);  // W3: DX11 billboard rendering
		void			RenderBranchesDX11(unsigned long ulRenderBitVector);    // W3.2/W4.3: DX11 branch rendering (shadow + color pass)
		void			RenderFrondsDX11(unsigned long ulRenderBitVector);      // W3.2/W4.3: DX11 frond rendering (shadow + color pass)
		void			RenderLeavesDX11(unsigned long ulRenderBitVector);      // W3.3/W4.3: DX11 leaf rendering (shadow + color pass)
		bool			EmitDX11SubmitProbeDraw();
		void			TrackDX11SubmitFromVisiblePass(DWORD dwVisiblePassCount);
		ID3D11ShaderResourceView* __GetTreeTextureSRV(class CSpeedTreeWrapper* pTreeWrapper);         // Composite atlas (fronds/leaves/billboards)
		ID3D11ShaderResourceView* __GetTreeBranchTextureSRV(class CSpeedTreeWrapper* pTreeWrapper);   // Branch bark texture

		// Iteration 2: Grass Geometry Generation and Texture Loading
		bool			GenerateGrassGeometry();
		bool			LoadGrassTexture();
		void			UpdateGrassConstantBuffer(const DirectX::SimpleMath::Matrix& matViewProj, const DirectX::SimpleMath::Vector3& cameraPos);

	private:
		DWORD							m_dwLastRenderedVisibleInstanceCount;
		DWORD							m_dwLastRenderedVisibleTickMS;
		DWORD							m_dwLastDX11SubmittedInstanceCount;

		// W3 SpeedTree DX11 billboard rendering resources
		ID3D11VertexShader*				m_pDX11SpeedTreeBillboardVS;
		ID3D11PixelShader*				m_pDX11SpeedTreeBillboardPS;
		ID3D11InputLayout*				m_pDX11SpeedTreeBillboardInputLayout;
		ID3D11Buffer*					m_pDX11SpeedTreeConstantBuffer;
		ID3D11Buffer*					m_pDX11SpeedTreeAlphaRefBuffer;
		ID3D11Buffer*					m_pDX11SpeedTreeLeafPlacementBuffer;
		ID3D11SamplerState*				m_pDX11SpeedTreeSamplerState;
		ID3D11BlendState*				m_pDX11SpeedTreeBlendState;
		ID3D11BlendState*				m_pDX11SpeedTreeAlphaToCoverageBlendState;
		ID3D11Buffer*					m_pDX11SpeedTreeDynamicVB;
		ID3D11ShaderResourceView*		m_pDX11SpeedTreeDefaultTextureSRV;

		// W3.2/W3.3: Branch/Frond/Leaf shadow rendering resources
		ID3D11VertexShader*				m_pDX11SpeedTreeBranchVS;
		ID3D11PixelShader*				m_pDX11SpeedTreeBranchPS;
		ID3D11PixelShader*				m_pDX11SpeedTreeBranchOpaquePS;
		ID3D11PixelShader*				m_pDX11SpeedTreeShadowAlphaPS;
		ID3D11InputLayout*				m_pDX11SpeedTreeBranchInputLayout;
		ID3D11VertexShader*				m_pDX11SpeedTreeLeafVS;
		ID3D11InputLayout*				m_pDX11SpeedTreeLeafInputLayout;

		bool							m_bDX11SpeedTreeResourcesReady;
		bool							m_bDX11ShadowViewProjOverrideActive;
		DirectX::SimpleMath::Matrix						m_matDX11ShadowViewProjOverride;
		std::unordered_set<const CSpeedTreeWrapper*> m_setDX11ForceGeometryInstances;

		// Iteration 1: DX11 Grass Rendering Resources
		ID3D11VertexShader*				m_pDX11GrassVS;
		ID3D11PixelShader*				m_pDX11GrassPS;
		ID3D11InputLayout*				m_pDX11GrassInputLayout;
		ID3D11Buffer*					m_pDX11GrassConstantBuffer;
		ID3D11Buffer*					m_pDX11GrassVertexBuffer;
		ID3D11SamplerState*				m_pDX11GrassSamplerState;
		ID3D11BlendState*				m_pDX11GrassBlendState;
		ID3D11ShaderResourceView*		m_pDX11GrassTextureSRV;
		ID3D11Buffer*					m_pDX11GrassAlphaRefBuffer;  // Alpha reference constant buffer
		ID3D11PixelShader*				m_pDX11GrassShadowAlphaPS;   // Shadow casting pixel shader
		UINT							m_uiDX11GrassVertexCount;
		bool							m_bDX11GrassResourcesReady;

		// Iteration 2: Grass Wrapper Instance
		CSpeedGrassWrapper*				m_pGrassWrapper;
		CMapOutdoor*					m_pGrassMapOutdoor;
		UINT							m_uiDX11GrassRegionCount;
		UINT							m_uiDX11GrassBladeCount;

		// Iteration 2: Cached view/projection matrices and camera position for grass rendering
		DirectX::SimpleMath::Matrix		m_matCachedView;
		DirectX::SimpleMath::Matrix		m_matCachedProj;
		DirectX::SimpleMath::Vector3	m_vCachedCameraPos;
		bool							m_bMatricesCached;

		// Iteration 3: Wind animation data for grass
		DirectX::SimpleMath::Matrix		m_matGrassWindMatrix;
		float							m_fGrassWindTime;

		// Iteration 4: Grass LOD configuration
		float							m_fGrassLodNearDistance;      // LOD 0 distance
		float							m_fGrassLodFarDistance;       // LOD cull distance
		UINT							m_uiDX11GrassRenderedBlades;  // Actual blades rendered after LOD
		float							m_fGrassLodBlendFactor;       // Current LOD blend (0.0=near, 1.0=far)
		float							m_fGrassSize;                // Configurable grass blade size
};
