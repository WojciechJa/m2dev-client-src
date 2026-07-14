#include "StdAfx.h"
#include "PythonSystem.h"
#include "PythonApplication.h"

PyObject * systemGetWidth(PyObject* poSelf, PyObject* poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetWidth());
}

PyObject * systemGetHeight(PyObject* poSelf, PyObject* poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetHeight());
}

PyObject * systemSetInterfaceHandler(PyObject* poSelf, PyObject* poArgs)
{
	PyObject* poHandler;
	if (!PyTuple_GetObject(poArgs, 0, &poHandler))
		return Py_BuildException();

	CPythonSystem::Instance().SetInterfaceHandler(poHandler);
	return Py_BuildNone();
}

PyObject * systemDestroyInterfaceHandler(PyObject* poSelf, PyObject* poArgs)
{
	CPythonSystem::Instance().DestroyInterfaceHandler();
	return Py_BuildNone();
}

PyObject * systemReserveResource(PyObject* poSelf, PyObject* poArgs)
{
	std::set<std::string> ResourceSet;
	CResourceManager::Instance().PushBackgroundLoadingSet(ResourceSet);
	return Py_BuildNone();
}

PyObject * systemisInterfaceConfig(PyObject* poSelf, PyObject* poArgs)
{
	int isInterfaceConfig = CPythonSystem::Instance().isInterfaceConfig();
	return Py_BuildValue("i", isInterfaceConfig);
}

PyObject * systemSaveWindowStatus(PyObject* poSelf, PyObject* poArgs)
{
	int iIndex;
	if (!PyTuple_GetInteger(poArgs, 0, &iIndex))
		return Py_BuildException();

	int iVisible;
	if (!PyTuple_GetInteger(poArgs, 1, &iVisible))
		return Py_BuildException();

	int iMinimized;
	if (!PyTuple_GetInteger(poArgs, 2, &iMinimized))
		return Py_BuildException();

	int ix;
	if (!PyTuple_GetInteger(poArgs, 3, &ix))
		return Py_BuildException();

	int iy;
	if (!PyTuple_GetInteger(poArgs, 4, &iy))
		return Py_BuildException();

	int iHeight;
	if (!PyTuple_GetInteger(poArgs, 5, &iHeight))
		return Py_BuildException();

	CPythonSystem::Instance().SaveWindowStatus(iIndex, iVisible, iMinimized, ix, iy, iHeight);
	return Py_BuildNone();
}

PyObject * systemGetWindowStatus(PyObject* poSelf, PyObject* poArgs)
{
	int iIndex;
	if (!PyTuple_GetInteger(poArgs, 0, &iIndex))
		return Py_BuildException();

	const CPythonSystem::TWindowStatus & c_rWindowStatus = CPythonSystem::Instance().GetWindowStatusReference(iIndex);
	return Py_BuildValue("iiiii", c_rWindowStatus.isVisible,
								  c_rWindowStatus.isMinimized,
								  c_rWindowStatus.ixPosition,
								  c_rWindowStatus.iyPosition,
								  c_rWindowStatus.iHeight);
}

PyObject * systemGetConfig(PyObject * poSelf, PyObject * poArgs)
{
	CPythonSystem::TConfig *tmp = CPythonSystem::Instance().GetConfig();

	int iRes = CPythonSystem::Instance().GetResolutionIndex(tmp->width, tmp->height, tmp->bpp);
	int iFrequency = CPythonSystem::Instance().GetFrequencyIndex(iRes, tmp->frequency);

	return Py_BuildValue("iiiiiiii",  iRes,
									  iFrequency,
									  tmp->is_software_cursor,
									  tmp->is_object_culling,
									  tmp->music_volume,
									  tmp->voice_volume,
									  tmp->gamma,
									  tmp->iDistance);
}

PyObject * systemSetSaveID(PyObject * poSelf, PyObject * poArgs)
{
	int iValue;
	if (!PyTuple_GetInteger(poArgs, 0, &iValue))
		return Py_BuildException();

	char * szSaveID;
	if (!PyTuple_GetString(poArgs, 1, &szSaveID))
		return Py_BuildException();

	CPythonSystem::Instance().SetSaveID(iValue, szSaveID);
	return Py_BuildNone();
}

PyObject * systemisSaveID(PyObject * poSelf, PyObject * poArgs)
{
	int value = CPythonSystem::Instance().IsSaveID();
	return Py_BuildValue("i", value);
}

PyObject * systemGetSaveID(PyObject * poSelf, PyObject * poArgs)
{
	const char * c_szSaveID = CPythonSystem::Instance().GetSaveID();
	return Py_BuildValue("s", c_szSaveID);
}

PyObject * systemGetMusicVolume(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("f", CPythonSystem::Instance().GetMusicVolume());
}

PyObject * systemGetSoundVolume(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("f", CPythonSystem::Instance().GetSoundVolume());
}

PyObject * systemSetMusicVolume(PyObject * poSelf, PyObject * poArgs)
{
	float fVolume;
	if (!PyTuple_GetFloat(poArgs, 0, &fVolume))
		return Py_BuildException();

	CPythonSystem::Instance().SetMusicVolume(fVolume);
	return Py_BuildNone();
}

