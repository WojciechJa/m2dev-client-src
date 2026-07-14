// Decal.cpp: implementation of the CDecal class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Decal.h"
#include "GrpDeviceDX11.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <vector>

namespace
{
	template <typename T>
	inline void ReleaseCOM(T*& rpObject)
	{
		if (rpObject)
		{
			rpObject->Release();
			rpObject = nullptr;
		}
	}

	struct SDecalDX11Vertex
	{
		float x, y, z;
		DWORD diffuse;
	};

	struct SDecalDX11CB
	{
		D3DXMATRIX matViewProj;
	};

	bool CompileDecalShader(
		const char* c_szSource,
		const char* c_szEntry,
		const char* c_szTarget,
		ID3DBlob** ppOutBlob)
	{
		if (!c_szSource || !c_szEntry || !c_szTarget || !ppOutBlob)
			return false;

		*ppOutBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;
		const HRESULT hResult = D3DCompile(
			c_szSource,
			strlen(c_szSource),
			nullptr,
			nullptr,
			nullptr,
			c_szEntry,
			c_szTarget,
			D3DCOMPILE_ENABLE_STRICTNESS,
			0u,
			ppOutBlob,
			&pErrorBlob);

		if (FAILED(hResult))
		{
			if (pErrorBlob && pErrorBlob->GetBufferPointer())
				TraceError("DX11_DECAL_SHADER_COMPILE_FAIL entry=%s target=%s hr=0x%08X error=%s", c_szEntry, c_szTarget, static_cast<unsigned int>(hResult), static_cast<const char*>(pErrorBlob->GetBufferPointer()));
			else
				TraceError("DX11_DECAL_SHADER_COMPILE_FAIL entry=%s target=%s hr=0x%08X", c_szEntry, c_szTarget, static_cast<unsigned int>(hResult));
		}

		ReleaseCOM(pErrorBlob);
		return SUCCEEDED(hResult) && *ppOutBlob;
	}
}

//////////////////////////////////////////////////////////////////////
// CDecal
//////////////////////////////////////////////////////////////////////

CDecal::CDecal():m_cfDecalEpsilon(0.25f)
{
	Clear();
}

CDecal::~CDecal()
{
	Clear();
}

void CDecal::Clear()
{
	m_v3Center = TPosition(0.0f, 0.0f, 0.0f);
	m_v3Normal = TPosition(0.0f, 0.0f, 0.0f);

	m_v4LeftPlane = DirectX::SimpleMath::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	m_v4RightPlane = DirectX::SimpleMath::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	m_v4TopPlane = DirectX::SimpleMath::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	m_v4BottomPlane = DirectX::SimpleMath::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	m_v4FrontPlane = DirectX::SimpleMath::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	m_v4BackPlane = DirectX::SimpleMath::Vector4(0.0f, 0.0f, 0.0f, 0.0f);

	m_dwVertexCount = 0;
	m_dwPrimitiveCount = 0;
	
	m_TriangleFanStructVector.clear();

	memset(m_Vertices, 0, sizeof(m_Vertices));
	memset(m_Indices, 0, sizeof(m_Indices));
}

void CDecal::ClipMesh(DWORD dwPrimitiveCount, const TPosition *c_pv3Vertex, const TPosition *c_pv3Normal)
{
	TPosition		v3NewVertex[9];
	TPosition		v3NewNormal[9];
	
	// Clip one triangle at a time
	for(DWORD dwi = 0; dwi < dwPrimitiveCount; ++dwi)
	{
		const TPosition & v3_1 = c_pv3Vertex[3 * dwi];
		const TPosition & v3_2 = c_pv3Vertex[3 * dwi + 1];
		const TPosition & v3_3 = c_pv3Vertex[3 * dwi + 2];
		
		TPosition v3Cross;
		const auto vv_ = (v3_2 - v3_1);
		const auto vv_2 = (v3_3 - v3_1);
		DXMath::Vec3Cross(&v3Cross, &vv_, &vv_2);
		if (DXMath::Vec3Dot(&m_v3Normal, &v3Cross) > ( m_cfDecalEpsilon ) * DXMath::Vec3Length(&v3Cross))
		{
			v3NewVertex[0] = v3_1;
			v3NewVertex[1] = v3_2;
			v3NewVertex[2] = v3_3;
			
			v3NewNormal[0] = c_pv3Normal[3 * dwi];
			v3NewNormal[1] = c_pv3Normal[3 * dwi + 1];
			v3NewNormal[2] = c_pv3Normal[3 * dwi + 2];
			
			DWORD dwCount = ClipPolygon(3, v3NewVertex, v3NewNormal, v3NewVertex, v3NewNormal);
			if ((dwCount != 0) && (!AddPolygon(dwCount, v3NewVertex, v3NewNormal))) break;
 		}
	}
}

