// SPDX-License-Identifier: MIT
// ImGuiEnvironmentControls.h - Environment/Sky/Sun Debug Controls
// M3-SKY-BLEND-FIX-74: Real-time environment parameter tweaking
//
// Purpose: Provide full control over environment rendering (sky, clouds, fog, sun, lighting)
// Integration: ImGuiManager â†’ CImGuiEnvironmentControls â†’ MapOutdoor environment data

#pragma once

#include <string>
#include <vector>
#include "../EterLib/GrpBase.h"  // D3DXVECTOR2/3, D3DXCOLOR types
#include "../GameLib/MapType.h"  // TEnvironmentData definition

// M3-SKY-BLEND-FIX-74: Environment preset for quick switching (Day/Night/Sunset/Overcast)
struct SEnvironmentPreset
{
	std::string strName;
	std::string strDescription;

	// Skybox parameters
	D3DXVECTOR3 v3SkyBoxScale;
	BYTE bySkyBoxGradientLevelUpper;
	BYTE bySkyBoxGradientLevelLower;
	BOOL bSkyBoxTextureRenderMode;
    TGradientColor CloudGradientColor;
    std::vector<TGradientColor> SkyBoxGradientColorVector;

	// Cloud parameters
	D3DXVECTOR2 v2CloudScale;
	float fCloudHeight;
	D3DXVECTOR2 v2CloudTextureScale;
	D3DXVECTOR2 v2CloudSpeed;

	// Fog parameters
	BOOL bFogEnable;
	BOOL bDensityFog;
	float fFogNearDistance;
	float fFogFarDistance;
	D3DXCOLOR FogColor;
	BYTE bFogLevel;

	// Sun/Lens Flare parameters
	BOOL bLensFlareEnable;
	D3DXCOLOR LensFlareBrightnessColor;
	float fLensFlareMaxBrightness;
	BOOL bMainFlareEnable;
	float fMainFlareSize;

	// Lighting parameters
	BOOL bDirLightBackground;
	BOOL bDirLightCharacter;
	D3DXVECTOR3 v3DirLightBackgroundDirection;
	D3DXCOLOR kDirLightBackgroundAmbient;
	D3DXCOLOR kDirLightBackgroundDiffuse;
	D3DXVECTOR3 v3DirLightCharacterDirection;
	D3DXCOLOR kDirLightCharacterAmbient;
	D3DXCOLOR kDirLightCharacterDiffuse;

	// Screen filter
	BOOL bFilteringEnable;
	D3DXCOLOR FilteringColor;

	// Wind
	float fWindStrength;
	float fWindRandom;

	SEnvironmentPreset()
		: bySkyBoxGradientLevelUpper(8)
		, bySkyBoxGradientLevelLower(8)
		, bSkyBoxTextureRenderMode(TRUE)
		, fCloudHeight(2000.0f)
		, bFogEnable(TRUE)
		, bDensityFog(FALSE)
		, fFogNearDistance(1000.0f)
		, fFogFarDistance(10000.0f)
		, bFogLevel(5)
		, bLensFlareEnable(TRUE)
		, fLensFlareMaxBrightness(1.0f)
		, bMainFlareEnable(TRUE)
		, fMainFlareSize(0.5f)
		, bDirLightBackground(TRUE)
		, bDirLightCharacter(TRUE)
		, v3DirLightBackgroundDirection(0.0f, 0.0f, -1.0f)
		, kDirLightBackgroundAmbient(0.35f, 0.35f, 0.35f, 1.0f)
		, kDirLightBackgroundDiffuse(0.80f, 0.80f, 0.80f, 1.0f)
		, v3DirLightCharacterDirection(0.0f, 0.0f, -1.0f)
		, kDirLightCharacterAmbient(0.35f, 0.35f, 0.35f, 1.0f)
		, kDirLightCharacterDiffuse(0.80f, 0.80f, 0.80f, 1.0f)
		, bFilteringEnable(FALSE)
		, fWindStrength(0.5f)
		, fWindRandom(0.3f)
	{
	}
};