PyObject * systemSetSoundVolume(PyObject * poSelf, PyObject * poArgs)
{
	float fVolume;
	if (!PyTuple_GetFloat(poArgs, 0, &fVolume))
		return Py_BuildException();

	CPythonSystem::Instance().SetSoundVolume(fVolume);
	return Py_BuildNone();
}

PyObject * systemGetRenderFPSLimit(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetRenderFPSLimit());
}

PyObject * systemGetRenderAPI(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetRenderAPI());
}

PyObject * systemSetRenderAPI(PyObject * poSelf, PyObject * poArgs)
{
	int iRenderAPI;
	if (!PyTuple_GetInteger(poArgs, 0, &iRenderAPI))
		return Py_BuildException();

	CPythonSystem::Instance().SetRenderAPI(iRenderAPI);
	return Py_BuildNone();
}

PyObject * systemGetShaderCacheVersion(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetShaderCacheVersion());
}

PyObject * systemSetShaderCacheVersion(PyObject * poSelf, PyObject * poArgs)
{
	int iVersion;
	if (!PyTuple_GetInteger(poArgs, 0, &iVersion))
		return Py_BuildException();

	CPythonSystem::Instance().SetShaderCacheVersion(iVersion);
	return Py_BuildNone();
}

PyObject * systemSetRenderFPSLimit(PyObject * poSelf, PyObject * poArgs)
{
	int iFPSLimit;
	if (!PyTuple_GetInteger(poArgs, 0, &iFPSLimit))
		return Py_BuildException();

	CPythonSystem::Instance().SetRenderFPSLimit(iFPSLimit);
	CPythonApplication::Instance().SetFPS(CPythonSystem::Instance().GetRenderFPSLimit());
	return Py_BuildNone();
}

PyObject * systemGetVSync(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsVSyncEnabled() ? 1 : 0);
}

PyObject * systemGetDX11ExperimentalPresent(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11ExperimentalPresentEnabled() ? 1 : 0);
}

PyObject * systemGetDX11NativeWorldAutoGate(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11NativeWorldAutoGateEnabled() ? 1 : 0);
}

PyObject * systemGetDX11StrictNativeOnly(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11StrictNativeOnlyEnabled() ? 1 : 0);
}

PyObject * systemGetDX11DisableDX9CompatDevice(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11DisableDX9CompatDeviceEnabled() ? 1 : 0);
}

PyObject * systemGetDX11TerrainStabilizationMode(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11TerrainStabilizationModeEnabled() ? 1 : 0);
}

PyObject * systemGetDX11TexturePipelineMode(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetDX11TexturePipelineMode());
}

PyObject * systemGetDX11UIPassOnly(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11UIPassOnlyEnabled() ? 1 : 0);
}

PyObject * systemGetDX11UINativeTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11UINativeTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11UITextureTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11UITextureTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldDepthTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldDepthTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldBatchTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldBatchTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldSpriteTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldSpriteTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldStateTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldStateTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldPassesTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldPassesTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldBridgeTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldBridgeTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldSubsystemTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldSubsystemTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldRealtimeTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldRealtimeTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldMetricsTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldMetricsTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldInstanceFeedTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldInstanceFeedTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldFinalcheckTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldFinalcheckTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldHandoffTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldHandoffTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldSwapchainTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldSwapchainTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldPresentPathTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldPresentPathTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldVisiblePass1Test(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldVisiblePass1TestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldComposerTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldComposerTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldScenegraphTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldScenegraphTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldPipelineTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldPipelineTestEnabled() ? 1 : 0);
}

PyObject * systemGetDX11WorldFramegraphTest(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsDX11WorldFramegraphTestEnabled() ? 1 : 0);
}

PyObject * systemSetVSync(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	const bool isEnabled = iEnabled ? true : false;
	const bool oldState = CPythonSystem::Instance().IsVSyncEnabled();
	CPythonSystem::Instance().SetVSyncEnabled(isEnabled);
	const bool applied = CPythonApplication::Instance().SetVSync(isEnabled);
	if (!applied)
		CPythonSystem::Instance().SetVSyncEnabled(oldState);
	return Py_BuildValue("i", applied ? 1 : 0);
}

PyObject * systemSetDX11ExperimentalPresent(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	const bool isEnabled = iEnabled ? true : false;
	CPythonSystem::Instance().SetDX11ExperimentalPresentEnabled(isEnabled);
	CPythonApplication::Instance().SetDX11ExperimentalPresent(isEnabled);
	return Py_BuildNone();
}

