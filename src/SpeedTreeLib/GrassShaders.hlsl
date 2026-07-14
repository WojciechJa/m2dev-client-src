// DX11 Grass Shaders
// Basic billboard grass rendering without wind animation
// Iteration 1 - Foundation

// Grass Constant Buffer
cbuffer GrassCB : register(b0)
{
	row_major float4x4 gWorldViewProj;
	float3 gCameraPos;
	float gGrassSize;
}

// Vertex Input
struct VS_IN
{
	float3 pos : POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
};

// Pixel Input
struct PS_IN
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
};

// Grass Vertex Shader - Basic Billboard
PS_IN main(VS_IN input)
{
	PS_IN output;

	// Get world position
	float3 worldPos = input.pos;

	// Billboard transformation: always face camera
	float3 toCamera = gCameraPos - worldPos;
	toCamera.y = 0.0f;  // Keep grass upright
	toCamera = normalize(toCamera);

	// Calculate billboard right and up vectors
	float3 right = float3(1.0f, 0.0f, 0.0f);
	float3 up = float3(0.0f, 1.0f, 0.0f);

	// Expand single vertex to quad
	float2 offset = input.uv * gGrassSize;
	worldPos += right * offset.x + up * offset.y;

	// Transform to clip space
	output.pos = mul(float4(worldPos, 1.0f), gWorldViewProj);
	output.uv = input.uv;
	output.color = input.color;

	return output;
}

// Grass Pixel Shader - Basic Texturing
float4 main(PS_IN input) : SV_TARGET
{
	// Sample grass texture
	// TODO: Add texture sampling when texture is available
	// For now, just output vertex color
	float4 color = input.color;

	// Alpha test to discard transparent pixels
	// TODO: Add alpha test when texture is loaded
	// if (color.a < 0.5f) discard;

	return color;
}

// Grass Pixel Shader with Texture (placeholder for Iteration 2)
/*
Texture2D g_txGrass : register(t0);
SamplerState g_smGrass : register(s0);

float4 main(PS_IN input) : SV_TARGET
{
	float4 color = g_txGrass.Sample(g_smGrass, input.uv);
	color *= input.color;  // Apply vertex color variation

	// Alpha test
	if (color.a < 0.3f)
		discard;

	return color;
}
*/
