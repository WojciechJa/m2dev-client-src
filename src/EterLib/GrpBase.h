#pragma once

#include "Ray.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include <d3d11.h>
#include <DirectXMath.h>
#include "../../../extern/third_party/DirectXTK/Inc/SimpleMath.h"

using D3DXVECTOR2 = DirectX::SimpleMath::Vector2;
using D3DXVECTOR3 = DirectX::SimpleMath::Vector3;
using D3DXVECTOR4 = DirectX::SimpleMath::Vector4;
using D3DXQUATERNION = DirectX::SimpleMath::Quaternion;
using D3DXMATRIX = DirectX::SimpleMath::Matrix;
using D3DXPLANE = DirectX::SimpleMath::Vector4;

// DX11-native compatibility constants for legacy D3DX call sites.
static constexpr DWORD D3DX_FILTER_LINEAR = 0x2;
static constexpr float D3DX_PI = DirectX::XM_PI;
static constexpr float D3DX_2PI = DirectX::XM_2PI;
static constexpr float D3DX_PI_2 = DirectX::XM_PIDIV2;

struct D3DXCOLOR
{
	float r;
	float g;
	float b;
	float a;

	D3DXCOLOR() : r(0.0f), g(0.0f), b(0.0f), a(0.0f) {}
	D3DXCOLOR(float fr, float fg, float fb, float fa) : r(fr), g(fg), b(fb), a(fa) {}
	D3DXCOLOR(const DirectX::SimpleMath::Color& color) : r(color.x), g(color.y), b(color.z), a(color.w) {}
	D3DXCOLOR(DWORD dwColor)
	{
		a = float((dwColor >> 24) & 0xFF) / 255.0f;
		r = float((dwColor >> 16) & 0xFF) / 255.0f;
		g = float((dwColor >> 8) & 0xFF) / 255.0f;
		b = float((dwColor >> 0) & 0xFF) / 255.0f;
	}

	operator DWORD() const
	{
		const DWORD dwA = DWORD(std::clamp(a, 0.0f, 1.0f) * 255.0f) & 0xFF;
		const DWORD dwR = DWORD(std::clamp(r, 0.0f, 1.0f) * 255.0f) & 0xFF;
		const DWORD dwG = DWORD(std::clamp(g, 0.0f, 1.0f) * 255.0f) & 0xFF;
		const DWORD dwB = DWORD(std::clamp(b, 0.0f, 1.0f) * 255.0f) & 0xFF;
		return (dwA << 24) | (dwR << 16) | (dwG << 8) | dwB;
	}

	D3DXCOLOR operator*(const D3DXCOLOR& rhs) const
	{
		return D3DXCOLOR(r * rhs.r, g * rhs.g, b * rhs.b, a * rhs.a);
	}

	D3DXCOLOR operator*(float s) const
	{
		return D3DXCOLOR(r * s, g * s, b * s, a * s);
	}

	D3DXCOLOR operator+(const D3DXCOLOR& rhs) const
	{
		return D3DXCOLOR(r + rhs.r, g + rhs.g, b + rhs.b, a + rhs.a);
	}

	D3DXCOLOR operator-(const D3DXCOLOR& rhs) const
	{
		return D3DXCOLOR(r - rhs.r, g - rhs.g, b - rhs.b, a - rhs.a);
	}

	D3DXCOLOR& operator=(const DirectX::SimpleMath::Color& color)
	{
		r = color.x;
		g = color.y;
		b = color.z;
		a = color.w;
		return *this;
	}

	operator DirectX::SimpleMath::Color() const
	{
		return DirectX::SimpleMath::Color(r, g, b, a);
	}
};

inline D3DXCOLOR operator*(float s, const D3DXCOLOR& c)
{
	return c * s;
}

inline float D3DXToRadian(float fDegree)
{
	return DirectX::XMConvertToRadians(fDegree);
}

inline float D3DXToDegree(float fRadian)
{
	return DirectX::XMConvertToDegrees(fRadian);
}

#if !defined(_d3d9TYPES_H_)
using D3DCOLOR = DWORD;

struct D3DVIEWPORT9
{
	DWORD X = 0;
	DWORD Y = 0;
	DWORD Width = 0;
	DWORD Height = 0;
	float MinZ = 0.0f;
	float MaxZ = 1.0f;
};

struct D3DPRESENT_PARAMETERS
{
	UINT BackBufferWidth = 0;
	UINT BackBufferHeight = 0;
	UINT PresentationInterval = 0;
};

enum D3DPRIMITIVETYPE : uint32_t
{
	D3DPT_POINTLIST = 1,
	D3DPT_LINELIST = 2,
	D3DPT_LINESTRIP = 3,
	D3DPT_TRIANGLELIST = 4,
	D3DPT_TRIANGLESTRIP = 5,
	D3DPT_TRIANGLEFAN = 6,
};

#ifndef DX11_D3DFILLMODE_DEFINED
using D3DFILLMODE = uint32_t;
#define DX11_D3DFILLMODE_DEFINED 1
#endif
static constexpr D3DFILLMODE D3DFILL_POINT = 1u;
static constexpr D3DFILLMODE D3DFILL_WIREFRAME = 2u;
static constexpr D3DFILLMODE D3DFILL_SOLID = 3u;

struct D3DMATERIAL9
{
	D3DXCOLOR Diffuse;
	D3DXCOLOR Ambient;
	D3DXCOLOR Specular;
	D3DXCOLOR Emissive;
	float Power = 0.0f;
};