PyObject * systemSetDX11NativeWorldAutoGate(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11NativeWorldAutoGateEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11StrictNativeOnly(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11StrictNativeOnlyEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11DisableDX9CompatDevice(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11DisableDX9CompatDeviceEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11TerrainStabilizationMode(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11TerrainStabilizationModeEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11TexturePipelineMode(PyObject * poSelf, PyObject * poArgs)
{
	int iMode;
	if (!PyTuple_GetInteger(poArgs, 0, &iMode))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11TexturePipelineMode(iMode);
	return Py_BuildNone();
}

PyObject * systemSetDX11UIPassOnly(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11UIPassOnlyEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11UINativeTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11UINativeTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11UITextureTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11UITextureTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldDepthTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldDepthTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldBatchTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldBatchTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldSpriteTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldSpriteTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldStateTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldStateTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldPassesTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldPassesTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldBridgeTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldBridgeTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldSubsystemTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldSubsystemTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldRealtimeTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldRealtimeTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldMetricsTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldMetricsTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldInstanceFeedTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldInstanceFeedTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldFinalcheckTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldFinalcheckTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldHandoffTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldHandoffTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldSwapchainTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldSwapchainTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldPresentPathTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldPresentPathTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldVisiblePass1Test(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldVisiblePass1TestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldComposerTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldComposerTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldScenegraphTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldScenegraphTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldPipelineTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldPipelineTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemSetDX11WorldFramegraphTest(PyObject * poSelf, PyObject * poArgs)
{
	int iEnabled;
	if (!PyTuple_GetInteger(poArgs, 0, &iEnabled))
		return Py_BuildException();

	CPythonSystem::Instance().SetDX11WorldFramegraphTestEnabled(iEnabled ? true : false);
	return Py_BuildNone();
}

PyObject * systemGetPerfProfile(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetPerfProfile());
}

PyObject * systemSetPerfProfile(PyObject * poSelf, PyObject * poArgs)
{
	int iProfile;
	if (!PyTuple_GetInteger(poArgs, 0, &iProfile))
		return Py_BuildException();

	CPythonSystem::Instance().SetPerfProfile(iProfile);
	CPythonApplication::Instance().ApplyPerformanceConfig(
		CPythonSystem::Instance().GetPerfProfile(),
		CPythonSystem::Instance().IsFXAdaptiveEnabled(),
		CPythonSystem::Instance().IsAnimLODEnabled(),
		CPythonSystem::Instance().IsTextTailOptEnabled(),
		CPythonSystem::Instance().GetShadowCadence(),
		CPythonSystem::Instance().GetFXStrideBias(),
		CPythonSystem::Instance().IsShadowDynamicBoostEnabled(),
		CPythonSystem::Instance().IsTextTailGridOptEnabled());
	return Py_BuildNone();
}

PyObject * systemGetFXAdaptive(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsFXAdaptiveEnabled() ? 1 : 0);
}

PyObject * systemSetFXAdaptive(PyObject * poSelf, PyObject * poArgs)
{
	int iFlag;
	if (!PyTuple_GetInteger(poArgs, 0, &iFlag))
		return Py_BuildException();

	CPythonSystem::Instance().SetFXAdaptiveEnabled(iFlag ? true : false);
	CPythonApplication::Instance().ApplyPerformanceConfig(
		CPythonSystem::Instance().GetPerfProfile(),
		CPythonSystem::Instance().IsFXAdaptiveEnabled(),
		CPythonSystem::Instance().IsAnimLODEnabled(),
		CPythonSystem::Instance().IsTextTailOptEnabled(),
		CPythonSystem::Instance().GetShadowCadence(),
		CPythonSystem::Instance().GetFXStrideBias(),
		CPythonSystem::Instance().IsShadowDynamicBoostEnabled(),
		CPythonSystem::Instance().IsTextTailGridOptEnabled());
	return Py_BuildNone();
}

PyObject * systemGetAnimLOD(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsAnimLODEnabled() ? 1 : 0);
}

PyObject * systemSetAnimLOD(PyObject * poSelf, PyObject * poArgs)
{
	int iFlag;
	if (!PyTuple_GetInteger(poArgs, 0, &iFlag))
		return Py_BuildException();

	CPythonSystem::Instance().SetAnimLODEnabled(iFlag ? true : false);
	CPythonApplication::Instance().ApplyPerformanceConfig(
		CPythonSystem::Instance().GetPerfProfile(),
		CPythonSystem::Instance().IsFXAdaptiveEnabled(),
		CPythonSystem::Instance().IsAnimLODEnabled(),
		CPythonSystem::Instance().IsTextTailOptEnabled(),
		CPythonSystem::Instance().GetShadowCadence(),
		CPythonSystem::Instance().GetFXStrideBias(),
		CPythonSystem::Instance().IsShadowDynamicBoostEnabled(),
		CPythonSystem::Instance().IsTextTailGridOptEnabled());
	return Py_BuildNone();
}

PyObject * systemGetTextTailOpt(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsTextTailOptEnabled() ? 1 : 0);
}

PyObject * systemSetTextTailOpt(PyObject * poSelf, PyObject * poArgs)
{
	int iFlag;
	if (!PyTuple_GetInteger(poArgs, 0, &iFlag))
		return Py_BuildException();

	CPythonSystem::Instance().SetTextTailOptEnabled(iFlag ? true : false);
	CPythonApplication::Instance().ApplyPerformanceConfig(
		CPythonSystem::Instance().GetPerfProfile(),
		CPythonSystem::Instance().IsFXAdaptiveEnabled(),
		CPythonSystem::Instance().IsAnimLODEnabled(),
		CPythonSystem::Instance().IsTextTailOptEnabled(),
		CPythonSystem::Instance().GetShadowCadence(),
		CPythonSystem::Instance().GetFXStrideBias(),
		CPythonSystem::Instance().IsShadowDynamicBoostEnabled(),
		CPythonSystem::Instance().IsTextTailGridOptEnabled());
	return Py_BuildNone();
}

PyObject * systemGetShadowCadence(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetShadowCadence());
}

PyObject * systemSetShadowCadence(PyObject * poSelf, PyObject * poArgs)
{
	int iCadence;
	if (!PyTuple_GetInteger(poArgs, 0, &iCadence))
		return Py_BuildException();

	CPythonSystem::Instance().SetShadowCadence(iCadence);
	CPythonApplication::Instance().ApplyPerformanceConfig(
		CPythonSystem::Instance().GetPerfProfile(),
		CPythonSystem::Instance().IsFXAdaptiveEnabled(),
		CPythonSystem::Instance().IsAnimLODEnabled(),
		CPythonSystem::Instance().IsTextTailOptEnabled(),
		CPythonSystem::Instance().GetShadowCadence(),
		CPythonSystem::Instance().GetFXStrideBias(),
		CPythonSystem::Instance().IsShadowDynamicBoostEnabled(),
		CPythonSystem::Instance().IsTextTailGridOptEnabled());
	return Py_BuildNone();
}

PyObject * systemGetFXStrideBias(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetFXStrideBias());
}

