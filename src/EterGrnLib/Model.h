#pragma once

#include "Eterlib/GrpVertexBuffer.h"
#include "Eterlib/GrpIndexBuffer.h"

#include "Mesh.h"

// DX11 Migration: Vertex layout metadata to replace FVF flags
struct SVertexLayoutMetadata
{
	DWORD dwVertexStride;
	bool bHasPosition;
	bool bHasNormal;
	bool bHasTexCoord0;
	bool bHasTexCoord1;

	SVertexLayoutMetadata()
		: dwVertexStride(0)
		, bHasPosition(false)
		, bHasNormal(false)
		, bHasTexCoord0(false)
		, bHasTexCoord1(false)
	{}

	static SVertexLayoutMetadata CreateFromFVF(DWORD dwFvF);
};

class CGrannyModel : public CReferenceObject
{
	public:
		typedef struct SMeshNode
		{
			int					iMesh;
			const CGrannyMesh * pMesh;
			SMeshNode *			pNextMeshNode;
		} TMeshNode;

	public:
		CGrannyModel();
		virtual ~CGrannyModel();

		bool IsEmpty() const;
		bool CreateFromGrannyModelPointer(granny_model* pgrnModel);
		bool CreateDeviceObjects();
		void DestroyDeviceObjects();
		void Destroy();

		int GetRigidVertexCount() const;
		DWORD GetRigidVertexStride() const;
		int GetDeformVertexCount() const;
		int GetVertexCount() const;

		bool CanDeformPNTVertices() const;
		void DeformPNTVertices(void* dstBaseVertices, D3DXMATRIX* boneMatrices, const std::vector<granny_mesh_binding*>& c_rvct_pgrnMeshBinding) const;

		int GetIdxCount();
		int GetMeshCount() const;
		CGrannyMesh * GetMeshPointer(int iMesh);
		granny_model * GetGrannyModelPointer();
		const CGrannyMesh* GetMeshPointer(int iMesh) const;

	ID3D11Buffer* GetPNTD3DVertexBuffer() const;
	ID3D11Buffer* GetD3DIndexBuffer() const;

		const CGrannyModel::TMeshNode*  GetMeshNodeList(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType) const;

	// DX11 Model Sync M3-EGRN17.C: Lock/Unlock implementations in .cpp
	// (CPU-shadow-backed in strict mode, compatibility path in hybrid mode).
	bool LockVertices(void** indicies, void** vertices) const;
	void UnlockVertices() const;

	const CGrannyMaterialPalette& GetMaterialPalette() const;

	protected:
		bool LoadMeshs();		
		bool LoadPNTVertices();
		bool LoadIndices();
		void Initialize();

		BOOL CheckMeshIndex(int iIndex) const;
		void AppendMeshNode(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType, int iMesh);

	protected:
		// Granny Data
		granny_model *			m_pgrnModel;

		// Static Data
		CGrannyMesh *			m_meshs;

		CGraphicVertexBuffer	m_pntVtxBuf;	// for rigid mesh
		CGraphicIndexBuffer		m_idxBuf;

		TMeshNode *				m_meshNodes;
		TMeshNode *				m_meshNodeLists[CGrannyMesh::TYPE_MAX_NUM][CGrannyMaterial::TYPE_MAX_NUM];

		int						m_deformVtxCount;
		int						m_rigidVtxCount;
		int						m_vtxCount;
		int						m_idxCount;

		int						m_meshNodeSize;
		int						m_meshNodeCapacity;

		bool					m_canDeformPNVertices;
		
		CGrannyMaterialPalette	m_kMtrlPal;
	private:
		bool					m_bHaveBlendThing;
	public:
		bool					HaveBlendThing() { return m_bHaveBlendThing; }
	
	//////////////////////////////////////////////////////////////////////////
	// New members to support PNT2 type models
	protected:
		bool __LoadVertices();
	protected:
		SVertexLayoutMetadata m_kVertexLayout;
	// New members to support PNT2 type models
	//////////////////////////////////////////////////////////////////////////

	protected:
		// CGrannyModel instances are owned by CGraphicThing's array allocation.
		// They must not self-delete via CReferenceObject default OnSelfDestruct().
		void OnSelfDestruct() override;

};

