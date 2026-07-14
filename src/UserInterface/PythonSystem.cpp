#include "StdAfx.h"
#include "PythonSystem.h"
#include "PythonApplication.h"

#define DEFAULT_VALUE_ALWAYS_SHOW_NAME		true

namespace {
	int NormalizeRenderAPI(int iRenderAPI)
	{
#if defined(DX11_STRICT_ONLY)
		(void)iRenderAPI;
		return 11;
#else
		switch (iRenderAPI)
		{
			case 9:
			case 11:
				return iRenderAPI;
			default:
				return 9;
		}
#endif
	}

	int ParseRenderAPI(const char* c_szValue)
	{
		if (!c_szValue || !c_szValue[0])
			return 9;

		if (!stricmp(c_szValue, "DX11"))
			return 11;
		if (!stricmp(c_szValue, "DX9"))
			return 9;

		return NormalizeRenderAPI(atoi(c_szValue));
	}

	const char* RenderAPIToString(int iRenderAPI)
	{
		return NormalizeRenderAPI(iRenderAPI) == 11 ? "DX11" : "DX9";
	}

	int NormalizeShaderCacheVersion(int iVersion)
	{
		if (iVersion < 1)
			return 1;
		if (iVersion > 1024)
			return 1024;

		return iVersion;
	}

	int NormalizeRenderFPSLimit(int iFPSLimit)
	{
		switch (iFPSLimit)
		{
			case 0:
			case 60:
			case 90:
			case 120:
				return iFPSLimit;
			default:
				return 60;
		}
	}

	int NormalizeDX11TexturePipelineMode(int iMode)
	{
		switch (iMode)
		{
		case 0:
		case 1:
		case 2:
			return iMode;
		default:
			return 0;
		}
	}

	int ParseDX11TexturePipelineMode(const char* c_szValue)
	{
		if (!c_szValue || !c_szValue[0])
			return 0;

		if (!stricmp(c_szValue, "NATIVE"))
			return 0;
		if (!stricmp(c_szValue, "HYBRID"))
			return 1;
		if (!stricmp(c_szValue, "LEGACY"))
			return 2;

		return NormalizeDX11TexturePipelineMode(atoi(c_szValue));
	}

	const char* DX11TexturePipelineModeToString(int iMode)
	{
		switch (NormalizeDX11TexturePipelineMode(iMode))
		{
		case 0:
			return "NATIVE";
		case 2:
			return "LEGACY";
		case 1:
		default:
			return "HYBRID";
		}
	}

	int NormalizePerfProfile(int iProfile)
	{
		if (iProfile < 0)
			return 1;
		if (iProfile > 2)
			return 1;

		return iProfile;
	}

	int ParsePerfProfile(const char* c_szValue)
	{
		if (!c_szValue || !c_szValue[0])
			return 1;

		if (!stricmp(c_szValue, "QUALITY"))
			return 0;
		if (!stricmp(c_szValue, "BALANCED"))
			return 1;
		if (!stricmp(c_szValue, "PERFORMANCE"))
			return 2;

		return NormalizePerfProfile(atoi(c_szValue));
	}

	const char* PerfProfileToString(int iProfile)
	{
		switch (NormalizePerfProfile(iProfile))
		{
			case 0:
				return "QUALITY";
			case 2:
				return "PERFORMANCE";
			case 1:
			default:
				return "BALANCED";
		}
	}

	int NormalizeShadowCadence(int iCadence)
	{
		if (iCadence < 1)
			return 1;
		if (iCadence > 3)
			return 3;

		return iCadence;
	}

	int NormalizeFXStrideBias(int iBias)
	{
		if (iBias < 0)
			return 0;
		if (iBias > 2)
			return 2;

		return iBias;
	}

	int NormalizeTextTailOptRange(int iRange)
	{
		if (iRange < 1500)
			iRange = 1500;
		else if (iRange > 9000)
			iRange = 9000;

		return ((iRange + 50) / 100) * 100;
	}
}

void CPythonSystem::SetInterfaceHandler(PyObject * poHandler)
{
// NOTE : ???????????? ???????????? ????????? ?????????. ??????????????? ?????? ?????? Python?????? ????????? ???????????? ?????? ??????.
//        ????????? __del__??? Destroy??? ????????? Handler??? NULL??? ????????????. - [levites]
//	if (m_poInterfaceHandler)
//		Py_DECREF(m_poInterfaceHandler);

	m_poInterfaceHandler = poHandler;

//	if (m_poInterfaceHandler)
//		Py_INCREF(m_poInterfaceHandler);
}

void CPythonSystem::DestroyInterfaceHandler()
{
	m_poInterfaceHandler = NULL;
}

void CPythonSystem::SaveWindowStatus(int iIndex, int iVisible, int iMinimized, int ix, int iy, int iHeight)
{
	m_WindowStatus[iIndex].isVisible	= iVisible;
	m_WindowStatus[iIndex].isMinimized	= iMinimized;
	m_WindowStatus[iIndex].ixPosition	= ix;
	m_WindowStatus[iIndex].iyPosition	= iy;
	m_WindowStatus[iIndex].iHeight		= iHeight;
}

void CPythonSystem::GetDisplaySettings()
{
	memset(m_ResolutionList, 0, sizeof(TResolution) * RESOLUTION_MAX_NUM);
	m_ResolutionCount = 0;

	auto AddDisplayMode = [&](DWORD width, DWORD height, DWORD bpp, DWORD refreshRate)
	{
		if (width < 800 || height < 600)
			return;

		if (bpp != 16 && bpp != 32)
			return;

		if (refreshRate < 30u || refreshRate > 1000u)
			refreshRate = 60u;

		int iResolutionIndex = -1;
		for (int i = 0; i < m_ResolutionCount; ++i)
		{
			if (m_ResolutionList[i].bpp == bpp &&
				m_ResolutionList[i].width == width &&
				m_ResolutionList[i].height == height)
			{
				iResolutionIndex = i;
				break;
			}
		}

		if (iResolutionIndex < 0)
		{
			if (m_ResolutionCount >= RESOLUTION_MAX_NUM)
				return;

			iResolutionIndex = m_ResolutionCount++;
			m_ResolutionList[iResolutionIndex].width = width;
			m_ResolutionList[iResolutionIndex].height = height;
			m_ResolutionList[iResolutionIndex].bpp = bpp;
			m_ResolutionList[iResolutionIndex].frequency_count = 0;
		}

		TResolution& rkResolution = m_ResolutionList[iResolutionIndex];
		for (int j = 0; j < rkResolution.frequency_count; ++j)
		{
			if (rkResolution.frequency[j] == refreshRate)
				return;
		}

		int iFrequencyCap = static_cast<int>(sizeof(rkResolution.frequency) / sizeof(rkResolution.frequency[0]));
		if (iFrequencyCap > FREQUENCY_MAX_NUM)
			iFrequencyCap = FREQUENCY_MAX_NUM;
		if (rkResolution.frequency_count < iFrequencyCap)
			rkResolution.frequency[rkResolution.frequency_count++] = refreshRate;
	};

	static bool s_bLoggedDisplayEnumerationPath = false;
	if (!s_bLoggedDisplayEnumerationPath)
	{
		s_bLoggedDisplayEnumerationPath = true;
		TraceError("DX11_DISPLAY_ENUM path=EnumDisplaySettingsA reason=dx11_single_path");
	}

	DEVMODEA kMode;
	ZeroMemory(&kMode, sizeof(kMode));
	kMode.dmSize = sizeof(kMode);

	for (DWORD dwModeNum = 0; EnumDisplaySettingsA(NULL, dwModeNum, &kMode); ++dwModeNum)
	{
		AddDisplayMode(kMode.dmPelsWidth, kMode.dmPelsHeight, kMode.dmBitsPerPel, kMode.dmDisplayFrequency);
	}

	if (m_ResolutionCount == 0)
	{
		AddDisplayMode(m_Config.width, m_Config.height, m_Config.bpp, m_Config.frequency);
	}
}

