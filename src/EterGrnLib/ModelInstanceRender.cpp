#include "StdAfx.h"



#include "Eterlib/StateManager.h"



#include "ModelInstance.h"



#include "Model.h"
#include "ThingInstance.h"
#include "EterLib/Camera.h"
#include "Eterlib/GrpDeviceDX11.h"



#include "EterLib/GrpTextureDX11.h"
#include <algorithm>
#include <d3dcompiler.h>







namespace



{



	struct DX11ObjectConstantBuffer



	{



		D3DXMATRIX matWorld;



		D3DXMATRIX matViewProj;



		D3DXVECTOR4 vLightDir;



		D3DXVECTOR4 vAmbient;



		D3DXVECTOR4 vViewPosAndSpecPower;



		D3DXVECTOR4 vSpecularColorAndEnable;



	};







	ID3D11VertexShader* g_pDX11ObjectVS = nullptr;



	ID3D11PixelShader* g_pDX11ObjectPS = nullptr;



	ID3D11InputLayout* g_pDX11ObjectInputLayout = nullptr;
	ID3D11VertexShader* g_pDX11ObjectPNT2VS = nullptr;
	ID3D11PixelShader* g_pDX11ObjectPNT2PS = nullptr;
	ID3D11InputLayout* g_pDX11ObjectPNT2InputLayout = nullptr;
	ID3D11Device* g_pDX11ObjectPNT2Device = nullptr;
	bool g_bDX11ObjectUsePNT2 = false;



	ID3D11Buffer* g_pDX11ObjectConstantBuffer = nullptr;



	ID3D11SamplerState* g_pDX11ObjectSamplerState = nullptr;

	ID3D11RasterizerState* g_pDX11ObjectRasterState = nullptr;
	ID3D11BlendState* g_pDX11ObjectOpaqueBlendState = nullptr;
	ID3D11BlendState* g_pDX11ObjectAlphaBlendState = nullptr;
	ID3D11DepthStencilState* g_pDX11ObjectDepthEnableState = nullptr;
	ID3D11DepthStencilState* g_pDX11ObjectDepthReadState = nullptr;
	ID3D11Device* g_pDX11ObjectStateDevice = nullptr;
ID3D11Device* g_pDX11ObjectResourceDevice = nullptr;



	D3DXVECTOR4 g_vDX11ObjectLightDir(0.0f, 0.0f, -1.0f, 0.0f);



	D3DXVECTOR4 g_vDX11ObjectAmbient(0.62f, 0.62f, 0.62f, 0.0f);



	D3DXMATRIX g_matDX11ObjectViewProj;

	static D3DXVECTOR3 __GetDX11ObjectViewPosition()
	{
		CCamera* pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
		if (pCurrentCamera)
		{
			const DirectX::SimpleMath::Vector3& vEye = pCurrentCamera->GetEye();
			return D3DXVECTOR3(vEye.x, vEye.y, vEye.z);
		}

		return D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	}


	template <typename T>
	static void __AssignDX11ObjectResource(T*& rpDst, T* pSrc)
	{
		if (rpDst == pSrc)
			return;

		safe_release(rpDst);
		rpDst = pSrc;
		if (rpDst)
			rpDst->AddRef();
	}

		static bool __EnsureDX11StandaloneObjectPipeline(ID3D11DeviceContext* c)
	{
		if(!c) return false;
		if(g_pDX11ObjectVS&&g_pDX11ObjectPS&&g_pDX11ObjectInputLayout&&g_pDX11ObjectConstantBuffer&&g_pDX11ObjectSamplerState) return true;
		ID3D11Device* d=nullptr; c->GetDevice(&d); if(!d) return false;
		static const char* vs=R"(
cbuffer C:register(b0){row_major float4x4 W;row_major float4x4 VP;float4 L;float4 A;float4 EyePow;float4 Spec;};
struct I{float3 p:POSITION;float3 n:NORMAL;float2 u:TEXCOORD0;};struct O{float4 p:SV_POSITION;float3 n:TEXCOORD0;float2 u:TEXCOORD1;float3 w:TEXCOORD2;};
O main(I i){O o;float4 w=mul(float4(i.p,1),W);o.p=mul(w,VP);o.n=normalize(mul(float4(i.n,0),W).xyz);o.u=i.u;o.w=w.xyz;return o;})";
		static const char* ps=R"(
Texture2D D:register(t0);Texture2D Op:register(t1);SamplerState S:register(s0);cbuffer C:register(b0){row_major float4x4 W;row_major float4x4 VP;float4 L;float4 A;float4 EyePow;float4 Spec;};
struct I{float4 p:SV_POSITION;float3 n:TEXCOORD0;float2 u:TEXCOORD1;float3 w:TEXCOORD2;};float4 main(I i):SV_TARGET{float3 n=normalize(i.n),l=normalize(-L.xyz);float nd=saturate(dot(n,l));float lit=saturate(A.x+(1-A.x)*nd);float4 b=D.Sample(S,i.u);if(A.w>.5){b.a*=Op.Sample(S,i.u).r;if(b.a<=.001)discard;}else b.a=1;float3 v=normalize(EyePow.xyz-i.w),h=normalize(l+v);float sp=pow(saturate(dot(n,h)),max(EyePow.w,1))*nd*Spec.w;b.rgb=saturate(b.rgb*lit+Spec.rgb*sp);return b;})";
		ID3DBlob *vb=nullptr,*pb=nullptr,*e=nullptr;HRESULT h=D3DCompile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_4_0",0,0,&vb,&e);
		if(FAILED(h)||!vb){TraceError("DX11_EGRN_STANDALONE_PIPELINE_FAIL stage=vs hr=0x%08X",(unsigned)h);safe_release(e);safe_release(vb);safe_release(d);return false;}safe_release(e);
		h=D3DCompile(ps,strlen(ps),nullptr,nullptr,nullptr,"main","ps_4_0",0,0,&pb,&e);if(FAILED(h)||!pb){TraceError("DX11_EGRN_STANDALONE_PIPELINE_FAIL stage=ps hr=0x%08X",(unsigned)h);safe_release(e);safe_release(vb);safe_release(pb);safe_release(d);return false;}safe_release(e);
		safe_release(g_pDX11ObjectVS);safe_release(g_pDX11ObjectPS);safe_release(g_pDX11ObjectInputLayout);safe_release(g_pDX11ObjectConstantBuffer);safe_release(g_pDX11ObjectSamplerState);safe_release(g_pDX11ObjectResourceDevice);
		h=d->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&g_pDX11ObjectVS);if(SUCCEEDED(h))h=d->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&g_pDX11ObjectPS);
		D3D11_INPUT_ELEMENT_DESC l[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
		if(SUCCEEDED(h))h=d->CreateInputLayout(l,ARRAYSIZE(l),vb->GetBufferPointer(),vb->GetBufferSize(),&g_pDX11ObjectInputLayout);D3D11_BUFFER_DESC bd={};bd.ByteWidth=sizeof(DX11ObjectConstantBuffer);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.Usage=D3D11_USAGE_DEFAULT;if(SUCCEEDED(h))h=d->CreateBuffer(&bd,nullptr,&g_pDX11ObjectConstantBuffer);
		D3D11_SAMPLER_DESC sd={};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;if(SUCCEEDED(h))h=d->CreateSamplerState(&sd,&g_pDX11ObjectSamplerState);safe_release(vb);safe_release(pb);
		if(FAILED(h)){TraceError("DX11_EGRN_STANDALONE_PIPELINE_FAIL stage=create hr=0x%08X",(unsigned)h);safe_release(g_pDX11ObjectVS);safe_release(g_pDX11ObjectPS);safe_release(g_pDX11ObjectInputLayout);safe_release(g_pDX11ObjectConstantBuffer);safe_release(g_pDX11ObjectSamplerState);safe_release(d);return false;}
		g_pDX11ObjectResourceDevice=d;TraceError("DX11_EGRN_STANDALONE_PIPELINE_READY reason=pre_world_character_render");return true;
	}
	static bool __EnsureDX11ObjectPNT2Pipeline(ID3D11DeviceContext* pContext)
	{
		if (!pContext)
			return false;

		ID3D11Device* pDevice = nullptr;
		pContext->GetDevice(&pDevice);
		if (!pDevice)
			return false;

		if (g_pDX11ObjectPNT2Device == pDevice && g_pDX11ObjectPNT2VS &&
			g_pDX11ObjectPNT2PS && g_pDX11ObjectPNT2InputLayout)
		{
			pDevice->Release();
			return true;
		}

		safe_release(g_pDX11ObjectPNT2VS);
		safe_release(g_pDX11ObjectPNT2PS);
		safe_release(g_pDX11ObjectPNT2InputLayout);
		safe_release(g_pDX11ObjectPNT2Device);

		static const char* c_szVS = R"(
cbuffer C:register(b0){row_major float4x4 W;row_major float4x4 VP;float4 L;float4 A;float4 EyePow;float4 Spec;};
struct I{float3 p:POSITION;float3 n:NORMAL;float2 u0:TEXCOORD0;float2 u1:TEXCOORD1;};
struct O{float4 p:SV_POSITION;float3 n:TEXCOORD0;float2 u0:TEXCOORD1;float3 w:TEXCOORD2;float2 u1:TEXCOORD3;};
O main(I i){O o;float4 w=mul(float4(i.p,1),W);o.p=mul(w,VP);o.n=normalize(mul(float4(i.n,0),W).xyz);o.u0=i.u0;o.u1=i.u1;o.w=w.xyz;return o;})";
		static const char* c_szPS = R"(
