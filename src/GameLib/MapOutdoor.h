#pragma once

#include <map> // B4.4: For DDS texture cache
#include <string>
#include <cstdint>

#include "EterLib/SkyBox.h"
#include "EterLib/LensFlare.h"
#include "EterLib/ScreenFilter.h"

#include "PRTerrainLib/TerrainType.h"
#include "PRTerrainLib/TextureSet.h"

#include "SpeedTreeLib/SpeedTreeForestDirectX.h"

#include "MapBase.h"
#include "Area.h"
#include "AreaTerrain.h"
#include "AreaLoaderThread.h"

#include "MonsterAreaInfo.h"


#define LOAD_SIZE_WIDTH				1

#define AROUND_AREA_NUM				1+(LOAD_SIZE_WIDTH*2)*(LOAD_SIZE_WIDTH*2)*2
#define MAX_PREPARE_SIZE			9
#define MAX_MAPSIZE					256		// 0 ~ 255, cellsize 200 = 64km

#define TERRAINPATCH_LODMAX		3

typedef struct SOutdoorMapCoordinate
{
	short m_sTerrainCoordX;		// Terrain ì¢Œí‘œ
	short m_sTerrainCoordY;
} TOutdoorMapCoordinate;

typedef std::map<const std::string, TOutdoorMapCoordinate> TOutdoorMapCoordinateMap;

class CTerrainPatchProxy;
class CTerrainQuadtreeNode;

// DX11 forward declarations
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11SamplerState;
struct ID3D11BlendState;
struct ID3D11DepthStencilView;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;

// DirectXTK forward declarations (Phase 2)
namespace DirectX
{
	inline namespace DX11
	{
		class CommonStates;
	}
}
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;

class CMapOutdoor : public CMapBase
{
	public:
		enum
		{
			VIEW_NONE = 0,
			VIEW_PART,
			VIEW_ALL,
		};

		enum EPart
		{
			PART_TERRAIN,
			PART_OBJECT,
			PART_CLOUD,
			PART_WATER,
			PART_TREE,
			PART_SKY,
			PART_NUM,
		};

	public:
		CMapOutdoor();
		virtual ~CMapOutdoor();

		virtual void	OnBeginEnvironment();

	protected:
		bool			Initialize();

		virtual bool	Destroy();
		virtual void	OnSetEnvironmentDataPtr();
		virtual void	OnResetEnvironmentDataPtr();

		virtual void	OnRender();

		virtual void	OnPreAssignTerrainPtr() {};

	public:
		void			SetInverseViewAndDynamicShaodwMatrices();
		virtual bool	Load(float x, float y, float z);
		virtual float	GetHeight(float x, float y);
		virtual float	GetCacheHeight(float x, float y);

		virtual bool	Update(float fX, float fY, float fZ);
		virtual void	UpdateAroundAmbience(float fX, float fY, float fZ);

	public:
		void			Clear();

		void			SetVisiblePart(int ePart, bool isVisible);
		void			SetSplatLimit(int iSplatNum);
		std::vector<int> &	GetRenderedSplatNum(int * piPatch, int * piSplat, float * pfSplatRatio);
		CArea::TCRCWithNumberVector & GetRenderedGraphicThingInstanceNum(DWORD * pdwGraphicThingInstanceNum, DWORD * pdwCRCNum);

		bool			LoadSetting(const char * c_szFileName);

		void			ApplyLight(DWORD dwVersion, const SLightDesc& c_rkLight);
		void			SetEnvironmentScreenFilter();
		void			ApplyEnvironmentDistanceOnly();
		void			SetEnvironmentSkyBox();
		void			SetEnvironmentLensFlare();

		// M3-SKY-BLEND-FIX-74: Apply fog parameters to GPU render state pipeline
		void			__ApplyFogToGPU();

		// M3-SKY-BLEND-FIX-74: Skybox render policy controls
		void			SetSkyRenderPolicyOverride(ESkyRenderPolicy ePolicy);
		ESkyRenderPolicy GetSkyRenderPolicyOverride() const;

		// DX11 weather/environment bridge state used by native world passes.
		struct SDX11EnvironmentBridgeState
		{
			bool bValid;
			bool bSnowEnabled;
			int iDayMode;
			int iWeatherMonth;
			float fRainIntensity;
			float fWindStrength;
			DWORD dwFogColor;
			float fFogNear;
			float fFogFar;
			D3DXVECTOR3 v3BackgroundLightDirection;
			D3DXCOLOR kBackgroundLightAmbient;
			D3DXCOLOR kBackgroundLightDiffuse;
			DWORD dwLastUpdateMS;
		};
		void			UpdateDX11EnvironmentBridgeState(bool bSnowEnabled, int iDayMode, int iWeatherMonth, float fRainIntensity);
		const SDX11EnvironmentBridgeState& GetDX11EnvironmentBridgeState() const { return m_kDX11EnvironmentBridgeState; }

		void			CreateCharacterShadowTexture();
		void			ReleaseCharacterShadowTexture();
		void			SetShadowTextureSize(WORD size);

		bool			BeginRenderCharacterShadowToTexture();
		void			EndRenderCharacterShadowToTexture();
		void			RenderWater();
		void			RenderMarkedArea();
		void			RecurseRenderAttr(CTerrainQuadtreeNode *Node, bool bCullEnable=TRUE);
		void			DrawPatchAttr(long patchnum);
		void			ClearGuildArea();
		void			RegisterGuildArea(int isx, int isy, int iex, int iey);

		void			VisibleMarkedArea();
		void			DisableMarkedArea();

