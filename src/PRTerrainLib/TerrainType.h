#ifndef __TERRAIn_TYPES__
#define __TERRAIn_TYPES__

#include "EterLib/GrpVertexBuffer.h"
#include "EterLib/GrpIndexBuffer.h"
#include <d3d11.h>
#include "EterLib/DirectXMathHelpers.h"

#define TERRAIN_PATCHSIZE	16
#define TERRAIN_SIZE		128
#define TERRAIN_PATCHCOUNT	TERRAIN_SIZE/TERRAIN_PATCHSIZE
#define MAXTERRAINTEXTURES	256

typedef struct
{
	long					Active;
	long					NeedsUpdate;
	ID3D11ShaderResourceView*		pd3dTexture;
	ID3D11ShaderResourceView* pTextureSRV; // M2-PRTERRAIN-DX11-NATIVE-01: DX11 SRV instead of ID3D11ShaderResourceView*
} TTerainSplat;

typedef struct
{
 	DWORD			TileCount[MAXTERRAINTEXTURES];
	DWORD			PatchTileCount[TERRAIN_PATCHCOUNT*TERRAIN_PATCHCOUNT][MAXTERRAINTEXTURES];
	TTerainSplat 	Splats[MAXTERRAINTEXTURES];
	bool			m_bNeedsUpdate;
} TTerrainSplatPatch;

typedef struct
{
	char					used;
	short					mat;
	
	CGraphicVertexBuffer	vb;
	CGraphicIndexBuffer		ib;
	long					VertexSize;
	
	short					NumIndices;
	
	float					minx, maxx;
	float					miny, maxy;
	float					minz, maxz;
} TERRAIN_VBUFFER;

typedef struct
{
	char name[19];
	float ambi_r, ambi_g, ambi_b, ambi_a;		/* Ambient Color */
	float diff_r, diff_g, diff_b, diff_a;		/* Diffuse Color */
	float spec_r, spec_g, spec_b, spec_a;		/* Specular Color */
	float spec_power;							/* Specular power */
} PR_MATERIAL;

typedef struct
{
	/* Public Settings */
	float			PageUVLength;
	long			SquaresPerTexture;              /* Heightfield squares per texture (128 texels) */
	long			SplatTilesX;					/* Number of splat textures across map */
	long			SplatTilesY;					/* Number of splat textures down map */
	long			DisableWrapping;
	long			DisableShadow;
	long			ShadowMode;
	long			OutsideVisible;
	DirectX::SimpleMath::Vector3 SunLocation; // M2-PRTERRAIN-DX11-NATIVE-01: Migrated from D3DXVECTOR3
} TTerrainGlobals;

/* Converts a floating point number to an integer by truncation, using
   the FISTP instruction */

#define PR_FLOAT_TO_INT(inreal, outint) (outint) = static_cast<int>(inreal);

#endif //__TERRAIn_TYPES__