Texture2D D:register(t0);Texture2D Lm:register(t1);SamplerState S:register(s0);cbuffer C:register(b0){row_major float4x4 W;row_major float4x4 VP;float4 L;float4 A;float4 EyePow;float4 Spec;};
struct I{float4 p:SV_POSITION;float3 n:TEXCOORD0;float2 u0:TEXCOORD1;float3 w:TEXCOORD2;float2 u1:TEXCOORD3;};
float4 main(I i):SV_TARGET{float3 n=normalize(i.n),l=normalize(-L.xyz);float nd=saturate(dot(n,l));float lit=saturate(A.x+(1-A.x)*nd);float4 b=D.Sample(S,i.u0);float3 lm=Lm.Sample(S,i.u1).rgb;b.rgb=saturate(b.rgb*lm*2.0*lit);b.a=1;float3 v=normalize(EyePow.xyz-i.w),h=normalize(l+v);float sp=pow(saturate(dot(n,h)),max(EyePow.w,1))*nd*Spec.w;b.rgb=saturate(b.rgb+Spec.rgb*sp);return b;})";

		ID3DBlob* pVSBlob = nullptr;
		ID3DBlob* pPSBlob = nullptr;
		ID3DBlob* pError = nullptr;
		HRESULT hr = D3DCompile(c_szVS, strlen(c_szVS), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &pVSBlob, &pError);
		if (FAILED(hr) || !pVSBlob)
		{
			TraceError("DX11_EGRN_PNT2_PIPELINE_FAIL stage=vs hr=0x%08X", static_cast<unsigned>(hr));
			safe_release(pError);
			safe_release(pVSBlob);
			pDevice->Release();
			return false;
		}
		safe_release(pError);
		hr = D3DCompile(c_szPS, strlen(c_szPS), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pPSBlob, &pError);
		if (FAILED(hr) || !pPSBlob)
		{
			TraceError("DX11_EGRN_PNT2_PIPELINE_FAIL stage=ps hr=0x%08X", static_cast<unsigned>(hr));
			safe_release(pError);
			safe_release(pVSBlob);
			safe_release(pPSBlob);
			pDevice->Release();
			return false;
		}
		safe_release(pError);

		hr = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &g_pDX11ObjectPNT2VS);
		if (SUCCEEDED(hr))
			hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &g_pDX11ObjectPNT2PS);
		const D3D11_INPUT_ELEMENT_DESC akLayout[] = {
			{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
			{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
			{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
			{"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}
		};
		if (SUCCEEDED(hr))
			hr = pDevice->CreateInputLayout(akLayout, ARRAYSIZE(akLayout), pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &g_pDX11ObjectPNT2InputLayout);
		safe_release(pVSBlob);
		safe_release(pPSBlob);
		if (FAILED(hr))
		{
			TraceError("DX11_EGRN_PNT2_PIPELINE_FAIL stage=create hr=0x%08X", static_cast<unsigned>(hr));
			safe_release(g_pDX11ObjectPNT2VS);
			safe_release(g_pDX11ObjectPNT2PS);
			safe_release(g_pDX11ObjectPNT2InputLayout);
			pDevice->Release();
			return false;
		}

		g_pDX11ObjectPNT2Device = pDevice;
		return true;
	}
	static void __ReleaseDX11ObjectPassStateCache()
	{
		if (g_pDX11ObjectOpaqueBlendState)
		{
			g_pDX11ObjectOpaqueBlendState->Release();
			g_pDX11ObjectOpaqueBlendState = nullptr;
		}
		if (g_pDX11ObjectAlphaBlendState)
		{
			g_pDX11ObjectAlphaBlendState->Release();
			g_pDX11ObjectAlphaBlendState = nullptr;
		}
		if (g_pDX11ObjectDepthEnableState)
		{
			g_pDX11ObjectDepthEnableState->Release();
			g_pDX11ObjectDepthEnableState = nullptr;
		}
		if (g_pDX11ObjectDepthReadState)
		{
			g_pDX11ObjectDepthReadState->Release();
			g_pDX11ObjectDepthReadState = nullptr;
		}
		if (g_pDX11ObjectStateDevice)
		{
			g_pDX11ObjectStateDevice->Release();
			g_pDX11ObjectStateDevice = nullptr;
		}
	}

	static bool __EnsureDX11ObjectPassStateCache(ID3D11DeviceContext* pContext)
	{
		if (!pContext)
			return false;

		ID3D11Device* pDevice = nullptr;
		pContext->GetDevice(&pDevice);
		if (!pDevice)
			return false;

		if (g_pDX11ObjectStateDevice != pDevice)
		{
			__ReleaseDX11ObjectPassStateCache();
			g_pDX11ObjectStateDevice = pDevice;
			g_pDX11ObjectStateDevice->AddRef();
		}

		bool bOk = true;
		static bool s_bLoggedPassStateCreateFail = false;

		if (!g_pDX11ObjectOpaqueBlendState)
		{
			D3D11_BLEND_DESC kBlendDesc;
			ZeroMemory(&kBlendDesc, sizeof(kBlendDesc));
			kBlendDesc.RenderTarget[0].BlendEnable = FALSE;
			kBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			bOk = SUCCEEDED(pDevice->CreateBlendState(&kBlendDesc, &g_pDX11ObjectOpaqueBlendState)) && bOk;
		}

		if (!g_pDX11ObjectAlphaBlendState)
		{
			D3D11_BLEND_DESC kBlendDesc;
			ZeroMemory(&kBlendDesc, sizeof(kBlendDesc));
			kBlendDesc.RenderTarget[0].BlendEnable = TRUE;
			kBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			kBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			kBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			kBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			kBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			kBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			kBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			bOk = SUCCEEDED(pDevice->CreateBlendState(&kBlendDesc, &g_pDX11ObjectAlphaBlendState)) && bOk;
		}

		if (!g_pDX11ObjectDepthEnableState)
		{
			D3D11_DEPTH_STENCIL_DESC kDesc;
			ZeroMemory(&kDesc, sizeof(kDesc));
			kDesc.DepthEnable = TRUE;
			kDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			kDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			bOk = SUCCEEDED(pDevice->CreateDepthStencilState(&kDesc, &g_pDX11ObjectDepthEnableState)) && bOk;
		}

		if (!g_pDX11ObjectDepthReadState)
		{
			D3D11_DEPTH_STENCIL_DESC kDesc;
			ZeroMemory(&kDesc, sizeof(kDesc));
			kDesc.DepthEnable = TRUE;
			kDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			kDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			bOk = SUCCEEDED(pDevice->CreateDepthStencilState(&kDesc, &g_pDX11ObjectDepthReadState)) && bOk;
		}

		if (!bOk && !s_bLoggedPassStateCreateFail)
		{
			s_bLoggedPassStateCreateFail = true;
			TraceError("DX11_EGRN_OBJECT_PASS_STATE_FAIL opaque=%d alpha=%d depth_enable=%d depth_read=%d",
				g_pDX11ObjectOpaqueBlendState ? 1 : 0,
				g_pDX11ObjectAlphaBlendState ? 1 : 0,
				g_pDX11ObjectDepthEnableState ? 1 : 0,
				g_pDX11ObjectDepthReadState ? 1 : 0);
		}

		pDevice->Release();
		return g_pDX11ObjectOpaqueBlendState && g_pDX11ObjectAlphaBlendState && g_pDX11ObjectDepthEnableState && g_pDX11ObjectDepthReadState;
	}

	struct DX11ObjectPassStateScope
	{
		ID3D11DeviceContext* pContext;
		ID3D11BlendState* pPrevBlendState;
		float afPrevBlendFactor[4];
		UINT uPrevSampleMask;
		ID3D11DepthStencilState* pPrevDepthState;
		UINT uPrevStencilRef;
		ID3D11RasterizerState* pPrevRasterState;

		explicit DX11ObjectPassStateScope(ID3D11DeviceContext* pInContext)
			: pContext(pInContext)
			, pPrevBlendState(nullptr)
			, uPrevSampleMask(0u)
			, pPrevDepthState(nullptr)
			, uPrevStencilRef(0u)
			, pPrevRasterState(nullptr)
		{
			afPrevBlendFactor[0] = 0.0f;
			afPrevBlendFactor[1] = 0.0f;
			afPrevBlendFactor[2] = 0.0f;
			afPrevBlendFactor[3] = 0.0f;

			if (!pContext)
				return;

			pContext->OMGetBlendState(&pPrevBlendState, afPrevBlendFactor, &uPrevSampleMask);
			pContext->OMGetDepthStencilState(&pPrevDepthState, &uPrevStencilRef);
			pContext->RSGetState(&pPrevRasterState);
		}

		~DX11ObjectPassStateScope()
		{
			if (!pContext)
				return;

			pContext->OMSetBlendState(pPrevBlendState, afPrevBlendFactor, uPrevSampleMask);
			pContext->OMSetDepthStencilState(pPrevDepthState, uPrevStencilRef);
			pContext->RSSetState(pPrevRasterState);

			// M3-RS-OWNERSHIP-EGRN-75: Verify state restoration and detect conflicts
			static DWORD s_dwLastTelemetryMS = 0;
			static DWORD s_dwScopeExitCount = 0;
			static DWORD s_dwStateConflictCount = 0;
			++s_dwScopeExitCount;

			// Verify blend state was restored (throttled check)
			const DWORD dwNow = GetTickCount();
			if ((dwNow - s_dwLastTelemetryMS) >= 10000u && s_dwScopeExitCount > 0u)
			{
				ID3D11BlendState* pCheckBlend = nullptr;
				float afCheckFactor[4];
				UINT uCheckMask;
				pContext->OMGetBlendState(&pCheckBlend, afCheckFactor, &uCheckMask);

				// Detect if blend state doesn't match expected restored state
				if (pCheckBlend != pPrevBlendState)
					++s_dwStateConflictCount;

				if (pCheckBlend)
					pCheckBlend->Release();

				// Log telemetry every 10 seconds
				if (s_dwStateConflictCount > 0u)
				{
					TraceError("DX11_EGRN_STATE_CONFLICT scope_exits=%u conflicts=%u interval_ms=10000",
						s_dwScopeExitCount, s_dwStateConflictCount);
				}

				s_dwLastTelemetryMS = dwNow;
				s_dwScopeExitCount = 0u;
				s_dwStateConflictCount = 0u;
			}

			if (pPrevBlendState)
				pPrevBlendState->Release();
			if (pPrevDepthState)
				pPrevDepthState->Release();
			if (pPrevRasterState)
				pPrevRasterState->Release();
		}
	};

	static ID3D11RasterizerState* __GetOrCreateDX11ObjectRasterState(ID3D11DeviceContext* pContext)
	{
		if (g_pDX11ObjectRasterState)
			return g_pDX11ObjectRasterState;

		if (!pContext)
			return nullptr;

		ID3D11Device* pDevice = nullptr;
		pContext->GetDevice(&pDevice);
		if (!pDevice)
			return nullptr;

		D3D11_RASTERIZER_DESC kDesc;
		ZeroMemory(&kDesc, sizeof(kDesc));
		kDesc.FillMode = D3D11_FILL_SOLID;
        // DX11 parity for world/object pass: GRP_CULL_CW -> DX11 cull front (FrontCCW = FALSE).
        // IMPORTANT: changing this to CULL_NONE/CULL_BACK previously caused inside-out world regression.
        kDesc.CullMode = D3D11_CULL_FRONT;
		kDesc.FrontCounterClockwise = FALSE;
		kDesc.DepthBias = 0;
		kDesc.DepthBiasClamp = 0.0f;
		kDesc.SlopeScaledDepthBias = 0.0f;
		kDesc.DepthClipEnable = TRUE;
		kDesc.ScissorEnable = FALSE;
		kDesc.MultisampleEnable = FALSE;
		kDesc.AntialiasedLineEnable = FALSE;

		const HRESULT hr = pDevice->CreateRasterizerState(&kDesc, &g_pDX11ObjectRasterState);
		pDevice->Release();
		if (FAILED(hr))
		{
			static DWORD s_dwLastCreateFailMS = 0u;
			const DWORD dwNow = GetTickCount();
			if (0u == s_dwLastCreateFailMS || (dwNow - s_dwLastCreateFailMS) >= 2000u)
			{
				s_dwLastCreateFailMS = dwNow;
				TraceError("DX11_EGRN_OBJECT_RASTER_STATE_FAIL hr=0x%08X", static_cast<unsigned int>(hr));
			}
			return nullptr;
		}

		return g_pDX11ObjectRasterState;
	}







	static bool __BindDX11ObjectPipeline(ID3D11DeviceContext* pContext, bool bShadowPass, CGrannyMaterial::EType eMtrlType)



	{



		if (pContext && !bShadowPass)
			__EnsureDX11StandaloneObjectPipeline(pContext);

		if (!pContext || !g_pDX11ObjectVS || !g_pDX11ObjectInputLayout || !g_pDX11ObjectConstantBuffer)
		{
			CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
			const bool bWorldRendererPorted = (pDX11Device && pDX11Device->IsNativeWorldRendererPorted());

			static DWORD s_dwLastBindLogMS = 0u;
			const DWORD dwNow = GetTickCount();
			if (0u == s_dwLastBindLogMS || (dwNow - s_dwLastBindLogMS) >= 2000u)
			{
				s_dwLastBindLogMS = dwNow;
				if (bWorldRendererPorted)
				{
					TraceError("DX11_EGRN_OBJECT_BIND_FAIL reason=pipeline_unavailable vs=%p ps=%p layout=%p cb=%p",
						g_pDX11ObjectVS, g_pDX11ObjectPS, g_pDX11ObjectInputLayout, g_pDX11ObjectConstantBuffer);
				}
				else
				{
					TraceError("DX11_EGRN_OBJECT_BIND_DEFER reason=world_warmup_pipeline_pending vs=%p ps=%p layout=%p cb=%p",
						g_pDX11ObjectVS, g_pDX11ObjectPS, g_pDX11ObjectInputLayout, g_pDX11ObjectConstantBuffer);
				}
			}

			return false;
		}




		if (!__EnsureDX11ObjectPassStateCache(pContext))
			return false;

		ID3D11Device* pContextDevice = nullptr;
		pContext->GetDevice(&pContextDevice);
		if (g_pDX11ObjectResourceDevice && pContextDevice && g_pDX11ObjectResourceDevice != pContextDevice)
		{
			static DWORD s_dwLastDeviceMismatchLogMS = 0u;
			const DWORD dwNow = GetTickCount();
			if (0u == s_dwLastDeviceMismatchLogMS || (dwNow - s_dwLastDeviceMismatchLogMS) >= 2000u)
			{
				s_dwLastDeviceMismatchLogMS = dwNow;
				TraceError("DX11_EGRN_OBJECT_BIND_FAIL reason=device_mismatch cb_device=%p context_device=%p cb=%p",
					g_pDX11ObjectResourceDevice, pContextDevice, g_pDX11ObjectConstantBuffer);
			}
			safe_release(pContextDevice);
			return false;
		}
		safe_release(pContextDevice);


		// Granny opacity materials are legacy alpha-test/cutout materials. Alpha
		// blending them makes complete mob meshes translucent and disables depth
		// writes, while DX9 rendered every surviving opacity texel as opaque.
		ID3D11BlendState* pBlendState = g_pDX11ObjectOpaqueBlendState;
		ID3D11DepthStencilState* pDepthState = g_pDX11ObjectDepthEnableState;

		pContext->IASetInputLayout(g_pDX11ObjectInputLayout);
		pContext->VSSetShader(g_pDX11ObjectVS, nullptr, 0);
		pContext->PSSetShader(bShadowPass ? nullptr : g_pDX11ObjectPS, nullptr, 0);
		pContext->VSSetConstantBuffers(0, 1, &g_pDX11ObjectConstantBuffer);
		pContext->PSSetConstantBuffers(0, 1, &g_pDX11ObjectConstantBuffer);

		if (!bShadowPass && g_pDX11ObjectSamplerState)
			pContext->PSSetSamplers(0, 1, &g_pDX11ObjectSamplerState);

		pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		if (ID3D11RasterizerState* pObjectRasterState = __GetOrCreateDX11ObjectRasterState(pContext))
			pContext->RSSetState(pObjectRasterState);

		if (!pBlendState || !pDepthState)
			return false;

		const float afBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		pContext->OMSetBlendState(pBlendState, afBlendFactor, 0xFFFFFFFFu);
		pContext->OMSetDepthStencilState(pDepthState, 0u);

		return true;



	}







	static void __UpdateDX11ObjectConstantBuffer(
		ID3D11DeviceContext* pContext,
		const D3DXMATRIX& matWorld,
		const D3DXVECTOR4& vAmbient,
		const D3DXVECTOR4& vViewPosAndSpecPower,
		const D3DXVECTOR4& vSpecularColorAndEnable)



	{



		if (!pContext || !g_pDX11ObjectConstantBuffer)



			return;







		DX11ObjectConstantBuffer cb = {};



		cb.matWorld = matWorld;



		cb.matViewProj = g_matDX11ObjectViewProj;



		cb.vLightDir = g_vDX11ObjectLightDir;



		cb.vAmbient = vAmbient;



		cb.vViewPosAndSpecPower = vViewPosAndSpecPower;



		cb.vSpecularColorAndEnable = vSpecularColorAndEnable;



		pContext->UpdateSubresource(g_pDX11ObjectConstantBuffer, 0, nullptr, &cb, 0, 0);



	}







	static void __CleanupDX11ObjectPipeline(ID3D11DeviceContext* pContext)



	{



		if (!pContext)



			return;







		ID3D11ShaderResourceView* apNullSRV[2] = { nullptr, nullptr };



		pContext->PSSetShaderResources(0, 2, apNullSRV);



		ID3D11SamplerState* pNullSampler = nullptr;



		pContext->PSSetSamplers(0, 1, &pNullSampler);



		ID3D11Buffer* pNullCB = nullptr;



		pContext->VSSetConstantBuffers(0, 1, &pNullCB);



		pContext->PSSetConstantBuffers(0, 1, &pNullCB);



		pContext->VSSetShader(nullptr, nullptr, 0);



		pContext->PSSetShader(nullptr, nullptr, 0);



		pContext->IASetInputLayout(nullptr);



	}







	static bool __UseOpacityPass(CGrannyMaterial::EType eMtrlType)



	{



		return eMtrlType == CGrannyMaterial::TYPE_BLEND_PNT;



	}

	static const char* __DX11MeshTypeName(CGrannyMesh::EType eMeshType)
	{
		return (eMeshType == CGrannyMesh::TYPE_DEFORM) ? "deform" : "rigid";
	}

	static const char* __DX11MaterialTypeName(CGrannyMaterial::EType eMtrlType)
	{
		return (eMtrlType == CGrannyMaterial::TYPE_BLEND_PNT) ? "blend_pnt" : "diffuse_pnt";
	}

	static void __LogDX11MeshVBMissing(
		const char* c_szPass,
		const void* pModel,
		CGrannyMesh::EType eMeshType,
		CGrannyMaterial::EType eMtrlType,
		bool bReady,
		ID3D11Buffer* pIndexBuffer,
		ID3D11Buffer* pRigidVB,
		ID3D11Buffer* pDeformVB)
	{
		static DWORD s_dwLastLogMS = 0u;
		static DWORD s_dwSkipCount = 0u;
		++s_dwSkipCount;

		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastLogMS || (dwNow - s_dwLastLogMS) >= 3000u)
		{
			s_dwLastLogMS = dwNow;
			TraceError(
				"DX11_EGRN_MESH_VB_MISSING pass=%s count=%u mesh=%s material=%s ready=%d model=%p idx=%p rigid=%p deform=%p",
				c_szPass ? c_szPass : "unknown",
				s_dwSkipCount,
				__DX11MeshTypeName(eMeshType),
				__DX11MaterialTypeName(eMtrlType),
				bReady ? 1 : 0,
				pModel,
				pIndexBuffer,
				pRigidVB,
				pDeformVB);
			s_dwSkipCount = 0u;
		}
	}



}











#ifdef _TEST







#include "Eterlib/GrpScreen.h"







void Granny_RenderBoxBones(const granny_skeleton* pkGrnSkeleton, const granny_world_pose* pkGrnWorldPose, const D3DXMATRIX& matBase)



{



	D3DXMATRIX matWorld;



	CScreen screen;	



	for (int iBone = 0; iBone != pkGrnSkeleton->BoneCount; ++iBone)



	{



		const granny_bone& rkGrnBone = pkGrnSkeleton->Bones[iBone];				



		const D3DXMATRIX* c_matBone=(const D3DXMATRIX*)GrannyGetWorldPose4x4(pkGrnWorldPose, iBone);



		



		D3DXMatrixMultiply(&matWorld, c_matBone, &matBase);



		



		STATEMANAGER.SetTransform(GRP_TS_WORLD, &matWorld);



		screen.RenderBox3d(-5.0f, -5.0f, -5.0f, 5.0f, 5.0f, 5.0f);



	}



}







#endif











void CGrannyModelInstance::DeformNoSkin(const D3DXMATRIX * c_pWorldMatrix)



{



	if (IsEmpty())



		return;







	// DELETED



	//m_pgrnWorldPose = m_pgrnWorldPoseReal;



	///////////////////////////////



	



	UpdateWorldPose();



	UpdateWorldMatrices(c_pWorldMatrix);



}







///////////////////////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////////////////////////



//// Render



///////////////////////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////////////////////////



// With One Texture



void CGrannyModelInstance::RenderWithOneTexture()



{



	if (IsEmpty())



		return;







	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();



	if (!pDX11Device || !pDX11Device->IsValid() || !pDX11Device->GetContext())



		return;







	const D3DXMATRIX& matView = CGraphicBase::GetViewMatrix();



	const D3DXMATRIX& matProj = CGraphicBase::GetProjMatrix();



	const D3DXMATRIX matViewProj = matView * matProj;



	RenderWithOneTextureDX11(pDX11Device->GetContext(), matViewProj);



}







void CGrannyModelInstance::BlendRenderWithOneTexture()



{



	RenderWithOneTexture();



}







// With Two Texture



void CGrannyModelInstance::RenderWithTwoTexture()



{



	if (IsEmpty())



		return;







	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();



	if (!pDX11Device || !pDX11Device->IsValid() || !pDX11Device->GetContext())



		return;







	const D3DXMATRIX& matView = CGraphicBase::GetViewMatrix();



	const D3DXMATRIX& matProj = CGraphicBase::GetProjMatrix();



	const D3DXMATRIX matViewProj = matView * matProj;



	RenderWithTwoTextureDX11(pDX11Device->GetContext(), matViewProj);



}







void CGrannyModelInstance::BlendRenderWithTwoTexture()



{



	RenderWithTwoTexture();



}







void CGrannyModelInstance::RenderWithoutTexture()



{



	if (IsEmpty())



		return;







	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();



	if (!pDX11Device || !pDX11Device->IsValid() || !pDX11Device->GetContext())



		return;







	const D3DXMATRIX& matView = CGraphicBase::GetViewMatrix();



	const D3DXMATRIX& matProj = CGraphicBase::GetProjMatrix();



	const D3DXMATRIX matViewProj = matView * matProj;



	RenderToShadowMapDX11(pDX11Device->GetContext(), matViewProj);



}







///////////////////////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////////////////////////



//// Render Mesh List



///////////////////////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////////////////////////







// With One Texture



void CGrannyModelInstance::RenderMeshNodeListWithOneTexture(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType)



{



	assert(m_pModel != NULL);







	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();



	ID3D11DeviceContext* pContext = pDX11Device ? pDX11Device->GetContext() : nullptr;



	if (!pDX11Device || !pDX11Device->IsValid() || !pContext || !m_pDX11IndexBuffer)



		return;






	// M3-RS-OWNERSHIP-EGRN-75: Scoped state management to prevent leakage
	DX11ObjectPassStateScope stateScope(pContext);

	if (!__BindDX11ObjectPipeline(pContext, false, eMtrlType))
	{
		return;
	}







	const CGrannyModel::TMeshNode* pMeshNode = m_pModel->GetMeshNodeList(eMeshType, eMtrlType);
	if (!pMeshNode)
	{
		__CleanupDX11ObjectPipeline(pContext);
		return;
	}

	ID3D11Buffer* pVB = (eMeshType == CGrannyMesh::TYPE_DEFORM) ? m_pDX11DeformableVertexBuffer : m_pDX11RigidVertexBuffer;
	if (!pVB && pDX11Device && pDX11Device->IsValid())
	{
		CreateDX11VertexBuffers(pDX11Device->GetDevice());
		if (eMeshType == CGrannyMesh::TYPE_DEFORM && m_pDX11DeformableVertexBuffer)
			UpdateDX11DeformableVertexBuffer(pContext);
		pVB = (eMeshType == CGrannyMesh::TYPE_DEFORM) ? m_pDX11DeformableVertexBuffer : m_pDX11RigidVertexBuffer;
	}



	if (!pVB)



	{



		__LogDX11MeshVBMissing(
			"one_texture",
			m_pModel,
			eMeshType,
			eMtrlType,
			m_bDX11VertexBuffersReady,
			m_pDX11IndexBuffer,
			m_pDX11RigidVertexBuffer,
			m_pDX11DeformableVertexBuffer);
		__CleanupDX11ObjectPipeline(pContext);



		return;



	}







	const UINT uVertexStride = (eMeshType == CGrannyMesh::TYPE_DEFORM)
		? static_cast<UINT>(sizeof(TPNTVertex))
		: std::max<UINT>(static_cast<UINT>(sizeof(TPNTVertex)), m_pModel->GetRigidVertexStride());
	const bool bUsePNT2Pipeline = g_bDX11ObjectUsePNT2 &&
		eMeshType == CGrannyMesh::TYPE_RIGID &&
		eMtrlType == CGrannyMaterial::TYPE_BLEND_PNT &&
		uVertexStride >= static_cast<UINT>(sizeof(TPNT2Vertex)) &&
		__EnsureDX11ObjectPNT2Pipeline(pContext);

	STATEMANAGER.SetInputLayout(bUsePNT2Pipeline ? g_pDX11ObjectPNT2InputLayout : g_pDX11ObjectInputLayout);
	STATEMANAGER.SetVertexShader(bUsePNT2Pipeline ? g_pDX11ObjectPNT2VS : g_pDX11ObjectVS);
	STATEMANAGER.SetPixelShader(bUsePNT2Pipeline ? g_pDX11ObjectPNT2PS : g_pDX11ObjectPS);
	STATEMANAGER.SetStreamSource(0, pVB, uVertexStride);



	STATEMANAGER.SetIndices(m_pDX11IndexBuffer, DXGI_FORMAT_R16_UINT, 0);







	const D3DXVECTOR3 vViewPos = __GetDX11ObjectViewPosition();
	const D3DXVECTOR4 vViewPosAndSpecPower(vViewPos.x, vViewPos.y, vViewPos.z, 1.0f);
	const D3DXVECTOR4 vSpecularColorAndEnable(0.0f, 0.0f, 0.0f, 0.0f);



	const bool bUseOpacityPass = __UseOpacityPass(eMtrlType);



	const ID3D11ShaderResourceView* pWhiteFallbackSRV = CGraphicTextureDX11::GetWhiteFallbackTexture(pDX11Device->GetDevice());







	while (pMeshNode)



	{



		const CGrannyMesh* pMesh = pMeshNode->pMesh;



		const int vtxMeshBasePos = pMesh->GetVertexBasePosition();



		const CGrannyMesh::TTriGroupNode* pTriGroupNode = pMesh->GetTriGroupNodeList(eMtrlType);



		const int vtxCount = pMesh->GetVertexCount();







		while (pTriGroupNode)



		{



			ms_faceCount += pTriGroupNode->triCount;







			CGrannyMaterial& rkMtrl = m_kMtrlPal.GetMaterialRef(pTriGroupNode->mtrlIndex);



			if (!material_data_.pImage)



			{



				if (std::fabs(rkMtrl.GetSpecularPower() - material_data_.fSpecularPower) >= std::numeric_limits<float>::epsilon())



					rkMtrl.SetSpecularInfo(material_data_.isSpecularEnable, material_data_.fSpecularPower, material_data_.bSphereMapIndex);



			}







			rkMtrl.ApplyRenderState();



			const CGraphicTexture* pDiffuseTex = rkMtrl.GetDiffuseTexture();



			const CGraphicTexture* pOpacityTex = rkMtrl.GetOpacityTexture();



			ID3D11ShaderResourceView* pDiffuseSRV = pDiffuseTex ? pDiffuseTex->GetD3D11TextureSRV() : const_cast<ID3D11ShaderResourceView*>(pWhiteFallbackSRV);



			ID3D11ShaderResourceView* pOpacitySRV = nullptr;



			if (bUseOpacityPass)



				pOpacitySRV = pOpacityTex ? pOpacityTex->GetD3D11TextureSRV() : const_cast<ID3D11ShaderResourceView*>(pWhiteFallbackSRV);







			STATEMANAGER.SetTexture(0, pDiffuseSRV);



			STATEMANAGER.SetTexture(1, pOpacitySRV);







			D3DXVECTOR4 vAmbient = g_vDX11ObjectAmbient;



			vAmbient.w = bUseOpacityPass ? 1.0f : 0.0f;

			float fSpecularStrength = 0.0f;
			float fSpecularExponent = 1.0f;
			if (rkMtrl.IsSpecularEnabled())
			{
				const float fMaterialSpecularPower = std::max(0.0f, rkMtrl.GetSpecularPower());
				if (fMaterialSpecularPower > 0.0f)
				{
					fSpecularStrength = std::min(1.0f, fMaterialSpecularPower);
					fSpecularExponent = std::max(1.0f, fMaterialSpecularPower * 64.0f);
				}
			}

			const D3DXVECTOR4 vViewPosAndSpecPower(vViewPos.x, vViewPos.y, vViewPos.z, fSpecularExponent);
			const D3DXVECTOR4 vSpecularColorAndEnable(
				fSpecularStrength,
				fSpecularStrength,
				fSpecularStrength,
				(fSpecularStrength > 0.0f) ? 1.0f : 0.0f);
			__UpdateDX11ObjectConstantBuffer(
				pContext,
				m_meshMatrices[pMeshNode->iMesh],
				vAmbient,
				vViewPosAndSpecPower,
				vSpecularColorAndEnable);







			STATEMANAGER.DrawIndexedPrimitive(GRP_PT_TRIANGLELIST, vtxMeshBasePos, 0, vtxCount, pTriGroupNode->idxPos, pTriGroupNode->triCount);



			CGraphicThingInstance::AddDX11SubmittedDrawCount(1);



			rkMtrl.RestoreRenderState();



			pTriGroupNode = pTriGroupNode->pNextTriGroupNode;



		}







		pMeshNode = pMeshNode->pNextMeshNode;



	}







	__CleanupDX11ObjectPipeline(pContext);



}







// With Two Texture



void CGrannyModelInstance::RenderMeshNodeListWithTwoTexture(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType)
{
	RenderMeshNodeListWithOneTexture(eMeshType, eMtrlType);
}







// Without Texture



void CGrannyModelInstance::RenderMeshNodeListWithoutTexture(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType)



{



	assert(m_pModel != NULL);







	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();



	ID3D11DeviceContext* pContext = pDX11Device ? pDX11Device->GetContext() : nullptr;



	if (!pDX11Device || !pDX11Device->IsValid() || !pContext || !m_pDX11IndexBuffer)



		return;



	// M3-RS-OWNERSHIP-EGRN-75: Scoped state management to prevent leakage
	DX11ObjectPassStateScope stateScope(pContext);




	if (!__BindDX11ObjectPipeline(pContext, true, eMtrlType))



		return;







	const CGrannyModel::TMeshNode* pMeshNode = m_pModel->GetMeshNodeList(eMeshType, eMtrlType);
	if (!pMeshNode)
	{
		__CleanupDX11ObjectPipeline(pContext);
		return;
	}

	ID3D11Buffer* pVB = (eMeshType == CGrannyMesh::TYPE_DEFORM) ? m_pDX11DeformableVertexBuffer : m_pDX11RigidVertexBuffer;
	if (!pVB && pDX11Device && pDX11Device->IsValid())
	{
		CreateDX11VertexBuffers(pDX11Device->GetDevice());
		if (eMeshType == CGrannyMesh::TYPE_DEFORM && m_pDX11DeformableVertexBuffer)
			UpdateDX11DeformableVertexBuffer(pContext);
		pVB = (eMeshType == CGrannyMesh::TYPE_DEFORM) ? m_pDX11DeformableVertexBuffer : m_pDX11RigidVertexBuffer;
	}



	if (!pVB)



	{



		__LogDX11MeshVBMissing(
			"shadow_without_texture",
			m_pModel,
			eMeshType,
			eMtrlType,
			m_bDX11VertexBuffersReady,
			m_pDX11IndexBuffer,
			m_pDX11RigidVertexBuffer,
			m_pDX11DeformableVertexBuffer);
		__CleanupDX11ObjectPipeline(pContext);



		return;



	}







	STATEMANAGER.SetInputLayout(g_pDX11ObjectInputLayout);



	STATEMANAGER.SetVertexShader(g_pDX11ObjectVS);



	STATEMANAGER.SetPixelShader(nullptr);



	const UINT uVertexStride = (eMeshType == CGrannyMesh::TYPE_DEFORM)
		? static_cast<UINT>(sizeof(TPNTVertex))
		: std::max<UINT>(static_cast<UINT>(sizeof(TPNTVertex)), m_pModel->GetRigidVertexStride());
	STATEMANAGER.SetStreamSource(0, pVB, uVertexStride);



	STATEMANAGER.SetIndices(m_pDX11IndexBuffer, DXGI_FORMAT_R16_UINT, 0);







	const D3DXVECTOR3 vViewPos = __GetDX11ObjectViewPosition();
	const D3DXVECTOR4 vViewPosAndSpecPower(vViewPos.x, vViewPos.y, vViewPos.z, 1.0f);
	const D3DXVECTOR4 vSpecularColorAndEnable(0.0f, 0.0f, 0.0f, 0.0f);



	while (pMeshNode)



	{



		const CGrannyMesh* pMesh = pMeshNode->pMesh;



		const int vtxMeshBasePos = pMesh->GetVertexBasePosition();



		const CGrannyMesh::TTriGroupNode* pTriGroupNode = pMesh->GetTriGroupNodeList(eMtrlType);



		const int vtxCount = pMesh->GetVertexCount();







		while (pTriGroupNode)



		{



			ms_faceCount += pTriGroupNode->triCount;



			__UpdateDX11ObjectConstantBuffer(
				pContext,
				m_meshMatrices[pMeshNode->iMesh],
				g_vDX11ObjectAmbient,
				vViewPosAndSpecPower,
				vSpecularColorAndEnable);



			STATEMANAGER.DrawIndexedPrimitive(GRP_PT_TRIANGLELIST, vtxMeshBasePos, 0, vtxCount, pTriGroupNode->idxPos, pTriGroupNode->triCount);



			CGraphicThingInstance::AddDX11SubmittedDrawCount(1);



			pTriGroupNode = pTriGroupNode->pNextTriGroupNode;



		}







		pMeshNode = pMeshNode->pNextMeshNode;



	}







	__CleanupDX11ObjectPipeline(pContext);



}







void CGrannyModelInstance::SetDX11ObjectShaders(



	ID3D11VertexShader* pVS,



	ID3D11PixelShader* pPS,



	ID3D11InputLayout* pInputLayout,



	ID3D11Buffer* pConstantBuffer,



	ID3D11SamplerState* pSampler,



	const D3DXVECTOR4& vLightDir,



	const D3DXVECTOR4& vAmbient)



{



	__AssignDX11ObjectResource(g_pDX11ObjectVS, pVS);
	__AssignDX11ObjectResource(g_pDX11ObjectPS, pPS);
	__AssignDX11ObjectResource(g_pDX11ObjectInputLayout, pInputLayout);
	__AssignDX11ObjectResource(g_pDX11ObjectConstantBuffer, pConstantBuffer);
	__AssignDX11ObjectResource(g_pDX11ObjectSamplerState, pSampler);

	safe_release(g_pDX11ObjectResourceDevice);
	if (g_pDX11ObjectConstantBuffer)
		g_pDX11ObjectConstantBuffer->GetDevice(&g_pDX11ObjectResourceDevice);

	g_vDX11ObjectLightDir = vLightDir;
	g_vDX11ObjectAmbient = vAmbient;







	static bool s_bLoggedObjectShaderSetup = false;



	if (!s_bLoggedObjectShaderSetup)



	{



		s_bLoggedObjectShaderSetup = true;



		TraceError("DX11_EGRN_OBJECT_SHADER_SETUP vs=%p ps=%p layout=%p cb=%p sampler=%p",



			pVS, pPS, pInputLayout, pConstantBuffer, pSampler);



	}



}







void CGrannyModelInstance::RenderWithOneTextureDX11(ID3D11DeviceContext* pContext, const D3DXMATRIX& matViewProj)



{



	if (!pContext || IsEmpty())
		return;

	DX11ObjectPassStateScope kPassStateScope(pContext);

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if ((!m_bDX11VertexBuffersReady || !m_pDX11IndexBuffer) && pDX11Device && pDX11Device->IsValid())
	{
		CreateDX11VertexBuffers(pDX11Device->GetDevice());
		static DWORD s_dwLastLazyInitLogMS = 0u;
		const DWORD dwNow = GetTickCount();
		if (0u == s_dwLastLazyInitLogMS || (dwNow - s_dwLastLazyInitLogMS) >= 2000u)
		{
			s_dwLastLazyInitLogMS = dwNow;
			TraceError("DX11_EGRN_LAZY_BUFFER_INIT ready=%d idx=%p rigid=%p deform=%p",
				m_bDX11VertexBuffersReady ? 1 : 0,
				m_pDX11IndexBuffer,
				m_pDX11RigidVertexBuffer,
				m_pDX11DeformableVertexBuffer);
		}
	}

	if (!m_pDX11IndexBuffer)
	{
		static bool s_bLoggedMissingIndex = false;
		if (!s_bLoggedMissingIndex)
		{
			s_bLoggedMissingIndex = true;
			TraceError("DX11_EGRN_RENDER_SKIP reason=missing_index_buffer model=%p", m_pModel);
		}
		return;
	}

	if (m_pModel->CanDeformPNTVertices() && m_pDX11DeformableVertexBuffer)
		UpdateDX11DeformableVertexBuffer(pContext);







	g_matDX11ObjectViewProj = matViewProj;



	RenderMeshNodeListWithOneTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_DIFFUSE_PNT);



	RenderMeshNodeListWithOneTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_BLEND_PNT);



	RenderMeshNodeListWithOneTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_DIFFUSE_PNT);



	RenderMeshNodeListWithOneTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_BLEND_PNT);

}







