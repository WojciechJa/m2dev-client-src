#pragma once

#include <d3d11.h>
#include <DirectXMath.h>

// DX11 Grass Vertex Format
// Matches the grass shader input structure
struct GrassVertex
{
	DirectX::XMFLOAT3 position;     // World position (X, Y, Z)
	DirectX::XMFLOAT2 uv;           // Texcoord for grass blade (U, V)
	DirectX::XMFLOAT4 color;        // Vertex color variation (R, G, B, A)

	GrassVertex()
		: position(0.0f, 0.0f, 0.0f)
		, uv(0.0f, 0.0f)
		, color(1.0f, 1.0f, 1.0f, 1.0f)
	{
	}

	GrassVertex(
		const DirectX::XMFLOAT3& pos,
		const DirectX::XMFLOAT2& texcoord,
		const DirectX::XMFLOAT4& col)
		: position(pos)
		, uv(texcoord)
		, color(col)
	{
	}
};

// DX11 Input Layout for Grass Rendering
// Matches GrassVertex structure with GrassShaders.hlsl input
const D3D11_INPUT_ELEMENT_DESC GrassInputLayout[] =
{
	{
		"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
		0, D3D11_INPUT_PER_VERTEX_DATA, 0
	},
	{
		"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
		12, D3D11_INPUT_PER_VERTEX_DATA, 0
	},
	{
		"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
		20, D3D11_INPUT_PER_VERTEX_DATA, 0
	}
};

const UINT GrassInputLayoutElements = ARRAYSIZE(GrassInputLayout);

// Grass Constant Buffer
// Matches GrassCB in GrassShaders.hlsl (Iteration 3 - Wind Animation)
#pragma pack(push, 4)
struct GrassConstantBuffer
{
	DirectX::XMFLOAT4X4 worldViewProj;      // 64 bytes - World-view-projection matrix
	DirectX::XMFLOAT4 cameraPosAndSize;     // 16 bytes - Camera position (xyz) and grass size (w)
	DirectX::XMFLOAT4 timeAndWind;          // 16 bytes - Time (x), wind strength (y), padding (zw)
	DirectX::XMFLOAT4X4 windMatrix;         // 64 bytes - Wind rotation matrix
};
#pragma pack(pop)

static_assert(sizeof(GrassConstantBuffer) % 16 == 0, "GrassConstantBuffer must be 16-byte aligned");

// Grass Rendering Statistics
struct GrassStats
{
	UINT vertexCount;     // Total grass vertices rendered
	UINT triangleCount;   // Total triangles rendered
	UINT regionCount;     // Number of grass regions
	UINT culledRegionCount; // Regions culled by frustum

	GrassStats()
		: vertexCount(0)
		, triangleCount(0)
		, regionCount(0)
		, culledRegionCount(0)
	{
	}
};