static constexpr DWORD D3DCREATE_HARDWARE_VERTEXPROCESSING = 0x00000040L;
static constexpr DWORD D3DCREATE_MIXED_VERTEXPROCESSING = 0x00000080L;
static constexpr DWORD D3DPMISCCAPS_CLIPTLVERTS = 0x00000040L;
#ifndef D3DVS_VERSION
#define D3DVS_VERSION(major, minor) (((major) << 8) | (minor))
#endif

// DX11-native: neutral FVF-style layout flags used by legacy call sites.
#ifndef DX11_FVF_LAYOUT_DEFINED
#define DX11_FVF_LAYOUT_DEFINED 1
inline constexpr DWORD FVF_XYZ = 0x002;
inline constexpr DWORD FVF_NORMAL = 0x010;
inline constexpr DWORD FVF_DIFFUSE = 0x040;
inline constexpr DWORD FVF_TEX1 = 0x100;
inline constexpr DWORD FVF_TEX2 = 0x200;
inline constexpr DWORD FVF_TEXCOUNT_MASK = 0xF00;
#endif

using D3DBLEND = uint32_t;
#ifndef DX11_D3DBLEND_ZERO_DEFINED
static constexpr D3DBLEND D3DBLEND_ZERO = 1u;
#define DX11_D3DBLEND_ZERO_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_ONE_DEFINED
static constexpr D3DBLEND D3DBLEND_ONE = 2u;
#define DX11_D3DBLEND_ONE_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_SRCCOLOR_DEFINED
static constexpr D3DBLEND D3DBLEND_SRCCOLOR = 3u;
#define DX11_D3DBLEND_SRCCOLOR_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_INVSRCCOLOR_DEFINED
static constexpr D3DBLEND D3DBLEND_INVSRCCOLOR = 4u;
#define DX11_D3DBLEND_INVSRCCOLOR_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_SRCALPHA_DEFINED
static constexpr D3DBLEND D3DBLEND_SRCALPHA = 5u;
#define DX11_D3DBLEND_SRCALPHA_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_INVSRCALPHA_DEFINED
static constexpr D3DBLEND D3DBLEND_INVSRCALPHA = 6u;
#define DX11_D3DBLEND_INVSRCALPHA_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_DESTALPHA_DEFINED
static constexpr D3DBLEND D3DBLEND_DESTALPHA = 7u;
#define DX11_D3DBLEND_DESTALPHA_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_INVDESTALPHA_DEFINED
static constexpr D3DBLEND D3DBLEND_INVDESTALPHA = 8u;
#define DX11_D3DBLEND_INVDESTALPHA_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_DESTCOLOR_DEFINED
static constexpr D3DBLEND D3DBLEND_DESTCOLOR = 9u;
#define DX11_D3DBLEND_DESTCOLOR_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_INVDESTCOLOR_DEFINED
static constexpr D3DBLEND D3DBLEND_INVDESTCOLOR = 10u;
#define DX11_D3DBLEND_INVDESTCOLOR_DEFINED 1
#endif
#ifndef DX11_D3DBLEND_SRCALPHASAT_DEFINED
static constexpr D3DBLEND D3DBLEND_SRCALPHASAT = 11u;
#define DX11_D3DBLEND_SRCALPHASAT_DEFINED 1
#endif

using D3DBLENDOP = uint32_t;
static constexpr D3DBLENDOP D3DBLENDOP_ADD = 1u;
static constexpr D3DBLENDOP D3DBLENDOP_SUBTRACT = 2u;
static constexpr D3DBLENDOP D3DBLENDOP_REVSUBTRACT = 3u;
static constexpr D3DBLENDOP D3DBLENDOP_MIN = 4u;
static constexpr D3DBLENDOP D3DBLENDOP_MAX = 5u;

using D3DCMPFUNC = uint32_t;
static constexpr D3DCMPFUNC D3DCMP_NEVER = 1u;
static constexpr D3DCMPFUNC D3DCMP_LESS = 2u;
static constexpr D3DCMPFUNC D3DCMP_EQUAL = 3u;
static constexpr D3DCMPFUNC D3DCMP_LESSEQUAL = 4u;
static constexpr D3DCMPFUNC D3DCMP_GREATER = 5u;
static constexpr D3DCMPFUNC D3DCMP_NOTEQUAL = 6u;
static constexpr D3DCMPFUNC D3DCMP_GREATEREQUAL = 7u;
static constexpr D3DCMPFUNC D3DCMP_ALWAYS = 8u;

using D3DCULL = uint32_t;
static constexpr D3DCULL D3DCULL_NONE = 1u;
static constexpr D3DCULL D3DCULL_CW = 2u;
static constexpr D3DCULL D3DCULL_CCW = 3u;

using D3DSTENCILOP = uint32_t;
static constexpr D3DSTENCILOP D3DSTENCILOP_KEEP = 1u;
static constexpr D3DSTENCILOP D3DSTENCILOP_ZERO = 2u;
static constexpr D3DSTENCILOP D3DSTENCILOP_REPLACE = 3u;
static constexpr D3DSTENCILOP D3DSTENCILOP_INCRSAT = 4u;
static constexpr D3DSTENCILOP D3DSTENCILOP_DECRSAT = 5u;
static constexpr D3DSTENCILOP D3DSTENCILOP_INVERT = 6u;
static constexpr D3DSTENCILOP D3DSTENCILOP_INCR = 7u;
static constexpr D3DSTENCILOP D3DSTENCILOP_DECR = 8u;