PyObject * systemSetFXStrideBias(PyObject * poSelf, PyObject * poArgs)
{
	int iBias;
	if (!PyTuple_GetInteger(poArgs, 0, &iBias))
		return Py_BuildException();

	CPythonSystem::Instance().SetFXStrideBias(iBias);
	CPythonApplication::Instance().ApplyPerformanceConfig(
		CPythonSystem::Instance().GetPerfProfile(),
		CPythonSystem::Instance().IsFXAdaptiveEnabled(),
		CPythonSystem::Instance().IsAnimLODEnabled(),
		CPythonSystem::Instance().IsTextTailOptEnabled(),
		CPythonSystem::Instance().GetShadowCadence(),
		CPythonSystem::Instance().GetFXStrideBias(),
		CPythonSystem::Instance().IsShadowDynamicBoostEnabled(),
		CPythonSystem::Instance().IsTextTailGridOptEnabled());
	return Py_BuildNone();
}

PyObject * systemGetShadowDynamicBoost(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsShadowDynamicBoostEnabled() ? 1 : 0);
}

PyObject * systemSetShadowDynamicBoost(PyObject * poSelf, PyObject * poArgs)
{
	int iFlag;
	if (!PyTuple_GetInteger(poArgs, 0, &iFlag))
		return Py_BuildException();

	CPythonSystem::Instance().SetShadowDynamicBoostEnabled(iFlag ? true : false);
	CPythonApplication::Instance().ApplyPerformanceConfig(
		CPythonSystem::Instance().GetPerfProfile(),
		CPythonSystem::Instance().IsFXAdaptiveEnabled(),
		CPythonSystem::Instance().IsAnimLODEnabled(),
		CPythonSystem::Instance().IsTextTailOptEnabled(),
		CPythonSystem::Instance().GetShadowCadence(),
		CPythonSystem::Instance().GetFXStrideBias(),
		CPythonSystem::Instance().IsShadowDynamicBoostEnabled(),
		CPythonSystem::Instance().IsTextTailGridOptEnabled());
	return Py_BuildNone();
}

PyObject * systemGetTextTailGridOpt(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsTextTailGridOptEnabled() ? 1 : 0);
}

PyObject * systemSetTextTailGridOpt(PyObject * poSelf, PyObject * poArgs)
{
	int iFlag;
	if (!PyTuple_GetInteger(poArgs, 0, &iFlag))
		return Py_BuildException();

	CPythonSystem::Instance().SetTextTailGridOptEnabled(iFlag ? true : false);
	CPythonApplication::Instance().ApplyPerformanceConfig(
		CPythonSystem::Instance().GetPerfProfile(),
		CPythonSystem::Instance().IsFXAdaptiveEnabled(),
		CPythonSystem::Instance().IsAnimLODEnabled(),
		CPythonSystem::Instance().IsTextTailOptEnabled(),
		CPythonSystem::Instance().GetShadowCadence(),
		CPythonSystem::Instance().GetFXStrideBias(),
		CPythonSystem::Instance().IsShadowDynamicBoostEnabled(),
		CPythonSystem::Instance().IsTextTailGridOptEnabled());
	return Py_BuildNone();
}

PyObject * systemGetTextTailOptRange(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetTextTailOptRange());
}

PyObject * systemSetTextTailOptRange(PyObject * poSelf, PyObject * poArgs)
{
	int iRange;
	if (!PyTuple_GetInteger(poArgs, 0, &iRange))
		return Py_BuildException();

	CPythonSystem::Instance().SetTextTailOptRange(iRange);
	CPythonApplication::Instance().SetTextTailOptRange(CPythonSystem::Instance().GetTextTailOptRange());
	return Py_BuildNone();
}

PyObject * systemIsSoftwareCursor(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsSoftwareCursor());
}

PyObject * systemSetViewChatFlag(PyObject * poSelf, PyObject * poArgs)
{
	int iFlag;
	if (!PyTuple_GetInteger(poArgs, 0, &iFlag))
		return Py_BuildException();

	CPythonSystem::Instance().SetViewChatFlag(iFlag);

	return Py_BuildNone();
}

