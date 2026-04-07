// SPDX-License-Identifier: MIT
// ImGuiEnvironmentControls.cpp - Environment/Sky/Sun Debug Controls Implementation
// M3-SKY-BLEND-FIX-74: Real-time environment parameter tweaking

#include "ImGuiEnvironmentControls.h"

// ImGui
#include "../vendor/imgui/imgui.h"

// Game includes
#include "../GameLib/MapManager.h"
#include "../GameLib/MapOutdoor.h"
#include "../GameLib/MapType.h"
#include "../UserInterface/PythonBackground.h"  // M3-SKY-BLEND-FIX-74: For CPythonBackground singleton
#include "../EterLib/SkyBox.h"
#include "../EterLib/LensFlare.h"
#include "../EterBase/Timer.h"
#include <algorithm>

// M3-SKY-BLEND-FIX-74: Singleton instance
CImGuiEnvironmentControls* CImGuiEnvironmentControls::ms_pInstance = nullptr;

namespace
{
    inline TColor MakeColor(const D3DXCOLOR& c)
    {
        return TColor(c.r, c.g, c.b, c.a);
    }

    inline TGradientColor MakeGradientColor(const D3DXCOLOR& first, const D3DXCOLOR& second)
    {
        TGradientColor grad = {};
        grad.m_FirstColor = MakeColor(first);
        grad.m_SecondColor = MakeColor(second);
        return grad;
    }

    inline bool NearlyEqualFloat(float a, float b, float eps = 0.0001f)
    {
        const float d = a - b;
        const float ad = (d >= 0.0f) ? d : -d;
        return ad <= eps;
    }

    inline bool NearlyEqualColor(const D3DXCOLOR& a, const D3DXCOLOR& b, float eps = 0.0001f)
    {
        return NearlyEqualFloat(a.r, b.r, eps) &&
               NearlyEqualFloat(a.g, b.g, eps) &&
               NearlyEqualFloat(a.b, b.b, eps) &&
               NearlyEqualFloat(a.a, b.a, eps);
    }

    inline bool NearlyEqualColor(const TColor& a, const TColor& b, float eps = 0.0001f)
    {
        return NearlyEqualFloat(a.r, b.r, eps) &&
               NearlyEqualFloat(a.g, b.g, eps) &&
               NearlyEqualFloat(a.b, b.b, eps) &&
               NearlyEqualFloat(a.a, b.a, eps);
    }

    inline bool NearlyEqualVec2(const D3DXVECTOR2& a, const D3DXVECTOR2& b, float eps = 0.0001f)
    {
        return NearlyEqualFloat(a.x, b.x, eps) && NearlyEqualFloat(a.y, b.y, eps);
    }

    inline bool NearlyEqualVec3(const D3DXVECTOR3& a, const D3DXVECTOR3& b, float eps = 0.0001f)
    {
        return NearlyEqualFloat(a.x, b.x, eps) && NearlyEqualFloat(a.y, b.y, eps) && NearlyEqualFloat(a.z, b.z, eps);
    }

    inline bool NearlyEqualGradient(const TGradientColor& a, const TGradientColor& b, float eps = 0.0001f)
    {
        return NearlyEqualColor(a.m_FirstColor, b.m_FirstColor, eps) && NearlyEqualColor(a.m_SecondColor, b.m_SecondColor, eps);
    }
}


//====================================================================================
// Singleton Management
//====================================================================================

CImGuiEnvironmentControls* CImGuiEnvironmentControls::Instance()
{
	return ms_pInstance;
}

bool CImGuiEnvironmentControls::Create()
{
	if (ms_pInstance)
		return false;

	ms_pInstance = new CImGuiEnvironmentControls();
	if (!ms_pInstance->Initialize())
	{
		delete ms_pInstance;
		ms_pInstance = nullptr;
		return false;
	}

	return true;
}

void CImGuiEnvironmentControls::Destroy()
{
	if (!ms_pInstance)
		return;

	ms_pInstance->Shutdown();
	delete ms_pInstance;
	ms_pInstance = nullptr;
}

//====================================================================================
// Constructor / Destructor
//====================================================================================

CImGuiEnvironmentControls::CImGuiEnvironmentControls()
	: m_bEnabled(true)  // M3-SKY-BLEND-FIX-74: Enable by default
	, m_bInitialized(false)
	, m_bDirty(false)
	, m_iCurrentPresetIndex(-1)
	, m_bShowSunPosition(false)
	, m_bShowFogVisualization(false)
	, m_dwPresetSwitchCount(0)
	, m_dwParameterChangeCount(0)
	, m_dwLastRefreshTime(0)
	, m_pLastEnvironmentData(nullptr)
	, m_bUserHasModified(false)
	, m_bPresetApplied(false)  // M3-SKY-PRESET-PERSIST-74: Initialize preset tracking
	, m_eSkyRenderPolicyOverride(POLICY_AUTO_FROM_MSENV)  // M3-SKY-BLEND-FIX-74: Default to auto mode
{
}

CImGuiEnvironmentControls::~CImGuiEnvironmentControls()
{
	Shutdown();
}

//====================================================================================
// Initialization / Shutdown
//====================================================================================

bool CImGuiEnvironmentControls::Initialize()
{
	if (m_bInitialized)
		return true;

	// Initialize presets
	InitializePresets();

	m_bInitialized = true;
	return true;
}

void CImGuiEnvironmentControls::Shutdown()
{
	if (!m_bInitialized)
		return;

	m_bInitialized = false;
}

//====================================================================================
// Preset System
//====================================================================================

void CImGuiEnvironmentControls::InitializePresets()
{
	CreateDayPreset();
	CreateNightPreset();
	CreateSunsetPreset();
	CreateOvercastPreset();
}