int	CPythonSystem::GetResolutionCount()
{
	return m_ResolutionCount;
}

int CPythonSystem::GetFrequencyCount(int index)
{
	if (index >= m_ResolutionCount)
		return 0;

    return m_ResolutionList[index].frequency_count;
}

bool CPythonSystem::GetResolution(int index, OUT DWORD *width, OUT DWORD *height, OUT DWORD *bpp)
{
	if (index >= m_ResolutionCount)
		return false;

	*width = m_ResolutionList[index].width;
	*height = m_ResolutionList[index].height;
	*bpp = m_ResolutionList[index].bpp;
	return true;
}

bool CPythonSystem::GetFrequency(int index, int freq_index, OUT DWORD *frequncy)
{
	if (index >= m_ResolutionCount)
		return false;

	if (freq_index >= m_ResolutionList[index].frequency_count)
		return false;

	*frequncy = m_ResolutionList[index].frequency[freq_index];
	return true;
}

int	CPythonSystem::GetResolutionIndex(DWORD width, DWORD height, DWORD bit)
{
	DWORD re_width, re_height, re_bit;
	int i = 0;

	while (GetResolution(i, &re_width, &re_height, &re_bit))
	{
		if (re_width == width)
			if (re_height == height)
				if (re_bit == bit)
					return i;
		i++;
	}

	return 0;
}

int	CPythonSystem::GetFrequencyIndex(int res_index, DWORD frequency)
{
	DWORD re_frequency;
	int i = 0;

	while (GetFrequency(res_index, i, &re_frequency))
	{
		if (re_frequency == frequency)
			return i;

		i++;
	}

	return 0;
}

DWORD CPythonSystem::GetWidth()
{
	return m_Config.width;
}

DWORD CPythonSystem::GetHeight()
{
	return m_Config.height;
}
DWORD CPythonSystem::GetBPP()
{
	return m_Config.bpp;
}
DWORD CPythonSystem::GetFrequency()
{
	return m_Config.frequency;
}

int CPythonSystem::GetRenderAPI()
{
	return NormalizeRenderAPI(m_Config.iRenderAPI);
}

void CPythonSystem::SetRenderAPI(int iRenderAPI)
{
	m_Config.iRenderAPI = NormalizeRenderAPI(iRenderAPI);
}

int CPythonSystem::GetShaderCacheVersion()
{
	return NormalizeShaderCacheVersion(m_Config.iShaderCacheVersion);
}

void CPythonSystem::SetShaderCacheVersion(int iVersion)
{
	m_Config.iShaderCacheVersion = NormalizeShaderCacheVersion(iVersion);
}

int CPythonSystem::GetRenderFPSLimit()
{
	return NormalizeRenderFPSLimit(m_Config.iRenderFPSLimit);
}

void CPythonSystem::SetRenderFPSLimit(int iFPSLimit)
{
	m_Config.iRenderFPSLimit = NormalizeRenderFPSLimit(iFPSLimit);
}

bool CPythonSystem::IsVSyncEnabled()
{
	return m_Config.bVSync;
}

void CPythonSystem::SetVSyncEnabled(bool isEnabled)
{
	m_Config.bVSync = isEnabled;
}

bool CPythonSystem::IsDX11ExperimentalPresentEnabled()
{
	return m_Config.bDX11ExperimentalPresent;
}

void CPythonSystem::SetDX11ExperimentalPresentEnabled(bool isEnabled)
{
	m_Config.bDX11ExperimentalPresent = isEnabled;
}

bool CPythonSystem::IsDX11FirstPassActiveEnabled()
{
	return m_Config.bDX11FirstPassActive;
}

void CPythonSystem::SetDX11FirstPassActiveEnabled(bool isEnabled)
{
	m_Config.bDX11FirstPassActive = isEnabled;
}

bool CPythonSystem::IsDX11NativeVisibleEnabled()
{
	return m_Config.bDX11NativeVisible;
}

void CPythonSystem::SetDX11NativeVisibleEnabled(bool isEnabled)
{
	m_Config.bDX11NativeVisible = isEnabled;
}

bool CPythonSystem::IsDX11NativeUIMinimalEnabled()
{
	return m_Config.bDX11NativeUIMinimal;
}

void CPythonSystem::SetDX11NativeUIMinimalEnabled(bool isEnabled)
{
	m_Config.bDX11NativeUIMinimal = isEnabled;
}

bool CPythonSystem::IsDX11NativeWorldMinimalEnabled()
{
	return m_Config.bDX11NativeWorldMinimal;
}

void CPythonSystem::SetDX11NativeWorldMinimalEnabled(bool isEnabled)
{
	m_Config.bDX11NativeWorldMinimal = isEnabled;
}

bool CPythonSystem::IsDX11NativeWorldForceVisibleEnabled()
{
	return m_Config.bDX11NativeWorldForceVisible;
}

void CPythonSystem::SetDX11NativeWorldForceVisibleEnabled(bool isEnabled)
{
	m_Config.bDX11NativeWorldForceVisible = isEnabled;
}

bool CPythonSystem::IsDX11NativeWorldAutoGateEnabled()
{
	return m_Config.bDX11NativeWorldAutoGate;
}

void CPythonSystem::SetDX11NativeWorldAutoGateEnabled(bool isEnabled)
{
	m_Config.bDX11NativeWorldAutoGate = isEnabled;
}

bool CPythonSystem::IsDX11StrictNativeOnlyEnabled()
{
	return m_Config.bDX11StrictNativeOnly;
}

void CPythonSystem::SetDX11StrictNativeOnlyEnabled(bool isEnabled)
{
	m_Config.bDX11StrictNativeOnly = isEnabled;
}

bool CPythonSystem::IsDX11DisableDX9CompatDeviceEnabled()
{
	return m_Config.bDX11DisableDX9CompatDevice;
}

void CPythonSystem::SetDX11DisableDX9CompatDeviceEnabled(bool isEnabled)
{
	m_Config.bDX11DisableDX9CompatDevice = isEnabled;
}

bool CPythonSystem::IsDX11TerrainStabilizationModeEnabled()
{
	return m_Config.bDX11TerrainStabilizationMode;
}

void CPythonSystem::SetDX11TerrainStabilizationModeEnabled(bool isEnabled)
{
	m_Config.bDX11TerrainStabilizationMode = isEnabled;
}

int CPythonSystem::GetDX11TexturePipelineMode()
{
	return NormalizeDX11TexturePipelineMode(m_Config.iDX11TexturePipelineMode);
}

void CPythonSystem::SetDX11TexturePipelineMode(int iMode)
{
	m_Config.iDX11TexturePipelineMode = NormalizeDX11TexturePipelineMode(iMode);
}

bool CPythonSystem::IsDX11VisibleBootstrapEnabled()
{
	return m_Config.bDX11VisibleBootstrap;
}

void CPythonSystem::SetDX11VisibleBootstrapEnabled(bool isEnabled)
{
	m_Config.bDX11VisibleBootstrap = isEnabled;
}

bool CPythonSystem::IsDX11UIPassOnlyEnabled()
{
	return m_Config.bDX11UIPassOnly;
}

void CPythonSystem::SetDX11UIPassOnlyEnabled(bool isEnabled)
{
	m_Config.bDX11UIPassOnly = isEnabled;
}

bool CPythonSystem::IsDX11UINativeTestEnabled()
{
	return m_Config.bDX11UINativeTest;
}

void CPythonSystem::SetDX11UINativeTestEnabled(bool isEnabled)
{
	m_Config.bDX11UINativeTest = isEnabled;
}

bool CPythonSystem::IsDX11UITextureTestEnabled()
{
	return m_Config.bDX11UITextureTest;
}