PyObject * systemIsViewChat(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsViewChat());
}

PyObject * systemSetAlwaysShowNameFlag(PyObject * poSelf, PyObject * poArgs)
{
	int iFlag;
	if (!PyTuple_GetInteger(poArgs, 0, &iFlag))
		return Py_BuildException();

	CPythonSystem::Instance().SetAlwaysShowNameFlag(iFlag);

	return Py_BuildNone();
}

PyObject * systemSetShowDamageFlag(PyObject * poSelf, PyObject * poArgs)
{
	int iFlag;
	if (!PyTuple_GetInteger(poArgs, 0, &iFlag))
		return Py_BuildException();

	CPythonSystem::Instance().SetShowDamageFlag(iFlag);

	return Py_BuildNone();
}

PyObject * systemSetShowSalesTextFlag(PyObject * poSelf, PyObject * poArgs)
{
	int iFlag;
	if (!PyTuple_GetInteger(poArgs, 0, &iFlag))
		return Py_BuildException();

	CPythonSystem::Instance().SetShowSalesTextFlag(iFlag);

	return Py_BuildNone();
}

PyObject * systemIsAlwaysShowName(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsAlwaysShowName());
}

PyObject * systemIsShowDamage(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsShowDamage());
}

PyObject * systemIsShowSalesText(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().IsShowSalesText());
}

PyObject * systemSetConfig(PyObject * poSelf, PyObject * poArgs)
{
	int res_index;
	int width;
	int height;
	int bpp;
	int frequency_index;
	int frequency;
	int software_cursor;
	int shadow;
	int object_culling;
	int music_volume;
	int voice_volume;
	int gamma;
	int distance;

	if (!PyTuple_GetInteger(poArgs, 0, &res_index))
		return Py_BuildException();

	if (!PyTuple_GetInteger(poArgs, 1, &frequency_index))
		return Py_BuildException();

	if (!PyTuple_GetInteger(poArgs, 2, &software_cursor))
		return Py_BuildException();

	if (!PyTuple_GetInteger(poArgs, 3, &shadow))
		return Py_BuildException();

	if (!PyTuple_GetInteger(poArgs, 4, &object_culling))
		return Py_BuildException();

	if (!PyTuple_GetInteger(poArgs, 5, &music_volume))
		return Py_BuildException();

	if (!PyTuple_GetInteger(poArgs, 6, &voice_volume))
		return Py_BuildException();

	if (!PyTuple_GetInteger(poArgs, 7, &gamma))
		return Py_BuildException();

	if (!PyTuple_GetInteger(poArgs, 8, &distance))
		return Py_BuildException();

	if (!CPythonSystem::Instance().GetResolution(res_index, (DWORD *) &width, (DWORD *) &height, (DWORD *) &bpp))
		return Py_BuildNone();

	if (!CPythonSystem::Instance().GetFrequency(res_index,frequency_index, (DWORD *) &frequency))
		return Py_BuildNone();

	CPythonSystem::TConfig tmp;

	memcpy(&tmp, CPythonSystem::Instance().GetConfig(), sizeof(tmp));

	tmp.width				= width;
	tmp.height				= height;
	tmp.bpp					= bpp;
	tmp.frequency			= frequency;
	tmp.is_software_cursor	= software_cursor ? true : false;
	tmp.is_object_culling	= object_culling ? true : false;
	tmp.music_volume		= (char) music_volume;
	tmp.voice_volume		= (char) voice_volume;
	tmp.gamma				= gamma;
	tmp.iDistance			= distance;

	CPythonSystem::Instance().SetConfig(&tmp);
	return Py_BuildNone();
}

PyObject * systemApplyConfig(PyObject * poSelf, PyObject * poArgs)
{
	CPythonSystem::Instance().ApplyConfig();
	return Py_BuildNone();
}

PyObject * systemSaveConfig(PyObject * poSelf, PyObject * poArgs)
{
	int ret = CPythonSystem::Instance().SaveConfig();
	return Py_BuildValue("i", ret);
}

PyObject * systemGetResolutionCount(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetResolutionCount());
}

PyObject * systemGetFrequencyCount(PyObject * poSelf, PyObject * poArgs)
{
	int	index;

	if (!PyTuple_GetInteger(poArgs, 0, &index))
		return Py_BuildException();

	return Py_BuildValue("i", CPythonSystem::Instance().GetFrequencyCount(index));
}

PyObject * systemGetResolution(PyObject * poSelf, PyObject * poArgs)
{
	int	index;
	DWORD width = 0, height = 0, bpp = 0;

	if (!PyTuple_GetInteger(poArgs, 0, &index))
		return Py_BuildException();

	CPythonSystem::Instance().GetResolution(index, &width, &height, &bpp);
	return Py_BuildValue("iii", width, height, bpp);
}

PyObject * systemGetCurrentResolution(PyObject * poSelf, PyObject *poArgs)
{
	CPythonSystem::TConfig *tmp = CPythonSystem::Instance().GetConfig();
	return Py_BuildValue("iii", tmp->width, tmp->height, tmp->bpp);
}