using D3DTEXTUREFILTERTYPE = uint32_t;
static constexpr D3DTEXTUREFILTERTYPE D3DTEXF_NONE = 0u;
static constexpr D3DTEXTUREFILTERTYPE D3DTEXF_POINT = 1u;
static constexpr D3DTEXTUREFILTERTYPE D3DTEXF_LINEAR = 2u;
static constexpr D3DTEXTUREFILTERTYPE D3DTEXF_ANISOTROPIC = 3u;

using D3DTEXTUREADDRESS = uint32_t;
static constexpr D3DTEXTUREADDRESS D3DTADDRESS_WRAP = 1u;
static constexpr D3DTEXTUREADDRESS D3DTADDRESS_MIRROR = 2u;
static constexpr D3DTEXTUREADDRESS D3DTADDRESS_CLAMP = 3u;
static constexpr D3DTEXTUREADDRESS D3DTADDRESS_BORDER = 4u;
static constexpr D3DTEXTUREADDRESS D3DTADDRESS_MIRRORONCE = 5u;

using D3DFOGMODE = uint32_t;
static constexpr D3DFOGMODE D3DFOG_NONE = 0u;
static constexpr D3DFOGMODE D3DFOG_EXP = 1u;
static constexpr D3DFOGMODE D3DFOG_EXP2 = 2u;
static constexpr D3DFOGMODE D3DFOG_LINEAR = 3u;

using D3DRENDERSTATETYPE = uint32_t;
static constexpr D3DRENDERSTATETYPE D3DRS_ZENABLE = 7u;
static constexpr D3DRENDERSTATETYPE D3DRS_FILLMODE = 8u;
static constexpr D3DRENDERSTATETYPE D3DRS_SHADEMODE = 9u;
static constexpr D3DRENDERSTATETYPE D3DRS_ZWRITEENABLE = 14u;
static constexpr D3DRENDERSTATETYPE D3DRS_SRCBLEND = 19u;
static constexpr D3DRENDERSTATETYPE D3DRS_DESTBLEND = 20u;
static constexpr D3DRENDERSTATETYPE D3DRS_CULLMODE = 22u;
static constexpr D3DRENDERSTATETYPE D3DRS_ZFUNC = 23u;
static constexpr D3DRENDERSTATETYPE D3DRS_ALPHABLENDENABLE = 27u;
static constexpr D3DRENDERSTATETYPE D3DRS_FOGENABLE = 28u;
static constexpr D3DRENDERSTATETYPE D3DRS_SPECULARENABLE = 29u;
static constexpr D3DRENDERSTATETYPE D3DRS_FOGCOLOR = 34u;
static constexpr D3DRENDERSTATETYPE D3DRS_FOGSTART = 36u;
static constexpr D3DRENDERSTATETYPE D3DRS_FOGEND = 37u;
static constexpr D3DRENDERSTATETYPE D3DRS_FOGDENSITY = 38u;
static constexpr D3DRENDERSTATETYPE D3DRS_RANGEFOGENABLE = 48u;
static constexpr D3DRENDERSTATETYPE D3DRS_TEXTUREFACTOR = 60u;
static constexpr D3DRENDERSTATETYPE D3DRS_LIGHTING = 137u;
static constexpr D3DRENDERSTATETYPE D3DRS_AMBIENT = 139u;
static constexpr D3DRENDERSTATETYPE D3DRS_FOGVERTEXMODE = 140u;
static constexpr D3DRENDERSTATETYPE D3DRS_COLORWRITEENABLE = 168u;
static constexpr D3DRENDERSTATETYPE D3DRS_BLENDOP = 171u;
static constexpr D3DRENDERSTATETYPE D3DRS_SCISSORTESTENABLE = 174u;

using D3DSAMPLERSTATETYPE = uint32_t;
static constexpr D3DSAMPLERSTATETYPE D3DSAMP_ADDRESSU = 1u;
static constexpr D3DSAMPLERSTATETYPE D3DSAMP_ADDRESSV = 2u;
static constexpr D3DSAMPLERSTATETYPE D3DSAMP_ADDRESSW = 3u;
static constexpr D3DSAMPLERSTATETYPE D3DSAMP_BORDERCOLOR = 4u;
static constexpr D3DSAMPLERSTATETYPE D3DSAMP_MAGFILTER = 5u;
static constexpr D3DSAMPLERSTATETYPE D3DSAMP_MINFILTER = 6u;
static constexpr D3DSAMPLERSTATETYPE D3DSAMP_MIPFILTER = 7u;
static constexpr D3DSAMPLERSTATETYPE D3DSAMP_MAXANISOTROPY = 10u;

using D3DTRANSFORMSTATETYPE = uint32_t;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_VIEW = 2u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_PROJECTION = 3u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_TEXTURE0 = 16u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_TEXTURE1 = 17u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_TEXTURE2 = 18u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_TEXTURE3 = 19u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_TEXTURE4 = 20u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_TEXTURE5 = 21u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_TEXTURE6 = 22u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_TEXTURE7 = 23u;
static constexpr D3DTRANSFORMSTATETYPE D3DTS_WORLD = 256u;

using D3DTEXTURESTAGESTATETYPE = uint32_t;