void CPythonSystem::SetDX11UITextureTestEnabled(bool isEnabled)
{
	m_Config.bDX11UITextureTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldDepthTestEnabled()
{
	return m_Config.bDX11WorldDepthTest;
}

void CPythonSystem::SetDX11WorldDepthTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldDepthTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldBatchTestEnabled()
{
	return m_Config.bDX11WorldBatchTest;
}

void CPythonSystem::SetDX11WorldBatchTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldBatchTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldSpriteTestEnabled()
{
	return m_Config.bDX11WorldSpriteTest;
}

void CPythonSystem::SetDX11WorldSpriteTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldSpriteTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldStateTestEnabled()
{
	return m_Config.bDX11WorldStateTest;
}

void CPythonSystem::SetDX11WorldStateTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldStateTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldPassesTestEnabled()
{
	return m_Config.bDX11WorldPassesTest;
}

void CPythonSystem::SetDX11WorldPassesTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldPassesTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldBridgeTestEnabled()
{
	return m_Config.bDX11WorldBridgeTest;
}

void CPythonSystem::SetDX11WorldBridgeTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldBridgeTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldSubsystemTestEnabled()
{
	return m_Config.bDX11WorldSubsystemTest;
}

void CPythonSystem::SetDX11WorldSubsystemTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldSubsystemTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldRealtimeTestEnabled()
{
	return m_Config.bDX11WorldRealtimeTest;
}

void CPythonSystem::SetDX11WorldRealtimeTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldRealtimeTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldMetricsTestEnabled()
{
	return m_Config.bDX11WorldMetricsTest;
}

void CPythonSystem::SetDX11WorldMetricsTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldMetricsTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldInstanceFeedTestEnabled()
{
	return m_Config.bDX11WorldInstanceFeedTest;
}

void CPythonSystem::SetDX11WorldInstanceFeedTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldInstanceFeedTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldFinalcheckTestEnabled()
{
	return m_Config.bDX11WorldFinalcheckTest;
}

void CPythonSystem::SetDX11WorldFinalcheckTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldFinalcheckTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldHandoffTestEnabled()
{
	return m_Config.bDX11WorldHandoffTest;
}

void CPythonSystem::SetDX11WorldHandoffTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldHandoffTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldSwapchainTestEnabled()
{
	return m_Config.bDX11WorldSwapchainTest;
}

void CPythonSystem::SetDX11WorldSwapchainTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldSwapchainTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldPresentPathTestEnabled()
{
	return m_Config.bDX11WorldPresentPathTest;
}

void CPythonSystem::SetDX11WorldPresentPathTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldPresentPathTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldVisiblePass1TestEnabled()
{
	return m_Config.bDX11WorldVisiblePass1Test;
}

void CPythonSystem::SetDX11WorldVisiblePass1TestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldVisiblePass1Test = isEnabled;
}

bool CPythonSystem::IsDX11WorldComposerTestEnabled()
{
	return m_Config.bDX11WorldComposerTest;
}

void CPythonSystem::SetDX11WorldComposerTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldComposerTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldScenegraphTestEnabled()
{
	return m_Config.bDX11WorldScenegraphTest;
}

void CPythonSystem::SetDX11WorldScenegraphTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldScenegraphTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldPipelineTestEnabled()
{
	return m_Config.bDX11WorldPipelineTest;
}

void CPythonSystem::SetDX11WorldPipelineTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldPipelineTest = isEnabled;
}

bool CPythonSystem::IsDX11WorldFramegraphTestEnabled()
{
	return m_Config.bDX11WorldFramegraphTest;
}

void CPythonSystem::SetDX11WorldFramegraphTestEnabled(bool isEnabled)
{
	m_Config.bDX11WorldFramegraphTest = isEnabled;
}

int CPythonSystem::GetPerfProfile()
{
	return NormalizePerfProfile(m_Config.iPerfProfile);
}

void CPythonSystem::SetPerfProfile(int iProfile)
{
	m_Config.iPerfProfile = NormalizePerfProfile(iProfile);
}

bool CPythonSystem::IsFXAdaptiveEnabled()
{
	return m_Config.bFXAdaptive;
}

void CPythonSystem::SetFXAdaptiveEnabled(bool isEnabled)
{
	m_Config.bFXAdaptive = isEnabled;
}

bool CPythonSystem::IsAnimLODEnabled()
{
	return m_Config.bAnimLOD;
}

void CPythonSystem::SetAnimLODEnabled(bool isEnabled)
{
	m_Config.bAnimLOD = isEnabled;
}

bool CPythonSystem::IsTextTailOptEnabled()
{
	return m_Config.bTextTailOpt;
}

void CPythonSystem::SetTextTailOptEnabled(bool isEnabled)
{
	m_Config.bTextTailOpt = isEnabled;
}

int CPythonSystem::GetShadowCadence()
{
	return NormalizeShadowCadence(m_Config.iShadowCadence);
}

void CPythonSystem::SetShadowCadence(int iCadence)
{
	m_Config.iShadowCadence = NormalizeShadowCadence(iCadence);
}

int CPythonSystem::GetFXStrideBias()
{
	return NormalizeFXStrideBias(m_Config.iFXStrideBias);
}

void CPythonSystem::SetFXStrideBias(int iBias)
{
	m_Config.iFXStrideBias = NormalizeFXStrideBias(iBias);
}

bool CPythonSystem::IsShadowDynamicBoostEnabled()
{
	return m_Config.bShadowDynamicBoost;
}

void CPythonSystem::SetShadowDynamicBoostEnabled(bool isEnabled)
{
	m_Config.bShadowDynamicBoost = isEnabled;
}

bool CPythonSystem::IsTextTailGridOptEnabled()
{
	return m_Config.bTextTailGridOpt;
}

void CPythonSystem::SetTextTailGridOptEnabled(bool isEnabled)
{
	m_Config.bTextTailGridOpt = isEnabled;
}

int CPythonSystem::GetTextTailOptRange()
{
	return NormalizeTextTailOptRange(m_Config.iTextTailOptRange);
}

void CPythonSystem::SetTextTailOptRange(int iRange)
{
	m_Config.iTextTailOptRange = NormalizeTextTailOptRange(iRange);
}

bool CPythonSystem::IsNoSoundCard()
{
	return m_Config.bNoSoundCard;
}

bool CPythonSystem::IsSoftwareCursor()
{
	return m_Config.is_software_cursor;
}

float CPythonSystem::GetMusicVolume()
{
	return m_Config.music_volume;
}

float CPythonSystem::GetSoundVolume()
{
	return m_Config.voice_volume;
}

void CPythonSystem::SetMusicVolume(float fVolume)
{
	m_Config.music_volume = fVolume;
}

void CPythonSystem::SetSoundVolume(float fVolume)
{
	m_Config.voice_volume = fVolume;
}

int CPythonSystem::GetDistance()
{
	return m_Config.iDistance;
}

int CPythonSystem::GetShadowLevel()
{
	return m_Config.iShadowLevel;
}

void CPythonSystem::SetShadowLevel(unsigned int level)
{
	m_Config.iShadowLevel = MIN(level, 5);
	CPythonBackground::instance().RefreshShadowLevel();
}

// MR-14: Fog update by Alaric
int CPythonSystem::GetFogLevel()
{
	return m_Config.iFogLevel;
}

void CPythonSystem::SetFogLevel(unsigned int level)
{
	m_Config.iFogLevel = MIN(level, 2);
}
// MR-14: -- END OF -- Fog update by Alaric

int CPythonSystem::IsSaveID()
{
	return m_Config.isSaveID;
}

const char * CPythonSystem::GetSaveID()
{
	return m_Config.SaveID;
}

bool CPythonSystem::isViewCulling()
{
	return m_Config.is_object_culling;
}

void CPythonSystem::SetSaveID(int iValue, const char * c_szSaveID)
{
	if (iValue != 1)
		return;
	
	m_Config.isSaveID = iValue;
	strncpy(m_Config.SaveID, c_szSaveID, sizeof(m_Config.SaveID) - 1);
}