PyObject * systemGetFrequency(PyObject * poSelf, PyObject * poArgs)
{
	int	index, frequency_index;
	DWORD frequency = 0;

	if (!PyTuple_GetInteger(poArgs, 0, &index))
		return Py_BuildException();

	if (!PyTuple_GetInteger(poArgs, 1, &frequency_index))
		return Py_BuildException();

	CPythonSystem::Instance().GetFrequency(index, frequency_index, &frequency);
	return Py_BuildValue("i", frequency);
}

PyObject * systemGetShadowLevel(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetShadowLevel());
}

PyObject * systemSetShadowLevel(PyObject * poSelf, PyObject * poArgs)
{
	int level;

	if (!PyTuple_GetInteger(poArgs, 0, &level))
		return Py_BuildException();

	if (level > 0)
		CPythonSystem::Instance().SetShadowLevel(level);

	return Py_BuildNone();
}

// MR-14: Fog update by Alaric
PyObject * systemGetFogLevel(PyObject * poSelf, PyObject * poArgs)
{
	return Py_BuildValue("i", CPythonSystem::Instance().GetFogLevel());
}

PyObject * systemSetFogLevel(PyObject * poSelf, PyObject * poArgs)
{
	int iLevel;
	if (!PyTuple_GetInteger(poArgs, 0, &iLevel))
		return Py_BuildException();

	CPythonSystem::Instance().SetFogLevel(iLevel);
	return Py_BuildNone();
}
// MR-14: -- END OF -- Fog update by Alaric