void CImGuiEnvironmentControls::CreateDayPreset()
{
	SEnvironmentPreset& preset = m_aPresets[0];
	preset.strName = "Day";
	preset.strDescription = "Bright clear day with full sun";

	// Skybox
	preset.v3SkyBoxScale = D3DXVECTOR3(29000.0f, 29000.0f, 29000.0f);  // M3-SKY-SCALE-PERSIST: Optimal default scale
	// FIX: Match gradient levels to actual color count (5 colors = 2 upper + 3 lower)
	// This prevents NormalizeGradientVector from interpolating colors incorrectly
	preset.bySkyBoxGradientLevelUpper = 2;  // Was 8
	preset.bySkyBoxGradientLevelLower = 3;  // Was 8
	preset.bSkyBoxTextureRenderMode = TRUE;
    preset.CloudGradientColor = MakeGradientColor(D3DXCOLOR(0.90f, 0.95f, 1.00f, 1.0f), D3DXCOLOR(0.65f, 0.80f, 1.00f, 1.0f));
    preset.SkyBoxGradientColorVector =
    {
        MakeGradientColor(D3DXCOLOR(0.18f, 0.45f, 0.95f, 1.0f), D3DXCOLOR(0.22f, 0.50f, 0.98f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.20f, 0.52f, 0.98f, 1.0f), D3DXCOLOR(0.28f, 0.58f, 1.00f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.32f, 0.64f, 1.00f, 1.0f), D3DXCOLOR(0.45f, 0.72f, 1.00f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.55f, 0.78f, 1.00f, 1.0f), D3DXCOLOR(0.72f, 0.86f, 1.00f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.80f, 0.90f, 1.00f, 1.0f), D3DXCOLOR(0.92f, 0.96f, 1.00f, 1.0f))
    };

	// Clouds
	preset.v2CloudScale = D3DXVECTOR2(200.0f, 200.0f);
	preset.fCloudHeight = 2000.0f;
	preset.v2CloudTextureScale = D3DXVECTOR2(2.0f, 2.0f);
	preset.v2CloudSpeed = D3DXVECTOR2(0.005f, 0.005f);

	// Fog
	preset.bFogEnable = TRUE;
	preset.bDensityFog = FALSE;
	preset.fFogNearDistance = 5000.0f;
	preset.fFogFarDistance = 15000.0f;
	preset.FogColor = D3DXCOLOR(0.7f, 0.8f, 0.9f, 1.0f);  // Light blue
	preset.bFogLevel = 3;  // Light fog

	// Sun
	preset.bLensFlareEnable = TRUE;
	preset.LensFlareBrightnessColor = D3DXCOLOR(1.0f, 0.95f, 0.8f, 1.0f);  // Warm yellow
	preset.fLensFlareMaxBrightness = 1.0f;
	preset.bMainFlareEnable = TRUE;
	preset.fMainFlareSize = 0.5f;

	// Lighting
	preset.bDirLightBackground = TRUE;
	preset.bDirLightCharacter = TRUE;
	preset.v3DirLightBackgroundDirection = D3DXVECTOR3(-0.25f, -0.55f, -0.79f);
	preset.kDirLightBackgroundAmbient = D3DXCOLOR(0.36f, 0.38f, 0.42f, 1.0f);
	preset.kDirLightBackgroundDiffuse = D3DXCOLOR(0.92f, 0.90f, 0.84f, 1.0f);
	preset.v3DirLightCharacterDirection = D3DXVECTOR3(-0.20f, -0.48f, -0.85f);
	preset.kDirLightCharacterAmbient = D3DXCOLOR(0.30f, 0.32f, 0.36f, 1.0f);
	preset.kDirLightCharacterDiffuse = D3DXCOLOR(0.95f, 0.92f, 0.86f, 1.0f);

	// Screen Filter
	preset.bFilteringEnable = FALSE;
	preset.FilteringColor = D3DXCOLOR(1.0f, 1.0f, 1.0f, 0.0f);

	// Wind
	preset.fWindStrength = 0.3f;
	preset.fWindRandom = 0.2f;
}

void CImGuiEnvironmentControls::CreateNightPreset()
{
	SEnvironmentPreset& preset = m_aPresets[1];
	preset.strName = "Night";
	preset.strDescription = "Dark night with no sun, heavy fog";

	// Skybox
	preset.v3SkyBoxScale = D3DXVECTOR3(29000.0f, 29000.0f, 29000.0f);  // M3-SKY-SCALE-PERSIST: Optimal default scale
	// FIX: Match gradient levels to actual color count (5 colors = 2 upper + 3 lower)
	// This prevents NormalizeGradientVector from interpolating colors incorrectly
	preset.bySkyBoxGradientLevelUpper = 2;  // Was 8
	preset.bySkyBoxGradientLevelLower = 3;  // Was 8
	preset.bSkyBoxTextureRenderMode = TRUE;
    preset.CloudGradientColor = MakeGradientColor(D3DXCOLOR(0.10f, 0.12f, 0.22f, 1.0f), D3DXCOLOR(0.04f, 0.05f, 0.10f, 1.0f));
    preset.SkyBoxGradientColorVector =
    {
        MakeGradientColor(D3DXCOLOR(0.01f, 0.01f, 0.06f, 1.0f), D3DXCOLOR(0.02f, 0.02f, 0.08f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.02f, 0.02f, 0.08f, 1.0f), D3DXCOLOR(0.03f, 0.03f, 0.10f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.03f, 0.03f, 0.10f, 1.0f), D3DXCOLOR(0.04f, 0.04f, 0.12f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.04f, 0.05f, 0.13f, 1.0f), D3DXCOLOR(0.06f, 0.06f, 0.16f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.06f, 0.07f, 0.18f, 1.0f), D3DXCOLOR(0.09f, 0.10f, 0.24f, 1.0f))
    };

	// Clouds
	preset.v2CloudScale = D3DXVECTOR2(200.0f, 200.0f);
	preset.fCloudHeight = 2000.0f;
	preset.v2CloudTextureScale = D3DXVECTOR2(2.0f, 2.0f);
	preset.v2CloudSpeed = D3DXVECTOR2(0.002f, 0.002f);  // Slower at night

	// Fog
	preset.bFogEnable = TRUE;
	preset.bDensityFog = TRUE;
	preset.fFogNearDistance = 500.0f;
	preset.fFogFarDistance = 5000.0f;
	preset.FogColor = D3DXCOLOR(0.1f, 0.1f, 0.15f, 1.0f);  // Dark blue
	preset.bFogLevel = 8;  // Heavy fog

	// Sun (disabled at night)
	preset.bLensFlareEnable = FALSE;
	preset.LensFlareBrightnessColor = D3DXCOLOR(0.0f, 0.0f, 0.0f, 0.0f);
	preset.fLensFlareMaxBrightness = 0.0f;
	preset.bMainFlareEnable = FALSE;
	preset.fMainFlareSize = 0.0f;

	// Lighting
	preset.bDirLightBackground = TRUE;
	preset.bDirLightCharacter = TRUE;
	preset.v3DirLightBackgroundDirection = D3DXVECTOR3(-0.05f, -0.20f, -0.98f);
	preset.kDirLightBackgroundAmbient = D3DXCOLOR(0.10f, 0.12f, 0.18f, 1.0f);
	preset.kDirLightBackgroundDiffuse = D3DXCOLOR(0.20f, 0.24f, 0.35f, 1.0f);
	preset.v3DirLightCharacterDirection = D3DXVECTOR3(-0.02f, -0.15f, -0.99f);
	preset.kDirLightCharacterAmbient = D3DXCOLOR(0.12f, 0.14f, 0.20f, 1.0f);
	preset.kDirLightCharacterDiffuse = D3DXCOLOR(0.24f, 0.28f, 0.40f, 1.0f);

	// Screen Filter (dark blue tint)
	preset.bFilteringEnable = TRUE;
	preset.FilteringColor = D3DXCOLOR(0.3f, 0.3f, 0.5f, 0.3f);

	// Wind
	preset.fWindStrength = 0.5f;
	preset.fWindRandom = 0.4f;
}