		void			UpdateSky();
		void			RenderCollision();
		void			RenderSky();
		void			RenderCloud();
		void			RenderBeforeLensFlare();
		void			RenderAfterLensFlare();
		void			RenderScreenFiltering();

		void			SetWireframe(bool bWireFrame);
		bool			IsWireframe();

		bool			GetPickingPointWithRay(const CRay & rRay, D3DXVECTOR3 * v3IntersectPt);
		bool			GetPickingPointWithRayOnlyTerrain(const CRay & rRay, D3DXVECTOR3 * v3IntersectPt);
		bool			GetPickingPoint(D3DXVECTOR3 * v3IntersectPt);
		void			GetTerrainCount(short * psTerrainCountX, short * psTerrainCountY)
		{
			*psTerrainCountX = m_sTerrainCountX;
			*psTerrainCountY = m_sTerrainCountY;
		}

		bool			SetTerrainCount(short sTerrainCountX, short sTerrainCountY);

		// Shadow
		void			SetDrawShadow(bool bDrawShadow);
		void			SetDrawCharacterShadow(bool bDrawChrShadow);

		DWORD			GetShadowMapColor(float fx, float fy);

	protected:
		bool			__PickTerrainHeight(float& fPos, const D3DXVECTOR3& v3Start, const D3DXVECTOR3& v3End, float fStep, float fRayRange, float fLimitRange, D3DXVECTOR3* pv3Pick);

		virtual void	__ClearGarvage();
		virtual void	__UpdateGarvage();

		virtual bool	LoadTerrain(WORD wTerrainCoordX, WORD wTerrainCoordY, WORD wCellCoordX, WORD wCellCoordY);
		virtual bool	LoadArea(WORD wAreaCoordX, WORD wAreaCoordY, WORD wCellCoordX, WORD wCellCoordY);
		virtual void	UpdateAreaList(long lCenterX, long lCenterY);
		bool			isTerrainLoaded(WORD wX, WORD wY);
		bool			isAreaLoaded(WORD wX, WORD wY);

		void			AssignTerrainPtr();				// í˜„ìž¬ ì¢Œí‘œì—ì„œ ì£¼ìœ„(ex. 3x3)ì— ìžˆëŠ” ê²ƒë“¤ì˜ í¬ì¸í„°ë¥¼ ì—°ê²°í•œë‹¤. (ì—…ë°ì´íŠ¸ ì‹œ ë¶ˆë ¤ì§)

		//////////////////////////////////////////////////////////////////////////
		// New
		//////////////////////////////////////////////////////////////////////////
		// ì—¬ëŸ¬ê°€ì§€ ë§µë“¤ì„ ì–»ëŠ”ë‹¤.
		void			GetHeightMap(const BYTE & c_rucTerrainNum, WORD ** pwHeightMap);
		void			GetNormalMap(const BYTE & c_rucTerrainNum, char ** pucNormalMap);

		// Water
		void			GetWaterMap(const BYTE & c_rucTerrainNum, BYTE ** pucWaterMap);
		void			GetWaterHeight(BYTE byTerrainNum, BYTE byWaterNum, long * plWaterHeight);


	//////////////////////////////////////////////////////////////////////////
	// Terrain
	//////////////////////////////////////////////////////////////////////////
	protected:
		// ë°ì´í„°
		CTerrain *					m_pTerrain[AROUND_AREA_NUM];	// Terrain
		CTerrainPatchProxy *		m_pTerrainPatchProxyList;			// CTerrainì„ ëžœë”ë§ í• ë•Œ ì‹¤ì œë¡œ ëžœë”ë§í•˜ëŠ” í´ë¦¬ê³¤ íŒ¨ì¹˜ë“¤... Seamless Map ì„ ìœ„í•´ CTerrainìœ¼ë¡œë¶€í„° ë…ë¦½...

		long						m_lViewRadius;				// ì‹œì•¼ ê±°ë¦¬.. ì…€ë‹¨ìœ„ìž„..
		float						m_fHeightScale;				// ë†’ì´ ìŠ¤ì¼€ì¼... 1.0ì¼ë•Œ 0~655.35ë¯¸í„°ê¹Œì§€ í‘œí˜„ ê°€ëŠ¥.

		short						m_sTerrainCountX, m_sTerrainCountY;		// seamless map ì•ˆì— ë“¤ì–´ê°€ëŠ” Terrainê°œìˆ˜

		TOutdoorMapCoordinate		m_CurCoordinate;		// í˜„ìž¬ì˜ ì¢Œí‘œ

		long						m_lCurCoordStartX, m_lCurCoordStartY;
		TOutdoorMapCoordinate		m_PrevCoordinate;		// í˜„ìž¬ì˜ ì¢Œí‘œ
		TOutdoorMapCoordinateMap	m_EntryPointMap;

		WORD						m_wPatchCount;

		//////////////////////////////////////////////////////////////////////////
		// Index Buffer
		WORD *						m_pwaIndices[TERRAINPATCH_LODMAX];

		CGraphicIndexBuffer			m_IndexBuffer[TERRAINPATCH_LODMAX];
		WORD						m_wNumIndices[TERRAINPATCH_LODMAX];
		
		virtual void	DestroyTerrain();

		void			CreateTerrainPatchProxyList();
		void			DestroyTerrainPatchProxyList();

		void			UpdateTerrain(float fX, float fY);

		void			ConvertTerrainToTnL(long lx, long ly);

		void			AssignPatch(long lPatchNum, long lx0, long ly0, long lx1, long ly1);