using D3DTEXTUREARG = uint32_t;
static constexpr D3DTEXTUREARG D3DTA_DIFFUSE = 0x00000000u;
static constexpr D3DTEXTUREARG D3DTA_CURRENT = 0x00000001u;
static constexpr D3DTEXTUREARG D3DTA_TEXTURE = 0x00000002u;
static constexpr D3DTEXTUREARG D3DTA_TFACTOR = 0x00000003u;

static constexpr DWORD D3DTTFF_DISABLE = 0u;
static constexpr DWORD D3DTTFF_COUNT1 = 1u;
static constexpr DWORD D3DTTFF_COUNT2 = 2u;
static constexpr DWORD D3DTTFF_COUNT3 = 3u;
static constexpr DWORD D3DTTFF_COUNT4 = 4u;
using D3DTEXTUREOP = uint32_t;
static constexpr D3DTEXTUREOP D3DTOP_DISABLE = 1u;
static constexpr D3DTEXTUREOP D3DTOP_SELECTARG1 = 2u;
static constexpr D3DTEXTUREOP D3DTOP_SELECTARG2 = 3u;
static constexpr D3DTEXTUREOP D3DTOP_MODULATE = 4u;

using D3DFORMAT = uint32_t;
static constexpr D3DFORMAT D3DFMT_UNKNOWN = 0;
static constexpr D3DFORMAT D3DFMT_INDEX16 = 101;
static constexpr D3DFORMAT D3DFMT_INDEX32 = 102;
static constexpr D3DFORMAT D3DFMT_A8R8G8B8 = 21;
static constexpr D3DFORMAT D3DFMT_X8R8G8B8 = 22;

using D3DPOOL = uint32_t;
static constexpr D3DPOOL D3DPOOL_DEFAULT = 0;
static constexpr D3DPOOL D3DPOOL_MANAGED = 1;
static constexpr D3DPOOL D3DPOOL_SYSTEMMEM = 2;

using D3DVECTOR = D3DXVECTOR3;

static constexpr DWORD D3DUSAGE_DYNAMIC = 0x200L;
static constexpr DWORD D3DUSAGE_WRITEONLY = 0x8L;
static constexpr DWORD D3DLOCK_DISCARD = 0x2000L;

static constexpr UINT D3DPRESENT_INTERVAL_ONE = 1u;
static constexpr UINT D3DPRESENT_INTERVAL_IMMEDIATE = 0u;
#endif // !defined(_d3d9TYPES_H_)

#if !defined(_D3D9CAPS_H_) && !defined(DX11_D3DCAPS9_DEFINED)
struct D3DCAPS9
{
	DWORD PrimitiveMiscCaps = 0;
	DWORD VertexShaderVersion = 0;
};
#define DX11_D3DCAPS9_DEFINED 1
#endif // !defined(_D3D9CAPS_H_) && !defined(DX11_D3DCAPS9_DEFINED)

#ifndef D3DCOLOR_ARGB
inline DWORD D3DCOLOR_ARGB(int a, int r, int g, int b)
{
	return (DWORD((a & 0xFF) << 24) |
		DWORD((r & 0xFF) << 16) |
		DWORD((g & 0xFF) << 8) |
		DWORD((b & 0xFF)));
}

#endif // D3DCOLOR_ARGB

#ifndef D3DCOLOR_COLORVALUE
inline DWORD D3DCOLOR_COLORVALUE(float r, float g, float b, float a)
{
	return D3DCOLOR_ARGB(
		int(std::clamp(a, 0.0f, 1.0f) * 255.0f),
		int(std::clamp(r, 0.0f, 1.0f) * 255.0f),
		int(std::clamp(g, 0.0f, 1.0f) * 255.0f),
		int(std::clamp(b, 0.0f, 1.0f) * 255.0f));
}
#endif // D3DCOLOR_COLORVALUE


#ifndef D3DXIFF_DDS
using D3DXIMAGE_FILEFORMAT = DWORD;
static constexpr D3DXIMAGE_FILEFORMAT D3DXIFF_BMP = 0u;
static constexpr D3DXIMAGE_FILEFORMAT D3DXIFF_JPG = 1u;
static constexpr D3DXIMAGE_FILEFORMAT D3DXIFF_TGA = 2u;
static constexpr D3DXIMAGE_FILEFORMAT D3DXIFF_PNG = 3u;
static constexpr D3DXIMAGE_FILEFORMAT D3DXIFF_DDS = 4u;
static constexpr D3DXIMAGE_FILEFORMAT D3DXIFF_PPM = 5u;
static constexpr D3DXIMAGE_FILEFORMAT D3DXIFF_DIB = 6u;
static constexpr D3DXIMAGE_FILEFORMAT D3DXIFF_HDR = 7u;
static constexpr D3DXIMAGE_FILEFORMAT D3DXIFF_PFM = 8u;

HRESULT D3DXSaveTextureToFileA(const char* pDestFile, D3DXIMAGE_FILEFORMAT DestFormat, ID3D11ShaderResourceView* pSrcTexture, const void* pPalette);
HRESULT D3DXSaveTextureToFileW(const wchar_t* pDestFile, D3DXIMAGE_FILEFORMAT DestFormat, ID3D11ShaderResourceView* pSrcTexture, const void* pPalette);
#ifdef UNICODE
#define D3DXSaveTextureToFile D3DXSaveTextureToFileW
#else
#define D3DXSaveTextureToFile D3DXSaveTextureToFileA
#endif
#endif