void CImGuiEnvironmentControls::CreateSunsetPreset()
{
	SEnvironmentPreset& preset = m_aPresets[2];
	preset.strName = "Sunset";
	preset.strDescription = "Orange/red sunset with sun low on horizon";

	// Skybox
	preset.v3SkyBoxScale = D3DXVECTOR3(29000.0f, 29000.0f, 29000.0f);  // M3-SKY-SCALE-PERSIST: Optimal default scale
	// FIX: Match gradient levels to actual color count (5 colors = 3 upper + 2 lower)
	// This prevents NormalizeGradientVector from interpolating colors incorrectly
	preset.bySkyBoxGradientLevelUpper = 3;  // Was 10 (incorrect)
	preset.bySkyBoxGradientLevelLower = 2;  // Was 10 (incorrect)
	preset.bSkyBoxTextureRenderMode = TRUE;
	preset.CloudGradientColor = MakeGradientColor(D3DXCOLOR(1.00f, 0.60f, 0.35f, 1.0f), D3DXCOLOR(0.82f, 0.32f, 0.18f, 1.0f));
	preset.SkyBoxGradientColorVector =
	{
		MakeGradientColor(D3DXCOLOR(0.32f, 0.10f, 0.08f, 1.0f), D3DXCOLOR(0.42f, 0.13f, 0.10f, 1.0f)),
		MakeGradientColor(D3DXCOLOR(0.55f, 0.18f, 0.12f, 1.0f), D3DXCOLOR(0.68f, 0.24f, 0.14f, 1.0f)),
		MakeGradientColor(D3DXCOLOR(0.82f, 0.35f, 0.16f, 1.0f), D3DXCOLOR(0.94f, 0.48f, 0.20f, 1.0f)),
		MakeGradientColor(D3DXCOLOR(0.98f, 0.62f, 0.30f, 1.0f), D3DXCOLOR(1.00f, 0.75f, 0.45f, 1.0f)),
		MakeGradientColor(D3DXCOLOR(1.00f, 0.82f, 0.58f, 1.0f), D3DXCOLOR(1.00f, 0.90f, 0.72f, 1.0f))
	};

	// Clouds
	preset.v2CloudScale = D3DXVECTOR2(200.0f, 200.0f);
	preset.fCloudHeight = 1800.0f;
	preset.v2CloudTextureScale = D3DXVECTOR2(2.0f, 2.0f);
	preset.v2CloudSpeed = D3DXVECTOR2(0.003f, 0.003f);

	// Fog
	preset.bFogEnable = TRUE;
	preset.bDensityFog = FALSE;
	preset.fFogNearDistance = 2000.0f;
	preset.fFogFarDistance = 10000.0f;
	preset.FogColor = D3DXCOLOR(0.9f, 0.5f, 0.3f, 1.0f);  // Orange
	preset.bFogLevel = 5;

	// Sun
	preset.bLensFlareEnable = TRUE;
	preset.LensFlareBrightnessColor = D3DXCOLOR(1.0f, 0.6f, 0.3f, 1.0f);  // Orange-red
	preset.fLensFlareMaxBrightness = 0.8f;
	preset.bMainFlareEnable = TRUE;
	preset.fMainFlareSize = 0.7f;  // Larger sun at sunset

	// Lighting
	preset.bDirLightBackground = TRUE;
	preset.bDirLightCharacter = TRUE;
	preset.v3DirLightBackgroundDirection = D3DXVECTOR3(-0.52f, -0.30f, -0.80f);
	preset.kDirLightBackgroundAmbient = D3DXCOLOR(0.32f, 0.22f, 0.18f, 1.0f);
	preset.kDirLightBackgroundDiffuse = D3DXCOLOR(1.00f, 0.72f, 0.46f, 1.0f);
	preset.v3DirLightCharacterDirection = D3DXVECTOR3(-0.45f, -0.28f, -0.84f);
	preset.kDirLightCharacterAmbient = D3DXCOLOR(0.30f, 0.20f, 0.16f, 1.0f);
	preset.kDirLightCharacterDiffuse = D3DXCOLOR(1.00f, 0.70f, 0.42f, 1.0f);

	// Screen Filter (warm orange tint)
	preset.bFilteringEnable = TRUE;
	preset.FilteringColor = D3DXCOLOR(1.0f, 0.7f, 0.4f, 0.2f);

	// Wind
	preset.fWindStrength = 0.4f;
	preset.fWindRandom = 0.3f;
}

void CImGuiEnvironmentControls::CreateOvercastPreset()
{
	SEnvironmentPreset& preset = m_aPresets[3];
	preset.strName = "Overcast";
	preset.strDescription = "Cloudy/overcast with no sun, grey atmosphere";

	// Skybox
	preset.v3SkyBoxScale = D3DXVECTOR3(29000.0f, 29000.0f, 29000.0f);  // M3-SKY-SCALE-PERSIST: Optimal default scale
	// FIX: Match gradient levels to actual color count (5 colors = 2 upper + 3 lower)
	// This prevents NormalizeGradientVector from interpolating colors incorrectly
	preset.bySkyBoxGradientLevelUpper = 2;  // Was 8
	preset.bySkyBoxGradientLevelLower = 3;  // Was 8
	preset.bSkyBoxTextureRenderMode = TRUE;
    preset.CloudGradientColor = MakeGradientColor(D3DXCOLOR(0.62f, 0.64f, 0.66f, 1.0f), D3DXCOLOR(0.42f, 0.44f, 0.46f, 1.0f));
    preset.SkyBoxGradientColorVector =
    {
        MakeGradientColor(D3DXCOLOR(0.22f, 0.24f, 0.27f, 1.0f), D3DXCOLOR(0.26f, 0.28f, 0.31f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.30f, 0.32f, 0.35f, 1.0f), D3DXCOLOR(0.36f, 0.38f, 0.41f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.40f, 0.42f, 0.45f, 1.0f), D3DXCOLOR(0.48f, 0.50f, 0.53f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.54f, 0.56f, 0.59f, 1.0f), D3DXCOLOR(0.62f, 0.64f, 0.67f, 1.0f)),
        MakeGradientColor(D3DXCOLOR(0.68f, 0.70f, 0.73f, 1.0f), D3DXCOLOR(0.76f, 0.78f, 0.81f, 1.0f))
    };

	// Clouds
	preset.v2CloudScale = D3DXVECTOR2(250.0f, 250.0f);  // Larger clouds
	preset.fCloudHeight = 1500.0f;  // Lower clouds
	preset.v2CloudTextureScale = D3DXVECTOR2(3.0f, 3.0f);
	preset.v2CloudSpeed = D3DXVECTOR2(0.008f, 0.008f);  // Faster clouds

	// Fog
	preset.bFogEnable = TRUE;
	preset.bDensityFog = TRUE;
	preset.fFogNearDistance = 1000.0f;
	preset.fFogFarDistance = 8000.0f;
	preset.FogColor = D3DXCOLOR(0.5f, 0.5f, 0.5f, 1.0f);  // Grey
	preset.bFogLevel = 6;

	// Sun (no sun in overcast)
	preset.bLensFlareEnable = FALSE;
	preset.LensFlareBrightnessColor = D3DXCOLOR(0.5f, 0.5f, 0.5f, 0.3f);
	preset.fLensFlareMaxBrightness = 0.2f;
	preset.bMainFlareEnable = FALSE;
	preset.fMainFlareSize = 0.0f;

	// Lighting
	preset.bDirLightBackground = TRUE;
	preset.bDirLightCharacter = TRUE;
	preset.v3DirLightBackgroundDirection = D3DXVECTOR3(-0.12f, -0.42f, -0.90f);
	preset.kDirLightBackgroundAmbient = D3DXCOLOR(0.28f, 0.28f, 0.30f, 1.0f);
	preset.kDirLightBackgroundDiffuse = D3DXCOLOR(0.58f, 0.60f, 0.62f, 1.0f);
	preset.v3DirLightCharacterDirection = D3DXVECTOR3(-0.10f, -0.40f, -0.91f);
	preset.kDirLightCharacterAmbient = D3DXCOLOR(0.26f, 0.26f, 0.28f, 1.0f);
	preset.kDirLightCharacterDiffuse = D3DXCOLOR(0.56f, 0.58f, 0.60f, 1.0f);

	// Screen Filter (grey desaturate)
	preset.bFilteringEnable = TRUE;
	preset.FilteringColor = D3DXCOLOR(0.8f, 0.8f, 0.8f, 0.15f);

	// Wind
	preset.fWindStrength = 0.7f;
	preset.fWindRandom = 0.5f;
}