CPythonSystem::TConfig * CPythonSystem::GetConfig()
{
	return &m_Config;
}

void CPythonSystem::SetConfig(TConfig * pNewConfig)
{
	m_Config = *pNewConfig;
	m_Config.iDX11TexturePipelineMode = NormalizeDX11TexturePipelineMode(m_Config.iDX11TexturePipelineMode);
}

void CPythonSystem::SetDefaultConfig()
{
	memset(&m_Config, 0, sizeof(m_Config));

	m_Config.width				= 1024;
	m_Config.height				= 768;
	m_Config.bpp				= 32;

	m_Config.bWindowed			= false;
	// Full DX11 policy: default runtime boots with DX11 renderer.
	m_Config.iRenderAPI			= 11;
	m_Config.iShaderCacheVersion = 1;
	m_Config.iRenderFPSLimit	= 60;
	m_Config.bVSync				= true;
	m_Config.bDX11ExperimentalPresent = false;
	m_Config.bDX11FirstPassActive = false;
	m_Config.bDX11NativeVisible = false;
	m_Config.bDX11NativeUIMinimal = false;
	m_Config.bDX11NativeWorldMinimal = false;
	m_Config.bDX11NativeWorldForceVisible = false;
	m_Config.bDX11NativeWorldAutoGate = true;
	// Full DX11 policy: default runtime uses strict native-only path.
	m_Config.bDX11StrictNativeOnly = true;
	m_Config.bDX11DisableDX9CompatDevice = false;
	m_Config.bDX11TerrainStabilizationMode = false;
	m_Config.iDX11TexturePipelineMode = 0;
	m_Config.bDX11VisibleBootstrap = false;
	m_Config.bDX11UIPassOnly = false;
	m_Config.bDX11UINativeTest = false;
	m_Config.bDX11UITextureTest = false;
	m_Config.bDX11WorldDepthTest = false;
	m_Config.bDX11WorldBatchTest = false;
	m_Config.bDX11WorldSpriteTest = false;
	m_Config.bDX11WorldStateTest = false;
	m_Config.bDX11WorldPassesTest = false;
	m_Config.bDX11WorldBridgeTest = false;
	m_Config.bDX11WorldSubsystemTest = false;
	m_Config.bDX11WorldRealtimeTest = false;
	m_Config.bDX11WorldMetricsTest = false;
	m_Config.bDX11WorldInstanceFeedTest = false;
	m_Config.bDX11WorldFinalcheckTest = false;
	m_Config.bDX11WorldHandoffTest = false;
	m_Config.bDX11WorldSwapchainTest = false;
	m_Config.bDX11WorldPresentPathTest = false;
	m_Config.bDX11WorldVisiblePass1Test = true;
	m_Config.bDX11WorldComposerTest = false;
	m_Config.bDX11WorldScenegraphTest = false;
	m_Config.bDX11WorldPipelineTest = false;
	m_Config.bDX11WorldFramegraphTest = false;
	m_Config.iPerfProfile		= 1;
	m_Config.bFXAdaptive		= true;
	m_Config.bAnimLOD			= true;
	m_Config.bTextTailOpt		= true;
	m_Config.iShadowCadence		= 2;
	m_Config.iFXStrideBias		= 1;
	m_Config.bShadowDynamicBoost = true;
	m_Config.bTextTailGridOpt	= true;
	m_Config.iTextTailOptRange	= 3500;

	m_Config.is_software_cursor	= false;
	m_Config.is_object_culling	= true;
	m_Config.iDistance			= 3;

	m_Config.gamma				= 3;
	m_Config.music_volume		= 1.0f;
	m_Config.voice_volume		= 1.0f;

	m_Config.bDecompressDDS		= 0;
	m_Config.bSoftwareTiling	= 0;
	m_Config.iShadowLevel		= 3;
	// MR-14: Fog update by Alaric
	m_Config.iFogLevel			= 0;
	// MR-14: -- END OF -- Fog update by Alaric
	m_Config.bViewChat			= true;
	m_Config.bAlwaysShowName	= DEFAULT_VALUE_ALWAYS_SHOW_NAME;
	m_Config.bShowDamage		= true;
	m_Config.bShowSalesText		= true;
}

bool CPythonSystem::IsWindowed()
{
	return m_Config.bWindowed;
}

bool CPythonSystem::IsViewChat()
{
	return m_Config.bViewChat;
}

void CPythonSystem::SetViewChatFlag(int iFlag)
{
	m_Config.bViewChat = iFlag == 1 ? true : false;
}

bool CPythonSystem::IsAlwaysShowName()
{
	return m_Config.bAlwaysShowName;
}

void CPythonSystem::SetAlwaysShowNameFlag(int iFlag)
{
	m_Config.bAlwaysShowName = iFlag == 1 ? true : false;
}

bool CPythonSystem::IsShowDamage()
{
	return m_Config.bShowDamage;
}

void CPythonSystem::SetShowDamageFlag(int iFlag)
{
	m_Config.bShowDamage = iFlag == 1 ? true : false;
}

bool CPythonSystem::IsShowSalesText()
{
	return m_Config.bShowSalesText;
}

void CPythonSystem::SetShowSalesTextFlag(int iFlag)
{
	m_Config.bShowSalesText = iFlag == 1 ? true : false;
}

bool CPythonSystem::IsAutoTiling()
{
	if (m_Config.bSoftwareTiling == 0)
		return true;

	return false;
}

void CPythonSystem::SetSoftwareTiling(bool isEnable)
{
	if (isEnable)
		m_Config.bSoftwareTiling=1;
	else
		m_Config.bSoftwareTiling=2;
}

bool CPythonSystem::IsSoftwareTiling()
{
	if (m_Config.bSoftwareTiling==1)
		return true;

	return false;
}

bool CPythonSystem::IsUseDefaultIME()
{
	return m_Config.bUseDefaultIME;
}