class CImGuiEnvironmentControls
{
public:
	// Singleton interface
	static CImGuiEnvironmentControls* Instance();
	static bool Create();
	static void Destroy();

	// Initialization
	bool Initialize();
	void Shutdown();

	// Render environment controls window
	void Render();

	// Enable/disable window visibility
	void SetEnabled(bool bEnable) { m_bEnabled = bEnable; }
	bool IsEnabled() const { return m_bEnabled; }
	void Toggle() { m_bEnabled = !m_bEnabled; }

	// Quick preset system
	void ApplyPreset(int iPresetIndex);
	void SaveCurrentToPreset(int iPresetIndex);
	int GetCurrentPresetIndex() const { return m_iCurrentPresetIndex; }
	const char* GetPresetName(int iPresetIndex) const;

	// Update current environment data from game (call every frame)
	void RefreshFromGame();

	// Apply modifications to game environment (call when user changes params)
	void ApplyToGame();

private:
	CImGuiEnvironmentControls();
	~CImGuiEnvironmentControls();

	// Prevent copying
	CImGuiEnvironmentControls(const CImGuiEnvironmentControls&) = delete;
	CImGuiEnvironmentControls& operator=(const CImGuiEnvironmentControls&) = delete;

	// Render sections
	void RenderPresetSection();
	void RenderSkyboxSection();
	void RenderCloudSection();
	void RenderFogSection();
	void RenderSunSection();
	void RenderLightingSection();
	void RenderScreenFilterSection();
	void RenderWindSection();
	void RenderStatisticsSection();

	// Preset initialization
	void InitializePresets();
	void CreateDayPreset();
	void CreateNightPreset();
	void CreateSunsetPreset();
	void CreateOvercastPreset();

	// Helper methods
	TEnvironmentData* GetCurrentEnvironmentData();
	void MarkDirty() { m_bDirty = true; m_bPresetApplied = false; }  // Clear preset on manual changes
	bool IsDirty() const { return m_bDirty; }

	// Member variables
	bool m_bEnabled;
	bool m_bInitialized;
	bool m_bDirty;  // True when user has modified parameters

	// Quick presets (4 presets: Day, Night, Sunset, Overcast)
	static const int PRESET_COUNT = 4;
	SEnvironmentPreset m_aPresets[PRESET_COUNT];
	int m_iCurrentPresetIndex;

	// Working copy of environment data (modified by UI, then applied to game)
	SEnvironmentPreset m_workingEnv;

	// Visual helpers state
	bool m_bShowSunPosition;
	bool m_bShowFogVisualization;

	// Telemetry
	DWORD m_dwPresetSwitchCount;
	DWORD m_dwParameterChangeCount;
	DWORD m_dwLastRefreshTime;

	// M3-SKY-BLEND-FIX-74: Track environment data pointer to detect external changes
	const TEnvironmentData* m_pLastEnvironmentData;
	bool m_bUserHasModified;  // True when user has made changes that should persist

	// M3-SKY-PRESET-PERSIST-74: Persist preset across environment data changes (day/night cycle)
	bool m_bPresetApplied;  // True when a preset was applied (and should be re-applied if env data changes)
	SEnvironmentPreset m_lastAppliedPreset;  // Storage for the last applied preset

	// M3-SKY-BLEND-FIX-74: Skybox render policy override (runtime control, not persisted in presets)
	ESkyRenderPolicy m_eSkyRenderPolicyOverride;

	// Singleton instance
	static CImGuiEnvironmentControls* ms_pInstance;
};

// Global access macros
#define ImGuiEnvCtrl() CImGuiEnvironmentControls::Instance()
#define IsImGuiEnvCtrlEnabled() (CImGuiEnvironmentControls::Instance() && CImGuiEnvironmentControls::Instance()->IsEnabled())