void CImGuiEnvironmentControls::ApplyPreset(int iPresetIndex)
{
	if (iPresetIndex < 0 || iPresetIndex >= PRESET_COUNT)
		return;

	// M3-SKY-SCALE-PERSIST: Save current sky scale before applying preset
	D3DXVECTOR3 v3SavedSkyScale = m_workingEnv.v3SkyBoxScale;

	m_workingEnv = m_aPresets[iPresetIndex];
	m_iCurrentPresetIndex = iPresetIndex;
	++m_dwPresetSwitchCount;

	// M3-SKY-SCALE-PERSIST: Restore saved sky scale (don't overwrite user's manual setting)
	m_workingEnv.v3SkyBoxScale = v3SavedSkyScale;

	// M3-SKY-PRESET-PERSIST-74: Store preset for re-application when environment data changes
	m_lastAppliedPreset = m_aPresets[iPresetIndex];
	m_bPresetApplied = true;

	if (iPresetIndex == 1)
		CPythonBackground::Instance().ChangeToNight();
	else
		CPythonBackground::Instance().ChangeToDay();

	// Apply to game immediately
	ApplyToGame();

	TraceError("M3_SKY_PRESET_APPLIED preset=%s index=%d",
		m_aPresets[iPresetIndex].strName.c_str(), iPresetIndex);
}

void CImGuiEnvironmentControls::SaveCurrentToPreset(int iPresetIndex)
{
	if (iPresetIndex < 0 || iPresetIndex >= PRESET_COUNT)
		return;

	// Save current working environment to preset (preserving name/description)
	std::string strName = m_aPresets[iPresetIndex].strName;
	std::string strDescription = m_aPresets[iPresetIndex].strDescription;

	m_aPresets[iPresetIndex] = m_workingEnv;
	m_aPresets[iPresetIndex].strName = strName;
	m_aPresets[iPresetIndex].strDescription = strDescription;

	TraceError("M3_SKY_PRESET_SAVED preset=%s index=%d", strName.c_str(), iPresetIndex);
}

const char* CImGuiEnvironmentControls::GetPresetName(int iPresetIndex) const
{
	if (iPresetIndex < 0 || iPresetIndex >= PRESET_COUNT)
		return "Invalid";

	return m_aPresets[iPresetIndex].strName.c_str();
}

//====================================================================================
// Environment Data Access
//====================================================================================

TEnvironmentData* CImGuiEnvironmentControls::GetCurrentEnvironmentData()
{
	// M3-SKY-BLEND-FIX-74: Use CPythonBackground singleton (not CMapManager)
	if (!CPythonBackground::Instance().IsMapOutdoor())
		return nullptr;

	const TEnvironmentData* pEnvData = nullptr;
	CPythonBackground::Instance().GetCurrentEnvironmentData(&pEnvData);
	return const_cast<TEnvironmentData*>(pEnvData);
}

void CImGuiEnvironmentControls::RefreshFromGame()
{
	TEnvironmentData* pEnvData = GetCurrentEnvironmentData();
	if (!pEnvData)
		return;

	// M3-SKY-BLEND-FIX-74: Telemetry - check texture state on refresh
	TraceError("ENV_REFRESH skybox_tex0=%s cloud_tex=%s gradient_count=%d",
		pEnvData->strSkyBoxFaceFileName[0].empty() ? "EMPTY" : "OK",
		pEnvData->strCloudTextureFileName.empty() ? "EMPTY" : "OK",
		(int)pEnvData->SkyBoxGradientColorVector.size());

	// M3-SKY-BLEND-FIX-74: Copy game environment to working copy
	// (Simplified - only copy parameters we control in UI)
	m_workingEnv.v3SkyBoxScale = pEnvData->v3SkyBoxScale;
	m_workingEnv.bySkyBoxGradientLevelUpper = pEnvData->bySkyBoxGradientLevelUpper;
	m_workingEnv.bySkyBoxGradientLevelLower = pEnvData->bySkyBoxGradientLevelLower;
	m_workingEnv.bSkyBoxTextureRenderMode = pEnvData->bSkyBoxTextureRenderMode;
	m_workingEnv.CloudGradientColor = pEnvData->CloudGradientColor;
	m_workingEnv.SkyBoxGradientColorVector = pEnvData->SkyBoxGradientColorVector;

	m_workingEnv.v2CloudScale = pEnvData->v2CloudScale;
	m_workingEnv.fCloudHeight = pEnvData->fCloudHeight;
	m_workingEnv.v2CloudTextureScale = pEnvData->v2CloudTextureScale;
	m_workingEnv.v2CloudSpeed = pEnvData->v2CloudSpeed;

	m_workingEnv.bFogEnable = pEnvData->bFogEnable;
	m_workingEnv.bDensityFog = pEnvData->bDensityFog;
	m_workingEnv.fFogNearDistance = pEnvData->m_fFogNearDistance;
	m_workingEnv.fFogFarDistance = pEnvData->m_fFogFarDistance;
	m_workingEnv.FogColor = pEnvData->FogColor;
	m_workingEnv.bFogLevel = pEnvData->bFogLevel;

	m_workingEnv.bLensFlareEnable = pEnvData->bLensFlareEnable;
	m_workingEnv.LensFlareBrightnessColor = pEnvData->LensFlareBrightnessColor;
	m_workingEnv.fLensFlareMaxBrightness = pEnvData->fLensFlareMaxBrightness;
	m_workingEnv.bMainFlareEnable = pEnvData->bMainFlareEnable;
	m_workingEnv.fMainFlareSize = pEnvData->fMainFlareSize;

	m_workingEnv.bDirLightBackground = pEnvData->bDirLightsEnable[ENV_DIRLIGHT_BACKGROUND];
	m_workingEnv.bDirLightCharacter = pEnvData->bDirLightsEnable[ENV_DIRLIGHT_CHARACTER];
	m_workingEnv.v3DirLightBackgroundDirection = pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction;
	m_workingEnv.kDirLightBackgroundAmbient = D3DXCOLOR(
		pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient.r,
		pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient.g,
		pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient.b,
		pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient.a);
	m_workingEnv.kDirLightBackgroundDiffuse = D3DXCOLOR(
		pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse.r,
		pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse.g,
		pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse.b,
		pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse.a);
	m_workingEnv.v3DirLightCharacterDirection = pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Direction;
	m_workingEnv.kDirLightCharacterAmbient = D3DXCOLOR(
		pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient.r,
		pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient.g,
		pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient.b,
		pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient.a);
	m_workingEnv.kDirLightCharacterDiffuse = D3DXCOLOR(
		pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse.r,
		pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse.g,
		pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse.b,
		pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse.a);

	m_workingEnv.bFilteringEnable = pEnvData->bFilteringEnable;
	m_workingEnv.FilteringColor = pEnvData->FilteringColor;

	m_workingEnv.fWindStrength = pEnvData->fWindStrength;
	m_workingEnv.fWindRandom = pEnvData->fWindRandom;

	m_dwLastRefreshTime = ELTimer_GetMSec();
}