bool CPythonSystem::LoadConfig()
{
	FILE * fp = NULL;

	if (NULL == (fp = fopen("config/metin2.cfg", "rt")))
		return false;

	char buf[256];
	char command[256];
	char value[256];
	bool bSeenDX11NativeWorldAutoGate = false;
	bool bSeenDX11StrictNativeOnly = false;
	bool bSeenDX11DisableDX9CompatDevice = false;
	bool bSeenDX11TerrainStabilizationMode = false;
	bool bSeenDX11TexturePipelineMode = false;

	while (fgets(buf, 256, fp))
	{
		if (sscanf(buf, " %s %s\n", command, value) == EOF)
			break;

		if (!stricmp(command, "WIDTH"))
			m_Config.width		= atoi(value);
		else if (!stricmp(command, "HEIGHT"))
			m_Config.height	= atoi(value);
		else if (!stricmp(command, "BPP"))
			m_Config.bpp		= atoi(value);
		else if (!stricmp(command, "FREQUENCY"))
			m_Config.frequency = atoi(value);
		else if (!stricmp(command, "SOFTWARE_CURSOR"))
			m_Config.is_software_cursor = atoi(value) ? true : false;
		else if (!stricmp(command, "OBJECT_CULLING"))
			m_Config.is_object_culling = atoi(value) ? true : false;
		else if (!stricmp(command, "VISIBILITY"))
			m_Config.iDistance = atoi(value);
		else if (!stricmp(command, "MUSIC_VOLUME"))
			m_Config.music_volume = atof(value);
		else if (!stricmp(command, "VOICE_VOLUME"))
			m_Config.voice_volume = atof(value);
		else if (!stricmp(command, "GAMMA"))
			m_Config.gamma = atoi(value);
		else if (!stricmp(command, "IS_SAVE_ID"))
			m_Config.isSaveID = atoi(value);
		else if (!stricmp(command, "SAVE_ID"))
			strncpy(m_Config.SaveID, value, 20);
		else if (!stricmp(command, "PRE_LOADING_DELAY_TIME"))
			g_iLoadingDelayTime = atoi(value);
		else if (!stricmp(command, "WINDOWED"))
		{
			m_Config.bWindowed = atoi(value) == 1 ? true : false;
		}
		else if (!stricmp(command, "USE_DEFAULT_IME"))
			m_Config.bUseDefaultIME = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "SOFTWARE_TILING"))
			m_Config.bSoftwareTiling = atoi(value);
		else if (!stricmp(command, "SHADOW_LEVEL"))
			m_Config.iShadowLevel = atoi(value);
		// MR-14: Fog update by Alaric
		else if (!stricmp(command, "FOG_LEVEL"))
			m_Config.iFogLevel = atoi(value);
		// MR-14: -- END OF -- Fog update by Alaric
		else if (!stricmp(command, "DECOMPRESSED_TEXTURE"))
			m_Config.bDecompressDDS = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "NO_SOUND_CARD"))
			m_Config.bNoSoundCard = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "VIEW_CHAT"))
			m_Config.bViewChat = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "ALWAYS_VIEW_NAME"))
			m_Config.bAlwaysShowName = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "SHOW_DAMAGE"))
			m_Config.bShowDamage = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "SHOW_SALESTEXT"))
			m_Config.bShowSalesText = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "RENDER_API"))
			m_Config.iRenderAPI = ParseRenderAPI(value);
		else if (!stricmp(command, "SHADER_CACHE_VERSION"))
			m_Config.iShaderCacheVersion = NormalizeShaderCacheVersion(atoi(value));
		else if (!stricmp(command, "RENDER_FPS_LIMIT"))
			m_Config.iRenderFPSLimit = NormalizeRenderFPSLimit(atoi(value));
		else if (!stricmp(command, "VSYNC"))
			m_Config.bVSync = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_EXPERIMENTAL_PRESENT"))
			m_Config.bDX11ExperimentalPresent = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_FIRST_PASS_ACTIVE"))
			m_Config.bDX11FirstPassActive = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_NATIVE_VISIBLE"))
			m_Config.bDX11NativeVisible = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_NATIVE_UI_MINIMAL"))
			m_Config.bDX11NativeUIMinimal = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_NATIVE_WORLD_MINIMAL"))
			m_Config.bDX11NativeWorldMinimal = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_NATIVE_WORLD_FORCE_VISIBLE"))
			m_Config.bDX11NativeWorldForceVisible = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_NATIVE_WORLD_AUTOGATE"))
		{
			bSeenDX11NativeWorldAutoGate = true;
			m_Config.bDX11NativeWorldAutoGate = atoi(value) == 1 ? true : false;
		}
		else if (!stricmp(command, "DX11_STRICT_NATIVE_ONLY"))
		{
			bSeenDX11StrictNativeOnly = true;
			m_Config.bDX11StrictNativeOnly = atoi(value) == 1 ? true : false;
		}
		else if (!stricmp(command, "DX11_DISABLE_DX9_COMPAT_DEVICE"))
		{
			bSeenDX11DisableDX9CompatDevice = true;
			m_Config.bDX11DisableDX9CompatDevice = atoi(value) == 1 ? true : false;
		}
		else if (!stricmp(command, "DX11_TERRAIN_STABILIZATION_MODE"))
		{
			bSeenDX11TerrainStabilizationMode = true;
			m_Config.bDX11TerrainStabilizationMode = atoi(value) == 1 ? true : false;
		}
		else if (!stricmp(command, "DX11_TEXTURE_PIPELINE_MODE"))
		{
			bSeenDX11TexturePipelineMode = true;
			m_Config.iDX11TexturePipelineMode = ParseDX11TexturePipelineMode(value);
		}
		else if (!stricmp(command, "DX11_VISIBLE_BOOTSTRAP"))
			m_Config.bDX11VisibleBootstrap = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_UI_PASS_ONLY"))
			m_Config.bDX11UIPassOnly = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_UI_NATIVE_TEST"))
			m_Config.bDX11UINativeTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_UI_TEXTURE_TEST"))
			m_Config.bDX11UITextureTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_DEPTH_TEST"))
			m_Config.bDX11WorldDepthTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_BATCH_TEST"))
			m_Config.bDX11WorldBatchTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_SPRITE_TEST"))
			m_Config.bDX11WorldSpriteTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_STATE_TEST"))
			m_Config.bDX11WorldStateTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_PASSES_TEST"))
			m_Config.bDX11WorldPassesTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_BRIDGE_TEST"))
			m_Config.bDX11WorldBridgeTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_SUBSYSTEM_TEST"))
			m_Config.bDX11WorldSubsystemTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_REALTIME_TEST"))
			m_Config.bDX11WorldRealtimeTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_METRICS_TEST"))
			m_Config.bDX11WorldMetricsTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_INSTANCE_FEED_TEST"))
			m_Config.bDX11WorldInstanceFeedTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_FINALCHECK_TEST"))
			m_Config.bDX11WorldFinalcheckTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_HANDOFF_TEST"))
			m_Config.bDX11WorldHandoffTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_SWAPCHAIN_TEST"))
			m_Config.bDX11WorldSwapchainTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_PRESENTPATH_TEST"))
			m_Config.bDX11WorldPresentPathTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_VISIBLE_PASS1_TEST"))
			m_Config.bDX11WorldVisiblePass1Test = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_COMPOSER_TEST"))
			m_Config.bDX11WorldComposerTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_SCENEGRAPH_TEST"))
			m_Config.bDX11WorldScenegraphTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_PIPELINE_TEST"))
			m_Config.bDX11WorldPipelineTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "DX11_WORLD_FRAMEGRAPH_TEST"))
			m_Config.bDX11WorldFramegraphTest = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "PERF_PROFILE"))
			m_Config.iPerfProfile = ParsePerfProfile(value);
		else if (!stricmp(command, "FX_ADAPTIVE"))
			m_Config.bFXAdaptive = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "ANIM_LOD"))
			m_Config.bAnimLOD = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "TEXTTAIL_OPT"))
			m_Config.bTextTailOpt = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "SHADOW_CADENCE"))
			m_Config.iShadowCadence = NormalizeShadowCadence(atoi(value));
		else if (!stricmp(command, "FX_STRIDE_BIAS"))
			m_Config.iFXStrideBias = NormalizeFXStrideBias(atoi(value));
		else if (!stricmp(command, "SHADOW_DYNAMIC_BOOST"))
			m_Config.bShadowDynamicBoost = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "TEXTTAIL_GRID_OPT"))
			m_Config.bTextTailGridOpt = atoi(value) == 1 ? true : false;
		else if (!stricmp(command, "TEXTTAIL_OPT_RANGE"))
			m_Config.iTextTailOptRange = NormalizeTextTailOptRange(atoi(value));
	}

	m_Config.iRenderFPSLimit = NormalizeRenderFPSLimit(m_Config.iRenderFPSLimit);
	m_Config.iRenderAPI = NormalizeRenderAPI(m_Config.iRenderAPI);
	m_Config.iShaderCacheVersion = NormalizeShaderCacheVersion(m_Config.iShaderCacheVersion);
	m_Config.iPerfProfile = NormalizePerfProfile(m_Config.iPerfProfile);
	m_Config.iShadowCadence = NormalizeShadowCadence(m_Config.iShadowCadence);
	m_Config.iFXStrideBias = NormalizeFXStrideBias(m_Config.iFXStrideBias);
	m_Config.iTextTailOptRange = NormalizeTextTailOptRange(m_Config.iTextTailOptRange);
	m_Config.iDX11TexturePipelineMode = NormalizeDX11TexturePipelineMode(m_Config.iDX11TexturePipelineMode);

	// Runtime strict policy also enforces DX11-only render API.
	// Keep this independent from build-time strict so config-only mode works too.
	if (m_Config.bDX11StrictNativeOnly && 11 != m_Config.iRenderAPI)
	{
		const int iOldRenderAPI = m_Config.iRenderAPI;
		m_Config.iRenderAPI = 11;
		TraceError("DX11_CONFIG_OVERRIDE key=RENDER_API old=%d new=11 reason=strict_native_only_policy", iOldRenderAPI);
	}

