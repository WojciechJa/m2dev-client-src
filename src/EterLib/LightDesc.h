#pragma once

#include "GrpBase.h"

// DX11-neutral light type descriptor (no D3D9 enum dependency).
using ELightDescType = uint32_t;
static constexpr ELightDescType LIGHT_DESC_TYPE_POINT = 1u;
static constexpr ELightDescType LIGHT_DESC_TYPE_SPOT = 2u;
static constexpr ELightDescType LIGHT_DESC_TYPE_DIRECTIONAL = 3u;

struct SLightDesc
{
	SLightDesc()
		: Type(LIGHT_DESC_TYPE_POINT)
		, Diffuse(0.0f, 0.0f, 0.0f, 1.0f)
		, Specular(0.0f, 0.0f, 0.0f, 1.0f)
		, Ambient(0.5f, 0.5f, 0.5f, 1.0f)
		, Position(0.0f, 0.0f, 0.0f)
		, Direction(0.0f, 0.0f, 1.0f)
		, Range(0.0f)
		, Falloff(1.0f)
		, Attenuation0(0.0f)
		, Attenuation1(1.0f)
		, Attenuation2(0.0f)
		, Theta(0.0f)
		, Phi(0.0f)
	{
	}

	ELightDescType Type;
	D3DXCOLOR Diffuse;
	D3DXCOLOR Specular;
	D3DXCOLOR Ambient;
	D3DXVECTOR3 Position;
	D3DXVECTOR3 Direction;
	float Range;
	float Falloff;
	float Attenuation0;
	float Attenuation1;
	float Attenuation2;
	float Theta;
	float Phi;
};