void CImGuiEnvironmentControls::ApplyToGame()
{
    TEnvironmentData* pEnvData = GetCurrentEnvironmentData();
    if (!pEnvData)
    {
        static bool s_bEnvDataMissingLogged = false;
        if (!s_bEnvDataMissingLogged)
        {
            s_bEnvDataMissingLogged = true;
            TraceError("DX11_ENV_CONTROL_UNBOUND control=environment_data reason=null_environment_data");
        }
        return;
    }

    auto IsSkyGradientEqual = [&](const std::vector<TGradientColor>& a, const std::vector<TGradientColor>& b) -> bool
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (!NearlyEqualGradient(a[i], b[i]))
                return false;
        }
        return true;
    };

    auto NormalizeGradientVector = [](const std::vector<TGradientColor>& src, size_t requiredCount) -> std::vector<TGradientColor>
    {
        const size_t count = (requiredCount == 0u) ? 1u : requiredCount;
        std::vector<TGradientColor> out;
        out.resize(count);

        auto fallback = []() -> TGradientColor
        {
            TGradientColor g = {};
            g.m_FirstColor = TColor(0.20f, 0.35f, 0.70f, 1.0f);
            g.m_SecondColor = TColor(0.05f, 0.10f, 0.25f, 1.0f);
            return g;
        };

        auto sample = [&](float t) -> TGradientColor
        {
            if (src.empty())
                return fallback();
            if (src.size() == 1u)
                return src[0];

            const float tt = std::min(1.0f, std::max(0.0f, t));
            const float pos = tt * static_cast<float>(src.size() - 1u);
            const size_t i0 = static_cast<size_t>(pos);
            const size_t i1 = std::min(i0 + 1u, src.size() - 1u);
            const float a = pos - static_cast<float>(i0);

            TGradientColor g = {};
            g.m_FirstColor = TColor(
                src[i0].m_FirstColor.r + (src[i1].m_FirstColor.r - src[i0].m_FirstColor.r) * a,
                src[i0].m_FirstColor.g + (src[i1].m_FirstColor.g - src[i0].m_FirstColor.g) * a,
                src[i0].m_FirstColor.b + (src[i1].m_FirstColor.b - src[i0].m_FirstColor.b) * a,
                src[i0].m_FirstColor.a + (src[i1].m_FirstColor.a - src[i0].m_FirstColor.a) * a);
            g.m_SecondColor = TColor(
                src[i0].m_SecondColor.r + (src[i1].m_SecondColor.r - src[i0].m_SecondColor.r) * a,
                src[i0].m_SecondColor.g + (src[i1].m_SecondColor.g - src[i0].m_SecondColor.g) * a,
                src[i0].m_SecondColor.b + (src[i1].m_SecondColor.b - src[i0].m_SecondColor.b) * a,
                src[i0].m_SecondColor.a + (src[i1].m_SecondColor.a - src[i0].m_SecondColor.a) * a);
            return g;
        };

        for (size_t i = 0; i < count; ++i)
        {
            const float t = (count <= 1u) ? 0.0f : (static_cast<float>(i) / static_cast<float>(count - 1u));
            out[i] = sample(t);
        }
        return out;
    };

    const size_t uRequiredGradientCount = std::max<size_t>(
        1u,
        static_cast<size_t>(m_workingEnv.bySkyBoxGradientLevelUpper) +
        static_cast<size_t>(m_workingEnv.bySkyBoxGradientLevelLower));

    // M3-SKY-BLEND-FIX-74: Only normalize if count doesn't match (prevents black sky bugs)
    // Normalization interpolates colors and can cause dark sky if levels are set incorrectly
    if (m_workingEnv.SkyBoxGradientColorVector.size() != uRequiredGradientCount)
    {
        TraceError("DX11_SKY_GRADIENT_MISMATCH current=%zu required=%zu normalizing=1",
            m_workingEnv.SkyBoxGradientColorVector.size(), uRequiredGradientCount);
        m_workingEnv.SkyBoxGradientColorVector = NormalizeGradientVector(m_workingEnv.SkyBoxGradientColorVector, uRequiredGradientCount);
    }

    const bool bDistanceChanged =
        !NearlyEqualVec3(pEnvData->v3SkyBoxScale, m_workingEnv.v3SkyBoxScale) ||
        !NearlyEqualFloat(pEnvData->m_fFogNearDistance, m_workingEnv.fFogNearDistance) ||
        !NearlyEqualFloat(pEnvData->m_fFogFarDistance, m_workingEnv.fFogFarDistance) ||
        (pEnvData->bFogEnable != m_workingEnv.bFogEnable) ||
        (pEnvData->bDensityFog != m_workingEnv.bDensityFog) ||
        (pEnvData->bFogLevel != m_workingEnv.bFogLevel) ||
        !NearlyEqualColor(pEnvData->FogColor, m_workingEnv.FogColor);

    const bool bSkyStructuralChanged =
        (pEnvData->bySkyBoxGradientLevelUpper != m_workingEnv.bySkyBoxGradientLevelUpper) ||
        (pEnvData->bySkyBoxGradientLevelLower != m_workingEnv.bySkyBoxGradientLevelLower) ||
        (pEnvData->bSkyBoxTextureRenderMode != m_workingEnv.bSkyBoxTextureRenderMode) ||
        !NearlyEqualVec2(pEnvData->v2CloudScale, m_workingEnv.v2CloudScale) ||
        !NearlyEqualFloat(pEnvData->fCloudHeight, m_workingEnv.fCloudHeight) ||
        !NearlyEqualVec2(pEnvData->v2CloudTextureScale, m_workingEnv.v2CloudTextureScale) ||
        !NearlyEqualVec2(pEnvData->v2CloudSpeed, m_workingEnv.v2CloudSpeed) ||
        !NearlyEqualGradient(pEnvData->CloudGradientColor, m_workingEnv.CloudGradientColor) ||
        !IsSkyGradientEqual(pEnvData->SkyBoxGradientColorVector, m_workingEnv.SkyBoxGradientColorVector);

    const bool bLensChanged =
        (pEnvData->bLensFlareEnable != m_workingEnv.bLensFlareEnable) ||
        !NearlyEqualColor(pEnvData->LensFlareBrightnessColor, m_workingEnv.LensFlareBrightnessColor) ||
        !NearlyEqualFloat(pEnvData->fLensFlareMaxBrightness, m_workingEnv.fLensFlareMaxBrightness) ||
        (pEnvData->bMainFlareEnable != m_workingEnv.bMainFlareEnable) ||
        !NearlyEqualFloat(pEnvData->fMainFlareSize, m_workingEnv.fMainFlareSize);

    const bool bLightingChanged =
        (pEnvData->bDirLightsEnable[ENV_DIRLIGHT_BACKGROUND] != m_workingEnv.bDirLightBackground) ||
        (pEnvData->bDirLightsEnable[ENV_DIRLIGHT_CHARACTER] != m_workingEnv.bDirLightCharacter) ||
        !NearlyEqualVec3(pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction, m_workingEnv.v3DirLightBackgroundDirection) ||
        !NearlyEqualColor(D3DXCOLOR(pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient.r, pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient.g, pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient.b, pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient.a), m_workingEnv.kDirLightBackgroundAmbient) ||
        !NearlyEqualColor(D3DXCOLOR(pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse.r, pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse.g, pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse.b, pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse.a), m_workingEnv.kDirLightBackgroundDiffuse) ||
        !NearlyEqualVec3(pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Direction, m_workingEnv.v3DirLightCharacterDirection) ||
        !NearlyEqualColor(D3DXCOLOR(pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient.r, pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient.g, pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient.b, pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient.a), m_workingEnv.kDirLightCharacterAmbient) ||
        !NearlyEqualColor(D3DXCOLOR(pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse.r, pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse.g, pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse.b, pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse.a), m_workingEnv.kDirLightCharacterDiffuse);

    const bool bScreenFilterChanged =
        (pEnvData->bFilteringEnable != m_workingEnv.bFilteringEnable) ||
        !NearlyEqualColor(pEnvData->FilteringColor, m_workingEnv.FilteringColor);

    const bool bWindChanged =
        !NearlyEqualFloat(pEnvData->fWindStrength, m_workingEnv.fWindStrength) ||
        !NearlyEqualFloat(pEnvData->fWindRandom, m_workingEnv.fWindRandom);

    pEnvData->v3SkyBoxScale = m_workingEnv.v3SkyBoxScale;
    pEnvData->bySkyBoxGradientLevelUpper = m_workingEnv.bySkyBoxGradientLevelUpper;
    pEnvData->bySkyBoxGradientLevelLower = m_workingEnv.bySkyBoxGradientLevelLower;
    pEnvData->bSkyBoxTextureRenderMode = m_workingEnv.bSkyBoxTextureRenderMode;
    pEnvData->CloudGradientColor = m_workingEnv.CloudGradientColor;
    pEnvData->SkyBoxGradientColorVector = m_workingEnv.SkyBoxGradientColorVector;

    pEnvData->v2CloudScale = m_workingEnv.v2CloudScale;
    pEnvData->fCloudHeight = m_workingEnv.fCloudHeight;
    pEnvData->v2CloudTextureScale = m_workingEnv.v2CloudTextureScale;
    pEnvData->v2CloudSpeed = m_workingEnv.v2CloudSpeed;

    pEnvData->bFogEnable = m_workingEnv.bFogEnable;
    pEnvData->bDensityFog = m_workingEnv.bDensityFog;
    pEnvData->m_fFogNearDistance = m_workingEnv.fFogNearDistance;
    pEnvData->m_fFogFarDistance = m_workingEnv.fFogFarDistance;
    pEnvData->FogColor = m_workingEnv.FogColor;
    pEnvData->bFogLevel = m_workingEnv.bFogLevel;

    pEnvData->bLensFlareEnable = m_workingEnv.bLensFlareEnable;
    pEnvData->LensFlareBrightnessColor = m_workingEnv.LensFlareBrightnessColor;
    pEnvData->fLensFlareMaxBrightness = m_workingEnv.fLensFlareMaxBrightness;
    pEnvData->bMainFlareEnable = m_workingEnv.bMainFlareEnable;
    pEnvData->fMainFlareSize = m_workingEnv.fMainFlareSize;

    pEnvData->bDirLightsEnable[ENV_DIRLIGHT_BACKGROUND] = m_workingEnv.bDirLightBackground;
    pEnvData->bDirLightsEnable[ENV_DIRLIGHT_CHARACTER] = m_workingEnv.bDirLightCharacter;
    pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Direction = m_workingEnv.v3DirLightBackgroundDirection;
    pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Ambient = m_workingEnv.kDirLightBackgroundAmbient;
    pEnvData->DirLights[ENV_DIRLIGHT_BACKGROUND].Diffuse = m_workingEnv.kDirLightBackgroundDiffuse;
    pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Direction = m_workingEnv.v3DirLightCharacterDirection;
    pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Ambient = m_workingEnv.kDirLightCharacterAmbient;
    pEnvData->DirLights[ENV_DIRLIGHT_CHARACTER].Diffuse = m_workingEnv.kDirLightCharacterDiffuse;

    pEnvData->bFilteringEnable = m_workingEnv.bFilteringEnable;
    pEnvData->FilteringColor = m_workingEnv.FilteringColor;

    pEnvData->fWindStrength = m_workingEnv.fWindStrength;
    pEnvData->fWindRandom = m_workingEnv.fWindRandom;

    if (CPythonBackground::Instance().IsMapOutdoor())
    {
        CMapOutdoor& rMapOutdoor = CPythonBackground::Instance().GetMapOutdoorRef();

        if (bSkyStructuralChanged)
            rMapOutdoor.SetEnvironmentSkyBox();
        else if (bDistanceChanged)
            rMapOutdoor.ApplyEnvironmentDistanceOnly();

        if (bLensChanged)
            rMapOutdoor.SetEnvironmentLensFlare();

        if (bScreenFilterChanged)
            rMapOutdoor.SetEnvironmentScreenFilter();
    }

    unsigned int uChangedMask = 0u;
    if (bDistanceChanged)      uChangedMask |= (1u << 0);
    if (bSkyStructuralChanged) uChangedMask |= (1u << 1);
    if (bLensChanged)          uChangedMask |= (1u << 2);
    if (bLightingChanged)      uChangedMask |= (1u << 3);
    if (bScreenFilterChanged)  uChangedMask |= (1u << 4);
    if (bWindChanged)          uChangedMask |= (1u << 5);

    TraceError("DX11_ENV_APPLY mask=0x%02X source=debugui distance=%u sky=%u lens=%u light=%u filter=%u wind=%u",
        uChangedMask,
        bDistanceChanged ? 1u : 0u,
        bSkyStructuralChanged ? 1u : 0u,
        bLensChanged ? 1u : 0u,
        bLightingChanged ? 1u : 0u,
        bScreenFilterChanged ? 1u : 0u,
        bWindChanged ? 1u : 0u);

    m_bDirty = false;
    m_bUserHasModified = true;
    ++m_dwParameterChangeCount;
}