void initsystem()
{
	static PyMethodDef s_methods[] =
	{
		// MR-14: Fog update by Alaric
		{ "GetFogLevel",				systemGetFogLevel,				METH_VARARGS },
		{ "SetFogLevel",				systemSetFogLevel,				METH_VARARGS },
		// MR-14: -- END OF -- Fog update by Alaric

		{ "GetWidth",					systemGetWidth,					METH_VARARGS },
		{ "GetHeight",					systemGetHeight,				METH_VARARGS },

		{ "SetInterfaceHandler",		systemSetInterfaceHandler,		METH_VARARGS },
		{ "DestroyInterfaceHandler",	systemDestroyInterfaceHandler,	METH_VARARGS },
		{ "ReserveResource",			systemReserveResource,			METH_VARARGS },

		{ "isInterfaceConfig",			systemisInterfaceConfig,		METH_VARARGS },
		{ "SaveWindowStatus",			systemSaveWindowStatus,			METH_VARARGS },
		{ "GetWindowStatus",			systemGetWindowStatus,			METH_VARARGS },

		{ "GetResolutionCount",			systemGetResolutionCount,		METH_VARARGS },
		{ "GetFrequencyCount",			systemGetFrequencyCount,		METH_VARARGS },

		{ "GetCurrentResolution",		systemGetCurrentResolution,		METH_VARARGS },

		{ "GetResolution",				systemGetResolution,			METH_VARARGS },
		{ "GetFrequency",				systemGetFrequency,				METH_VARARGS },

		{ "ApplyConfig",				systemApplyConfig,				METH_VARARGS },
		{ "SetConfig",					systemSetConfig,				METH_VARARGS },
		{ "SaveConfig",					systemSaveConfig,				METH_VARARGS },
		{ "GetConfig",					systemGetConfig,				METH_VARARGS },

		{ "SetSaveID",					systemSetSaveID,				METH_VARARGS },
		{ "isSaveID",					systemisSaveID,					METH_VARARGS },
		{ "GetSaveID",					systemGetSaveID,				METH_VARARGS },

		{ "GetMusicVolume",				systemGetMusicVolume,			METH_VARARGS },
		{ "GetSoundVolume",				systemGetSoundVolume,			METH_VARARGS },
		{ "GetRenderAPI",				systemGetRenderAPI,				METH_VARARGS },
		{ "GetShaderCacheVersion",		systemGetShaderCacheVersion,	METH_VARARGS },
		{ "GetRenderFPSLimit",			systemGetRenderFPSLimit,		METH_VARARGS },
		{ "GetVSync",					systemGetVSync,					METH_VARARGS },
		{ "GetDX11ExperimentalPresent",	systemGetDX11ExperimentalPresent, METH_VARARGS },
		{ "GetDX11NativeWorldAutoGate",	systemGetDX11NativeWorldAutoGate, METH_VARARGS },
		{ "GetDX11StrictNativeOnly",	systemGetDX11StrictNativeOnly, METH_VARARGS },
		{ "GetDX11DisableDX9CompatDevice",	systemGetDX11DisableDX9CompatDevice, METH_VARARGS },
		{ "GetDX11TerrainStabilizationMode",	systemGetDX11TerrainStabilizationMode, METH_VARARGS },
		{ "GetDX11TexturePipelineMode",	systemGetDX11TexturePipelineMode, METH_VARARGS },
		{ "GetDX11UIPassOnly",			systemGetDX11UIPassOnly,		METH_VARARGS },
		{ "GetDX11UINativeTest",		systemGetDX11UINativeTest,		METH_VARARGS },
		{ "GetDX11UITextureTest",		systemGetDX11UITextureTest,		METH_VARARGS },
		{ "GetDX11WorldDepthTest",		systemGetDX11WorldDepthTest,	METH_VARARGS },
		{ "GetDX11WorldBatchTest",		systemGetDX11WorldBatchTest,	METH_VARARGS },
		{ "GetDX11WorldSpriteTest",		systemGetDX11WorldSpriteTest,	METH_VARARGS },
		{ "GetDX11WorldStateTest",		systemGetDX11WorldStateTest,	METH_VARARGS },
		{ "GetDX11WorldPassesTest",		systemGetDX11WorldPassesTest,	METH_VARARGS },
		{ "GetDX11WorldBridgeTest",		systemGetDX11WorldBridgeTest,	METH_VARARGS },
		{ "GetDX11WorldSubsystemTest",	systemGetDX11WorldSubsystemTest,	METH_VARARGS },
		{ "GetDX11WorldRealtimeTest",	systemGetDX11WorldRealtimeTest,	METH_VARARGS },
		{ "GetDX11WorldMetricsTest",	systemGetDX11WorldMetricsTest,	METH_VARARGS },
		{ "GetDX11WorldInstanceFeedTest",	systemGetDX11WorldInstanceFeedTest,	METH_VARARGS },
		{ "GetDX11WorldFinalcheckTest",	systemGetDX11WorldFinalcheckTest,	METH_VARARGS },
		{ "GetDX11WorldHandoffTest",	systemGetDX11WorldHandoffTest,	METH_VARARGS },
		{ "GetDX11WorldSwapchainTest",	systemGetDX11WorldSwapchainTest,	METH_VARARGS },
		{ "GetDX11WorldPresentPathTest",	systemGetDX11WorldPresentPathTest,	METH_VARARGS },
		{ "GetDX11WorldVisiblePass1Test",	systemGetDX11WorldVisiblePass1Test,	METH_VARARGS },
		{ "GetDX11WorldComposerTest",	systemGetDX11WorldComposerTest,	METH_VARARGS },
		{ "GetDX11WorldScenegraphTest",	systemGetDX11WorldScenegraphTest,	METH_VARARGS },
		{ "GetDX11WorldPipelineTest",	systemGetDX11WorldPipelineTest,	METH_VARARGS },
		{ "GetDX11WorldFramegraphTest",	systemGetDX11WorldFramegraphTest,	METH_VARARGS },
		{ "GetPerfProfile",				systemGetPerfProfile,			METH_VARARGS },
		{ "GetFXAdaptive",				systemGetFXAdaptive,			METH_VARARGS },
		{ "GetAnimLOD",					systemGetAnimLOD,				METH_VARARGS },
		{ "GetTextTailOpt",				systemGetTextTailOpt,			METH_VARARGS },
		{ "GetShadowCadence",			systemGetShadowCadence,			METH_VARARGS },
		{ "GetFXStrideBias",			systemGetFXStrideBias,			METH_VARARGS },
		{ "GetShadowDynamicBoost",		systemGetShadowDynamicBoost,	METH_VARARGS },
		{ "GetTextTailGridOpt",			systemGetTextTailGridOpt,		METH_VARARGS },
		{ "GetTextTailOptRange",		systemGetTextTailOptRange,		METH_VARARGS },

		{ "SetMusicVolume",				systemSetMusicVolume,			METH_VARARGS },
		{ "SetSoundVolume",				systemSetSoundVolume,			METH_VARARGS },
		{ "SetRenderAPI",				systemSetRenderAPI,				METH_VARARGS },
		{ "SetShaderCacheVersion",		systemSetShaderCacheVersion,	METH_VARARGS },
		{ "SetRenderFPSLimit",			systemSetRenderFPSLimit,		METH_VARARGS },
		{ "SetVSync",					systemSetVSync,					METH_VARARGS },
		{ "SetDX11ExperimentalPresent",	systemSetDX11ExperimentalPresent, METH_VARARGS },
		{ "SetDX11NativeWorldAutoGate",	systemSetDX11NativeWorldAutoGate, METH_VARARGS },
		{ "SetDX11StrictNativeOnly",	systemSetDX11StrictNativeOnly, METH_VARARGS },
		{ "SetDX11DisableDX9CompatDevice",	systemSetDX11DisableDX9CompatDevice, METH_VARARGS },
		{ "SetDX11TerrainStabilizationMode",	systemSetDX11TerrainStabilizationMode, METH_VARARGS },
		{ "SetDX11TexturePipelineMode",	systemSetDX11TexturePipelineMode, METH_VARARGS },
		{ "SetDX11UIPassOnly",			systemSetDX11UIPassOnly,		METH_VARARGS },
		{ "SetDX11UINativeTest",		systemSetDX11UINativeTest,		METH_VARARGS },
		{ "SetDX11UITextureTest",		systemSetDX11UITextureTest,		METH_VARARGS },
		{ "SetDX11WorldDepthTest",		systemSetDX11WorldDepthTest,	METH_VARARGS },
		{ "SetDX11WorldBatchTest",		systemSetDX11WorldBatchTest,	METH_VARARGS },
		{ "SetDX11WorldSpriteTest",		systemSetDX11WorldSpriteTest,	METH_VARARGS },
		{ "SetDX11WorldStateTest",		systemSetDX11WorldStateTest,	METH_VARARGS },
		{ "SetDX11WorldPassesTest",		systemSetDX11WorldPassesTest,	METH_VARARGS },
		{ "SetDX11WorldBridgeTest",		systemSetDX11WorldBridgeTest,	METH_VARARGS },
		{ "SetDX11WorldSubsystemTest",	systemSetDX11WorldSubsystemTest,	METH_VARARGS },
		{ "SetDX11WorldRealtimeTest",	systemSetDX11WorldRealtimeTest,	METH_VARARGS },
		{ "SetDX11WorldMetricsTest",	systemSetDX11WorldMetricsTest,	METH_VARARGS },
		{ "SetDX11WorldInstanceFeedTest",	systemSetDX11WorldInstanceFeedTest,	METH_VARARGS },
		{ "SetDX11WorldFinalcheckTest",	systemSetDX11WorldFinalcheckTest,	METH_VARARGS },
		{ "SetDX11WorldHandoffTest",	systemSetDX11WorldHandoffTest,	METH_VARARGS },
		{ "SetDX11WorldSwapchainTest",	systemSetDX11WorldSwapchainTest,	METH_VARARGS },
		{ "SetDX11WorldPresentPathTest",	systemSetDX11WorldPresentPathTest,	METH_VARARGS },
		{ "SetDX11WorldVisiblePass1Test",	systemSetDX11WorldVisiblePass1Test,	METH_VARARGS },
		{ "SetDX11WorldComposerTest",	systemSetDX11WorldComposerTest,	METH_VARARGS },
		{ "SetDX11WorldScenegraphTest",	systemSetDX11WorldScenegraphTest,	METH_VARARGS },
		{ "SetDX11WorldPipelineTest",	systemSetDX11WorldPipelineTest,	METH_VARARGS },
		{ "SetDX11WorldFramegraphTest",	systemSetDX11WorldFramegraphTest,	METH_VARARGS },
		{ "SetPerfProfile",				systemSetPerfProfile,			METH_VARARGS },
		{ "SetFXAdaptive",				systemSetFXAdaptive,			METH_VARARGS },
		{ "SetAnimLOD",					systemSetAnimLOD,				METH_VARARGS },
		{ "SetTextTailOpt",				systemSetTextTailOpt,			METH_VARARGS },
		{ "SetShadowCadence",			systemSetShadowCadence,			METH_VARARGS },
		{ "SetFXStrideBias",			systemSetFXStrideBias,			METH_VARARGS },
		{ "SetShadowDynamicBoost",		systemSetShadowDynamicBoost,	METH_VARARGS },
		{ "SetTextTailGridOpt",			systemSetTextTailGridOpt,		METH_VARARGS },
		{ "SetTextTailOptRange",		systemSetTextTailOptRange,		METH_VARARGS },
		{ "IsSoftwareCursor",			systemIsSoftwareCursor,			METH_VARARGS },

		{ "SetViewChatFlag",			systemSetViewChatFlag,			METH_VARARGS },
		{ "IsViewChat",					systemIsViewChat,				METH_VARARGS },

		{ "SetAlwaysShowNameFlag",		systemSetAlwaysShowNameFlag,	METH_VARARGS },
		{ "IsAlwaysShowName",			systemIsAlwaysShowName,			METH_VARARGS },

		{ "SetShowDamageFlag",			systemSetShowDamageFlag,		METH_VARARGS },
		{ "IsShowDamage",				systemIsShowDamage,				METH_VARARGS },

		{ "SetShowSalesTextFlag",		systemSetShowSalesTextFlag,		METH_VARARGS },
		{ "IsShowSalesText",			systemIsShowSalesText,			METH_VARARGS },

		{ "GetShadowLevel",				systemGetShadowLevel,			METH_VARARGS },
		{ "SetShadowLevel",				systemSetShadowLevel,			METH_VARARGS },

		{ NULL,							NULL,							NULL }
	};

	PyObject * poModule = Py_InitModule("systemSetting", s_methods);

	PyModule_AddIntConstant(poModule, "WINDOW_STATUS",		CPythonSystem::WINDOW_STATUS);
	PyModule_AddIntConstant(poModule, "WINDOW_INVENTORY",	CPythonSystem::WINDOW_INVENTORY);
	PyModule_AddIntConstant(poModule, "WINDOW_ABILITY",		CPythonSystem::WINDOW_ABILITY);
	PyModule_AddIntConstant(poModule, "WINDOW_SOCIETY",		CPythonSystem::WINDOW_SOCIETY);
	PyModule_AddIntConstant(poModule, "WINDOW_JOURNAL",		CPythonSystem::WINDOW_JOURNAL);
	PyModule_AddIntConstant(poModule, "WINDOW_COMMAND",		CPythonSystem::WINDOW_COMMAND);

	PyModule_AddIntConstant(poModule, "WINDOW_QUICK",		CPythonSystem::WINDOW_QUICK);
	PyModule_AddIntConstant(poModule, "WINDOW_GAUGE",		CPythonSystem::WINDOW_GAUGE);
	PyModule_AddIntConstant(poModule, "WINDOW_MINIMAP",		CPythonSystem::WINDOW_MINIMAP);
	PyModule_AddIntConstant(poModule, "WINDOW_CHAT",		CPythonSystem::WINDOW_CHAT);
}
