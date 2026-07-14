#include "StdAfx.h"
#include "SimpleLightData.h"
#include "EterLib/GrpLightManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
	static ELightDescType ParseLightTypeToken(const std::string& c_rstToken, ELightDescType eFallback)
	{
		std::string strToken = c_rstToken;
		std::transform(strToken.begin(), strToken.end(), strToken.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if ("point" == strToken || "1" == strToken)
			return LIGHT_DESC_TYPE_POINT;
		if ("spot" == strToken || "2" == strToken)
			return LIGHT_DESC_TYPE_SPOT;
		if ("directional" == strToken || "dir" == strToken || "3" == strToken)
			return LIGHT_DESC_TYPE_DIRECTIONAL;
		return eFallback;
	}

	static float NormalizeSpotAngle(float fValue, float fFallback)
	{
		if (!std::isfinite(fValue) || fValue <= 0.0f)
			return fFallback;

		float fAngle = fValue;
		if (fAngle > DirectX::XM_2PI)
			fAngle = DirectX::XMConvertToRadians(fAngle);

		return std::max(0.001f, fAngle);
	}

	static D3DXVECTOR3 NormalizeDirection(const D3DXVECTOR3& c_rDir, const D3DXVECTOR3& c_rFallback)
	{
		D3DXVECTOR3 vDir = c_rDir;
		const float fLengthSq = D3DXVec3LengthSq(&vDir);
		if (fLengthSq <= 0.000001f || !std::isfinite(fLengthSq))
			return c_rFallback;

		D3DXVec3Normalize(&vDir, &vDir);
		return vDir;
	}
}

CDynamicPool<CLightData> CLightData::ms_kPool;

void CLightData::DestroySystem()
{
	ms_kPool.Destroy();
}

CLightData* CLightData::New()
{
	return ms_kPool.Alloc();
}

void CLightData::Delete(CLightData* pkData)
{
	pkData->Clear();
	ms_kPool.Free(pkData);
}



void CLightData::OnClear()
{
	m_fMaxRange = 300.0f;

	m_TimeEventTableRange.clear();

	m_cAmbient.r = 0.5f;
	m_cAmbient.g = 0.5f;
	m_cAmbient.b = 0.5f;
	m_cAmbient.a = 1.0f;
	m_cDiffuse.r = 0.0f;
	m_cDiffuse.g = 0.0f;
	m_cDiffuse.b = 0.0f;
	m_cDiffuse.a = 1.0f;
	m_vDirection = D3DXVECTOR3(0.0f, 0.0f, -1.0f);
	m_eLightType = LIGHT_DESC_TYPE_POINT;

	m_fDuration = 1.0f;

	m_fAttenuation0 = 0.0f;
	m_fAttenuation1 = 0.1f;
	m_fAttenuation2 = 0.0f;
	m_fFalloff = 1.0f;
	m_fTheta = DirectX::XM_PIDIV4;
	m_fPhi = DirectX::XM_PIDIV2;

	m_bLoopFlag = false;
	m_iLoopCount = 0;
}
void CLightData::GetRange(float fTime, float& rRange)
{
	if (m_TimeEventTableRange.empty())
	{
		rRange = 1.0f * m_fMaxRange;
		if (rRange<0.0f)
			rRange = 0.0f;
		return;
	}
	
	rRange = GetTimeEventBlendValue(fTime, m_TimeEventTableRange);
	rRange *= m_fMaxRange;
	if (rRange<0.0f)
		rRange = 0.0f;
	return;
}

bool CLightData::OnIsData()
{
	return true;
}