bool CDecal::AddPolygon(DWORD dwAddCount, const TPosition *c_pv3Vertex, const TPosition  * /*c_pv3Normal */)
{
	if (m_dwVertexCount + dwAddCount >= MAX_DECAL_VERTICES)
		return false;

	TTRIANGLEFANSTRUCT aTriangleFanStruct;
	aTriangleFanStruct.m_wMinIndex = m_dwVertexCount;
	aTriangleFanStruct.m_dwVertexCount = dwAddCount;
	aTriangleFanStruct.m_dwPrimitiveCount = dwAddCount - 2;
	aTriangleFanStruct.m_dwVBOffset = m_dwVertexCount;

	m_TriangleFanStructVector.push_back(aTriangleFanStruct);

	DWORD dwCount = m_dwVertexCount;

	// Add polygon as a triangle fan
	WORD * wIndex = m_Indices + dwCount;

	m_dwPrimitiveCount += dwAddCount - 2;
	//float fOne_over_1MinusDecalEpsilon = 1.0f / (1.0f - m_cfDecalEpsilon);
	
	// Assign vertex colors
	for (DWORD dwVertexNum = 0; dwVertexNum < dwAddCount; ++dwVertexNum)
	{
		*wIndex++ = (WORD) dwCount;
		m_Vertices[dwCount].position = c_pv3Vertex[dwVertexNum];
		//const TPosition & v3Normal = c_pv3Normal[dwVertexNum];
		//float fAlpha = (DXMath::Vec3Dot(&m_v3Normal, &v3Normal) / DXMath::Vec3Length(&v3Normal) - m_cfDecalEpsilon) * fOne_over_1MinusDecalEpsilon;
		//m_Vertices[dwCount].diffuse = D3DXCOLOR(1.0f, 1.0f, 1.0f, (fAlpha > 0.0f) ? fAlpha : 0.0f);
		m_Vertices[dwCount].diffuse = 0xFFFFFFFF;
		++dwCount;
	}

	m_dwVertexCount = dwCount;
	return true;
}

DWORD CDecal::ClipPolygon(DWORD dwVertexCount, 
						 const TPosition *c_pv3Vertex, 
						 const TPosition *c_pv3Normal, 
						 TPosition *c_pv3NewVertex, 
						 TPosition *c_pv3NewNormal) const
{
	TPosition		v3TempVertex[9];
	TPosition		v3TempNormal[9];
	
	// Clip against all six planes
	DWORD dwCount = ClipPolygonAgainstPlane(m_v4LeftPlane, dwVertexCount, c_pv3Vertex, c_pv3Normal, v3TempVertex, v3TempNormal);
	if (dwCount != 0)
	{
		dwCount = ClipPolygonAgainstPlane(m_v4RightPlane, dwCount, v3TempVertex, v3TempNormal, c_pv3NewVertex, c_pv3NewNormal);
		if (dwCount != 0)
		{
			dwCount = ClipPolygonAgainstPlane(m_v4BottomPlane, dwCount, c_pv3NewVertex, c_pv3NewNormal, v3TempVertex, v3TempNormal);
			if (dwCount != 0)
			{
				dwCount = ClipPolygonAgainstPlane(m_v4TopPlane, dwCount, v3TempVertex, v3TempNormal, c_pv3NewVertex, c_pv3NewNormal);
				if (dwCount != 0)
				{
					dwCount = ClipPolygonAgainstPlane(m_v4BackPlane, dwCount, c_pv3NewVertex, c_pv3NewNormal, v3TempVertex, v3TempNormal);
					if (dwCount != 0)
					{
						dwCount = ClipPolygonAgainstPlane(m_v4FrontPlane, dwCount, v3TempVertex, v3TempNormal, c_pv3NewVertex, c_pv3NewNormal);
					}
				}
			}
		}
	}
	return dwCount;
}