#if defined(DX11_STRICT_ONLY)
	// Compile-time strict build must never boot in DX9 mode, even with stale config.
	if (11 != m_Config.iRenderAPI)
	{
		const int iOldRenderAPI = m_Config.iRenderAPI;
		m_Config.iRenderAPI = 11;
		TraceError("DX11_CONFIG_OVERRIDE key=RENDER_API old=%d new=11 reason=dx11_strict_only_build", iOldRenderAPI);
	}
	if (!m_Config.bDX11StrictNativeOnly)
	{
		m_Config.bDX11StrictNativeOnly = true;
		TraceError("DX11_CONFIG_OVERRIDE key=DX11_STRICT_NATIVE_ONLY old=0 new=1 reason=dx11_strict_only_build");
	}
	if (!m_Config.bDX11DisableDX9CompatDevice)
	{
		m_Config.bDX11DisableDX9CompatDevice = true;
		TraceError("DX11_CONFIG_OVERRIDE key=DX11_DISABLE_DX9_COMPAT_DEVICE old=0 new=1 reason=dx11_strict_only_build");
	}
#endif

	if (11 == m_Config.iRenderAPI)
	{
		if (!bSeenDX11NativeWorldAutoGate)
		{
			m_Config.bDX11NativeWorldAutoGate = true;
			TraceError("DX11_CONFIG_MIGRATION key=DX11_NATIVE_WORLD_AUTOGATE value=1 reason=missing_cfg_key");
		}
		if (!bSeenDX11StrictNativeOnly)
		{
			m_Config.bDX11StrictNativeOnly = true;
			TraceError("DX11_CONFIG_MIGRATION key=DX11_STRICT_NATIVE_ONLY value=1 reason=full_dx11_policy_missing_cfg_key");
		}
		if (!bSeenDX11DisableDX9CompatDevice)
		{
			m_Config.bDX11DisableDX9CompatDevice = true;
			TraceError("DX11_CONFIG_MIGRATION key=DX11_DISABLE_DX9_COMPAT_DEVICE value=1 reason=missing_cfg_key");
		}
		if (!bSeenDX11TerrainStabilizationMode)
		{
			m_Config.bDX11TerrainStabilizationMode = false;
			TraceError("DX11_CONFIG_MIGRATION key=DX11_TERRAIN_STABILIZATION_MODE value=0 reason=missing_cfg_key");
		}
		if (!bSeenDX11TexturePipelineMode)
		{
			m_Config.iDX11TexturePipelineMode = 0;
			TraceError("DX11_CONFIG_MIGRATION key=DX11_TEXTURE_PIPELINE_MODE value=NATIVE reason=missing_cfg_key");
		}

		// Full DX11 migration policy: when renderer is DX11, keep strict native-only enabled.
		// This avoids drifting back to bridge/copy path due stale config.
		if (!m_Config.bDX11StrictNativeOnly)
		{
			m_Config.bDX11StrictNativeOnly = true;
			TraceError("DX11_CONFIG_OVERRIDE key=DX11_STRICT_NATIVE_ONLY old=0 new=1 reason=full_dx11_native_policy");
		}

		// Stabilization mode may keep native-present blocked intentionally.
		if (m_Config.bDX11TerrainStabilizationMode && m_Config.bDX11NativeWorldAutoGate)
		{
			TraceError("DX11_CONFIG_NOTE key=DX11_NATIVE_WORLD_AUTOGATE value=1 note=terrain_stabilization_mode_will_block_auto_present");
		}

		// Strict DX11-only runtime policy: no bridge/copy cutover mode.
		// Enforced by full DX11 policy for DX11 renderer sessions.
		if (m_Config.bDX11StrictNativeOnly)
		{
			if (!m_Config.bDX11NativeVisible)
			{
				m_Config.bDX11NativeVisible = true;
				TraceError("DX11_CONFIG_CONFLICT key=DX11_NATIVE_VISIBLE old=0 new=1 reason=strict_native_only_requires_native_visible");
			}
			if (!m_Config.bDX11NativeWorldAutoGate)
			{
				m_Config.bDX11NativeWorldAutoGate = true;
				TraceError("DX11_CONFIG_OVERRIDE key=DX11_NATIVE_WORLD_AUTOGATE old=0 new=1 reason=strict_native_only_policy");
			}
			if (!m_Config.bDX11DisableDX9CompatDevice)
			{
				m_Config.bDX11DisableDX9CompatDevice = true;
				TraceError("DX11_CONFIG_OVERRIDE key=DX11_DISABLE_DX9_COMPAT_DEVICE old=0 new=1 reason=strict_native_only_policy");
			}
			if (m_Config.bDX11TerrainStabilizationMode)
			{
				m_Config.bDX11TerrainStabilizationMode = false;
				TraceError("DX11_CONFIG_OVERRIDE key=DX11_TERRAIN_STABILIZATION_MODE old=1 new=0 reason=strict_native_only_policy");
			}
		}

		// Full DX11 strict-native policy:
		// UI/world minimal toggles are debug bring-up knobs and can hide content
		// (e.g. terrain/UI) in normal gameplay sessions. Force them OFF here.
		if (m_Config.bDX11StrictNativeOnly)
		{
			if (m_Config.bDX11NativeUIMinimal)
			{
				m_Config.bDX11NativeUIMinimal = false;
				TraceError("DX11_CONFIG_OVERRIDE key=DX11_NATIVE_UI_MINIMAL old=1 new=0 reason=strict_native_only_policy");
			}
			if (m_Config.bDX11NativeWorldMinimal)
			{
				m_Config.bDX11NativeWorldMinimal = false;
				TraceError("DX11_CONFIG_OVERRIDE key=DX11_NATIVE_WORLD_MINIMAL old=1 new=0 reason=strict_native_only_policy");
			}
			if (m_Config.bDX11UIPassOnly)
			{
				m_Config.bDX11UIPassOnly = false;
				TraceError("DX11_CONFIG_OVERRIDE key=DX11_UI_PASS_ONLY old=1 new=0 reason=strict_native_only_policy");
			}
			if (m_Config.bDX11FirstPassActive)
			{
				m_Config.bDX11FirstPassActive = false;
				TraceError("DX11_CONFIG_OVERRIDE key=DX11_FIRST_PASS_ACTIVE old=1 new=0 reason=strict_native_only_policy");
			}
		}

		// Respect explicit user/system configuration for native world autogate.
		// Do not force-enable it here; terrain stabilization and runtime bring-up
		// may intentionally keep autogate disabled.

		// Keep full native texture path on DX11 runtime.
		if (0 != m_Config.iDX11TexturePipelineMode)
		{
			const int iOldMode = m_Config.iDX11TexturePipelineMode;
			m_Config.iDX11TexturePipelineMode = 0;
			TraceError("DX11_CONFIG_OVERRIDE key=DX11_TEXTURE_PIPELINE_MODE old=%d new=%d reason=full_dx11_native_texture_policy", iOldMode, m_Config.iDX11TexturePipelineMode);
		}

		TraceError(
			"DX11_CONFIG_STATE render_api=%d native_world_autogate=%d strict_native_only=%d disable_dx9_compat_device=%d terrain_stabilization=%d texture_pipeline_mode=%d",
			m_Config.iRenderAPI,
			m_Config.bDX11NativeWorldAutoGate ? 1 : 0,
			m_Config.bDX11StrictNativeOnly ? 1 : 0,
			m_Config.bDX11DisableDX9CompatDevice ? 1 : 0,
			m_Config.bDX11TerrainStabilizationMode ? 1 : 0,
			m_Config.iDX11TexturePipelineMode);
	}

	if (m_Config.bWindowed)
	{
		unsigned screen_width = GetSystemMetrics(SM_CXFULLSCREEN);
		unsigned screen_height = GetSystemMetrics(SM_CYFULLSCREEN);

		if (m_Config.width >= screen_width)
		{
			m_Config.width = screen_width;
		}
		// if (m_Config.height >= screen_height)
		// {
			// m_Config.height = screen_height;
		// }
		if (m_Config.height >= screen_height) //Fix
		{
			int config_height = m_Config.height;
			int difference = (config_height-screen_height)+7;
			m_Config.height = config_height - difference;
		}
	}
	
	m_OldConfig = m_Config;

	fclose(fp);