//====================================================================================
// Main Render Method
//====================================================================================

void CImGuiEnvironmentControls::Render()
{
	if (!m_bEnabled)
		return;

	// Create environment controls window
	// M3-SKY-BLEND-FIX-74: Detect when game changes environment data pointer (day/night cycle, map load)
	// M3-SKY-PRESET-PERSIST-74: Re-apply preset if environment data changes to prevent overwrite
	TEnvironmentData* pCurrentEnvData = GetCurrentEnvironmentData();
	if (pCurrentEnvData != m_pLastEnvironmentData)
	{
		// Environment data pointer changed (new .msenv loaded by game)
		m_pLastEnvironmentData = pCurrentEnvData;

		if (m_bPresetApplied && pCurrentEnvData)
		{
			// M3-SKY-PRESET-PERSIST-74: Preset was applied - restore it instead of refreshing from game
			// This prevents RefreshFromGame() from overwriting preset colors with default values
			m_workingEnv = m_lastAppliedPreset;
			ApplyToGame();
			TraceError("M3_SKY_PRESET_RESTORED preset=%s after_env_pointer_change",
				m_lastAppliedPreset.strName.c_str());
		}
		else if (m_bUserHasModified && pCurrentEnvData)
		{
			// User has made manual modifications - re-apply them to the new environment data
			ApplyToGame();
		}
		else
		{
			// No user modifications - refresh UI from new environment
			RefreshFromGame();
		}
	}
	ImGui::SetNextWindowSize(ImVec2(450, 700), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Environment Controls (M3-SKY-BLEND-FIX-74)", &m_bEnabled, ImGuiWindowFlags_None))
	{
		ImGui::End();
		return;
	}

	// Render all sections
	RenderPresetSection();
	RenderSkyboxSection();
	RenderCloudSection();
	RenderFogSection();
	RenderSunSection();
	RenderLightingSection();
	RenderScreenFilterSection();
	RenderWindSection();
	RenderStatisticsSection();

	ImGui::End();
}

//====================================================================================
// Render Sections (implementations continue below...)
//====================================================================================

void CImGuiEnvironmentControls::RenderPresetSection()
{
	if (!ImGui::CollapsingHeader("Quick Presets", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::Indent();

	// Current preset display
	ImGui::Text("Current Preset: %s", m_iCurrentPresetIndex >= 0 ?
		GetPresetName(m_iCurrentPresetIndex) : "Custom");

	ImGui::Separator();

	// Preset buttons in a 2x2 grid
	for (int i = 0; i < PRESET_COUNT; ++i)
	{
		if (ImGui::Button(m_aPresets[i].strName.c_str(), ImVec2(100, 0)))
		{
			ApplyPreset(i);
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", m_aPresets[i].strDescription.c_str());
		}

		// 2 buttons per row
		if ((i % 2) == 0 && i < (PRESET_COUNT - 1))
			ImGui::SameLine();
	}

	ImGui::Separator();

	// Save current to preset
	ImGui::Text("Save Current As:");
	for (int i = 0; i < PRESET_COUNT; ++i)
	{
		char szBtnLabel[64];
		sprintf_s(szBtnLabel, "Overwrite %s##save%d", m_aPresets[i].strName.c_str(), i);
		if (ImGui::Button(szBtnLabel, ImVec2(150, 0)))
		{
			SaveCurrentToPreset(i);
		}

		if ((i % 2) == 0 && i < (PRESET_COUNT - 1))
			ImGui::SameLine();
	}

	ImGui::Unindent();
}

void CImGuiEnvironmentControls::RenderSkyboxSection()
{
	if (!ImGui::CollapsingHeader("Skybox Parameters"))
		return;

	ImGui::Indent();

	bool bChanged = false;

	// M3-SKY-BLEND-FIX-74: Sky scale with logarithmic slider (runtime uses thousands, not 0.1-10.0)
	// Real-world .msenv values: 3500.0 typical, range 100-100000
	float fSkyScaleX = m_workingEnv.v3SkyBoxScale.x;
	if (ImGui::SliderFloat("Sky Scale", &fSkyScaleX, 100.0f, 100000.0f, "%.0f", ImGuiSliderFlags_Logarithmic))
	{
		m_workingEnv.v3SkyBoxScale = D3DXVECTOR3(fSkyScaleX, fSkyScaleX, fSkyScaleX);
		bChanged = true;
	}

	// M3-SKY-BLEND-FIX-74: Gradient levels - AUTO-CALCULATED from color count
	// Manual editing disabled to prevent gradient normalization bugs (black sky)
	const size_t uCurrentGradientColorCount = m_workingEnv.SkyBoxGradientColorVector.size();

	// Auto-calculate gradient levels from color count (prevents normalization bugs)
	// Formula: For N colors, use upper=(N+1)/2, lower=N/2 to balance distribution
	const BYTE byAutoUpper = static_cast<BYTE>((uCurrentGradientColorCount + 1) / 2);
	const BYTE byAutoLower = static_cast<BYTE>(uCurrentGradientColorCount / 2);

	m_workingEnv.bySkyBoxGradientLevelUpper = byAutoUpper;
	m_workingEnv.bySkyBoxGradientLevelLower = byAutoLower;

	// Display as read-only (manual editing causes black sky due to normalization)
	ImGui::Text("Gradient Levels: Upper=%d, Lower=%d (auto from %zu colors)",
		(int)byAutoUpper, (int)byAutoLower, uCurrentGradientColorCount);
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Gradient levels are auto-calculated from color count.\n"
			"Manual editing disabled to prevent black sky bugs.\n"
			"Current preset has %zu gradient colors.", uCurrentGradientColorCount);
	}

	// M3-SKY-BLEND-FIX-74: 3-state policy selector (was 2-state checkbox)
	const char* szPolicyNames[] = { "Auto (MSENV)", "Force Gradient", "Force Texture" };
	int iPolicyIndex = static_cast<int>(m_eSkyRenderPolicyOverride);
	if (ImGui::Combo("Render Policy", &iPolicyIndex, szPolicyNames, 3))
	{
		m_eSkyRenderPolicyOverride = static_cast<ESkyRenderPolicy>(iPolicyIndex);
		bChanged = true;

		// Apply policy to MapOutdoor immediately
		class CMapOutdoor& rkMapOutdoor = CPythonBackground::Instance().GetMapOutdoorRef();
		rkMapOutdoor.SetSkyRenderPolicyOverride(m_eSkyRenderPolicyOverride);
	}

	// Show current MSENV mode value (for reference when using Auto policy)
	ImGui::SameLine();
	ImGui::TextDisabled("(MSENV: %s)", m_workingEnv.bSkyBoxTextureRenderMode ? "Texture" : "Gradient");

	if (bChanged)
	{
		MarkDirty();
		ApplyToGame();
	}

	ImGui::Unindent();
}

void CImGuiEnvironmentControls::RenderCloudSection()
{
	if (!ImGui::CollapsingHeader("Cloud Parameters"))
		return;

	ImGui::Indent();

	bool bChanged = false;

	bChanged |= ImGui::SliderFloat2("Cloud Scale", (float*)&m_workingEnv.v2CloudScale, 10.0f, 500.0f);
	bChanged |= ImGui::SliderFloat("Cloud Height", &m_workingEnv.fCloudHeight, -1000.0f, 5000.0f);
	bChanged |= ImGui::SliderFloat2("Cloud Texture Scale", (float*)&m_workingEnv.v2CloudTextureScale, 0.1f, 10.0f);
	bChanged |= ImGui::SliderFloat2("Cloud Speed", (float*)&m_workingEnv.v2CloudSpeed, -1.0f, 1.0f, "%.4f");

	if (bChanged)
	{
		MarkDirty();
		ApplyToGame();
	}

	ImGui::Unindent();
}

void CImGuiEnvironmentControls::RenderFogSection()
{
	if (!ImGui::CollapsingHeader("Fog Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::Indent();

	bool bChanged = false;

	bool bFogEnabled = m_workingEnv.bFogEnable;
	if (ImGui::Checkbox("Enable Fog", &bFogEnabled))
	{
		m_workingEnv.bFogEnable = bFogEnabled;
		bChanged = true;
	}

	bool bDensityFog = m_workingEnv.bDensityFog;
	if (ImGui::Checkbox("Density Fog (vs Range)", &bDensityFog))
	{
		m_workingEnv.bDensityFog = bDensityFog;
		bChanged = true;
	}

	bChanged |= ImGui::SliderFloat("Fog Near", &m_workingEnv.fFogNearDistance, 0.0f, 10000.0f);
	bChanged |= ImGui::SliderFloat("Fog Far", &m_workingEnv.fFogFarDistance, 0.0f, 50000.0f);

	int iFogLevel = m_workingEnv.bFogLevel;
	bChanged |= ImGui::SliderInt("Fog Level", &iFogLevel, 0, 10);
	m_workingEnv.bFogLevel = (BYTE)iFogLevel;

	bChanged |= ImGui::ColorEdit4("Fog Color", (float*)&m_workingEnv.FogColor);

	if (bChanged)
	{
		MarkDirty();
		ApplyToGame();
	}

	ImGui::Unindent();
}

void CImGuiEnvironmentControls::RenderSunSection()
{
	if (!ImGui::CollapsingHeader("Sun / Lens Flare", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::Indent();

	bool bChanged = false;

	bool bLensFlareEnabled = m_workingEnv.bLensFlareEnable;
	if (ImGui::Checkbox("Enable Lens Flare", &bLensFlareEnabled))
	{
		m_workingEnv.bLensFlareEnable = bLensFlareEnabled;
		bChanged = true;
	}

	bool bMainFlareEnabled = m_workingEnv.bMainFlareEnable;
	if (ImGui::Checkbox("Enable Main Sun Disc", &bMainFlareEnabled))
	{
		m_workingEnv.bMainFlareEnable = bMainFlareEnabled;
		bChanged = true;
	}

	bChanged |= ImGui::SliderFloat("Sun Size", &m_workingEnv.fMainFlareSize, 0.0f, 2.0f);
	bChanged |= ImGui::SliderFloat("Sun Brightness", &m_workingEnv.fLensFlareMaxBrightness, 0.0f, 2.0f);
	bChanged |= ImGui::ColorEdit4("Sun Color", (float*)&m_workingEnv.LensFlareBrightnessColor);

	ImGui::Text("Sun Direction (Background): %.2f %.2f %.2f",
		m_workingEnv.v3DirLightBackgroundDirection.x,
		m_workingEnv.v3DirLightBackgroundDirection.y,
		m_workingEnv.v3DirLightBackgroundDirection.z);

	if (bChanged)
	{
		MarkDirty();
		ApplyToGame();
	}

	ImGui::Unindent();
}

void CImGuiEnvironmentControls::RenderLightingSection()
{
	if (!ImGui::CollapsingHeader("Directional Lighting"))
		return;

	ImGui::Indent();

	bool bChanged = false;

	bool bDirLightBg = m_workingEnv.bDirLightBackground;
	if (ImGui::Checkbox("Background Light", &bDirLightBg))
	{
		m_workingEnv.bDirLightBackground = bDirLightBg;
		bChanged = true;
	}

	bool bDirLightChar = m_workingEnv.bDirLightCharacter;
	if (ImGui::Checkbox("Character Light", &bDirLightChar))
	{
		m_workingEnv.bDirLightCharacter = bDirLightChar;
		bChanged = true;
	}

	bChanged |= ImGui::SliderFloat3("BG Light Dir", (float*)&m_workingEnv.v3DirLightBackgroundDirection, -1.0f, 1.0f);
	bChanged |= ImGui::ColorEdit4("BG Ambient", (float*)&m_workingEnv.kDirLightBackgroundAmbient);
	bChanged |= ImGui::ColorEdit4("BG Diffuse", (float*)&m_workingEnv.kDirLightBackgroundDiffuse);
	bChanged |= ImGui::SliderFloat3("Char Light Dir", (float*)&m_workingEnv.v3DirLightCharacterDirection, -1.0f, 1.0f);
	bChanged |= ImGui::ColorEdit4("Char Ambient", (float*)&m_workingEnv.kDirLightCharacterAmbient);
	bChanged |= ImGui::ColorEdit4("Char Diffuse", (float*)&m_workingEnv.kDirLightCharacterDiffuse);

	if (bChanged)
	{
		MarkDirty();
		ApplyToGame();
	}

	ImGui::Unindent();
}

void CImGuiEnvironmentControls::RenderScreenFilterSection()
{
	if (!ImGui::CollapsingHeader("Screen Color Filter"))
		return;

	ImGui::Indent();

	bool bChanged = false;

	bool bFilterEnabled = m_workingEnv.bFilteringEnable;
	if (ImGui::Checkbox("Enable Screen Filter", &bFilterEnabled))
	{
		m_workingEnv.bFilteringEnable = bFilterEnabled;
		bChanged = true;
	}

	bChanged |= ImGui::ColorEdit4("Filter Color", (float*)&m_workingEnv.FilteringColor);

	if (bChanged)
	{
		MarkDirty();
		ApplyToGame();
	}

	ImGui::Unindent();
}

void CImGuiEnvironmentControls::RenderWindSection()
{
	if (!ImGui::CollapsingHeader("Wind Parameters"))
		return;

	ImGui::Indent();

	bool bChanged = false;

	bChanged |= ImGui::SliderFloat("Wind Strength", &m_workingEnv.fWindStrength, 0.0f, 2.0f);
	bChanged |= ImGui::SliderFloat("Wind Randomness", &m_workingEnv.fWindRandom, 0.0f, 1.0f);

	if (bChanged)
	{
		MarkDirty();
		ApplyToGame();
	}

	ImGui::Unindent();
}

void CImGuiEnvironmentControls::RenderStatisticsSection()
{
	if (!ImGui::CollapsingHeader("Statistics & Info"))
		return;

	ImGui::Indent();

	ImGui::Text("Preset Switches: %u", m_dwPresetSwitchCount);
	ImGui::Text("Parameter Changes: %u", m_dwParameterChangeCount);
	ImGui::Text("Last Refresh: %u ms ago", ELTimer_GetMSec() - m_dwLastRefreshTime);
	ImGui::Text("Dirty Flag: %s", m_bDirty ? "YES" : "NO");

	ImGui::Separator();

	if (ImGui::Button("Force Refresh from Game"))
	{
		RefreshFromGame();
	}

	ImGui::SameLine();

	if (ImGui::Button("Force Apply to Game"))
	{
		ApplyToGame();
	}

	ImGui::Unindent();
}