inline D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX* pOut)
{
	*pOut = D3DXMATRIX::Identity;
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX* pOut, const D3DXMATRIX* pM1, const D3DXMATRIX* pM2)
{
	*pOut = (*pM1) * (*pM2);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX* pOut, float*, const D3DXMATRIX* pM)
{
	const DirectX::XMMATRIX kInv = DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(pM));
	DirectX::XMStoreFloat4x4(pOut, kInv);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixRotationX(D3DXMATRIX* pOut, float angle)
{
	*pOut = D3DXMATRIX::CreateRotationX(angle);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixRotationY(D3DXMATRIX* pOut, float angle)
{
	*pOut = D3DXMATRIX::CreateRotationY(angle);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixRotationZ(D3DXMATRIX* pOut, float angle)
{
	*pOut = D3DXMATRIX::CreateRotationZ(angle);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixRotationAxis(D3DXMATRIX* pOut, const D3DXVECTOR3* pAxis, float angle)
{
	*pOut = D3DXMATRIX::CreateFromAxisAngle(*pAxis, angle);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixRotationYawPitchRoll(D3DXMATRIX* pOut, float yaw, float pitch, float roll)
{
	*pOut = D3DXMATRIX::CreateFromYawPitchRoll(yaw, pitch, roll);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixRotationQuaternion(D3DXMATRIX* pOut, const D3DXQUATERNION* pQ)
{
	*pOut = D3DXMATRIX::CreateFromQuaternion(*pQ);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixTranslation(D3DXMATRIX* pOut, float x, float y, float z)
{
	*pOut = D3DXMATRIX::CreateTranslation(x, y, z);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixScaling(D3DXMATRIX* pOut, float x, float y, float z)
{
	*pOut = D3DXMATRIX::CreateScale(x, y, z);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixLookAtRH(D3DXMATRIX* pOut, const D3DXVECTOR3* pEye, const D3DXVECTOR3* pAt, const D3DXVECTOR3* pUp)
{
	const DirectX::XMMATRIX m = DirectX::XMMatrixLookAtRH(
		DirectX::XMLoadFloat3(pEye),
		DirectX::XMLoadFloat3(pAt),
		DirectX::XMLoadFloat3(pUp));
	DirectX::XMStoreFloat4x4(pOut, m);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixOrthoRH(D3DXMATRIX* pOut, float w, float h, float zn, float zf)
{
	const DirectX::XMMATRIX m = DirectX::XMMatrixOrthographicRH(w, h, zn, zf);
	DirectX::XMStoreFloat4x4(pOut, m);
	return pOut;
}

// M2-GRPBASE-TYPE-CUT-73: Renamed function to use FVF_* constants (neutral naming)
inline UINT D3DXGetFVFVertexSize(DWORD fvf)
{
	UINT stride = 0;

	if (fvf & FVF_XYZ)
		stride += 3u * sizeof(float);
	if (fvf & FVF_NORMAL)
		stride += 3u * sizeof(float);
	if (fvf & FVF_DIFFUSE)
		stride += sizeof(DWORD);

	const UINT texCount = (fvf & FVF_TEXCOUNT_MASK) >> 8;
	stride += texCount * 2u * sizeof(float);
	return stride;
}

inline D3DXMATRIX* D3DXMatrixOrthoOffCenterRH(D3DXMATRIX* pOut, float l, float r, float b, float t, float zn, float zf)
{
	const DirectX::XMMATRIX m = DirectX::XMMatrixOrthographicOffCenterRH(l, r, b, t, zn, zf);
	DirectX::XMStoreFloat4x4(pOut, m);
	return pOut;
}

inline D3DXMATRIX* D3DXMatrixPerspectiveFovRH(D3DXMATRIX* pOut, float fovy, float aspect, float zn, float zf)
{
	const DirectX::XMMATRIX m = DirectX::XMMatrixPerspectiveFovRH(fovy, aspect, zn, zf);
	DirectX::XMStoreFloat4x4(pOut, m);
	return pOut;
}

inline float D3DXVec3Length(const D3DXVECTOR3* pV)
{
	return DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(pV)));
}

inline float D3DXVec3LengthSq(const D3DXVECTOR3* pV)
{
	return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(DirectX::XMLoadFloat3(pV)));
}

inline float D3DXVec3Dot(const D3DXVECTOR3* pV1, const D3DXVECTOR3* pV2)
{
	return DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(pV1), DirectX::XMLoadFloat3(pV2)));
}

inline D3DXVECTOR3* D3DXVec3Cross(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV1, const D3DXVECTOR3* pV2)
{
	DirectX::XMStoreFloat3(pOut, DirectX::XMVector3Cross(DirectX::XMLoadFloat3(pV1), DirectX::XMLoadFloat3(pV2)));
	return pOut;
}

inline D3DXVECTOR3* D3DXVec3Normalize(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV)
{
	DirectX::XMStoreFloat3(pOut, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(pV)));
	return pOut;
}

inline D3DXVECTOR3* D3DXVec3TransformCoord(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, const D3DXMATRIX* pM)
{
	DirectX::XMStoreFloat3(pOut, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(pV), DirectX::XMLoadFloat4x4(pM)));
	return pOut;
}

inline D3DXVECTOR3* D3DXVec3TransformNormal(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, const D3DXMATRIX* pM)
{
	DirectX::XMStoreFloat3(pOut, DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(pV), DirectX::XMLoadFloat4x4(pM)));
	return pOut;
}

inline D3DXVECTOR3* D3DXVec3Add(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV1, const D3DXVECTOR3* pV2)
{
	*pOut = *pV1 + *pV2;
	return pOut;
}

inline D3DXVECTOR3* D3DXVec3Scale(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, float s)
{
	*pOut = *pV * s;
	return pOut;
}

inline D3DXVECTOR3* D3DXVec3Lerp(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV1, const D3DXVECTOR3* pV2, float t)
{
	*pOut = DirectX::SimpleMath::Vector3::Lerp(*pV1, *pV2, t);
	return pOut;
}

inline float D3DXVec2Length(const D3DXVECTOR2* pV)
{
	return DirectX::XMVectorGetX(DirectX::XMVector2Length(DirectX::XMLoadFloat2(pV)));
}

inline D3DXVECTOR4* D3DXVec4Transform(D3DXVECTOR4* pOut, const D3DXVECTOR4* pV, const D3DXMATRIX* pM)
{
	DirectX::XMStoreFloat4(pOut, DirectX::XMVector4Transform(DirectX::XMLoadFloat4(pV), DirectX::XMLoadFloat4x4(pM)));
	return pOut;
}

inline D3DXQUATERNION* D3DXQuaternionRotationAxis(D3DXQUATERNION* pOut, const D3DXVECTOR3* pAxis, float angle)
{
	DirectX::XMStoreFloat4(pOut, DirectX::XMQuaternionRotationAxis(DirectX::XMLoadFloat3(pAxis), angle));
	return pOut;
}

inline D3DXQUATERNION* D3DXQuaternionRotationYawPitchRoll(D3DXQUATERNION* pOut, float yaw, float pitch, float roll)
{
	DirectX::XMStoreFloat4(pOut, DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
	return pOut;
}

inline D3DXQUATERNION* D3DXQuaternionMultiply(D3DXQUATERNION* pOut, const D3DXQUATERNION* pQ1, const D3DXQUATERNION* pQ2)
{
	*pOut = (*pQ1) * (*pQ2);
	return pOut;
}

inline D3DXQUATERNION* D3DXQuaternionConjugate(D3DXQUATERNION* pOut, const D3DXQUATERNION* pQ)
{
	DirectX::XMStoreFloat4(pOut, DirectX::XMQuaternionConjugate(DirectX::XMLoadFloat4(pQ)));
	return pOut;
}

inline D3DXCOLOR* D3DXColorModulate(D3DXCOLOR* pOut, const D3DXCOLOR* pC1, const D3DXCOLOR* pC2)
{
	*pOut = (*pC1) * (*pC2);
	return pOut;
}

inline D3DXPLANE* D3DXPlaneNormalize(D3DXPLANE* pOut, const D3DXPLANE* pP)
{
	DirectX::XMStoreFloat4(pOut, DirectX::XMPlaneNormalize(DirectX::XMLoadFloat4(pP)));
	return pOut;
}

inline D3DXVECTOR3* D3DXVec3Project(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, const D3DVIEWPORT9* pViewport, const D3DXMATRIX* pProjection, const D3DXMATRIX* pView, const D3DXMATRIX* pWorld)
{
	const DirectX::XMVECTOR v = DirectX::XMVector3Project(
		DirectX::XMLoadFloat3(pV),
		static_cast<float>(pViewport->X),
		static_cast<float>(pViewport->Y),
		static_cast<float>(pViewport->Width),
		static_cast<float>(pViewport->Height),
		pViewport->MinZ,
		pViewport->MaxZ,
		DirectX::XMLoadFloat4x4(pProjection),
		DirectX::XMLoadFloat4x4(pView),
		DirectX::XMLoadFloat4x4(pWorld));
	DirectX::XMStoreFloat3(pOut, v);
	return pOut;
}

inline D3DXVECTOR3* D3DXVec3Unproject(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, const D3DVIEWPORT9* pViewport, const D3DXMATRIX* pProjection, const D3DXMATRIX* pView, const D3DXMATRIX* pWorld)
{
	const DirectX::XMVECTOR v = DirectX::XMVector3Unproject(
		DirectX::XMLoadFloat3(pV),
		static_cast<float>(pViewport->X),
		static_cast<float>(pViewport->Y),
		static_cast<float>(pViewport->Width),
		static_cast<float>(pViewport->Height),
		pViewport->MinZ,
		pViewport->MaxZ,
		DirectX::XMLoadFloat4x4(pProjection),
		DirectX::XMLoadFloat4x4(pView),
		DirectX::XMLoadFloat4x4(pWorld));
	DirectX::XMStoreFloat3(pOut, v);
	return pOut;
}

class CD3DXMeshCompat
{
public:
	HRESULT DrawSubset(UINT) { return S_OK; }
	ULONG Release() { return 0u; }
};

using LPD3DXMESH = CD3DXMeshCompat*;

inline HRESULT D3DXCreateSphere(ID3D11Device*, float, UINT, UINT, LPD3DXMESH* ppMesh, void*)
{
	if (!ppMesh)
		return E_INVALIDARG;
	*ppMesh = new CD3DXMeshCompat();
	return S_OK;
}

inline HRESULT D3DXCreateCylinder(ID3D11Device*, float, float, float, UINT, UINT, LPD3DXMESH* ppMesh, void*)
{
	if (!ppMesh)
		return E_INVALIDARG;
	*ppMesh = new CD3DXMeshCompat();
	return S_OK;
}

void PixelPositionToD3DXVECTOR3(const D3DXVECTOR3& c_rkPPosSrc, D3DXVECTOR3* pv3Dst);
void D3DXVECTOR3ToPixelPosition(const D3DXVECTOR3& c_rv3Src, D3DXVECTOR3* pv3Dst);

class CGraphicTexture;

typedef WORD TIndex;

typedef struct SFace
{
	TIndex indices[3];
} TFace;

typedef D3DXVECTOR3 TPosition;

typedef D3DXVECTOR3 TNormal;

typedef D3DXVECTOR2 TTextureCoordinate;

typedef DWORD TDiffuse;
typedef DWORD TAmbient;
typedef DWORD TSpecular;

typedef union UDepth
{
	float	f;
	long	l;
	DWORD	dw;
} TDepth;

typedef struct SVertex
{
	float x, y, z;
	DWORD color;
	float u, v;
} TVertex;

struct STVertex
{
	float x, y, z, rhw;
};

struct SPVertex
{
	float x, y, z;
};

typedef struct SPDVertex
{
	float x, y, z;
	DWORD color;
} TPDVertex;

struct SPDTVertexRaw
{
	float px, py, pz;
	DWORD diffuse;
	float u, v;
};

typedef struct SPTVertex
{
	TPosition position;
	TTextureCoordinate texCoord;
} TPTVertex;

typedef struct SPDTVertex
{
	TPosition	position;
	TDiffuse	diffuse;
	TTextureCoordinate texCoord;
} TPDTVertex;

typedef struct SPNTVertex
{
	TPosition			position;
	TNormal				normal;
	TTextureCoordinate	texCoord;
} TPNTVertex;

typedef struct SPNT2Vertex
{
	TPosition	position;
	TNormal		normal;
	TTextureCoordinate texCoord;
	TTextureCoordinate texCoord2;
} TPNT2Vertex;

typedef struct SPDT2Vertex
{	
	TPosition	position;
	DWORD		diffuse;	
	TTextureCoordinate texCoord;
	TTextureCoordinate texCoord2;
} TPDT2Vertex;

typedef struct SNameInfo
{
	DWORD	name;
	TDepth	depth;
} TNameInfo;

typedef struct SBoundBox
{
	float sx, sy, sz;
	float ex, ey, ez;
	int meshIndex;
	int boneIndex;
} TBoundBox;

const WORD c_FillRectIndices[6] = { 0, 2, 1, 2, 3, 1 };

/*
enum EIndexCount
{
	LINE_INDEX_COUNT = 2,
	TRIANGLE_INDEX_COUNT = 2*3,
	RECTANGLE_INDEX_COUNT = 2*4,
	CUBE_INDEX_COUNT = 2*4*3,
	FILLED_TRIANGLE_INDEX_COUNT = 3,
	FILLED_RECTANGLE_INDEX_COUNT = 3*2,
	FILLED_CUBE_INDEX_COUNT = 3*2*6,
};
*/

class CGraphicBase
{
	public:
		static uint64_t GetAvailableTextureMemory();
		static const D3DXMATRIX& GetViewMatrix();
		static const D3DXMATRIX& GetProjMatrix();
		static const D3DXMATRIX & GetIdentityMatrix();

		enum
		{			
			DEFAULT_IB_LINE, 
			DEFAULT_IB_LINE_TRI, 
			DEFAULT_IB_LINE_RECT, 
			DEFAULT_IB_LINE_CUBE, 
			DEFAULT_IB_FILL_TRI,
			DEFAULT_IB_FILL_RECT,
			DEFAULT_IB_FILL_CUBE,
			DEFAULT_IB_NUM,
		};

	public:
		CGraphicBase();
		virtual	~CGraphicBase();

		void		SetSimpleCamera(float x, float y, float z, float pitch, float roll);
		void		SetEyeCamera(float xEye, float yEye, float zEye, float xCenter, float yCenter, float zCenter, float xUp, float yUp, float zUp);
		void		SetAroundCamera(float distance, float pitch, float roll, float lookAtZ = 0.0f);
		void		SetPositionCamera(float fx, float fy, float fz, float fDistance, float fPitch, float fRotation);
		void		MoveCamera(float fdeltax, float fdeltay, float fdeltaz);

		void		GetTargetPosition(float * px, float * py, float * pz);
		void		GetCameraPosition(float * px, float * py, float * pz);
		void		SetOrtho2D(float hres, float vres, float zres);
		void		SetOrtho3D(float hres, float vres, float zmin, float zmax);
		void		SetPerspective(float fov, float aspect, float nearz, float farz);
		float		GetFOV();
		void		GetClipPlane(float * fNearY, float * fFarY)
		{
			*fNearY = ms_fNearY;
			*fFarY = ms_fFarY;
		}

		////////////////////////////////////////////////////////////////////////
		void		PushMatrix();

		void		MultMatrix( const D3DXMATRIX* pMat );
		void		MultMatrixLocal( const D3DXMATRIX* pMat );
	
		void		Translate(float x, float y, float z);
		void		Rotate(float degree, float x, float y, float z);
		void		RotateLocal(float degree, float x, float y, float z);
		void		RotateYawPitchRollLocal(float fYaw, float fPitch, float fRoll);
		void		Scale(float x, float y, float z);
		void		PopMatrix();		
		void		LoadMatrix(const D3DXMATRIX & c_rSrcMatrix);		
		void		GetMatrix(D3DXMATRIX * pRetMatrix) const;
		const		D3DXMATRIX * GetMatrixPointer() const;

		// Special Routine
		void		GetSphereMatrix(D3DXMATRIX * pMatrix, float fValue = 0.1f);

		////////////////////////////////////////////////////////////////////////
		void		InitScreenEffect();
		void		SetScreenEffectWaving(float fDuringTime, int iPower);
		void		SetScreenEffectFlashing(float fDuringTime, const D3DXCOLOR & c_rColor);

		////////////////////////////////////////////////////////////////////////
		DWORD		GetColor(float r, float g, float b, float a = 1.0f);

		DWORD		GetFaceCount();
		void		ResetFaceCount();
		HRESULT		GetLastResult();

		void		UpdateProjMatrix();
		void		UpdateViewMatrix();

		void		SetViewport(DWORD dwX, DWORD dwY, DWORD dwWidth, DWORD dwHeight, float fMinZ, float fMaxZ);
		static void		GetViewport(DWORD* pdwX, DWORD* pdwY, DWORD* pdwWidth, DWORD* pdwHeight, float* pfMinZ, float* pfMaxZ);
		static void		GetBackBufferSize(UINT* puWidth, UINT* puHeight);
		static void		SetBackBufferSize(UINT uWidth, UINT uHeight);
		static bool		IsTLVertexClipping();
		static bool		IsFastTNL();
		static bool		IsLowTextureMemory();
		static bool		IsHighTextureMemory();

		static void SetDefaultIndexBuffer(UINT eDefIB);
		static bool SetPDTStream(SPDTVertexRaw* pVertices, UINT uVtxCount);
		static bool SetPDTStream(SPDTVertex* pVertices, UINT uVtxCount);
		
	protected:
		static std::vector<D3DXMATRIX>	ms_matStack;

		static D3DXMATRIX				ms_matIdentity;

		static D3DXMATRIX				ms_matView;
		static D3DXMATRIX				ms_matProj;
		static D3DXMATRIX				ms_matInverseView;
		static D3DXMATRIX				ms_matInverseViewYAxis;

		static D3DXMATRIX				ms_matWorld;
		static D3DXMATRIX				ms_matWorldView;

	protected:
		//void		UpdatePrePipeLineMatrix();
		void		UpdatePipeLineMatrix();

	protected:
		// ÃªÂ°ÂÃ¬Â¢â€¦ D3DX Mesh Ã«â€œÂ¤ (Ã¬Â»Â¬Ã«Â£Â¨Ã¬Â Â¼ Ã«ÂÂ°Ã¬ÂÂ´Ã­â€žÂ° Ã«â€œÂ±Ã¬Ââ€ž Ã­â€˜Å“Ã¬â€¹Å“Ã­â„¢Å“ Ã«â€¢Å’ Ã¬â€œÂ´Ã«â€¹Â¤)
		static LPD3DXMESH				ms_lpSphereMesh;
		static LPD3DXMESH				ms_lpCylinderMesh;

	protected:
		static HRESULT					ms_hLastResult;

		static int						ms_iWidth;
		static int						ms_iHeight;	

		static HWND						ms_hWnd;
		static HDC						ms_hDC;
		static D3DVIEWPORT9				ms_Viewport;

		static DWORD					ms_faceCount;
		static D3DCAPS9					ms_d3dCaps;
		static D3DPRESENT_PARAMETERS	ms_d3dPresentParameter;
		
		static DWORD					ms_dwD3DBehavior;

		static D3DXMATRIX				ms_matScreen0;
		static D3DXMATRIX				ms_matScreen1;
		static D3DXMATRIX				ms_matScreen2;
		//static D3DXMATRIX				ms_matPrePipeLine;

		static D3DXVECTOR3				ms_vtPickRayOrig;
		static D3DXVECTOR3				ms_vtPickRayDir;

		static float					ms_fFieldOfView;
		static float					ms_fAspect;
		static float					ms_fNearY;
		static float					ms_fFarY;

		// 2004.11.18.myevan.DynamicVertexBufferÃ«Â¡Å“ ÃªÂµÂÃ¬Â²Â´
		/*
		static std::vector<TIndex>		ms_lineIdxVector;
		static std::vector<TIndex>		ms_lineTriIdxVector;
		static std::vector<TIndex>		ms_lineRectIdxVector;
		static std::vector<TIndex>		ms_lineCubeIdxVector;

		static std::vector<TIndex>		ms_fillTriIdxVector;
		static std::vector<TIndex>		ms_fillRectIdxVector;
		static std::vector<TIndex>		ms_fillCubeIdxVector;
		*/

		// Screen Effect - Waving, Flashing and so on..
		static DWORD					ms_dwWavingEndTime;
		static int						ms_iWavingPower;
		static DWORD					ms_dwFlashingEndTime;
		static D3DXCOLOR				ms_FlashingColor;

		// Terrain pickingÃ¬Å¡Â© Ray... CCamera Ã¬ÂÂ´Ã¬Å¡Â©Ã­â€¢ËœÃ«Å â€ Ã«Â²â€žÃ¬Â â€ž.. ÃªÂ¸Â°Ã¬Â¡Â´Ã¬ÂËœ RayÃ¬â„¢â‚¬ Ã­â€ ÂµÃ­â€¢Â© Ã­â€¢â€žÃ¬Å¡â€...
 		static CRay						ms_Ray;

		// 
		static bool						ms_bSupportDXT;
		static bool						ms_isLowTextureMemory;
		static bool						ms_isHighTextureMemory;

		enum
		{
			PDT_VERTEX_NUM = 16,
			PDT_VERTEXBUFFER_NUM = 100,				
		};
		
		
};