//	Tracef("LoadConfig: Resolution: %dx%d %dBPP %dHZ Software Cursor: %d, Music/Voice Volume: %d/%d Gamma: %d\n",
//		m_Config.width,
//		m_Config.height,
//		m_Config.bpp,
//		m_Config.frequency,
//		m_Config.is_software_cursor,
//		m_Config.music_volume,
//		m_Config.voice_volume,
//		m_Config.gamma);

	return true;
}

bool CPythonSystem::SaveConfig()
{
	FILE *fp;

	if (NULL == (fp = fopen("config/metin2.cfg", "wt")))
		return false;

	fprintf(fp, "WIDTH						%d\n"
				"HEIGHT						%d\n"
				"BPP						%d\n"
				"FREQUENCY					%d\n"
				"SOFTWARE_CURSOR			%d\n"
				"OBJECT_CULLING				%d\n"
				"VISIBILITY					%d\n"
				"MUSIC_VOLUME				%.3f\n"
				"VOICE_VOLUME				%.3f\n"
				"GAMMA						%d\n"
				"IS_SAVE_ID					%d\n"
				"SAVE_ID					%s\n"
				"PRE_LOADING_DELAY_TIME		%d\n"
				"DECOMPRESSED_TEXTURE		%d\n",
				m_Config.width,
				m_Config.height,
				m_Config.bpp,
				m_Config.frequency,
				m_Config.is_software_cursor,
				m_Config.is_object_culling,
				m_Config.iDistance,
				m_Config.music_volume,
				m_Config.voice_volume,
				m_Config.gamma,
				m_Config.isSaveID,
				m_Config.SaveID,
				g_iLoadingDelayTime,
				m_Config.bDecompressDDS);

	if (m_Config.bWindowed == 1)
		fprintf(fp, "WINDOWED			%d\n", m_Config.bWindowed);
	if (m_Config.bViewChat == 0)
		fprintf(fp, "VIEW_CHAT			%d\n", m_Config.bViewChat);
	if (m_Config.bAlwaysShowName != DEFAULT_VALUE_ALWAYS_SHOW_NAME)
		fprintf(fp, "ALWAYS_VIEW_NAME	%d\n", m_Config.bAlwaysShowName);
	if (m_Config.bShowDamage == 0)
		fprintf(fp, "SHOW_DAMAGE		%d\n", m_Config.bShowDamage);
	if (m_Config.bShowSalesText == 0)
		fprintf(fp, "SHOW_SALESTEXT		%d\n", m_Config.bShowSalesText);

	fprintf(fp, "USE_DEFAULT_IME		%d\n", m_Config.bUseDefaultIME);
	fprintf(fp, "SOFTWARE_TILING		%d\n", m_Config.bSoftwareTiling);
	fprintf(fp, "SHADOW_LEVEL			%d\n", m_Config.iShadowLevel);
	// MR-14: Fog update by Alaric
	fprintf(fp, "FOG_LEVEL				%d\n", m_Config.iFogLevel);
	// MR-14: -- END OF -- Fog update by Alaric
	fprintf(fp, "RENDER_API				%s\n", RenderAPIToString(m_Config.iRenderAPI));
	fprintf(fp, "SHADER_CACHE_VERSION		%d\n", NormalizeShaderCacheVersion(m_Config.iShaderCacheVersion));
	fprintf(fp, "RENDER_FPS_LIMIT		%d\n", NormalizeRenderFPSLimit(m_Config.iRenderFPSLimit));
	fprintf(fp, "VSYNC					%d\n", m_Config.bVSync ? 1 : 0);
	fprintf(fp, "DX11_EXPERIMENTAL_PRESENT	%d\n", m_Config.bDX11ExperimentalPresent ? 1 : 0);
	fprintf(fp, "DX11_FIRST_PASS_ACTIVE		%d\n", m_Config.bDX11FirstPassActive ? 1 : 0);
	fprintf(fp, "DX11_NATIVE_VISIBLE		%d\n", m_Config.bDX11NativeVisible ? 1 : 0);
	fprintf(fp, "DX11_NATIVE_UI_MINIMAL		%d\n", m_Config.bDX11NativeUIMinimal ? 1 : 0);
	fprintf(fp, "DX11_NATIVE_WORLD_MINIMAL	%d\n", m_Config.bDX11NativeWorldMinimal ? 1 : 0);
	fprintf(fp, "DX11_NATIVE_WORLD_FORCE_VISIBLE	%d\n", m_Config.bDX11NativeWorldForceVisible ? 1 : 0);
	fprintf(fp, "DX11_NATIVE_WORLD_AUTOGATE	%d\n", m_Config.bDX11NativeWorldAutoGate ? 1 : 0);
	fprintf(fp, "DX11_STRICT_NATIVE_ONLY	%d\n", m_Config.bDX11StrictNativeOnly ? 1 : 0);
	fprintf(fp, "DX11_DISABLE_DX9_COMPAT_DEVICE	%d\n", m_Config.bDX11DisableDX9CompatDevice ? 1 : 0);
	fprintf(fp, "DX11_TERRAIN_STABILIZATION_MODE	%d\n", m_Config.bDX11TerrainStabilizationMode ? 1 : 0);
	fprintf(fp, "DX11_TEXTURE_PIPELINE_MODE	%s\n", DX11TexturePipelineModeToString(m_Config.iDX11TexturePipelineMode));
	fprintf(fp, "DX11_VISIBLE_BOOTSTRAP		%d\n", m_Config.bDX11VisibleBootstrap ? 1 : 0);
	fprintf(fp, "DX11_UI_PASS_ONLY		%d\n", m_Config.bDX11UIPassOnly ? 1 : 0);
	fprintf(fp, "DX11_UI_NATIVE_TEST		%d\n", m_Config.bDX11UINativeTest ? 1 : 0);
	fprintf(fp, "DX11_UI_TEXTURE_TEST		%d\n", m_Config.bDX11UITextureTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_DEPTH_TEST		%d\n", m_Config.bDX11WorldDepthTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_BATCH_TEST		%d\n", m_Config.bDX11WorldBatchTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_SPRITE_TEST		%d\n", m_Config.bDX11WorldSpriteTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_STATE_TEST		%d\n", m_Config.bDX11WorldStateTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_PASSES_TEST		%d\n", m_Config.bDX11WorldPassesTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_BRIDGE_TEST		%d\n", m_Config.bDX11WorldBridgeTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_SUBSYSTEM_TEST	%d\n", m_Config.bDX11WorldSubsystemTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_REALTIME_TEST	%d\n", m_Config.bDX11WorldRealtimeTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_METRICS_TEST	%d\n", m_Config.bDX11WorldMetricsTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_INSTANCE_FEED_TEST	%d\n", m_Config.bDX11WorldInstanceFeedTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_FINALCHECK_TEST	%d\n", m_Config.bDX11WorldFinalcheckTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_HANDOFF_TEST	%d\n", m_Config.bDX11WorldHandoffTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_SWAPCHAIN_TEST	%d\n", m_Config.bDX11WorldSwapchainTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_PRESENTPATH_TEST	%d\n", m_Config.bDX11WorldPresentPathTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_VISIBLE_PASS1_TEST	%d\n", m_Config.bDX11WorldVisiblePass1Test ? 1 : 0);
	fprintf(fp, "DX11_WORLD_COMPOSER_TEST	%d\n", m_Config.bDX11WorldComposerTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_SCENEGRAPH_TEST	%d\n", m_Config.bDX11WorldScenegraphTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_PIPELINE_TEST	%d\n", m_Config.bDX11WorldPipelineTest ? 1 : 0);
	fprintf(fp, "DX11_WORLD_FRAMEGRAPH_TEST	%d\n", m_Config.bDX11WorldFramegraphTest ? 1 : 0);
	fprintf(fp, "PERF_PROFILE			%s\n", PerfProfileToString(m_Config.iPerfProfile));
	fprintf(fp, "FX_ADAPTIVE			%d\n", m_Config.bFXAdaptive ? 1 : 0);
	fprintf(fp, "ANIM_LOD				%d\n", m_Config.bAnimLOD ? 1 : 0);
	fprintf(fp, "TEXTTAIL_OPT			%d\n", m_Config.bTextTailOpt ? 1 : 0);
	fprintf(fp, "SHADOW_CADENCE			%d\n", NormalizeShadowCadence(m_Config.iShadowCadence));
	fprintf(fp, "FX_STRIDE_BIAS			%d\n", NormalizeFXStrideBias(m_Config.iFXStrideBias));
	fprintf(fp, "SHADOW_DYNAMIC_BOOST		%d\n", m_Config.bShadowDynamicBoost ? 1 : 0);
	fprintf(fp, "TEXTTAIL_GRID_OPT		%d\n", m_Config.bTextTailGridOpt ? 1 : 0);
	fprintf(fp, "TEXTTAIL_OPT_RANGE		%d\n", NormalizeTextTailOptRange(m_Config.iTextTailOptRange));
	fprintf(fp, "\n");

	fclose(fp);
	return true;
}

bool CPythonSystem::LoadInterfaceStatus()
{
	FILE * File;
	File = fopen("config/interface.cfg", "rb");

	if (!File)
		return false;

	fread(m_WindowStatus, 1, sizeof(TWindowStatus) * WINDOW_MAX_NUM, File);
	fclose(File);
	return true;
}

void CPythonSystem::SaveInterfaceStatus()
{
	if (!m_poInterfaceHandler)
		return;

	PyCallClassMemberFunc(m_poInterfaceHandler, "OnSaveInterfaceStatus", Py_BuildValue("()"));

	FILE * File;

	File = fopen("config/interface.cfg", "wb");

	if (!File)
	{
		TraceError("Cannot open interface.cfg");
		return;
	}

	fwrite(m_WindowStatus, 1, sizeof(TWindowStatus) * WINDOW_MAX_NUM, File);
	fclose(File);
}

bool CPythonSystem::isInterfaceConfig()
{
	return m_isInterfaceConfig;
}

const CPythonSystem::TWindowStatus & CPythonSystem::GetWindowStatusReference(int iIndex)
{
	return m_WindowStatus[iIndex];
}

void CPythonSystem::ApplyConfig() // ?????? ????????? ?????? ????????? ???????????? ?????? ????????? ?????? ??????.
{
	if (m_OldConfig.gamma != m_Config.gamma)
	{
		float val = 1.0f;
		
		switch (m_Config.gamma)
		{
			case 0: val = 0.4f;	break;
			case 1: val = 0.7f; break;
			case 2: val = 1.0f; break;
			case 3: val = 1.2f; break;
			case 4: val = 1.4f; break;
		}
		
		CPythonGraphic::Instance().SetGamma(val);
	}

	if (m_OldConfig.is_software_cursor != m_Config.is_software_cursor)
	{
		if (m_Config.is_software_cursor)
			CPythonApplication::Instance().SetCursorMode(CPythonApplication::CURSOR_MODE_SOFTWARE);
		else
			CPythonApplication::Instance().SetCursorMode(CPythonApplication::CURSOR_MODE_HARDWARE);
	}

	if (m_OldConfig.iRenderFPSLimit != m_Config.iRenderFPSLimit)
		CPythonApplication::Instance().SetFPS(m_Config.iRenderFPSLimit);

	if (m_OldConfig.bVSync != m_Config.bVSync)
		CPythonApplication::Instance().SetVSync(m_Config.bVSync);

	if (m_OldConfig.bDX11ExperimentalPresent != m_Config.bDX11ExperimentalPresent)
		CPythonApplication::Instance().SetDX11ExperimentalPresent(m_Config.bDX11ExperimentalPresent);

	if (m_OldConfig.iRenderAPI != m_Config.iRenderAPI)
		TraceError("RENDER_API changed from %s to %s. Restart client to apply render backend switch.",
			RenderAPIToString(m_OldConfig.iRenderAPI),
			RenderAPIToString(m_Config.iRenderAPI));

	if (m_OldConfig.iShaderCacheVersion != m_Config.iShaderCacheVersion)
		TraceError("SHADER_CACHE_VERSION changed from %d to %d.",
			NormalizeShaderCacheVersion(m_OldConfig.iShaderCacheVersion),
			NormalizeShaderCacheVersion(m_Config.iShaderCacheVersion));

	if (m_OldConfig.iPerfProfile != m_Config.iPerfProfile ||
		m_OldConfig.bFXAdaptive != m_Config.bFXAdaptive ||
		m_OldConfig.bAnimLOD != m_Config.bAnimLOD ||
		m_OldConfig.bTextTailOpt != m_Config.bTextTailOpt ||
		m_OldConfig.iShadowCadence != m_Config.iShadowCadence ||
		m_OldConfig.iFXStrideBias != m_Config.iFXStrideBias ||
		m_OldConfig.bShadowDynamicBoost != m_Config.bShadowDynamicBoost ||
		m_OldConfig.bTextTailGridOpt != m_Config.bTextTailGridOpt)
	{
		CPythonApplication::Instance().ApplyPerformanceConfig(
			NormalizePerfProfile(m_Config.iPerfProfile),
			m_Config.bFXAdaptive,
			m_Config.bAnimLOD,
			m_Config.bTextTailOpt,
			NormalizeShadowCadence(m_Config.iShadowCadence),
			NormalizeFXStrideBias(m_Config.iFXStrideBias),
			m_Config.bShadowDynamicBoost,
			m_Config.bTextTailGridOpt);
	}

	if (m_OldConfig.iTextTailOptRange != m_Config.iTextTailOptRange)
		CPythonApplication::Instance().SetTextTailOptRange(NormalizeTextTailOptRange(m_Config.iTextTailOptRange));

	m_OldConfig = m_Config;

	ChangeSystem();
}

void CPythonSystem::ChangeSystem()
{
	// Shadow
	/*
	if (m_Config.is_shadow)
		CScreen::SetShadowFlag(true);
	else
		CScreen::SetShadowFlag(false);
	*/
	SoundEngine& rkSndMgr = SoundEngine::Instance();
	/*
	float fMusicVolume;
	if (0 == m_Config.music_volume)
		fMusicVolume = 0.0f;
	else
		fMusicVolume= (float)pow(10.0f, (-1.0f + (float)m_Config.music_volume / 5.0f));
		*/
	rkSndMgr.SetMusicVolume(m_Config.music_volume);

	/*
	float fVoiceVolume;
	if (0 == m_Config.voice_volume)
		fVoiceVolume = 0.0f;
	else
		fVoiceVolume = (float)pow(10.0f, (-1.0f + (float)m_Config.voice_volume / 5.0f));
	*/
	rkSndMgr.SetSoundVolume(m_Config.voice_volume);
}

void CPythonSystem::Clear()
{
	SetInterfaceHandler(NULL);
}

CPythonSystem::CPythonSystem()
{
	memset(&m_Config, 0, sizeof(TConfig));

	m_poInterfaceHandler = NULL;

	SetDefaultConfig();

	LoadConfig();

	ChangeSystem();

	if (LoadInterfaceStatus())
		m_isInterfaceConfig = true;
	else
		m_isInterfaceConfig = false;
}

CPythonSystem::~CPythonSystem()
{
	assert(m_poInterfaceHandler==NULL && "CPythonSystem MUST CLEAR!");
}