void CGrannyModelInstance::RenderWithTwoTextureDX11(ID3D11DeviceContext* pContext, const D3DXMATRIX& matViewProj)
{
	const bool bPreviousUsePNT2 = g_bDX11ObjectUsePNT2;
	g_bDX11ObjectUsePNT2 = true;
	RenderWithOneTextureDX11(pContext, matViewProj);
	g_bDX11ObjectUsePNT2 = bPreviousUsePNT2;
}







void CGrannyModelInstance::RenderToShadowMapDX11(ID3D11DeviceContext* pContext, const D3DXMATRIX& c_rmatLightViewProj)



{



	if (!pContext || IsEmpty())
		return;

	DX11ObjectPassStateScope kPassStateScope(pContext);

	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if ((!m_bDX11VertexBuffersReady || !m_pDX11IndexBuffer) && pDX11Device && pDX11Device->IsValid())
		CreateDX11VertexBuffers(pDX11Device->GetDevice());

	if (!m_pDX11IndexBuffer)
		return;

	if (m_pModel->CanDeformPNTVertices() && m_pDX11DeformableVertexBuffer)
		UpdateDX11DeformableVertexBuffer(pContext);







	g_matDX11ObjectViewProj = c_rmatLightViewProj;



	RenderMeshNodeListWithoutTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_DIFFUSE_PNT);



	RenderMeshNodeListWithoutTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_BLEND_PNT);



	RenderMeshNodeListWithoutTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_DIFFUSE_PNT);



	RenderMeshNodeListWithoutTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_BLEND_PNT);



}







UINT CGrannyModelInstance::__RenderMeshNodeListDX11(ID3D11DeviceContext* pContext, const D3DXMATRIX& matViewProj, CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType)

{

	if (!pContext || IsEmpty())

		return 0u;



	g_matDX11ObjectViewProj = matViewProj;

	const DWORD dwBefore = CGraphicThingInstance::GetDX11SubmittedDrawCount();

	RenderMeshNodeListWithOneTexture(eMeshType, eMtrlType);

	const DWORD dwAfter = CGraphicThingInstance::GetDX11SubmittedDrawCount();

	return (dwAfter >= dwBefore) ? (dwAfter - dwBefore) : 0u;

}



void CGrannyModelInstance::__RenderMeshNodeListToShadowMapDX11(ID3D11DeviceContext* pContext, const D3DXMATRIX& c_rmatLightViewProj, CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType)

{

	if (!pContext || IsEmpty())
		return;

	DX11ObjectPassStateScope kPassStateScope(pContext);



	g_matDX11ObjectViewProj = c_rmatLightViewProj;

	RenderMeshNodeListWithoutTexture(eMeshType, eMtrlType);

}