BOOL CLightData::OnLoadScript(CTextFileLoader & rTextFileLoader)
{
	if (!rTextFileLoader.GetTokenFloat("duration",&m_fDuration))
		m_fDuration = 1.0f;
	
	if (!rTextFileLoader.GetTokenBoolean("loopflag",&m_bLoopFlag))
		m_bLoopFlag = false;
	
	if (!rTextFileLoader.GetTokenInteger("loopcount",&m_iLoopCount))
		m_iLoopCount = 0;
	
	if (!rTextFileLoader.GetTokenColor("ambientcolor",&m_cAmbient))
		return FALSE;
	
	if (!rTextFileLoader.GetTokenColor("diffusecolor",&m_cDiffuse))
		return FALSE;

	if (!rTextFileLoader.GetTokenFloat("maxrange",&m_fMaxRange))
		return FALSE;

	if (!rTextFileLoader.GetTokenFloat("attenuation0",&m_fAttenuation0))
		return FALSE;

	if (!rTextFileLoader.GetTokenFloat("attenuation1",&m_fAttenuation1))
		return FALSE;

	if (!rTextFileLoader.GetTokenFloat("attenuation2",&m_fAttenuation2))
		return FALSE;

	std::string strLightTypeToken;
	if (rTextFileLoader.GetTokenString("lighttype", &strLightTypeToken))
	{
		m_eLightType = ParseLightTypeToken(strLightTypeToken, m_eLightType);
	}
	else
	{
		int iLightTypeRaw = static_cast<int>(m_eLightType);
		if (rTextFileLoader.GetTokenInteger("lighttype", &iLightTypeRaw))
			m_eLightType = ParseLightTypeToken(std::to_string(iLightTypeRaw), m_eLightType);
	}

	DirectX::SimpleMath::Vector3 vDirection;
	if (rTextFileLoader.GetTokenDirection("direction", &vDirection) ||
		rTextFileLoader.GetTokenVector3("direction", &vDirection))
	{
		m_vDirection.x = vDirection.x;
		m_vDirection.y = vDirection.y;
		m_vDirection.z = vDirection.z;
	}
	m_vDirection = NormalizeDirection(m_vDirection, D3DXVECTOR3(0.0f, 0.0f, -1.0f));

	float fSpotTheta = m_fTheta;
	if (rTextFileLoader.GetTokenFloat("theta", &fSpotTheta))
		m_fTheta = NormalizeSpotAngle(fSpotTheta, m_fTheta);

	float fSpotPhi = m_fPhi;
	if (rTextFileLoader.GetTokenFloat("phi", &fSpotPhi))
		m_fPhi = NormalizeSpotAngle(fSpotPhi, m_fPhi);

	if (m_fPhi < m_fTheta)
		m_fPhi = m_fTheta + 0.001f;

	if (!rTextFileLoader.GetTokenFloat("falloff", &m_fFalloff))
		m_fFalloff = std::max(0.0f, m_fFalloff);
	else
		m_fFalloff = std::max(0.0f, m_fFalloff);

	if (!GetTokenTimeEventFloat(rTextFileLoader,"timeeventrange",&m_TimeEventTableRange))
	{
		m_TimeEventTableRange.clear();
	}

	return true;
}

CLightData::CLightData()
{
	Clear();
}

CLightData::~CLightData()
{
}

float CLightData::GetDuration()
{
	return m_fDuration;
}
void CLightData::InitializeLight(SLightDesc& light)
{
	light.Type = m_eLightType;
	
	light.Ambient = m_cAmbient;
	light.Diffuse = m_cDiffuse;
	light.Specular = m_cDiffuse;
	light.Direction = NormalizeDirection(m_vDirection, D3DXVECTOR3(0.0f, 0.0f, -1.0f));
	light.Attenuation0 = m_fAttenuation0;
	light.Attenuation1 = m_fAttenuation1;
	light.Attenuation2 = m_fAttenuation2;
	light.Falloff = m_fFalloff;
	light.Theta = m_fTheta;
	light.Phi = m_fPhi;


	D3DXVECTOR3 position;
	GetPosition( 0.0f, position);
	light.Position = position;
	
	GetRange(0.0f, light.Range);
}