		//////////////////////////////////////////////////////////////////////////
		// Index Buffer
		void			ADDLvl1TL(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl1T(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount,  const BYTE & c_rucNumLineWarp);
		void			ADDLvl1TR(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl1L(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl1R(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl1BL(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl1B(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl1BR(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl1M(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl2TL(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl2T(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl2TR(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl2L(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl2R(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl2BL(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl2B(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl2BR(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);
		void			ADDLvl2M(WORD * pIndices, WORD & rwCount, const WORD & c_rwCurCount, const BYTE & c_rucNumLineWarp);

	public:
		BOOL			GetTerrainPointer(BYTE c_ucTerrainNum, CTerrain ** ppTerrain);
		float			GetTerrainHeight(float fx, float fy);
		bool			GetWaterHeight(int iX, int iY, long * plWaterHeight);
		bool			GetNormal(int ix, int iy, D3DXVECTOR3 * pv3Normal);

		void			RenderTerrain();

		const long		GetViewRadius()			{ return m_lViewRadius;		}
		const float		GetHeightScale()		{ return m_fHeightScale;	}

		const TOutdoorMapCoordinate & GetEntryPoint(const std::string & c_rstrEntryPointName) const;
		void SetEntryPoint(const std::string & c_rstrEntryPointName, const TOutdoorMapCoordinate & c_rOutdoorMapCoordinate);
		const TOutdoorMapCoordinate & GetCurCoordinate() { return m_CurCoordinate; }
		const TOutdoorMapCoordinate & GetPrevCoordinate() { return m_PrevCoordinate; }

	//////////////////////////////////////////////////////////////////////////
	// Area
	//////////////////////////////////////////////////////////////////////////
	protected:
		CArea *			m_pArea[AROUND_AREA_NUM];		// Data

		virtual void	DestroyArea();

		void			__UpdateArea(D3DXVECTOR3& v3Player);
		void			__Game_UpdateArea(D3DXVECTOR3& v3Player);

		void			__BuildDynamicSphereInstanceVector();

		void			__CollectShadowReceiver(D3DXVECTOR3& v3Target, D3DXVECTOR3& v3Light);
		void			__CollectCollisionPCBlocker(D3DXVECTOR3& v3Eye, D3DXVECTOR3& v3Target, float fDistance);
		void			__CollectCollisionShadowReceiver(D3DXVECTOR3& v3Target, D3DXVECTOR3& v3Light);
		void			__UpdateAroundAreaList();
		bool			__IsInShadowReceiverList(CGraphicObjectInstance* pkObjInstTest);
		bool			__IsInPCBlockerList(CGraphicObjectInstance* pkObjInstTest);

		void			ConvertToMapCoords(float fx, float fy, int *iCellX, int *iCellY, BYTE * pucSubCellX, BYTE * pucSubCellY, WORD * pwTerrainNumX, WORD * pwTerrainNumY);

	public:
		BOOL			GetAreaPointer(const BYTE c_ucAreaNum, CArea ** ppArea);
		void			RenderArea(bool bRenderAmbience = true);
		void			RenderBlendArea();
		void			RenderDungeon();
		void			RenderEffect();
		void			RenderPCBlocker();
		void			RenderTree();

	public:
		//////////////////////////////////////////////////////////////////////////
		// For Grass
		//////////////////////////////////////////////////////////////////////////
		float		GetHeight(float* pPos);
		bool		GetBrushColor(float fX, float fY, float* pLowColor, float* pHighColor);
		bool		isAttrOn(float fX, float fY, BYTE byAttr);
		bool		GetAttr(float fX, float fY, BYTE * pbyAttr);
		bool		isAttrOn(int iX, int iY, BYTE byAttr);
		bool		GetAttr(int iX, int iY, BYTE * pbyAttr);

		void		SetMaterialDiffuse(float fr, float fg, float fb);
		void		SetMaterialAmbient(float fr, float fg, float fb);
		void		SetTerrainMaterial(const PR_MATERIAL * pMaterial);

		bool		GetTerrainNum(float fx, float fy, BYTE * pbyTerrainNum);
		bool		GetTerrainNumFromCoord(WORD wCoordX, WORD wCoordY, BYTE * pbyTerrainNum);

	protected:
		//////////////////////////////////////////////////////////////////////////
		// New
		//////////////////////////////////////////////////////////////////////////
		long					m_lCenterX, m_lCenterY;		// Terrain ì¢Œí‘œ ë‚´ì˜ ì…€ ì¢Œí‘œ...
		long					m_lOldReadX, m_lOldReadY;	/* Last center */

		//////////////////////////////////////////////////////////////////////////
		// Octree
		//////////////////////////////////////////////////////////////////////////
		CTerrainQuadtreeNode * 	m_pRootNode;

		void					BuildQuadTree();
		CTerrainQuadtreeNode *	AllocQuadTreeNode(long x0, long y0, long x1, long y1);
		void					SubDivideNode(CTerrainQuadtreeNode * Node);
		void					UpdateQuadTreeHeights(CTerrainQuadtreeNode *Node);


		void					FreeQuadTree();

		struct TPatchDrawStruct
		{
			float fDistance;
			BYTE byTerrainNum;
			long lPatchNum;
			CTerrainPatchProxy * pTerrainPatchProxy;

			bool operator<( const TPatchDrawStruct & rhs) const
			{
				return fDistance < rhs.fDistance;
			}
		};

	protected:

		std::vector<std::pair<float, long> > m_PatchVector;
		// DX11 terrain stabilization: short-lived fallback cache for transient empty-cull frames.
		std::vector<std::pair<float, long> > m_DX11LastNonEmptyPatchVector;
		DWORD m_dwDX11LastNonEmptyPatchVectorMS;
		long m_lDX11LastNonEmptyPatchTotalCount;
		std::vector<TPatchDrawStruct> m_PatchDrawStructVector;

		void					SetPatchDrawVector();

		void					NEW_DrawWireFrame(CTerrainPatchProxy * pTerrainPatchProxy, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType);

		void					DrawWireFrame(long patchnum, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType);
		void					DrawWater(long patchnum);

		bool					m_bDrawWireFrame;
		bool					m_bDrawShadow;
		bool					m_bDrawChrShadow;

		//////////////////////////////////////////////////////////////////////////
		// Water
		D3DXMATRIX				m_matBump;
		void					LoadWaterTexture();
		void					UnloadWaterTexture();
		//Water
		//////////////////////////////////////////////////////////////////////////

		//////////////////////////////////////////////////////////////////////////
		// Character Shadow (DX11-native)
		WORD					m_wShadowMapSize;
		//////////////////////////////////////////////////////////////////////////

		// View Frustum Culling
		D3DXPLANE					m_plane[6];
		bool						m_bDX11TerrainUsePositiveYForFrustum;

		void BuildViewFrustum(D3DXMATRIX & mat);

		CTextureSet					m_TextureSet;

	protected:
		CSkyBox						m_SkyBox;
		CLensFlare					m_LensFlare;
		CScreenFilter				m_ScreenFilter;

		// M3-SKY-BLEND-FIX-74: Skybox render policy override
		ESkyRenderPolicy			m_eSkyRenderPolicyOverride;

	protected:
		void SetIndexBuffer();
		void SelectIndexBuffer(BYTE byLODLevel, WORD * pwPrimitiveCount, GrpPrimitiveType * pePrimitiveType);

		D3DXMATRIX m_matWorldForCommonUse;
		D3DXMATRIX m_matViewInverse;

		D3DXMATRIX m_matSplatAlpha;
		D3DXMATRIX m_matStaticShadow;
		D3DXMATRIX m_matDynamicShadow;
		D3DXMATRIX m_matDynamicShadowScale;
		D3DXMATRIX m_matLightView;

		float m_fTerrainTexCoordBase;
		float m_fWaterTexCoordBase;

		float m_fXforDistanceCaculation, m_fYforDistanceCaculation;

	protected:
		typedef std::vector<CTerrain *>		TTerrainPtrVector;
		typedef TTerrainPtrVector::iterator TTerrainPtrVectorIterator;
		typedef std::vector<CArea *>		TAreaPtrVector;
		typedef TAreaPtrVector::iterator	TAreaPtrVectorIterator;

		TTerrainPtrVector			m_TerrainVector;
		TTerrainPtrVector			m_TerrainDeleteVector;
		TTerrainPtrVector			m_TerrainLoadRequestVector;
		TTerrainPtrVector			m_TerrainLoadWaitVector;
		TTerrainPtrVectorIterator	m_TerrainPtrVectorIterator;

		TAreaPtrVector				m_AreaVector;
		TAreaPtrVector				m_AreaDeleteVector;
		TAreaPtrVector				m_AreaLoadRequestVector;
		TAreaPtrVector				m_AreaLoadWaitVector;
		TAreaPtrVectorIterator		m_AreaPtrVectorIterator;

		struct FPushToDeleteVector
		{
			enum EDeleteDir
			{
				DELETE_LEFT,
				DELETE_RIGHT,
				DELETE_TOP,
				DELETE_BOTTOM,
			};

			EDeleteDir m_eLRDeleteDir;
			EDeleteDir m_eTBDeleteDir;
			TOutdoorMapCoordinate m_CurCoordinate;

			FPushToDeleteVector(EDeleteDir eLRDeleteDir, EDeleteDir eTBDeleteDir, TOutdoorMapCoordinate CurCoord)
			{
				m_eLRDeleteDir = eLRDeleteDir;
				m_eTBDeleteDir = eTBDeleteDir;
				m_CurCoordinate = CurCoord;
			}
		};

		struct FPushTerrainToDeleteVector : public FPushToDeleteVector
		{
			TTerrainPtrVector	m_ReturnTerrainVector;

			FPushTerrainToDeleteVector(EDeleteDir eLRDeleteDir, EDeleteDir eTBDeleteDir, TOutdoorMapCoordinate CurCoord)
				: FPushToDeleteVector(eLRDeleteDir, eTBDeleteDir, CurCoord)
			{
				m_ReturnTerrainVector.clear();
			}

			void operator() (CTerrain * pTerrain);
		};

		struct FPushAreaToDeleteVector : public FPushToDeleteVector
		{
			TAreaPtrVector		m_ReturnAreaVector;

			FPushAreaToDeleteVector(EDeleteDir eLRDeleteDir, EDeleteDir eTBDeleteDir, TOutdoorMapCoordinate CurCoord)
				: FPushToDeleteVector(eLRDeleteDir, eTBDeleteDir, CurCoord)
			{
				m_ReturnAreaVector.clear();
			}

			void operator() (CArea * pArea);
		};

	protected:
		void InitializeVisibleParts();
		bool IsVisiblePart(int ePart);

		float __GetNoFogDistance();
		float __GetFogDistance();


	protected:
		DWORD m_dwVisiblePartFlags;

		int m_iRenderedSplatNumSqSum;
		int m_iRenderedSplatNum;
		int m_iRenderedPatchNum;
		std::vector<int> m_RenderedTextureNumVector;
		int m_iSplatLimit;

	protected:
		int m_iPatchTerrainVertexCount;
		int m_iPatchWaterVertexCount;

		int m_iPatchTerrainVertexSize;
		int m_iPatchWaterVertexSize;

		DWORD m_dwRenderedCRCNum;
		DWORD m_dwRenderedGraphicThingInstanceNum;

		std::list<RECT> m_rkList_kGuildArea;

	protected:
		void __RenderTerrain_RecurseRenderQuadTree(CTerrainQuadtreeNode *Node, bool bCullCheckNeed = true);
		int	 __RenderTerrain_RecurseRenderQuadTree_CheckBoundingCircle(const DirectX::SimpleMath::Vector3 & c_v3Center, const float & c_fRadius);

		void __RenderTerrain_AppendPatch(const DirectX::SimpleMath::Vector3& c_rv3Center, float fDistance, long lPatchNum);

		void __RenderTerrain_RenderSoftwareTransformPatch();
		void __RenderTerrain_RenderHardwareTransformPatch();

	protected:
		void __HardwareTransformPatch_RenderPatchSplat(long patchnum, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType);
		void __HardwareTransformPatch_RenderPatchNone(long patchnum, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType);


	protected:
		struct SoftwareTransformPatch_SData
		{
			enum
			{
				SPLAT_VB_NUM = 8,
				NONE_VB_NUM = 8,
			};

			// M3-GAMELIB-TERRAIN-HEADER-68: m_pkVBSplat/m_pkVBNone removed (STP compatibility path removed)
			DWORD m_dwSplatPos;
			DWORD m_dwNonePos;
			DWORD m_dwLightVersion;
		} m_kSTPD;

		struct SoftwareTransformPatch_SRenderState {
			D3DXMATRIX m_m4Proj;
			D3DXMATRIX m_m4Frustum;
			D3DXMATRIX m_m4DynamicShadow;
		SLightDesc  m_kLight;
			GrpMaterial m_kMtrl;
			D3DXVECTOR3 m_v3Player;
			DWORD m_dwFogColor;
			float m_fScreenHalfWidth;
			float m_fScreenHalfHeight;

			float m_fFogNearDistance;
			float m_fFogFarDistance;
			float m_fFogNearTransZ;
			float m_fFogFarTransZ;
			float m_fFogLenInv;
		};

		struct SoftwareTransformPatch_STVertex
		{
			D3DXVECTOR4 kPosition;
		};

		struct SoftwareTransformPatch_STLVertex
		{
			D3DXVECTOR4 kPosition;
			DWORD dwDiffuse;
			DWORD dwFog;
			D3DXVECTOR2 kTexTile;
			D3DXVECTOR2 kTexAlpha;
			D3DXVECTOR2 kTexStaticShadow;
			D3DXVECTOR2 kTexDynamicShadow;
		};


		void __SoftwareTransformPatch_ApplyRenderState();
		void __SoftwareTransformPatch_RestoreRenderState(DWORD dwFogEnable);

		void __SoftwareTransformPatch_Initialize();
		bool __SoftwareTransformPatch_Create();
		void __SoftwareTransformPatch_Destroy();
		void __SoftwareTransformPatch_BuildPipeline(SoftwareTransformPatch_SRenderState& rkTPRS);
		void __SoftwareTransformPatch_BuildPipeline_BuildFogFuncTable(SoftwareTransformPatch_SRenderState& rkTPRS);
		bool __SoftwareTransformPatch_SetTransform(SoftwareTransformPatch_SRenderState& rkTPRS, SoftwareTransformPatch_STLVertex* akTransVertex, CTerrainPatchProxy& rkTerrainPatchProxy, UINT uTerrainX, UINT uTerrainY, bool isFogEnable, bool isDynamicShadow);

		bool __SoftwareTransformPatch_SetSplatStream(SoftwareTransformPatch_STLVertex* akTransVertex);
		bool __SoftwareTransformPatch_SetShadowStream(SoftwareTransformPatch_STLVertex* akTransVertex);

		void __SoftwareTransformPatch_ApplyStaticShadowRenderState();
		void __SoftwareTransformPatch_RestoreStaticShadowRenderState();

		void __SoftwareTransformPatch_ApplyFogShadowRenderState();
		void __SoftwareTransformPatch_RestoreFogShadowRenderState();
		void __SoftwareTransformPatch_ApplyDynamicShadowRenderState();
		void __SoftwareTransformPatch_RestoreDynamicShadowRenderState();
		void __SoftwareTransformPatch_RenderPatchSplat(SoftwareTransformPatch_SRenderState& rkTPRS, long patchnum, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType, bool isFogEnable);
		void __SoftwareTransformPatch_RenderPatchNone(SoftwareTransformPatch_SRenderState& rkTPRS, long patchnum, WORD wPrimitiveCount, GrpPrimitiveType ePrimitiveType);


	protected:
		std::vector<CGraphicObjectInstance *> m_ShadowReceiverVector;
		std::vector<CGraphicObjectInstance *> m_PCBlockerVector;

	protected:
		float	m_fOpaqueWaterDepth;
		CGraphicImageInstance m_WaterInstances[30];

	public:
		float	GetOpaqueWaterDepth() { return m_fOpaqueWaterDepth;	}
		void	SetOpaqueWaterDepth(float fOpaqueWaterDepth) { m_fOpaqueWaterDepth = fOpaqueWaterDepth; }

	protected:
		CGraphicImageInstance	m_attrImageInstance;
		CGraphicImageInstance	m_BuildingTransparentImageInstance;
		D3DXMATRIX				m_matBuildingTransparent;

	protected:
		CDynamicPool<CMonsterAreaInfo>		m_kPool_kMonsterAreaInfo;
		TMonsterAreaInfoPtrVector			m_MonsterAreaInfoPtrVector;
		TMonsterAreaInfoPtrVectorIterator	m_MonsterAreaInfoPtrVectorIterator;

	public:
		bool LoadMonsterAreaInfo();

		CMonsterAreaInfo * AddMonsterAreaInfo(long lOriginX, long lOriginY, long lSizeX, long lSizeY);
		void RemoveAllMonsterAreaInfo();

		DWORD GetMonsterAreaInfoCount() { return m_MonsterAreaInfoPtrVector.size();	}
		bool GetMonsterAreaInfoFromVectorIndex(DWORD dwMonsterAreaInfoVectorIndex, CMonsterAreaInfo ** ppMonsterAreaInfo);

		CMonsterAreaInfo * AddNewMonsterAreaInfo(long lOriginX, long lOriginY, long lSizeX, long lSizeY,
			CMonsterAreaInfo::EMonsterAreaInfoType eMonsterAreaInfoType,
			DWORD dwVID, DWORD dwCount, CMonsterAreaInfo::EMonsterDir eMonsterDir);

	public:
		void GetBaseXY(DWORD * pdwBaseX, DWORD * pdwBaseY);
		void SetBaseXY(DWORD dwBaseX, DWORD dwBaseY);

		void SetTransparentTree(bool bTransparentTree) { m_bTransparentTree = bTransparentTree;}
		void EnableTerrainOnlyForHeight(bool bFlag) { m_bEnableTerrainOnlyForHeight = bFlag; }
		void EnablePortal(bool bFlag);
		bool IsEnablePortal() { return m_bEnablePortal; }

	protected:
		DWORD			m_dwBaseX;
		DWORD			m_dwBaseY;

		D3DXVECTOR3		m_v3Player;
		
		bool			m_bShowEntirePatchTextureCount;
		bool			m_bTransparentTree;
		bool			m_bEnableTerrainOnlyForHeight;
		bool			m_bEnablePortal;

	// XMas
	private:
		struct SXMasTree
		{
			CSpeedTreeForest::SpeedTreeWrapperPtr m_pkTree;
			int m_iEffectID;
		} m_kXMas;

		void __XMasTree_Initialize();
		void __XMasTree_Create(float x, float y, float z, const char* c_szTreeName, const char* c_szEffName);

	public:
		void XMasTree_Destroy();
		void XMasTree_Set(float x, float y, float z, const char* c_szTreeName, const char* c_szEffName);

	// Special Effect
	private:
		typedef std::map<DWORD, int> TSpecialEffectMap;
		TSpecialEffectMap m_kMap_dwID_iEffectID;

	public:
		void SpecialEffect_Create(DWORD dwID, float x, float y, float z, const char* c_szEffName);
		void SpecialEffect_Delete(DWORD dwID);
		void SpecialEffect_Destroy();

	private:
		struct SHeightCache 
		{
			struct SItem
			{
				DWORD	m_dwKey;
				float	m_fHeight;
			};

			enum
			{
				HASH_SIZE = 100,
			};

			std::vector<SItem> m_akVct_kItem[HASH_SIZE];

			bool m_isUpdated;
		} m_kHeightCache;
		
		void __HeightCache_Init();
		void __HeightCache_Update();

	public:
		void SetEnvironmentDataName(const std::string& strEnvironmentDataName);
		std::string& GetEnvironmentDataName();

	// ========== DX11 Terrain Rendering API ==========
	public:
		// Initialize DX11 terrain resources (shaders, pipeline states)
		bool InitializeDX11TerrainResources(ID3D11Device* pDevice);
		void DestroyDX11TerrainResources();

		// Build DX11 vertex buffers for all terrain patches
		bool BuildDX11TerrainVertexBuffers(ID3D11Device* pDevice);

		// Main DX11 terrain rendering function
		void RenderTerrainDX11(
			ID3D11Device* pDevice,
			ID3D11DeviceContext* pContext,
			uint32_t* pdwOutObservedMask = nullptr,
			uint32_t* pdwOutSubmittedMask = nullptr,
			uint32_t* pdwOutApplicableMask = nullptr);

		// B9-B11 feature probes used by runtime world-port mask policy.
		// These report whether object/effect/speedtree subsystems are populated and drawable for the current scene.
		bool ProbeDX11ObjectsReady();
		bool ProbeDX11EffectsReady();
		bool ProbeDX11SpeedTreeReady();

		// DX11 water rendering
		bool InitializeDX11WaterResources(ID3D11Device* pDevice);
		void DestroyDX11WaterResources();
		void RenderWaterDX11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);

		// DX11 dynamic shadows (CSM 3-cascade + PCF)
		bool InitializeDX11ShadowResources(ID3D11Device* pDevice);
		void DestroyDX11ShadowResources();
		void RenderShadowCastersDX11(ID3D11DeviceContext* pContext);
		void RenderShadowReceiversDX11(ID3D11DeviceContext* pContext);
		bool IsDynamicShadowCaster(float fHeight) const;
		bool IsDX11DynamicShadowsReady() const { return m_bDX11ShadowResourcesReady; }

		// S1: Character shadow caster registration (called from PythonBackground)
		void ClearCharacterShadowCasters();
		void RegisterCharacterShadowCaster(class CGraphicThingInstance* pInstance);
		void RegisterObjectShadowCaster(class CGraphicThingInstance* pInstance);

		// Check if DX11 resources are ready
		bool IsDX11TerrainReady() const { return m_bDX11TerrainResourcesReady; }
		bool IsDX11WaterReady() const { return m_bDX11WaterResourcesReady; }
		int GetDX11LastRenderedWaterPatchCount() const { return m_iDX11LastRenderedWaterPatchCount; }
		int GetDX11LastObservedWaterPatchCount() const { return m_iDX11LastObservedWaterPatchCount; }
		DWORD GetDX11LastSubmittedObjectCount() const { return m_dwDX11LastSubmittedObjectCount; }
		DWORD GetDX11LastSubmittedEffectCount() const { return m_dwDX11LastSubmittedEffectCount; }
		DWORD GetDX11LastSubmittedEffectParticleCount() const { return m_dwDX11LastSubmittedEffectParticleCount; }
		DWORD GetDX11LastSubmittedEffectMeshCount() const { return m_dwDX11LastSubmittedEffectMeshCount; }
		DWORD GetDX11LastSubmittedSpeedTreeCount() const { return m_dwDX11LastSubmittedSpeedTreeCount; }
		DWORD GetDX11LastShadowSubmittedSpeedTreeCount() const { return m_dwDX11ShadowLastSubmittedSpeedTreeCount; }

		// Get stored DX11 device (for terrain patches to use during load)
		ID3D11Device* GetDX11Device() const { return m_pDX11Device; }

		// W4.2: Get DX11 object shaders for character rendering (fix DEVICE_DRAW_VERTEX_SHADER_NOT_SET)
		ID3D11VertexShader* GetDX11ObjectVS() const { return m_pDX11ObjectVS; }
		ID3D11PixelShader* GetDX11ObjectPS() const { return m_pDX11ObjectPS; }
		ID3D11InputLayout* GetDX11ObjectInputLayout() const { return m_pDX11ObjectInputLayout; }
		ID3D11Buffer* GetDX11ObjectConstantBuffer() const { return m_pDX11ObjectConstantBuffer; }
		ID3D11SamplerState* GetDX11ObjectSamplerState() const { return m_pDX11ObjectSamplerState; }

	protected:
		// DX11 terrain rendering internals
		bool __CreateDX11TerrainShaders(ID3D11Device* pDevice);
		void __DestroyDX11TerrainShaders();
		bool __CreateDX11TerrainPipelineStates(ID3D11Device* pDevice);
		void __DestroyDX11TerrainPipelineStates();
		bool __RenderTerrain_DX11HardwareTransformPatch(
			ID3D11Device* pDevice,
			ID3D11DeviceContext* pContext,
			const D3DXMATRIX& matTerrainViewProj);

		// DX11 terrain splat rendering internals
		bool __CreateDX11TerrainSplatShaders(ID3D11Device* pDevice);
		void __DestroyDX11TerrainSplatShaders();
		bool __CreateDX11TerrainSplatBlendState(ID3D11Device* pDevice);
		void __DestroyDX11TerrainSplatBlendState();

		// DX11 texture integration helpers (B4)
		ID3D11ShaderResourceView* __GetOrCreateDX11TerrainTextureSRV(const char* szFilename);
		ID3D11ShaderResourceView* __GetTerrainTextureSRV(bool* pbWasFallbackWhite = nullptr, const char* szFilename = nullptr);
		ID3D11ShaderResourceView* __GetSplatTextureSRV(
			bool* pbWasFallbackWhite = nullptr,
			const char* szFilename = nullptr,
			CTerrain* pTerrain = nullptr,
			DWORD dwSplatIndex = 0);
		ID3D11ShaderResourceView* __GetOrCreateDX11SplatAlphaSRV(CTerrain* pTerrain, DWORD dwSplatIndex);

		// B4.4: Direct DDS loading (bypass DX9 DXT1 conversion)
		ID3D11ShaderResourceView* __LoadTerrainTextureDDS(const char* szFilename, ID3D11Device* pDevice);
		ID3D11ShaderResourceView* __LoadTerrainTextureWIC(const char* szFilename, ID3D11Device* pDevice);
		void __ClearDX11TerrainTextureSRVCache();

		// DX11 water rendering internals
		bool __CreateDX11WaterShaders(ID3D11Device* pDevice);
		void __DestroyDX11WaterShaders();
		bool __CreateDX11WaterPipelineStates(ID3D11Device* pDevice);
		void __DestroyDX11WaterPipelineStates();
		void __RenderWater_DX11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);

		// W4.1: DX11 object rendering (characters, buildings, NPCs)
		bool __CreateDX11ObjectShaders(ID3D11Device* pDevice);
		void __DestroyDX11ObjectShaders();
		void __RenderObjectsDX11(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
		void __RenderCharactersDX11(ID3D11DeviceContext* pContext);

		// DX11 dynamic shadow internals
		bool __CreateDX11ShadowShaders(ID3D11Device* pDevice);
		void __DestroyDX11ShadowShaders();
		bool __CreateDX11ShadowPipelineStates(ID3D11Device* pDevice);
		void __DestroyDX11ShadowPipelineStates();
		bool __CreateDX11ShadowMapResources(ID3D11Device* pDevice);
		void __DestroyDX11ShadowMapResources();
		void __UpdateDX11ShadowCascadeMatrices();
		D3DXVECTOR3 __GetDX11ShadowLightDirection() const;
		void __LogDX11ShadowFallback(const char* c_szReason);

	private:
		// DX11 terrain resources
		bool m_bDX11TerrainResourcesReady;
		ID3D11Device* m_pDX11Device; // Stored for terrain patches to use during load
		ID3D11VertexShader* m_pDX11TerrainVertexShader;
		ID3D11PixelShader* m_pDX11TerrainPixelShader;
		ID3D11InputLayout* m_pDX11TerrainInputLayout;
		ID3D11Buffer* m_pDX11TerrainConstantBuffer;
		ID3D11Buffer* m_pDX11TerrainIndexBuffer;
		UINT m_uDX11TerrainIndexCount;
		ID3D11SamplerState* m_pDX11TerrainSamplerState;

		// DX11 terrain textures
		ID3D11Texture2D* m_pDX11TerrainDefaultTexture;
		ID3D11ShaderResourceView* m_pDX11TerrainDefaultTextureSRV;
		ID3D11Texture2D* m_pDX11TerrainMissingTexture;
		ID3D11ShaderResourceView* m_pDX11TerrainMissingTextureSRV;

		// B4.4: Direct DDS loading cache (bypass DX9 DXT1 conversion)
		std::map<std::string, ID3D11ShaderResourceView*> m_mapDX11TerrainTextureSRVCache;
		std::map<uint64_t, ID3D11ShaderResourceView*> m_mapDX11SplatAlphaSRVCache;

		// Phase 2: DirectXTK CommonStates for standardized D3D11 states
		DirectX::DX11::CommonStates* m_pDX11CommonStates;

		// DX11 terrain splat resources
		bool m_bDX11TerrainSplatResourcesReady;
		ID3D11VertexShader* m_pDX11TerrainSplatVertexShader;
		ID3D11PixelShader* m_pDX11TerrainSplatPixelShader;
		ID3D11BlendState* m_pDX11TerrainSplatBlendState;
		ID3D11SamplerState* m_pDX11TerrainSplatAlphaSamplerState;

		// DX11 water resources
		bool m_bDX11WaterResourcesReady;
		ID3D11VertexShader* m_pDX11WaterVertexShader;
		ID3D11PixelShader* m_pDX11WaterPixelShader;
		ID3D11InputLayout* m_pDX11WaterInputLayout;
		ID3D11Buffer* m_pDX11WaterConstantBuffer;
		ID3D11BlendState* m_pDX11WaterBlendState;
		ID3D11DepthStencilState* m_pDX11WaterDepthState;
		ID3D11RasterizerState* m_pDX11WaterRasterState;
		ID3D11SamplerState* m_pDX11WaterSamplerState;
		ID3D11ShaderResourceView* m_apDX11WaterTextureSRV[30];
		int m_iDX11LastRenderedWaterPatchCount;
		int m_iDX11LastObservedWaterPatchCount;

		// W4.1: DX11 object rendering resources (characters, buildings, NPCs)
		ID3D11VertexShader* m_pDX11ObjectVS;
		ID3D11PixelShader* m_pDX11ObjectPS;
		ID3D11InputLayout* m_pDX11ObjectInputLayout;
		ID3D11Buffer* m_pDX11ObjectConstantBuffer;
		ID3D11SamplerState* m_pDX11ObjectSamplerState;

		DWORD m_dwDX11LastSubmittedObjectCount;
		DWORD m_dwDX11LastSubmittedEffectCount;
		DWORD m_dwDX11LastSubmittedEffectParticleCount;
		DWORD m_dwDX11LastSubmittedEffectMeshCount;
		DWORD m_dwDX11LastSubmittedSpeedTreeCount;

		// DX11 dynamic shadow resources (CSM)
		bool m_bDX11ShadowResourcesReady;
		bool m_bDX11ShadowReceiverActive;
		bool m_bDX11ShadowFallbackActive;
		DWORD m_dwDX11ShadowLastFallbackLogMS;
		UINT m_uDX11ShadowMapSize;
		ID3D11Texture2D* m_pDX11ShadowMapTextureArray;
		ID3D11DepthStencilView* m_apDX11ShadowCascadeDSV[3];
		ID3D11ShaderResourceView* m_pDX11ShadowMapArraySRV;
		ID3D11Buffer* m_pDX11ShadowFrameConstantBuffer;
		ID3D11Buffer* m_pDX11ShadowObjectConstantBuffer;
		ID3D11SamplerState* m_pDX11ShadowComparisonSampler;
		ID3D11RasterizerState* m_pDX11ShadowRasterizerState;
		ID3D11DepthStencilState* m_pDX11ShadowDepthState;
		ID3D11VertexShader* m_pDX11ShadowCasterVertexShader;
		ID3D11PixelShader* m_pDX11ShadowCasterPixelShader;
		ID3D11VertexShader* m_pDX11ShadowReceiverVertexShader;
		ID3D11PixelShader* m_pDX11ShadowReceiverPixelShader;
		D3DXMATRIX m_akDX11ShadowLightViewProj[3];
		float m_afDX11ShadowCascadeSplits[4];
		DWORD m_dwDX11ShadowLastCasterActors;
		DWORD m_dwDX11ShadowLastCasterObjects;
		DWORD m_dwDX11ShadowLastCasterSpeedTree;
		DWORD m_dwDX11ShadowLastSubmittedSpeedTreeCount;
		DWORD m_dwDX11ShadowLastFilteredFlat;
		// Weather/day-night extensibility: this direction can be fed by sky/sun/moon/weather controllers.
		D3DXVECTOR3 m_v3DX11ShadowLightDir;
		SDX11EnvironmentBridgeState m_kDX11EnvironmentBridgeState;
		DWORD m_dwDX11EnvironmentBridgeLastLogMS;
		uint64_t m_uDX11SkyboxAppliedRevision;
		DWORD m_dwDX11SkyboxLastRevisionLogMS;
		bool m_bDX11LensFlareInitialized;
		// S1: Registered shadow casters (populated per-frame by PythonBackground)
		std::vector<class CGraphicThingInstance*> m_kVct_pkCharacterShadowCasters;
		std::vector<class CGraphicThingInstance*> m_kVct_pkObjectShadowCasters;

	protected:
		std::string		m_settings_envDataName;
		std::string		m_envDataName;

	private:
		bool m_bSettingTerrainVisible;
};