DWORD CDecal::ClipPolygonAgainstPlane(const DirectX::SimpleMath::Vector4& c_rv4Plane, 
									  DWORD dwVertexCount,
									  const TPosition *c_pv3Vertex, 
									  const TPosition *c_pv3Normal, 
									  TPosition *c_pv3NewVertex, 
									  TPosition *c_pv3NewNormal)
{
	bool bNegative[10];
	
	// Classify vertices
	DWORD dwNegativeCount = 0;
	for (DWORD dwi = 0; dwi < dwVertexCount; ++dwi)
	{
		bool bNeg = (DXMath::PlaneDotCoord(&c_rv4Plane, &c_pv3Vertex[dwi]) < 0.0F);
		bNegative[dwi] = bNeg;
		dwNegativeCount += bNeg;
	}
	
	// Discard this polygon if it's completely culled
	if (dwNegativeCount == dwVertexCount)
		return 0;
	
	DWORD dwCount = 0;
	for (DWORD dwCurIndex = 0; dwCurIndex < dwVertexCount; ++dwCurIndex)
	{
		// dwPrevIndex is the index of the previous vertex
		DWORD dwPrevIndex = (dwCurIndex != 0) ? dwCurIndex - 1 : dwVertexCount - 1;
		
		if (bNegative[dwCurIndex])
		{
			if (!bNegative[dwPrevIndex])
			{
				// Current vertex is on negative side of plane,
				// but previous vertex is on positive side.
				const TPosition& v3_1 = c_pv3Vertex[dwPrevIndex];
				const TPosition& v3_2 = c_pv3Vertex[dwCurIndex];
				float ft = DXMath::PlaneDotCoord(&c_rv4Plane, &v3_1) / (c_rv4Plane.x * (v3_1.x - v3_2.x) + c_rv4Plane.y * (v3_1.y - v3_2.y) + c_rv4Plane.z * (v3_1.z - v3_2.z));
 				c_pv3NewVertex[dwCount] = v3_1 * (1.0f - ft) + v3_2 * ft;
				const TPosition& v3_n1 = c_pv3Normal[dwPrevIndex];
				const TPosition& v3_n2 = c_pv3Normal[dwCurIndex];
 				c_pv3NewNormal[dwCount] = v3_n1 * (1.0f - ft) + v3_n2 * ft;
				++dwCount;
			}
		}
		else
		{
			if (bNegative[dwPrevIndex])
			{
				// Current vertex is on positive side of plane,
				// but previous vertex is on negative side.
				const TPosition& v3_1 = c_pv3Vertex[dwCurIndex];
				const TPosition& v3_2 = c_pv3Vertex[dwPrevIndex];
				float ft = DXMath::PlaneDotCoord(&c_rv4Plane, &v3_1) / (c_rv4Plane.x * (v3_1.x - v3_2.x) + c_rv4Plane.y * (v3_1.y - v3_2.y) + c_rv4Plane.z * (v3_1.z - v3_2.z));
 				c_pv3NewVertex[dwCount] = v3_1 * (1.0f - ft) + v3_2 * ft;
				const TPosition& v3_n1 = c_pv3Normal[dwCurIndex];
				const TPosition& v3_n2 = c_pv3Normal[dwPrevIndex];
 				c_pv3NewNormal[dwCount] = v3_n1 * (1.0f - ft) + v3_n2 * ft;
				++dwCount;
			}
			
			// Include current vertex
 			c_pv3NewVertex[dwCount] = c_pv3Vertex[dwCurIndex];
 			c_pv3NewNormal[dwCount] = c_pv3Normal[dwCurIndex];
			++dwCount;
		}
	}
	
	// Return number of vertices in clipped polygon
	return dwCount;
}

/*
void CDecal::Update()
{
}
*/

void CDecal::Render()
{
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
		return;

	if (m_dwVertexCount < 3u || m_TriangleFanStructVector.empty())
		return;

	ID3D11Device* pDevice = pDX11Device->GetDevice();
	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	if (!pDevice || !pContext)
		return;

	static ID3D11VertexShader* s_pDecalVS = nullptr;
	static ID3D11PixelShader* s_pDecalPS = nullptr;
	static ID3D11InputLayout* s_pDecalInputLayout = nullptr;
	static ID3D11Buffer* s_pDecalConstantBuffer = nullptr;
	static bool s_bDecalPipelineInitAttempted = false;
	if (!s_bDecalPipelineInitAttempted)
	{
		s_bDecalPipelineInitAttempted = true;

		static const char* c_szDecalVS = R"(
cbuffer DecalCB : register(b0)
{
	row_major float4x4 gViewProj;
};
struct VSIn
{
	float3 pos : POSITION;
	float4 col : COLOR0;
};
struct VSOut
{
	float4 pos : SV_POSITION;
	float4 col : COLOR0;
};
VSOut main(VSIn i)
{
	VSOut o;
	o.pos = mul(float4(i.pos, 1.0f), gViewProj);
	o.col = i.col;
	return o;
}
)";

		static const char* c_szDecalPS = R"(
struct PSIn
{
	float4 pos : SV_POSITION;
	float4 col : COLOR0;
};
float4 main(PSIn i) : SV_TARGET
{
	return i.col;
}
)";

		ID3DBlob* pVSBlob = nullptr;
		ID3DBlob* pPSBlob = nullptr;
		if (CompileDecalShader(c_szDecalVS, "main", "vs_4_0", &pVSBlob) &&
			CompileDecalShader(c_szDecalPS, "main", "ps_4_0", &pPSBlob))
		{
			HRESULT hResult = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &s_pDecalVS);
			if (SUCCEEDED(hResult))
				hResult = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &s_pDecalPS);

			if (SUCCEEDED(hResult))
			{
				const D3D11_INPUT_ELEMENT_DESC akLayout[] =
				{
					{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SDecalDX11Vertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
					{"COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, offsetof(SDecalDX11Vertex, diffuse), D3D11_INPUT_PER_VERTEX_DATA, 0},
				};

				hResult = pDevice->CreateInputLayout(
					akLayout,
					_countof(akLayout),
					pVSBlob->GetBufferPointer(),
					pVSBlob->GetBufferSize(),
					&s_pDecalInputLayout);
			}

			if (SUCCEEDED(hResult))
			{
				D3D11_BUFFER_DESC kCBDesc = {};
				kCBDesc.ByteWidth = sizeof(SDecalDX11CB);
				kCBDesc.Usage = D3D11_USAGE_DYNAMIC;
				kCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				kCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				hResult = pDevice->CreateBuffer(&kCBDesc, nullptr, &s_pDecalConstantBuffer);
			}

			if (FAILED(hResult))
				TraceError("DX11_DECAL_PIPELINE_INIT_FAIL hr=0x%08X", static_cast<unsigned int>(hResult));
		}

		ReleaseCOM(pVSBlob);
		ReleaseCOM(pPSBlob);
	}

	if (!s_pDecalVS || !s_pDecalPS || !s_pDecalInputLayout || !s_pDecalConstantBuffer)
		return;

	std::vector<SDecalDX11Vertex> kVertices;
	kVertices.reserve(m_dwPrimitiveCount * 3u);
	for (const TTRIANGLEFANSTRUCT& rkFan : m_TriangleFanStructVector)
	{
		if (rkFan.m_dwVertexCount < 3u)
			continue;

		const DWORD dwBase = rkFan.m_dwVBOffset;
		for (DWORD i = 2u; i < rkFan.m_dwVertexCount; ++i)
		{
			const TPDTVertex& rkV0 = m_Vertices[dwBase];
			const TPDTVertex& rkV1 = m_Vertices[dwBase + i - 1u];
			const TPDTVertex& rkV2 = m_Vertices[dwBase + i];

			kVertices.push_back({rkV0.position.x, rkV0.position.y, rkV0.position.z, rkV0.diffuse});
			kVertices.push_back({rkV1.position.x, rkV1.position.y, rkV1.position.z, rkV1.diffuse});
			kVertices.push_back({rkV2.position.x, rkV2.position.y, rkV2.position.z, rkV2.diffuse});
		}
	}

	if (kVertices.empty())
		return;

	D3D11_BUFFER_DESC kVBDesc = {};
	kVBDesc.ByteWidth = static_cast<UINT>(kVertices.size() * sizeof(SDecalDX11Vertex));
	kVBDesc.Usage = D3D11_USAGE_IMMUTABLE;
	kVBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA kVBData = {};
	kVBData.pSysMem = &kVertices[0];

	ID3D11Buffer* pVertexBuffer = nullptr;
	const HRESULT hVBResult = pDevice->CreateBuffer(&kVBDesc, &kVBData, &pVertexBuffer);
	if (FAILED(hVBResult) || !pVertexBuffer)
	{
		TraceError("DX11_DECAL_VB_CREATE_FAIL hr=0x%08X vtx=%u", static_cast<unsigned int>(hVBResult), static_cast<unsigned int>(kVertices.size()));
		return;
	}

	D3D11_MAPPED_SUBRESOURCE kMappedCB = {};
	const HRESULT hMapResult = pContext->Map(s_pDecalConstantBuffer, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &kMappedCB);
	if (SUCCEEDED(hMapResult) && kMappedCB.pData)
	{
		SDecalDX11CB kCBData = {};
		kCBData.matViewProj = CGraphicBase::GetViewMatrix() * CGraphicBase::GetProjMatrix();
		memcpy(kMappedCB.pData, &kCBData, sizeof(kCBData));
		pContext->Unmap(s_pDecalConstantBuffer, 0u);
	}
	else
	{
		pVertexBuffer->Release();
		return;
	}

	ID3D11InputLayout* pOldLayout = nullptr;
	ID3D11VertexShader* pOldVS = nullptr;
	ID3D11PixelShader* pOldPS = nullptr;
	ID3D11Buffer* pOldVSConst = nullptr;
	ID3D11Buffer* pOldVB = nullptr;
	UINT uOldStride = 0u;
	UINT uOldOffset = 0u;
	D3D11_PRIMITIVE_TOPOLOGY eOldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

	pContext->IAGetInputLayout(&pOldLayout);
	pContext->IAGetPrimitiveTopology(&eOldTopology);
	pContext->IAGetVertexBuffers(0u, 1u, &pOldVB, &uOldStride, &uOldOffset);
	pContext->VSGetShader(&pOldVS, nullptr, nullptr);
	pContext->PSGetShader(&pOldPS, nullptr, nullptr);
	pContext->VSGetConstantBuffers(0u, 1u, &pOldVSConst);

	const UINT uStride = sizeof(SDecalDX11Vertex);
	const UINT uOffset = 0u;
	pContext->IASetInputLayout(s_pDecalInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->IASetVertexBuffers(0u, 1u, &pVertexBuffer, &uStride, &uOffset);
	pContext->VSSetShader(s_pDecalVS, nullptr, 0u);
	pContext->PSSetShader(s_pDecalPS, nullptr, 0u);
	pContext->VSSetConstantBuffers(0u, 1u, &s_pDecalConstantBuffer);
	pContext->Draw(static_cast<UINT>(kVertices.size()), 0u);

	pContext->VSSetConstantBuffers(0u, 1u, &pOldVSConst);
	pContext->VSSetShader(pOldVS, nullptr, 0u);
	pContext->PSSetShader(pOldPS, nullptr, 0u);
	pContext->IASetVertexBuffers(0u, 1u, &pOldVB, &uOldStride, &uOldOffset);
	pContext->IASetPrimitiveTopology(eOldTopology);
	pContext->IASetInputLayout(pOldLayout);

	ReleaseCOM(pOldLayout);
	ReleaseCOM(pOldVS);
	ReleaseCOM(pOldPS);
	ReleaseCOM(pOldVSConst);
	ReleaseCOM(pOldVB);
	pVertexBuffer->Release();
}
