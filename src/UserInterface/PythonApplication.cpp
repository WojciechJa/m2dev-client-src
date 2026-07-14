#include "StdAfx.h"
#include "eterBase/Error.h"
#include "eterlib/Camera.h"
#include "eterlib/AttributeInstance.h"
#include "gamelib/AreaTerrain.h"
#include "EterGrnLib/Material.h"

#include "resource.h"
#include "PythonApplication.h"
#include "PythonCharacterManager.h"
#include "config.h"
#include "EterLib/GrpDeviceDX11.h"
#include "EterLib/StateManager.h"
#include "EterLib/GrpTextureDX11.h"  // M3-TEXTURE-ASYNC-10-RUNTIME
#include "EterLib/SystemMemoryDetector.h"  // M3-TEXTURE-ASYNC-10-RUNTIME

#include "ProcessScanner.h"

#include <utf8.h>

extern void GrannyCreateSharedDeformBuffer();
extern void GrannyDestroySharedDeformBuffer();

namespace
{
	static const int DX11_COMPAT_GRACE_REASON_MAIN_INSTANCE_MISSING = 0x01;
	static const int DX11_COMPAT_GRACE_REASON_FINALCHECK_MISSING = 0x02;
	static const DWORD DX11_FULL_BLOCK_RUNTIME = (1u << 0);
	static const DWORD DX11_FULL_BLOCK_VISIBLE = (1u << 1);
	static const DWORD DX11_FULL_BLOCK_WORLD = (1u << 2);
	static const DWORD DX11_FULL_BLOCK_UI = (1u << 3);
	static const DWORD DX11_FULL_BLOCK_CUTOVER = (1u << 4);
	static const DWORD DX11_VISIBLE_BLOCK_SUCCESS_MIN = 40u;
	static const DWORD DX11_AUTO_PROMOTE_VISIBLE_SUCCESS_MIN = 40u;
	static const DWORD DX11_AUTO_PROMOTE_RUNTIME_MIN_MS = 180000u;
	static const DWORD DX11_CUTOVER_READY_VISIBLE_SUCCESS_MIN = 40u;
	static const DWORD DX11_CUTOVER_READY_RUNTIME_MS_MIN = 120000u;
	static const DWORD DX11_CUTOVER_READY_CONFIRM_FRAMES = 300u;
	static const DWORD DX11_CUTOVER_READY_MAX_INTERVAL_FRAMES = 60u;
	static const DWORD DX11_CUTOVER_READY_MAX_STRESS_FRAMES = 120u;
	static const DWORD DX11_CUTOVER_STATUS_INTERVAL_MS = 15000u;
	static const DWORD DX11_CUTOVER_READY_REMINDER_MS = 60000u;
	static const DWORD DX11_NATIVE_HEARTBEAT_INTERVAL_MS = 45000u;
	static const DWORD DX11_WORLD_HEARTBEAT_INTERVAL_MS = 45000u;
	static const DWORD DX11_BRIDGE_IO_HEARTBEAT_INTERVAL_MS = 45000u;
	static const DWORD DX11_TEXTURE_ASYNC_HEARTBEAT_INTERVAL_MS = 45000u;  // M3-TEXTURE-ASYNC-10-RUNTIME
	static const DWORD DX11_BLOCKER_REPEAT_LOG_INTERVAL_MS = 15000u;
	static DWORD g_dwDX11TextureAsyncLogTick = 0;  // M3-TEXTURE-ASYNC-10-RUNTIME
	static DWORD g_dwDX11ShadowPolicyLogTick = 0;

	const char* DX11RuntimeCompatGraceReasonToString(int iReasonMask)
	{
		if ((iReasonMask & DX11_COMPAT_GRACE_REASON_MAIN_INSTANCE_MISSING) &&
			(iReasonMask & DX11_COMPAT_GRACE_REASON_FINALCHECK_MISSING))
		{
			return "main_instance_missing+finalcheck_missing";
		}
		if (iReasonMask & DX11_COMPAT_GRACE_REASON_MAIN_INSTANCE_MISSING)
			return "main_instance_missing";
		if (iReasonMask & DX11_COMPAT_GRACE_REASON_FINALCHECK_MISSING)
			return "finalcheck_missing";
		return "none";
	}

	bool IsWindows10OrGreaterRuntime()
	{
		HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
		if (!hNtdll)
			return false;

		typedef LONG (WINAPI* TRtlGetVersionFn)(OSVERSIONINFOW*);
		TRtlGetVersionFn pRtlGetVersion = reinterpret_cast<TRtlGetVersionFn>(GetProcAddress(hNtdll, "RtlGetVersion"));
		if (!pRtlGetVersion)
			return false;

		OSVERSIONINFOW kVersionInfo;
		ZeroMemory(&kVersionInfo, sizeof(kVersionInfo));
		kVersionInfo.dwOSVersionInfoSize = sizeof(kVersionInfo);

		if (pRtlGetVersion(&kVersionInfo) != 0)
			return false;

		return (kVersionInfo.dwMajorVersion >= 10);
	}

	int ToFeatureLevelInt(D3D_FEATURE_LEVEL eFeatureLevel)
	{
		switch (eFeatureLevel)
		{
			case D3D_FEATURE_LEVEL_11_0: return 110;
			case D3D_FEATURE_LEVEL_10_1: return 101;
			case D3D_FEATURE_LEVEL_10_0: return 100;
			case D3D_FEATURE_LEVEL_9_3:  return 93;
			case D3D_FEATURE_LEVEL_9_2:  return 92;
			case D3D_FEATURE_LEVEL_9_1:  return 91;
			default: return 0;
		}
	}

	void DX11FormatWorldPortMask(uint32_t dwMask, char* szOut, size_t stOutSize)
	{
		if (!szOut || 0 == stOutSize)
			return;

		szOut[0] = '\0';
		bool bFirst = true;
		const auto AppendToken = [&](const char* c_szToken)
		{
			if (!c_szToken || !c_szToken[0])
				return;
			if (!bFirst)
				strcat_s(szOut, stOutSize, "|");
			strcat_s(szOut, stOutSize, c_szToken);
			bFirst = false;
		};

		if (dwMask & CGraphicDeviceDX11::WORLD_TERRAIN_DX11)
			AppendToken("terrain");
		if (dwMask & CGraphicDeviceDX11::WORLD_OBJECTS_DX11)
			AppendToken("objects");
		if (dwMask & CGraphicDeviceDX11::WORLD_EFFECTS_DX11)
			AppendToken("effects");
		if (dwMask & CGraphicDeviceDX11::WORLD_SPEEDTREE_DX11)
			AppendToken("speedtree");
		if (dwMask & CGraphicDeviceDX11::WORLD_WATER_DX11)
			AppendToken("water");

		if (bFirst)
			strcat_s(szOut, stOutSize, "none");
	}

	uint32_t DX11CountSetBits(uint32_t dwValue)
	{
		uint32_t dwCount = 0;
		while (dwValue)
		{
			dwValue &= (dwValue - 1u);
			++dwCount;
		}
		return dwCount;
	}
}

float MIN_FOG = 2400.0f;
double g_specularSpd=0.007f;

CPythonApplication * CPythonApplication::ms_pInstance;

float c_fDefaultCameraRotateSpeed = 1.5f;
float c_fDefaultCameraPitchSpeed = 1.5f;
float c_fDefaultCameraZoomSpeed = 0.05f;

CPythonApplication::CPythonApplication() :
m_bCursorVisible(TRUE),
m_bLiarCursorOn(false),
m_iCursorMode(CURSOR_MODE_HARDWARE),
m_isWindowed(false),
m_isFrameSkipDisable(false),
m_poMouseHandler(NULL),
m_iFPS(60),
m_fRenderFrameIntervalMS(1000.0f / 60.0f),
m_dNextRenderTimeMS(0.0),
m_dwNextUpdateTime(0),
m_isVSyncEnabled(true),
m_iPerfProfile(1),
m_bFXAdaptive(true),
m_bAnimLOD(true),
m_bTextTailOpt(true),
m_iShadowCadence(2),
m_iFXStrideBias(1),
m_bShadowDynamicBoost(true),
m_bTextTailGridOpt(true),
m_iTextTailOptRange(3500),
m_bPerfAutoReduced(false),
m_dwPerfOverBudgetFrames(0),
m_dwPerfUnderBudgetFrames(0),
m_dwPerfRenderFrameCounter(0),
m_dwPerfActiveEffects(0),
m_dwPerfActiveParticles(0),
m_dwPerfVisibleTextTails(0),
m_dwPerfShadowMS(0),
m_dwPerfCharacterMS(0),
m_dwPerfMapMS(0),
m_dwPerfEffectUpdateMS(0),
m_dwPerfEffectRenderMS(0),
m_dwPerfTextTailMS(0),
m_dwPerfTextTailCollisionChecks(0),
m_dwUpdateFPS(0),
m_dwRenderFPS(0),
m_fAveRenderTime(0.0f),
m_dwFaceCount(0),
m_fGlobalTime(0.0f),
m_fGlobalElapsedTime(0.0f),
m_dwLButtonDownTime(0),
m_dwLastIdleTime(0),
m_IsMovingMainWindow(false),
m_bHasLastShadowCameraEye(false),
m_v3LastShadowCameraEye(0.0f, 0.0f, 0.0f),
m_eRenderBackend(RENDER_BACKEND_DX9),
m_iRequestedRenderAPI(9),
m_isRenderBackendFallback(false),
m_iRenderBackendFallbackReason(RENDER_BACKEND_FALLBACK_NONE),
m_isDX11ProbeSuccessful(false),
m_iDX11ProbeFeatureLevel(0),
m_bDX11ExperimentalPresent(false),
m_dwDX11ExperimentalPresentFailCount(0),
m_bDX11RuntimeCompatMode(false),
m_bDX11WorldNativePass1Mode(false),
m_bDX11WorldNativePass2Mode(false),
m_bDX11WorldNativePass3Mode(false),
m_bDX11WorldNativePass4Mode(false),
m_bDX11WorldNativePass5Mode(false),
m_bDX11WorldNativePass6Mode(false),
m_bDX11WorldNativePass7Mode(false),
m_bDX11WorldNativePass8Mode(false),
m_bDX11WorldNativePass9Mode(false),
m_bDX11WorldNativePass10Mode(false),
m_bDX11WorldNativePass11Mode(false),
m_bDX11WorldNativePass12Mode(false),
m_bDX11WorldNativePass13Mode(false),
m_bDX11WorldNativePass14Mode(false),
m_bDX11WorldNativePass15Mode(false),
m_bDX11WorldNativePass16Mode(false),
m_bDX11WorldHandoffProbeMode(false),
m_dwDX11RuntimeCompatFrameCount(0),
m_dwDX11RuntimeCompatStartMS(0),
m_dwDX11RuntimeCompatElapsedMS(0),
m_dwDX11RuntimeCompatGraceUntilMS(0),
m_bDX11RuntimeCompatGraceMode(false),
m_iDX11RuntimeCompatGraceReasonMask(0),
m_dwDX11RuntimeCompatGraceUsedCount(0),
m_dwDX11RuntimeCompatGraceExpiredCount(0),
m_dwDX11RuntimeCompatGraceCoalescedCount(0),
m_dwDX11RuntimeCompatGraceSuppressedCount(0),
m_dwDX11RuntimeCompatGracePendingSinceMS(0),
m_iDX11RuntimeCompatGracePendingReasonMask(0),
m_dwDX11RuntimeCompatLastGraceEnterMS(0),
m_dwDX11RuntimeCompatLastGraceLeaveMS(0),
m_dwDX11HandoffStableGoodFrames(0),
m_bDX11HandoffStableLatched(false),
m_dwDX11StableStressFrames(0),
m_bDX11VisiblePass1AutoDisabled(false),
m_dwDX11VisiblePass1FailCount(0),
m_dwDX11VisiblePass1SuccessCount(0),
m_dwDX11VisiblePass1LastAttemptFrame(0),
m_dwDX11VisiblePass1LastIntervalFrames(0),
m_dwDX11WorldSubmitMaskMismatchCount(0),
m_bDX11WorldSubmitMaskMismatchActive(false),
m_dwDX11WorldSubmitMaskMismatchLastLogMS(0),
m_uDX11WorldSubmitMaskMismatchTelemetryObserved(0u),
m_uDX11WorldSubmitMaskMismatchTelemetrySubmitted(0u),
m_uDX11WorldSubmitMaskMismatchTelemetryApplicable(0u),
m_uDX11WorldSubmitMaskMismatchTelemetryCommitted(0u),
m_uDX11WorldSubmitMaskMismatchGateObserved(0u),
m_uDX11WorldSubmitMaskMismatchGateSubmitted(0u),
m_uDX11WorldSubmitMaskMismatchGateApplicable(0u),
m_uDX11WorldSubmitMaskMismatchGateCommitted(0u),
m_uDX11WorldSubmitMaskMismatchLastReasonMask(0u),
m_uDX11WorldSubmitMaskMismatchLastPhaseActive(0u),
m_dwDX11WorldSubmitMaskMismatchLastFrame(0u),
m_dwDX11WorldSubmitMaskMismatchLastElapsedMS(0u)
{
#ifndef _DEBUG
	SetEterExceptionHandler();
#endif

	CTimer::Instance().UseCustomTime();
	m_dwWidth = 800;
	m_dwHeight = 600;

	ms_pInstance = this;
	m_isWindowFullScreenEnable = FALSE;

	m_v3CenterPosition = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	m_dwStartLocalTime = ELTimer_GetMSec();
	m_tServerTime = 0;
	m_tLocalStartTime = 0;

	m_iPort = 0;

	m_isActivateWnd = false;
	m_isMinimizedWnd = true;

	m_fRotationSpeed = 0.0f;
	m_fPitchSpeed = 0.0f;
	m_fZoomSpeed = 0.0f;

	m_fFaceSpd=0.0f;

	m_dwFaceAccCount=0;
	m_dwFaceAccTime=0;

	m_dwFaceSpdSum=0;
	m_dwFaceSpdCount=0;

	m_FlyingManager.SetMapManagerPtr(&m_pyBackground);

	m_iCursorNum = CURSOR_SHAPE_NORMAL;
	m_iContinuousCursorNum = CURSOR_SHAPE_NORMAL;

	m_isSpecialCameraMode = FALSE;
	m_fCameraRotateSpeed = c_fDefaultCameraRotateSpeed;
	m_fCameraPitchSpeed = c_fDefaultCameraPitchSpeed;
	m_fCameraZoomSpeed = c_fDefaultCameraZoomSpeed;

	m_iCameraMode = CAMERA_MODE_NORMAL;
	m_fBlendCameraStartTime = 0.0f;
	m_fBlendCameraBlendTime = 0.0f;

	m_iForceSightRange = -1;

	CCameraManager::Instance().AddCamera(EVENT_CAMERA_NUMBER);

	m_InitialMouseMovingPoint = {};
}

CPythonApplication::~CPythonApplication()
{
}

void CPythonApplication::GetMousePosition(POINT* ppt)
{
	CMSApplication::GetMousePosition(ppt);
}

void CPythonApplication::SetMinFog(float fMinFog)
{
	MIN_FOG = fMinFog;
}

void CPythonApplication::SetFrameSkip(bool isEnable)
{
	if (isEnable)
		m_isFrameSkipDisable=false;
	else
		m_isFrameSkipDisable=true;
}

void CPythonApplication::NotifyHack(const char* c_szFormat, ...)
{
	char szBuf[1024];

	va_list args;
	va_start(args, c_szFormat);	
	_vsnprintf(szBuf, sizeof(szBuf), c_szFormat, args);
	va_end(args);
	m_pyNetworkStream.NotifyHack(szBuf);
}

void CPythonApplication::GetInfo(UINT eInfo, std::string* pstInfo)
{
	switch (eInfo)
	{
	case INFO_ACTOR:
		m_kChrMgr.GetInfo(pstInfo);
		break;
	case INFO_EFFECT:
		m_kEftMgr.GetInfo(pstInfo);			
		break;
	case INFO_ITEM:
		m_pyItem.GetInfo(pstInfo);
		break;
	case INFO_TEXTTAIL:
		m_pyTextTail.GetInfo(pstInfo);
		break;
	}
}

void CPythonApplication::Abort()
{
	TraceError("============================================================================================================");
	TraceError("Abort!!!!\n\n");

	PostQuitMessage(0);
}

void CPythonApplication::Exit()
{
	PostQuitMessage(0);
}

void CPythonApplication::RenderGame()
{
	if (m_eRenderBackend == RENDER_BACKEND_DX11 &&
		m_pySystem.IsDX11FirstPassActiveEnabled() &&
		m_pySystem.IsDX11UIPassOnlyEnabled() &&
		!m_pySystem.IsDX11WorldFinalcheckTestEnabled())
	{
		// Early DX11 migration stage: keep only UI pass visible on DX9 compatibility path.
		// Finalcheck unlocks normal world rendering through the compatibility path.
		return;
	}

	const bool bDX11HandoffProbeActive =
		(m_eRenderBackend == RENDER_BACKEND_DX11) &&
		m_pySystem.IsDX11FirstPassActiveEnabled() &&
		m_pySystem.IsDX11UIPassOnlyEnabled() &&
		m_pySystem.IsDX11WorldFinalcheckTestEnabled() &&
		m_bDX11RuntimeCompatMode &&
		m_bDX11WorldHandoffProbeMode;
	const bool bDX11HandoffPass9Active = (bDX11HandoffProbeActive && m_bDX11WorldNativePass9Mode);
	const bool bDX11HandoffPass10Active = (bDX11HandoffProbeActive && m_bDX11WorldNativePass10Mode);
	const bool bDX11HandoffPass11Active = (bDX11HandoffProbeActive && m_bDX11WorldNativePass11Mode);
	const bool bDX11HandoffPass12Active = (bDX11HandoffProbeActive && m_bDX11WorldNativePass12Mode);
	const bool bDX11HandoffPass13Active = (bDX11HandoffProbeActive && m_bDX11WorldNativePass13Mode);
	const bool bDX11HandoffPass14Active = (bDX11HandoffProbeActive && m_bDX11WorldNativePass14Mode);
	const bool bDX11HandoffPass15Active = (bDX11HandoffProbeActive && m_bDX11WorldNativePass15Mode);
	const bool bDX11HandoffPass16Active = (bDX11HandoffProbeActive && m_bDX11WorldNativePass16Mode);
	int iDX11HandoffPassLevel = 0;
	if (bDX11HandoffPass16Active)
		iDX11HandoffPassLevel = 16;
	else if (bDX11HandoffPass15Active)
		iDX11HandoffPassLevel = 15;
	else if (bDX11HandoffPass14Active)
		iDX11HandoffPassLevel = 14;
	else if (bDX11HandoffPass13Active)
		iDX11HandoffPassLevel = 13;
	else if (bDX11HandoffPass12Active)
		iDX11HandoffPassLevel = 12;
	else if (bDX11HandoffPass11Active)
		iDX11HandoffPassLevel = 11;
	else if (bDX11HandoffPass10Active)
		iDX11HandoffPassLevel = 10;
	else if (bDX11HandoffPass9Active)
		iDX11HandoffPassLevel = 9;

	const DWORD dwDX11HandoffPassMask =
		(bDX11HandoffPass9Active ? 0x01u : 0u) |
		(bDX11HandoffPass10Active ? 0x02u : 0u) |
		(bDX11HandoffPass11Active ? 0x04u : 0u) |
		(bDX11HandoffPass12Active ? 0x08u : 0u) |
		(bDX11HandoffPass13Active ? 0x10u : 0u) |
		(bDX11HandoffPass14Active ? 0x20u : 0u) |
		(bDX11HandoffPass15Active ? 0x40u : 0u) |
		(bDX11HandoffPass16Active ? 0x80u : 0u);
	const bool bDX11HandoffRenderStress =
		((m_dwRenderFPS > 0 && m_dwRenderFPS < 50) ||
		 (m_dwPerfMapMS > 12) ||
		 (m_dwPerfCharacterMS > 10) ||
		 (m_dwPerfEffectRenderMS > 8) ||
		 (m_dwPerfShadowMS > 6));
	const bool bDX11HandoffReducedFrame =
		(bDX11HandoffProbeActive &&
		 (((iDX11HandoffPassLevel < 9) && ((m_dwPerfRenderFrameCounter & 1u) != 0)) ||
		  ((iDX11HandoffPassLevel >= 9) && bDX11HandoffRenderStress && ((m_dwPerfRenderFrameCounter & 1u) != 0))));
	const bool bDX11HandoffStrongReduction =
		(bDX11HandoffProbeActive &&
		 iDX11HandoffPassLevel >= 14 &&
		 bDX11HandoffRenderStress &&
		 ((m_dwPerfRenderFrameCounter & 1u) != 0));
	const bool bDX11HandoffUltraReduction =
		(bDX11HandoffProbeActive &&
		 iDX11HandoffPassLevel >= 16 &&
		 bDX11HandoffRenderStress &&
		 ((m_dwPerfRenderFrameCounter & 3u) != 0));

	if (bDX11HandoffProbeActive)
	{
		static DWORD s_dwDX11HandoffProbeLogTick = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (0 == s_dwDX11HandoffProbeLogTick || dwNow - s_dwDX11HandoffProbeLogTick >= 5000)
		{
			s_dwDX11HandoffProbeLogTick = dwNow;
			TraceError(
				"DX11_HANDOFF_PROBE heartbeat reduced_frame=%d strong_reduction=%d ultra_reduction=%d stress=%d fps=%u pass9=%d pass_level=%d pass_mask=0x%02X frame=%u elapsed_ms=%u stage=%s grace_active=%d grace_reason=%s grace_used=%u grace_expired=%u grace_coalesced=%u grace_suppressed=%u",
				bDX11HandoffReducedFrame ? 1 : 0,
				bDX11HandoffStrongReduction ? 1 : 0,
				bDX11HandoffUltraReduction ? 1 : 0,
				bDX11HandoffRenderStress ? 1 : 0,
				m_dwRenderFPS,
				bDX11HandoffPass9Active ? 1 : 0,
				iDX11HandoffPassLevel,
				static_cast<unsigned int>(dwDX11HandoffPassMask),
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS,
				GetDX11RuntimeStage(),
				m_bDX11RuntimeCompatGraceMode ? 1 : 0,
				GetDX11RuntimeCompatGraceReason(),
				m_dwDX11RuntimeCompatGraceUsedCount,
				m_dwDX11RuntimeCompatGraceExpiredCount,
				m_dwDX11RuntimeCompatGraceCoalescedCount,
				m_dwDX11RuntimeCompatGraceSuppressedCount);
		}
	}

	float fAspect = m_kWndMgr.GetAspect();
	float fFarClip = m_pyBackground.GetFarClip();
	const float fNearClip = 100.0f;
	if (!(fAspect > 0.0001f))
		fAspect = 1.7777778f;
	if (!(fFarClip > fNearClip + 0.001f))
		fFarClip = fNearClip + 1000.0f;

	m_pyGraphic.SetPerspective(30.0f, fAspect, fNearClip, fFarClip);

	CCullingManager::Instance().Process();

	m_kChrMgr.Deform();

	const DWORD dwShadowStart = ELTimer_GetMSec();
	const bool bDX11LegacyShadowTexturePolicyDisabled =
		(m_eRenderBackend == RENDER_BACKEND_DX11) &&
		!m_pySystem.IsDX11FirstPassActiveEnabled() &&
		m_pySystem.IsDX11NativeVisibleEnabled() &&
		m_pySystem.IsDX11NativeWorldMinimalEnabled();
	static bool s_bDX11LegacyShadowTexturePolicyWasActive = false;
	bool bDX11ShadowReceiverPathEnabled = !bDX11LegacyShadowTexturePolicyDisabled;
	if (bDX11LegacyShadowTexturePolicyDisabled)
	{
		// Render-priority mode: world DX11 present is more important than shadow fidelity.
		// Keep legacy shadow texture path disabled in native world mode.
		bDX11ShadowReceiverPathEnabled = false;

		m_pyBackground.SetDrawShadow(false);
		m_pyBackground.SetDrawCharacterShadow(false);
		s_bDX11LegacyShadowTexturePolicyWasActive = true;
	}
	else if (s_bDX11LegacyShadowTexturePolicyWasActive)
	{
		// Restore user-configured shadow mode when leaving DX11 world shadow policy.
		m_pyBackground.RefreshShadowLevel();
		g_dwDX11ShadowPolicyLogTick = 0;
		s_bDX11LegacyShadowTexturePolicyWasActive = false;
	}
	bool bRenderShadow = bDX11ShadowReceiverPathEnabled;
	if (bRenderShadow && m_iShadowCadence > 1)
	{
		bool bForceShadowPerFrame = false;
		CInstanceBase* pkMainInstance = m_kChrMgr.GetMainInstancePtr();
		if (pkMainInstance && pkMainInstance->IsWalking())
			bForceShadowPerFrame = true;

		if (m_bShadowDynamicBoost)
		{
			CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
			if (pCamera)
			{
				const DirectX::SimpleMath::Vector3 v3CameraEye = pCamera->GetEye();
				if (!m_bHasLastShadowCameraEye)
				{
					m_bHasLastShadowCameraEye = true;
					m_v3LastShadowCameraEye = v3CameraEye;
				}
				else
				{
					const DirectX::SimpleMath::Vector3 v3Diff = v3CameraEye - m_v3LastShadowCameraEye;
					if (v3Diff.Dot(v3Diff) > (80.0f * 80.0f))
						bForceShadowPerFrame = true;

					m_v3LastShadowCameraEye = v3CameraEye;
				}
			}
		}
		else
		{
			m_bHasLastShadowCameraEye = false;
		}

		if (!bForceShadowPerFrame)
		{
			if (((m_dwPerfRenderFrameCounter - 1) % static_cast<DWORD>(m_iShadowCadence)) != 0)
				bRenderShadow = false;
		}
	}
	if (bRenderShadow)
		m_pyBackground.RenderCharacterShadowToTexture();
	else if (bDX11LegacyShadowTexturePolicyDisabled)
	{
		const DWORD dwNow = ELTimer_GetMSec();
		if (0 == g_dwDX11ShadowPolicyLogTick || dwNow - g_dwDX11ShadowPolicyLogTick >= DX11_NATIVE_HEARTBEAT_INTERVAL_MS)
		{
			g_dwDX11ShadowPolicyLogTick = dwNow;
			TraceError(
				"DX11_WORLD_SHADOW_POLICY legacy_shadow_texture=0 receiver_path_disabled=%d dynamic_shadow_planned=0 runtime_compat=%d pass16=%d frame=%u elapsed_ms=%u",
				bDX11ShadowReceiverPathEnabled ? 0 : 1,
				m_bDX11RuntimeCompatMode ? 1 : 0,
				m_bDX11WorldNativePass16Mode ? 1 : 0,
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS);
		}
	}
	m_dwPerfShadowMS = ELTimer_GetMSec() - dwShadowStart;

	m_pyGraphic.SetGameRenderState();
	m_pyGraphic.PushState();

	{
		long lx, ly;
		m_kWndMgr.GetMousePosition(lx, ly);
		m_pyGraphic.SetCursorPosition(lx, ly);
	}

	DWORD dwMapStart = ELTimer_GetMSec();
	m_pyBackground.RenderSky();

	if (!bDX11HandoffReducedFrame)
	{
		m_pyBackground.RenderBeforeLensFlare();
		m_pyBackground.RenderCloud();
	}

	m_pyBackground.BeginEnvironment();
	m_pyBackground.Render();
	m_dwPerfMapMS = ELTimer_GetMSec() - dwMapStart;

	m_pyBackground.SetCharacterDirLight();
	const DWORD dwCharacterStart = ELTimer_GetMSec();
	m_kChrMgr.Render();
	m_dwPerfCharacterMS = ELTimer_GetMSec() - dwCharacterStart;

	dwMapStart = ELTimer_GetMSec();
	m_pyBackground.SetBackgroundDirLight();
	if (!bDX11HandoffStrongReduction)
		m_pyBackground.RenderWater();
	if (!bDX11HandoffReducedFrame)
		m_pyBackground.RenderSnow();
	if (!bDX11HandoffReducedFrame)
		m_pyBackground.RenderRain();
	if (!bDX11HandoffReducedFrame)
		m_pyBackground.RenderStorm();

	// Reset effect submit/render counters after world pass consumed previous-frame values
	// and before any current-frame effect draws start.
	m_kEftMgr.BeginDX11WorldFrameTelemetry();

	if (!bDX11HandoffStrongReduction && !bDX11HandoffUltraReduction)
		m_pyBackground.RenderEffect();

	m_pyBackground.EndEnvironment();
	m_dwPerfMapMS += ELTimer_GetMSec() - dwMapStart;

	const DWORD dwEffectRenderStart = ELTimer_GetMSec();
	m_kEftMgr.Render();
	m_dwPerfEffectRenderMS = ELTimer_GetMSec() - dwEffectRenderStart;

	dwMapStart = ELTimer_GetMSec();
	if (!bDX11HandoffUltraReduction || ((m_dwPerfRenderFrameCounter & 1u) == 0))
		m_pyItem.Render();
	m_FlyingManager.Render();

	m_pyBackground.BeginEnvironment();
	m_pyBackground.RenderPCBlocker();
	m_pyBackground.EndEnvironment();

	if (!bDX11HandoffReducedFrame)
		m_pyBackground.RenderAfterLensFlare();

	// M3-SCREEN-FILTER-FIX: Render screen filter overlay (night tint, atmospheric effects)
	// Must be rendered LAST, after all other world rendering (as final fullscreen overlay)
	if (!bDX11HandoffReducedFrame)
		m_pyBackground.RenderScreenFiltering();

	m_dwPerfMapMS += ELTimer_GetMSec() - dwMapStart;
}

void CPythonApplication::UpdateGame()
{
	POINT ptMouse;
	GetMousePosition(&ptMouse);

	CGraphicTextInstance::Hyperlink_UpdateMousePos(ptMouse.x, ptMouse.y);


	//!@# Alt+Tab ???? SetTransfor ???????? ???????? ???????? ???????????? ???????? - [levites]
	//if (m_isActivateWnd)
	{
		CScreen s;
		float fAspect = UI::CWindowManager::Instance().GetAspect();
		float fFarClip = CPythonBackground::Instance().GetFarClip();
		const float fNearClip = 100.0f;
		if (!(fAspect > 0.0001f))
			fAspect = 1.7777778f;
		if (!(fFarClip > fNearClip + 0.001f))
			fFarClip = fNearClip + 1000.0f;

		s.SetPerspective(30.0f, fAspect, fNearClip, fFarClip);
		s.BuildViewFrustum();
	}

	TPixelPosition kPPosMainActor;
	m_pyPlayer.NEW_GetMainActorPosition(&kPPosMainActor);

	m_pyBackground.Update(kPPosMainActor.x, kPPosMainActor.y, kPPosMainActor.z);

	m_GameEventManager.SetCenterPosition(kPPosMainActor.x, kPPosMainActor.y, kPPosMainActor.z);
	m_GameEventManager.Update();

	m_kChrMgr.Update();

	const DWORD dwEffectUpdateStart = ELTimer_GetMSec();
	m_kEftMgr.Update();
	m_kEftMgr.UpdateSound();
	m_dwPerfEffectUpdateMS = ELTimer_GetMSec() - dwEffectUpdateStart;
	
	m_FlyingManager.Update();
	m_pyItem.Update(ptMouse);
	m_pyPlayer.Update();

	// NOTE : Update ???????? ???????? ???????? ???????????????? ???????? ???????? ???????????? - [levites]
	//        ???? ???????? ???????????? ???????? ???????????????? Sound???? ???????? ???????????????? ???????????? ???????? ???????????? ????????????.
	m_pyPlayer.NEW_GetMainActorPosition(&kPPosMainActor);
	SetCenterPosition(kPPosMainActor.x, kPPosMainActor.y, kPPosMainActor.z);
}

bool CPythonApplication::Process()
{
	ELTimer_SetFrameMSec();
	DWORD dwStart = ELTimer_GetMSec();
	static DWORD s_dwUpdateFrameCount = 0;
	static DWORD s_dwRenderFrameCount = 0;
	static DWORD s_dwFaceCount = 0;
	static UINT s_uiLoad = 0;
	static DWORD s_dwCheckTime = ELTimer_GetMSec();

	DWORD dwNow = ELTimer_GetMSec();
	if (dwNow - s_dwCheckTime > 1000) [[unlikely]]
	{
		m_dwUpdateFPS = s_dwUpdateFrameCount;
		m_dwRenderFPS = s_dwRenderFrameCount;
		m_dwLoad = s_uiLoad;
		m_dwFaceCount = s_dwFaceCount / std::max(1ul, s_dwRenderFrameCount);
		s_dwCheckTime = dwNow;
		s_uiLoad = s_dwFaceCount = s_dwUpdateFrameCount = s_dwRenderFrameCount = 0;
	}

	if (m_dwNextUpdateTime == 0)
		m_dwNextUpdateTime = dwNow;
	if (m_dNextRenderTimeMS <= 0.0)
		m_dNextRenderTimeMS = static_cast<double>(dwNow);

	const int iMaxCatchUpUpdates = m_isFrameSkipDisable ? 1 : 5;
	int iUpdateSteps = 0;
	while (dwNow >= m_dwNextUpdateTime && iUpdateSteps < iMaxCatchUpUpdates)
	{
		__RunUpdateStep(s_dwUpdateFrameCount);
		++iUpdateSteps;

		const DWORD dwFrameStep = std::max(1ul, CTimer::Instance().GetElapsedMilliecond());
		m_dwNextUpdateTime += dwFrameStep;
		dwNow = ELTimer_GetMSec();
	}

	if (iUpdateSteps == iMaxCatchUpUpdates && dwNow > m_dwNextUpdateTime + 500)
	{
		const DWORD dwDelta = dwNow - m_dwNextUpdateTime;
		const DWORD dwAdjust = (dwDelta / 16) * 16;
		if (dwAdjust > 0)
		{
			m_dwNextUpdateTime += dwAdjust;
			CTimer::Instance().Adjust(dwAdjust);
		}
	}

	bool shouldRender = false;
	dwNow = ELTimer_GetMSec();
	if (0 == m_iFPS)
	{
		shouldRender = true;
		m_dNextRenderTimeMS = static_cast<double>(dwNow);
	}
	else if (static_cast<double>(dwNow) >= m_dNextRenderTimeMS)
	{
		shouldRender = true;
		do
		{
			m_dNextRenderTimeMS += m_fRenderFrameIntervalMS;
		}
		while (m_dNextRenderTimeMS <= static_cast<double>(dwNow));
	}

	if (shouldRender)
		__RunRenderStep(s_dwRenderFrameCount, s_dwFaceCount);

	const DWORD dwBeforeSleep = ELTimer_GetMSec();
	__SleepFrame(dwBeforeSleep, m_dwNextUpdateTime);
	const DWORD dwAfterSleep = ELTimer_GetMSec();
	if (dwAfterSleep > dwBeforeSleep)
	{
		const DWORD dwSlept = dwAfterSleep - dwBeforeSleep;
		s_uiLoad = (s_uiLoad > dwSlept) ? (s_uiLoad - dwSlept) : 0;
	}

	s_uiLoad += ELTimer_GetMSec() - dwStart;
	return true;
}

int CPythonApplication::__NormalizeFPSLimit(int iFPS) const
{
	switch (iFPS)
	{
		case 0:
		case 60:
		case 90:
		case 120:
			return iFPS;
		default:
			return 60;
	}
}

int CPythonApplication::__NormalizePerfProfile(int iProfile) const
{
	if (iProfile < 0)
		return 1;
	if (iProfile > 2)
		return 1;

	return iProfile;
}

int CPythonApplication::__NormalizeShadowCadence(int iCadence) const
{
	if (iCadence < 1)
		return 1;
	if (iCadence > 3)
		return 3;

	return iCadence;
}

int CPythonApplication::__NormalizeFXStrideBias(int iBias) const
{
	if (iBias < 0)
		return 0;
	if (iBias > 2)
		return 2;

	return iBias;
}

void CPythonApplication::__ApplyPerformanceSettings()
{
	m_kEftMgr.SetPerformanceSettings(m_iPerfProfile, m_bFXAdaptive, m_bPerfAutoReduced, m_iFXStrideBias);
	m_kChrMgr.SetAnimationLODSettings(m_bAnimLOD, m_iPerfProfile);
	m_pyTextTail.SetOptimizationSettings(m_bTextTailOpt, m_iPerfProfile);
	m_pyTextTail.SetGridOptimizationEnabled(m_bTextTailGridOpt);
	m_pyTextTail.SetOptimizationRange(static_cast<float>(m_iTextTailOptRange));
}

void CPythonApplication::__UpdatePerfAutoAdjustment()
{
	if (!m_bFXAdaptive)
	{
		if (m_bPerfAutoReduced)
		{
			m_bPerfAutoReduced = false;
			__ApplyPerformanceSettings();
		}
		m_dwPerfOverBudgetFrames = 0;
		m_dwPerfUnderBudgetFrames = 0;
		return;
	}

	if (m_iPerfProfile == 0)
	{
		if (m_bPerfAutoReduced)
		{
			m_bPerfAutoReduced = false;
			__ApplyPerformanceSettings();
		}
		m_dwPerfOverBudgetFrames = 0;
		m_dwPerfUnderBudgetFrames = 0;
		return;
	}

	const bool bDX11NativePresentMode =
		(m_eRenderBackend == RENDER_BACKEND_DX11) &&
		!m_pySystem.IsDX11FirstPassActiveEnabled() &&
		m_pySystem.IsDX11NativeVisibleEnabled();

	const float fTargetMS = (m_iFPS > 0) ? (1000.0f / static_cast<float>(m_iFPS)) : 16.667f;
	const float fOverBudgetMS = fTargetMS * (bDX11NativePresentMode ? 1.22f : 1.35f);
	const float fUnderBudgetMS = fTargetMS * (bDX11NativePresentMode ? 0.93f : 0.90f);
	const DWORD dwOverBudgetFramesThreshold = bDX11NativePresentMode ? 12u : 24u;
	const DWORD dwUnderBudgetFramesThreshold = bDX11NativePresentMode ? 210u : 180u;

	if (static_cast<float>(m_dwCurRenderTime) > fOverBudgetMS)
	{
		++m_dwPerfOverBudgetFrames;
		m_dwPerfUnderBudgetFrames = 0;
	}
	else if (static_cast<float>(m_dwCurRenderTime) < fUnderBudgetMS)
	{
		++m_dwPerfUnderBudgetFrames;
		if (m_dwPerfOverBudgetFrames > 0)
			--m_dwPerfOverBudgetFrames;
	}
	else
	{
		if (m_dwPerfOverBudgetFrames > 0)
			--m_dwPerfOverBudgetFrames;
		if (m_dwPerfUnderBudgetFrames > 0)
			--m_dwPerfUnderBudgetFrames;
	}

	if (!m_bPerfAutoReduced && m_dwPerfOverBudgetFrames >= dwOverBudgetFramesThreshold)
	{
		m_bPerfAutoReduced = true;
		m_dwPerfOverBudgetFrames = 0;
		m_dwPerfUnderBudgetFrames = 0;
		__ApplyPerformanceSettings();
		TraceError(
			"DX11_PERF_AUTO_REDUCE state=on backend=%s stage=%s render_ms=%u target_ms=%.2f over_ms=%.2f map_ms=%u chr_ms=%u fx_ms=%u shadow_ms=%u",
			GetRenderBackend(),
			GetDX11RuntimeStage(),
			m_dwCurRenderTime,
			fTargetMS,
			fOverBudgetMS,
			m_dwPerfMapMS,
			m_dwPerfCharacterMS,
			m_dwPerfEffectRenderMS,
			m_dwPerfShadowMS);
	}
	else if (m_bPerfAutoReduced && m_dwPerfUnderBudgetFrames >= dwUnderBudgetFramesThreshold)
	{
		m_bPerfAutoReduced = false;
		m_dwPerfOverBudgetFrames = 0;
		m_dwPerfUnderBudgetFrames = 0;
		__ApplyPerformanceSettings();
		TraceError(
			"DX11_PERF_AUTO_REDUCE state=off backend=%s stage=%s render_ms=%u target_ms=%.2f under_ms=%.2f map_ms=%u chr_ms=%u fx_ms=%u shadow_ms=%u",
			GetRenderBackend(),
			GetDX11RuntimeStage(),
			m_dwCurRenderTime,
			fTargetMS,
			fUnderBudgetMS,
			m_dwPerfMapMS,
			m_dwPerfCharacterMS,
			m_dwPerfEffectRenderMS,
			m_dwPerfShadowMS);
	}
}

void CPythonApplication::__UpdateRenderFrameInterval()
{
	if (m_iFPS <= 0)
		m_fRenderFrameIntervalMS = 0.0f;
	else
		m_fRenderFrameIntervalMS = 1000.0f / static_cast<float>(m_iFPS);
}

void CPythonApplication::__RunUpdateStep(DWORD& rUpdateFrameCount)
{
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime1 = ELTimer_GetMSec();
#endif
	CTimer& rkTimer = CTimer::Instance();
	rkTimer.Advance();

	m_fGlobalTime = rkTimer.GetCurrentSecond();
	m_fGlobalElapsedTime = rkTimer.GetElapsedSecond();

	DWORD updatestart = ELTimer_GetMSec();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime2 = ELTimer_GetMSec();
#endif
	m_pyNetworkStream.Process();
	m_kGuildMarkUploader.Process();
	m_kGuildMarkDownloader.Process();
	m_kAccountConnector.Process();

#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime3 = ELTimer_GetMSec();
#endif
	UpdateKeyboard();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime4 = ELTimer_GetMSec();
#endif
	POINT Point;
	if (GetCursorPos(&Point)) [[likely]]
	{
		ScreenToClient(m_hWnd, &Point);
		OnMouseMove(Point.x, Point.y);
	}
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime5 = ELTimer_GetMSec();
#endif
	__UpdateCamera();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime6 = ELTimer_GetMSec();
#endif
	CResourceManager::Instance().Update();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime7 = ELTimer_GetMSec();
#endif
	OnCameraUpdate();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime8 = ELTimer_GetMSec();
#endif
	OnMouseUpdate();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime9 = ELTimer_GetMSec();
#endif
	OnUIUpdate();

#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime10 = ELTimer_GetMSec();

	if (dwUpdateTime10 - dwUpdateTime1 > 10)
	{
		static FILE* fp = fopen("perf_app_update.txt", "w");

		fprintf(fp, "AU.Total %d (Time %d)\n", dwUpdateTime9 - dwUpdateTime1, ELTimer_GetMSec());
		fprintf(fp, "AU.TU %d\n", dwUpdateTime2 - dwUpdateTime1);
		fprintf(fp, "AU.NU %d\n", dwUpdateTime3 - dwUpdateTime2);
		fprintf(fp, "AU.KU %d\n", dwUpdateTime4 - dwUpdateTime3);
		fprintf(fp, "AU.MP %d\n", dwUpdateTime5 - dwUpdateTime4);
		fprintf(fp, "AU.CP %d\n", dwUpdateTime6 - dwUpdateTime5);
		fprintf(fp, "AU.RU %d\n", dwUpdateTime7 - dwUpdateTime6);
		fprintf(fp, "AU.CU %d\n", dwUpdateTime8 - dwUpdateTime7);
		fprintf(fp, "AU.MU %d\n", dwUpdateTime9 - dwUpdateTime8);
		fprintf(fp, "AU.UU %d\n", dwUpdateTime10 - dwUpdateTime9);
		fprintf(fp, "----------------------------------\n");
		fflush(fp);
	}
#endif

	m_dwCurUpdateTime = ELTimer_GetMSec() - updatestart;
	m_dwPerfActiveEffects = m_kEftMgr.GetActiveEffectCount();
	m_dwPerfActiveParticles = m_kEftMgr.GetActiveParticleCount();
	m_dwPerfVisibleTextTails = m_pyTextTail.GetVisibleTextTailCount();
	m_dwPerfTextTailMS = m_pyTextTail.GetLastFrameMS();
	m_dwPerfTextTailCollisionChecks = m_pyTextTail.GetLastCollisionCheckCount();
	++rUpdateFrameCount;
}

void CPythonApplication::__RunRenderStep(DWORD& rRenderFrameCount, DWORD& rFaceCount)
{
	// DX9 path requires StateManager singleton. In strict DX11 cutover this singleton can be absent,
	// and hard-returning here would skip the entire frame before BeginFrame/Present.
	if (!CStateManager::InstancePtr())
	{
		if (m_eRenderBackend != RENDER_BACKEND_DX11)
			return;

		static bool s_bDX11StateManagerBypassLogged = false;
		if (!s_bDX11StateManagerBypassLogged)
		{
			s_bDX11StateManagerBypassLogged = true;
			TraceError("DX11_RENDER_GUARD state_manager_missing_bypassed backend=dx11");
		}
	}

	++m_dwPerfRenderFrameCounter;

	// Keep specular scroll speed stable per-second (not per-frame),
	// so "Unlimited" FPS does not accelerate armor glow animation.
	const DWORD dwSpecularNow = ELTimer_GetMSec();
	static DWORD s_dwLastSpecularMS = dwSpecularNow;
	DWORD dwSpecularDeltaMS = dwSpecularNow - s_dwLastSpecularMS;
	s_dwLastSpecularMS = dwSpecularNow;

	if (dwSpecularDeltaMS > 100)
		dwSpecularDeltaMS = 100;

	const float fSpecularFrameScale = static_cast<float>(dwSpecularDeltaMS) / (1000.0f / 60.0f);
	const float fSpecularStep = static_cast<float>(g_specularSpd) * fSpecularFrameScale;
	CGrannyMaterial::TranslateSpecularMatrix(fSpecularStep, fSpecularStep, 0.0f);

	DWORD dwRenderStartTime = ELTimer_GetMSec();
	bool canRender = true;
	const bool bDX11BackendActive = (m_eRenderBackend == RENDER_BACKEND_DX11);
	const bool bDX11FirstPassHybrid = (bDX11BackendActive && m_pySystem.IsDX11FirstPassActiveEnabled());
	const bool bDX11CutoverRuntimeMode = (bDX11BackendActive && !bDX11FirstPassHybrid);
	// Runtime control: use Python config instead of compile-time DX11_STRICT_ONLY define
	const bool bDX11StrictNativeOnlyBuild = m_pySystem.IsDX11StrictNativeOnlyEnabled();
	const bool bDX11NativeVisibleConfigEnabled = (bDX11CutoverRuntimeMode && m_pySystem.IsDX11NativeVisibleEnabled());
	bool bDX11NativeUIMinimalConfigEnabled = (bDX11NativeVisibleConfigEnabled && m_pySystem.IsDX11NativeUIMinimalEnabled());
	bool bDX11NativeWorldMinimalConfigEnabled = (bDX11NativeVisibleConfigEnabled && m_pySystem.IsDX11NativeWorldMinimalEnabled());
	const bool bDX11NativeWorldAutoGateConfigValue = m_pySystem.IsDX11NativeWorldAutoGateEnabled();
	const bool bDX11StrictNativeOnlyEnabled =
		(bDX11CutoverRuntimeMode && (bDX11StrictNativeOnlyBuild || m_pySystem.IsDX11StrictNativeOnlyEnabled()));
	const bool bDX11TerrainStabilizationMode = (bDX11CutoverRuntimeMode && m_pySystem.IsDX11TerrainStabilizationModeEnabled());
	const bool bDX11NativeWorldAutoGateConfigEnabled =
		bDX11NativeVisibleConfigEnabled &&
		(bDX11StrictNativeOnlyEnabled || (bDX11NativeWorldAutoGateConfigValue && !bDX11TerrainStabilizationMode));
	// Full DX11 migration policy: disable DX9 bridge texture path in runtime.
	// Config value is intentionally ignored here until full-port stage is completed.
	const int iDX11TexturePipelineModeConfig = m_pySystem.GetDX11TexturePipelineMode();
	(void)iDX11TexturePipelineModeConfig;
	CGraphicDeviceDX11::EDX11TexturePipelineMode eDX11TexturePipelineMode = CGraphicDeviceDX11::DX11_TEXTURE_PIPELINE_NATIVE;
	if (bDX11BackendActive)
		m_grpDeviceDX11.SetDX11TexturePipelineMode(eDX11TexturePipelineMode);
	const bool bDX11VisibleBootstrap = (bDX11FirstPassHybrid && m_pySystem.IsDX11VisibleBootstrapEnabled());
	if (bDX11CutoverRuntimeMode)
	{
		// Keep user/runtime-selected minimal toggles intact during cutover.
		// Forcing them OFF caused missing UI/world content on some maps/sessions.
		bDX11NativeUIMinimalConfigEnabled = (bDX11NativeVisibleConfigEnabled && m_pySystem.IsDX11NativeUIMinimalEnabled());
		bDX11NativeWorldMinimalConfigEnabled = (bDX11NativeVisibleConfigEnabled && m_pySystem.IsDX11NativeWorldMinimalEnabled());
		if (bDX11NativeVisibleConfigEnabled &&
			!bDX11NativeUIMinimalConfigEnabled &&
			!bDX11StrictNativeOnlyEnabled)
		{
			m_pySystem.SetDX11NativeUIMinimalEnabled(true);
			bDX11NativeUIMinimalConfigEnabled = true;
			TraceError("DX11_NATIVE_UI_POLICY action=force_on reason=native_visible_cutover");
		}

		const bool bAnyDX11BootstrapTestFlags =
			m_pySystem.IsDX11VisibleBootstrapEnabled() ||
			m_pySystem.IsDX11UIPassOnlyEnabled() ||
			m_pySystem.IsDX11UINativeTestEnabled() ||
			m_pySystem.IsDX11UITextureTestEnabled() ||
			m_pySystem.IsDX11WorldDepthTestEnabled() ||
			m_pySystem.IsDX11WorldBatchTestEnabled() ||
			m_pySystem.IsDX11WorldSpriteTestEnabled() ||
			m_pySystem.IsDX11WorldStateTestEnabled() ||
			m_pySystem.IsDX11WorldPassesTestEnabled() ||
			m_pySystem.IsDX11WorldBridgeTestEnabled() ||
			m_pySystem.IsDX11WorldSubsystemTestEnabled() ||
			m_pySystem.IsDX11WorldRealtimeTestEnabled() ||
			m_pySystem.IsDX11WorldMetricsTestEnabled() ||
			m_pySystem.IsDX11WorldInstanceFeedTestEnabled() ||
			m_pySystem.IsDX11WorldFinalcheckTestEnabled() ||
			m_pySystem.IsDX11WorldHandoffTestEnabled() ||
			m_pySystem.IsDX11WorldSwapchainTestEnabled() ||
			m_pySystem.IsDX11WorldPresentPathTestEnabled() ||
			m_pySystem.IsDX11WorldVisiblePass1TestEnabled() ||
			m_pySystem.IsDX11WorldComposerTestEnabled() ||
			m_pySystem.IsDX11WorldScenegraphTestEnabled() ||
			m_pySystem.IsDX11WorldPipelineTestEnabled() ||
			m_pySystem.IsDX11WorldFramegraphTestEnabled();
		if (bAnyDX11BootstrapTestFlags)
		{
			m_pySystem.SetDX11VisibleBootstrapEnabled(false);
			m_pySystem.SetDX11UIPassOnlyEnabled(false);
			m_pySystem.SetDX11UINativeTestEnabled(false);
			m_pySystem.SetDX11UITextureTestEnabled(false);
			m_pySystem.SetDX11WorldDepthTestEnabled(false);
			m_pySystem.SetDX11WorldBatchTestEnabled(false);
			m_pySystem.SetDX11WorldSpriteTestEnabled(false);
			m_pySystem.SetDX11WorldStateTestEnabled(false);
			m_pySystem.SetDX11WorldPassesTestEnabled(false);
			m_pySystem.SetDX11WorldBridgeTestEnabled(false);
			m_pySystem.SetDX11WorldSubsystemTestEnabled(false);
			m_pySystem.SetDX11WorldRealtimeTestEnabled(false);
			m_pySystem.SetDX11WorldMetricsTestEnabled(false);
			m_pySystem.SetDX11WorldInstanceFeedTestEnabled(false);
			m_pySystem.SetDX11WorldFinalcheckTestEnabled(false);
			m_pySystem.SetDX11WorldHandoffTestEnabled(false);
			m_pySystem.SetDX11WorldSwapchainTestEnabled(false);
			m_pySystem.SetDX11WorldPresentPathTestEnabled(false);
			m_pySystem.SetDX11WorldVisiblePass1TestEnabled(false);
			m_pySystem.SetDX11WorldComposerTestEnabled(false);
			m_pySystem.SetDX11WorldScenegraphTestEnabled(false);
			m_pySystem.SetDX11WorldPipelineTestEnabled(false);
			m_pySystem.SetDX11WorldFramegraphTestEnabled(false);

			static DWORD s_dwDX11TestFlagsResetLogTick = 0;
			const DWORD dwResetNow = ELTimer_GetMSec();
			if (0 == s_dwDX11TestFlagsResetLogTick || dwResetNow - s_dwDX11TestFlagsResetLogTick >= 1000)
			{
				s_dwDX11TestFlagsResetLogTick = dwResetNow;
				TraceError("DX11_TEST_FLAGS_RUNTIME_RESET reason=cutover_runtime");
			}
		}
	}
	const bool bDX11UINativeConfigEnabledRaw = m_pySystem.IsDX11UINativeTestEnabled();
	// Hard safety: in cutover-runtime we never allow bootstrap DX11 test scenes to become visible.
	const bool bDX11ForceDisableUINativeTests = bDX11CutoverRuntimeMode;
	const bool bDX11UINativeConfigEnabled = (bDX11UINativeConfigEnabledRaw && !bDX11ForceDisableUINativeTests);
	const bool bDX11UINativeTestSuppressedByNativeVisible =
		(bDX11BackendActive && bDX11NativeVisibleConfigEnabled && bDX11UINativeConfigEnabled);
	const bool bDX11UINativeTest =
		(bDX11BackendActive && !bDX11VisibleBootstrap && bDX11UINativeConfigEnabled && !bDX11UINativeTestSuppressedByNativeVisible);
	if (bDX11ForceDisableUINativeTests && bDX11UINativeConfigEnabledRaw)
	{
		static bool s_bDX11ForceDisableUINativeLogged = false;
		if (!s_bDX11ForceDisableUINativeLogged)
		{
			s_bDX11ForceDisableUINativeLogged = true;
			TraceError("DX11_UI_NATIVE_TEST forced_off reason=cutover_runtime");
		}
	}
	const bool bDX11UITextureTest = (bDX11UINativeTest && m_pySystem.IsDX11UITextureTestEnabled());
	const bool bDX11WorldDepthTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldDepthTestEnabled());
	const bool bDX11WorldBatchTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldBatchTestEnabled());
	const bool bDX11WorldSpriteTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldSpriteTestEnabled());
	const bool bDX11WorldStateTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldStateTestEnabled());
	const bool bDX11WorldPassesTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldPassesTestEnabled());
	const bool bDX11WorldBridgeTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldBridgeTestEnabled());
	const bool bDX11WorldSubsystemTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldSubsystemTestEnabled());
	const bool bDX11WorldRealtimeTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldRealtimeTestEnabled());
	const bool bDX11WorldMetricsTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldMetricsTestEnabled());
	const bool bDX11WorldInstanceFeedTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldInstanceFeedTestEnabled());
	const bool bDX11WorldFinalcheckTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldFinalcheckTestEnabled());
	const bool bDX11RuntimeFinalcheckGate = (bDX11WorldFinalcheckTest || bDX11CutoverRuntimeMode);
	const bool bDX11WorldHandoffTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldHandoffTestEnabled());
	const bool bDX11WorldSwapchainTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldSwapchainTestEnabled());
	const bool bDX11WorldPresentPathTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldPresentPathTestEnabled());
	const bool bDX11WorldComposerTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldComposerTestEnabled());
	const bool bDX11WorldScenegraphTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldScenegraphTestEnabled());
	const bool bDX11WorldPipelineTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldPipelineTestEnabled());
	const bool bDX11WorldFramegraphTest = (bDX11UINativeTest && m_pySystem.IsDX11WorldFramegraphTestEnabled());
	const bool bDX11RuntimeStressNow =
		(!m_isMinimizedWnd &&
		 ((m_dwRenderFPS > 0 && m_dwRenderFPS < 50) ||
		  (m_dwPerfMapMS > 12) ||
		  (m_dwPerfCharacterMS > 10) ||
		  (m_dwPerfEffectRenderMS > 8) ||
		  (m_dwPerfShadowMS > 6)));
	static const char* s_szDX11LastRenderCheckpoint = "runstep_init";
	auto __SetDX11RenderCheckpoint = [&](const char* c_szCheckpoint)
	{
		s_szDX11LastRenderCheckpoint = c_szCheckpoint;
	};
	auto __LogDX11RenderCheckpoint = [&](bool bForce)
	{
		if (m_eRenderBackend != RENDER_BACKEND_DX11 || !m_pySystem.IsDX11FirstPassActiveEnabled())
			return;

		static DWORD s_dwDX11RenderCheckpointLogTick = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (bForce || 0 == s_dwDX11RenderCheckpointLogTick || dwNow - s_dwDX11RenderCheckpointLogTick >= 5000)
		{
			s_dwDX11RenderCheckpointLogTick = dwNow;
			TraceError(
				"DX11_RENDER_CHECKPOINT cp=%s frame=%u elapsed_ms=%u stage=%s",
				s_szDX11LastRenderCheckpoint,
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS,
				GetDX11RuntimeStage());
		}
	};
	static std::string s_stLastBlockerSubsystem;
	static std::string s_stLastBlockerDetail;
	static DWORD s_dwLastBlockerTick = 0;
	static DWORD s_dwFirstBlockerTick = 0;
	static DWORD s_dwLastBlockerSeenTick = 0;
	static DWORD s_dwBlockerRepeatCount = 0;
	auto __FlushDX11NativeBlockerResolved = [&](const char* c_szReason)
	{
		if (0 == s_dwBlockerRepeatCount || s_stLastBlockerSubsystem.empty() || s_stLastBlockerDetail.empty())
			return;

		const DWORD dwDurationMS =
			(s_dwLastBlockerSeenTick >= s_dwFirstBlockerTick)
				? (s_dwLastBlockerSeenTick - s_dwFirstBlockerTick)
				: 0;
		TraceError(
			"DX11_NATIVE_BLOCKER_RESOLVED subsystem=%s detail=%s repeats=%u duration_ms=%u reason=%s frame=%u elapsed_ms=%u",
			s_stLastBlockerSubsystem.c_str(),
			s_stLastBlockerDetail.c_str(),
			s_dwBlockerRepeatCount,
			dwDurationMS,
			c_szReason,
			m_dwDX11RuntimeCompatFrameCount,
			m_dwDX11RuntimeCompatElapsedMS);
	};
	auto __ClearDX11NativeBlockerState = [&]()
	{
		s_stLastBlockerSubsystem.clear();
		s_stLastBlockerDetail.clear();
		s_dwBlockerRepeatCount = 0;
		s_dwLastBlockerTick = 0;
		s_dwFirstBlockerTick = 0;
		s_dwLastBlockerSeenTick = 0;
	};
	auto __ResolveDX11NativeBlockerSubsystem = [&](const char* c_szSubsystem, const char* c_szReason)
	{
		if (!c_szSubsystem || s_stLastBlockerSubsystem.empty())
			return;
		if (s_stLastBlockerSubsystem != c_szSubsystem)
			return;
		__FlushDX11NativeBlockerResolved(c_szReason);
		__ClearDX11NativeBlockerState();
	};
	auto __LogDX11NativeBlocker = [&](const char* c_szSubsystem, const char* c_szDetail)
	{

		if (!bDX11NativeVisibleConfigEnabled)
		{
			__FlushDX11NativeBlockerResolved("native_visible_cfg_off");
			__ClearDX11NativeBlockerState();
			return;
		}

		const DWORD dwNow = ELTimer_GetMSec();
		if (s_stLastBlockerSubsystem != c_szSubsystem || s_stLastBlockerDetail != c_szDetail)
		{
			__FlushDX11NativeBlockerResolved("changed");
			s_stLastBlockerSubsystem = c_szSubsystem;
			s_stLastBlockerDetail = c_szDetail;
			s_dwBlockerRepeatCount = 0;
			s_dwLastBlockerTick = 0;
			s_dwFirstBlockerTick = dwNow;
			s_dwLastBlockerSeenTick = dwNow;
		}

		if (s_dwBlockerRepeatCount < 0xffffffffu)
			++s_dwBlockerRepeatCount;
		s_dwLastBlockerSeenTick = dwNow;

		if (s_dwBlockerRepeatCount <= 3u ||
			0 == s_dwLastBlockerTick ||
			dwNow - s_dwLastBlockerTick >= DX11_BLOCKER_REPEAT_LOG_INTERVAL_MS)
		{
			s_dwLastBlockerTick = dwNow;
			TraceError(
				"DX11_NATIVE_BLOCKER subsystem=%s detail=%s repeat=%u frame=%u elapsed_ms=%u",
				c_szSubsystem,
				c_szDetail,
				s_dwBlockerRepeatCount,
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS);
		}
	};
	__SetDX11RenderCheckpoint("runstep_begin");
	const DWORD dwDX11RuntimeCompatFramePrev = m_dwDX11RuntimeCompatFrameCount;
	const DWORD dwDX11RuntimeCompatElapsedPrev = m_dwDX11RuntimeCompatElapsedMS;
	const char* c_szDX11RuntimeStagePrev = GetDX11RuntimeStage();
	const int iDX11RuntimeCompatGraceReasonPrev = m_iDX11RuntimeCompatGraceReasonMask;
	const DWORD dwRuntimeNowMS = ELTimer_GetMSec();
	const bool bDX11RuntimeCompatFinalcheckEnabled = bDX11RuntimeFinalcheckGate;
	const bool bDX11RuntimeCompatHasMainInstance = (NULL != m_kChrMgr.GetMainInstancePtr());
	const DWORD kDX11RuntimeCompatGraceMS = 3000u;
	const DWORD kDX11RuntimeCompatGraceReentryCoalesceMS = 1500u;
	const DWORD kDX11RuntimeCompatGraceDebounceMS = 200u;
	const int iDX11RuntimeCompatGracePendingReasonPrev = m_iDX11RuntimeCompatGracePendingReasonMask;
	bool bDX11RuntimeCompatGracePendingStart = false;
	bool bDX11RuntimeCompatGracePendingClear = false;
	DWORD dwDX11RuntimeCompatGracePendingMS = 0;
	int iDX11RuntimeCompatGraceReasonRaw = 0;
	if (!bDX11RuntimeCompatHasMainInstance)
		iDX11RuntimeCompatGraceReasonRaw |= DX11_COMPAT_GRACE_REASON_MAIN_INSTANCE_MISSING;
	if (!bDX11RuntimeCompatFinalcheckEnabled)
		iDX11RuntimeCompatGraceReasonRaw |= DX11_COMPAT_GRACE_REASON_FINALCHECK_MISSING;
	int iDX11RuntimeCompatGraceReasonNow = iDX11RuntimeCompatGraceReasonRaw;
	if (m_bDX11RuntimeCompatMode && 0 != iDX11RuntimeCompatGraceReasonRaw)
	{
		if (0 == m_dwDX11RuntimeCompatGracePendingSinceMS ||
			m_iDX11RuntimeCompatGracePendingReasonMask != iDX11RuntimeCompatGraceReasonRaw)
		{
			m_dwDX11RuntimeCompatGracePendingSinceMS = dwRuntimeNowMS;
			m_iDX11RuntimeCompatGracePendingReasonMask = iDX11RuntimeCompatGraceReasonRaw;
			bDX11RuntimeCompatGracePendingStart = true;
		}

		dwDX11RuntimeCompatGracePendingMS = dwRuntimeNowMS - m_dwDX11RuntimeCompatGracePendingSinceMS;
		if (dwDX11RuntimeCompatGracePendingMS < kDX11RuntimeCompatGraceDebounceMS)
		{
			iDX11RuntimeCompatGraceReasonNow = 0;
		}
	}
	else if (m_bDX11RuntimeCompatMode && (0 != m_dwDX11RuntimeCompatGracePendingSinceMS || 0 != m_iDX11RuntimeCompatGracePendingReasonMask))
	{
		bDX11RuntimeCompatGracePendingClear = true;
		dwDX11RuntimeCompatGracePendingMS = dwRuntimeNowMS - m_dwDX11RuntimeCompatGracePendingSinceMS;
		if (dwDX11RuntimeCompatGracePendingMS < kDX11RuntimeCompatGraceDebounceMS)
		{
			if (m_dwDX11RuntimeCompatGraceSuppressedCount < 0xffffffffu)
				++m_dwDX11RuntimeCompatGraceSuppressedCount;
		}
		m_dwDX11RuntimeCompatGracePendingSinceMS = 0;
		m_iDX11RuntimeCompatGracePendingReasonMask = 0;
	}
	else if (!m_bDX11RuntimeCompatMode)
	{
		m_dwDX11RuntimeCompatGracePendingSinceMS = 0;
		m_iDX11RuntimeCompatGracePendingReasonMask = 0;
	}

	const bool bDX11RuntimeCompatReadyNow = (0 == iDX11RuntimeCompatGraceReasonNow);

	bool bDX11RuntimeCompatGraceMode = false;
	bool bDX11RuntimeCompatGraceExpired = false;
	bool bDX11RuntimeCompatGraceEnterCoalesced = false;
	bool bDX11RuntimeCompatMode = false;

	if (bDX11RuntimeCompatReadyNow)
	{
		bDX11RuntimeCompatMode = true;
		m_dwDX11RuntimeCompatGraceUntilMS = 0;
		m_iDX11RuntimeCompatGraceReasonMask = 0;
	}
	else if (m_bDX11RuntimeCompatMode)
	{
		if (0 == m_dwDX11RuntimeCompatGraceUntilMS)
		{
			m_dwDX11RuntimeCompatGraceUntilMS = dwRuntimeNowMS + kDX11RuntimeCompatGraceMS;
			m_dwDX11RuntimeCompatLastGraceEnterMS = dwRuntimeNowMS;
			if (m_dwDX11RuntimeCompatLastGraceLeaveMS > 0 &&
				dwRuntimeNowMS > m_dwDX11RuntimeCompatLastGraceLeaveMS &&
				(dwRuntimeNowMS - m_dwDX11RuntimeCompatLastGraceLeaveMS) <= kDX11RuntimeCompatGraceReentryCoalesceMS)
			{
				bDX11RuntimeCompatGraceEnterCoalesced = true;
				if (m_dwDX11RuntimeCompatGraceCoalescedCount < 0xffffffffu)
					++m_dwDX11RuntimeCompatGraceCoalescedCount;
			}
			else
			{
				if (m_dwDX11RuntimeCompatGraceUsedCount < 0xffffffffu)
					++m_dwDX11RuntimeCompatGraceUsedCount;
			}
		}
		m_iDX11RuntimeCompatGraceReasonMask = iDX11RuntimeCompatGraceReasonNow;

		if (dwRuntimeNowMS <= m_dwDX11RuntimeCompatGraceUntilMS)
		{
			bDX11RuntimeCompatMode = true;
			bDX11RuntimeCompatGraceMode = true;
		}
		else
		{
			bDX11RuntimeCompatGraceExpired = true;
			if (m_dwDX11RuntimeCompatGraceExpiredCount < 0xffffffffu)
				++m_dwDX11RuntimeCompatGraceExpiredCount;
		}
	}
	else
	{
		m_dwDX11RuntimeCompatGraceUntilMS = 0;
		m_iDX11RuntimeCompatGraceReasonMask = 0;
		m_dwDX11RuntimeCompatGracePendingSinceMS = 0;
		m_iDX11RuntimeCompatGracePendingReasonMask = 0;
	}
	// Log RuntimeCompatMode activation (once)
	static bool s_bDX11RuntimeCompatModeLogged = false;
	if (bDX11RuntimeCompatMode && !s_bDX11RuntimeCompatModeLogged)
	{
		TraceError("DX11_RUNTIME_COMPAT_MODE_ACTIVATED grace_mode=%d frame_count=%u elapsed_ms=%u",
			bDX11RuntimeCompatGraceMode ? 1 : 0, m_dwDX11RuntimeCompatFrameCount, dwRuntimeNowMS);
		s_bDX11RuntimeCompatModeLogged = true;
	}

	m_bDX11RuntimeCompatMode = bDX11RuntimeCompatMode;
	m_bDX11RuntimeCompatGraceMode = bDX11RuntimeCompatGraceMode;
	if (bDX11RuntimeCompatMode)
	{
		if (!bDX11RuntimeCompatGraceMode)
		{
			if (m_dwDX11RuntimeCompatFrameCount < 0xffffffffu)
				++m_dwDX11RuntimeCompatFrameCount;

			if (0 == m_dwDX11RuntimeCompatStartMS)
				m_dwDX11RuntimeCompatStartMS = dwRuntimeNowMS;

			m_dwDX11RuntimeCompatElapsedMS = dwRuntimeNowMS - m_dwDX11RuntimeCompatStartMS;
		}
		else if (0 == m_dwDX11RuntimeCompatStartMS)
		{
			// Safety guard: keep counters consistent if grace path is entered very early.
			m_dwDX11RuntimeCompatStartMS = dwRuntimeNowMS;
		}

		const bool bDX11FastCutoverPassRamp =
			(bDX11CutoverRuntimeMode && bDX11NativeVisibleConfigEnabled && bDX11NativeWorldMinimalConfigEnabled);
		const DWORD kDX11Pass2Frame  = bDX11FastCutoverPassRamp ? 20u   : 180u;
		const DWORD kDX11Pass3Frame  = bDX11FastCutoverPassRamp ? 40u   : 420u;
		const DWORD kDX11Pass4Frame  = bDX11FastCutoverPassRamp ? 60u   : 600u;
		const DWORD kDX11Pass5Frame  = bDX11FastCutoverPassRamp ? 80u   : 780u;
		const DWORD kDX11Pass6Frame  = bDX11FastCutoverPassRamp ? 100u  : 960u;
		const DWORD kDX11Pass7Frame  = bDX11FastCutoverPassRamp ? 120u  : 1140u;
		const DWORD kDX11Pass8Frame  = bDX11FastCutoverPassRamp ? 140u  : 1320u;
		const DWORD kDX11Pass9Frame  = bDX11FastCutoverPassRamp ? 160u  : 1800u;
		const DWORD kDX11Pass10Frame = bDX11FastCutoverPassRamp ? 180u  : 2160u;
		const DWORD kDX11Pass11Frame = bDX11FastCutoverPassRamp ? 200u  : 2340u;
		const DWORD kDX11Pass12Frame = bDX11FastCutoverPassRamp ? 220u  : 2520u;
		const DWORD kDX11Pass13Frame = bDX11FastCutoverPassRamp ? 240u  : 2700u;
		const DWORD kDX11Pass14Frame = bDX11FastCutoverPassRamp ? 260u  : 2880u;
		const DWORD kDX11Pass15Frame = bDX11FastCutoverPassRamp ? 280u  : 3060u;
		const DWORD kDX11Pass16Frame = bDX11FastCutoverPassRamp ? 300u  : 3240u;
		const DWORD kDX11Pass2MS  = bDX11FastCutoverPassRamp ? 300u  : 3000u;
		const DWORD kDX11Pass3MS  = bDX11FastCutoverPassRamp ? 600u  : 7000u;
		const DWORD kDX11Pass4MS  = bDX11FastCutoverPassRamp ? 900u  : 10000u;
		const DWORD kDX11Pass5MS  = bDX11FastCutoverPassRamp ? 1200u : 13000u;
		const DWORD kDX11Pass6MS  = bDX11FastCutoverPassRamp ? 1500u : 16000u;
		const DWORD kDX11Pass7MS  = bDX11FastCutoverPassRamp ? 1800u : 19000u;
		const DWORD kDX11Pass8MS  = bDX11FastCutoverPassRamp ? 2100u : 22000u;
		const DWORD kDX11Pass9MS  = bDX11FastCutoverPassRamp ? 2400u : 30000u;
		const DWORD kDX11Pass10MS = bDX11FastCutoverPassRamp ? 2700u : 36000u;
		const DWORD kDX11Pass11MS = bDX11FastCutoverPassRamp ? 3000u : 39000u;
		const DWORD kDX11Pass12MS = bDX11FastCutoverPassRamp ? 3300u : 42000u;
		const DWORD kDX11Pass13MS = bDX11FastCutoverPassRamp ? 3600u : 45000u;
		const DWORD kDX11Pass14MS = bDX11FastCutoverPassRamp ? 3900u : 48000u;
		const DWORD kDX11Pass15MS = bDX11FastCutoverPassRamp ? 4200u : 51000u;
		const DWORD kDX11Pass16MS = bDX11FastCutoverPassRamp ? 4500u : 54000u;

		m_bDX11WorldNativePass1Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass2Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass2MS)
			m_bDX11WorldNativePass2Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass3Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass3MS)
			m_bDX11WorldNativePass3Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass4Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass4MS)
			m_bDX11WorldNativePass4Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass5Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass5MS)
			m_bDX11WorldNativePass5Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass6Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass6MS)
			m_bDX11WorldNativePass6Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass7Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass7MS)
			m_bDX11WorldNativePass7Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass8Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass8MS)
			m_bDX11WorldNativePass8Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass9Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass9MS)
			m_bDX11WorldNativePass9Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass10Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass10MS)
			m_bDX11WorldNativePass10Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass11Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass11MS)
			m_bDX11WorldNativePass11Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass12Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass12MS)
			m_bDX11WorldNativePass12Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass13Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass13MS)
			m_bDX11WorldNativePass13Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass14Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass14MS)
			m_bDX11WorldNativePass14Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass15Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass15MS)
			m_bDX11WorldNativePass15Mode = true;
		if (m_dwDX11RuntimeCompatFrameCount >= kDX11Pass16Frame || m_dwDX11RuntimeCompatElapsedMS >= kDX11Pass16MS)
			m_bDX11WorldNativePass16Mode = true;

		// Log Pass progression (only when milestones are reached)
		static bool s_bPass4Logged = false;
		static bool s_bPass16Logged = false;
		if (m_bDX11WorldNativePass4Mode && !s_bPass4Logged)
		{
			TraceError("DX11_PASS4_REACHED fast_ramp=%d frame_count=%u elapsed_ms=%u threshold_frame=%u threshold_ms=%u",
				bDX11FastCutoverPassRamp ? 1 : 0, m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS,
				kDX11Pass4Frame, kDX11Pass4MS);
			s_bPass4Logged = true;
		}
		if (m_bDX11WorldNativePass16Mode && !s_bPass16Logged)
		{
			TraceError("DX11_PASS16_REACHED fast_ramp=%d frame_count=%u elapsed_ms=%u threshold_frame=%u threshold_ms=%u",
				bDX11FastCutoverPassRamp ? 1 : 0, m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS,
				kDX11Pass16Frame, kDX11Pass16MS);
			s_bPass16Logged = true;
		}

		const DWORD kDX11HandoffStableGoodFramesRequired = bDX11FirstPassHybrid ? 600u : 120u;
		const DWORD kDX11HandoffStableElapsedRequiredMS = bDX11FirstPassHybrid ? 120000u : 45000u;
		bool bDX11HandoffStableReady = m_bDX11HandoffStableLatched;
		bool bDX11ReenterProbe = false;
		if (m_bDX11WorldNativePass16Mode)
		{
			if (!bDX11FirstPassHybrid)
			{
				// Cutover runtime: once pass16 is reached, keep native-visible path stable
				// and skip probe dynamics used by first-pass hybrid rollout.
				bDX11HandoffStableReady = true;
				m_bDX11HandoffStableLatched = true;
				if (m_dwDX11HandoffStableGoodFrames < kDX11HandoffStableGoodFramesRequired)
					m_dwDX11HandoffStableGoodFrames = kDX11HandoffStableGoodFramesRequired;
			}

			if (!bDX11RuntimeStressNow && m_dwDX11RuntimeCompatFrameCount >= 3600)
			{
				if (m_dwDX11HandoffStableGoodFrames < 0xffffffffu)
					++m_dwDX11HandoffStableGoodFrames;
			}
			else if (!m_bDX11HandoffStableLatched)
			{
				m_dwDX11HandoffStableGoodFrames = 0;
			}

			if (m_dwDX11HandoffStableGoodFrames >= kDX11HandoffStableGoodFramesRequired ||
				m_dwDX11RuntimeCompatElapsedMS >= kDX11HandoffStableElapsedRequiredMS)
			{
				bDX11HandoffStableReady = true;
				m_bDX11HandoffStableLatched = true;
			}

			if (bDX11HandoffStableReady && !m_bDX11WorldHandoffProbeMode)
			{
				if (bDX11RuntimeStressNow)
				{
					if (m_dwDX11StableStressFrames < 0xffffffffu)
						++m_dwDX11StableStressFrames;
				}
				else
				{
					m_dwDX11StableStressFrames = 0;
				}

				// If stable mode is under sustained load, re-enter probe for adaptive throttling.
				if (bDX11FirstPassHybrid && m_dwDX11StableStressFrames >= 600)
				{
					bDX11ReenterProbe = true;
					m_dwDX11StableStressFrames = 0;
					m_dwDX11HandoffStableGoodFrames = 0;
					m_bDX11HandoffStableLatched = false;
					TraceError(
						"DX11_HANDOFF_STABLE reenter_probe frame=%u elapsed_ms=%u fps=%u map_ms=%u chr_ms=%u fx_ms=%u shadow_ms=%u",
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS,
						m_dwRenderFPS,
						m_dwPerfMapMS,
						m_dwPerfCharacterMS,
						m_dwPerfEffectRenderMS,
						m_dwPerfShadowMS);
				}
			}
			else
			{
				m_dwDX11StableStressFrames = 0;
			}
		}
		else
		{
			m_dwDX11HandoffStableGoodFrames = 0;
			m_bDX11HandoffStableLatched = false;
			m_dwDX11StableStressFrames = 0;
		}

		if (!bDX11FirstPassHybrid)
			m_bDX11WorldHandoffProbeMode = false;
		else
			m_bDX11WorldHandoffProbeMode = (m_bDX11WorldNativePass8Mode && m_dwDX11RuntimeCompatElapsedMS >= 26000 && (!bDX11HandoffStableReady || bDX11ReenterProbe));
	}
	else
	{
		m_dwDX11RuntimeCompatFrameCount = 0;
		m_dwDX11RuntimeCompatStartMS = 0;
		m_dwDX11RuntimeCompatElapsedMS = 0;
		m_dwDX11RuntimeCompatGraceUntilMS = 0;
		m_bDX11RuntimeCompatGraceMode = false;
		m_iDX11RuntimeCompatGraceReasonMask = 0;
		m_dwDX11RuntimeCompatGraceUsedCount = 0;
		m_dwDX11RuntimeCompatGraceExpiredCount = 0;
		m_dwDX11RuntimeCompatGraceCoalescedCount = 0;
		m_dwDX11RuntimeCompatGraceSuppressedCount = 0;
		m_dwDX11RuntimeCompatGracePendingSinceMS = 0;
		m_iDX11RuntimeCompatGracePendingReasonMask = 0;
		m_dwDX11RuntimeCompatLastGraceEnterMS = 0;
		m_dwDX11RuntimeCompatLastGraceLeaveMS = 0;
		m_dwDX11HandoffStableGoodFrames = 0;
		m_bDX11HandoffStableLatched = false;
		m_dwDX11StableStressFrames = 0;
		m_bDX11VisiblePass1AutoDisabled = false;
		m_dwDX11VisiblePass1FailCount = 0;
		m_dwDX11VisiblePass1SuccessCount = 0;
		m_dwDX11VisiblePass1LastAttemptFrame = 0;
		m_dwDX11VisiblePass1LastIntervalFrames = 0;
		m_dwDX11WorldSubmitMaskMismatchCount = 0;
		m_bDX11WorldSubmitMaskMismatchActive = false;
		m_dwDX11WorldSubmitMaskMismatchLastLogMS = 0;
		m_uDX11WorldSubmitMaskMismatchTelemetryObserved = 0u;
		m_uDX11WorldSubmitMaskMismatchTelemetrySubmitted = 0u;
		m_uDX11WorldSubmitMaskMismatchTelemetryApplicable = 0u;
		m_uDX11WorldSubmitMaskMismatchTelemetryCommitted = 0u;
			m_uDX11WorldSubmitMaskMismatchGateObserved = 0u;
			m_uDX11WorldSubmitMaskMismatchGateSubmitted = 0u;
			m_uDX11WorldSubmitMaskMismatchGateApplicable = 0u;
			m_uDX11WorldSubmitMaskMismatchGateCommitted = 0u;
			m_uDX11WorldSubmitMaskMismatchLastReasonMask = 0u;
			m_uDX11WorldSubmitMaskMismatchLastPhaseActive = 0u;
			m_dwDX11WorldSubmitMaskMismatchLastFrame = 0u;
			m_dwDX11WorldSubmitMaskMismatchLastElapsedMS = 0u;
		m_bDX11WorldNativePass2Mode = false;
		m_bDX11WorldNativePass3Mode = false;
		m_bDX11WorldNativePass4Mode = false;
		m_bDX11WorldNativePass5Mode = false;
		m_bDX11WorldNativePass6Mode = false;
		m_bDX11WorldNativePass7Mode = false;
		m_bDX11WorldNativePass8Mode = false;
		m_bDX11WorldNativePass9Mode = false;
		m_bDX11WorldNativePass10Mode = false;
		m_bDX11WorldNativePass11Mode = false;
		m_bDX11WorldNativePass12Mode = false;
		m_bDX11WorldNativePass13Mode = false;
		m_bDX11WorldNativePass14Mode = false;
		m_bDX11WorldNativePass15Mode = false;
		m_bDX11WorldNativePass16Mode = false;
		m_bDX11WorldHandoffProbeMode = false;
	}

	static bool s_bPrevDX11RuntimeCompatMode = false;
	static bool s_bPrevDX11RuntimeCompatGraceMode = false;
	static bool s_bPrevDX11NativePass1 = false;
	static bool s_bPrevDX11NativePass2 = false;
	static bool s_bPrevDX11NativePass3 = false;
	static bool s_bPrevDX11NativePass4 = false;
	static bool s_bPrevDX11NativePass5 = false;
	static bool s_bPrevDX11NativePass6 = false;
	static bool s_bPrevDX11NativePass7 = false;
	static bool s_bPrevDX11NativePass8 = false;
	static bool s_bPrevDX11NativePass9 = false;
	static bool s_bPrevDX11NativePass10 = false;
	static bool s_bPrevDX11NativePass11 = false;
	static bool s_bPrevDX11NativePass12 = false;
	static bool s_bPrevDX11NativePass13 = false;
	static bool s_bPrevDX11NativePass14 = false;
	static bool s_bPrevDX11NativePass15 = false;
	static bool s_bPrevDX11NativePass16 = false;
	static bool s_bPrevDX11HandoffProbe = false;
	static bool s_bPrevDX11HandoffStable = false;

	if (bDX11RuntimeCompatMode && !s_bPrevDX11RuntimeCompatMode)
	{
		TraceError("DX11_RUNTIME_COMPAT enter frame=%u elapsed_ms=%u", m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS);
	}
	else if (!bDX11RuntimeCompatMode && s_bPrevDX11RuntimeCompatMode)
	{
		TraceError(
			"DX11_RUNTIME_COMPAT leave reason finalcheck=%d has_main=%d grace_was_active=%d grace_expired=%d grace_reason_last=%s backend=%s frame=%u elapsed_ms=%u stage=%s",
			bDX11RuntimeCompatFinalcheckEnabled ? 1 : 0,
			bDX11RuntimeCompatHasMainInstance ? 1 : 0,
			s_bPrevDX11RuntimeCompatGraceMode ? 1 : 0,
			bDX11RuntimeCompatGraceExpired ? 1 : 0,
			DX11RuntimeCompatGraceReasonToString(iDX11RuntimeCompatGraceReasonPrev),
			GetRenderBackend(),
			dwDX11RuntimeCompatFramePrev,
			dwDX11RuntimeCompatElapsedPrev,
			c_szDX11RuntimeStagePrev);
	}
	s_bPrevDX11RuntimeCompatMode = bDX11RuntimeCompatMode;

	if (bDX11RuntimeCompatGracePendingStart)
	{
		TraceError(
			"DX11_RUNTIME_COMPAT grace_pending reason=%s debounce_ms=%u frame=%u elapsed_ms=%u stage=%s",
			DX11RuntimeCompatGraceReasonToString(m_iDX11RuntimeCompatGracePendingReasonMask),
			kDX11RuntimeCompatGraceDebounceMS,
			m_dwDX11RuntimeCompatFrameCount,
			m_dwDX11RuntimeCompatElapsedMS,
			GetDX11RuntimeStage());
	}
	else if (bDX11RuntimeCompatGracePendingClear && dwDX11RuntimeCompatGracePendingMS < kDX11RuntimeCompatGraceDebounceMS)
	{
		const DWORD dwSuppressedCount = m_dwDX11RuntimeCompatGraceSuppressedCount;
		if (dwSuppressedCount <= 5u || 0u == (dwSuppressedCount % 20u))
		{
			TraceError(
				"DX11_RUNTIME_COMPAT grace_suppressed reason=%s pending_ms=%u debounce_ms=%u suppressed=%u frame=%u elapsed_ms=%u stage=%s",
				DX11RuntimeCompatGraceReasonToString(iDX11RuntimeCompatGracePendingReasonPrev),
				dwDX11RuntimeCompatGracePendingMS,
				kDX11RuntimeCompatGraceDebounceMS,
				dwSuppressedCount,
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS,
				GetDX11RuntimeStage());
		}
	}

	if (bDX11RuntimeCompatGraceMode && !s_bPrevDX11RuntimeCompatGraceMode)
	{
		const DWORD dwGraceLeftMS = (m_dwDX11RuntimeCompatGraceUntilMS > dwRuntimeNowMS) ? (m_dwDX11RuntimeCompatGraceUntilMS - dwRuntimeNowMS) : 0u;
		TraceError(
			"DX11_RUNTIME_COMPAT grace_enter reason=%s coalesced=%d pending_ms=%u grace_left_ms=%u grace_used=%u grace_expired=%u grace_coalesced=%u grace_suppressed=%u frame=%u elapsed_ms=%u stage=%s",
			GetDX11RuntimeCompatGraceReason(),
			bDX11RuntimeCompatGraceEnterCoalesced ? 1 : 0,
			dwDX11RuntimeCompatGracePendingMS,
			dwGraceLeftMS,
			m_dwDX11RuntimeCompatGraceUsedCount,
			m_dwDX11RuntimeCompatGraceExpiredCount,
			m_dwDX11RuntimeCompatGraceCoalescedCount,
			m_dwDX11RuntimeCompatGraceSuppressedCount,
			m_dwDX11RuntimeCompatFrameCount,
			m_dwDX11RuntimeCompatElapsedMS,
			GetDX11RuntimeStage());
	}
	else if (!bDX11RuntimeCompatGraceMode && s_bPrevDX11RuntimeCompatGraceMode && bDX11RuntimeCompatMode)
	{
		m_dwDX11RuntimeCompatLastGraceLeaveMS = dwRuntimeNowMS;
		TraceError(
			"DX11_RUNTIME_COMPAT grace_leave reason=restored grace_last=%s grace_used=%u grace_expired=%u grace_coalesced=%u grace_suppressed=%u frame=%u elapsed_ms=%u stage=%s",
			DX11RuntimeCompatGraceReasonToString(iDX11RuntimeCompatGraceReasonPrev),
			m_dwDX11RuntimeCompatGraceUsedCount,
			m_dwDX11RuntimeCompatGraceExpiredCount,
			m_dwDX11RuntimeCompatGraceCoalescedCount,
			m_dwDX11RuntimeCompatGraceSuppressedCount,
			m_dwDX11RuntimeCompatFrameCount,
			m_dwDX11RuntimeCompatElapsedMS,
			GetDX11RuntimeStage());
	}
	s_bPrevDX11RuntimeCompatGraceMode = bDX11RuntimeCompatGraceMode;

	auto __LogPassActivation = [&](int iPassNo, bool bEnabled, bool& rbPrevEnabled)
	{
		if (bEnabled && !rbPrevEnabled)
		{
			TraceError("DX11_PASS_ACTIVATE pass=%d frame=%u elapsed_ms=%u", iPassNo, m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS);
		}
		rbPrevEnabled = bEnabled;
	};

	__LogPassActivation(1, m_bDX11WorldNativePass1Mode, s_bPrevDX11NativePass1);
	__LogPassActivation(2, m_bDX11WorldNativePass2Mode, s_bPrevDX11NativePass2);
	__LogPassActivation(3, m_bDX11WorldNativePass3Mode, s_bPrevDX11NativePass3);
	__LogPassActivation(4, m_bDX11WorldNativePass4Mode, s_bPrevDX11NativePass4);
	__LogPassActivation(5, m_bDX11WorldNativePass5Mode, s_bPrevDX11NativePass5);
	__LogPassActivation(6, m_bDX11WorldNativePass6Mode, s_bPrevDX11NativePass6);
	__LogPassActivation(7, m_bDX11WorldNativePass7Mode, s_bPrevDX11NativePass7);
	__LogPassActivation(8, m_bDX11WorldNativePass8Mode, s_bPrevDX11NativePass8);
	__LogPassActivation(9, m_bDX11WorldNativePass9Mode, s_bPrevDX11NativePass9);
	__LogPassActivation(10, m_bDX11WorldNativePass10Mode, s_bPrevDX11NativePass10);
	__LogPassActivation(11, m_bDX11WorldNativePass11Mode, s_bPrevDX11NativePass11);
	__LogPassActivation(12, m_bDX11WorldNativePass12Mode, s_bPrevDX11NativePass12);
	__LogPassActivation(13, m_bDX11WorldNativePass13Mode, s_bPrevDX11NativePass13);
	__LogPassActivation(14, m_bDX11WorldNativePass14Mode, s_bPrevDX11NativePass14);
	__LogPassActivation(15, m_bDX11WorldNativePass15Mode, s_bPrevDX11NativePass15);
	__LogPassActivation(16, m_bDX11WorldNativePass16Mode, s_bPrevDX11NativePass16);

	if (m_bDX11WorldHandoffProbeMode && !s_bPrevDX11HandoffProbe)
	{
		TraceError("DX11_HANDOFF_PROBE enter frame=%u elapsed_ms=%u", m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS);
	}
	else if (!m_bDX11WorldHandoffProbeMode && s_bPrevDX11HandoffProbe)
	{
		TraceError("DX11_HANDOFF_PROBE leave");
	}
	s_bPrevDX11HandoffProbe = m_bDX11WorldHandoffProbeMode;

	const bool bDX11HandoffStableMode = (bDX11RuntimeCompatMode && m_bDX11WorldNativePass16Mode && !m_bDX11WorldHandoffProbeMode);
	if (bDX11HandoffStableMode && !s_bPrevDX11HandoffStable)
	{
		TraceError(
			"DX11_HANDOFF_STABLE enter frame=%u elapsed_ms=%u good_frames=%u",
			m_dwDX11RuntimeCompatFrameCount,
			m_dwDX11RuntimeCompatElapsedMS,
			m_dwDX11HandoffStableGoodFrames);
	}
	else if (!bDX11HandoffStableMode && s_bPrevDX11HandoffStable)
	{
		TraceError("DX11_HANDOFF_STABLE leave");
	}
	s_bPrevDX11HandoffStable = bDX11HandoffStableMode;

	if (m_eRenderBackend == RENDER_BACKEND_DX11)
	{
		static DWORD s_dwDX11CutoverReadyFrames = 0;
		static DWORD s_dwDX11CutoverReadyStartMS = 0;
		static DWORD s_dwDX11CutoverReadyFlapCount = 0;
		static DWORD s_dwDX11CutoverStatusTick = 0;
		static DWORD s_dwDX11CutoverReadyReminderTick = 0;
		static DWORD s_dwDX11CutoverLastInhibitMask = 0xFFFFFFFFu;
		static bool s_bDX11CutoverReadyLatched = false;

		const bool bDX11FirstPassActiveNow = m_pySystem.IsDX11FirstPassActiveEnabled();
		if (!bDX11FirstPassActiveNow)
		{
			s_dwDX11CutoverReadyFrames = 0;
			s_dwDX11CutoverReadyStartMS = 0;
			s_dwDX11CutoverReadyFlapCount = 0;
			s_dwDX11CutoverStatusTick = 0;
			s_dwDX11CutoverReadyReminderTick = 0;
			s_dwDX11CutoverLastInhibitMask = 0xFFFFFFFFu;
			s_bDX11CutoverReadyLatched = false;
		}
		else
		{
			DWORD dwDX11CutoverInhibitMask = 0u;
			const bool bDX11CutoverRuntimeStable =
				m_bDX11RuntimeCompatMode &&
				m_bDX11WorldNativePass16Mode &&
				!m_bDX11WorldHandoffProbeMode &&
				!m_bDX11RuntimeCompatGraceMode;
			const bool bDX11CutoverVisibleCadenceReady =
				(m_dwDX11VisiblePass1LastIntervalFrames > 0u &&
				 m_dwDX11VisiblePass1LastIntervalFrames <= DX11_CUTOVER_READY_MAX_INTERVAL_FRAMES);
			const bool bDX11CutoverVisibleSuccessReady =
				(m_dwDX11VisiblePass1SuccessCount >= DX11_CUTOVER_READY_VISIBLE_SUCCESS_MIN);
			const bool bDX11CutoverRuntimeUptimeReady =
				(m_dwDX11RuntimeCompatElapsedMS >= DX11_CUTOVER_READY_RUNTIME_MS_MIN);
			const bool bDX11CutoverGraceHistoryClean =
				(0u == m_dwDX11RuntimeCompatGraceUsedCount && 0u == m_dwDX11RuntimeCompatGraceExpiredCount);
			const bool bDX11CutoverStressTooHigh =
				(m_dwDX11StableStressFrames >= DX11_CUTOVER_READY_MAX_STRESS_FRAMES);

			if (m_pySystem.IsDX11UIPassOnlyEnabled())
				dwDX11CutoverInhibitMask |= 0x001u;
			if (!bDX11CutoverRuntimeStable)
				dwDX11CutoverInhibitMask |= 0x002u;
			if (m_bDX11WorldHandoffProbeMode)
				dwDX11CutoverInhibitMask |= 0x004u;
			if (m_bDX11VisiblePass1AutoDisabled)
				dwDX11CutoverInhibitMask |= 0x008u;
			if (m_dwDX11VisiblePass1FailCount > 0u)
				dwDX11CutoverInhibitMask |= 0x010u;
			if (!bDX11CutoverVisibleSuccessReady)
				dwDX11CutoverInhibitMask |= 0x020u;
			if (!bDX11CutoverVisibleCadenceReady)
				dwDX11CutoverInhibitMask |= 0x040u;
			if (!bDX11CutoverRuntimeUptimeReady)
				dwDX11CutoverInhibitMask |= 0x080u;
			if (!bDX11CutoverGraceHistoryClean)
				dwDX11CutoverInhibitMask |= 0x100u;
			if (bDX11CutoverStressTooHigh)
				dwDX11CutoverInhibitMask |= 0x200u;

			const bool bDX11CutoverReadyNow = (0u == dwDX11CutoverInhibitMask);
			if (bDX11CutoverReadyNow)
			{
				if (0u == s_dwDX11CutoverReadyFrames)
					s_dwDX11CutoverReadyStartMS = dwRuntimeNowMS;

				if (s_dwDX11CutoverReadyFrames < 0xFFFFFFFFu)
					++s_dwDX11CutoverReadyFrames;

				if (!s_bDX11CutoverReadyLatched && s_dwDX11CutoverReadyFrames >= DX11_CUTOVER_READY_CONFIRM_FRAMES)
				{
					s_bDX11CutoverReadyLatched = true;
					s_dwDX11CutoverReadyReminderTick = 0;
					TraceError(
						"DX11_CUTOVER_READY state=latched ready_frames=%u ready_ms=%u frame=%u elapsed_ms=%u note=auto_cutover_enabled",
						s_dwDX11CutoverReadyFrames,
						(dwRuntimeNowMS >= s_dwDX11CutoverReadyStartMS) ? (dwRuntimeNowMS - s_dwDX11CutoverReadyStartMS) : 0u,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}

				if (s_bDX11CutoverReadyLatched && m_pySystem.IsDX11FirstPassActiveEnabled())
				{
					m_pySystem.SetDX11FirstPassActiveEnabled(false);
					m_pySystem.SetDX11VisibleBootstrapEnabled(false);
					const bool bCutoverSaved = m_pySystem.SaveConfig();
					TraceError(
						"DX11_CUTOVER_APPLIED action=disable_first_pass_hybrid saved=%d ready_frames=%u ready_ms=%u frame=%u elapsed_ms=%u",
						bCutoverSaved ? 1 : 0,
						s_dwDX11CutoverReadyFrames,
						(dwRuntimeNowMS >= s_dwDX11CutoverReadyStartMS) ? (dwRuntimeNowMS - s_dwDX11CutoverReadyStartMS) : 0u,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}
			}
			else
			{
				if (s_dwDX11CutoverReadyFrames > 0u || s_bDX11CutoverReadyLatched)
				{
					if (s_dwDX11CutoverReadyFlapCount < 0xFFFFFFFFu)
						++s_dwDX11CutoverReadyFlapCount;

					if (s_dwDX11CutoverReadyFlapCount <= 5u || 0u == (s_dwDX11CutoverReadyFlapCount % 20u))
					{
						TraceError(
							"DX11_CUTOVER_READY state=reset flaps=%u inhibit_mask=0x%03X frame=%u elapsed_ms=%u",
							s_dwDX11CutoverReadyFlapCount,
							static_cast<unsigned int>(dwDX11CutoverInhibitMask),
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}
				}

				s_dwDX11CutoverReadyFrames = 0;
				s_dwDX11CutoverReadyStartMS = 0;
				s_bDX11CutoverReadyLatched = false;
			}

			if (s_bDX11CutoverReadyLatched && m_pySystem.IsDX11FirstPassActiveEnabled())
			{
				if (0u == s_dwDX11CutoverReadyReminderTick || dwRuntimeNowMS - s_dwDX11CutoverReadyReminderTick >= DX11_CUTOVER_READY_REMINDER_MS)
				{
					s_dwDX11CutoverReadyReminderTick = dwRuntimeNowMS;
					TraceError(
						"DX11_CUTOVER_READY state=armed_auto_apply first_pass_active=1 frame=%u elapsed_ms=%u",
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}
			}

			if (0u == s_dwDX11CutoverStatusTick ||
				dwRuntimeNowMS - s_dwDX11CutoverStatusTick >= DX11_CUTOVER_STATUS_INTERVAL_MS ||
				s_dwDX11CutoverLastInhibitMask != dwDX11CutoverInhibitMask)
			{
				s_dwDX11CutoverStatusTick = dwRuntimeNowMS;
				s_dwDX11CutoverLastInhibitMask = dwDX11CutoverInhibitMask;
				TraceError(
					"DX11_CUTOVER_STATUS ready=%d latched=%d ready_frames=%u ready_ms=%u confirm_frames=%u inhibit_mask=0x%03X first_pass=%d ui_pass_only=%d runtime_stable=%d probe=%d visible_success_ready=%d visible_cadence_ready=%d runtime_uptime_ready=%d grace_clean=%d visible_success=%u visible_success_target=%u visible_fail=%u visible_interval=%u stress=%d stress_frames=%u grace_used=%u grace_expired=%u frame=%u elapsed_ms=%u",
					(0u == dwDX11CutoverInhibitMask) ? 1 : 0,
					s_bDX11CutoverReadyLatched ? 1 : 0,
					s_dwDX11CutoverReadyFrames,
					(s_dwDX11CutoverReadyStartMS > 0u && dwRuntimeNowMS >= s_dwDX11CutoverReadyStartMS) ? (dwRuntimeNowMS - s_dwDX11CutoverReadyStartMS) : 0u,
					DX11_CUTOVER_READY_CONFIRM_FRAMES,
					static_cast<unsigned int>(dwDX11CutoverInhibitMask),
					bDX11FirstPassActiveNow ? 1 : 0,
					m_pySystem.IsDX11UIPassOnlyEnabled() ? 1 : 0,
					bDX11CutoverRuntimeStable ? 1 : 0,
					m_bDX11WorldHandoffProbeMode ? 1 : 0,
					bDX11CutoverVisibleSuccessReady ? 1 : 0,
					bDX11CutoverVisibleCadenceReady ? 1 : 0,
					bDX11CutoverRuntimeUptimeReady ? 1 : 0,
					bDX11CutoverGraceHistoryClean ? 1 : 0,
					m_dwDX11VisiblePass1SuccessCount,
					DX11_CUTOVER_READY_VISIBLE_SUCCESS_MIN,
					m_dwDX11VisiblePass1FailCount,
					m_dwDX11VisiblePass1LastIntervalFrames,
					bDX11CutoverStressTooHigh ? 1 : 0,
					m_dwDX11StableStressFrames,
					m_dwDX11RuntimeCompatGraceUsedCount,
					m_dwDX11RuntimeCompatGraceExpiredCount,
					m_dwDX11RuntimeCompatFrameCount,
					m_dwDX11RuntimeCompatElapsedMS);
			}
		}
	}

	if (bDX11HandoffStableMode)
	{
		static DWORD s_dwDX11StableHeartbeatTick = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		DWORD dwStableHeartbeatIntervalMS = DX11_NATIVE_HEARTBEAT_INTERVAL_MS;
		// When the client is effectively idle (fps=0 and no measured world/ui cost),
		// throttle stable heartbeats to keep syserr readable during long idle sessions.
		if (!bDX11RuntimeStressNow &&
			0u == m_dwRenderFPS &&
			m_dwPerfMapMS <= 1u &&
			0u == m_dwPerfCharacterMS &&
			0u == m_dwPerfEffectRenderMS &&
			0u == m_dwPerfShadowMS)
		{
			dwStableHeartbeatIntervalMS = std::max<DWORD>(DX11_NATIVE_HEARTBEAT_INTERVAL_MS, 60000u);
		}
		if (0 == s_dwDX11StableHeartbeatTick || dwNow - s_dwDX11StableHeartbeatTick >= dwStableHeartbeatIntervalMS)
		{
			DWORD dwVisiblePass1HeartbeatInterval = m_dwDX11VisiblePass1LastIntervalFrames;
			if (0u == dwVisiblePass1HeartbeatInterval)
			{
				const bool bDX11VisiblePass1PilotConfig = m_pySystem.IsDX11WorldVisiblePass1TestEnabled();
				const DWORD kDX11VisiblePass1PostGraceSettleMS = 2000u;
				const bool bDX11VisiblePass1PostGraceSettle =
					(m_dwDX11RuntimeCompatLastGraceLeaveMS > 0 &&
					 dwRuntimeNowMS > m_dwDX11RuntimeCompatLastGraceLeaveMS &&
					 (dwRuntimeNowMS - m_dwDX11RuntimeCompatLastGraceLeaveMS) < kDX11VisiblePass1PostGraceSettleMS);
				const bool bDX11VisiblePass1PilotEligible =
					bDX11UINativeTest &&
					bDX11WorldFinalcheckTest &&
					bDX11RuntimeCompatMode &&
					m_bDX11WorldNativePass16Mode &&
					!m_bDX11WorldHandoffProbeMode &&
					!m_bDX11RuntimeCompatGraceMode &&
					!bDX11VisiblePass1PostGraceSettle;
				if (bDX11VisiblePass1PilotConfig && bDX11VisiblePass1PilotEligible && !m_bDX11VisiblePass1AutoDisabled)
				{
					dwVisiblePass1HeartbeatInterval = 600u;
					if (m_dwDX11VisiblePass1SuccessCount >= 8u)
						dwVisiblePass1HeartbeatInterval = 60u;
					else if (m_dwDX11VisiblePass1SuccessCount >= 3u)
						dwVisiblePass1HeartbeatInterval = 180u;

					const bool bDX11VisiblePass1BaseConfidence =
						(m_dwDX11VisiblePass1FailCount == 0u) &&
						(m_dwDX11RuntimeCompatGraceUsedCount == 0u) &&
						(m_dwDX11RuntimeCompatGraceExpiredCount == 0u);
					const bool bDX11VisiblePass1HighConfidence =
						bDX11VisiblePass1BaseConfidence &&
						(m_dwDX11VisiblePass1SuccessCount >= 120u) &&
						(m_dwDX11RuntimeCompatElapsedMS >= 180000u);
					const bool bDX11VisiblePass1UltraConfidence =
						bDX11VisiblePass1BaseConfidence &&
						(m_dwDX11VisiblePass1SuccessCount >= 900u) &&
						(m_dwDX11RuntimeCompatElapsedMS >= 600000u);
					const bool bDX11VisiblePass1ExtremeConfidence =
						bDX11VisiblePass1BaseConfidence &&
						(m_dwDX11VisiblePass1SuccessCount >= 1800u) &&
						(m_dwDX11RuntimeCompatElapsedMS >= 1200000u);
					if (bDX11VisiblePass1HighConfidence)
						dwVisiblePass1HeartbeatInterval = 30u;
					if (bDX11VisiblePass1UltraConfidence)
						dwVisiblePass1HeartbeatInterval = 10u;
					if (bDX11VisiblePass1ExtremeConfidence)
						dwVisiblePass1HeartbeatInterval = 5u;

					if (bDX11RuntimeStressNow && dwVisiblePass1HeartbeatInterval < 600u)
						dwVisiblePass1HeartbeatInterval = 600u;
				}
			}

			s_dwDX11StableHeartbeatTick = dwNow;

			// Auto-promotion: once hybrid runtime is stable and visible DX11 path is proven,
			// stop forcing UI-only mode to unlock the world migration blocker.
			if (m_pySystem.IsDX11UIPassOnlyEnabled())
			{
				const bool bDX11VisiblePromotionReady =
					(!m_bDX11VisiblePass1AutoDisabled) &&
					(m_dwDX11VisiblePass1FailCount == 0u) &&
					(m_dwDX11VisiblePass1SuccessCount >= DX11_AUTO_PROMOTE_VISIBLE_SUCCESS_MIN) &&
					(m_dwDX11VisiblePass1LastIntervalFrames > 0u && m_dwDX11VisiblePass1LastIntervalFrames <= 30u) &&
					(!m_bDX11RuntimeCompatGraceMode) &&
					(m_dwDX11RuntimeCompatGraceUsedCount == 0u) &&
					(m_dwDX11RuntimeCompatGraceExpiredCount == 0u) &&
					(!m_bDX11WorldHandoffProbeMode) &&
					(!bDX11RuntimeStressNow) &&
					(m_dwDX11RuntimeCompatElapsedMS >= DX11_AUTO_PROMOTE_RUNTIME_MIN_MS);

				if (bDX11VisiblePromotionReady)
				{
					m_pySystem.SetDX11UIPassOnlyEnabled(false);
					const bool bSaved = m_pySystem.SaveConfig();
					TraceError(
						"DX11_AUTOPROMOTE applied action=disable_ui_pass_only saved=%d visible_success=%u visible_interval=%u frame=%u elapsed_ms=%u",
						bSaved ? 1 : 0,
						m_dwDX11VisiblePass1SuccessCount,
						m_dwDX11VisiblePass1LastIntervalFrames,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}
			}

			const DWORD dwDX11FullBlockMask = GetDX11FullRenderBlockMask();
			TraceError(
				"DX11_HANDOFF_STABLE heartbeat stress=%d stress_frames=%u fps=%u map_ms=%u chr_ms=%u fx_ms=%u shadow_ms=%u visible_pass1_cfg=%d visible_pass1_auto_disabled=%d visible_pass1_success=%u visible_pass1_fail=%u visible_pass1_interval=%u full_phase=%s full_remaining=%d block_mask=0x%02X block_summary=%s block_runtime=%d block_visible=%d block_world=%d block_ui=%d block_cutover=%d grace_active=%d grace_reason=%s grace_used=%u grace_expired=%u grace_coalesced=%u grace_suppressed=%u frame=%u elapsed_ms=%u",
				bDX11RuntimeStressNow ? 1 : 0,
				m_dwDX11StableStressFrames,
				m_dwRenderFPS,
				m_dwPerfMapMS,
				m_dwPerfCharacterMS,
				m_dwPerfEffectRenderMS,
				m_dwPerfShadowMS,
				m_pySystem.IsDX11WorldVisiblePass1TestEnabled() ? 1 : 0,
				m_bDX11VisiblePass1AutoDisabled ? 1 : 0,
				m_dwDX11VisiblePass1SuccessCount,
				m_dwDX11VisiblePass1FailCount,
				dwVisiblePass1HeartbeatInterval,
				GetDX11FullRenderPhase(),
				GetDX11FullRenderRemainingMajorStages(),
				static_cast<unsigned int>(dwDX11FullBlockMask),
				GetDX11FullRenderBlockSummary(),
				(dwDX11FullBlockMask & DX11_FULL_BLOCK_RUNTIME) ? 1 : 0,
				(dwDX11FullBlockMask & DX11_FULL_BLOCK_VISIBLE) ? 1 : 0,
				(dwDX11FullBlockMask & DX11_FULL_BLOCK_WORLD) ? 1 : 0,
				(dwDX11FullBlockMask & DX11_FULL_BLOCK_UI) ? 1 : 0,
				(dwDX11FullBlockMask & DX11_FULL_BLOCK_CUTOVER) ? 1 : 0,
				m_bDX11RuntimeCompatGraceMode ? 1 : 0,
				GetDX11RuntimeCompatGraceReason(),
				m_dwDX11RuntimeCompatGraceUsedCount,
				m_dwDX11RuntimeCompatGraceExpiredCount,
				m_dwDX11RuntimeCompatGraceCoalescedCount,
				m_dwDX11RuntimeCompatGraceSuppressedCount,
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS);
		}
	}

	if (m_eRenderBackend == RENDER_BACKEND_DX11)
	{
		const char* c_szDX11Stage = GetDX11RuntimeStage();
		const char* c_szDX11FullPhase = GetDX11FullRenderPhase();
		const int iDX11FullRemaining = GetDX11FullRenderRemainingMajorStages();
		const DWORD dwDX11FullBlockMask = GetDX11FullRenderBlockMask();
		static std::string s_stLastDX11Stage;
		static std::string s_stLastDX11FullPhase;
		static int s_iLastDX11FullRemaining = -1;
		static DWORD s_dwLastDX11FullBlockMask = 0xFFFFFFFFu;
		if (s_stLastDX11Stage != c_szDX11Stage)
		{
			s_stLastDX11Stage = c_szDX11Stage;
			TraceError("DX11_RUNTIME_STAGE %s", c_szDX11Stage);
		}
		if (s_stLastDX11FullPhase != c_szDX11FullPhase || s_iLastDX11FullRemaining != iDX11FullRemaining || s_dwLastDX11FullBlockMask != dwDX11FullBlockMask)
		{
			s_stLastDX11FullPhase = c_szDX11FullPhase;
			s_iLastDX11FullRemaining = iDX11FullRemaining;
			s_dwLastDX11FullBlockMask = dwDX11FullBlockMask;
			TraceError(
				"DX11_FULL_RENDER_STATUS phase=%s remaining_major=%d block_mask=0x%02X block_summary=%s ui_pass_only=%d first_pass_hybrid=%d visible_success=%u visible_fail=%u visible_interval=%u runtime_compat=%d pass16=%d handoff_probe=%d grace_active=%d",
				c_szDX11FullPhase,
				iDX11FullRemaining,
				static_cast<unsigned int>(dwDX11FullBlockMask),
				GetDX11FullRenderBlockSummary(),
				m_pySystem.IsDX11UIPassOnlyEnabled() ? 1 : 0,
				m_pySystem.IsDX11FirstPassActiveEnabled() ? 1 : 0,
				m_dwDX11VisiblePass1SuccessCount,
				m_dwDX11VisiblePass1FailCount,
				m_dwDX11VisiblePass1LastIntervalFrames,
				m_bDX11RuntimeCompatMode ? 1 : 0,
				m_bDX11WorldNativePass16Mode ? 1 : 0,
				m_bDX11WorldHandoffProbeMode ? 1 : 0,
				m_bDX11RuntimeCompatGraceMode ? 1 : 0);
		}
	}

	if (m_isMinimizedWnd) [[unlikely]]
	{
		__SetDX11RenderCheckpoint("can_render_minimized");
		canRender = false;
	}
	else if ((m_eRenderBackend == RENDER_BACKEND_DX9 || bDX11FirstPassHybrid) && m_pyGraphic.IsLostDevice()) [[unlikely]]
	{
		__SetDX11RenderCheckpoint("dx9_lost_device_restore");
		CPythonBackground& rkBG = CPythonBackground::Instance();
		rkBG.ReleaseCharacterShadowTexture();

		if (m_pyGraphic.RestoreDevice())
			rkBG.CreateCharacterShadowTexture();
		else
			canRender = false;
	}
	else if (m_eRenderBackend == RENDER_BACKEND_DX11 && !m_grpDeviceDX11.IsValid()) [[unlikely]]
	{
		__SetDX11RenderCheckpoint("dx11_device_invalid");
		canRender = false;
	}

	if (!canRender)
	{
		const bool bForceCheckpointLog = (0 != strcmp(s_szDX11LastRenderCheckpoint, "can_render_minimized"));
		__LogDX11RenderCheckpoint(bForceCheckpointLog);
		return;
	}

	if (m_bDX11ExperimentalPresent && m_eRenderBackend == RENDER_BACKEND_DX9 && m_grpDeviceDX11.IsValid())
	{
		// In DX9 fallback mode we only validate DX11 presentability via TEST flag.
		// Calling a real DX11 Present() on the same HWND as DX9 causes random black flicker.
		// Do not issue DX11 draw/clear here; keep this path non-invasive for DX9 output.
		if (!m_grpDeviceDX11.PresentTest())
		{
			++m_dwDX11ExperimentalPresentFailCount;
			if (m_dwDX11ExperimentalPresentFailCount == 1 || (m_dwDX11ExperimentalPresentFailCount % 60) == 0)
				TraceError("DX11_EXPERIMENTAL_PRESENT: Present(TEST) failed (count=%u)", m_dwDX11ExperimentalPresentFailCount);

			if (m_dwDX11ExperimentalPresentFailCount >= 300)
			{
				m_bDX11ExperimentalPresent = false;
				TraceError("DX11_EXPERIMENTAL_PRESENT auto-disabled after repeated present failures.");
			}
		}
		else if (m_dwDX11ExperimentalPresentFailCount > 0)
		{
			TraceError("DX11_EXPERIMENTAL_PRESENT recovered after %u failures.", m_dwDX11ExperimentalPresentFailCount);
			m_dwDX11ExperimentalPresentFailCount = 0;
		}
	}

	if (m_eRenderBackend == RENDER_BACKEND_DX11)
	{
		// Hard safety: once native-visible runtime is enabled, never render bootstrap visuals.
		const bool bDX11AllowBootstrapTestRendering =
			(!bDX11CutoverRuntimeMode && !bDX11NativeVisibleConfigEnabled);
		const bool bNeedDX11BootstrapFrame =
			(bDX11FirstPassHybrid || bDX11VisibleBootstrap || (bDX11UINativeTest && bDX11AllowBootstrapTestRendering));
		if (bNeedDX11BootstrapFrame)
		{
			__SetDX11RenderCheckpoint("dx11_begin_frame");
			if (!m_grpDeviceDX11.BeginFrame(0.02f, 0.02f, 0.02f, 1.0f))
			{
				__SetDX11RenderCheckpoint("dx11_begin_frame_fail");
				__LogDX11RenderCheckpoint(true);
				return;
			}
			if (bDX11UINativeTest && bDX11AllowBootstrapTestRendering)
			{
				if (bDX11WorldFinalcheckTest)
				{
					if (bDX11RuntimeCompatMode)
					{
						__SetDX11RenderCheckpoint("dx11_runtime_compat_draw");
						const float fBaseTime = CTimer::Instance().GetCurrentSecond();
						const bool bDepthPass = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 0.87f);
						const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
						const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
						const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
						const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);
						auto __LogDX11RuntimePassFail = [&](int iPassNo, bool bPassResult, const char* c_szPassCheckpoint)
						{
							if (bPassResult)
								return;

							__SetDX11RenderCheckpoint(c_szPassCheckpoint);
							static DWORD s_dwDX11RuntimePassFailLogTick = 0;
							const DWORD dwNow = ELTimer_GetMSec();
							if (0 == s_dwDX11RuntimePassFailLogTick || dwNow - s_dwDX11RuntimePassFailLogTick >= 1000)
							{
								s_dwDX11RuntimePassFailLogTick = dwNow;
								TraceError(
									"DX11_RUNTIME_PASS_FAIL pass=%d frame=%u elapsed_ms=%u cp=%s stage=%s",
									iPassNo,
									m_dwDX11RuntimeCompatFrameCount,
									m_dwDX11RuntimeCompatElapsedMS,
									c_szPassCheckpoint,
									GetDX11RuntimeStage());
							}
						};
						__LogDX11RuntimePassFail(0, bDepthPass, "dx11_runtime_depth");

						bool bNativePass1 = true;
						if (m_bDX11WorldNativePass1Mode)
						{
							int iTerrainTiles = 120 + (iAliveCount / 3) + static_cast<int>(m_dwPerfMapMS / 3);
							int iActorCount = 56 + iAliveCount + (iDeadCount / 3) + static_cast<int>(m_dwPerfCharacterMS / 3);
							int iFXCount = 40 + iEffectCount + (iParticleCount / 16) + static_cast<int>(m_dwPerfEffectRenderMS / 3);

							if (iTerrainTiles < 100)
								iTerrainTiles = 100;
							else if (iTerrainTiles > 320)
								iTerrainTiles = 320;

							if (iActorCount < 48)
								iActorCount = 48;
							else if (iActorCount > 260)
								iActorCount = 260;

							if (iFXCount < 32)
								iFXCount = 32;
							else if (iFXCount > 220)
								iFXCount = 220;

							const bool bWorldPass = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 0.96f, iTerrainTiles, iActorCount, iFXCount);
							const bool bStatePass = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 1.08f, (iActorCount > 220 ? 220 : iActorCount));
							bNativePass1 = (bWorldPass && bStatePass);
						}
						__LogDX11RuntimePassFail(1, bNativePass1, "dx11_runtime_pass1");

						bool bNativePass2 = true;
						if (m_bDX11WorldNativePass2Mode)
						{
							int iActorCountPass2 = 72 + iAliveCount + (iDeadCount / 2) + static_cast<int>(m_dwPerfCharacterMS / 3);
							int iFXCountPass2 = 64 + iEffectCount + (iParticleCount / 10) + static_cast<int>(m_dwPerfEffectRenderMS / 3);

							if (iActorCountPass2 < 64)
								iActorCountPass2 = 64;
							else if (iActorCountPass2 > 280)
								iActorCountPass2 = 280;

							if (iFXCountPass2 < 56)
								iFXCountPass2 = 56;
							else if (iFXCountPass2 > 260)
								iFXCountPass2 = 260;

							const bool bSpritePass = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 1.15f, iFXCountPass2);
							const bool bBatchPass = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime * 1.23f, iActorCountPass2);
							const bool bDepthRecheckPass = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 1.31f);
							bNativePass2 = (bSpritePass && bBatchPass && bDepthRecheckPass);
						}
						__LogDX11RuntimePassFail(2, bNativePass2, "dx11_runtime_pass2");

						bool bNativePass3 = true;
						if (m_bDX11WorldNativePass3Mode)
						{
							int iTerrainTilesPass3 = 84 + (iAliveCount / 3) + static_cast<int>(m_dwPerfMapMS / 4);
							int iActorCountPass3 = 52 + (iAliveCount / 2) + (iDeadCount / 4) + static_cast<int>(m_dwPerfCharacterMS / 4);
							int iFXCountPass3 = 44 + (iEffectCount / 2) + (iParticleCount / 20) + static_cast<int>(m_dwPerfEffectRenderMS / 4);

							if (iTerrainTilesPass3 < 72)
								iTerrainTilesPass3 = 72;
							else if (iTerrainTilesPass3 > 220)
								iTerrainTilesPass3 = 220;

							if (iActorCountPass3 < 44)
								iActorCountPass3 = 44;
							else if (iActorCountPass3 > 210)
								iActorCountPass3 = 210;

							if (iFXCountPass3 < 36)
								iFXCountPass3 = 36;
							else if (iFXCountPass3 > 190)
								iFXCountPass3 = 190;

							const bool bWorldPass3 = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 1.38f, iTerrainTilesPass3, iActorCountPass3, iFXCountPass3);
							const bool bStatePass3 = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 1.46f, iActorCountPass3);
							bNativePass3 = (bWorldPass3 && bStatePass3);
						}
						__LogDX11RuntimePassFail(3, bNativePass3, "dx11_runtime_pass3");

						bool bNativePass4 = true;
						if (m_bDX11WorldNativePass4Mode)
						{
							int iActorCountPass4 = 40 + (iAliveCount / 2) + (iDeadCount / 5) + static_cast<int>(m_dwPerfCharacterMS / 5);
							int iFXCountPass4 = 34 + (iEffectCount / 2) + (iParticleCount / 24) + static_cast<int>(m_dwPerfEffectRenderMS / 5);

							if (iActorCountPass4 < 32)
								iActorCountPass4 = 32;
							else if (iActorCountPass4 > 180)
								iActorCountPass4 = 180;

							if (iFXCountPass4 < 28)
								iFXCountPass4 = 28;
							else if (iFXCountPass4 > 170)
								iFXCountPass4 = 170;

							const bool bBatchPass4 = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime * 1.53f, iActorCountPass4);
							const bool bSpritePass4 = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 1.61f, iFXCountPass4);
							bNativePass4 = (bBatchPass4 && bSpritePass4);
						}
						__LogDX11RuntimePassFail(4, bNativePass4, "dx11_runtime_pass4");

						bool bNativePass5 = true;
						if (m_bDX11WorldNativePass5Mode)
						{
							int iTerrainTilesPass5 = 66 + (iAliveCount / 4) + static_cast<int>(m_dwPerfMapMS / 5);
							int iActorCountPass5 = 36 + (iAliveCount / 3) + (iDeadCount / 6) + static_cast<int>(m_dwPerfCharacterMS / 6);

							if (iTerrainTilesPass5 < 60)
								iTerrainTilesPass5 = 60;
							else if (iTerrainTilesPass5 > 180)
								iTerrainTilesPass5 = 180;

							if (iActorCountPass5 < 28)
								iActorCountPass5 = 28;
							else if (iActorCountPass5 > 160)
								iActorCountPass5 = 160;

							const bool bWorldPass5 = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 1.68f, iTerrainTilesPass5, iActorCountPass5, 0);
							const bool bDepthPass5 = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 1.76f);
							bNativePass5 = (bWorldPass5 && bDepthPass5);
						}
						__LogDX11RuntimePassFail(5, bNativePass5, "dx11_runtime_pass5");

						bool bNativePass6 = true;
						if (m_bDX11WorldNativePass6Mode)
						{
							int iStateCountPass6 = 30 + (iAliveCount / 4) + static_cast<int>(m_dwPerfCharacterMS / 6);
							if (iStateCountPass6 < 24)
								iStateCountPass6 = 24;
							else if (iStateCountPass6 > 140)
								iStateCountPass6 = 140;

							const bool bStatePass6 = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 1.83f, iStateCountPass6);
							const bool bDepthPass6 = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 1.91f);
							bNativePass6 = (bStatePass6 && bDepthPass6);
						}
						__LogDX11RuntimePassFail(6, bNativePass6, "dx11_runtime_pass6");

						bool bNativePass7 = true;
						if (m_bDX11WorldNativePass7Mode)
						{
							int iTerrainTilesPass7 = 52 + (iAliveCount / 5) + static_cast<int>(m_dwPerfMapMS / 6);
							int iActorCountPass7 = 28 + (iAliveCount / 4) + static_cast<int>(m_dwPerfCharacterMS / 7);
							int iFXCountPass7 = 24 + (iEffectCount / 3) + static_cast<int>(m_dwPerfEffectRenderMS / 7);

							if (iTerrainTilesPass7 < 44)
								iTerrainTilesPass7 = 44;
							else if (iTerrainTilesPass7 > 150)
								iTerrainTilesPass7 = 150;

							if (iActorCountPass7 < 22)
								iActorCountPass7 = 22;
							else if (iActorCountPass7 > 120)
								iActorCountPass7 = 120;

							if (iFXCountPass7 < 18)
								iFXCountPass7 = 18;
							else if (iFXCountPass7 > 120)
								iFXCountPass7 = 120;

							const bool bWorldPass7 = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 1.98f, iTerrainTilesPass7, iActorCountPass7, iFXCountPass7);
							bNativePass7 = bWorldPass7;
						}
						__LogDX11RuntimePassFail(7, bNativePass7, "dx11_runtime_pass7");

						bool bNativePass8 = true;
						if (m_bDX11WorldNativePass8Mode)
						{
							const bool bDepthPass8 = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 2.05f);
							const bool bStatePass8 = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 2.12f, 48);
							bNativePass8 = (bDepthPass8 && bStatePass8);
						}
						__LogDX11RuntimePassFail(8, bNativePass8, "dx11_runtime_pass8");

						bool bNativePass9 = true;
						if (m_bDX11WorldNativePass9Mode)
						{
							int iActorCountPass9 = 22 + (iAliveCount / 5) + static_cast<int>(m_dwPerfCharacterMS / 8);
							if (iActorCountPass9 < 18)
								iActorCountPass9 = 18;
							else if (iActorCountPass9 > 96)
								iActorCountPass9 = 96;

							const bool bBatchPass9 = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime * 2.19f, iActorCountPass9);
							const bool bDepthPass9 = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 2.27f);
							bNativePass9 = (bBatchPass9 && bDepthPass9);
						}
						__LogDX11RuntimePassFail(9, bNativePass9, "dx11_runtime_pass9");

						bool bNativePass10 = true;
						if (m_bDX11WorldNativePass10Mode)
						{
							int iStateCountPass10 = 18 + (iAliveCount / 6) + static_cast<int>(m_dwPerfCharacterMS / 10);
							int iSpriteCountPass10 = 18 + (iEffectCount / 4) + static_cast<int>(m_dwPerfEffectRenderMS / 11);
							if (iStateCountPass10 < 16)
								iStateCountPass10 = 16;
							else if (iStateCountPass10 > 84)
								iStateCountPass10 = 84;
							if (iSpriteCountPass10 < 14)
								iSpriteCountPass10 = 14;
							else if (iSpriteCountPass10 > 72)
								iSpriteCountPass10 = 72;

							const bool bStatePass10 = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 2.33f, iStateCountPass10);
							const bool bSpritePass10 = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 2.39f, iSpriteCountPass10);
							bNativePass10 = (bStatePass10 && bSpritePass10);
						}
						__LogDX11RuntimePassFail(10, bNativePass10, "dx11_runtime_pass10");

						bool bNativePass11 = true;
						if (m_bDX11WorldNativePass11Mode)
						{
							int iTerrainTilesPass11 = 34 + (iAliveCount / 7) + static_cast<int>(m_dwPerfMapMS / 8);
							int iActorCountPass11 = 16 + (iAliveCount / 7) + static_cast<int>(m_dwPerfCharacterMS / 11);
							int iFXCountPass11 = 14 + (iEffectCount / 5) + static_cast<int>(m_dwPerfEffectRenderMS / 12);
							if (iTerrainTilesPass11 < 28)
								iTerrainTilesPass11 = 28;
							else if (iTerrainTilesPass11 > 108)
								iTerrainTilesPass11 = 108;
							if (iActorCountPass11 < 14)
								iActorCountPass11 = 14;
							else if (iActorCountPass11 > 78)
								iActorCountPass11 = 78;
							if (iFXCountPass11 < 12)
								iFXCountPass11 = 12;
							else if (iFXCountPass11 > 66)
								iFXCountPass11 = 66;

							const bool bWorldPass11 = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 2.45f, iTerrainTilesPass11, iActorCountPass11, iFXCountPass11);
							bNativePass11 = bWorldPass11;
						}
						__LogDX11RuntimePassFail(11, bNativePass11, "dx11_runtime_pass11");

						bool bNativePass12 = true;
						if (m_bDX11WorldNativePass12Mode)
						{
							int iActorCountPass12 = 16 + (iAliveCount / 8) + static_cast<int>(m_dwPerfCharacterMS / 12);
							if (iActorCountPass12 < 14)
								iActorCountPass12 = 14;
							else if (iActorCountPass12 > 70)
								iActorCountPass12 = 70;

							const bool bBatchPass12 = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime * 2.51f, iActorCountPass12);
							const bool bDepthPass12 = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 2.57f);
							bNativePass12 = (bBatchPass12 && bDepthPass12);
						}
						__LogDX11RuntimePassFail(12, bNativePass12, "dx11_runtime_pass12");

						bool bNativePass13 = true;
						if (m_bDX11WorldNativePass13Mode)
						{
							int iTerrainTilesPass13 = 24 + (iAliveCount / 9) + static_cast<int>(m_dwPerfMapMS / 10);
							int iActorCountPass13 = 14 + (iAliveCount / 9) + static_cast<int>(m_dwPerfCharacterMS / 13);
							if (iTerrainTilesPass13 < 20)
								iTerrainTilesPass13 = 20;
							else if (iTerrainTilesPass13 > 92)
								iTerrainTilesPass13 = 92;
							if (iActorCountPass13 < 12)
								iActorCountPass13 = 12;
							else if (iActorCountPass13 > 64)
								iActorCountPass13 = 64;

							const bool bWorldPass13 = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 2.63f, iTerrainTilesPass13, iActorCountPass13, 0);
							const bool bStatePass13 = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 2.69f, iActorCountPass13);
							bNativePass13 = (bWorldPass13 && bStatePass13);
						}
						__LogDX11RuntimePassFail(13, bNativePass13, "dx11_runtime_pass13");

						bool bNativePass14 = true;
						if (m_bDX11WorldNativePass14Mode)
						{
							int iSpriteCountPass14 = 12 + (iEffectCount / 7) + static_cast<int>(m_dwPerfEffectRenderMS / 15);
							if (iSpriteCountPass14 < 10)
								iSpriteCountPass14 = 10;
							else if (iSpriteCountPass14 > 56)
								iSpriteCountPass14 = 56;

							const bool bSpritePass14 = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 2.75f, iSpriteCountPass14);
							const bool bDepthPass14 = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 2.81f);
							bNativePass14 = (bSpritePass14 && bDepthPass14);
						}
						__LogDX11RuntimePassFail(14, bNativePass14, "dx11_runtime_pass14");

						bool bNativePass15 = true;
						if (m_bDX11WorldNativePass15Mode)
						{
							int iActorCountPass15 = 12 + (iAliveCount / 10) + static_cast<int>(m_dwPerfCharacterMS / 15);
							int iSpriteCountPass15 = 10 + (iEffectCount / 8) + static_cast<int>(m_dwPerfEffectRenderMS / 16);
							if (iActorCountPass15 < 10)
								iActorCountPass15 = 10;
							else if (iActorCountPass15 > 52)
								iActorCountPass15 = 52;
							if (iSpriteCountPass15 < 8)
								iSpriteCountPass15 = 8;
							else if (iSpriteCountPass15 > 44)
								iSpriteCountPass15 = 44;

							const bool bBatchPass15 = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime * 2.87f, iActorCountPass15);
							const bool bSpritePass15 = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 2.93f, iSpriteCountPass15);
							const bool bStatePass15 = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 2.99f, iActorCountPass15);
							bNativePass15 = (bBatchPass15 && bSpritePass15 && bStatePass15);
						}
						__LogDX11RuntimePassFail(15, bNativePass15, "dx11_runtime_pass15");

						bool bNativePass16 = true;
						if (m_bDX11WorldNativePass16Mode)
						{
							int iTerrainTilesPass16 = 18 + (iAliveCount / 12) + static_cast<int>(m_dwPerfMapMS / 14);
							int iActorCountPass16 = 10 + (iAliveCount / 12) + static_cast<int>(m_dwPerfCharacterMS / 16);
							if (iTerrainTilesPass16 < 16)
								iTerrainTilesPass16 = 16;
							else if (iTerrainTilesPass16 > 72)
								iTerrainTilesPass16 = 72;
							if (iActorCountPass16 < 8)
								iActorCountPass16 = 8;
							else if (iActorCountPass16 > 40)
								iActorCountPass16 = 40;

							const bool bWorldPass16 = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 3.05f, iTerrainTilesPass16, iActorCountPass16, 0);
							const bool bDepthPass16 = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 3.11f);
							bNativePass16 = (bWorldPass16 && bDepthPass16);
						}
						__LogDX11RuntimePassFail(16, bNativePass16, "dx11_runtime_pass16");

						const bool bRuntimeCompatDrawResult = (bDepthPass && bNativePass1 && bNativePass2 && bNativePass3 && bNativePass4 && bNativePass5 && bNativePass6 && bNativePass7 && bNativePass8 && bNativePass9 && bNativePass10 && bNativePass11 && bNativePass12 && bNativePass13 && bNativePass14 && bNativePass15 && bNativePass16);
						if (!bRuntimeCompatDrawResult)
						{
							static DWORD s_dwDX11WorldRuntimeCompatDrawFailCount = 0;
							++s_dwDX11WorldRuntimeCompatDrawFailCount;
							if (1 == s_dwDX11WorldRuntimeCompatDrawFailCount || 0 == (s_dwDX11WorldRuntimeCompatDrawFailCount % 120))
								TraceError(
									"DX11 world runtime compat/native pass draw failed (count=%u frame=%u elapsed_ms=%u rDepth=%d r1=%d r2=%d r3=%d r4=%d r5=%d r6=%d r7=%d r8=%d r9=%d r10=%d r11=%d r12=%d r13=%d r14=%d r15=%d r16=%d a1=%d a2=%d a3=%d a4=%d a5=%d a6=%d a7=%d a8=%d a9=%d a10=%d a11=%d a12=%d a13=%d a14=%d a15=%d a16=%d cp=%s stage=%s)",
									s_dwDX11WorldRuntimeCompatDrawFailCount,
									m_dwDX11RuntimeCompatFrameCount,
									m_dwDX11RuntimeCompatElapsedMS,
									bDepthPass ? 1 : 0,
									bNativePass1 ? 1 : 0,
									bNativePass2 ? 1 : 0,
									bNativePass3 ? 1 : 0,
									bNativePass4 ? 1 : 0,
									bNativePass5 ? 1 : 0,
									bNativePass6 ? 1 : 0,
									bNativePass7 ? 1 : 0,
									bNativePass8 ? 1 : 0,
									bNativePass9 ? 1 : 0,
									bNativePass10 ? 1 : 0,
									bNativePass11 ? 1 : 0,
									bNativePass12 ? 1 : 0,
									bNativePass13 ? 1 : 0,
									bNativePass14 ? 1 : 0,
									bNativePass15 ? 1 : 0,
									bNativePass16 ? 1 : 0,
									m_bDX11WorldNativePass1Mode ? 1 : 0,
									m_bDX11WorldNativePass2Mode ? 1 : 0,
									m_bDX11WorldNativePass3Mode ? 1 : 0,
									m_bDX11WorldNativePass4Mode ? 1 : 0,
									m_bDX11WorldNativePass5Mode ? 1 : 0,
									m_bDX11WorldNativePass6Mode ? 1 : 0,
									m_bDX11WorldNativePass7Mode ? 1 : 0,
									m_bDX11WorldNativePass8Mode ? 1 : 0,
									m_bDX11WorldNativePass9Mode ? 1 : 0,
									m_bDX11WorldNativePass10Mode ? 1 : 0,
									m_bDX11WorldNativePass11Mode ? 1 : 0,
									m_bDX11WorldNativePass12Mode ? 1 : 0,
									m_bDX11WorldNativePass13Mode ? 1 : 0,
									m_bDX11WorldNativePass14Mode ? 1 : 0,
									m_bDX11WorldNativePass15Mode ? 1 : 0,
									m_bDX11WorldNativePass16Mode ? 1 : 0,
									s_szDX11LastRenderCheckpoint,
									GetDX11RuntimeStage());
						}
					}
					else
					{
						const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
						const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
						const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
						const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);

						int iTerrainTiles = 210 + (iAliveCount / 2) + static_cast<int>(m_dwPerfMapMS / 2) + static_cast<int>(m_dwPerfCharacterMS / 2);
						int iActorCount = 104 + iAliveCount + (iDeadCount / 2) + static_cast<int>(m_dwPerfCharacterMS / 2);
						int iFXCount = 80 + iEffectCount + (iParticleCount / 8) + static_cast<int>(m_dwPerfEffectRenderMS / 2);

						if (iTerrainTiles < 190)
							iTerrainTiles = 190;
						else if (iTerrainTiles > 580)
							iTerrainTiles = 580;

						if (iActorCount < 96)
							iActorCount = 96;
						else if (iActorCount > 500)
							iActorCount = 500;

						if (iFXCount < 72)
							iFXCount = 72;
						else if (iFXCount > 480)
							iFXCount = 480;

						int iTerrainTilesB = iTerrainTiles - 32;
						if (iTerrainTilesB < 176)
							iTerrainTilesB = 176;

						int iActorCountB = iActorCount - 24;
						if (iActorCountB < 88)
							iActorCountB = 88;

						int iFXCountB = iFXCount - 18;
						if (iFXCountB < 66)
							iFXCountB = 66;

						const float fBaseTime = CTimer::Instance().GetCurrentSecond();
						const bool bDepthA = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 0.84f);
						const bool bPassesA = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 0.92f, iTerrainTiles, iActorCount, iFXCount);
						const bool bStateA = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 0.99f, (iActorCount > 360 ? 360 : iActorCount));
						const bool bSpriteA = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 1.06f, (iFXCount > 360 ? 360 : iFXCount));
						const bool bBatchA = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime * 1.12f, (iActorCount > 360 ? 360 : iActorCount));
						const bool bPassesB = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 1.19f, iTerrainTilesB, iActorCountB, iFXCountB);
						const bool bStateB = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 1.26f, (iActorCountB > 340 ? 340 : iActorCountB));
						const bool bDepthB = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 1.33f);
						const bool bFinalcheckDrawResult = (bDepthA && bPassesA && bStateA && bSpriteA && bBatchA && bPassesB && bStateB && bDepthB);
						if (!bFinalcheckDrawResult)
						{
							static DWORD s_dwDX11WorldFinalcheckDrawFailCount = 0;
							++s_dwDX11WorldFinalcheckDrawFailCount;
							if (1 == s_dwDX11WorldFinalcheckDrawFailCount || 0 == (s_dwDX11WorldFinalcheckDrawFailCount % 120))
								TraceError("DX11 world finalcheck test draw call failed (count=%u)", s_dwDX11WorldFinalcheckDrawFailCount);
						}
					}
				}
				else if (bDX11WorldHandoffTest)
				{
					const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
					const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
					const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
					const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);

					int iTerrainTiles = 190 + (iAliveCount / 2) + static_cast<int>(m_dwPerfMapMS / 2) + static_cast<int>(m_dwPerfCharacterMS / 2);
					int iActorCount = 92 + iAliveCount + (iDeadCount / 2) + static_cast<int>(m_dwPerfCharacterMS / 2);
					int iFXCount = 70 + iEffectCount + (iParticleCount / 9) + static_cast<int>(m_dwPerfEffectRenderMS / 2);

					if (iTerrainTiles < 170)
						iTerrainTiles = 170;
					else if (iTerrainTiles > 540)
						iTerrainTiles = 540;

					if (iActorCount < 84)
						iActorCount = 84;
					else if (iActorCount > 460)
						iActorCount = 460;

					if (iFXCount < 64)
						iFXCount = 64;
					else if (iFXCount > 440)
						iFXCount = 440;

					int iTerrainTilesB = iTerrainTiles - 28;
					if (iTerrainTilesB < 160)
						iTerrainTilesB = 160;

					int iActorCountB = iActorCount - 20;
					if (iActorCountB < 76)
						iActorCountB = 76;

					int iFXCountB = iFXCount - 16;
					if (iFXCountB < 58)
						iFXCountB = 58;

					const float fBaseTime = CTimer::Instance().GetCurrentSecond();
					const bool bDepthA = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 0.88f);
					const bool bPassesA = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 0.95f, iTerrainTiles, iActorCount, iFXCount);
					const bool bStateA = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 1.02f, (iActorCount > 320 ? 320 : iActorCount));
					const bool bSpriteA = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 1.08f, (iFXCount > 320 ? 320 : iFXCount));
					const bool bBatchA = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime * 1.14f, (iActorCount > 320 ? 320 : iActorCount));
					const bool bPassesB = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 1.22f, iTerrainTilesB, iActorCountB, iFXCountB);
					const bool bStateB = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 1.29f, (iActorCountB > 300 ? 300 : iActorCountB));
					const bool bHandoffDrawResult = (bDepthA && bPassesA && bStateA && bSpriteA && bBatchA && bPassesB && bStateB);
					if (!bHandoffDrawResult)
					{
						static DWORD s_dwDX11WorldHandoffDrawFailCount = 0;
						++s_dwDX11WorldHandoffDrawFailCount;
						if (1 == s_dwDX11WorldHandoffDrawFailCount || 0 == (s_dwDX11WorldHandoffDrawFailCount % 120))
							TraceError("DX11 world handoff test draw call failed (count=%u)", s_dwDX11WorldHandoffDrawFailCount);
					}
				}
				else if (bDX11WorldSwapchainTest)
				{
					const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
					const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
					const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
					const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);

					int iTerrainTiles = 170 + (iAliveCount / 2) + static_cast<int>(m_dwPerfMapMS / 2) + static_cast<int>(m_dwPerfCharacterMS / 3);
					int iActorCount = 80 + iAliveCount + (iDeadCount / 2) + static_cast<int>(m_dwPerfCharacterMS / 2);
					int iFXCount = 60 + iEffectCount + (iParticleCount / 10) + static_cast<int>(m_dwPerfEffectRenderMS / 2);

					if (iTerrainTiles < 150)
						iTerrainTiles = 150;
					else if (iTerrainTiles > 500)
						iTerrainTiles = 500;

					if (iActorCount < 72)
						iActorCount = 72;
					else if (iActorCount > 420)
						iActorCount = 420;

					if (iFXCount < 56)
						iFXCount = 56;
					else if (iFXCount > 400)
						iFXCount = 400;

					int iTerrainTilesB = iTerrainTiles - 24;
					if (iTerrainTilesB < 140)
						iTerrainTilesB = 140;

					int iActorCountB = iActorCount - 18;
					if (iActorCountB < 64)
						iActorCountB = 64;

					int iFXCountB = iFXCount - 14;
					if (iFXCountB < 52)
						iFXCountB = 52;

					const float fBaseTime = CTimer::Instance().GetCurrentSecond();
					const bool bPassesA = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 0.89f, iTerrainTiles, iActorCount, iFXCount);
					const bool bStateA = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 0.97f, (iActorCount > 280 ? 280 : iActorCount));
					const bool bBatchA = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime * 1.04f, (iActorCount > 300 ? 300 : iActorCount));
					const bool bSpriteA = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 1.12f, (iFXCount > 300 ? 300 : iFXCount));
					const bool bPassesB = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 1.21f, iTerrainTilesB, iActorCountB, iFXCountB);
					const bool bDepthA = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 1.31f);
					const bool bSwapchainDrawResult = (bPassesA && bStateA && bBatchA && bSpriteA && bPassesB && bDepthA);
					if (!bSwapchainDrawResult)
					{
						static DWORD s_dwDX11WorldSwapchainDrawFailCount = 0;
						++s_dwDX11WorldSwapchainDrawFailCount;
						if (1 == s_dwDX11WorldSwapchainDrawFailCount || 0 == (s_dwDX11WorldSwapchainDrawFailCount % 120))
							TraceError("DX11 world swapchain test draw call failed (count=%u)", s_dwDX11WorldSwapchainDrawFailCount);
					}
				}
				else if (bDX11WorldPresentPathTest)
				{
					TPixelPosition kCenterPos;
					kCenterPos.x = 0.0f;
					kCenterPos.y = 0.0f;
					kCenterPos.z = 0.0f;
					GetCenterPosition(&kCenterPos);

					const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
					const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
					const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
					const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);
					const int iTextTailCount = static_cast<int>(m_dwPerfVisibleTextTails);
					const int iTargetVID = static_cast<int>(m_pyPlayer.GetTargetVID());

					int iCameraDistance = 2500;
					CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
					if (pCamera)
					{
						const DirectX::SimpleMath::Vector3& c_rEye = pCamera->GetEye();
						const DirectX::SimpleMath::Vector3& c_rTarget = pCamera->GetTarget();
						const DirectX::SimpleMath::Vector3 v3Distance(c_rEye.x - c_rTarget.x, c_rEye.y - c_rTarget.y, c_rEye.z - c_rTarget.z);
						const float fDistance = v3Distance.Length();
						if (fDistance > 0.0f)
							iCameraDistance = static_cast<int>(fDistance);
					}

					int iAbsCenterX = static_cast<int>(kCenterPos.x);
					if (iAbsCenterX < 0) iAbsCenterX = -iAbsCenterX;
					int iAbsCenterY = static_cast<int>(kCenterPos.y);
					if (iAbsCenterY < 0) iAbsCenterY = -iAbsCenterY;

					int iPerfPressure = static_cast<int>(m_dwCurRenderTime + m_dwCurUpdateTime);
					iPerfPressure += static_cast<int>(m_dwPerfShadowMS + m_dwPerfCharacterMS + m_dwPerfMapMS);
					iPerfPressure += static_cast<int>(m_dwPerfEffectUpdateMS + m_dwPerfEffectRenderMS + m_dwPerfTextTailMS);
					iPerfPressure /= 2;
					if (iPerfPressure < 0)
						iPerfPressure = 0;
					else if (iPerfPressure > 220)
						iPerfPressure = 220;

					int iTerrainTiles = 150 + (iAliveCount / 2) + (iCameraDistance / 44) + (iAbsCenterX % 26) + (iPerfPressure / 3);
					int iActorCount = 68 + iAliveCount + (iDeadCount / 2) + (iTargetVID != 0 ? 20 : 0) + (iTextTailCount / 3) + (iPerfPressure / 4);
					int iFXCount = 52 + iEffectCount + (iParticleCount / 11) + (iAbsCenterY % 24) + (iPerfPressure / 4);

					if (iTerrainTiles < 140)
						iTerrainTiles = 140;
					else if (iTerrainTiles > 460)
						iTerrainTiles = 460;

					if (iActorCount < 64)
						iActorCount = 64;
					else if (iActorCount > 400)
						iActorCount = 400;

					if (iFXCount < 48)
						iFXCount = 48;
					else if (iFXCount > 380)
						iFXCount = 380;

					const float fBaseTime = CTimer::Instance().GetCurrentSecond();
					const bool bPassesA = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 0.97f, iTerrainTiles, iActorCount, iFXCount);
					const bool bStateA = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 1.03f, (iActorCount > 250 ? 250 : iActorCount));
					const bool bPassesB = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 1.08f, iTerrainTiles - 22, iActorCount - 16, iFXCount - 18);
					const bool bSpriteA = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 1.14f, (iFXCount > 280 ? 280 : iFXCount));
					const bool bBatchA = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime * 1.21f, (iActorCount > 280 ? 280 : iActorCount));
					const bool bDepthA = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 1.29f);
					const bool bPresentPathDrawResult = (bPassesA && bStateA && bPassesB && bSpriteA && bBatchA && bDepthA);
					if (!bPresentPathDrawResult)
					{
						static DWORD s_dwDX11WorldPresentPathDrawFailCount = 0;
						++s_dwDX11WorldPresentPathDrawFailCount;
						if (1 == s_dwDX11WorldPresentPathDrawFailCount || 0 == (s_dwDX11WorldPresentPathDrawFailCount % 120))
							TraceError("DX11 world presentpath test draw call failed (count=%u)", s_dwDX11WorldPresentPathDrawFailCount);
					}
				}
				else if (bDX11WorldComposerTest)
				{
					TPixelPosition kCenterPos;
					kCenterPos.x = 0.0f;
					kCenterPos.y = 0.0f;
					kCenterPos.z = 0.0f;
					GetCenterPosition(&kCenterPos);

					const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
					const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
					const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
					const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);
					const int iTextTailCount = static_cast<int>(m_dwPerfVisibleTextTails);
					const int iTargetVID = static_cast<int>(m_pyPlayer.GetTargetVID());

					int iCameraDistance = 2500;
					CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
					if (pCamera)
					{
						const DirectX::SimpleMath::Vector3& c_rEye = pCamera->GetEye();
						const DirectX::SimpleMath::Vector3& c_rTarget = pCamera->GetTarget();
						const DirectX::SimpleMath::Vector3 v3Distance(c_rEye.x - c_rTarget.x, c_rEye.y - c_rTarget.y, c_rEye.z - c_rTarget.z);
						const float fDistance = v3Distance.Length();
						if (fDistance > 0.0f)
							iCameraDistance = static_cast<int>(fDistance);
					}

					int iAbsCenterX = static_cast<int>(kCenterPos.x);
					if (iAbsCenterX < 0) iAbsCenterX = -iAbsCenterX;
					int iAbsCenterY = static_cast<int>(kCenterPos.y);
					if (iAbsCenterY < 0) iAbsCenterY = -iAbsCenterY;

					int iPerfPressure = static_cast<int>(m_dwCurRenderTime + m_dwCurUpdateTime);
					iPerfPressure += static_cast<int>(m_dwPerfShadowMS + m_dwPerfCharacterMS + m_dwPerfMapMS);
					iPerfPressure += static_cast<int>(m_dwPerfEffectUpdateMS + m_dwPerfEffectRenderMS + m_dwPerfTextTailMS);
					iPerfPressure /= 3;
					if (iPerfPressure < 0)
						iPerfPressure = 0;
					else if (iPerfPressure > 200)
						iPerfPressure = 200;

					int iTerrainTiles = 140 + (iAliveCount / 2) + (iCameraDistance / 46) + (iAbsCenterX % 24) + (iPerfPressure / 3);
					int iActorCount = 58 + iAliveCount + (iDeadCount / 2) + (iTargetVID != 0 ? 18 : 0) + (iTextTailCount / 3) + (iPerfPressure / 4);
					int iFXCount = 42 + iEffectCount + (iParticleCount / 12) + (iAbsCenterY % 22) + (iPerfPressure / 4);

					if (iTerrainTiles < 130)
						iTerrainTiles = 130;
					else if (iTerrainTiles > 440)
						iTerrainTiles = 440;

					if (iActorCount < 56)
						iActorCount = 56;
					else if (iActorCount > 380)
						iActorCount = 380;

					if (iFXCount < 40)
						iFXCount = 40;
					else if (iFXCount > 360)
						iFXCount = 360;

					const float fBaseTime = CTimer::Instance().GetCurrentSecond();
					const bool bDepthOk = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(fBaseTime * 0.93f);
					const bool bBatchOk = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(fBaseTime, (iActorCount > 260 ? 260 : iActorCount));
					const bool bStateOk = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 1.05f, (iActorCount > 240 ? 240 : iActorCount));
					const bool bPassesOk = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime * 1.11f, iTerrainTiles, iActorCount, iFXCount);
					const bool bSpriteOk = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 1.17f, (iFXCount > 260 ? 260 : iFXCount));
					const bool bComposerDrawResult = (bDepthOk && bBatchOk && bStateOk && bPassesOk && bSpriteOk);
					if (!bComposerDrawResult)
					{
						static DWORD s_dwDX11WorldComposerDrawFailCount = 0;
						++s_dwDX11WorldComposerDrawFailCount;
						if (1 == s_dwDX11WorldComposerDrawFailCount || 0 == (s_dwDX11WorldComposerDrawFailCount % 120))
							TraceError("DX11 world composer test draw call failed (count=%u)", s_dwDX11WorldComposerDrawFailCount);
					}
				}
				else if (bDX11WorldScenegraphTest)
				{
					TPixelPosition kCenterPos;
					kCenterPos.x = 0.0f;
					kCenterPos.y = 0.0f;
					kCenterPos.z = 0.0f;
					GetCenterPosition(&kCenterPos);

					const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
					const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
					const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
					const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);
					const int iTextTailCount = static_cast<int>(m_dwPerfVisibleTextTails);
					const int iTargetVID = static_cast<int>(m_pyPlayer.GetTargetVID());

					int iCameraDistance = 2500;
					CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
					if (pCamera)
					{
						const DirectX::SimpleMath::Vector3& c_rEye = pCamera->GetEye();
						const DirectX::SimpleMath::Vector3& c_rTarget = pCamera->GetTarget();
						const DirectX::SimpleMath::Vector3 v3Distance(c_rEye.x - c_rTarget.x, c_rEye.y - c_rTarget.y, c_rEye.z - c_rTarget.z);
						const float fDistance = v3Distance.Length();
						if (fDistance > 0.0f)
							iCameraDistance = static_cast<int>(fDistance);
					}

					int iAbsCenterX = static_cast<int>(kCenterPos.x);
					if (iAbsCenterX < 0) iAbsCenterX = -iAbsCenterX;
					int iAbsCenterY = static_cast<int>(kCenterPos.y);
					if (iAbsCenterY < 0) iAbsCenterY = -iAbsCenterY;

					int iPerfPressure = static_cast<int>(m_dwCurRenderTime + m_dwCurUpdateTime);
					iPerfPressure += static_cast<int>(m_dwPerfShadowMS + m_dwPerfCharacterMS + m_dwPerfMapMS);
					iPerfPressure += static_cast<int>(m_dwPerfEffectUpdateMS + m_dwPerfEffectRenderMS + m_dwPerfTextTailMS);
					iPerfPressure /= 3;
					if (iPerfPressure < 0)
						iPerfPressure = 0;
					else if (iPerfPressure > 180)
						iPerfPressure = 180;

					int iTerrainTiles = 130 + (iAliveCount / 2) + (iCameraDistance / 48) + (iAbsCenterX % 20) + (iPerfPressure / 3);
					int iActorCount = 48 + iAliveCount + (iDeadCount / 2) + (iTargetVID != 0 ? 14 : 0) + (iTextTailCount / 3) + (iPerfPressure / 5);
					int iFXCount = 34 + iEffectCount + (iParticleCount / 13) + (iAbsCenterY % 18) + (iPerfPressure / 4);

					if (iTerrainTiles < 120)
						iTerrainTiles = 120;
					else if (iTerrainTiles > 420)
						iTerrainTiles = 420;

					if (iActorCount < 44)
						iActorCount = 44;
					else if (iActorCount > 360)
						iActorCount = 360;

					if (iFXCount < 30)
						iFXCount = 30;
					else if (iFXCount > 340)
						iFXCount = 340;

					const float fBaseTime = CTimer::Instance().GetCurrentSecond();
					const bool bPassesOk = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(fBaseTime, iTerrainTiles, iActorCount, iFXCount);
					const bool bStateOk = m_grpDeviceDX11.DrawBootstrapWorldStateTest(fBaseTime * 1.07f, (iActorCount > 220 ? 220 : iActorCount));
					const bool bSpriteOk = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(fBaseTime * 1.13f, (iFXCount > 240 ? 240 : iFXCount));
					const bool bScenegraphDrawResult = (bPassesOk && bStateOk && bSpriteOk);
					if (!bScenegraphDrawResult)
					{
						static DWORD s_dwDX11WorldScenegraphDrawFailCount = 0;
						++s_dwDX11WorldScenegraphDrawFailCount;
						if (1 == s_dwDX11WorldScenegraphDrawFailCount || 0 == (s_dwDX11WorldScenegraphDrawFailCount % 120))
							TraceError("DX11 world scenegraph test draw call failed (count=%u)", s_dwDX11WorldScenegraphDrawFailCount);
					}
				}
				else if (bDX11WorldPipelineTest)
				{
					TPixelPosition kCenterPos;
					kCenterPos.x = 0.0f;
					kCenterPos.y = 0.0f;
					kCenterPos.z = 0.0f;
					GetCenterPosition(&kCenterPos);

					const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
					const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
					const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
					const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);
					const int iTextTailCount = static_cast<int>(m_dwPerfVisibleTextTails);
					const int iTargetVID = static_cast<int>(m_pyPlayer.GetTargetVID());

					int iCameraDistance = 2500;
					CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
					if (pCamera)
					{
						const DirectX::SimpleMath::Vector3& c_rEye = pCamera->GetEye();
						const DirectX::SimpleMath::Vector3& c_rTarget = pCamera->GetTarget();
						const DirectX::SimpleMath::Vector3 v3Distance(c_rEye.x - c_rTarget.x, c_rEye.y - c_rTarget.y, c_rEye.z - c_rTarget.z);
						const float fDistance = v3Distance.Length();
						if (fDistance > 0.0f)
							iCameraDistance = static_cast<int>(fDistance);
					}

					int iAbsCenterX = static_cast<int>(kCenterPos.x);
					if (iAbsCenterX < 0) iAbsCenterX = -iAbsCenterX;
					int iAbsCenterY = static_cast<int>(kCenterPos.y);
					if (iAbsCenterY < 0) iAbsCenterY = -iAbsCenterY;

					int iPerfPressure = static_cast<int>(m_dwCurRenderTime + m_dwCurUpdateTime);
					iPerfPressure += static_cast<int>(m_dwPerfShadowMS + m_dwPerfCharacterMS + m_dwPerfMapMS);
					iPerfPressure += static_cast<int>(m_dwPerfEffectUpdateMS + m_dwPerfEffectRenderMS + m_dwPerfTextTailMS);
					iPerfPressure /= 4;
					if (iPerfPressure < 0)
						iPerfPressure = 0;
					else if (iPerfPressure > 160)
						iPerfPressure = 160;

					static float s_fPipelinePressureEMA = 0.0f;
					static int s_iPipelineBand = 1;
					s_fPipelinePressureEMA = (s_fPipelinePressureEMA * 0.88f) + (static_cast<float>(iPerfPressure) * 0.12f);

					if (s_fPipelinePressureEMA > 28.0f)
						s_iPipelineBand = 0;
					else if (s_fPipelinePressureEMA < 16.0f)
						s_iPipelineBand = 2;
					else
						s_iPipelineBand = 1;

					int iTerrainBase = 120 + (iAliveCount / 2) + (iCameraDistance / 52) + (iAbsCenterX % 22) + (iPerfPressure / 3);
					int iActorBase = 36 + iAliveCount + (iDeadCount / 2) + (iTargetVID != 0 ? 12 : 0) + (iTextTailCount / 3) + (iPerfPressure / 5);
					int iFXBase = 22 + iEffectCount + (iParticleCount / 14) + (iAbsCenterY % 20) + (iPerfPressure / 4);

					if (iTerrainBase < 110)
						iTerrainBase = 110;
					else if (iTerrainBase > 400)
						iTerrainBase = 400;

					if (iActorBase < 42)
						iActorBase = 42;
					else if (iActorBase > 340)
						iActorBase = 340;

					if (iFXBase < 30)
						iFXBase = 30;
					else if (iFXBase > 320)
						iFXBase = 320;

					int iPassCount = (2 == s_iPipelineBand ? 3 : 2);
					if (0 == s_iPipelineBand)
						iPassCount = 2;

					bool bPipelineDrawResult = true;
					const float fDrawTime = CTimer::Instance().GetCurrentSecond();
					for (int iPass = 0; iPass < iPassCount; ++iPass)
					{
						int iTerrainTiles = iTerrainBase - (iPass * 20);
						int iActorCount = iActorBase - (iPass * 12);
						int iFXCount = iFXBase - (iPass * 10);

						if (iTerrainTiles < 96)
							iTerrainTiles = 96;
						if (iActorCount < 36)
							iActorCount = 36;
						if (iFXCount < 24)
							iFXCount = 24;

						const bool bPassResult = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(
							fDrawTime + (static_cast<float>(iPass) * 0.23f),
							iTerrainTiles,
							iActorCount,
							iFXCount);
						bPipelineDrawResult = bPipelineDrawResult && bPassResult;
					}

					if (!bPipelineDrawResult)
					{
						static DWORD s_dwDX11WorldPipelineDrawFailCount = 0;
						++s_dwDX11WorldPipelineDrawFailCount;
						if (1 == s_dwDX11WorldPipelineDrawFailCount || 0 == (s_dwDX11WorldPipelineDrawFailCount % 120))
							TraceError("DX11 world pipeline test draw call failed (count=%u)", s_dwDX11WorldPipelineDrawFailCount);
					}
				}
				else if (bDX11WorldFramegraphTest)
				{
					TPixelPosition kCenterPos;
					kCenterPos.x = 0.0f;
					kCenterPos.y = 0.0f;
					kCenterPos.z = 0.0f;
					GetCenterPosition(&kCenterPos);

					const float fAliveNow = static_cast<float>(m_kChrMgr.GetAliveInstanceCount());
					const float fDeadNow = static_cast<float>(m_kChrMgr.GetDeadInstanceCount());
					const float fEffectsNow = static_cast<float>(m_dwPerfActiveEffects);
					const float fParticlesNow = static_cast<float>(m_dwPerfActiveParticles);
					const float fTextTailNow = static_cast<float>(m_dwPerfVisibleTextTails);
					const int iTargetVID = static_cast<int>(m_pyPlayer.GetTargetVID());

					float fCameraDistance = 2500.0f;
					CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
					if (pCamera)
					{
						const DirectX::SimpleMath::Vector3& c_rEye = pCamera->GetEye();
						const DirectX::SimpleMath::Vector3& c_rTarget = pCamera->GetTarget();
						const DirectX::SimpleMath::Vector3 v3Distance(c_rEye.x - c_rTarget.x, c_rEye.y - c_rTarget.y, c_rEye.z - c_rTarget.z);
						const float fDistance = v3Distance.Length();
						if (fDistance > 0.0f)
							fCameraDistance = fDistance;
					}

					static float s_fEmaAlive = 0.0f;
					static float s_fEmaDead = 0.0f;
					static float s_fEmaEffects = 0.0f;
					static float s_fEmaParticles = 0.0f;
					static float s_fEmaTextTail = 0.0f;
					static float s_fEmaPressure = 0.0f;
					static int s_iQualityBand = 1;

					const float fAlpha = 0.18f;
					s_fEmaAlive = (s_fEmaAlive * (1.0f - fAlpha)) + (fAliveNow * fAlpha);
					s_fEmaDead = (s_fEmaDead * (1.0f - fAlpha)) + (fDeadNow * fAlpha);
					s_fEmaEffects = (s_fEmaEffects * (1.0f - fAlpha)) + (fEffectsNow * fAlpha);
					s_fEmaParticles = (s_fEmaParticles * (1.0f - fAlpha)) + (fParticlesNow * fAlpha);
					s_fEmaTextTail = (s_fEmaTextTail * (1.0f - fAlpha)) + (fTextTailNow * fAlpha);

					float fPressure = static_cast<float>(m_dwCurRenderTime + m_dwCurUpdateTime);
					fPressure += static_cast<float>(m_dwPerfShadowMS + m_dwPerfCharacterMS + m_dwPerfMapMS);
					fPressure += static_cast<float>(m_dwPerfEffectUpdateMS + m_dwPerfEffectRenderMS + m_dwPerfTextTailMS);
					fPressure *= 0.25f;
					s_fEmaPressure = (s_fEmaPressure * 0.85f) + (fPressure * 0.15f);

					if (s_fEmaPressure > 22.0f)
						s_iQualityBand = 0;
					else if (s_fEmaPressure < 14.0f)
						s_iQualityBand = 2;
					else
						s_iQualityBand = 1;

					const int iBandTerrainMul = (0 == s_iQualityBand ? 80 : (2 == s_iQualityBand ? 120 : 100));
					const int iBandActorMul = (0 == s_iQualityBand ? 85 : (2 == s_iQualityBand ? 118 : 100));
					const int iBandFXMul = (0 == s_iQualityBand ? 78 : (2 == s_iQualityBand ? 122 : 100));

					int iAbsCenterX = static_cast<int>(kCenterPos.x);
					if (iAbsCenterX < 0) iAbsCenterX = -iAbsCenterX;
					int iAbsCenterY = static_cast<int>(kCenterPos.y);
					if (iAbsCenterY < 0) iAbsCenterY = -iAbsCenterY;

					int iTerrainBase = 100 + static_cast<int>(s_fEmaAlive * 0.52f) + static_cast<int>(fCameraDistance / 52.0f) + (iAbsCenterX % 23);
					int iActorBase = 28 + static_cast<int>(s_fEmaAlive) + static_cast<int>(s_fEmaDead * 0.45f) + (iTargetVID != 0 ? 14 : 0) + static_cast<int>(s_fEmaTextTail * 0.22f);
					int iFXBase = 18 + static_cast<int>(s_fEmaEffects) + static_cast<int>(s_fEmaParticles / 14.0f) + (iAbsCenterY % 21);

					int iTerrainTiles = (iTerrainBase * iBandTerrainMul) / 100;
					int iActorCount = (iActorBase * iBandActorMul) / 100;
					int iFXCount = (iFXBase * iBandFXMul) / 100;

					if (iTerrainTiles < 100)
						iTerrainTiles = 100;
					else if (iTerrainTiles > 380)
						iTerrainTiles = 380;

					if (iActorCount < 36)
						iActorCount = 36;
					else if (iActorCount > 320)
						iActorCount = 320;

					if (iFXCount < 24)
						iFXCount = 24;
					else if (iFXCount > 300)
						iFXCount = 300;

					const bool bFramegraphDrawResult = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(
						CTimer::Instance().GetCurrentSecond(),
						iTerrainTiles,
						iActorCount,
						iFXCount);
					if (!bFramegraphDrawResult)
					{
						static DWORD s_dwDX11WorldFramegraphDrawFailCount = 0;
						++s_dwDX11WorldFramegraphDrawFailCount;
						if (1 == s_dwDX11WorldFramegraphDrawFailCount || 0 == (s_dwDX11WorldFramegraphDrawFailCount % 120))
							TraceError("DX11 world framegraph test draw call failed (count=%u)", s_dwDX11WorldFramegraphDrawFailCount);
					}
				}
				else if (bDX11WorldInstanceFeedTest)
				{
					TPixelPosition kCenterPos;
					kCenterPos.x = 0.0f;
					kCenterPos.y = 0.0f;
					kCenterPos.z = 0.0f;
					GetCenterPosition(&kCenterPos);

					const float fAliveNow = static_cast<float>(m_kChrMgr.GetAliveInstanceCount());
					const float fDeadNow = static_cast<float>(m_kChrMgr.GetDeadInstanceCount());
					const float fEffectsNow = static_cast<float>(m_dwPerfActiveEffects);
					const float fParticlesNow = static_cast<float>(m_dwPerfActiveParticles);
					const float fTextTailNow = static_cast<float>(m_dwPerfVisibleTextTails);
					const int iTargetVID = static_cast<int>(m_pyPlayer.GetTargetVID());

					float fCameraDistance = 2500.0f;
					CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
					if (pCamera)
					{
						const DirectX::SimpleMath::Vector3& c_rEye = pCamera->GetEye();
						const DirectX::SimpleMath::Vector3& c_rTarget = pCamera->GetTarget();
						const DirectX::SimpleMath::Vector3 v3Distance(c_rEye.x - c_rTarget.x, c_rEye.y - c_rTarget.y, c_rEye.z - c_rTarget.z);
						const float fDistance = v3Distance.Length();
						if (fDistance > 0.0f)
							fCameraDistance = fDistance;
					}

					static float s_fEmaAlive = 0.0f;
					static float s_fEmaDead = 0.0f;
					static float s_fEmaEffects = 0.0f;
					static float s_fEmaParticles = 0.0f;
					static float s_fEmaTextTail = 0.0f;
					const float fAlpha = 0.20f;
					s_fEmaAlive = (s_fEmaAlive * (1.0f - fAlpha)) + (fAliveNow * fAlpha);
					s_fEmaDead = (s_fEmaDead * (1.0f - fAlpha)) + (fDeadNow * fAlpha);
					s_fEmaEffects = (s_fEmaEffects * (1.0f - fAlpha)) + (fEffectsNow * fAlpha);
					s_fEmaParticles = (s_fEmaParticles * (1.0f - fAlpha)) + (fParticlesNow * fAlpha);
					s_fEmaTextTail = (s_fEmaTextTail * (1.0f - fAlpha)) + (fTextTailNow * fAlpha);

					int iAbsCenterX = static_cast<int>(kCenterPos.x);
					if (iAbsCenterX < 0) iAbsCenterX = -iAbsCenterX;
					int iAbsCenterY = static_cast<int>(kCenterPos.y);
					if (iAbsCenterY < 0) iAbsCenterY = -iAbsCenterY;

					int iPerfPressure = static_cast<int>(m_dwCurRenderTime + m_dwCurUpdateTime);
					iPerfPressure += static_cast<int>(m_dwPerfShadowMS + m_dwPerfCharacterMS + m_dwPerfMapMS);
					iPerfPressure += static_cast<int>(m_dwPerfEffectUpdateMS + m_dwPerfEffectRenderMS + m_dwPerfTextTailMS);
					iPerfPressure /= 4;
					if (iPerfPressure > 140)
						iPerfPressure = 140;
					else if (iPerfPressure < 0)
						iPerfPressure = 0;

					int iTerrainTiles = 90 + static_cast<int>(s_fEmaAlive * 0.55f) + static_cast<int>(fCameraDistance / 50.0f) + (iPerfPressure / 3) + (iAbsCenterX % 17);
					if (iTerrainTiles < 96)
						iTerrainTiles = 96;
					else if (iTerrainTiles > 360)
						iTerrainTiles = 360;

					int iActorCount = 24 + static_cast<int>(s_fEmaAlive) + static_cast<int>(s_fEmaDead * 0.45f) + (iTargetVID != 0 ? 14 : 0) + static_cast<int>(s_fEmaTextTail * 0.25f) + (iPerfPressure / 7);
					if (iActorCount < 30)
						iActorCount = 30;
					else if (iActorCount > 300)
						iActorCount = 300;

					int iFXCount = 14 + static_cast<int>(s_fEmaEffects) + static_cast<int>(s_fEmaParticles / 14.0f) + (iPerfPressure / 5) + (iAbsCenterY % 19);
					if (iFXCount < 20)
						iFXCount = 20;
					else if (iFXCount > 280)
						iFXCount = 280;

					const bool bInstanceFeedDrawResult = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(
						CTimer::Instance().GetCurrentSecond(),
						iTerrainTiles,
						iActorCount,
						iFXCount);
					if (!bInstanceFeedDrawResult)
					{
						static DWORD s_dwDX11WorldInstanceFeedDrawFailCount = 0;
						++s_dwDX11WorldInstanceFeedDrawFailCount;
						if (1 == s_dwDX11WorldInstanceFeedDrawFailCount || 0 == (s_dwDX11WorldInstanceFeedDrawFailCount % 120))
							TraceError("DX11 world instance feed test draw call failed (count=%u)", s_dwDX11WorldInstanceFeedDrawFailCount);
					}
				}
				else if (bDX11WorldMetricsTest)
				{
					TPixelPosition kCenterPos;
					kCenterPos.x = 0.0f;
					kCenterPos.y = 0.0f;
					kCenterPos.z = 0.0f;
					GetCenterPosition(&kCenterPos);

					const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
					const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
					const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
					const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);
					const int iTextTailCount = static_cast<int>(m_dwPerfVisibleTextTails);
					const int iTargetVID = static_cast<int>(m_pyPlayer.GetTargetVID());

					int iCameraDistance = 2500;
					CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
					if (pCamera)
					{
						const DirectX::SimpleMath::Vector3& c_rEye = pCamera->GetEye();
						const DirectX::SimpleMath::Vector3& c_rTarget = pCamera->GetTarget();
						const DirectX::SimpleMath::Vector3 v3Distance(c_rEye.x - c_rTarget.x, c_rEye.y - c_rTarget.y, c_rEye.z - c_rTarget.z);
						const float fDistance = v3Distance.Length();
						if (fDistance > 0.0f)
							iCameraDistance = static_cast<int>(fDistance);
					}

					int iAbsCenterX = static_cast<int>(kCenterPos.x);
					if (iAbsCenterX < 0) iAbsCenterX = -iAbsCenterX;
					int iAbsCenterY = static_cast<int>(kCenterPos.y);
					if (iAbsCenterY < 0) iAbsCenterY = -iAbsCenterY;

					int iPerfPressure = static_cast<int>(m_dwCurRenderTime + m_dwCurUpdateTime);
					iPerfPressure += static_cast<int>(m_dwPerfShadowMS + m_dwPerfCharacterMS + m_dwPerfMapMS);
					iPerfPressure += static_cast<int>(m_dwPerfEffectUpdateMS + m_dwPerfEffectRenderMS + m_dwPerfTextTailMS);
					iPerfPressure /= 3;
					if (iPerfPressure > 120)
						iPerfPressure = 120;
					else if (iPerfPressure < 0)
						iPerfPressure = 0;

					int iTerrainTiles = 96 + (iAliveCount / 2) + (iCameraDistance / 55) + (iAbsCenterX % 32) + (iPerfPressure / 2);
					if (iTerrainTiles < 90)
						iTerrainTiles = 90;
					else if (iTerrainTiles > 340)
						iTerrainTiles = 340;

					int iActorCount = 24 + iAliveCount + (iDeadCount / 2) + (iTargetVID != 0 ? 12 : 0) + (iTextTailCount / 3) + (iPerfPressure / 6);
					if (iActorCount < 30)
						iActorCount = 30;
					else if (iActorCount > 280)
						iActorCount = 280;

					int iFXCount = 12 + iEffectCount + (iParticleCount / 16) + (iAbsCenterY % 24) + (iPerfPressure / 4);
					if (iFXCount < 20)
						iFXCount = 20;
					else if (iFXCount > 260)
						iFXCount = 260;

					const bool bMetricsDrawResult = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(
						CTimer::Instance().GetCurrentSecond(),
						iTerrainTiles,
						iActorCount,
						iFXCount);
					if (!bMetricsDrawResult)
					{
						static DWORD s_dwDX11WorldMetricsDrawFailCount = 0;
						++s_dwDX11WorldMetricsDrawFailCount;
						if (1 == s_dwDX11WorldMetricsDrawFailCount || 0 == (s_dwDX11WorldMetricsDrawFailCount % 120))
							TraceError("DX11 world metrics test draw call failed (count=%u)", s_dwDX11WorldMetricsDrawFailCount);
					}
				}
				else if (bDX11WorldRealtimeTest)
				{
					TPixelPosition kCenterPos;
					kCenterPos.x = 0.0f;
					kCenterPos.y = 0.0f;
					kCenterPos.z = 0.0f;
					GetCenterPosition(&kCenterPos);

					const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
					const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
					const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
					const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);
					const int iTextTailCount = static_cast<int>(m_dwPerfVisibleTextTails);
					const int iTargetVID = static_cast<int>(m_pyPlayer.GetTargetVID());

					int iCameraDistance = 2500;
					CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
					if (pCamera)
					{
						const DirectX::SimpleMath::Vector3& c_rEye = pCamera->GetEye();
						const DirectX::SimpleMath::Vector3& c_rTarget = pCamera->GetTarget();
						const DirectX::SimpleMath::Vector3 v3Distance(c_rEye.x - c_rTarget.x, c_rEye.y - c_rTarget.y, c_rEye.z - c_rTarget.z);
						const float fDistance = v3Distance.Length();
						if (fDistance > 0.0f)
							iCameraDistance = static_cast<int>(fDistance);
					}

					int iLoadBoost = static_cast<int>(m_dwCurRenderTime / 2) + static_cast<int>(m_dwCurUpdateTime / 2);
					if (iLoadBoost > 60)
						iLoadBoost = 60;
					else if (iLoadBoost < 0)
						iLoadBoost = 0;

					int iAbsCenterX = static_cast<int>(kCenterPos.x);
					if (iAbsCenterX < 0) iAbsCenterX = -iAbsCenterX;
					int iAbsCenterY = static_cast<int>(kCenterPos.y);
					if (iAbsCenterY < 0) iAbsCenterY = -iAbsCenterY;

					int iTerrainTiles = 100 + (iAliveCount / 2) + (iCameraDistance / 65) + ((iAbsCenterX / 100) % 20) + iLoadBoost;
					if (iTerrainTiles < 80)
						iTerrainTiles = 80;
					else if (iTerrainTiles > 320)
						iTerrainTiles = 320;

					int iActorCount = 28 + iAliveCount + (iDeadCount / 2) + (iTargetVID != 0 ? 10 : 0) + (iTextTailCount / 4);
					if (iActorCount < 30)
						iActorCount = 30;
					else if (iActorCount > 260)
						iActorCount = 260;

					int iFXCount = 16 + iEffectCount + (iParticleCount / 18) + ((iAbsCenterY / 100) % 15) + iLoadBoost;
					if (iFXCount < 20)
						iFXCount = 20;
					else if (iFXCount > 240)
						iFXCount = 240;

					const bool bRealtimeDrawResult = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(
						CTimer::Instance().GetCurrentSecond(),
						iTerrainTiles,
						iActorCount,
						iFXCount);
					if (!bRealtimeDrawResult)
					{
						static DWORD s_dwDX11WorldRealtimeDrawFailCount = 0;
						++s_dwDX11WorldRealtimeDrawFailCount;
						if (1 == s_dwDX11WorldRealtimeDrawFailCount || 0 == (s_dwDX11WorldRealtimeDrawFailCount % 120))
							TraceError("DX11 world realtime test draw call failed (count=%u)", s_dwDX11WorldRealtimeDrawFailCount);
					}
				}
				else if (bDX11WorldSubsystemTest)
				{
					const int iAliveCount = static_cast<int>(m_kChrMgr.GetAliveInstanceCount());
					const int iDeadCount = static_cast<int>(m_kChrMgr.GetDeadInstanceCount());
					const int iEffectCount = static_cast<int>(m_dwPerfActiveEffects);
					const int iParticleCount = static_cast<int>(m_dwPerfActiveParticles);
					const int iTextTailCount = static_cast<int>(m_dwPerfVisibleTextTails);
					const int iTargetVID = static_cast<int>(m_pyPlayer.GetTargetVID());

					int iCameraDistance = 2500;
					CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
					if (pCamera)
					{
						const DirectX::SimpleMath::Vector3& c_rEye = pCamera->GetEye();
						const DirectX::SimpleMath::Vector3& c_rTarget = pCamera->GetTarget();
						const DirectX::SimpleMath::Vector3 v3Distance(c_rEye.x - c_rTarget.x, c_rEye.y - c_rTarget.y, c_rEye.z - c_rTarget.z);
						const float fDistance = v3Distance.Length();
						if (fDistance > 0.0f)
							iCameraDistance = static_cast<int>(fDistance);
					}

					int iTerrainTiles = 120 + (iAliveCount / 3) + (iCameraDistance / 60);
					if (iTerrainTiles < 80)
						iTerrainTiles = 80;
					else if (iTerrainTiles > 300)
						iTerrainTiles = 300;

					int iActorCount = 30 + iAliveCount + (iDeadCount / 2) + (iTargetVID != 0 ? 8 : 0);
					if (iActorCount < 30)
						iActorCount = 30;
					else if (iActorCount > 240)
						iActorCount = 240;

					int iFXCount = 20 + iEffectCount + (iParticleCount / 20) + (iTextTailCount / 2);
					if (iFXCount < 20)
						iFXCount = 20;
					else if (iFXCount > 220)
						iFXCount = 220;

					const bool bSubsystemDrawResult = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(
						CTimer::Instance().GetCurrentSecond(),
						iTerrainTiles,
						iActorCount,
						iFXCount);
					if (!bSubsystemDrawResult)
					{
						static DWORD s_dwDX11WorldSubsystemDrawFailCount = 0;
						++s_dwDX11WorldSubsystemDrawFailCount;
						if (1 == s_dwDX11WorldSubsystemDrawFailCount || 0 == (s_dwDX11WorldSubsystemDrawFailCount % 120))
							TraceError("DX11 world subsystem test draw call failed (count=%u)", s_dwDX11WorldSubsystemDrawFailCount);
					}
				}
				else if (bDX11WorldBridgeTest)
				{
					TPixelPosition kCenterPos;
					kCenterPos.x = 0.0f;
					kCenterPos.y = 0.0f;
					kCenterPos.z = 0.0f;
					GetCenterPosition(&kCenterPos);

					int iAbsCenterX = static_cast<int>(kCenterPos.x);
					if (iAbsCenterX < 0) iAbsCenterX = -iAbsCenterX;
					int iAbsCenterY = static_cast<int>(kCenterPos.y);
					if (iAbsCenterY < 0) iAbsCenterY = -iAbsCenterY;

					const int iTerrainTiles = 120 + ((iAbsCenterX / 100) % 80);
					const int iActorCount = 40 + std::min(140, static_cast<int>(m_dwPerfVisibleTextTails) + static_cast<int>(m_dwPerfActiveEffects / 2));
					const int iFXCount = 20 + std::min(120, static_cast<int>(m_dwPerfActiveEffects) + static_cast<int>(m_dwPerfActiveParticles / 25) + ((iAbsCenterY / 100) % 20));

					const bool bBridgeDrawResult = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(
						CTimer::Instance().GetCurrentSecond(),
						iTerrainTiles,
						iActorCount,
						iFXCount);
					if (!bBridgeDrawResult)
					{
						static DWORD s_dwDX11WorldBridgeDrawFailCount = 0;
						++s_dwDX11WorldBridgeDrawFailCount;
						if (1 == s_dwDX11WorldBridgeDrawFailCount || 0 == (s_dwDX11WorldBridgeDrawFailCount % 120))
							TraceError("DX11 world bridge test draw call failed (count=%u)", s_dwDX11WorldBridgeDrawFailCount);
					}
				}
				else if (bDX11WorldPassesTest)
				{
					const bool bPassesDrawResult = m_grpDeviceDX11.DrawBootstrapWorldPassesTest(CTimer::Instance().GetCurrentSecond(), 180, 120, 80);
					if (!bPassesDrawResult)
					{
						static DWORD s_dwDX11WorldPassesDrawFailCount = 0;
						++s_dwDX11WorldPassesDrawFailCount;
						if (1 == s_dwDX11WorldPassesDrawFailCount || 0 == (s_dwDX11WorldPassesDrawFailCount % 120))
							TraceError("DX11 world passes test draw call failed (count=%u)", s_dwDX11WorldPassesDrawFailCount);
					}
				}
				else if (bDX11WorldStateTest)
				{
					const bool bStateDrawResult = m_grpDeviceDX11.DrawBootstrapWorldStateTest(CTimer::Instance().GetCurrentSecond(), 180);
					if (!bStateDrawResult)
					{
						static DWORD s_dwDX11WorldStateDrawFailCount = 0;
						++s_dwDX11WorldStateDrawFailCount;
						if (1 == s_dwDX11WorldStateDrawFailCount || 0 == (s_dwDX11WorldStateDrawFailCount % 120))
							TraceError("DX11 world state test draw call failed (count=%u)", s_dwDX11WorldStateDrawFailCount);
					}
				}
				else if (bDX11WorldSpriteTest)
				{
					const bool bSpriteDrawResult = m_grpDeviceDX11.DrawBootstrapWorldSpriteTest(CTimer::Instance().GetCurrentSecond(), 200);
					if (!bSpriteDrawResult)
					{
						static DWORD s_dwDX11WorldSpriteDrawFailCount = 0;
						++s_dwDX11WorldSpriteDrawFailCount;
						if (1 == s_dwDX11WorldSpriteDrawFailCount || 0 == (s_dwDX11WorldSpriteDrawFailCount % 120))
							TraceError("DX11 world sprite test draw call failed (count=%u)", s_dwDX11WorldSpriteDrawFailCount);
					}
				}
				else if (bDX11WorldBatchTest)
				{
					const bool bBatchDrawResult = m_grpDeviceDX11.DrawBootstrapWorldBatchTest(CTimer::Instance().GetCurrentSecond(), 200);
					if (!bBatchDrawResult)
					{
						static DWORD s_dwDX11WorldBatchDrawFailCount = 0;
						++s_dwDX11WorldBatchDrawFailCount;
						if (1 == s_dwDX11WorldBatchDrawFailCount || 0 == (s_dwDX11WorldBatchDrawFailCount % 120))
							TraceError("DX11 world batch test draw call failed (count=%u)", s_dwDX11WorldBatchDrawFailCount);
					}
				}
				else if (bDX11WorldDepthTest)
				{
					const bool bDepthDrawResult = m_grpDeviceDX11.DrawBootstrapWorldDepthTest(CTimer::Instance().GetCurrentSecond());
					if (!bDepthDrawResult)
					{
						static DWORD s_dwDX11WorldDepthDrawFailCount = 0;
						++s_dwDX11WorldDepthDrawFailCount;
						if (1 == s_dwDX11WorldDepthDrawFailCount || 0 == (s_dwDX11WorldDepthDrawFailCount % 120))
							TraceError("DX11 world depth test draw call failed (count=%u)", s_dwDX11WorldDepthDrawFailCount);
					}
				}

				if (bDX11UINativeConfigEnabled)
				{
					long lx = 0;
					long ly = 0;
					m_kWndMgr.GetMousePosition(lx, ly);
					const bool bUIDrawResult = bDX11UITextureTest
						? m_grpDeviceDX11.DrawBootstrapUITextureOverlay(static_cast<float>(lx), static_cast<float>(ly))
						: m_grpDeviceDX11.DrawBootstrapUIOverlay(static_cast<float>(lx), static_cast<float>(ly));
					if (!bUIDrawResult)
					{
						static DWORD s_dwDX11UIOverlayDrawFailCount = 0;
						++s_dwDX11UIOverlayDrawFailCount;
						if (1 == s_dwDX11UIOverlayDrawFailCount || 0 == (s_dwDX11UIOverlayDrawFailCount % 120))
							TraceError("DX11 UI native test draw call failed (count=%u)", s_dwDX11UIOverlayDrawFailCount);
					}
				}
			}
			else if (bDX11FirstPassHybrid)
			{
				if (!m_grpDeviceDX11.DrawBootstrapTriangle())
				{
					static DWORD s_dwDX11BootstrapDrawFailCount = 0;
					++s_dwDX11BootstrapDrawFailCount;
					if (1 == s_dwDX11BootstrapDrawFailCount || 0 == (s_dwDX11BootstrapDrawFailCount % 120))
						TraceError("DX11 bootstrap draw call failed (count=%u)", s_dwDX11BootstrapDrawFailCount);
				}
			}
		}

		if (!bDX11FirstPassHybrid && bDX11UINativeTest && bDX11AllowBootstrapTestRendering)
		{
			// DX11 first-pass migration:
			// keep frame lifecycle active (clear/present/resize/vsync) before porting full world/UI render graph.
			__SetDX11RenderCheckpoint("dx11_present_bootstrap");
			if (!m_grpDeviceDX11.Present())
			{
				__SetDX11RenderCheckpoint("dx11_present_bootstrap_fail");
				__LogDX11RenderCheckpoint(true);
				return;
			}

			const DWORD dwRenderEndTimeDX11 = ELTimer_GetMSec();
			static DWORD s_dwRenderCheckTimeDX11 = dwRenderEndTimeDX11;
			static DWORD s_dwRenderRangeTimeDX11 = 0;
			static DWORD s_dwRenderRangeFrameDX11 = 0;

			m_dwCurRenderTime = dwRenderEndTimeDX11 - dwRenderStartTime;
			s_dwRenderRangeTimeDX11 += m_dwCurRenderTime;
			++s_dwRenderRangeFrameDX11;

			if (dwRenderEndTimeDX11 - s_dwRenderCheckTimeDX11 > 1000) [[unlikely]]
			{
				m_fAveRenderTime = float(double(s_dwRenderRangeTimeDX11) / double(s_dwRenderRangeFrameDX11));

				s_dwRenderCheckTimeDX11 = ELTimer_GetMSec();
				s_dwRenderRangeTimeDX11 = 0;
				s_dwRenderRangeFrameDX11 = 0;
			}

			__UpdatePerfAutoAdjustment();
			__SetDX11RenderCheckpoint("runstep_end_dx11_bootstrap");
			__LogDX11RenderCheckpoint(false);
			++rRenderFrameCount;
			return;
		}
		else if (!bDX11FirstPassHybrid && !bDX11UINativeTest)
		{
			// In native-visible cutover runtime, do not issue Present(TEST):
			// this path must never reactivate any bootstrap/test presentation behavior.
			if (bDX11CutoverRuntimeMode || bDX11NativeVisibleConfigEnabled)
			{
				__SetDX11RenderCheckpoint("dx11_present_test_cutover_runtime_skip");
			}
			else
			{
				__SetDX11RenderCheckpoint("dx11_present_test_cutover_runtime");
				m_grpDeviceDX11.PresentTest();
			}
		}
		else if (bDX11VisibleBootstrap && bDX11AllowBootstrapTestRendering)
		{
			__SetDX11RenderCheckpoint("dx11_present_visible_bootstrap");
			if (!m_grpDeviceDX11.Present())
			{
				__SetDX11RenderCheckpoint("dx11_present_visible_bootstrap_fail");
				__LogDX11RenderCheckpoint(true);
				return;
			}

			const DWORD dwRenderEndTimeDX11 = ELTimer_GetMSec();
			static DWORD s_dwRenderCheckTimeDX11Visible = dwRenderEndTimeDX11;
			static DWORD s_dwRenderRangeTimeDX11Visible = 0;
			static DWORD s_dwRenderRangeFrameDX11Visible = 0;

			m_dwCurRenderTime = dwRenderEndTimeDX11 - dwRenderStartTime;
			s_dwRenderRangeTimeDX11Visible += m_dwCurRenderTime;
			++s_dwRenderRangeFrameDX11Visible;

			if (dwRenderEndTimeDX11 - s_dwRenderCheckTimeDX11Visible > 1000) [[unlikely]]
			{
				m_fAveRenderTime = float(double(s_dwRenderRangeTimeDX11Visible) / double(s_dwRenderRangeFrameDX11Visible));

				s_dwRenderCheckTimeDX11Visible = ELTimer_GetMSec();
				s_dwRenderRangeTimeDX11Visible = 0;
				s_dwRenderRangeFrameDX11Visible = 0;
			}

			__UpdatePerfAutoAdjustment();
			__SetDX11RenderCheckpoint("runstep_end_dx11_visible");
			__LogDX11RenderCheckpoint(false);
			++rRenderFrameCount;
			return;
		}
		else if (bDX11UINativeTest && bDX11AllowBootstrapTestRendering)
		{
			if (bDX11WorldFinalcheckTest)
			{
				// Final checkpoint handoff:
				// keep DX11 test rendering active, but let DX9 remain the visible output path.
				// Present(TEST) avoids black-flicker while validating DX11 swapchain liveness.
				__SetDX11RenderCheckpoint("dx11_present_test_finalcheck");
				m_grpDeviceDX11.PresentTest();
			}
			else
			{
				__SetDX11RenderCheckpoint("dx11_present_ui_native");
				if (!m_grpDeviceDX11.Present())
				{
					__SetDX11RenderCheckpoint("dx11_present_ui_native_fail");
					__LogDX11RenderCheckpoint(true);
					return;
				}

				const DWORD dwRenderEndTimeDX11 = ELTimer_GetMSec();
				static DWORD s_dwRenderCheckTimeDX11UINative = dwRenderEndTimeDX11;
				static DWORD s_dwRenderRangeTimeDX11UINative = 0;
				static DWORD s_dwRenderRangeFrameDX11UINative = 0;

				m_dwCurRenderTime = dwRenderEndTimeDX11 - dwRenderStartTime;
				s_dwRenderRangeTimeDX11UINative += m_dwCurRenderTime;
				++s_dwRenderRangeFrameDX11UINative;

				if (dwRenderEndTimeDX11 - s_dwRenderCheckTimeDX11UINative > 1000) [[unlikely]]
				{
					m_fAveRenderTime = float(double(s_dwRenderRangeTimeDX11UINative) / double(s_dwRenderRangeFrameDX11UINative));

					s_dwRenderCheckTimeDX11UINative = ELTimer_GetMSec();
					s_dwRenderRangeTimeDX11UINative = 0;
					s_dwRenderRangeFrameDX11UINative = 0;
				}

				__UpdatePerfAutoAdjustment();
				__SetDX11RenderCheckpoint("runstep_end_dx11_ui_native");
				__LogDX11RenderCheckpoint(false);
				++rRenderFrameCount;
				return;
			}
		}
	}

	CCullingManager::Instance().Update();
	__SetDX11RenderCheckpoint("dx9_begin");

	// DX11-only mode: Begin DX11 frame (bind render targets, clear, set viewport)
#if defined(DX11_STRICT_ONLY)
	if (m_grpDeviceDX11.IsValid())
	{
		if (!m_grpDeviceDX11.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f))  // Clear to black
		{
			__SetDX11RenderCheckpoint("dx11_beginframe_fail");
			__LogDX11RenderCheckpoint(true);
			static DWORD s_dwDX11StrictEarlyBeginFrameFailLogTick = 0;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0 == s_dwDX11StrictEarlyBeginFrameFailLogTick || dwNow - s_dwDX11StrictEarlyBeginFrameFailLogTick >= 2000u)
			{
				s_dwDX11StrictEarlyBeginFrameFailLogTick = dwNow;
				TraceError(
					"DX11_STRICT_EARLY_BEGIN_FRAME_FAIL valid=%d backend=%d frame=%u elapsed_ms=%u",
					m_grpDeviceDX11.IsValid() ? 1 : 0,
					bDX11BackendActive ? 1 : 0,
					m_dwDX11RuntimeCompatFrameCount,
					m_dwDX11RuntimeCompatElapsedMS);
			}
			// Do not abort strict frame here. Let present-path diagnostics run.
		}
		else
		{
			static bool s_bDX11StartupFrameClearStartedLogged = false;
			if (!s_bDX11StartupFrameClearStartedLogged)
			{
				s_bDX11StartupFrameClearStartedLogged = true;
				TraceError(
					"DX11_STARTUP_TIMELINE event=frame_clear_started frame=%u elapsed_ms=%u",
					m_dwDX11RuntimeCompatFrameCount,
					m_dwDX11RuntimeCompatElapsedMS);
			}
		}
	}
#endif

	if (!m_pyGraphic.Begin())
	{
		__SetDX11RenderCheckpoint("dx9_begin_fail");
		__LogDX11RenderCheckpoint(true);
#if defined(DX11_STRICT_ONLY)
		// In strict DX11 mode do not hard-abort when legacy BeginScene path reports failure.
		// Continue into DX11 runtime/present routing so frame visibility is not blocked by DX9 guards.
		if (bDX11BackendActive && m_grpDeviceDX11.IsValid())
		{
			static DWORD s_dwDX11StrictBeginBypassLogTick = 0;
			const DWORD dwNow = ELTimer_GetMSec();
			if (0 == s_dwDX11StrictBeginBypassLogTick || dwNow - s_dwDX11StrictBeginBypassLogTick >= 2000u)
			{
				s_dwDX11StrictBeginBypassLogTick = dwNow;
				TraceError(
					"DX11_STRICT_LEGACY_BEGIN_BYPASS backend=%d valid=%d frame=%u elapsed_ms=%u",
					bDX11BackendActive ? 1 : 0,
					m_grpDeviceDX11.IsValid() ? 1 : 0,
					m_dwDX11RuntimeCompatFrameCount,
					m_dwDX11RuntimeCompatElapsedMS);
			}
		}
		else
		{
			return;
		}
#else
		return;
#endif
	}

	m_pyGraphic.ClearDepthBuffer();

#ifdef _DEBUG
	m_pyGraphic.SetClearColor(0.3f, 0.3f, 0.3f);
	m_pyGraphic.Clear();
#endif

	const bool bDX11NativeVisibleRequested = bDX11NativeVisibleConfigEnabled;
	const bool bDX11NativeVisibleFastRampReady =
		(bDX11CutoverRuntimeMode &&
		 bDX11NativeVisibleConfigEnabled &&
		 m_bDX11WorldNativePass4Mode);
	const bool bDX11NativeVisiblePassRampReady =
		(bDX11StrictNativeOnlyEnabled || m_bDX11WorldNativePass16Mode || bDX11NativeVisibleFastRampReady);
	static bool s_bDX11StrictPassRampBypassLogged = false;
	if (bDX11StrictNativeOnlyEnabled && !s_bDX11StrictPassRampBypassLogged)
	{
		s_bDX11StrictPassRampBypassLogged = true;
		TraceError(
			"DX11_STRICT_PASS_RAMP_BYPASS active=1 pass16=%d pass4=%d",
			m_bDX11WorldNativePass16Mode ? 1 : 0,
			m_bDX11WorldNativePass4Mode ? 1 : 0);
	}
	const bool bDX11NativeVisibleRuntimeReady =
		m_bDX11RuntimeCompatMode &&
		bDX11NativeVisiblePassRampReady;
	const bool bDX11NativeUIMinimalRequested = (bDX11NativeVisibleRequested && bDX11NativeUIMinimalConfigEnabled);
	const bool bDX11NativeWorldMinimalConfigRequested = (bDX11NativeVisibleRequested && bDX11NativeWorldMinimalConfigEnabled);
	// Full DX11 cutover policy:
	// world-native path is always requested when native-visible is requested.
	// Legacy "world minimal" flag is now diagnostic-only and must not block native present.
	const bool bDX11NativeWorldMinimalRequested = bDX11NativeVisibleRequested;
	const bool bDX11NativeWorldForceVisibleRequested =
		(bDX11NativeWorldMinimalRequested && m_pySystem.IsDX11NativeWorldForceVisibleEnabled());
	const bool bDX11NativeWorldAutoPresentRequested =
		(bDX11NativeWorldMinimalRequested && !bDX11NativeWorldForceVisibleRequested);
	bool bDX11NativeVisibleActive = (bDX11NativeVisibleRequested && bDX11NativeVisibleRuntimeReady);

	// Log when Native Visible becomes active (once per session)
	static bool s_bNativeVisibleActivatedLogged = false;
	if (bDX11NativeVisibleActive && !s_bNativeVisibleActivatedLogged)
	{
		TraceError("DX11_NATIVE_VISIBLE_ACTIVATED requested=%d runtime_ready=%d pass4=%d pass16=%d frame=%u elapsed_ms=%u",
			bDX11NativeVisibleRequested ? 1 : 0, bDX11NativeVisibleRuntimeReady ? 1 : 0,
			m_bDX11WorldNativePass4Mode ? 1 : 0, m_bDX11WorldNativePass16Mode ? 1 : 0,
			m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS);
		s_bNativeVisibleActivatedLogged = true;
	}

	bool bDX11NativeUIMinimalActive = (bDX11NativeVisibleActive && bDX11NativeUIMinimalRequested);
	bool bDX11NativeWorldMinimalActive = false;
	bool bDX11NativeWorldMinimalDryRunActive = false;
	bool bDX11NativeWorldMinimalDryRunPausedByReady = false;
	bool bDX11NativeWorldForceVisibleActive = false;
	bool bDX11NativeWorldAutoPresentActive = false;
	bool bDX11NativeWorldShadowActive = false;
	static DWORD s_dwDX11WorldDryRunSuccessCount = 0;
	static DWORD s_dwDX11WorldDryRunFailCount = 0;
	static DWORD s_dwDX11WorldDryRunConsecutiveSuccess = 0;
	static DWORD s_dwDX11WorldDryRunConsecutiveFail = 0;
	static bool s_bDX11WorldDryRunReadyLatched = false;
	static DWORD s_dwDX11WorldDryRunReadySinceFrame = 0;
	static bool s_bDX11WorldNativePendingWasActive = false;
	static DWORD s_dwDX11WorldNativePendingLogTick = 0;
	static DWORD s_dwDX11WorldAutoPresentFailCooldownUntilMS = 0;
	static DWORD s_dwDX11StrictNativePresentBackoffUntilMS = 0;
	static DWORD s_dwDX11StrictNativePresentFailCount = 0;
	static bool s_bDX11BridgeReadyLatched = false;
	static DWORD s_dwDX11BridgeReadySinceFrame = 0;
	static DWORD s_dwDX11BridgeReadyHeartbeatTick = 0;
	if (bDX11NativeVisibleActive && bDX11NativeWorldMinimalRequested)
	{
		// Promote world native render as soon as warmup is stable and runtime is safe.
		// In strict DX11 there is no DX9 bridge image to show while dry-run converges,
		// so draw the real native world immediately and let present gates keep validating.
		const bool bDX11StrictNativeWorldBootstrapVisible = bDX11StrictNativeOnlyEnabled;
		const bool bDX11WorldNativeEligible =
			s_bDX11WorldDryRunReadyLatched ||
			bDX11StrictNativeWorldBootstrapVisible;

		if (bDX11WorldNativeEligible)
		{
			bDX11NativeWorldMinimalActive = true;
			bDX11NativeWorldMinimalDryRunActive = false;
			__ResolveDX11NativeBlockerSubsystem(
				"world_native_visible",
				s_bDX11WorldDryRunReadyLatched ? "world_native_active" : "strict_bootstrap_visible");

			if (bDX11StrictNativeWorldBootstrapVisible && !s_bDX11WorldDryRunReadyLatched)
			{
				static bool s_bDX11StrictWorldBootstrapVisibleLogged = false;
				if (!s_bDX11StrictWorldBootstrapVisibleLogged)
				{
					s_bDX11StrictWorldBootstrapVisibleLogged = true;
					TraceError(
						"DX11_WORLD_NATIVE_BOOTSTRAP visible=1 reason=strict_native_no_bridge frame=%u elapsed_ms=%u",
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}
			}
		}
		else if (!s_bDX11WorldDryRunReadyLatched)
		{
			bDX11NativeWorldMinimalDryRunActive = true;
		}
		else
		{
			bDX11NativeWorldMinimalDryRunPausedByReady = true;
		}

		if (!bDX11NativeWorldMinimalActive)
		{
			const DWORD dwWorldPendingNow = ELTimer_GetMSec();
			if (!s_bDX11WorldNativePendingWasActive ||
				0 == s_dwDX11WorldNativePendingLogTick ||
				dwWorldPendingNow - s_dwDX11WorldNativePendingLogTick >= 30000u)
			{
				s_dwDX11WorldNativePendingLogTick = dwWorldPendingNow;
				const char* c_szWorldPendingDetail = "world_native_pending_unknown";
				if (bDX11NativeWorldMinimalDryRunActive)
					c_szWorldPendingDetail = "warmup_in_progress";
				else if (bDX11NativeWorldMinimalDryRunPausedByReady)
				{
					if (m_bDX11WorldHandoffProbeMode)
						c_szWorldPendingDetail = "handoff_probe_hold";
					else if (m_bDX11RuntimeCompatGraceMode)
						c_szWorldPendingDetail = "runtime_grace_hold";
					else
						c_szWorldPendingDetail = "world_native_gate_hold";
				}
				__LogDX11NativeBlocker("world_native_visible", c_szWorldPendingDetail);
			}
			s_bDX11WorldNativePendingWasActive = true;
		}
		else
		{
			s_bDX11WorldNativePendingWasActive = false;
			__ResolveDX11NativeBlockerSubsystem("world_native_visible", "world_native_active");
		}
	}
	else
	{
		s_bDX11WorldNativePendingWasActive = false;
		const bool bDX11WorldMinimalConfiguredButRuntimeNotReady =
			(bDX11NativeVisibleRequested && bDX11NativeWorldMinimalRequested && !bDX11NativeVisibleActive);
		__ResolveDX11NativeBlockerSubsystem(
			"world_native_visible",
			bDX11WorldMinimalConfiguredButRuntimeNotReady ? "runtime_not_ready" : "world_minimal_inactive");
	}
	if (!bDX11NativeWorldMinimalDryRunActive &&
		!bDX11NativeWorldMinimalDryRunPausedByReady &&
		!bDX11NativeWorldMinimalActive)
	{
		s_dwDX11WorldDryRunConsecutiveSuccess = 0;
		s_dwDX11WorldDryRunConsecutiveFail = 0;
		if (s_bDX11WorldDryRunReadyLatched)
		{
			s_bDX11WorldDryRunReadyLatched = false;
			TraceError(
				"DX11_WORLD_NATIVE_READY state=0 reason=warmup_inactive success_total=%u fail_total=%u frame=%u elapsed_ms=%u",
				s_dwDX11WorldDryRunSuccessCount,
				s_dwDX11WorldDryRunFailCount,
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS);
		}
	}
	const bool bDX11WorldDryRunReady = s_bDX11WorldDryRunReadyLatched;
	const DWORD dwWorldForceNow = ELTimer_GetMSec();
	const bool bDX11WorldAutoPresentCooldownActiveNow =
		(s_dwDX11WorldAutoPresentFailCooldownUntilMS > dwWorldForceNow);
	const bool bDX11StrictNativePresentBackoffActiveNow =
		(bDX11StrictNativeOnlyEnabled && (s_dwDX11StrictNativePresentBackoffUntilMS > dwWorldForceNow));
	const uint32_t dwDX11WorldPortMaskRequired = CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK;
	const uint32_t dwDX11WorldObservedMask = m_grpDeviceDX11.GetNativeWorldObservedMask();
	const uint32_t dwDX11WorldSubmittedMask = m_grpDeviceDX11.GetNativeWorldSubmittedMask();
	const uint32_t dwDX11WorldSubmittedSeenMask = m_grpDeviceDX11.GetNativeWorldSubmittedSeenMask();
	const uint32_t dwDX11WorldApplicableMask = m_grpDeviceDX11.GetNativeWorldApplicableMask();
	const uint32_t dwDX11WorldCommittedMask = m_grpDeviceDX11.GetNativeWorldCommittedMask();
	const bool bDX11NativeWorldApplicableReady =
		(0u != (dwDX11WorldApplicableMask & CGraphicDeviceDX11::WORLD_TERRAIN_DX11));
	uint32_t dwDX11WorldPortRequiredEffectiveMask = (dwDX11WorldPortMaskRequired & dwDX11WorldApplicableMask);
	if (0u == (dwDX11WorldPortRequiredEffectiveMask & CGraphicDeviceDX11::WORLD_TERRAIN_DX11))
		dwDX11WorldPortRequiredEffectiveMask |= CGraphicDeviceDX11::WORLD_TERRAIN_DX11;
	const uint32_t dwDX11WorldSubmittedMissingMask =
		(dwDX11WorldPortRequiredEffectiveMask & ~dwDX11WorldSubmittedSeenMask);
	const uint32_t dwDX11WorldCommittedMissingMask =
		(dwDX11WorldPortRequiredEffectiveMask & ~dwDX11WorldCommittedMask);
	const bool bDX11NativeWorldSubmittedReady = (0u == dwDX11WorldSubmittedMissingMask);
	const bool bDX11NativeWorldCommittedReady = (0u == dwDX11WorldCommittedMissingMask);
	const bool bDX11NativeWorldRendererPorted = bDX11NativeWorldCommittedReady;
	const uint32_t dwDX11WorldPortMissingMask = m_grpDeviceDX11.GetNativeWorldMissingPortMask();
	if (!bDX11NativeWorldCommittedReady)
	{
		static DWORD s_dwDX11WorldPortBlockLogTick = 0;
		if (0 == s_dwDX11WorldPortBlockLogTick || dwWorldForceNow - s_dwDX11WorldPortBlockLogTick >= 30000u)
		{
			s_dwDX11WorldPortBlockLogTick = dwWorldForceNow;
			char szMissingEffective[128];
			char szMissingFull[128];
			DX11FormatWorldPortMask(dwDX11WorldCommittedMissingMask, szMissingEffective, sizeof(szMissingEffective));
			DX11FormatWorldPortMask(dwDX11WorldPortMissingMask, szMissingFull, sizeof(szMissingFull));
			TraceError(
				"DX11_WORLD_PORT_BLOCK observed=0x%02X submitted_frame=0x%02X submitted_seen=0x%02X committed=0x%02X applicable=0x%02X required=0x%02X required_effective=0x%02X missing_effective=%s missing_full=%s",
				static_cast<unsigned int>(dwDX11WorldObservedMask),
				static_cast<unsigned int>(dwDX11WorldSubmittedMask),
				static_cast<unsigned int>(dwDX11WorldSubmittedSeenMask),
				static_cast<unsigned int>(dwDX11WorldCommittedMask),
				static_cast<unsigned int>(dwDX11WorldApplicableMask),
				static_cast<unsigned int>(dwDX11WorldPortMaskRequired),
				static_cast<unsigned int>(dwDX11WorldPortRequiredEffectiveMask),
				szMissingEffective,
				szMissingFull);
		}
	}
	const bool bDX11NativeWorldForceVisibleAllowed =
		bDX11NativeVisibleActive &&
		bDX11NativeWorldMinimalRequested &&
		bDX11NativeWorldMinimalActive &&
		bDX11WorldDryRunReady &&
		bDX11NativeWorldApplicableReady &&
		bDX11NativeWorldSubmittedReady;
	if (bDX11NativeWorldForceVisibleRequested)
	{
		if (bDX11NativeWorldForceVisibleAllowed)
		{
			// Port acceleration path: bypass DX9 frame-copy bridge and present native DX11 world directly.
			// Safety gates keep this mode disabled until runtime/world/bridge are all stable.
			bDX11NativeWorldForceVisibleActive = true;
			__ResolveDX11NativeBlockerSubsystem("world_force_visible", "activated");
		}
		else
		{
			const char* c_szWorldForceBlockedDetail = "blocked_unknown";
			if (!bDX11NativeVisibleActive)
				c_szWorldForceBlockedDetail = "runtime_not_ready";
			else if (!bDX11WorldDryRunReady)
				c_szWorldForceBlockedDetail = "world_not_ready";
			else if (!bDX11NativeWorldApplicableReady)
				c_szWorldForceBlockedDetail = "applicable_mask_incomplete";
			else if (!bDX11NativeWorldSubmittedReady)
				c_szWorldForceBlockedDetail = "submitted_mask_incomplete";
			else if (m_bDX11WorldHandoffProbeMode)
				c_szWorldForceBlockedDetail = "handoff_probe_active";
			else if (m_bDX11RuntimeCompatGraceMode)
				c_szWorldForceBlockedDetail = "runtime_grace_active";
			else if (bDX11WorldAutoPresentCooldownActiveNow)
				c_szWorldForceBlockedDetail = "present_cooldown_active";
			__LogDX11NativeBlocker("world_force_visible", c_szWorldForceBlockedDetail);
		}
	}
	else
	{
		__ResolveDX11NativeBlockerSubsystem("world_force_visible", "config_off");
	}
	const DWORD kDX11NativePresentConfidenceFrames =
		bDX11StrictNativeOnlyEnabled ? 8u : 300u;
	static bool s_bDX11NativeConfidenceThresholdLogged = false;
	if (!s_bDX11NativeConfidenceThresholdLogged)
	{
		s_bDX11NativeConfidenceThresholdLogged = true;
		TraceError(
			"DX11_NATIVE_PRESENT_CONFIDENCE threshold=%u strict=%d",
			static_cast<unsigned int>(kDX11NativePresentConfidenceFrames),
			bDX11StrictNativeOnlyEnabled ? 1 : 0);
	}
	static DWORD s_dwDX11NativePresentConfidenceStableFrames = 0;
	if (bDX11NativeVisibleActive &&
		bDX11NativeWorldMinimalRequested &&
		bDX11NativeWorldMinimalActive &&
		bDX11WorldDryRunReady &&
		bDX11NativeWorldApplicableReady &&
		bDX11NativeWorldSubmittedReady &&
		bDX11NativeWorldCommittedReady &&
		!bDX11WorldAutoPresentCooldownActiveNow &&
		!bDX11StrictNativePresentBackoffActiveNow)
	{
		if (s_dwDX11NativePresentConfidenceStableFrames < 0xffffffffu)
			++s_dwDX11NativePresentConfidenceStableFrames;
	}
	else
	{
		s_dwDX11NativePresentConfidenceStableFrames = 0;
	}
	const bool bDX11NativePresentConfidenceReady =
		(s_dwDX11NativePresentConfidenceStableFrames >= kDX11NativePresentConfidenceFrames);

	const bool bDX11NativeWorldAutoPresentAllowed =
		bDX11NativeVisibleActive &&
		bDX11NativeWorldMinimalRequested &&
		bDX11NativeWorldMinimalActive &&
		bDX11WorldDryRunReady &&
		bDX11NativeWorldAutoGateConfigEnabled &&
		bDX11NativeWorldApplicableReady &&
		bDX11NativeWorldSubmittedReady &&
		bDX11NativeWorldCommittedReady &&
		bDX11NativePresentConfidenceReady &&
		!bDX11StrictNativePresentBackoffActiveNow;
	auto __LogDX11NativePresentGate = [&](const char* c_szReason)
	{
		static char s_szDX11NativePresentGateReason[64] = { 0 };
		static DWORD s_dwDX11NativePresentGateLogTick = 0;
		const char* c_szSafeReason = (c_szReason && c_szReason[0]) ? c_szReason : "unknown";
		const bool bReasonChanged = (0 != strcmp(s_szDX11NativePresentGateReason, c_szSafeReason));
		if (bReasonChanged || 0 == s_dwDX11NativePresentGateLogTick || dwWorldForceNow - s_dwDX11NativePresentGateLogTick >= 30000u)
		{
			s_dwDX11NativePresentGateLogTick = dwWorldForceNow;
			strcpy_s(s_szDX11NativePresentGateReason, sizeof(s_szDX11NativePresentGateReason), c_szSafeReason);
			TraceError(
				"DX11_NATIVE_PRESENT_GATE reason=%s requested=%d force=%d auto_gate=%d applicable_ready=%d submitted_ready=%d committed_ready=%d confidence_ready=%d confidence_frames=%u confidence_threshold=%u cooldown=%d strict_backoff=%d bridge_ready=%d frame=%u elapsed_ms=%u",
				c_szSafeReason,
				bDX11NativeWorldAutoPresentRequested ? 1 : 0,
				bDX11NativeWorldForceVisibleRequested ? 1 : 0,
				bDX11NativeWorldAutoGateConfigEnabled ? 1 : 0,
				bDX11NativeWorldApplicableReady ? 1 : 0,
				bDX11NativeWorldSubmittedReady ? 1 : 0,
				bDX11NativeWorldCommittedReady ? 1 : 0,
				bDX11NativePresentConfidenceReady ? 1 : 0,
				static_cast<unsigned int>(s_dwDX11NativePresentConfidenceStableFrames),
				static_cast<unsigned int>(kDX11NativePresentConfidenceFrames),
				bDX11WorldAutoPresentCooldownActiveNow ? 1 : 0,
				bDX11StrictNativePresentBackoffActiveNow ? 1 : 0,
				s_bDX11BridgeReadyLatched ? 1 : 0,
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS);
		}
	};
	if (!bDX11NativeVisibleActive)
	{
		__ResolveDX11NativeBlockerSubsystem("world_native_present", "runtime_not_ready");
		__LogDX11NativePresentGate("runtime_not_ready");
	}
	else if (bDX11NativeWorldAutoPresentRequested && !bDX11NativeWorldForceVisibleActive)
	{
		if (bDX11NativeWorldAutoPresentAllowed)
		{
			bDX11NativeWorldAutoPresentActive = true;
			__ResolveDX11NativeBlockerSubsystem("world_native_present", "activated");
			__LogDX11NativePresentGate("activated");

			// Log auto-present activation (once per session)
			static bool s_bAutoPresentActivatedLogged = false;
			if (!s_bAutoPresentActivatedLogged)
			{
				TraceError("DX11_AUTO_PRESENT_ACTIVATED world_minimal_active=%d dry_run_ready=%d autogate_enabled=%d frame=%u elapsed_ms=%u",
					bDX11NativeWorldMinimalActive ? 1 : 0, bDX11WorldDryRunReady ? 1 : 0,
					bDX11NativeWorldAutoGateConfigEnabled ? 1 : 0,
					m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS);
				s_bAutoPresentActivatedLogged = true;
			}
		}
		else
		{
			const char* c_szWorldAutoPresentBlockedDetail = "blocked_unknown";
			if (!bDX11NativeWorldMinimalActive)
			{
				if (bDX11NativeWorldMinimalDryRunActive)
					c_szWorldAutoPresentBlockedDetail = "world_warmup_active";
				else if (bDX11NativeWorldMinimalDryRunPausedByReady)
					c_szWorldAutoPresentBlockedDetail = "world_gate_pending";
				else
					c_szWorldAutoPresentBlockedDetail = "world_not_active";
			}
			else if (bDX11StrictNativePresentBackoffActiveNow)
				c_szWorldAutoPresentBlockedDetail = "strict_native_present_backoff";
			else if (bDX11StrictNativeOnlyEnabled &&
				(!bDX11NativeWorldApplicableReady || !bDX11NativeWorldSubmittedReady || !bDX11NativeWorldCommittedReady))
			{
				c_szWorldAutoPresentBlockedDetail = "strict_native_only_blocked_by_mask";
			}
			else if (!bDX11WorldDryRunReady)
				c_szWorldAutoPresentBlockedDetail = "world_not_ready";
			else if (!bDX11NativeWorldAutoGateConfigEnabled)
				c_szWorldAutoPresentBlockedDetail = bDX11TerrainStabilizationMode ? "terrain_stabilization_mode" : "auto_gate_disabled";
			else if (!bDX11NativeWorldApplicableReady)
				c_szWorldAutoPresentBlockedDetail = "applicable_mask_incomplete";
			else if (!bDX11NativeWorldSubmittedReady)
				c_szWorldAutoPresentBlockedDetail = "submitted_mask_incomplete";
			else if (!bDX11NativeWorldCommittedReady)
				c_szWorldAutoPresentBlockedDetail = "committed_mask_incomplete";
			else if (!bDX11NativePresentConfidenceReady)
				c_szWorldAutoPresentBlockedDetail = "confidence_window";
			else if (m_bDX11WorldHandoffProbeMode)
				c_szWorldAutoPresentBlockedDetail = "handoff_probe_active";
			else if (m_bDX11RuntimeCompatGraceMode)
				c_szWorldAutoPresentBlockedDetail = "runtime_grace_active";
			else if (bDX11WorldAutoPresentCooldownActiveNow)
				c_szWorldAutoPresentBlockedDetail = "cooldown_active";
			const bool bConfigGateOnlyBlocker =
				(0 == strcmp(c_szWorldAutoPresentBlockedDetail, "auto_gate_disabled")) ||
				(0 == strcmp(c_szWorldAutoPresentBlockedDetail, "terrain_stabilization_mode"));
			if (!bConfigGateOnlyBlocker)
				__LogDX11NativeBlocker("world_native_present", c_szWorldAutoPresentBlockedDetail);
			__LogDX11NativePresentGate(c_szWorldAutoPresentBlockedDetail);
		}
	}
	else
	{
		__ResolveDX11NativeBlockerSubsystem(
			"world_native_present",
			bDX11NativeWorldForceVisibleRequested ? "force_visible_override" : "inactive");
		__LogDX11NativePresentGate(bDX11NativeWorldForceVisibleRequested ? "force_visible_override" : "inactive");
	}
	// Render-first policy: shadows are optional and never gate native world rendering.
	bDX11NativeWorldShadowActive = false;

	// Minimal DX11 UI path currently draws only the hardware cursor overlay.
	// Full UI port is still pending; by default visible output still uses DX9 bridge copy
	// unless world-force-visible native present is explicitly requested and all gates are stable.
	const bool bDX11NativeCursorOverlayTestActive =
		(bDX11UINativeTest && bDX11NativeVisibleRequested && bDX11NativeVisibleRuntimeReady);
	const bool bDX11NativeCursorOverlayActive =
		(bDX11NativeUIMinimalActive || bDX11NativeCursorOverlayTestActive);
	long lDX11NativeCursorX = 0;
	long lDX11NativeCursorY = 0;
	if (bDX11NativeCursorOverlayActive)
		m_kWndMgr.GetMousePosition(lDX11NativeCursorX, lDX11NativeCursorY);

	if (bDX11BackendActive)
	{
		m_grpDeviceDX11.BindMainRenderTargets();
		if (ID3D11DeviceContext* pDX11Context = m_grpDeviceDX11.GetContext())
		{
			ID3D11ShaderResourceView* apNullSRV[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			ID3D11SamplerState* apNullSampler[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			ID3D11Buffer* apNullCB[4] = { nullptr, nullptr, nullptr, nullptr };
			ID3D11Buffer* apNullVB[1] = { nullptr };
			const FLOAT afBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
			UINT uZero = 0;

			// Ensure world-pass shaders/IA state cannot leak into UI phase on DX11 frames.
			pDX11Context->VSSetShader(nullptr, nullptr, 0);
			pDX11Context->PSSetShader(nullptr, nullptr, 0);
			pDX11Context->GSSetShader(nullptr, nullptr, 0);
			pDX11Context->HSSetShader(nullptr, nullptr, 0);
			pDX11Context->DSSetShader(nullptr, nullptr, 0);
			pDX11Context->CSSetShader(nullptr, nullptr, 0);
			pDX11Context->IASetInputLayout(nullptr);
			pDX11Context->IASetVertexBuffers(0, 1, apNullVB, &uZero, &uZero);
			pDX11Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
			pDX11Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
			pDX11Context->PSSetShaderResources(0, 8, apNullSRV);
			pDX11Context->VSSetShaderResources(0, 8, apNullSRV);
			pDX11Context->PSSetSamplers(0, 8, apNullSampler);
			pDX11Context->VSSetSamplers(0, 8, apNullSampler);
			pDX11Context->VSSetConstantBuffers(0, 4, apNullCB);
			pDX11Context->PSSetConstantBuffers(0, 4, apNullCB);
			pDX11Context->OMSetBlendState(nullptr, afBlendFactor, 0xFFFFFFFFu);
			pDX11Context->OMSetDepthStencilState(nullptr, 0u);
			pDX11Context->RSSetState(nullptr);
		}
	}

#if defined(DX11_STRICT_ONLY)
	// DX11_STRICT_ONLY: BeginFrame() must be called BEFORE OnUIRender()
	// Otherwise UI sprites are drawn and then erased by BeginFrame's clear operation
	if (bDX11BackendActive && m_grpDeviceDX11.IsValid())
	{
		if (!m_grpDeviceDX11.BeginFrame(0.02f, 0.02f, 0.02f, 1.0f))
		{
			// Do not abort the frame here. In strict mode this would block all present paths
			// and result in a persistent black screen with still-active UI hit detection.
			static DWORD s_dwDX11StrictBeginFrameFailLogTick = 0;
			const DWORD dwBeginFailNow = ELTimer_GetMSec();
			if (0 == s_dwDX11StrictBeginFrameFailLogTick || dwBeginFailNow - s_dwDX11StrictBeginFrameFailLogTick >= 2000u)
			{
				s_dwDX11StrictBeginFrameFailLogTick = dwBeginFailNow;
				TraceError(
					"DX11_STRICT_BEGIN_FRAME_FAIL valid=%d backend=%d native_visible_cfg=%d strict=%d frame=%u elapsed_ms=%u",
					m_grpDeviceDX11.IsValid() ? 1 : 0,
					bDX11BackendActive ? 1 : 0,
					bDX11NativeVisibleRequested ? 1 : 0,
					bDX11StrictNativeOnlyEnabled ? 1 : 0,
					m_dwDX11RuntimeCompatFrameCount,
					m_dwDX11RuntimeCompatElapsedMS);
			}
		}
	}
#endif

	m_pyGraphic.SetInterfaceRenderState();
	auto __LogDX11StartupFirstUISubmit = [&]()
	{
		static bool s_bDX11StartupFirstUISubmitLogged = false;
		if (!s_bDX11StartupFirstUISubmitLogged)
		{
			s_bDX11StartupFirstUISubmitLogged = true;
			TraceError(
				"DX11_STARTUP_TIMELINE event=first_ui_submit frame=%u elapsed_ms=%u",
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS);
		}
	};
	OnUIRender();
	__LogDX11StartupFirstUISubmit();
	if (!bDX11NativeCursorOverlayActive)
		OnMouseRender();

	// M3-TEXTTAIL-PARITY-25: Render pass order verification
	// Log checkpoint after UI rendering completes (including texttails)
	{
		static DWORD s_dwUIPassCompleteLogTick = 0;
		static bool s_bUIPassCompleteInitialLogged = false;
		const DWORD dwUIPassNow = ELTimer_GetMSec();

		if (!s_bUIPassCompleteInitialLogged)
		{
			s_bUIPassCompleteInitialLogged = true;
			s_dwUIPassCompleteLogTick = dwUIPassNow;
			TraceError("DX11_TEXTTAIL_PASS_ORDER stage=ui_complete texttails_rendered=1");
		}
		else if (dwUIPassNow - s_dwUIPassCompleteLogTick >= 30000u)
		{
			s_dwUIPassCompleteLogTick = dwUIPassNow;
			TraceError("DX11_TEXTTAIL_PASS_ORDER stage=ui_heartbeat render_sequence_valid=1");
		}
	}

	if (bDX11BackendActive)
	{
		if (ID3D11DeviceContext* pDX11Context = m_grpDeviceDX11.GetContext())
		{
			ID3D11ShaderResourceView* apNullSRV[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			ID3D11SamplerState* apNullSampler[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			ID3D11Buffer* apNullCB[4] = { nullptr, nullptr, nullptr, nullptr };
			ID3D11Buffer* apNullVB[1] = { nullptr };
			ID3D11ClassInstance* apNullClassInstances[1] = { nullptr };
			const FLOAT afBlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
			UINT uZero = 0u;

			// UI pass hygiene: keep next frame deterministic by clearing UI-bound pipeline state.
			pDX11Context->VSSetShader(nullptr, apNullClassInstances, 0);
			pDX11Context->PSSetShader(nullptr, apNullClassInstances, 0);
			pDX11Context->GSSetShader(nullptr, apNullClassInstances, 0);
			pDX11Context->HSSetShader(nullptr, apNullClassInstances, 0);
			pDX11Context->DSSetShader(nullptr, apNullClassInstances, 0);
			pDX11Context->CSSetShader(nullptr, apNullClassInstances, 0);
			pDX11Context->IASetInputLayout(nullptr);
			pDX11Context->IASetVertexBuffers(0, 1, apNullVB, &uZero, &uZero);
			pDX11Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
			pDX11Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
			pDX11Context->PSSetShaderResources(0, 8, apNullSRV);
			pDX11Context->VSSetShaderResources(0, 8, apNullSRV);
			pDX11Context->PSSetSamplers(0, 8, apNullSampler);
			pDX11Context->VSSetSamplers(0, 8, apNullSampler);
			pDX11Context->VSSetConstantBuffers(0, 4, apNullCB);
			pDX11Context->PSSetConstantBuffers(0, 4, apNullCB);
			pDX11Context->OMSetBlendState(nullptr, afBlendFactor, 0xFFFFFFFFu);
			pDX11Context->OMSetDepthStencilState(nullptr, 0u);
			pDX11Context->RSSetState(nullptr);
		}
	}

	m_pyGraphic.End();

	const char* c_szDX11VisiblePath =
		bDX11StrictNativeOnlyEnabled
			? "dx11_native_pending"
			: "dx9_compat";
	const char* c_szDX11VisibleReason = "native_visible_disabled";
	if (bDX11NativeVisibleRequested)
	{
		if (bDX11NativeVisibleRuntimeReady)
		{
			c_szDX11VisiblePath = "dx11_native";
			c_szDX11VisibleReason = m_bDX11WorldHandoffProbeMode ? "runtime_ready_probe" : "runtime_ready";
			if (bDX11NativeWorldMinimalRequested)
			{
				if (bDX11NativeWorldMinimalActive)
				{
					if (bDX11NativeWorldRendererPorted)
						c_szDX11VisibleReason = "runtime_ready_world_native";
					else
						c_szDX11VisibleReason = "runtime_ready_world_bridge";
				}
				else if (bDX11NativeWorldMinimalDryRunActive)
					c_szDX11VisibleReason = "runtime_ready_world_warmup";
				else if (bDX11WorldDryRunReady)
					c_szDX11VisibleReason = "runtime_ready_world_pending";
				else
					c_szDX11VisibleReason = "runtime_ready_world_bootstrap";
			}
			if (bDX11NativeWorldForceVisibleActive)
				c_szDX11VisibleReason = "runtime_ready_world_force_visible";
			else if (bDX11NativeWorldAutoPresentActive)
				c_szDX11VisibleReason =
					bDX11NativeWorldShadowActive
						? "runtime_ready_world_shadow"
						: "runtime_ready_world_present";
		}
		else if (!m_bDX11RuntimeCompatMode)
			c_szDX11VisibleReason = "runtime_compat_inactive";
		else if (!bDX11NativeVisiblePassRampReady)
			c_szDX11VisibleReason = "native_pass_ramp_inactive";
		else if (m_bDX11WorldHandoffProbeMode)
			c_szDX11VisibleReason = "handoff_probe_active";
		else if (m_bDX11RuntimeCompatGraceMode)
			c_szDX11VisibleReason = "runtime_grace_active";
		else
			c_szDX11VisibleReason = "runtime_pending";
	}
	if (bDX11BackendActive && bDX11NativeVisibleRequested && bDX11NativeVisibleRuntimeReady)
		__ResolveDX11NativeBlockerSubsystem("runtime_gate", "runtime_ready");

	static std::string s_stDX11LastVisiblePath;
	static std::string s_stDX11LastVisibleReason;
	static DWORD s_dwDX11VisiblePathLogTick = 0;
	const DWORD dwVisibleNow = ELTimer_GetMSec();
	const bool bDX11VisiblePathChanged =
		(s_stDX11LastVisiblePath != c_szDX11VisiblePath) ||
		(s_stDX11LastVisibleReason != c_szDX11VisibleReason);
	if (bDX11BackendActive &&
		(bDX11VisiblePathChanged || 0 == s_dwDX11VisiblePathLogTick || dwVisibleNow - s_dwDX11VisiblePathLogTick >= DX11_NATIVE_HEARTBEAT_INTERVAL_MS))
	{
		s_stDX11LastVisiblePath = c_szDX11VisiblePath;
		s_stDX11LastVisibleReason = c_szDX11VisibleReason;
		s_dwDX11VisiblePathLogTick = dwVisibleNow;
		TraceError(
			"DX11_VISIBLE_PATH path=%s reason=%s native_visible_cfg=%d runtime_compat=%d pass16=%d handoff_probe=%d grace=%d frame=%u elapsed_ms=%u",
			c_szDX11VisiblePath,
			c_szDX11VisibleReason,
			bDX11NativeVisibleRequested ? 1 : 0,
			m_bDX11RuntimeCompatMode ? 1 : 0,
			m_bDX11WorldNativePass16Mode ? 1 : 0,
			m_bDX11WorldHandoffProbeMode ? 1 : 0,
			m_bDX11RuntimeCompatGraceMode ? 1 : 0,
			m_dwDX11RuntimeCompatFrameCount,
			m_dwDX11RuntimeCompatElapsedMS);
	}
	static DWORD s_dwDX11NativeModeLogTick = 0;
	const DWORD dwNativeModeMask =
		(bDX11NativeVisibleRequested ? 1u : 0u) |
		(bDX11NativeUIMinimalRequested ? 2u : 0u) |
		(bDX11NativeWorldMinimalConfigRequested ? 4u : 0u) |
		(bDX11NativeUIMinimalActive ? 8u : 0u) |
		(bDX11NativeWorldMinimalActive ? 16u : 0u) |
		(bDX11NativeWorldMinimalDryRunActive ? 32u : 0u) |
		(bDX11WorldDryRunReady ? 64u : 0u) |
		(bDX11NativeWorldForceVisibleRequested ? 128u : 0u) |
		(bDX11NativeWorldForceVisibleActive ? 256u : 0u) |
		(bDX11NativeWorldMinimalDryRunPausedByReady ? 512u : 0u) |
		(bDX11NativeWorldMinimalActive ? 1024u : 0u) |
		(bDX11NativeWorldAutoPresentActive ? 2048u : 0u);
	static DWORD s_dwDX11LastNativeModeMask = 0xffffffffu;
	if (bDX11BackendActive &&
		(dwNativeModeMask != s_dwDX11LastNativeModeMask ||
		 0 == s_dwDX11NativeModeLogTick ||
		 dwVisibleNow - s_dwDX11NativeModeLogTick >= DX11_NATIVE_HEARTBEAT_INTERVAL_MS))
	{
		s_dwDX11LastNativeModeMask = dwNativeModeMask;
		s_dwDX11NativeModeLogTick = dwVisibleNow;
		TraceError(
			"DX11_NATIVE_MODE cfg_visible=%d cfg_ui_minimal=%d cfg_world_minimal=%d cfg_world_force_visible=%d ui_test_suppressed=%d active_visible=%d active_ui=%d active_world=%d world_warmup=%d world_warmup_paused=%d world_ready=%d world_force_visible=%d world_native_present=%d world_shadow=%d",
			bDX11NativeVisibleRequested ? 1 : 0,
			bDX11NativeUIMinimalRequested ? 1 : 0,
			bDX11NativeWorldMinimalConfigRequested ? 1 : 0,
			bDX11NativeWorldForceVisibleRequested ? 1 : 0,
			(bDX11UINativeTestSuppressedByNativeVisible || (bDX11ForceDisableUINativeTests && bDX11UINativeConfigEnabledRaw)) ? 1 : 0,
			bDX11NativeVisibleActive ? 1 : 0,
			bDX11NativeUIMinimalActive ? 1 : 0,
			bDX11NativeWorldMinimalActive ? 1 : 0,
			bDX11NativeWorldMinimalDryRunActive ? 1 : 0,
			bDX11NativeWorldMinimalDryRunPausedByReady ? 1 : 0,
			bDX11WorldDryRunReady ? 1 : 0,
			bDX11NativeWorldForceVisibleActive ? 1 : 0,
			bDX11NativeWorldAutoPresentActive ? 1 : 0,
			bDX11NativeWorldShadowActive ? 1 : 0);
	}

	static DWORD s_dwDX11NativePresentLastSuccessTick = 0;
	static DWORD s_dwDX11NativePresentWatchdogLogTick = 0;
	static DWORD s_dwDX11NativePresentWatchdogStallCount = 0;
	static DWORD s_dwDX11NativePresentWatchdogRecoverCount = 0;
	static DWORD s_dwDX11LoginUIPresentLogTick = 0;

	const bool bDX11MapReadyForNativeWorld = m_pyBackground.IsMapReady();
	const bool bDX11MainInstanceReadyForNativeWorld = bDX11RuntimeCompatHasMainInstance;
	const bool bDX11LoginUIOnlyPresentMode =
		bDX11BackendActive &&
		bDX11StrictNativeOnlyEnabled &&
		bDX11NativeVisibleRequested &&
		(!bDX11MapReadyForNativeWorld || !bDX11MainInstanceReadyForNativeWorld);
	if (bDX11LoginUIOnlyPresentMode)
	{
		// Strict DX11 login/select phases: do not run world warmup/native-world state machine.
		// Present only the UI frame until map content is ready.
		bDX11NativeVisibleActive = false;
		bDX11NativeWorldMinimalDryRunActive = false;
		bDX11NativeWorldMinimalDryRunPausedByReady = false;
		bDX11NativeWorldMinimalActive = false;
		bDX11NativeWorldAutoPresentActive = false;
		bDX11NativeWorldForceVisibleActive = false;

		const DWORD dwLoginUIPresentNow = ELTimer_GetMSec();
		if (0 == s_dwDX11LoginUIPresentLogTick || dwLoginUIPresentNow - s_dwDX11LoginUIPresentLogTick >= 2000u)
		{
			s_dwDX11LoginUIPresentLogTick = dwLoginUIPresentNow;
			const char* c_szLoginUIOnlyReason = "map_not_ready";
			if (!bDX11MapReadyForNativeWorld && !bDX11MainInstanceReadyForNativeWorld)
				c_szLoginUIOnlyReason = "map_not_ready+main_instance_missing";
			else if (!bDX11MainInstanceReadyForNativeWorld)
				c_szLoginUIOnlyReason = "main_instance_missing";
			TraceError(
				"DX11_LOGIN_UI_ONLY_PRESENT active=1 reason=%s map_ready=%d main_instance_ready=%d frame=%u elapsed_ms=%u",
				c_szLoginUIOnlyReason,
				bDX11MapReadyForNativeWorld ? 1 : 0,
				bDX11MainInstanceReadyForNativeWorld ? 1 : 0,
				m_dwDX11RuntimeCompatFrameCount,
				m_dwDX11RuntimeCompatElapsedMS);
		}
	}

	if (bDX11NativeVisibleActive)
	{
		__ResolveDX11NativeBlockerSubsystem("dx9_device", "strict_dx11_no_bridge");

			// Native-visible runtime path must start from a fresh DX11 frame each tick.
			// Without guaranteed clear/begin here, stale backbuffer contents can persist
			// and manifest as cursor trails or apparent frozen image.
			// NOTE: In DX11_STRICT_ONLY mode, BeginFrame() is already called BEFORE OnUIRender() (line ~4128)
			__SetDX11RenderCheckpoint("dx11_native_begin_frame");
#if defined(DX11_STRICT_ONLY)
			// BeginFrame already called before OnUIRender - skip duplicate call
			bool bBeginFrameOK = true;
#else
			bool bBeginFrameOK = m_grpDeviceDX11.BeginFrame(0.02f, 0.02f, 0.02f, 1.0f);
#endif
			if (!bBeginFrameOK)
			{
				__LogDX11NativeBlocker("dx11_begin_frame", "begin_failed");
				bDX11NativeVisibleActive = false;
			}
			else
			{
				// Frame timing diagnostic: Mark BeginFrame/Clear completion
				static DWORD s_dwBeginFrameClearLogTick = 0;
				const DWORD dwBeginNow = ELTimer_GetMSec();
				if (0 == s_dwBeginFrameClearLogTick || dwBeginNow - s_dwBeginFrameClearLogTick >= 5000u)
				{
					s_dwBeginFrameClearLogTick = dwBeginNow;
					TraceError("DX11_BEGIN_FRAME_CLEAR frame=%u elapsed_ms=%u", m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS);
				}
			}
			const bool bDX11NativeWorldRenderPathActive =
				bDX11NativeWorldMinimalDryRunActive ||
				bDX11NativeWorldMinimalActive ||
				bDX11NativeWorldAutoPresentActive ||
				bDX11NativeWorldForceVisibleActive;
			if (bDX11NativeWorldRenderPathActive)
			{
				int iRenderedPatchCount = 0;
				int iRenderedSplatCount = 0;
				float fRenderedSplatRatio = 0.0f;
				const std::vector<int>& rkRenderedTextureNumVector =
					m_pyBackground.GetRenderedSplatNum(&iRenderedPatchCount, &iRenderedSplatCount, &fRenderedSplatRatio);
				const int iRenderedTextureCount = static_cast<int>(rkRenderedTextureNumVector.size());
				DWORD dwRenderedGraphicThingInstanceNum = 0;
				DWORD dwRenderedCRCNum = 0;
				m_pyBackground.GetRenderedGraphicThingInstanceNum(&dwRenderedGraphicThingInstanceNum, &dwRenderedCRCNum);
				const bool bDX11TerrainPilotStatsValid = (iRenderedPatchCount > 0);

				int iTerrainTiles = 120 + static_cast<int>(m_dwPerfMapMS * 6) + static_cast<int>(m_dwPerfVisibleTextTails / 4);
				if (bDX11TerrainPilotStatsValid)
				{
					// Stage 1 terrain-native pilot: drive DX11 dry-run load from real map render stats.
					iTerrainTiles = iRenderedPatchCount;
					iTerrainTiles += iRenderedTextureCount * 2;
					iTerrainTiles += std::min(iRenderedSplatCount / 12, 96);
					if (fRenderedSplatRatio > 4.0f)
						iTerrainTiles += std::min(static_cast<int>((fRenderedSplatRatio - 4.0f) * 3.0f), 64);
				}
				if (iTerrainTiles < 80)
					iTerrainTiles = 80;
				else if (iTerrainTiles > 320)
					iTerrainTiles = 320;

				int iActorCount = static_cast<int>(dwRenderedGraphicThingInstanceNum);
				if (iActorCount > 0)
					iActorCount += static_cast<int>(m_dwPerfCharacterMS * 2);
				else
					iActorCount = 48 + static_cast<int>(m_dwPerfCharacterMS * 4) + static_cast<int>(m_dwPerfVisibleTextTails / 2);
				if (iActorCount < 40)
					iActorCount = 40;
				else if (iActorCount > 300)
					iActorCount = 300;

				int iFXCount = 24 + static_cast<int>(m_dwPerfEffectRenderMS * 6) + static_cast<int>(m_dwPerfActiveEffects / 2);
				if (iFXCount < 20)
					iFXCount = 20;
				else if (iFXCount > 260)
					iFXCount = 260;

				const DWORD kDX11WorldTerrainOnlyWarmupSuccessThreshold =
					bDX11StrictNativeOnlyEnabled ? 24u : 60u;
				const DWORD kDX11WorldTerrainActorWarmupSuccessThreshold =
					bDX11StrictNativeOnlyEnabled ? 48u : 120u;
				static bool s_bDX11WorldWarmupThresholdsLogged = false;
				if (!s_bDX11WorldWarmupThresholdsLogged)
				{
					s_bDX11WorldWarmupThresholdsLogged = true;
					TraceError(
						"DX11_WORLD_WARMUP_THRESHOLDS terrain_only=%u terrain_actor=%u strict=%d",
						static_cast<unsigned int>(kDX11WorldTerrainOnlyWarmupSuccessThreshold),
						static_cast<unsigned int>(kDX11WorldTerrainActorWarmupSuccessThreshold),
						bDX11StrictNativeOnlyEnabled ? 1 : 0);
				}
				int iDX11WorldPilotPhase = 1;
				if (s_dwDX11WorldDryRunConsecutiveSuccess >= kDX11WorldTerrainActorWarmupSuccessThreshold)
					iDX11WorldPilotPhase = 3;
				else if (s_dwDX11WorldDryRunConsecutiveSuccess >= kDX11WorldTerrainOnlyWarmupSuccessThreshold)
					iDX11WorldPilotPhase = 2;
				// Once native world render is committed, force full pass set immediately.
				// Warmup phasing is only useful before commit.
				if (bDX11NativeWorldMinimalActive || bDX11NativeWorldAutoPresentActive || bDX11NativeWorldForceVisibleActive)
					iDX11WorldPilotPhase = 3;

				const char* c_szDX11WorldPilotPhase = "terrain_only";
				if (2 == iDX11WorldPilotPhase)
					c_szDX11WorldPilotPhase = "terrain_actor";
				else if (3 == iDX11WorldPilotPhase)
					c_szDX11WorldPilotPhase = "terrain_actor_fx";
				const int iTerrainDrawCount = iTerrainTiles;
				const int iActorDrawCount = iActorCount;
				const int iFXDrawCount = iFXCount;

				static int s_iDX11WorldPilotPhase = -1;
				if (s_iDX11WorldPilotPhase != iDX11WorldPilotPhase)
				{
					s_iDX11WorldPilotPhase = iDX11WorldPilotPhase;
					TraceError(
						"DX11_WORLD_NATIVE_PILOT_PHASE phase=%s threshold1=%u threshold2=%u success_streak=%u frame=%u elapsed_ms=%u",
						c_szDX11WorldPilotPhase,
						kDX11WorldTerrainOnlyWarmupSuccessThreshold,
						kDX11WorldTerrainActorWarmupSuccessThreshold,
						s_dwDX11WorldDryRunConsecutiveSuccess,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}

				static DWORD s_dwDX11WorldTerrainPilotLogTick = 0;
				if (0 == s_dwDX11WorldTerrainPilotLogTick || dwVisibleNow - s_dwDX11WorldTerrainPilotLogTick >= DX11_WORLD_HEARTBEAT_INTERVAL_MS)
				{
					s_dwDX11WorldTerrainPilotLogTick = dwVisibleNow;
					TraceError(
						"DX11_WORLD_NATIVE_TERRAIN_PILOT stats_valid=%d phase=%s terrain_only=%d patch=%d splat=%d splat_ratio=%.3f textures=%d instances=%u crc=%u terrain_draw=%d actors_draw=%d fx_draw=%d frame=%u elapsed_ms=%u",
						bDX11TerrainPilotStatsValid ? 1 : 0,
						c_szDX11WorldPilotPhase,
						(1 == iDX11WorldPilotPhase) ? 1 : 0,
						iRenderedPatchCount,
						iRenderedSplatCount,
						fRenderedSplatRatio,
						iRenderedTextureCount,
						dwRenderedGraphicThingInstanceNum,
						dwRenderedCRCNum,
						iTerrainDrawCount,
						iActorDrawCount,
						iFXDrawCount,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}
				m_grpDeviceDX11.SetNativeWorldSceneStats(
					iRenderedPatchCount,
					iRenderedSplatCount,
					fRenderedSplatRatio,
					iRenderedTextureCount,
					dwRenderedGraphicThingInstanceNum,
					dwRenderedCRCNum);

				const bool bDX11WorldShadowPassActive = bDX11NativeWorldShadowActive;
				const uint32_t dwDX11WorldRequiredMask = CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK;
				uint32_t dwDX11WorldRequiredEffectiveMaskPreRender =
					(dwDX11WorldRequiredMask & m_grpDeviceDX11.GetNativeWorldApplicableMask());
				if (0u == (dwDX11WorldRequiredEffectiveMaskPreRender & CGraphicDeviceDX11::WORLD_TERRAIN_DX11))
					dwDX11WorldRequiredEffectiveMaskPreRender |= CGraphicDeviceDX11::WORLD_TERRAIN_DX11;
				const bool bDX11WorldRendererPortedPreRender =
					(0u == (dwDX11WorldRequiredEffectiveMaskPreRender & ~m_grpDeviceDX11.GetNativeWorldCommittedMask()));
				const bool bDX11WorldNativeDrawCommitted =
					bDX11NativeWorldMinimalActive ||
					bDX11NativeWorldAutoPresentActive ||
					bDX11NativeWorldForceVisibleActive;
				const bool bDX11WorldHybridPresentCommitted =
					(bDX11NativeWorldAutoPresentActive && !bDX11WorldRendererPortedPreRender);
				const char* c_szDX11WorldRuntimeStage = "warmup_bridge";
				if (bDX11WorldHybridPresentCommitted)
					c_szDX11WorldRuntimeStage = "hybrid_present";
				else if (bDX11WorldNativeDrawCommitted)
					c_szDX11WorldRuntimeStage = bDX11WorldRendererPortedPreRender ? "native_active" : "native_bridge";

				bool bDX11WorldTerrainRenderResult = true;
				uint32_t dwDX11WorldPortMaskObserved = 0u;
				uint32_t dwDX11WorldPortMaskSubmitted = 0u;
				uint32_t dwDX11WorldPortMaskApplicable = 0u;
				if (bDX11WorldNativeDrawCommitted)
				{
					// Runtime DX11 path executes after UI pass setup, which can leave
					// global projection/view in an ortho-like state. Restore game camera
					// matrices before building terrain frustum in DX11 world render.
					float fDX11WorldAspect = m_kWndMgr.GetAspect();
					float fDX11WorldFarClip = m_pyBackground.GetFarClip();
					const float fDX11WorldNearClip = 100.0f;
					if (!(fDX11WorldAspect > 0.0001f))
						fDX11WorldAspect = 1.7777778f;
					if (!(fDX11WorldFarClip > fDX11WorldNearClip + 0.001f))
						fDX11WorldFarClip = fDX11WorldNearClip + 1000.0f;
					m_pyGraphic.SetPerspective(30.0f, fDX11WorldAspect, fDX11WorldNearClip, fDX11WorldFarClip);
					m_pyGraphic.UpdateViewMatrix();

					bDX11WorldTerrainRenderResult = m_pyBackground.RenderTerrainDX11(
						m_grpDeviceDX11.GetDevice(),
						m_grpDeviceDX11.GetContext(),
						&dwDX11WorldPortMaskObserved,
						&dwDX11WorldPortMaskSubmitted,
						&dwDX11WorldPortMaskApplicable);

					// Frame timing diagnostic: Mark world render completion
					static DWORD s_dwAfterWorldRenderLogTick = 0;
					const DWORD dwAfterWorldNow = ELTimer_GetMSec();
					if (0 == s_dwAfterWorldRenderLogTick || dwAfterWorldNow - s_dwAfterWorldRenderLogTick >= 5000u)
					{
						s_dwAfterWorldRenderLogTick = dwAfterWorldNow;
						TraceError("DX11_AFTER_WORLD_RENDER result=%d frame=%u elapsed_ms=%u",
							bDX11WorldTerrainRenderResult ? 1 : 0, m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS);
					}

					// M3-TEXTTAIL-PARITY-25: Render pass order verification
					// Log checkpoint after world rendering completes
					static DWORD s_dwRenderPassOrderLogTick = 0;
					static bool s_bRenderPassOrderInitialLogged = false;
					const DWORD dwPassOrderNow = ELTimer_GetMSec();

					if (!s_bRenderPassOrderInitialLogged)
					{
						s_bRenderPassOrderInitialLogged = true;
						s_dwRenderPassOrderLogTick = dwPassOrderNow;
						TraceError("DX11_TEXTTAIL_PASS_ORDER stage=world_complete next=ui_pass");
					}
					else if (dwPassOrderNow - s_dwRenderPassOrderLogTick >= 30000u)
					{
						s_dwRenderPassOrderLogTick = dwPassOrderNow;
						TraceError("DX11_TEXTTAIL_PASS_ORDER stage=heartbeat world_ui_sequence=valid");
					}
				}

				// World-port runtime contract (v5):
				// - observed mask: probe telemetry
				// - submitted mask: real DX11 submit for current frame
				// - applicable mask: subsystems relevant for current scene/runtime state
				// - submitted_seen mask: latched real submits seen in current session/map lifecycle
				// - committed mask: gate decision source (derived from submitted_seen + applicable)
				static uint32_t s_dwDX11WorldPortMaskSubmittedSeen = 0u;
				if (!bDX11NativeVisibleActive || !bDX11NativeWorldMinimalRequested)
					s_dwDX11WorldPortMaskSubmittedSeen = 0u;

				const uint32_t dwDX11WorldPortMaskObservedMasked = (dwDX11WorldPortMaskObserved & dwDX11WorldRequiredMask);
				const uint32_t dwDX11WorldPortMaskSubmittedFrameMasked = (dwDX11WorldPortMaskSubmitted & dwDX11WorldRequiredMask);
				uint32_t dwDX11WorldPortMaskApplicableMasked = (dwDX11WorldPortMaskApplicable & dwDX11WorldRequiredMask);
				if (0u == (dwDX11WorldPortMaskApplicableMasked & CGraphicDeviceDX11::WORLD_TERRAIN_DX11))
					dwDX11WorldPortMaskApplicableMasked |= CGraphicDeviceDX11::WORLD_TERRAIN_DX11;

				static bool s_bDX11StartupFirstWorldSubmitLogged = false;
				if (!s_bDX11StartupFirstWorldSubmitLogged && 0u != dwDX11WorldPortMaskSubmittedFrameMasked)
				{
					s_bDX11StartupFirstWorldSubmitLogged = true;
					TraceError(
						"DX11_STARTUP_TIMELINE event=first_world_submit submitted_mask=0x%02X frame=%u elapsed_ms=%u",
						static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedFrameMasked),
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}

				if (bDX11WorldNativeDrawCommitted && bDX11WorldTerrainRenderResult)
					s_dwDX11WorldPortMaskSubmittedSeen |= (dwDX11WorldPortMaskSubmittedFrameMasked & dwDX11WorldPortMaskApplicableMasked);

				const uint32_t dwDX11WorldPortMaskRequiredEffective =
					(dwDX11WorldRequiredMask & dwDX11WorldPortMaskApplicableMasked);
				const uint32_t dwDX11WorldPortMaskSubmittedSeenMasked =
					(s_dwDX11WorldPortMaskSubmittedSeen & dwDX11WorldRequiredMask);
				const uint32_t dwDX11WorldPortMaskCommitted =
					(dwDX11WorldPortMaskSubmittedSeenMasked & dwDX11WorldPortMaskRequiredEffective);

				m_grpDeviceDX11.SetNativeWorldObservedMask(dwDX11WorldPortMaskObservedMasked);
				m_grpDeviceDX11.SetNativeWorldSubmittedMask(dwDX11WorldPortMaskSubmittedFrameMasked);
				m_grpDeviceDX11.SetNativeWorldApplicableMask(dwDX11WorldPortMaskApplicableMasked);
				m_grpDeviceDX11.SetNativeWorldSubmittedSeenMask(dwDX11WorldPortMaskSubmittedSeenMasked);
				m_grpDeviceDX11.SetNativeWorldCommittedMask(dwDX11WorldPortMaskCommitted);
				m_grpDeviceDX11.SetNativeWorldPortMask(dwDX11WorldPortMaskCommitted);
				CPythonBackground::Instance().SetDX11WorldSubmitCommittedMask(dwDX11WorldPortMaskCommitted);
				const CPythonBackground::SDX11WorldSubmitTelemetry& rkWorldSubmitTelemetry =
					CPythonBackground::Instance().GetDX11WorldSubmitTelemetry();

				const uint32_t dwTelemetryObservedMask =
					(rkWorldSubmitTelemetry.dwObservedMask & dwDX11WorldRequiredMask);
				const uint32_t dwTelemetrySubmittedMask =
					(rkWorldSubmitTelemetry.dwSubmittedMask & dwDX11WorldRequiredMask);
				const uint32_t dwTelemetryApplicableMask =
					(rkWorldSubmitTelemetry.dwApplicableMask & dwDX11WorldRequiredMask);
				const uint32_t dwTelemetryCommittedMask =
					(rkWorldSubmitTelemetry.dwCommittedMask & dwDX11WorldRequiredMask);
				const bool bWorldSubmitMaskParity =
					(dwTelemetryObservedMask == dwDX11WorldPortMaskObservedMasked) &&
					(dwTelemetrySubmittedMask == dwDX11WorldPortMaskSubmittedFrameMasked) &&
					(dwTelemetryApplicableMask == dwDX11WorldPortMaskApplicableMasked) &&
					(dwTelemetryCommittedMask == dwDX11WorldPortMaskCommitted);
				uint32_t uWorldSubmitMismatchReasonMask = 0u;
				if (dwTelemetryObservedMask != dwDX11WorldPortMaskObservedMasked)
					uWorldSubmitMismatchReasonMask |= 0x01u;
				if (dwTelemetrySubmittedMask != dwDX11WorldPortMaskSubmittedFrameMasked)
					uWorldSubmitMismatchReasonMask |= 0x02u;
				if (dwTelemetryApplicableMask != dwDX11WorldPortMaskApplicableMasked)
					uWorldSubmitMismatchReasonMask |= 0x04u;
				if (dwTelemetryCommittedMask != dwDX11WorldPortMaskCommitted)
					uWorldSubmitMismatchReasonMask |= 0x08u;
				const bool bWorldSubmitParityPhaseActive =
					bDX11WorldNativeDrawCommitted &&
					bDX11WorldTerrainRenderResult &&
					bDX11NativeVisibleActive &&
					bDX11NativeWorldMinimalRequested &&
					bDX11NativeWorldMinimalActive &&
					bDX11WorldDryRunReady &&
					bDX11NativeWorldApplicableReady &&
					bDX11NativeWorldSubmittedReady &&
					bDX11NativeWorldCommittedReady &&
					bDX11NativePresentConfidenceReady &&
					!m_bDX11WorldHandoffProbeMode &&
					!m_bDX11RuntimeCompatGraceMode &&
					!bDX11WorldAutoPresentCooldownActiveNow &&
					!bDX11StrictNativePresentBackoffActiveNow;
				const char* c_szWorldSubmitParityPhase = bWorldSubmitParityPhaseActive ? "active" : "warmup";

				m_uDX11WorldSubmitMaskMismatchTelemetryObserved = dwTelemetryObservedMask;
				m_uDX11WorldSubmitMaskMismatchTelemetrySubmitted = dwTelemetrySubmittedMask;
				m_uDX11WorldSubmitMaskMismatchTelemetryApplicable = dwTelemetryApplicableMask;
				m_uDX11WorldSubmitMaskMismatchTelemetryCommitted = dwTelemetryCommittedMask;
				m_uDX11WorldSubmitMaskMismatchGateObserved = dwDX11WorldPortMaskObservedMasked;
				m_uDX11WorldSubmitMaskMismatchGateSubmitted = dwDX11WorldPortMaskSubmittedFrameMasked;
				m_uDX11WorldSubmitMaskMismatchGateApplicable = dwDX11WorldPortMaskApplicableMasked;
				m_uDX11WorldSubmitMaskMismatchGateCommitted = dwDX11WorldPortMaskCommitted;

				if (!bWorldSubmitMaskParity)
				{
					m_uDX11WorldSubmitMaskMismatchLastReasonMask = uWorldSubmitMismatchReasonMask;
					m_uDX11WorldSubmitMaskMismatchLastPhaseActive = bWorldSubmitParityPhaseActive ? 1u : 0u;
					m_dwDX11WorldSubmitMaskMismatchLastFrame = m_dwDX11RuntimeCompatFrameCount;
					m_dwDX11WorldSubmitMaskMismatchLastElapsedMS = m_dwDX11RuntimeCompatElapsedMS;

					if (bWorldSubmitParityPhaseActive)
					{
						if (!m_bDX11WorldSubmitMaskMismatchActive)
							++m_dwDX11WorldSubmitMaskMismatchCount;

						if (!m_bDX11WorldSubmitMaskMismatchActive || (0u == m_dwDX11WorldSubmitMaskMismatchLastLogMS) ||
							(dwVisibleNow - m_dwDX11WorldSubmitMaskMismatchLastLogMS) >= 5000u)
						{
							m_dwDX11WorldSubmitMaskMismatchLastLogMS = dwVisibleNow;
							TraceError(
								"DX11_WORLD_SUBMIT_MASK_MISMATCH phase=%s reason_mask=0x%02X telemetry_obs=0x%02X telemetry_sub=0x%02X telemetry_app=0x%02X telemetry_com=0x%02X gate_obs=0x%02X gate_sub=0x%02X gate_app=0x%02X gate_com=0x%02X frame=%u elapsed_ms=%u",
								c_szWorldSubmitParityPhase,
								static_cast<unsigned int>(uWorldSubmitMismatchReasonMask),
								static_cast<unsigned int>(dwTelemetryObservedMask),
								static_cast<unsigned int>(dwTelemetrySubmittedMask),
								static_cast<unsigned int>(dwTelemetryApplicableMask),
								static_cast<unsigned int>(dwTelemetryCommittedMask),
								static_cast<unsigned int>(dwDX11WorldPortMaskObservedMasked),
								static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedFrameMasked),
								static_cast<unsigned int>(dwDX11WorldPortMaskApplicableMasked),
								static_cast<unsigned int>(dwDX11WorldPortMaskCommitted),
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
						m_bDX11WorldSubmitMaskMismatchActive = true;
					}
					else
					{
						if (m_bDX11WorldSubmitMaskMismatchActive)
						{
							m_bDX11WorldSubmitMaskMismatchActive = false;
							TraceError(
								"DX11_WORLD_SUBMIT_MASK_MISMATCH_RECOVERED phase=%s obs=0x%02X sub=0x%02X app=0x%02X com=0x%02X frame=%u elapsed_ms=%u",
								c_szWorldSubmitParityPhase,
								static_cast<unsigned int>(dwDX11WorldPortMaskObservedMasked),
								static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedFrameMasked),
								static_cast<unsigned int>(dwDX11WorldPortMaskApplicableMasked),
								static_cast<unsigned int>(dwDX11WorldPortMaskCommitted),
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}

						static DWORD s_dwWorldSubmitWarmupParityLogTick = 0u;
						if (0u == s_dwWorldSubmitWarmupParityLogTick || (dwVisibleNow - s_dwWorldSubmitWarmupParityLogTick) >= 5000u)
						{
							s_dwWorldSubmitWarmupParityLogTick = dwVisibleNow;
							TraceError(
								"DX11_WORLD_SUBMIT_MASK_PARITY_WARMUP phase=%s reason_mask=0x%02X telemetry_obs=0x%02X telemetry_sub=0x%02X telemetry_app=0x%02X telemetry_com=0x%02X gate_obs=0x%02X gate_sub=0x%02X gate_app=0x%02X gate_com=0x%02X frame=%u elapsed_ms=%u",
								c_szWorldSubmitParityPhase,
								static_cast<unsigned int>(uWorldSubmitMismatchReasonMask),
								static_cast<unsigned int>(dwTelemetryObservedMask),
								static_cast<unsigned int>(dwTelemetrySubmittedMask),
								static_cast<unsigned int>(dwTelemetryApplicableMask),
								static_cast<unsigned int>(dwTelemetryCommittedMask),
								static_cast<unsigned int>(dwDX11WorldPortMaskObservedMasked),
								static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedFrameMasked),
								static_cast<unsigned int>(dwDX11WorldPortMaskApplicableMasked),
								static_cast<unsigned int>(dwDX11WorldPortMaskCommitted),
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
					}
				}
				else if (m_bDX11WorldSubmitMaskMismatchActive)
				{
					m_bDX11WorldSubmitMaskMismatchActive = false;
					TraceError(
						"DX11_WORLD_SUBMIT_MASK_MISMATCH_RECOVERED phase=%s obs=0x%02X sub=0x%02X app=0x%02X com=0x%02X frame=%u elapsed_ms=%u",
						c_szWorldSubmitParityPhase,
						static_cast<unsigned int>(dwDX11WorldPortMaskObservedMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedFrameMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskApplicableMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskCommitted),
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}

				static DWORD s_dwWorldSubmitCountersLogTick = 0u;
				if (0u == s_dwWorldSubmitCountersLogTick || (dwVisibleNow - s_dwWorldSubmitCountersLogTick) >= 5000u)
				{
					s_dwWorldSubmitCountersLogTick = dwVisibleNow;
					TraceError(
						"DX11_WORLD_SUBMIT_COUNTERS terrain_patches=%d terrain_splats=%d water_patches=%d object_submitted=%u effect_submitted=%u effect_particle_submitted=%u effect_mesh_submitted=%u speedtree_submitted=%u observed_mask=0x%02X submitted_mask=0x%02X applicable_mask=0x%02X committed_mask=0x%02X",
						rkWorldSubmitTelemetry.iTerrainPatches,
						rkWorldSubmitTelemetry.iTerrainSplats,
						rkWorldSubmitTelemetry.iWaterPatches,
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwObjectSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwEffectSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwEffectParticleSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwEffectMeshSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwSpeedTreeSubmitted),
						static_cast<unsigned int>(dwDX11WorldPortMaskObservedMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedFrameMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskApplicableMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskCommitted));
				}

				static DWORD s_dwDX11WorldPortProbeLogTick = 0;
				if (0 == s_dwDX11WorldPortProbeLogTick || dwVisibleNow - s_dwDX11WorldPortProbeLogTick >= DX11_WORLD_HEARTBEAT_INTERVAL_MS)
				{
					s_dwDX11WorldPortProbeLogTick = dwVisibleNow;
					char szObservedMissing[128];
					char szSubmittedFrameMissing[128];
					char szSubmittedSeenMissing[128];
					char szCommittedMissing[128];
					const uint32_t dwObservedMissing = (dwDX11WorldPortMaskRequiredEffective & ~dwDX11WorldPortMaskObservedMasked);
					const uint32_t dwSubmittedFrameMissing = (dwDX11WorldPortMaskRequiredEffective & ~dwDX11WorldPortMaskSubmittedFrameMasked);
					const uint32_t dwSubmittedSeenMissing = (dwDX11WorldPortMaskRequiredEffective & ~dwDX11WorldPortMaskSubmittedSeenMasked);
					const uint32_t dwCommittedMissing = (dwDX11WorldPortMaskRequiredEffective & ~dwDX11WorldPortMaskCommitted);
					DX11FormatWorldPortMask(dwObservedMissing, szObservedMissing, sizeof(szObservedMissing));
					DX11FormatWorldPortMask(dwSubmittedFrameMissing, szSubmittedFrameMissing, sizeof(szSubmittedFrameMissing));
					DX11FormatWorldPortMask(dwSubmittedSeenMissing, szSubmittedSeenMissing, sizeof(szSubmittedSeenMissing));
					DX11FormatWorldPortMask(dwCommittedMissing, szCommittedMissing, sizeof(szCommittedMissing));
					TraceError(
						"DX11_WORLD_PORT_PROBE observed=0x%02X submitted_frame=0x%02X submitted_seen=0x%02X committed=0x%02X applicable=0x%02X required_effective=0x%02X observed_missing=%s submitted_frame_missing=%s submitted_seen_missing=%s committed_missing=%s gate_force=%d gate_auto=%d draw_ok=%d frame=%u elapsed_ms=%u",
						static_cast<unsigned int>(dwDX11WorldPortMaskObservedMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedFrameMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedSeenMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskCommitted),
						static_cast<unsigned int>(dwDX11WorldPortMaskApplicableMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskRequiredEffective),
						szObservedMissing,
						szSubmittedFrameMissing,
						szSubmittedSeenMissing,
						szCommittedMissing,
						bDX11NativeWorldForceVisibleRequested ? 1 : 0,
						bDX11NativeWorldAutoGateConfigEnabled ? 1 : 0,
						bDX11WorldTerrainRenderResult ? 1 : 0,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}

				static DWORD s_dwDX11WorldPortStateLogTick = 0;
				if (0 == s_dwDX11WorldPortStateLogTick || dwVisibleNow - s_dwDX11WorldPortStateLogTick >= DX11_WORLD_HEARTBEAT_INTERVAL_MS)
				{
					s_dwDX11WorldPortStateLogTick = dwVisibleNow;
					char szSubmittedSeenMissing[128];
					char szCommittedMissing[128];
					const uint32_t dwSubmittedSeenMissing = (dwDX11WorldPortMaskRequiredEffective & ~dwDX11WorldPortMaskSubmittedSeenMasked);
					const uint32_t dwCommittedMissing = (dwDX11WorldPortMaskRequiredEffective & ~dwDX11WorldPortMaskCommitted);
					DX11FormatWorldPortMask(dwSubmittedSeenMissing, szSubmittedSeenMissing, sizeof(szSubmittedSeenMissing));
					DX11FormatWorldPortMask(dwCommittedMissing, szCommittedMissing, sizeof(szCommittedMissing));
					TraceError(
						"DX11_WORLD_PORT_STATE observed=0x%02X submitted_frame=0x%02X submitted_seen=0x%02X committed=0x%02X applicable=0x%02X required_effective=0x%02X submitted_seen_missing=%s committed_missing=%s ready=%d frame=%u elapsed_ms=%u",
						static_cast<unsigned int>(dwDX11WorldPortMaskObservedMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedFrameMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskSubmittedSeenMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskCommitted),
						static_cast<unsigned int>(dwDX11WorldPortMaskApplicableMasked),
						static_cast<unsigned int>(dwDX11WorldPortMaskRequiredEffective),
						szSubmittedSeenMissing,
						szCommittedMissing,
						(0u == dwCommittedMissing) ? 1 : 0,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}

				const bool bDX11WorldRendererPorted =
					(0u == (dwDX11WorldPortMaskRequiredEffective & ~dwDX11WorldPortMaskCommitted));
				if (bDX11WorldTerrainRenderResult && bDX11WorldRendererPorted && bDX11WorldNativeDrawCommitted)
				{
					int iDX11PostRenderedPatchCount = 0;
					int iDX11PostRenderedSplatCount = 0;
					float fDX11PostRenderedSplatRatio = 0.0f;
					m_pyBackground.GetRenderedSplatNum(
						&iDX11PostRenderedPatchCount,
						&iDX11PostRenderedSplatCount,
						&fDX11PostRenderedSplatRatio);

					static DWORD s_dwDX11WorldCoverageLogTick = 0;
					if (0 == s_dwDX11WorldCoverageLogTick || dwVisibleNow - s_dwDX11WorldCoverageLogTick >= DX11_WORLD_HEARTBEAT_INTERVAL_MS)
					{
						s_dwDX11WorldCoverageLogTick = dwVisibleNow;
						TraceError(
							"DX11_WORLD_NATIVE_COVERAGE pre_patch=%d post_patch=%d pre_splat=%d post_splat=%d pre_ratio=%.3f post_ratio=%.3f frame=%u elapsed_ms=%u",
							iRenderedPatchCount,
							iDX11PostRenderedPatchCount,
							iRenderedSplatCount,
							iDX11PostRenderedSplatCount,
							fRenderedSplatRatio,
							fDX11PostRenderedSplatRatio,
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}

					const bool bDX11CoverageExpected = (iRenderedPatchCount >= 24);
					const bool bDX11CoverageMissing = (iDX11PostRenderedPatchCount <= 0);
					const bool bDX11CoverageTooLow =
						(iDX11PostRenderedPatchCount > 0) &&
						(iDX11PostRenderedPatchCount * 5 < iRenderedPatchCount);
					const bool bDX11CoverageGuardEnabled =
						(!bDX11TerrainStabilizationMode &&
						 bDX11NativeWorldAutoGateConfigEnabled &&
						 !bDX11StrictNativeOnlyEnabled);
					if (!bDX11CoverageGuardEnabled &&
						bDX11StrictNativeOnlyEnabled &&
						bDX11CoverageExpected &&
						(bDX11CoverageMissing || bDX11CoverageTooLow))
					{
						static DWORD s_dwDX11CoverageStrictSkipLogTick = 0;
						if (0 == s_dwDX11CoverageStrictSkipLogTick ||
							dwVisibleNow - s_dwDX11CoverageStrictSkipLogTick >= DX11_WORLD_HEARTBEAT_INTERVAL_MS)
						{
							s_dwDX11CoverageStrictSkipLogTick = dwVisibleNow;
							TraceError(
								"DX11_WORLD_NATIVE_COVERAGE_STRICT_SKIP pre_patch=%d post_patch=%d pre_splat=%d post_splat=%d pre_ratio=%.3f post_ratio=%.3f frame=%u elapsed_ms=%u",
								iRenderedPatchCount,
								iDX11PostRenderedPatchCount,
								iRenderedSplatCount,
								iDX11PostRenderedSplatCount,
								fRenderedSplatRatio,
								fDX11PostRenderedSplatRatio,
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
					}

					if (bDX11CoverageGuardEnabled &&
						bDX11CoverageExpected &&
						(bDX11CoverageMissing || bDX11CoverageTooLow))
					{
						bDX11WorldTerrainRenderResult = false;
						__LogDX11NativeBlocker("world_native_active", "terrain_coverage_guard");
						TraceError(
							"DX11_WORLD_NATIVE_COVERAGE_GUARD pre_patch=%d post_patch=%d pre_splat=%d post_splat=%d pre_ratio=%.3f post_ratio=%.3f frame=%u elapsed_ms=%u",
							iRenderedPatchCount,
							iDX11PostRenderedPatchCount,
							iRenderedSplatCount,
							iDX11PostRenderedSplatCount,
							fRenderedSplatRatio,
							fDX11PostRenderedSplatRatio,
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}
				}

				// Keep present telemetry aligned with the latest world draw results from this frame.
				// Using pre-render counts here causes false "clear-like" diagnostics and stale scene stats.
				if (bDX11WorldNativeDrawCommitted)
				{
					int iLatestRenderedPatchCount = iRenderedPatchCount;
					int iLatestRenderedSplatCount = iRenderedSplatCount;
					float fLatestRenderedSplatRatio = fRenderedSplatRatio;
					m_pyBackground.GetRenderedSplatNum(
						&iLatestRenderedPatchCount,
						&iLatestRenderedSplatCount,
						&fLatestRenderedSplatRatio);
					m_grpDeviceDX11.SetNativeWorldSceneStats(
						iLatestRenderedPatchCount,
						iLatestRenderedSplatCount,
						fLatestRenderedSplatRatio,
						iRenderedTextureCount,
						dwRenderedGraphicThingInstanceNum,
						dwRenderedCRCNum);
				}

				const bool bDX11WorldRuntimeDrawResult =
					!bDX11WorldRendererPorted
						? m_grpDeviceDX11.TickNativeWorldRuntime(
							c_szDX11WorldRuntimeStage,
							iTerrainDrawCount,
							iActorDrawCount,
							iFXDrawCount)
						: (bDX11WorldNativeDrawCommitted
							? m_grpDeviceDX11.DrawNativeWorldRenderPasses(
								CTimer::Instance().GetCurrentSecond(),
								iTerrainDrawCount,
								iActorDrawCount,
								iFXDrawCount)
							: m_grpDeviceDX11.DrawNativeWorldMinimalDryRun(
								CTimer::Instance().GetCurrentSecond(),
								iTerrainDrawCount,
								iActorDrawCount,
								iFXDrawCount));
				const bool bDX11WorldDryRunDrawResult = (bDX11WorldTerrainRenderResult && bDX11WorldRuntimeDrawResult);
				if (bDX11WorldDryRunDrawResult)
				{
					if (bDX11WorldShadowPassActive)
					{
						__ResolveDX11NativeBlockerSubsystem("world_native_shadow", "draw_ok");
					}
					else if (bDX11WorldNativeDrawCommitted)
					{
						if (bDX11WorldRendererPorted)
							__ResolveDX11NativeBlockerSubsystem("world_native_active", "draw_ok");
						else if (bDX11WorldHybridPresentCommitted)
							__ResolveDX11NativeBlockerSubsystem("world_native_warmup", "hybrid_heartbeat_ok");
						else
							__ResolveDX11NativeBlockerSubsystem("world_native_warmup", "runtime_bridge_ok");
					}
					else
					{
						__ResolveDX11NativeBlockerSubsystem("world_native_warmup", "draw_ok");
					}

					if (s_dwDX11WorldDryRunSuccessCount < 0xffffffffu)
						++s_dwDX11WorldDryRunSuccessCount;
					if (s_dwDX11WorldDryRunConsecutiveSuccess < 0xffffffffu)
						++s_dwDX11WorldDryRunConsecutiveSuccess;
					s_dwDX11WorldDryRunConsecutiveFail = 0;
				}
				else
				{
					if (s_dwDX11WorldDryRunFailCount < 0xffffffffu)
						++s_dwDX11WorldDryRunFailCount;
					if (s_dwDX11WorldDryRunConsecutiveFail < 0xffffffffu)
						++s_dwDX11WorldDryRunConsecutiveFail;
					s_dwDX11WorldDryRunConsecutiveSuccess = 0;

					const char* c_szDX11WorldDrawFailSubsystem = "world_native_warmup";
					if (bDX11WorldShadowPassActive)
						c_szDX11WorldDrawFailSubsystem = "world_native_shadow";
					else if (bDX11WorldNativeDrawCommitted && bDX11WorldRendererPorted)
						c_szDX11WorldDrawFailSubsystem = "world_native_active";
					__LogDX11NativeBlocker(
						c_szDX11WorldDrawFailSubsystem,
						"draw_failed");
					bDX11NativeWorldMinimalDryRunActive = false;
					if (bDX11NativeWorldMinimalActive)
					{
						__LogDX11NativeBlocker(
							bDX11WorldRendererPorted ? "world_native_active" : "world_native_warmup",
							"draw_failed_fallback_pending");
						bDX11NativeWorldMinimalActive = false;
						bDX11NativeWorldMinimalDryRunPausedByReady = true;
						__LogDX11NativeBlocker(
							bDX11WorldRendererPorted ? "world_native_active" : "world_native_warmup",
							"draw_failed_warmup_fallback");
					}
					if (bDX11NativeWorldAutoPresentActive || bDX11NativeWorldForceVisibleActive)
					{
						__LogDX11NativeBlocker("world_native_present", "draw_failed_fallback_pending");
						bDX11NativeWorldAutoPresentActive = false;
						s_dwDX11WorldAutoPresentFailCooldownUntilMS = ELTimer_GetMSec() + 5000u;
						__LogDX11NativeBlocker("world_native_present", "draw_failed_cooldown");
					}
					if (s_bDX11WorldDryRunReadyLatched)
					{
						s_bDX11WorldDryRunReadyLatched = false;
						TraceError(
							"DX11_WORLD_NATIVE_READY state=0 reason=warmup_draw_failed success_total=%u fail_total=%u frame=%u elapsed_ms=%u",
							s_dwDX11WorldDryRunSuccessCount,
							s_dwDX11WorldDryRunFailCount,
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}
				}

				const bool bDX11WorldAllowTerrainOnlyReady = bDX11NativeWorldForceVisibleRequested;
				const DWORD kDX11WorldDryRunReadySuccessThreshold =
					bDX11WorldAllowTerrainOnlyReady
						? kDX11WorldTerrainOnlyWarmupSuccessThreshold
						: kDX11WorldTerrainActorWarmupSuccessThreshold;
				const bool bDX11WorldReadyStructureSatisfied =
					bDX11WorldAllowTerrainOnlyReady || ((iDX11WorldPilotPhase >= 2) && (iActorDrawCount > 0));
				if (bDX11WorldDryRunDrawResult &&
					!s_bDX11WorldDryRunReadyLatched &&
					bDX11WorldReadyStructureSatisfied &&
					s_dwDX11WorldDryRunConsecutiveSuccess >= kDX11WorldDryRunReadySuccessThreshold)
				{
					s_bDX11WorldDryRunReadyLatched = true;
					s_dwDX11WorldDryRunReadySinceFrame = m_dwDX11RuntimeCompatFrameCount;
					TraceError(
						"DX11_WORLD_NATIVE_READY state=1 success_total=%u fail_total=%u success_streak=%u threshold=%u frame=%u elapsed_ms=%u",
						s_dwDX11WorldDryRunSuccessCount,
						s_dwDX11WorldDryRunFailCount,
						s_dwDX11WorldDryRunConsecutiveSuccess,
						kDX11WorldDryRunReadySuccessThreshold,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}

				static DWORD s_dwDX11WorldDryRunLogTick = 0;
				if (0 == s_dwDX11WorldDryRunLogTick || dwVisibleNow - s_dwDX11WorldDryRunLogTick >= DX11_WORLD_HEARTBEAT_INTERVAL_MS)
				{
					s_dwDX11WorldDryRunLogTick = dwVisibleNow;
					if (bDX11WorldShadowPassActive || (bDX11WorldNativeDrawCommitted && bDX11WorldRendererPorted))
					{
						TraceError(
							"DX11_WORLD_NATIVE_ACTIVE draw_ok=%d terrain=%d actors=%d fx=%d success_total=%u fail_total=%u success_streak=%u fail_streak=%u ready=%d ready_since_frame=%u frame=%u elapsed_ms=%u",
							bDX11WorldDryRunDrawResult ? 1 : 0,
							iTerrainDrawCount,
							iActorDrawCount,
							iFXDrawCount,
							s_dwDX11WorldDryRunSuccessCount,
							s_dwDX11WorldDryRunFailCount,
							s_dwDX11WorldDryRunConsecutiveSuccess,
							s_dwDX11WorldDryRunConsecutiveFail,
							s_bDX11WorldDryRunReadyLatched ? 1 : 0,
							s_dwDX11WorldDryRunReadySinceFrame,
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}
					else if (bDX11WorldNativeDrawCommitted)
					{
						if (bDX11WorldHybridPresentCommitted)
						{
							TraceError(
								"DX11_WORLD_NATIVE_HYBRID draw_ok=%d terrain=%d actors=%d fx=%d success_total=%u fail_total=%u success_streak=%u fail_streak=%u ready=%d ready_since_frame=%u frame=%u elapsed_ms=%u",
								bDX11WorldDryRunDrawResult ? 1 : 0,
								iTerrainDrawCount,
								iActorDrawCount,
								iFXDrawCount,
								s_dwDX11WorldDryRunSuccessCount,
								s_dwDX11WorldDryRunFailCount,
								s_dwDX11WorldDryRunConsecutiveSuccess,
								s_dwDX11WorldDryRunConsecutiveFail,
								s_bDX11WorldDryRunReadyLatched ? 1 : 0,
								s_dwDX11WorldDryRunReadySinceFrame,
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
						else
						{
							TraceError(
								"DX11_WORLD_NATIVE_BRIDGE draw_ok=%d terrain=%d actors=%d fx=%d success_total=%u fail_total=%u success_streak=%u fail_streak=%u ready=%d ready_since_frame=%u frame=%u elapsed_ms=%u",
								bDX11WorldDryRunDrawResult ? 1 : 0,
								iTerrainDrawCount,
								iActorDrawCount,
								iFXDrawCount,
								s_dwDX11WorldDryRunSuccessCount,
								s_dwDX11WorldDryRunFailCount,
								s_dwDX11WorldDryRunConsecutiveSuccess,
								s_dwDX11WorldDryRunConsecutiveFail,
								s_bDX11WorldDryRunReadyLatched ? 1 : 0,
								s_dwDX11WorldDryRunReadySinceFrame,
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
					}
					else
					{
						TraceError(
							"DX11_WORLD_NATIVE_WARMUP draw_ok=%d terrain=%d actors=%d fx=%d success_total=%u fail_total=%u success_streak=%u fail_streak=%u ready=%d ready_since_frame=%u frame=%u elapsed_ms=%u",
							bDX11WorldDryRunDrawResult ? 1 : 0,
							iTerrainDrawCount,
							iActorDrawCount,
							iFXDrawCount,
							s_dwDX11WorldDryRunSuccessCount,
							s_dwDX11WorldDryRunFailCount,
							s_dwDX11WorldDryRunConsecutiveSuccess,
							s_dwDX11WorldDryRunConsecutiveFail,
							s_bDX11WorldDryRunReadyLatched ? 1 : 0,
							s_dwDX11WorldDryRunReadySinceFrame,
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}
				}
				if (bDX11WorldShadowPassActive)
				{
					static DWORD s_dwDX11WorldShadowLogTick = 0;
					if (0 == s_dwDX11WorldShadowLogTick || dwVisibleNow - s_dwDX11WorldShadowLogTick >= DX11_WORLD_HEARTBEAT_INTERVAL_MS)
					{
						s_dwDX11WorldShadowLogTick = dwVisibleNow;
						TraceError(
							"DX11_WORLD_NATIVE_SHADOW draw_ok=%d terrain=%d actors=%d fx=%d ready=%d legacy_shadow_texture=0 dynamic_shadow_planned=1 frame=%u elapsed_ms=%u",
							bDX11WorldDryRunDrawResult ? 1 : 0,
							iTerrainDrawCount,
							iActorDrawCount,
							iFXDrawCount,
							s_bDX11WorldDryRunReadyLatched ? 1 : 0,
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}
				}
			}
			else if (bDX11NativeWorldMinimalDryRunPausedByReady)
			{
				static DWORD s_dwDX11WorldDryRunPausedLogTick = 0;
				if (0 == s_dwDX11WorldDryRunPausedLogTick || dwVisibleNow - s_dwDX11WorldDryRunPausedLogTick >= DX11_WORLD_HEARTBEAT_INTERVAL_MS)
				{
					s_dwDX11WorldDryRunPausedLogTick = dwVisibleNow;
					TraceError(
						"DX11_WORLD_NATIVE_WARMUP paused=1 reason=ready_latched success_total=%u fail_total=%u success_streak=%u ready=%d ready_since_frame=%u frame=%u elapsed_ms=%u",
						s_dwDX11WorldDryRunSuccessCount,
						s_dwDX11WorldDryRunFailCount,
						s_dwDX11WorldDryRunConsecutiveSuccess,
						s_bDX11WorldDryRunReadyLatched ? 1 : 0,
						s_dwDX11WorldDryRunReadySinceFrame,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}
			}

			static DWORD s_dwDX11BridgeCaptureOK = 0;
			static DWORD s_dwDX11BridgeCaptureFail = 0;
			static DWORD s_dwDX11BridgePresentOK = 0;
			static DWORD s_dwDX11BridgePresentFail = 0;
			static DWORD s_dwDX11BridgeCaptureSuccessStreak = 0;
			static DWORD s_dwDX11BridgePresentSuccessStreak = 0;
			static DWORD s_dwDX11BridgeIOTelemetryTick = 0;
			const DWORD kDX11NativePresentStallTimeoutMS = 2000u;
			bool bDX11NativePresentSucceededThisFrame = false;
			bool bDX11NativePresentFailedThisFrame = false;
			bool bDX11StrictPendingPresentedThisFrame = false;
			const bool bDX11FastCutoverBridgeReadyThreshold =
				(bDX11CutoverRuntimeMode && bDX11NativeVisibleConfigEnabled && bDX11NativeWorldMinimalConfigEnabled);
			const bool bDX11HybridBridgeReadyTarget =
				(!bDX11NativeWorldRendererPorted && bDX11NativeWorldAutoPresentRequested);
			const DWORD kDX11BridgeReadySuccessThresholdHybrid = bDX11FastCutoverBridgeReadyThreshold ? 300u : 600u;
			const DWORD kDX11BridgeReadySuccessThresholdNative = bDX11FastCutoverBridgeReadyThreshold ? 1200u : 2400u;
			const DWORD kDX11BridgeReadySuccessThreshold =
				bDX11HybridBridgeReadyTarget ? kDX11BridgeReadySuccessThresholdHybrid : kDX11BridgeReadySuccessThresholdNative;
			auto __SetDX11BridgeReadyState = [&](bool bReady, const char* c_szReason)
			{
				if (bReady == s_bDX11BridgeReadyLatched)
					return;

				s_bDX11BridgeReadyLatched = bReady;
				if (bReady)
					s_dwDX11BridgeReadySinceFrame = m_dwDX11RuntimeCompatFrameCount;

				TraceError(
					"DX11_NATIVE_BRIDGE_READY state=%d reason=%s capture_streak=%u present_streak=%u threshold=%u target=%s world_ready=%d frame=%u elapsed_ms=%u",
					bReady ? 1 : 0,
					c_szReason,
					s_dwDX11BridgeCaptureSuccessStreak,
					s_dwDX11BridgePresentSuccessStreak,
					kDX11BridgeReadySuccessThreshold,
					bDX11HybridBridgeReadyTarget ? "hybrid" : "native",
					s_bDX11WorldDryRunReadyLatched ? 1 : 0,
					m_dwDX11RuntimeCompatFrameCount,
					m_dwDX11RuntimeCompatElapsedMS);
			};
			auto __LogDX11BridgeIOTelemetry = [&](bool bForce)
			{
				if (bForce || 0 == s_dwDX11BridgeIOTelemetryTick || dwVisibleNow - s_dwDX11BridgeIOTelemetryTick >= DX11_BRIDGE_IO_HEARTBEAT_INTERVAL_MS)
				{
					s_dwDX11BridgeIOTelemetryTick = dwVisibleNow;
					TraceError(
						"DX11_NATIVE_BRIDGE_IO capture_ok=%u capture_fail=%u present_ok=%u present_fail=%u frame=%u elapsed_ms=%u",
						s_dwDX11BridgeCaptureOK,
						s_dwDX11BridgeCaptureFail,
						s_dwDX11BridgePresentOK,
						s_dwDX11BridgePresentFail,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS);
				}
			};
			auto __RunDX11BridgeCopyPresent = [&]() -> bool
			{
				if (s_dwDX11BridgeCaptureFail < 0xffffffffu)
					++s_dwDX11BridgeCaptureFail;
				if (s_dwDX11BridgePresentFail < 0xffffffffu)
					++s_dwDX11BridgePresentFail;

				s_dwDX11BridgeCaptureSuccessStreak = 0u;
				s_dwDX11BridgePresentSuccessStreak = 0u;

				__LogDX11NativeBlocker("present_bridge_capture", "strict_native_no_dx9_bridge");
				__LogDX11NativeBlocker("present_bridge_present", "strict_native_no_dx9_bridge");
				__SetDX11BridgeReadyState(false, "strict_native_no_dx9_bridge");
				__LogDX11BridgeIOTelemetry(true);
				return false;
			};

			if (bDX11BackendActive && bDX11NativeWorldRenderPathActive)
			{
				const bool bDX11WorldSceneReadyForPresent = m_pyBackground.IsMapReady();
				if (!bDX11WorldSceneReadyForPresent)
				{
					static DWORD s_dwDX11LoginWorldSuppressedLogTick = 0u;
					if (0u == s_dwDX11LoginWorldSuppressedLogTick ||
						dwVisibleNow - s_dwDX11LoginWorldSuppressedLogTick >= 3000u)
					{
						s_dwDX11LoginWorldSuppressedLogTick = dwVisibleNow;
						TraceError(
							"DX11_WORLD_WARMUP_SUPPRESSED reason=map_not_ready phase=non_game frame=%u elapsed_ms=%u",
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}
				}

				const bool bDX11WorldWarmupVisiblePending =
					bDX11WorldSceneReadyForPresent &&
					bDX11StrictNativeOnlyEnabled &&
					bDX11NativeVisibleActive &&
					!bDX11NativeWorldForceVisibleActive &&
					!bDX11NativeWorldAutoPresentActive &&
					!bDX11NativeWorldCommittedReady;

				if (bDX11WorldSceneReadyForPresent)
				{
					// Native world is rendered after the initial UI pass in this runtime branch.
					// Re-render UI right before native present so HUD is not covered by world draw.
					__SetDX11RenderCheckpoint("dx11_ui_overlay_post_world");
					m_grpDeviceDX11.BindMainRenderTargets();
					if (bDX11WorldWarmupVisiblePending)
					{
						__SetDX11RenderCheckpoint("dx11_world_warmup_visible");
						static DWORD s_dwDX11WorldWarmupVisibleLogTick = 0;
						if (0 == s_dwDX11WorldWarmupVisibleLogTick ||
							dwVisibleNow - s_dwDX11WorldWarmupVisibleLogTick >= 3000u)
						{
							s_dwDX11WorldWarmupVisibleLogTick = dwVisibleNow;
							TraceError(
								"DX11_WORLD_VISIBILITY_HOLD active=0 reason=warmup_visible_no_clear frame=%u elapsed_ms=%u",
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
					}
					else if (bDX11StrictNativeOnlyEnabled && !bDX11NativeWorldAutoPresentActive && bDX11NativeWorldCommittedReady)
					{
						static DWORD s_dwDX11WorldVisibilityHoldReleasedLogTick = 0;
						if (0 == s_dwDX11WorldVisibilityHoldReleasedLogTick ||
							dwVisibleNow - s_dwDX11WorldVisibilityHoldReleasedLogTick >= 3000u)
						{
							s_dwDX11WorldVisibilityHoldReleasedLogTick = dwVisibleNow;
							TraceError(
								"DX11_WORLD_VISIBILITY_HOLD active=0 reason=world_committed_pre_present frame=%u elapsed_ms=%u",
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
					}
					m_pyGraphic.SetInterfaceRenderState();
					OnUIRender();
					__LogDX11StartupFirstUISubmit();
					if (!bDX11NativeCursorOverlayActive)
						OnMouseRender();
				}
			}

			static DWORD s_dwDX11WorldForceVisiblePresentOK = 0;
			static DWORD s_dwDX11WorldForceVisiblePresentFail = 0;
			static DWORD s_dwDX11WorldForceVisiblePresentLogTick = 0;
			static DWORD s_dwDX11WorldNativePresentOK = 0;
			static DWORD s_dwDX11WorldNativePresentFail = 0;
			static DWORD s_dwDX11WorldNativePresentLogTick = 0;
			static DWORD s_dwDX11StrictNativePresentConsecutiveFail = 0;
			const bool bDX11NativeWorldPresentActive =
				(bDX11NativeWorldForceVisibleActive || bDX11NativeWorldAutoPresentActive);
			const bool bDX11NativeWorldHybridPresentActive =
				(bDX11NativeWorldAutoPresentActive && !bDX11NativeWorldRendererPorted);
			if (bDX11NativeWorldPresentActive)
			{
				__SetDX11RenderCheckpoint("dx11_native_world_present");
				if (bDX11NativeWorldHybridPresentActive)
				{
					if (bDX11StrictNativeOnlyEnabled)
					{
						__LogDX11NativeBlocker("world_native_present", "strict_native_only_no_hybrid_present");
						bDX11NativeVisibleActive = false;
					}
					else if (!__RunDX11BridgeCopyPresent())
					{
						if (s_dwDX11WorldNativePresentFail < 0xffffffffu)
							++s_dwDX11WorldNativePresentFail;
						__LogDX11NativeBlocker("world_native_present", "hybrid_bridge_present_failed");
						bDX11NativeVisibleActive = false;
					}
					else
					{
						if (s_dwDX11WorldNativePresentOK < 0xffffffffu)
							++s_dwDX11WorldNativePresentOK;
						__ResolveDX11NativeBlockerSubsystem("world_native_present", "hybrid_bridge_present_ok");
						if (0 == s_dwDX11WorldNativePresentLogTick ||
							dwVisibleNow - s_dwDX11WorldNativePresentLogTick >= DX11_NATIVE_HEARTBEAT_INTERVAL_MS)
						{
							s_dwDX11WorldNativePresentLogTick = dwVisibleNow;
							TraceError(
								"DX11_WORLD_NATIVE_PRESENT_HYBRID present_ok=%u present_fail=%u frame=%u elapsed_ms=%u",
								s_dwDX11WorldNativePresentOK,
								s_dwDX11WorldNativePresentFail,
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
					}
				}
				else
				{
				const bool bDX11ForceVisiblePresentPath = bDX11NativeWorldForceVisibleActive;

				// Log PresentNativeWorld call (once per session)
				static bool s_bPresentNativeWorldLogged = false;
				if (!s_bPresentNativeWorldLogged)
				{
					TraceError("DX11_PRESENT_PATH_NATIVE_WORLD force_visible=%d cursor_overlay=%d frame=%u elapsed_ms=%u",
						bDX11ForceVisiblePresentPath ? 1 : 0, bDX11NativeCursorOverlayActive ? 1 : 0,
						m_dwDX11RuntimeCompatFrameCount, m_dwDX11RuntimeCompatElapsedMS);
					s_bPresentNativeWorldLogged = true;
				}

				if (!m_grpDeviceDX11.PresentNativeWorld(
						bDX11NativeCursorOverlayActive,
						static_cast<float>(lDX11NativeCursorX),
						static_cast<float>(lDX11NativeCursorY)))
				{
					bDX11NativePresentFailedThisFrame = true;
					if (bDX11ForceVisiblePresentPath)
					{
						if (s_dwDX11WorldForceVisiblePresentFail < 0xffffffffu)
							++s_dwDX11WorldForceVisiblePresentFail;
						__LogDX11NativeBlocker("world_force_visible_present", "present_failed_fallback_to_bridge");
						bDX11NativeWorldForceVisibleActive = false;
					}
					else
					{
						if (s_dwDX11WorldNativePresentFail < 0xffffffffu)
							++s_dwDX11WorldNativePresentFail;
						const char* c_szNativePresentFailDetail =
							bDX11StrictNativeOnlyEnabled ? "strict_native_present_backoff" : "present_failed_bridge_fallback";
						__LogDX11NativeBlocker("world_native_present", c_szNativePresentFailDetail);
						if (bDX11StrictNativeOnlyEnabled)
						{
							if (s_dwDX11StrictNativePresentConsecutiveFail < 0xffffffffu)
								++s_dwDX11StrictNativePresentConsecutiveFail;
							s_dwDX11WorldAutoPresentFailCooldownUntilMS = ELTimer_GetMSec() + 5000u;
							bDX11NativeWorldAutoPresentActive = false;
							if (s_dwDX11StrictNativePresentFailCount < 0xffffffffu)
								++s_dwDX11StrictNativePresentFailCount;
							DWORD dwStrictBackoffMS = 10000u;
							if (s_dwDX11StrictNativePresentFailCount >= 4u)
								dwStrictBackoffMS = 60000u;
							else if (s_dwDX11StrictNativePresentFailCount >= 3u)
								dwStrictBackoffMS = 40000u;
							else if (s_dwDX11StrictNativePresentFailCount >= 2u)
								dwStrictBackoffMS = 20000u;
							s_dwDX11StrictNativePresentBackoffUntilMS = ELTimer_GetMSec() + dwStrictBackoffMS;
							TraceError(
								"DX11_NATIVE_PRESENT_BACKOFF reason=strict_native_present_failed backoff_ms=%u fail_count=%u frame=%u elapsed_ms=%u",
								dwStrictBackoffMS,
								s_dwDX11StrictNativePresentFailCount,
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
						else
						{
							s_dwDX11WorldAutoPresentFailCooldownUntilMS = ELTimer_GetMSec() + 5000u;
							bDX11NativeWorldAutoPresentActive = false;
						}
					}
					if (bDX11StrictNativeOnlyEnabled)
					{
						if (!m_grpDeviceDX11.PresentNativeWorldDryRun(
								bDX11NativeCursorOverlayActive,
								static_cast<float>(lDX11NativeCursorX),
								static_cast<float>(lDX11NativeCursorY)))
							bDX11NativeVisibleActive = false;
					}
					else if (!__RunDX11BridgeCopyPresent())
						bDX11NativeVisibleActive = false;
					else
					{
						if (bDX11ForceVisiblePresentPath)
							__ResolveDX11NativeBlockerSubsystem("world_force_visible_present", "bridge_fallback_ok");
						else
							__ResolveDX11NativeBlockerSubsystem("world_native_present", "bridge_fallback_ok");
						TraceError(
							"DX11_NATIVE_PRESENT_RECOVERY reason=%s fallback=bridge_copy frame=%u elapsed_ms=%u",
							bDX11ForceVisiblePresentPath ? "force_visible_present_failed" : "native_present_failed",
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}
				}
				else
				{
					bDX11NativePresentSucceededThisFrame = true;
					if (bDX11ForceVisiblePresentPath)
					{
						if (s_dwDX11WorldForceVisiblePresentOK < 0xffffffffu)
							++s_dwDX11WorldForceVisiblePresentOK;
						__ResolveDX11NativeBlockerSubsystem("world_force_visible_present", "present_ok");
						if (0 == s_dwDX11WorldForceVisiblePresentLogTick ||
							dwVisibleNow - s_dwDX11WorldForceVisiblePresentLogTick >= DX11_NATIVE_HEARTBEAT_INTERVAL_MS)
						{
							s_dwDX11WorldForceVisiblePresentLogTick = dwVisibleNow;
							TraceError(
								"DX11_WORLD_FORCE_VISIBLE_PRESENT present_ok=%u present_fail=%u frame=%u elapsed_ms=%u",
								s_dwDX11WorldForceVisiblePresentOK,
								s_dwDX11WorldForceVisiblePresentFail,
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
					}
					else
					{
						if (s_dwDX11WorldNativePresentOK < 0xffffffffu)
							++s_dwDX11WorldNativePresentOK;
						__ResolveDX11NativeBlockerSubsystem("world_native_present", "present_ok");
						s_dwDX11StrictNativePresentConsecutiveFail = 0;
						s_dwDX11StrictNativePresentFailCount = 0;
						s_dwDX11StrictNativePresentBackoffUntilMS = 0;
						if (0 == s_dwDX11WorldNativePresentLogTick ||
							dwVisibleNow - s_dwDX11WorldNativePresentLogTick >= DX11_NATIVE_HEARTBEAT_INTERVAL_MS)
						{
							s_dwDX11WorldNativePresentLogTick = dwVisibleNow;
							TraceError(
								"DX11_WORLD_NATIVE_PRESENT present_ok=%u present_fail=%u frame=%u elapsed_ms=%u",
								s_dwDX11WorldNativePresentOK,
								s_dwDX11WorldNativePresentFail,
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
					}
				}
				}
			}
			else
			{
				if (bDX11StrictNativeOnlyEnabled)
				{
					// Auto-present gate already emits detailed reasons for this subsystem.
					// Avoid double-emitting a second idle reason in the same frame.
					const bool bDX11AutoPresentReasonAlreadyOwnedByGate =
						(bDX11NativeWorldAutoPresentRequested && !bDX11NativeWorldForceVisibleRequested);
					if (!bDX11AutoPresentReasonAlreadyOwnedByGate)
					{
						const char* c_szStrictNativePresentIdleReason = "strict_native_only_waiting_for_world_activation";
						if (bDX11StrictNativePresentBackoffActiveNow)
							c_szStrictNativePresentIdleReason = "strict_native_present_backoff";
						else if (!bDX11NativeWorldMinimalActive)
							c_szStrictNativePresentIdleReason = "strict_native_only_waiting_for_world_activation";
						else if (!bDX11NativeWorldApplicableReady || !bDX11NativeWorldSubmittedReady || !bDX11NativeWorldCommittedReady)
							c_szStrictNativePresentIdleReason = "strict_native_only_blocked_by_mask";
						else if (!bDX11NativePresentConfidenceReady)
							c_szStrictNativePresentIdleReason = "confidence_window";
						else if (bDX11WorldAutoPresentCooldownActiveNow)
							c_szStrictNativePresentIdleReason = "cooldown_active";
						else
							c_szStrictNativePresentIdleReason = "strict_native_only_waiting_for_activation";

						__LogDX11NativeBlocker("world_native_present", c_szStrictNativePresentIdleReason);
					}
					if (!m_grpDeviceDX11.PresentNativeWorldDryRun(
							bDX11NativeCursorOverlayActive,
							static_cast<float>(lDX11NativeCursorX),
							static_cast<float>(lDX11NativeCursorY)))
						bDX11NativeVisibleActive = false;
					else
					{
						// PresentNativeWorldDryRun already presents the swapchain. Do not issue a second Present()
						// here, or flip-model swapchains can immediately show an unrendered backbuffer.
						bDX11StrictPendingPresentedThisFrame = true;
						static bool s_bDX11StrictPendingNativeVisiblePresentLogged = false;
						if (!s_bDX11StrictPendingNativeVisiblePresentLogged)
						{
							TraceError("DX11_STRICT_PENDING_PRESENT path=native_world_dryrun_present reason=world_native_present_inactive");
							s_bDX11StrictPendingNativeVisiblePresentLogged = true;
						}
						__SetDX11RenderCheckpoint("dx11_present_pending_strict_native_visible_done");
					}
				}
				else
				{
					if (!__RunDX11BridgeCopyPresent())
						bDX11NativeVisibleActive = false;
				}
			}

			const DWORD dwWatchdogNow = ELTimer_GetMSec();
			const bool bDX11StrictNativePresentBackoffActiveWatchdogNow =
				(bDX11StrictNativeOnlyEnabled && (s_dwDX11StrictNativePresentBackoffUntilMS > dwWatchdogNow));
			const bool bDX11NativeWorldPresentActiveNow =
				(bDX11NativeWorldForceVisibleActive || bDX11NativeWorldAutoPresentActive);
			const bool bDX11NativePresentWatchdogActiveWindow =
				(m_isActivateWnd && !IsIconic(m_hWnd));
			if (bDX11NativeVisibleActive &&
				bDX11NativeVisibleRuntimeReady &&
				bDX11NativePresentWatchdogActiveWindow &&
				bDX11NativeWorldPresentActiveNow &&
				!bDX11StrictNativePresentBackoffActiveWatchdogNow &&
				!bDX11NativePresentFailedThisFrame &&
				!bDX11StrictPendingPresentedThisFrame)
			{
				if (0 == s_dwDX11NativePresentLastSuccessTick)
					s_dwDX11NativePresentLastSuccessTick = dwVisibleNow;

				if (bDX11NativePresentSucceededThisFrame)
				{
					s_dwDX11NativePresentLastSuccessTick = dwVisibleNow;
				}
				else if (0 != s_dwDX11NativePresentLastSuccessTick &&
						 dwVisibleNow > s_dwDX11NativePresentLastSuccessTick &&
						 (dwVisibleNow - s_dwDX11NativePresentLastSuccessTick) >= kDX11NativePresentStallTimeoutMS)
				{
					if (0 == s_dwDX11NativePresentWatchdogLogTick ||
						dwVisibleNow - s_dwDX11NativePresentWatchdogLogTick >= DX11_NATIVE_HEARTBEAT_INTERVAL_MS)
					{
						s_dwDX11NativePresentWatchdogLogTick = dwVisibleNow;
						TraceError(
							"DX11_WORLD_NATIVE_PRESENT_WATCHDOG state=stall timeout_ms=%u last_success_ago_ms=%u frame=%u elapsed_ms=%u",
							kDX11NativePresentStallTimeoutMS,
							dwVisibleNow - s_dwDX11NativePresentLastSuccessTick,
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}

					__LogDX11NativeBlocker("world_native_present", "watchdog_stall_detected");
					if (s_dwDX11NativePresentWatchdogStallCount < 0xffffffffu)
						++s_dwDX11NativePresentWatchdogStallCount;
					bDX11NativeWorldForceVisibleActive = false;
					if (bDX11NativeWorldAutoPresentActive)
						s_dwDX11WorldAutoPresentFailCooldownUntilMS = ELTimer_GetMSec() + 5000u;
					bDX11NativeWorldAutoPresentActive = false;

					if (bDX11StrictNativeOnlyEnabled)
					{
						const DWORD dwStrictWatchdogBackoffMS = 10000u;
						const DWORD dwStrictWatchdogUntil = ELTimer_GetMSec() + dwStrictWatchdogBackoffMS;
						if (s_dwDX11StrictNativePresentBackoffUntilMS < dwStrictWatchdogUntil)
							s_dwDX11StrictNativePresentBackoffUntilMS = dwStrictWatchdogUntil;
						__LogDX11NativeBlocker("world_native_present", "strict_native_present_backoff");
						bDX11NativeVisibleActive = false;
					}
					else if (!__RunDX11BridgeCopyPresent())
					{
						bDX11NativeVisibleActive = false;
					}
					else
					{
						s_dwDX11NativePresentLastSuccessTick = dwVisibleNow;
						if (s_dwDX11NativePresentWatchdogRecoverCount < 0xffffffffu)
							++s_dwDX11NativePresentWatchdogRecoverCount;
						__ResolveDX11NativeBlockerSubsystem("world_native_present", "watchdog_bridge_recovered");
						TraceError(
							"DX11_NATIVE_PRESENT_RECOVERY reason=watchdog_bridge_recovered fallback=bridge_copy stall_count=%u recover_count=%u frame=%u elapsed_ms=%u",
							s_dwDX11NativePresentWatchdogStallCount,
							s_dwDX11NativePresentWatchdogRecoverCount,
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
					}
				}
			}
			else if (bDX11NativeVisibleActive && bDX11NativeVisibleRuntimeReady)
			{
				// Ignore watchdog while inactive/minimized to avoid false trips during alt-tab.
				s_dwDX11NativePresentLastSuccessTick = dwVisibleNow;
			}
	}

	if (bDX11NativeVisibleActive)
	{
		__SetDX11RenderCheckpoint("dx11_native_visible_done");
		static DWORD s_dwDX11NativeBridgeModeLogTick = 0;
		if (0 == s_dwDX11NativeBridgeModeLogTick || dwVisibleNow - s_dwDX11NativeBridgeModeLogTick >= 30000u)
		{
			s_dwDX11NativeBridgeModeLogTick = dwVisibleNow;
			const char* c_szDX11NativeBridgeMode = bDX11StrictNativeOnlyEnabled
				? "dx11_world_native_strict_dryrun"
				: "dx9_frame_copy";
			if (bDX11NativeWorldForceVisibleActive)
				c_szDX11NativeBridgeMode = "dx11_world_native_present_force";
			else if (bDX11NativeWorldAutoPresentActive)
				c_szDX11NativeBridgeMode = "dx11_world_native_present_auto";
			else if (bDX11NativeWorldMinimalActive)
				c_szDX11NativeBridgeMode = bDX11StrictNativeOnlyEnabled ? "dx11_world_native_strict_blocked" : "dx11_world_native_copy";
			else if (bDX11NativeWorldMinimalDryRunActive)
				c_szDX11NativeBridgeMode = bDX11StrictNativeOnlyEnabled ? "dx11_world_native_strict_warmup" : "dx11_world_warmup_copy";
			else if (bDX11NativeWorldMinimalDryRunPausedByReady)
				c_szDX11NativeBridgeMode = bDX11StrictNativeOnlyEnabled ? "dx11_world_native_strict_wait_activation" : "dx11_world_warmup_paused";
			TraceError(
				"DX11_NATIVE_BRIDGE mode=%s world_native=%d world_warmup=%d world_warmup_paused=%d world_ready=%d world_force_visible=%d world_native_present=%d world_shadow=%d bridge_ready=%d bridge_ready_since_frame=%u ui_native=%d cursor_overlay=%d handoff_probe=%d runtime_compat=%d pass16=%d present_last_success_age_ms=%u watchdog_stall=%u watchdog_recover=%u",
				c_szDX11NativeBridgeMode,
				bDX11NativeWorldMinimalActive ? 1 : 0,
				bDX11NativeWorldMinimalDryRunActive ? 1 : 0,
				bDX11NativeWorldMinimalDryRunPausedByReady ? 1 : 0,
				s_bDX11WorldDryRunReadyLatched ? 1 : 0,
				bDX11NativeWorldForceVisibleActive ? 1 : 0,
				bDX11NativeWorldAutoPresentActive ? 1 : 0,
				bDX11NativeWorldShadowActive ? 1 : 0,
				s_bDX11BridgeReadyLatched ? 1 : 0,
				s_dwDX11BridgeReadySinceFrame,
				bDX11NativeUIMinimalActive ? 1 : 0,
				bDX11NativeCursorOverlayActive ? 1 : 0,
				m_bDX11WorldHandoffProbeMode ? 1 : 0,
				m_bDX11RuntimeCompatMode ? 1 : 0,
				m_bDX11WorldNativePass16Mode ? 1 : 0,
				(0 != s_dwDX11NativePresentLastSuccessTick && dwVisibleNow >= s_dwDX11NativePresentLastSuccessTick)
					? (dwVisibleNow - s_dwDX11NativePresentLastSuccessTick)
					: 0u,
				s_dwDX11NativePresentWatchdogStallCount,
				s_dwDX11NativePresentWatchdogRecoverCount);
		}
	}
	else
	{
		if (bDX11BackendActive && bDX11NativeVisibleRequested && !bDX11NativeVisibleRuntimeReady)
			__LogDX11NativeBlocker("runtime_gate", c_szDX11VisibleReason);
		bool bPresentedByDX11StrictPendingPath = false;
		if (bDX11BackendActive && bDX11StrictNativeOnlyEnabled && m_grpDeviceDX11.IsValid())
		{
			__SetDX11RenderCheckpoint("dx11_present_pending_strict");
			bPresentedByDX11StrictPendingPath = m_grpDeviceDX11.Present();
			if (!bPresentedByDX11StrictPendingPath)
			{
				__SetDX11RenderCheckpoint("dx11_present_pending_strict_fail");
				__LogDX11RenderCheckpoint(true);
				return;
			}

			static bool s_bDX11StrictPendingPresentLogged = false;
			if (!s_bDX11StrictPendingPresentLogged)
			{
				TraceError("DX11_STRICT_PENDING_PRESENT path=swapchain_present reason=native_visible_runtime_pending");
				s_bDX11StrictPendingPresentLogged = true;
			}
			__SetDX11RenderCheckpoint("dx11_present_pending_strict_done");
		}

		if (!bPresentedByDX11StrictPendingPath)
		{
			m_pyGraphic.Show();
			__SetDX11RenderCheckpoint("dx9_present_done");
		}
	}

	if (bDX11FirstPassHybrid)
	{
		// In DX11 first-pass hybrid mode, DX9 remains visible by default.
		// Optional pilot: allow sparse real DX11 Present() in stable runtime to validate visible handoff safety.
		const bool bDX11VisiblePass1PilotConfig = m_pySystem.IsDX11WorldVisiblePass1TestEnabled();
		const DWORD kDX11VisiblePass1PostGraceSettleMS = 2000u;
		const bool bDX11VisiblePass1PostGraceSettle =
			(m_dwDX11RuntimeCompatLastGraceLeaveMS > 0 &&
			 dwRuntimeNowMS > m_dwDX11RuntimeCompatLastGraceLeaveMS &&
			 (dwRuntimeNowMS - m_dwDX11RuntimeCompatLastGraceLeaveMS) < kDX11VisiblePass1PostGraceSettleMS);
		const bool bDX11VisiblePass1PilotEligible =
			bDX11UINativeTest &&
			bDX11WorldFinalcheckTest &&
			bDX11RuntimeCompatMode &&
			m_bDX11WorldNativePass16Mode &&
			!m_bDX11WorldHandoffProbeMode &&
			!m_bDX11RuntimeCompatGraceMode &&
			!bDX11VisiblePass1PostGraceSettle;

		if (!bDX11VisiblePass1PilotConfig)
		{
			m_bDX11VisiblePass1AutoDisabled = false;
			m_dwDX11VisiblePass1FailCount = 0;
			m_dwDX11VisiblePass1SuccessCount = 0;
			m_dwDX11VisiblePass1LastAttemptFrame = 0;
			m_dwDX11VisiblePass1LastIntervalFrames = 0;
		}

		bool bPresentedVisibleDX11 = false;
		if (bDX11VisiblePass1PilotConfig && bDX11VisiblePass1PilotEligible && !m_bDX11VisiblePass1AutoDisabled)
		{
			DWORD dwVisiblePresentAttemptIntervalFrames = 600u;
			if (m_dwDX11VisiblePass1SuccessCount >= 8u)
				dwVisiblePresentAttemptIntervalFrames = 60u;
			else if (m_dwDX11VisiblePass1SuccessCount >= 3u)
				dwVisiblePresentAttemptIntervalFrames = 180u;

			const bool bDX11VisiblePass1BaseConfidence =
				(m_dwDX11VisiblePass1FailCount == 0u) &&
				(m_dwDX11RuntimeCompatGraceUsedCount == 0u) &&
				(m_dwDX11RuntimeCompatGraceExpiredCount == 0u);
			const bool bDX11VisiblePass1HighConfidence =
				bDX11VisiblePass1BaseConfidence &&
				(m_dwDX11VisiblePass1SuccessCount >= 120u) &&
				(m_dwDX11RuntimeCompatElapsedMS >= 180000u);
			const bool bDX11VisiblePass1UltraConfidence =
				bDX11VisiblePass1BaseConfidence &&
				(m_dwDX11VisiblePass1SuccessCount >= 900u) &&
				(m_dwDX11RuntimeCompatElapsedMS >= 600000u);
			const bool bDX11VisiblePass1ExtremeConfidence =
				bDX11VisiblePass1BaseConfidence &&
				(m_dwDX11VisiblePass1SuccessCount >= 1800u) &&
				(m_dwDX11RuntimeCompatElapsedMS >= 1200000u);
			if (bDX11VisiblePass1HighConfidence)
				dwVisiblePresentAttemptIntervalFrames = 30u;
			if (bDX11VisiblePass1UltraConfidence)
				dwVisiblePresentAttemptIntervalFrames = 10u;
			if (bDX11VisiblePass1ExtremeConfidence)
				dwVisiblePresentAttemptIntervalFrames = 5u;

			// Under render stress keep the safest cadence.
			if (bDX11RuntimeStressNow && dwVisiblePresentAttemptIntervalFrames < 600u)
				dwVisiblePresentAttemptIntervalFrames = 600u;

			if (m_dwDX11VisiblePass1LastIntervalFrames != dwVisiblePresentAttemptIntervalFrames)
			{
				const char* c_szVisibleCadenceTier = "base";
				if (dwVisiblePresentAttemptIntervalFrames <= 5u)
					c_szVisibleCadenceTier = "extreme";
				else if (dwVisiblePresentAttemptIntervalFrames <= 10u)
					c_szVisibleCadenceTier = "ultra";
				else if (dwVisiblePresentAttemptIntervalFrames <= 30u)
					c_szVisibleCadenceTier = "high";
				else if (dwVisiblePresentAttemptIntervalFrames <= 60u)
					c_szVisibleCadenceTier = "validated";
				else if (dwVisiblePresentAttemptIntervalFrames <= 180u)
					c_szVisibleCadenceTier = "warmup";
				else if (dwVisiblePresentAttemptIntervalFrames <= 600u)
					c_szVisibleCadenceTier = "safe";

				TraceError(
					"DX11_VISIBLE_PASS1 cadence_update interval_frames=%u tier=%s success=%u fail=%u grace_used=%u stress=%d frame=%u elapsed_ms=%u",
					dwVisiblePresentAttemptIntervalFrames,
					c_szVisibleCadenceTier,
					m_dwDX11VisiblePass1SuccessCount,
					m_dwDX11VisiblePass1FailCount,
					m_dwDX11RuntimeCompatGraceUsedCount,
					bDX11RuntimeStressNow ? 1 : 0,
					m_dwDX11RuntimeCompatFrameCount,
					m_dwDX11RuntimeCompatElapsedMS);
				m_dwDX11VisiblePass1LastIntervalFrames = dwVisiblePresentAttemptIntervalFrames;
			}

			const bool bShouldAttemptVisiblePresent =
				(0 == m_dwDX11VisiblePass1LastAttemptFrame) ||
				(m_dwDX11RuntimeCompatFrameCount >= (m_dwDX11VisiblePass1LastAttemptFrame + dwVisiblePresentAttemptIntervalFrames));

			if (bShouldAttemptVisiblePresent)
			{
				m_dwDX11VisiblePass1LastAttemptFrame = m_dwDX11RuntimeCompatFrameCount;
				__SetDX11RenderCheckpoint("dx11_present_visible_pass1_try");

				if (m_grpDeviceDX11.Present())
				{
					bPresentedVisibleDX11 = true;
					if (m_dwDX11VisiblePass1SuccessCount < 0xffffffffu)
						++m_dwDX11VisiblePass1SuccessCount;
					if (m_dwDX11VisiblePass1FailCount > 0)
					{
						TraceError("DX11_VISIBLE_PASS1 recovered success=%u after_failures=%u frame=%u elapsed_ms=%u",
							m_dwDX11VisiblePass1SuccessCount,
							m_dwDX11VisiblePass1FailCount,
							m_dwDX11RuntimeCompatFrameCount,
							m_dwDX11RuntimeCompatElapsedMS);
						m_dwDX11VisiblePass1FailCount = 0;
					}
					else
					{
						const bool bLogVisiblePresentSuccess =
							(m_dwDX11VisiblePass1SuccessCount <= 10u) ||
							(0u == (m_dwDX11VisiblePass1SuccessCount % 30u));
						if (bLogVisiblePresentSuccess)
						{
							TraceError("DX11_VISIBLE_PASS1 present_ok success=%u interval_frames=%u frame=%u elapsed_ms=%u",
								m_dwDX11VisiblePass1SuccessCount,
								dwVisiblePresentAttemptIntervalFrames,
								m_dwDX11RuntimeCompatFrameCount,
								m_dwDX11RuntimeCompatElapsedMS);
						}
					}
				}
				else
				{
					if (m_dwDX11VisiblePass1FailCount < 0xffffffffu)
						++m_dwDX11VisiblePass1FailCount;

					TraceError("DX11_VISIBLE_PASS1 present_fail count=%u interval_frames=%u frame=%u elapsed_ms=%u stage=%s",
						m_dwDX11VisiblePass1FailCount,
						dwVisiblePresentAttemptIntervalFrames,
						m_dwDX11RuntimeCompatFrameCount,
						m_dwDX11RuntimeCompatElapsedMS,
						GetDX11RuntimeStage());

					if (m_dwDX11VisiblePass1FailCount >= 3)
					{
						m_bDX11VisiblePass1AutoDisabled = true;
						TraceError("DX11_VISIBLE_PASS1 auto_disabled after repeated present failures.");
					}
				}
			}
		}

		if (!bPresentedVisibleDX11)
		{
			// Keep DX11 swapchain liveness diagnostics without overriding the presented DX9 frame.
			__SetDX11RenderCheckpoint("dx11_present_test_hybrid");
			m_grpDeviceDX11.PresentTest();
		}
	}

	const DWORD dwRenderEndTime = ELTimer_GetMSec();
	static DWORD s_dwRenderCheckTime = dwRenderEndTime;
	static DWORD s_dwRenderRangeTime = 0;
	static DWORD s_dwRenderRangeFrame = 0;

	m_dwCurRenderTime = dwRenderEndTime - dwRenderStartTime;
	s_dwRenderRangeTime += m_dwCurRenderTime;
	++s_dwRenderRangeFrame;

	if (dwRenderEndTime - s_dwRenderCheckTime > 1000) [[unlikely]]
	{
		m_fAveRenderTime = float(double(s_dwRenderRangeTime) / double(s_dwRenderRangeFrame));

		s_dwRenderCheckTime = ELTimer_GetMSec();
		s_dwRenderRangeTime = 0;
		s_dwRenderRangeFrame = 0;
	}

	const DWORD dwCurFaceCount = m_pyGraphic.GetFaceCount();
	m_pyGraphic.ResetFaceCount();
	rFaceCount += dwCurFaceCount;

	if (dwCurFaceCount > 5000)
	{
		m_dwFaceAccCount += dwCurFaceCount;
		m_dwFaceAccTime += m_dwCurRenderTime;

		m_fFaceSpd = (m_dwFaceAccCount / m_dwFaceAccTime);

		if (-1 == m_iForceSightRange)
		{
			static float s_fAveRenderTime = 16.0f;
			const float fRatio = 0.3f;
			s_fAveRenderTime = (s_fAveRenderTime * (100.0f - fRatio) + std::max(16.0f, static_cast<float>(m_dwCurRenderTime)) * fRatio) / 100.0f;

			const float fFar = DX11RuntimeConfig::kViewDistanceFarClipMax;
			const float fNear = MIN_FOG;
			const double dbAvePow = double(1000.0f / s_fAveRenderTime);
			const double dbMaxPow = 60.0;
			const float fDistance = std::max(static_cast<float>(fNear + (fFar - fNear) * (dbAvePow) / dbMaxPow), fNear);
			m_pyBackground.SetViewDistanceSet(0, fDistance);
		}
		else
		{
			m_pyBackground.SetViewDistanceSet(0, static_cast<float>(m_iForceSightRange));
		}
	}
	else
	{
		m_pyBackground.SetViewDistanceSet(0, DX11RuntimeConfig::kViewDistanceFarClipMax);
	}

	__UpdatePerfAutoAdjustment();
	__SetDX11RenderCheckpoint("runstep_end");
	__LogDX11RenderCheckpoint(false);

	++rRenderFrameCount;
}

void CPythonApplication::__SleepFrame(DWORD dwNow, DWORD dwNextUpdateTime)
{
	if (m_iFPS <= 0)
	{
		// Unlimited render: do not block on fixed update tick.
		Sleep(0);
		return;
	}

	DWORD dwNextWake = dwNextUpdateTime;
	if (m_iFPS > 0)
	{
		const DWORD dwNextRenderTime = static_cast<DWORD>(m_dNextRenderTimeMS);
		dwNextWake = std::min(dwNextWake, dwNextRenderTime);
	}

	if (dwNow >= dwNextWake)
	{
		Sleep(0);
		return;
	}

	const DWORD dwSleep = dwNextWake - dwNow;
	if (dwSleep > 1)
		Sleep(dwSleep - 1);
	else
		Sleep(0);
}

void CPythonApplication::UpdateClientRect()
{
	RECT rcApp;
	GetClientRect(&rcApp);
	OnSizeChange(rcApp.right - rcApp.left, rcApp.bottom - rcApp.top);
}

bool CPythonApplication::__ResizeRenderBackend(UINT uWidth, UINT uHeight)
{
	switch (m_eRenderBackend)
	{
		case RENDER_BACKEND_DX11:
		{
			const bool bDX11Resized = m_grpDeviceDX11.Resize(uWidth, uHeight);
#if defined(DX11_STRICT_ONLY)
			return bDX11Resized;
#else
			if (m_pySystem.IsDX11FirstPassActiveEnabled())
			{
				const bool bDX9CompatResized = m_grpDevice.ResizeBackBuffer(uWidth, uHeight);
				return bDX11Resized && bDX9CompatResized;
			}
			return bDX11Resized;
#endif
		}
		case RENDER_BACKEND_DX9:
		default:
		{
			const bool bDX9Resized = m_grpDevice.ResizeBackBuffer(uWidth, uHeight);
			if (m_iRequestedRenderAPI == 11 && m_grpDeviceDX11.IsValid())
				m_grpDeviceDX11.Resize(uWidth, uHeight);
			return bDX9Resized;
		}
	}
}

void CPythonApplication::SetMouseHandler(PyObject* poMouseHandler)
{
	m_poMouseHandler = poMouseHandler;
}

int CPythonApplication::CheckDeviceState()
{
	if (m_eRenderBackend == RENDER_BACKEND_DX11)
		return DEVICE_STATE_OK;

	CGraphicDevice::EDeviceState e_deviceState = m_grpDevice.GetDeviceState();

	switch (e_deviceState)
	{
		// ???????????????????? ???????????? ???????????????????? ???????? ???????????? ????????.
	case CGraphicDevice::DEVICESTATE_NULL:
		return DEVICE_STATE_FALSE;

		// DEVICESTATE_BROKEN???? ???????? ???????? ???????????????? ???????? ???? ???? ???????????? ???????? ????????.
		// ???????? ???????????? ???????? DrawPrimitive ???????? ???????? ???????? ???????????????????? ????????????.
	case CGraphicDevice::DEVICESTATE_BROKEN:
		return DEVICE_STATE_SKIP;

	case CGraphicDevice::DEVICESTATE_NEEDS_RESET:
		if (!m_grpDevice.Reset())
			return DEVICE_STATE_SKIP;

		break;
	}

	return DEVICE_STATE_OK;
}

bool CPythonApplication::CreateDevice(int width, int height, int Windowed, int bit, int frequency)
{
	auto createDX9Device = [&]() -> bool
	{
		int iRet;
		m_grpDevice.InitBackBufferCount(2);
		iRet = m_grpDevice.Create(GetWindowHandle(), width, height, Windowed ? true : false, bit, frequency);

		switch (iRet)
		{
			case CGraphicDevice::CREATE_OK:
				return true;

			case CGraphicDevice::CREATE_REFRESHRATE:
				return true;

			case CGraphicDevice::CREATE_ENUM:
			case CGraphicDevice::CREATE_DETECT:
				SET_EXCEPTION(CREATE_NO_APPROPRIATE_DEVICE);
				TraceError("CreateDevice: Enum & Detect failed");
				return false;

			case CGraphicDevice::CREATE_NO_DIRECTX:
				SET_EXCEPTION(CREATE_NO_DIRECTX);
				TraceError("CreateDevice: DirectX 8.1 or greater required to run game");
				return false;

			case CGraphicDevice::CREATE_DEVICE:
				SET_EXCEPTION(CREATE_DEVICE);
				TraceError("CreateDevice: GraphicDevice create failed");
				return false;

			case CGraphicDevice::CREATE_FORMAT:
				SET_EXCEPTION(CREATE_FORMAT);
				TraceError("CreateDevice: Change the screen format");
				return false;

			case CGraphicDevice::CREATE_GET_DEVICE_CAPS:
				PyErr_SetString(PyExc_RuntimeError, "GetDevCaps failed");
				TraceError("CreateDevice: GetDevCaps failed");
				return false;

			case CGraphicDevice::CREATE_GET_DEVICE_CAPS2:
				PyErr_SetString(PyExc_RuntimeError, "GetDevCaps2 failed");
				TraceError("CreateDevice: GetDevCaps2 failed");
				return false;

			default:
				if (iRet & CGraphicDevice::CREATE_OK)
				{
					if (iRet & CGraphicDevice::CREATE_NO_TNL)
					{
						CGrannyLODController::SetMinLODMode(true);
					}
					return true;
				}

				SET_EXCEPTION(UNKNOWN_ERROR);
				TraceError("CreateDevice: Unknown Error!");
				return false;
		}
	};

	if (m_eRenderBackend == RENDER_BACKEND_DX11)
	{
		bool bUseDX9CompatDevice = false;
		const char* c_szRuntimeCompatReason = "dx11_strict_only";
#if !defined(DX11_STRICT_ONLY)
		const bool bDisableDX9CompatDevice = m_pySystem.IsDX11DisableDX9CompatDeviceEnabled();
		bUseDX9CompatDevice = !bDisableDX9CompatDevice;
		c_szRuntimeCompatReason = bUseDX9CompatDevice ? "resource_subsystems_not_ported" : "dx9_compat_device_disabled";
#endif
		if (bUseDX9CompatDevice)
		{
			// DX11 path still relies on a broad set of legacy systems that directly use
			// CGraphicBase::ms_lpd3dDevice (fonts, textures, various resource classes).
			// Keep DX9 compatibility device alive unless explicit strict disable flag is set.
			if (!createDX9Device())
				return false;
		}

		if (!m_grpDeviceDX11.Create(GetWindowHandle(), static_cast<UINT>(width), static_cast<UINT>(height), Windowed ? true : false, m_pySystem.IsVSyncEnabled()))
		{
			TraceError("CreateDevice: DX11 backend create failed");
			return false;
		}
		if (!m_kEftMgr.InitializeDX11EffectResources(m_grpDeviceDX11.GetDevice()))
		{
			TraceError("DX11_EFFECT_RESOURCES init failed after DX11 device create (active DX11 backend).");
		}
		// M3-TEXTURE-ASYNC-10-RUNTIME: Automatic memory budget detection
		TraceError("DX11_TEXTURE_BUDGET_INIT_START");
		DWORD dwTotalRAM = CSystemMemoryDetector::GetTotalPhysicalMemoryMB();
		TraceError("DX11_TEXTURE_BUDGET_INIT_RAM total_ram_mb=%u", dwTotalRAM);

		DWORD dwOptimalBudget = CSystemMemoryDetector::DetectOptimalTextureBudgetMB();
		TraceError("DX11_TEXTURE_BUDGET_INIT_CALCULATED budget_mb=%u", dwOptimalBudget);
		CGraphicTextureDX11::SetMemoryBudgetMB(dwOptimalBudget);
		TraceError("DX11_TEXTURE_BUDGET_AUTO_SET total_ram_mb=%u budget_mb=%u", dwTotalRAM, dwOptimalBudget);

		DWORD dwVerifyMB = CGraphicTextureDX11::GetMemoryBudgetMB();
		TraceError("DX11_TEXTURE_BUDGET_VERIFY_IMMEDIATE get_result=%u expected=%u match=%d",
			dwVerifyMB, dwOptimalBudget, (dwVerifyMB == dwOptimalBudget) ? 1 : 0);
		TraceError(
			"DX11_RUNTIME_COMPAT legacy_dx9_device=%d reason=%s",
			bUseDX9CompatDevice ? 1 : 0,
			c_szRuntimeCompatReason);

#ifdef BUILD_DEBUG_UI
		// DX11 Model Sync: Initialize ImGui Developer Monitoring Tool
		if (!InitializeImGui())
		{
			TraceError("CreateDevice: ImGui initialization failed (non-critical, continuing without debug overlay)");
		}
#endif
		return true;
	}

	return createDX9Device();
}

void CPythonApplication::SetUserMovingMainWindow(bool flag)
{
	if (flag && !GetCursorPos(&m_InitialMouseMovingPoint))
		return;

	m_IsMovingMainWindow = flag;
}

bool CPythonApplication::IsUserMovingMainWindow() const
{
	return m_IsMovingMainWindow;
}

void CPythonApplication::UpdateMainWindowPosition()
{
	POINT finalPoint{};
	if (GetCursorPos(&finalPoint))
	{
		LONG xDiff = finalPoint.x - m_InitialMouseMovingPoint.x;
		LONG yDiff = finalPoint.y - m_InitialMouseMovingPoint.y;

		RECT r{};
		GetWindowRect(&r);

		SetPosition(r.left + xDiff, r.top + yDiff);
		m_InitialMouseMovingPoint = finalPoint;
	}
}

void CPythonApplication::Loop()
{	
	while (1)
	{	
		if (IsUserMovingMainWindow())
			UpdateMainWindowPosition();

		// Fair scheduling: avoid render starvation when the Windows message queue
		// is continuously non-empty (e.g. cursor/IME/input bursts).
		static const int s_iMaxMessageBurstPerFrame = 128;
		int iProcessedMessages = 0;
		while (iProcessedMessages < s_iMaxMessageBurstPerFrame && IsMessage())
		{
			if (!MessageProcess())
				return;

			++iProcessedMessages;
		}

		if (!Process())
			break;

		// M3-TEXTURE-ASYNC-10-RUNTIME: Process async texture loading results
		CGraphicTextureDX11::ProcessAsyncResults();

		// M3-SPEEDTREE-ATLAS-09: Report async texture stats to ImGui (every frame)
#ifdef BUILD_DEBUG_UI
		{
			DWORD dwPending, dwCompleted, dwFailed;
			CGraphicTextureDX11::GetAsyncStats(&dwPending, &dwCompleted, &dwFailed);

			DWORD dwCacheSize, dwTotalLoads, dwCacheHits;
			CGraphicTextureDX11::GetCacheStats(&dwCacheSize, &dwTotalLoads, &dwCacheHits);

			DWORD dwBudgetMB = CGraphicTextureDX11::GetMemoryBudgetMB();
			DWORD dwUsageMB = CGraphicTextureDX11::GetCurrentMemoryUsageMB();
			DWORD dwCacheMisses = (dwTotalLoads > dwCacheHits) ? (dwTotalLoads - dwCacheHits) : 0;

			ReportImGuiAsyncTextureStats(
				dwPending,
				dwCompleted,
				dwFailed,
				dwCacheSize,
				dwCacheHits,
				dwCacheMisses,
				dwBudgetMB,
				dwUsageMB);
		}
#endif

		// M3-TEXTURE-ASYNC-10-RUNTIME: Periodic memory adjustment (every 30s)
		static DWORD s_dwLastMemoryCheck = 0;
		DWORD dwNowCheck = ELTimer_GetMSec();
		if (dwNowCheck - s_dwLastMemoryCheck > 30000)
		{
			CSystemMemoryDetector::AdjustBudgetIfNeeded();
			s_dwLastMemoryCheck = dwNowCheck;
		}

#ifdef _DEBUG
		// M3-TEXTURE-ASYNC-10-RUNTIME: Periodic async texture loading telemetry (every 45s)
		if (0 == g_dwDX11TextureAsyncLogTick || dwNowCheck - g_dwDX11TextureAsyncLogTick >= DX11_TEXTURE_ASYNC_HEARTBEAT_INTERVAL_MS)
		{
			g_dwDX11TextureAsyncLogTick = dwNowCheck;
			
			DWORD dwCachedCount, dwTotalLoads, dwCacheHits;
			CGraphicTextureDX11::GetCacheStats(&dwCachedCount, &dwTotalLoads, &dwCacheHits);
			
			DWORD dwPending, dwCompleted, dwFailed;
			CGraphicTextureDX11::GetAsyncStats(&dwPending, &dwCompleted, &dwFailed);
			
			DWORD dwBudgetMB = CGraphicTextureDX11::GetMemoryBudgetMB();
			DWORD dwUsageMB = CGraphicTextureDX11::GetCurrentMemoryUsageMB();
			
			TraceError("DX11_TEXTURE_ASYNC_HEARTBEAT budget_mb=%u usage_mb=%u pending=%u completed=%u failed=%u cached=%u total_loads=%u cache_hits=%u",
				dwBudgetMB, dwUsageMB, dwPending, dwCompleted, dwFailed, dwCachedCount, dwTotalLoads, dwCacheHits);
		}
#endif



#ifdef BUILD_DEBUG_UI
		// DX11 Model Sync: Update ImGui metrics collector each frame
		if (CImGuiMetricsCollector::Instance())
		{
			// Get current frame metrics from PythonApplication
			// NOTE: m_fAveRenderTime is already in milliseconds, m_dwRenderFPS is frames per second
			const float fFrameTime = m_fAveRenderTime;  // Already in ms!
			const float fFPS = static_cast<float>(m_dwRenderFPS);

			// Runtime counters from active DX11 path (no simulated values)
			const UINT uDeviceDrawCalls = m_grpDeviceDX11.GetFrameDrawCalls();
			const UINT uDevicePrimitives = m_grpDeviceDX11.GetFramePrimitiveCount();
			const DWORD dwAliveEntities = CPythonCharacterManager::Instance().GetAliveInstanceCount();
			const DWORD dwDeadEntities = CPythonCharacterManager::Instance().GetDeadInstanceCount();
			const UINT uEntityCount = static_cast<UINT>(dwAliveEntities + dwDeadEntities);

			const uint32_t dwSubmittedMask = (m_grpDeviceDX11.GetNativeWorldSubmittedMask() & CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK);
			const UINT uWorldPassCount = static_cast<UINT>(DX11CountSetBits(dwSubmittedMask));
			const uint32_t uFeatureLevel = m_grpDeviceDX11.GetFeatureLevel();
			const uint32_t uWorldPortMask = m_grpDeviceDX11.GetNativeWorldPortMask();
			const uint32_t uMissingPortMask = m_grpDeviceDX11.GetNativeWorldMissingPortMask();
			const uint32_t uWorldObservedMask = (m_grpDeviceDX11.GetNativeWorldObservedMask() & CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK);
			const uint32_t uWorldSubmittedMask = (m_grpDeviceDX11.GetNativeWorldSubmittedMask() & CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK);
			const uint32_t uWorldApplicableMask = (m_grpDeviceDX11.GetNativeWorldApplicableMask() & CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK);
			const uint32_t uWorldCommittedMask = (m_grpDeviceDX11.GetNativeWorldPortMask() & CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK);
			const CLightManager::SLightTelemetry& rkLightTelemetry = CLightManager::Instance().GetTelemetry();
			const CPythonBackground::SDX11WorldSubmitTelemetry& rkWorldSubmitTelemetry =
				CPythonBackground::Instance().GetDX11WorldSubmitTelemetry();
			uint32_t uNoRTVWithPSCount = 0u;
			uint32_t uNoRTVWithPSIndexedCount = 0u;
			uint32_t uNoRTVWithPSNonIndexedCount = 0u;
			uint32_t uNoRTVWithPSLastTopology = 0u;
			uint32_t uNoRTVWithPSLastElements = 0u;
			uint32_t uNoRTVWithPSLastDepthBound = 0u;
			uint32_t uUnsupportedRenderStateCount = 0u;
			uint32_t uUnsupportedRenderStateLastType = 0u;
			uint32_t uUnsupportedRenderStateLastValue = 0u;
			uint32_t uFogEnable = 0u;
			uint32_t uFogMode = 0u;
			uint32_t uFogRangeEnable = 0u;
			uint32_t uFogColor = 0u;
			uint32_t uFogDensity = 0u;
			uint32_t uFogStart = 0u;
			uint32_t uFogEnd = 0u;
			uint32_t uVSConstClampCount = 0u;
			uint32_t uVSConstClampLastRegister = 0u;
			uint32_t uVSConstClampLastRequested = 0u;
			uint32_t uVSConstClampLastApplied = 0u;
			uint32_t uVSConstSetCallCount = 0u;
			uint32_t uVSConstSetRegisterCount = 0u;
			uint32_t uVSConstSetLastRegister = 0u;
			uint32_t uVSConstSetLastCount = 0u;
			uint32_t uVSConstUploadCount = 0u;
			uint32_t uVSConstUploadBytes = 0u;
			uint32_t uVSConstUploadStartRegister = 0u;
			uint32_t uVSConstUploadEndRegister = 0u;
			uint32_t uPSConstClampCount = 0u;
			uint32_t uPSConstClampLastRegister = 0u;
			uint32_t uPSConstClampLastRequested = 0u;
			uint32_t uPSConstClampLastApplied = 0u;
			uint32_t uPSConstSetCallCount = 0u;
			uint32_t uPSConstSetRegisterCount = 0u;
			uint32_t uPSConstSetLastRegister = 0u;
			uint32_t uPSConstSetLastCount = 0u;
			uint32_t uPSConstUploadCount = 0u;
			uint32_t uPSConstUploadBytes = 0u;
			uint32_t uPSConstUploadStartRegister = 0u;
			uint32_t uPSConstUploadEndRegister = 0u;
			const uint32_t uWorldSubmitMismatchCount = m_dwDX11WorldSubmitMaskMismatchCount;
			const uint32_t uWorldSubmitMismatchActive = m_bDX11WorldSubmitMaskMismatchActive ? 1u : 0u;
			const uint32_t uWorldSubmitMismatchTelemetryObserved = m_uDX11WorldSubmitMaskMismatchTelemetryObserved;
			const uint32_t uWorldSubmitMismatchTelemetrySubmitted = m_uDX11WorldSubmitMaskMismatchTelemetrySubmitted;
			const uint32_t uWorldSubmitMismatchTelemetryApplicable = m_uDX11WorldSubmitMaskMismatchTelemetryApplicable;
			const uint32_t uWorldSubmitMismatchTelemetryCommitted = m_uDX11WorldSubmitMaskMismatchTelemetryCommitted;
			const uint32_t uWorldSubmitMismatchGateObserved = m_uDX11WorldSubmitMaskMismatchGateObserved;
			const uint32_t uWorldSubmitMismatchGateSubmitted = m_uDX11WorldSubmitMaskMismatchGateSubmitted;
			const uint32_t uWorldSubmitMismatchGateApplicable = m_uDX11WorldSubmitMaskMismatchGateApplicable;
			const uint32_t uWorldSubmitMismatchGateCommitted = m_uDX11WorldSubmitMaskMismatchGateCommitted;
			const uint32_t uWorldSubmitMismatchLastReasonMask = m_uDX11WorldSubmitMaskMismatchLastReasonMask;
			const uint32_t uWorldSubmitMismatchLastPhaseActive = m_uDX11WorldSubmitMaskMismatchLastPhaseActive;
			const uint32_t uWorldSubmitMismatchLastFrame = m_dwDX11WorldSubmitMaskMismatchLastFrame;
			const uint32_t uWorldSubmitMismatchLastElapsedMS = m_dwDX11WorldSubmitMaskMismatchLastElapsedMS;
			if (CStateManager* pStateManager = CStateManager::InstancePtr())
			{
				const CStateManager::SDebugDrawDiagnostics& rkStateDrawDiag = pStateManager->GetDebugDrawDiagnostics();
				uNoRTVWithPSCount = rkStateDrawDiag.uNoRTVWithPSCount;
				uNoRTVWithPSIndexedCount = rkStateDrawDiag.uNoRTVWithPSIndexedCount;
				uNoRTVWithPSNonIndexedCount = rkStateDrawDiag.uNoRTVWithPSNonIndexedCount;
				uNoRTVWithPSLastTopology = rkStateDrawDiag.uNoRTVWithPSLastTopology;
				uNoRTVWithPSLastElements = rkStateDrawDiag.uNoRTVWithPSLastElements;
				uNoRTVWithPSLastDepthBound = rkStateDrawDiag.uNoRTVWithPSLastDepthBound;
				uUnsupportedRenderStateCount = rkStateDrawDiag.uUnsupportedRenderStateCount;
				uUnsupportedRenderStateLastType = rkStateDrawDiag.uUnsupportedRenderStateLastType;
				uUnsupportedRenderStateLastValue = rkStateDrawDiag.uUnsupportedRenderStateLastValue;
				uFogEnable = rkStateDrawDiag.uFogEnable;
				uFogMode = rkStateDrawDiag.uFogMode;
				uFogRangeEnable = rkStateDrawDiag.uFogRangeEnable;
				uFogColor = rkStateDrawDiag.uFogColor;
				uFogDensity = rkStateDrawDiag.uFogDensity;
				uFogStart = rkStateDrawDiag.uFogStart;
				uFogEnd = rkStateDrawDiag.uFogEnd;
				uVSConstClampCount = rkStateDrawDiag.uVSConstClampCount;
				uVSConstClampLastRegister = rkStateDrawDiag.uVSConstClampLastRegister;
				uVSConstClampLastRequested = rkStateDrawDiag.uVSConstClampLastRequested;
				uVSConstClampLastApplied = rkStateDrawDiag.uVSConstClampLastApplied;
				uVSConstSetCallCount = rkStateDrawDiag.uVSConstSetCallCount;
				uVSConstSetRegisterCount = rkStateDrawDiag.uVSConstSetRegisterCount;
				uVSConstSetLastRegister = rkStateDrawDiag.uVSConstSetLastRegister;
				uVSConstSetLastCount = rkStateDrawDiag.uVSConstSetLastCount;
				uVSConstUploadCount = rkStateDrawDiag.uVSConstUploadCount;
				uVSConstUploadBytes = rkStateDrawDiag.uVSConstUploadBytes;
				uVSConstUploadStartRegister = rkStateDrawDiag.uVSConstUploadStartRegister;
				uVSConstUploadEndRegister = rkStateDrawDiag.uVSConstUploadEndRegister;
				uPSConstClampCount = rkStateDrawDiag.uPSConstClampCount;
				uPSConstClampLastRegister = rkStateDrawDiag.uPSConstClampLastRegister;
				uPSConstClampLastRequested = rkStateDrawDiag.uPSConstClampLastRequested;
				uPSConstClampLastApplied = rkStateDrawDiag.uPSConstClampLastApplied;
				uPSConstSetCallCount = rkStateDrawDiag.uPSConstSetCallCount;
				uPSConstSetRegisterCount = rkStateDrawDiag.uPSConstSetRegisterCount;
				uPSConstSetLastRegister = rkStateDrawDiag.uPSConstSetLastRegister;
				uPSConstSetLastCount = rkStateDrawDiag.uPSConstSetLastCount;
				uPSConstUploadCount = rkStateDrawDiag.uPSConstUploadCount;
				uPSConstUploadBytes = rkStateDrawDiag.uPSConstUploadBytes;
				uPSConstUploadStartRegister = rkStateDrawDiag.uPSConstUploadStartRegister;
				uPSConstUploadEndRegister = rkStateDrawDiag.uPSConstUploadEndRegister;
			}
			const CEffectManager::SDX11TargetRingDiagnostics& rkTargetDiag = m_kEftMgr.GetDX11TargetRingDiagnostics();
			CImGuiMetricsCollector::Instance()->SetDX11Metrics(
				uFeatureLevel,
				uWorldPortMask,
				uMissingPortMask,
				uWorldObservedMask,
				uWorldSubmittedMask,
				uWorldApplicableMask,
				uWorldCommittedMask,
				rkLightTelemetry.dwRegisteredStaticCount,
				rkLightTelemetry.dwRegisteredDynamicCount,
				rkLightTelemetry.dwActiveStaticCount,
				rkLightTelemetry.dwActiveDynamicCount,
				rkLightTelemetry.dwRequestedActiveCount,
				rkLightTelemetry.dwBoundActiveCount,
				rkLightTelemetry.dwClippedBySlotCount,
				rkLightTelemetry.dwSlotCapacity,
				rkLightTelemetry.dwSkipIndex,
				static_cast<uint32_t>(std::max(0, rkWorldSubmitTelemetry.iTerrainPatches)),
				static_cast<uint32_t>(std::max(0, rkWorldSubmitTelemetry.iTerrainSplats)),
				static_cast<uint32_t>(std::max(0, rkWorldSubmitTelemetry.iWaterPatches)),
				rkWorldSubmitTelemetry.dwObjectSubmitted,
				rkWorldSubmitTelemetry.dwEffectSubmitted,
				rkWorldSubmitTelemetry.dwEffectParticleSubmitted,
				rkWorldSubmitTelemetry.dwEffectMeshSubmitted,
				rkTargetDiag.dwActiveInstanceCount,
				rkTargetDiag.dwSubmittedCount,
				rkTargetDiag.dwSkippedCount,
				rkTargetDiag.dwLastEffectCRC,
				rkTargetDiag.dwLastTimestampMS,
				rkTargetDiag.dwLastBlendState,
				rkTargetDiag.dwLastPipelineFlags,
				m_kEftMgr.GetDX11TargetRingAlphaClipThreshold(),
				rkTargetDiag.szLastReason,
				rkWorldSubmitTelemetry.dwSpeedTreeSubmitted,
				uNoRTVWithPSCount,
				uNoRTVWithPSIndexedCount,
				uNoRTVWithPSNonIndexedCount,
				uNoRTVWithPSLastTopology,
				uNoRTVWithPSLastElements,
				uNoRTVWithPSLastDepthBound,
				uUnsupportedRenderStateCount,
				uUnsupportedRenderStateLastType,
				uUnsupportedRenderStateLastValue,
				uFogEnable,
				uFogMode,
				uFogRangeEnable,
				uFogColor,
				uFogDensity,
				uFogStart,
				uFogEnd,
				uVSConstClampCount,
				uVSConstClampLastRegister,
				uVSConstClampLastRequested,
				uVSConstClampLastApplied,
				uVSConstSetCallCount,
				uVSConstSetRegisterCount,
				uVSConstSetLastRegister,
				uVSConstSetLastCount,
				uVSConstUploadCount,
				uVSConstUploadBytes,
				uVSConstUploadStartRegister,
				uVSConstUploadEndRegister,
				uPSConstClampCount,
				uPSConstClampLastRegister,
				uPSConstClampLastRequested,
				uPSConstClampLastApplied,
				uPSConstSetCallCount,
				uPSConstSetRegisterCount,
				uPSConstSetLastRegister,
				uPSConstSetLastCount,
				uPSConstUploadCount,
				uPSConstUploadBytes,
				uPSConstUploadStartRegister,
				uPSConstUploadEndRegister,
				uWorldSubmitMismatchCount,
				uWorldSubmitMismatchActive,
				uWorldSubmitMismatchTelemetryObserved,
				uWorldSubmitMismatchTelemetrySubmitted,
				uWorldSubmitMismatchTelemetryApplicable,
				uWorldSubmitMismatchTelemetryCommitted,
				uWorldSubmitMismatchGateObserved,
				uWorldSubmitMismatchGateSubmitted,
				uWorldSubmitMismatchGateApplicable,
				uWorldSubmitMismatchGateCommitted);

			UINT uEffectiveDrawCalls = uDeviceDrawCalls;
			UINT uEffectivePrimitives = uDevicePrimitives;
			UINT uEffectiveVertexCount = (uDevicePrimitives * 3u);
			UINT uSubsystemDrawCalls = 0;
			UINT uSubsystemPrimitives = 0;
			if (CImGuiGraphicsMetrics::Instance())
			{
				const SSubsystemStats& rkSubsystemStats = CImGuiGraphicsMetrics::Instance()->GetSubsystemStats();
				uSubsystemDrawCalls = rkSubsystemStats.GetTotalDrawCalls();
				const UINT64 ullSubsystemPrimitives = rkSubsystemStats.GetTotalPrimitives();
				uSubsystemPrimitives = static_cast<UINT>(std::min<UINT64>(ullSubsystemPrimitives, 0xffffffffull));

				if (uSubsystemDrawCalls > uEffectiveDrawCalls)
					uEffectiveDrawCalls = uSubsystemDrawCalls;
				if (uSubsystemPrimitives > uEffectivePrimitives)
					uEffectivePrimitives = uSubsystemPrimitives;
				const UINT64 ullVertices = static_cast<UINT64>(uEffectivePrimitives) * 3ull;
				uEffectiveVertexCount = static_cast<UINT>(std::min<UINT64>(ullVertices, 0xffffffffull));
			}

			// Only update if we have valid data
			if (fFPS > 0.0f && fFrameTime > 0.0f)
			{
				CImGuiMetricsCollector::Instance()->Update(
					fFrameTime,
					fFPS,
					uEffectiveDrawCalls,
					uEffectivePrimitives,
					uEffectiveVertexCount,
					uEntityCount,
					uWorldPassCount);

				static DWORD s_dwDX11ImGuiMetricsParityLogTick = 0;
				const DWORD dwNow = ELTimer_GetMSec();
				if (0 == s_dwDX11ImGuiMetricsParityLogTick || dwNow - s_dwDX11ImGuiMetricsParityLogTick >= 5000u)
				{
					s_dwDX11ImGuiMetricsParityLogTick = dwNow;
					TraceError(
						"DX11_IMGUI_METRICS_PARITY draw_device=%u draw_subsystems=%u draw_effective=%u prim_device=%u prim_subsystems=%u prim_effective=%u entities=%u world_passes=%u submitted_mask=0x%02X observed_mask=0x%02X applicable_mask=0x%02X committed_mask=0x%02X terrain=%u splats=%u water=%u objects=%u effects=%u effects_particle=%u effects_mesh=%u speedtree=%u light_req=%u light_bound=%u light_clip=%u light_cap=%u light_skip=%u no_rtv_ps=%u no_rtv_ps_idx=%u no_rtv_ps_non=%u no_rtv_ps_last_topo=%u no_rtv_ps_last_elem=%u no_rtv_ps_last_depth=%u unsupported_rs=%u unsupported_rs_last_type=%u unsupported_rs_last_value=%u fog_en=%u fog_mode=%u fog_range=%u fog_color=0x%08X fog_density=0x%08X fog_start=0x%08X fog_end=0x%08X vs_const_clamp=%u vs_const_last=%u/%u/%u vs_const_set=%u/%u/%u/%u vs_const_upload=%u/%u/%u-%u ps_const_clamp=%u ps_const_last=%u/%u/%u ps_const_set=%u/%u/%u/%u ps_const_upload=%u/%u/%u-%u world_mask_mismatch_cnt=%u world_mask_mismatch_active=%u world_mask_mismatch_tele=0x%02X/0x%02X/0x%02X/0x%02X world_mask_mismatch_gate=0x%02X/0x%02X/0x%02X/0x%02X world_mask_mismatch_last=0x%02X/%u/%u/%u",
						uDeviceDrawCalls,
						uSubsystemDrawCalls,
						uEffectiveDrawCalls,
						uDevicePrimitives,
						uSubsystemPrimitives,
						uEffectivePrimitives,
						uEntityCount,
						uWorldPassCount,
						static_cast<unsigned int>(dwSubmittedMask),
						static_cast<unsigned int>(uWorldObservedMask),
						static_cast<unsigned int>(uWorldApplicableMask),
						static_cast<unsigned int>(uWorldCommittedMask),
						static_cast<unsigned int>(std::max(0, rkWorldSubmitTelemetry.iTerrainPatches)),
						static_cast<unsigned int>(std::max(0, rkWorldSubmitTelemetry.iTerrainSplats)),
						static_cast<unsigned int>(std::max(0, rkWorldSubmitTelemetry.iWaterPatches)),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwObjectSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwEffectSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwEffectParticleSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwEffectMeshSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwSpeedTreeSubmitted),
						static_cast<unsigned int>(rkLightTelemetry.dwRequestedActiveCount),
						static_cast<unsigned int>(rkLightTelemetry.dwBoundActiveCount),
						static_cast<unsigned int>(rkLightTelemetry.dwClippedBySlotCount),
						static_cast<unsigned int>(rkLightTelemetry.dwSlotCapacity),
						static_cast<unsigned int>(rkLightTelemetry.dwSkipIndex),
						static_cast<unsigned int>(uNoRTVWithPSCount),
						static_cast<unsigned int>(uNoRTVWithPSIndexedCount),
						static_cast<unsigned int>(uNoRTVWithPSNonIndexedCount),
						static_cast<unsigned int>(uNoRTVWithPSLastTopology),
						static_cast<unsigned int>(uNoRTVWithPSLastElements),
						static_cast<unsigned int>(uNoRTVWithPSLastDepthBound),
						static_cast<unsigned int>(uUnsupportedRenderStateCount),
						static_cast<unsigned int>(uUnsupportedRenderStateLastType),
						static_cast<unsigned int>(uUnsupportedRenderStateLastValue),
						static_cast<unsigned int>(uFogEnable),
						static_cast<unsigned int>(uFogMode),
						static_cast<unsigned int>(uFogRangeEnable),
						static_cast<unsigned int>(uFogColor),
						static_cast<unsigned int>(uFogDensity),
						static_cast<unsigned int>(uFogStart),
						static_cast<unsigned int>(uFogEnd),
						static_cast<unsigned int>(uVSConstClampCount),
						static_cast<unsigned int>(uVSConstClampLastRegister),
						static_cast<unsigned int>(uVSConstClampLastRequested),
						static_cast<unsigned int>(uVSConstClampLastApplied),
						static_cast<unsigned int>(uVSConstSetCallCount),
						static_cast<unsigned int>(uVSConstSetRegisterCount),
						static_cast<unsigned int>(uVSConstSetLastRegister),
						static_cast<unsigned int>(uVSConstSetLastCount),
						static_cast<unsigned int>(uVSConstUploadCount),
						static_cast<unsigned int>(uVSConstUploadBytes),
						static_cast<unsigned int>(uVSConstUploadStartRegister),
						static_cast<unsigned int>(uVSConstUploadEndRegister),
						static_cast<unsigned int>(uPSConstClampCount),
						static_cast<unsigned int>(uPSConstClampLastRegister),
						static_cast<unsigned int>(uPSConstClampLastRequested),
						static_cast<unsigned int>(uPSConstClampLastApplied),
						static_cast<unsigned int>(uPSConstSetCallCount),
						static_cast<unsigned int>(uPSConstSetRegisterCount),
						static_cast<unsigned int>(uPSConstSetLastRegister),
						static_cast<unsigned int>(uPSConstSetLastCount),
						static_cast<unsigned int>(uPSConstUploadCount),
						static_cast<unsigned int>(uPSConstUploadBytes),
						static_cast<unsigned int>(uPSConstUploadStartRegister),
						static_cast<unsigned int>(uPSConstUploadEndRegister),
						static_cast<unsigned int>(uWorldSubmitMismatchCount),
						static_cast<unsigned int>(uWorldSubmitMismatchActive),
						static_cast<unsigned int>(uWorldSubmitMismatchTelemetryObserved),
						static_cast<unsigned int>(uWorldSubmitMismatchTelemetrySubmitted),
						static_cast<unsigned int>(uWorldSubmitMismatchTelemetryApplicable),
						static_cast<unsigned int>(uWorldSubmitMismatchTelemetryCommitted),
						static_cast<unsigned int>(uWorldSubmitMismatchGateObserved),
						static_cast<unsigned int>(uWorldSubmitMismatchGateSubmitted),
						static_cast<unsigned int>(uWorldSubmitMismatchGateApplicable),
						static_cast<unsigned int>(uWorldSubmitMismatchGateCommitted),
						static_cast<unsigned int>(uWorldSubmitMismatchLastReasonMask),
						static_cast<unsigned int>(uWorldSubmitMismatchLastPhaseActive),
						static_cast<unsigned int>(uWorldSubmitMismatchLastFrame),
						static_cast<unsigned int>(uWorldSubmitMismatchLastElapsedMS));

					// EffectLib runtime parity heartbeat for migration diagnostics:
					// compares EffectManager local counters/resources against world submit telemetry.
					const uint32_t uEffectActiveCount = m_kEftMgr.GetActiveEffectCount();
					const uint32_t uParticleActiveCount = m_kEftMgr.GetActiveParticleCount();
					const uint32_t uEffectMgrSubmitted = m_kEftMgr.GetDX11SubmittedEffectCount();
					const uint32_t uEffectMgrParticleSubmitted = m_kEftMgr.GetDX11SubmittedParticleCount();
					const uint32_t uEffectMgrMeshSubmitted = m_kEftMgr.GetDX11SubmittedMeshEffectCount();
					const uint32_t uEffectResourcesReady = m_kEftMgr.IsDX11EffectResourcesReady() ? 1u : 0u;
					const uint32_t uEffectDynamicVBCapacity = m_kEftMgr.GetDX11EffectDynamicVBCapacity();
					const long long llDeltaEffects =
						static_cast<long long>(uEffectMgrSubmitted) -
						static_cast<long long>(rkWorldSubmitTelemetry.dwEffectSubmitted);
					const long long llDeltaParticle =
						static_cast<long long>(uEffectMgrParticleSubmitted) -
						static_cast<long long>(rkWorldSubmitTelemetry.dwEffectParticleSubmitted);
					const long long llDeltaMesh =
						static_cast<long long>(uEffectMgrMeshSubmitted) -
						static_cast<long long>(rkWorldSubmitTelemetry.dwEffectMeshSubmitted);
					TraceError(
						"DX11_EFFECT_RUNTIME_PARITY active_effects=%u active_particles=%u resources_ready=%u dynamic_vb_capacity=%u mgr_effects=%u mgr_particle=%u mgr_mesh=%u world_effects=%u world_particle=%u world_mesh=%u delta_effects=%lld delta_particle=%lld delta_mesh=%lld",
						static_cast<unsigned int>(uEffectActiveCount),
						static_cast<unsigned int>(uParticleActiveCount),
						static_cast<unsigned int>(uEffectResourcesReady),
						static_cast<unsigned int>(uEffectDynamicVBCapacity),
						static_cast<unsigned int>(uEffectMgrSubmitted),
						static_cast<unsigned int>(uEffectMgrParticleSubmitted),
						static_cast<unsigned int>(uEffectMgrMeshSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwEffectSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwEffectParticleSubmitted),
						static_cast<unsigned int>(rkWorldSubmitTelemetry.dwEffectMeshSubmitted),
						llDeltaEffects,
						llDeltaParticle,
						llDeltaMesh);

					TraceError(
						"DX11_TARGETFX_RUNTIME active=%u submit=%u skip=%u last_crc=0x%08X last_ts=%u blend=0x%08X pipeline=0x%08X alpha_clip=%.6f reason=%s",
						static_cast<unsigned int>(rkTargetDiag.dwActiveInstanceCount),
						static_cast<unsigned int>(rkTargetDiag.dwSubmittedCount),
						static_cast<unsigned int>(rkTargetDiag.dwSkippedCount),
						static_cast<unsigned int>(rkTargetDiag.dwLastEffectCRC),
						static_cast<unsigned int>(rkTargetDiag.dwLastTimestampMS),
						static_cast<unsigned int>(rkTargetDiag.dwLastBlendState),
						static_cast<unsigned int>(rkTargetDiag.dwLastPipelineFlags),
						static_cast<double>(m_kEftMgr.GetDX11TargetRingAlphaClipThreshold()),
						rkTargetDiag.szLastReason[0] ? rkTargetDiag.szLastReason : "none");
				}

				// Forward metrics to ImGuiManager for rendering
				if (CImGuiManager::Instance())
				{
					CImGuiManager::Instance()->UpdateMetrics(
						CImGuiMetricsCollector::Instance()->GetCurrentSnapshot()
					);
				}
			}
		}
#endif

		m_dwLastIdleTime = ELTimer_GetMSec();
	}
}

bool LoadLocaleData(const char* localePath)
{
	CPythonNonPlayer&	rkNPCMgr	= CPythonNonPlayer::Instance();
	CItemManager&		rkItemMgr	= CItemManager::Instance();	
	CPythonSkill&		rkSkillMgr	= CPythonSkill::Instance();
	CPythonNetworkStream& rkNetStream = CPythonNetworkStream::Instance();

	char szItemList[256];
	char szItemProto[256];
	char szItemDesc[256];
	char szMobProto[256];
	char szSkillDescFileName[256];
	char szSkillTableFileName[256];
	char szInsultList[256];

	snprintf (szItemList,	sizeof (szItemList),	"%s/item_list.txt", GetLocalePathCommon());
	snprintf (szItemProto,	sizeof (szItemProto),	"%s/item_proto",	localePath);
	snprintf (szItemDesc,	sizeof (szItemDesc),	"%s/itemdesc.txt",	localePath);
	snprintf (szMobProto,	sizeof (szMobProto),	"%s/mob_proto",		localePath);
	snprintf (szSkillDescFileName, sizeof (szSkillDescFileName),	"%s/SkillDesc.txt", localePath);
	snprintf (szSkillTableFileName, sizeof (szSkillTableFileName),	"%s/SkillTable.txt", GetLocalePathCommon());
	snprintf (szInsultList,	sizeof (szInsultList),	"%s/insult.txt", localePath);

	rkNPCMgr.Destroy();
	rkItemMgr.Destroy();
	rkSkillMgr.Destroy();

	if (!rkItemMgr.LoadItemList(szItemList))
	{
		TraceError("LoadLocaleData - LoadItemList(%s) Error", szItemList);
	}

	if (!rkItemMgr.LoadItemTable(szItemProto))
	{
		TraceError("LoadLocaleData - LoadItemProto(%s) Error", szItemProto);
		return false;
	}

	if (!rkItemMgr.LoadItemDesc(szItemDesc))
	{
		Tracenf("LoadLocaleData - LoadItemDesc(%s) Error", szItemDesc);
	}

	if (!rkNPCMgr.LoadNonPlayerData(szMobProto))
	{
		TraceError("LoadLocaleData - LoadMobProto(%s) Error", szMobProto);
		return false;
	}

	if (!rkSkillMgr.RegisterSkillDesc(szSkillDescFileName))
	{
		TraceError("LoadLocaleData - RegisterSkillDesc(%s) Error", szMobProto);
		return false;
	}

	if (!rkSkillMgr.RegisterSkillTable(szSkillTableFileName))
	{
		TraceError("LoadLocaleData - RegisterSkillTable(%s) Error", szMobProto);
		return false;
	}

	if (!rkNetStream.LoadInsultList(szInsultList))
	{
		Tracenf("CPythonApplication - CPythonNetworkStream::LoadInsultList(%s)", szInsultList);
	}

	return true;
}

unsigned __GetWindowMode(bool windowed)
{
	if (windowed)
		return WS_OVERLAPPED | WS_CAPTION |   WS_SYSMENU | WS_MINIMIZEBOX;

	return WS_POPUP;
}

bool CPythonApplication::Create(PyObject * poSelf, const char * c_szName, int width, int height, int Windowed)
{
	// Initialize Game Thread Pool first - required by other systems
	CGameThreadPool* pThreadPool = CGameThreadPool::InstancePtr();
	if (pThreadPool)
	{
		pThreadPool->Initialize();
	}

	NANOBEGIN
		Windowed = CPythonSystem::Instance().IsWindowed() ? 1 : 0;

	bool bAnotherWindow = false;

	std::wstring wWindowName = Utf8ToWide(c_szName ? c_szName : "");

	if (FindWindowW(nullptr, wWindowName.c_str()))
		bAnotherWindow = true;

	m_dwWidth = width;
	m_dwHeight = height;

	// Window
	UINT WindowMode = __GetWindowMode(Windowed ? true : false);

	if (!CMSWindow::Create(c_szName, 4, 0, WindowMode, ::LoadIcon( GetInstance(), MAKEINTRESOURCE( IDI_METIN2 ) ), IDC_CURSOR_NORMAL))
	{
		TraceError("CMSWindow::Create failed");
		SET_EXCEPTION(CREATE_WINDOW);
		return false;
	}

	if (m_pySystem.IsUseDefaultIME())
	{
		CPythonIME::Instance().UseDefaultIME();
	}

#if defined(ENABLE_DISCORD_RPC)
	m_pyNetworkStream.Discord_Start();
#endif

	if (!m_pySystem.IsWindowed())
	{
		m_isWindowed = false;
		m_isWindowFullScreenEnable = TRUE;
		__SetFullScreenWindow(GetWindowHandle(), width, height, m_pySystem.GetBPP());

		Windowed = true;
	}
	else
	{
		AdjustSize(m_pySystem.GetWidth(), m_pySystem.GetHeight());

		if (Windowed)
		{
			m_isWindowed = true;

			if (bAnotherWindow)
			{
				RECT rc;

				GetClientRect(&rc);

				int windowWidth = rc.right - rc.left;
				int windowHeight = (rc.bottom - rc.top);

				CMSApplication::SetPosition(GetScreenWidth() - windowWidth, GetScreenHeight() - 60 - windowHeight);
			}
			SetPosition(-8, 0); //Fix
		}
		else
		{
			m_isWindowed = false;
			SetPosition(0, 0);
		}
	}

	NANOEND
		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		// Cursor
		if (!CreateCursors())
		{
			TraceError("CMSWindow::Cursors Create Error");
			SET_EXCEPTION("CREATE_CURSOR");
			return false;
		}

		if (!m_pySystem.IsNoSoundCard())
		{
			// Sound
			if (!m_SoundEngine.Initialize())
			{
				TraceError("Failed to initialize sound manager!");
				return false; // Is this important enough to stop the client?
			}
		}

		extern bool GRAPHICS_CAPS_SOFTWARE_TILING;

		if (!m_pySystem.IsAutoTiling())
			GRAPHICS_CAPS_SOFTWARE_TILING = m_pySystem.IsSoftwareTiling();

		m_iRequestedRenderAPI = m_pySystem.GetRenderAPI();
		m_eRenderBackend = RENDER_BACKEND_DX9;
		m_isRenderBackendFallback = false;
		m_iRenderBackendFallbackReason = RENDER_BACKEND_FALLBACK_NONE;
		m_isDX11ProbeSuccessful = false;
		m_iDX11ProbeFeatureLevel = 0;

#if defined(DX11_STRICT_ONLY)
		m_iRequestedRenderAPI = 11;
		m_eRenderBackend = RENDER_BACKEND_DX11;
		m_isRenderBackendFallback = false;
		m_iRenderBackendFallbackReason = RENDER_BACKEND_FALLBACK_NONE;

		if (!IsWindows10OrGreaterRuntime())
		{
			TraceError("RENDER_API=DX11 strict-only, but OS is lower than Windows 10; startup aborted.");
			return false;
		}

		CGraphicDeviceDX11 kDX11Probe;
		const bool bProbeResult = kDX11Probe.Create(
			GetWindowHandle(),
			m_pySystem.GetWidth(),
			m_pySystem.GetHeight(),
			Windowed ? true : false,
			m_pySystem.IsVSyncEnabled());
		m_iDX11ProbeFeatureLevel = bProbeResult ? ToFeatureLevelInt(kDX11Probe.GetFeatureLevel()) : 0;
		kDX11Probe.Destroy();
		m_isDX11ProbeSuccessful = bProbeResult;

		if (!bProbeResult)
		{
			m_iRenderBackendFallbackReason = RENDER_BACKEND_FALLBACK_DX11_PROBE_FAILED;
			TraceError("RENDER_API=DX11 strict-only, DX11 bootstrap probe failed; startup aborted.");
			return false;
		}

		TraceError("RENDER_API=DX11 strict-only mode enabled; active backend=DX11.");
#else
		if (11 == m_iRequestedRenderAPI)
		{
			const bool bEnableDX11FirstPass = m_pySystem.IsDX11FirstPassActiveEnabled();
			m_isRenderBackendFallback = true;
			const bool bIsWin10OrGreater = IsWindows10OrGreaterRuntime();
			if (!bIsWin10OrGreater)
			{
				m_iRenderBackendFallbackReason = RENDER_BACKEND_FALLBACK_OS_UNSUPPORTED;
				TraceError("RENDER_API=DX11 configured, but OS is lower than Windows 10; using DX9 fallback.");
			}
			else
			{
				CGraphicDeviceDX11 kDX11Probe;
				const bool bProbeResult = kDX11Probe.Create(
					GetWindowHandle(),
					m_pySystem.GetWidth(),
					m_pySystem.GetHeight(),
					Windowed ? true : false,
					m_pySystem.IsVSyncEnabled());
				m_iDX11ProbeFeatureLevel = bProbeResult ? ToFeatureLevelInt(kDX11Probe.GetFeatureLevel()) : 0;
				kDX11Probe.Destroy();

				m_isDX11ProbeSuccessful = bProbeResult;

				if (bProbeResult)
				{
					m_eRenderBackend = RENDER_BACKEND_DX11;
					m_isRenderBackendFallback = false;
					m_iRenderBackendFallbackReason = RENDER_BACKEND_FALLBACK_NONE;
					if (bEnableDX11FirstPass)
						TraceError("RENDER_API=DX11 configured, DX11 first-pass mode enabled; active backend=DX11 with DX9 visible compatibility path.");
					else
						TraceError("RENDER_API=DX11 configured, DX11 first-pass mode disabled; active backend=DX11 cutover runtime path.");

					// DX11 Native Visible Config Verification
					TraceError("DX11_CONFIG_FLAGS first_pass=%d native_visible=%d native_ui_minimal=%d native_world_minimal=%d native_world_autogate=%d strict_native_only=%d",
						m_pySystem.IsDX11FirstPassActiveEnabled() ? 1 : 0,
						m_pySystem.IsDX11NativeVisibleEnabled() ? 1 : 0,
						m_pySystem.IsDX11NativeUIMinimalEnabled() ? 1 : 0,
						m_pySystem.IsDX11NativeWorldMinimalEnabled() ? 1 : 0,
						m_pySystem.IsDX11NativeWorldAutoGateEnabled() ? 1 : 0,
						m_pySystem.IsDX11StrictNativeOnlyEnabled() ? 1 : 0);

					if (m_pySystem.IsDX11StrictNativeOnlyEnabled() && !m_pySystem.IsDX11NativeVisibleEnabled())
					{
						TraceError("DX11_CONFIG_CONFLICT key=DX11_NATIVE_VISIBLE value=0 strict_native_only=1 note=visibility_path_can_be_blocked");
					}
				}
				else
				{
					m_iRenderBackendFallbackReason = RENDER_BACKEND_FALLBACK_DX11_PROBE_FAILED;
					TraceError("RENDER_API=DX11 configured, but DX11 bootstrap probe failed; using DX9 fallback.");
				}
			}
		}
#endif

		// Device
		if (!CreateDevice(m_pySystem.GetWidth(), m_pySystem.GetHeight(), Windowed, m_pySystem.GetBPP(), m_pySystem.GetFrequency()))
			return false;

#if !defined(DX11_STRICT_ONLY)
		if (m_eRenderBackend == RENDER_BACKEND_DX9 && m_iRequestedRenderAPI == 11 && m_isDX11ProbeSuccessful)
		{
			if (!m_grpDeviceDX11.Create(
				GetWindowHandle(),
				static_cast<UINT>(m_pySystem.GetWidth()),
				static_cast<UINT>(m_pySystem.GetHeight()),
				Windowed ? true : false,
				m_pySystem.IsVSyncEnabled()))
			{
				m_iRenderBackendFallbackReason = RENDER_BACKEND_FALLBACK_DX11_BOOTSTRAP_CREATE_FAILED;
				TraceError("DX11 bootstrap persistent device creation failed; continuing with DX9.");
			}
			else
			{
				if (!m_kEftMgr.InitializeDX11EffectResources(m_grpDeviceDX11.GetDevice()))
				{
					TraceError("DX11_EFFECT_RESOURCES init failed after DX11 bootstrap persistent device create.");
				}
				TraceError("DX11 bootstrap persistent device is active in background (DX9 render fallback).");
			}
		}
#endif

		if (m_eRenderBackend == RENDER_BACKEND_DX11)
			m_isVSyncEnabled = m_grpDeviceDX11.IsVSyncEnabled();
		else
			m_isVSyncEnabled = m_grpDevice.IsVSyncEnabled();

		TraceError(
			"RENDER_API_BOOTSTRAP requested=%s active=%s fallback=%d fallback_reason=%s dx11_probe=%d dx11_probe_fl=%d dx11_bootstrap=%d dx11_first_pass_cfg=%d dx11_native_visible_cfg=%d dx11_visible_bootstrap_cfg=%d dx11_ui_pass_only_cfg=%d dx11_ui_native_test_cfg=%d dx11_ui_texture_test_cfg=%d dx11_world_depth_test_cfg=%d dx11_world_batch_test_cfg=%d dx11_world_sprite_test_cfg=%d dx11_world_state_test_cfg=%d dx11_world_passes_test_cfg=%d dx11_world_bridge_test_cfg=%d dx11_world_subsystem_test_cfg=%d dx11_world_realtime_test_cfg=%d dx11_world_metrics_test_cfg=%d dx11_world_instance_feed_test_cfg=%d dx11_world_finalcheck_test_cfg=%d dx11_world_handoff_test_cfg=%d dx11_world_swapchain_test_cfg=%d dx11_world_presentpath_test_cfg=%d dx11_world_visible_pass1_test_cfg=%d dx11_world_composer_test_cfg=%d dx11_world_scenegraph_test_cfg=%d dx11_world_pipeline_test_cfg=%d dx11_world_framegraph_test_cfg=%d",
			GetRequestedRenderBackend(),
			GetRenderBackend(),
			IsRenderBackendFallback(),
			GetRenderBackendFallbackReason(),
			GetDX11ProbeResult(),
			GetDX11ProbeFeatureLevel(),
			IsDX11BootstrapActive(),
			m_pySystem.IsDX11FirstPassActiveEnabled() ? 1 : 0,
			m_pySystem.IsDX11NativeVisibleEnabled() ? 1 : 0,
			m_pySystem.IsDX11VisibleBootstrapEnabled() ? 1 : 0,
			m_pySystem.IsDX11UIPassOnlyEnabled() ? 1 : 0,
			m_pySystem.IsDX11UINativeTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11UITextureTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldDepthTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldBatchTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldSpriteTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldStateTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldPassesTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldBridgeTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldSubsystemTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldRealtimeTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldMetricsTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldInstanceFeedTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldFinalcheckTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldHandoffTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldSwapchainTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldPresentPathTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldVisiblePass1TestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldComposerTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldScenegraphTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldPipelineTestEnabled() ? 1 : 0,
			m_pySystem.IsDX11WorldFramegraphTestEnabled() ? 1 : 0);
		TraceError(
			"RENDER_API_BOOTSTRAP_NATIVE dx11_native_ui_minimal_cfg=%d dx11_native_world_minimal_cfg=%d",
			m_pySystem.IsDX11NativeUIMinimalEnabled() ? 1 : 0,
			m_pySystem.IsDX11NativeWorldMinimalEnabled() ? 1 : 0);
		TraceError(
			"RENDER_API_BOOTSTRAP_NATIVE dx11_native_world_force_visible_cfg=%d",
			m_pySystem.IsDX11NativeWorldForceVisibleEnabled() ? 1 : 0);

		SetFPS(m_pySystem.GetRenderFPSLimit());
		if (!SetVSync(m_pySystem.IsVSyncEnabled()))
		{
			TraceError("Failed to apply VSync setting, using current device default.");
		}
		SetDX11ExperimentalPresent(m_pySystem.IsDX11ExperimentalPresentEnabled());
		const DWORD dwDX11FullBlockMask = GetDX11FullRenderBlockMask();
		TraceError(
			"RENDER_API_DIAG requested=%s active=%s fallback=%d fallback_reason_code=%d fallback_reason=%s dx11_probe=%d dx11_probe_fl=%d dx11_bootstrap=%d dx11_first_pass_cfg=%d dx11_native_visible=%d dx11_visible_bootstrap_cfg=%d dx11_ui_pass_only=%d dx11_ui_native_test=%d dx11_ui_texture_test=%d dx11_world_depth_test=%d dx11_world_batch_test=%d dx11_world_sprite_test=%d dx11_world_state_test=%d dx11_world_passes_test=%d dx11_world_bridge_test=%d dx11_world_subsystem_test=%d",
			GetRequestedRenderBackend(),
			GetRenderBackend(),
			IsRenderBackendFallback(),
			GetRenderBackendFallbackReasonCode(),
			GetRenderBackendFallbackReason(),
			GetDX11ProbeResult(),
			GetDX11ProbeFeatureLevel(),
			IsDX11BootstrapActive(),
			GetDX11FirstPassActive(),
			m_pySystem.IsDX11NativeVisibleEnabled() ? 1 : 0,
			GetDX11VisibleBootstrap(),
			GetDX11UIPassOnly(),
			GetDX11UINativeTest(),
			GetDX11UITextureTest(),
			GetDX11WorldDepthTest(),
			GetDX11WorldBatchTest(),
			GetDX11WorldSpriteTest(),
			GetDX11WorldStateTest(),
			GetDX11WorldPassesTest(),
			GetDX11WorldBridgeTest(),
			GetDX11WorldSubsystemTest());
		TraceError(
			"RENDER_API_DIAG_NATIVE dx11_native_ui_minimal=%d dx11_native_world_minimal=%d",
			m_pySystem.IsDX11NativeUIMinimalEnabled() ? 1 : 0,
			m_pySystem.IsDX11NativeWorldMinimalEnabled() ? 1 : 0);
		TraceError(
			"RENDER_API_DIAG_NATIVE dx11_native_world_force_visible=%d",
			m_pySystem.IsDX11NativeWorldForceVisibleEnabled() ? 1 : 0);
		TraceError(
			"RENDER_API_DIAG dx11_world_realtime_test=%d dx11_world_metrics_test=%d dx11_world_instance_feed_test=%d dx11_world_finalcheck_test=%d dx11_world_handoff_test=%d dx11_world_swapchain_test=%d dx11_world_presentpath_test=%d dx11_world_visible_pass1_test=%d dx11_world_composer_test=%d dx11_world_scenegraph_test=%d dx11_world_pipeline_test=%d dx11_world_framegraph_test=%d dx11_exp_present=%d dx11_stage=%s",
			GetDX11WorldRealtimeTest(),
			GetDX11WorldMetricsTest(),
			GetDX11WorldInstanceFeedTest(),
			GetDX11WorldFinalcheckTest(),
			GetDX11WorldHandoffTest(),
			GetDX11WorldSwapchainTest(),
			GetDX11WorldPresentPathTest(),
			m_pySystem.IsDX11WorldVisiblePass1TestEnabled() ? 1 : 0,
			GetDX11WorldComposerTest(),
			GetDX11WorldScenegraphTest(),
			GetDX11WorldPipelineTest(),
			GetDX11WorldFramegraphTest(),
			GetDX11ExperimentalPresent(),
			GetDX11RuntimeStage());
		TraceError(
			"RENDER_API_DIAG dx11_visible_pass1_success=%u dx11_visible_pass1_fail=%u dx11_visible_pass1_interval=%u dx11_full_phase=%s dx11_full_remaining=%d dx11_full_block_mask=0x%02X dx11_full_block_summary=%s dx11_grace_active=%d dx11_grace_reason=%s dx11_grace_used=%u dx11_grace_expired=%u dx11_grace_coalesced=%u dx11_grace_suppressed=%u",
			m_dwDX11VisiblePass1SuccessCount,
			m_dwDX11VisiblePass1FailCount,
			m_dwDX11VisiblePass1LastIntervalFrames,
			GetDX11FullRenderPhase(),
			GetDX11FullRenderRemainingMajorStages(),
			static_cast<unsigned int>(dwDX11FullBlockMask),
			GetDX11FullRenderBlockSummary(),
			m_bDX11RuntimeCompatGraceMode ? 1 : 0,
			GetDX11RuntimeCompatGraceReason(),
			m_dwDX11RuntimeCompatGraceUsedCount,
			m_dwDX11RuntimeCompatGraceExpiredCount,
			m_dwDX11RuntimeCompatGraceCoalescedCount,
			m_dwDX11RuntimeCompatGraceSuppressedCount);
		SetTextTailOptRange(m_pySystem.GetTextTailOptRange());
		ApplyPerformanceConfig(
			m_pySystem.GetPerfProfile(),
			m_pySystem.IsFXAdaptiveEnabled(),
			m_pySystem.IsAnimLODEnabled(),
			m_pySystem.IsTextTailOptEnabled(),
			m_pySystem.GetShadowCadence(),
			m_pySystem.GetFXStrideBias(),
			m_pySystem.IsShadowDynamicBoostEnabled(),
			m_pySystem.IsTextTailGridOptEnabled());

		const bool bUseLegacySharedDeformBuffers =
			(m_eRenderBackend == RENDER_BACKEND_DX9) ||
			((m_eRenderBackend == RENDER_BACKEND_DX11) && m_pySystem.IsDX11FirstPassActiveEnabled());
		if (bUseLegacySharedDeformBuffers)
		{
			GrannyCreateSharedDeformBuffer();
		}
		else
		{
			TraceError("DX11_CUTOVER_RUNTIME skip_legacy_granny_shared_deform_buffers");
		}

		if (m_pySystem.IsAutoTiling())
		{
			if (m_grpDevice.IsFastTNL())
			{
				m_pyBackground.ReserveSoftwareTilingEnable(false);
			}
			else
			{
				m_pyBackground.ReserveSoftwareTilingEnable(true);
			}
		}
		else
		{
			m_pyBackground.ReserveSoftwareTilingEnable(m_pySystem.IsSoftwareTiling());
		}

		SetVisibleMode(true);

		if (m_isWindowFullScreenEnable)
		{
			SetWindowPos(GetWindowHandle(), HWND_TOP, 0, 0, width, height, SWP_SHOWWINDOW);
		}

		if (!InitializeKeyboard(GetWindowHandle()))
			return false;

		m_pySystem.GetDisplaySettings();

		// Mouse
		if (m_pySystem.IsSoftwareCursor())
			SetCursorMode(CURSOR_MODE_SOFTWARE);
		else
			SetCursorMode(CURSOR_MODE_HARDWARE);

		// Network
		if (!m_netDevice.Create())
		{
			TraceError("NetDevice::Create failed");
			SET_EXCEPTION("CREATE_NETWORK");
			return false;
		}

		if (!m_grpDevice.IsFastTNL())
			CGrannyLODController::SetMinLODMode(true);

		m_pyItem.Create();

		// Other Modules
		DefaultFont_Startup();

		CPythonIME::Instance().Create(GetWindowHandle());
		CPythonIME::Instance().SetText("", 0);
		CPythonTextTail::Instance().Initialize();

		// Light Manager
		m_LightManager.Initialize();

		CGraphicImageInstance::CreateSystem(32);

		// ????????
		STICKYKEYS sStickKeys;
		memset(&sStickKeys, 0, sizeof(sStickKeys));
		sStickKeys.cbSize = sizeof(sStickKeys);
		SystemParametersInfo( SPI_GETSTICKYKEYS, sizeof(sStickKeys), &sStickKeys, 0 );
		m_dwStickyKeysFlag = sStickKeys.dwFlags;

		// ????????
		sStickKeys.dwFlags &= ~(SKF_AVAILABLE|SKF_HOTKEYACTIVE);
		SystemParametersInfo( SPI_SETSTICKYKEYS, sizeof(sStickKeys), &sStickKeys, 0 );

		// SphereMap
		CGrannyMaterial::CreateSphereMap(0, "d:/ymir work/special/spheremap.jpg");
		CGrannyMaterial::CreateSphereMap(1, "d:/ymir work/special/spheremap01.jpg");
		return true;
}

void CPythonApplication::SetGlobalCenterPosition(int32_t x, int32_t y)
{
	CPythonBackground& rkBG=CPythonBackground::Instance();
	rkBG.GlobalPositionToLocalPosition(x, y);

	float z = CPythonBackground::Instance().GetHeight(x, y);

	CPythonApplication::Instance().SetCenterPosition(x, y, z);
}

void CPythonApplication::SetCenterPosition(float fx, float fy, float fz)
{
	m_v3CenterPosition.x = +fx;
	m_v3CenterPosition.y = -fy;
	m_v3CenterPosition.z = +fz;
}

void CPythonApplication::GetCenterPosition(TPixelPosition * pPixelPosition)
{
	pPixelPosition->x = +m_v3CenterPosition.x;
	pPixelPosition->y = -m_v3CenterPosition.y;
	pPixelPosition->z = +m_v3CenterPosition.z;
}


void CPythonApplication::SetServerTime(time_t tTime)
{
	m_dwStartLocalTime	= ELTimer_GetMSec();
	m_tServerTime		= tTime;
	m_tLocalStartTime	= time(0);
}

time_t CPythonApplication::GetServerTime()
{
	return (ELTimer_GetMSec() - m_dwStartLocalTime) + m_tServerTime;
}

// 2005.03.28 - MALL ???????????????? ???????????????? ???????????? ???????????? ???????????????? time(0) ???????? ????????????????????
//              ???????????? ???????????? ???????????? ???????????? ???????? ???????? ???????? ???????????? ???????????? ????????
time_t CPythonApplication::GetServerTimeStamp()
{
	return (time(0) - m_tLocalStartTime) + m_tServerTime;
}

float CPythonApplication::GetGlobalTime()
{
	return m_fGlobalTime;
}

float CPythonApplication::GetGlobalElapsedTime()
{
	return m_fGlobalElapsedTime;
}

void CPythonApplication::SetFPS(int iFPS)
{
	m_iFPS = __NormalizeFPSLimit(iFPS);
	__UpdateRenderFrameInterval();
	m_dNextRenderTimeMS = static_cast<double>(ELTimer_GetMSec());
}

const char* CPythonApplication::GetRenderBackend() const
{
	switch (m_eRenderBackend)
	{
		case RENDER_BACKEND_DX11:
			return "DX11";
		case RENDER_BACKEND_DX9:
		default:
			return "DX9";
	}
}

const char* CPythonApplication::GetRequestedRenderBackend() const
{
	return (11 == m_iRequestedRenderAPI) ? "DX11" : "DX9";
}

int CPythonApplication::GetDX11FirstPassActive()
{
	return m_pySystem.IsDX11FirstPassActiveEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11VisibleBootstrap()
{
	return m_pySystem.IsDX11VisibleBootstrapEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11UIPassOnly()
{
	return m_pySystem.IsDX11UIPassOnlyEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11UINativeTest()
{
	return m_pySystem.IsDX11UINativeTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11UITextureTest()
{
	return m_pySystem.IsDX11UITextureTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldDepthTest()
{
	return m_pySystem.IsDX11WorldDepthTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldBatchTest()
{
	return m_pySystem.IsDX11WorldBatchTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldSpriteTest()
{
	return m_pySystem.IsDX11WorldSpriteTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldStateTest()
{
	return m_pySystem.IsDX11WorldStateTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldPassesTest()
{
	return m_pySystem.IsDX11WorldPassesTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldBridgeTest()
{
	return m_pySystem.IsDX11WorldBridgeTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldSubsystemTest()
{
	return m_pySystem.IsDX11WorldSubsystemTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldRealtimeTest()
{
	return m_pySystem.IsDX11WorldRealtimeTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldMetricsTest()
{
	return m_pySystem.IsDX11WorldMetricsTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldInstanceFeedTest()
{
	return m_pySystem.IsDX11WorldInstanceFeedTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldFinalcheckTest()
{
	return m_pySystem.IsDX11WorldFinalcheckTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldHandoffTest()
{
	return m_pySystem.IsDX11WorldHandoffTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldSwapchainTest()
{
	return m_pySystem.IsDX11WorldSwapchainTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldPresentPathTest()
{
	return m_pySystem.IsDX11WorldPresentPathTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldComposerTest()
{
	return m_pySystem.IsDX11WorldComposerTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldScenegraphTest()
{
	return m_pySystem.IsDX11WorldScenegraphTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldPipelineTest()
{
	return m_pySystem.IsDX11WorldPipelineTestEnabled() ? 1 : 0;
}

int CPythonApplication::GetDX11WorldFramegraphTest()
{
	return m_pySystem.IsDX11WorldFramegraphTestEnabled() ? 1 : 0;
}

const char* CPythonApplication::GetDX11RuntimeCompatGraceReason() const
{
	return DX11RuntimeCompatGraceReasonToString(m_iDX11RuntimeCompatGraceReasonMask);
}

const char* CPythonApplication::GetDX11FullRenderPhase()
{
	if (m_eRenderBackend != RENDER_BACKEND_DX11)
		return "dx9_legacy";


	if (!m_pySystem.IsDX11FirstPassActiveEnabled())
	{
		const bool bDX11NativeVisibleRequested = m_pySystem.IsDX11NativeVisibleEnabled();
		const bool bDX11NativeVisibleReady =
			m_bDX11RuntimeCompatMode &&
			m_bDX11WorldNativePass16Mode &&
			!m_bDX11RuntimeCompatGraceMode;
		if (!bDX11NativeVisibleRequested)
			return "dx11_cutover_runtime";
		if (bDX11NativeVisibleReady)
			return "dx11_cutover_native_visible";
		return "dx11_cutover_native_pending";
	}

	if (!m_pySystem.IsDX11UINativeTestEnabled())
		return "dx11_hybrid_ui_inactive";

	if (!m_pySystem.IsDX11WorldFinalcheckTestEnabled())
		return "dx11_hybrid_pre_finalcheck";

	if (!m_pySystem.IsDX11UIPassOnlyEnabled())
		return "dx11_hybrid_world_ported";

	if (!m_bDX11RuntimeCompatMode)
		return "dx11_hybrid_runtime_off";

	if (m_bDX11RuntimeCompatGraceMode)
		return "dx11_hybrid_runtime_grace";

	if (m_bDX11WorldHandoffProbeMode)
		return "dx11_hybrid_handoff_probe";

	if (!m_bDX11WorldNativePass16Mode)
		return "dx11_hybrid_pass_ramp";

	if (m_bDX11VisiblePass1AutoDisabled)
		return "dx11_hybrid_visible_rollback";

	if (m_dwDX11VisiblePass1LastIntervalFrames <= 5u && m_dwDX11VisiblePass1SuccessCount >= 1800u)
		return "dx11_hybrid_visible_extreme";

	if (m_dwDX11VisiblePass1LastIntervalFrames <= 10u && m_dwDX11VisiblePass1SuccessCount >= 900u)
		return "dx11_hybrid_visible_ultra";

	if (m_dwDX11VisiblePass1LastIntervalFrames <= 30u && m_dwDX11VisiblePass1SuccessCount >= 120u)
		return "dx11_hybrid_visible_high";

	return "dx11_hybrid_visible_safe";
}

DWORD CPythonApplication::GetDX11FullRenderBlockMask()
{
	if (m_eRenderBackend != RENDER_BACKEND_DX11)
	{
		return DX11_FULL_BLOCK_RUNTIME |
			   DX11_FULL_BLOCK_VISIBLE |
			   DX11_FULL_BLOCK_WORLD |
			   DX11_FULL_BLOCK_UI |
			   DX11_FULL_BLOCK_CUTOVER;
	}


	DWORD dwMask = 0u;
	const bool bRuntimeStabilized =
		(m_bDX11RuntimeCompatMode &&
		 m_bDX11WorldNativePass16Mode &&
		 !m_bDX11RuntimeCompatGraceMode);
	if (!bRuntimeStabilized)
		dwMask |= DX11_FULL_BLOCK_RUNTIME;

	bool bVisiblePathValidated = false;
	if (m_pySystem.IsDX11FirstPassActiveEnabled())
	{
		bVisiblePathValidated =
			(!m_bDX11VisiblePass1AutoDisabled &&
			 m_dwDX11VisiblePass1SuccessCount >= DX11_VISIBLE_BLOCK_SUCCESS_MIN &&
			 m_dwDX11VisiblePass1FailCount == 0u);
	}
	else
	{
		const bool bDX11NativeVisibleRequested = m_pySystem.IsDX11NativeVisibleEnabled();
		bVisiblePathValidated = (!bDX11NativeVisibleRequested) || bRuntimeStabilized;
	}
	if (!bVisiblePathValidated)
		dwMask |= DX11_FULL_BLOCK_VISIBLE;

	if (m_pySystem.IsDX11UIPassOnlyEnabled())
		dwMask |= DX11_FULL_BLOCK_WORLD;

	if (m_pySystem.IsDX11FirstPassActiveEnabled() && !m_pySystem.IsDX11UINativeTestEnabled())
		dwMask |= DX11_FULL_BLOCK_UI;

	if (m_pySystem.IsDX11FirstPassActiveEnabled())
		dwMask |= DX11_FULL_BLOCK_CUTOVER;

	return dwMask;
}

const char* CPythonApplication::GetDX11FullRenderBlockSummary()
{
	const DWORD dwMask = GetDX11FullRenderBlockMask();
	if (dwMask == 0u)
		return "none";

	static std::string s_stSummary;
	s_stSummary.clear();

	if (dwMask & DX11_FULL_BLOCK_RUNTIME)
		s_stSummary += "runtime,";
	if (dwMask & DX11_FULL_BLOCK_VISIBLE)
		s_stSummary += "visible,";
	if (dwMask & DX11_FULL_BLOCK_WORLD)
		s_stSummary += "world,";
	if (dwMask & DX11_FULL_BLOCK_UI)
		s_stSummary += "ui,";
	if (dwMask & DX11_FULL_BLOCK_CUTOVER)
		s_stSummary += "cutover,";

	if (!s_stSummary.empty())
		s_stSummary.erase(s_stSummary.length() - 1u);

	return s_stSummary.c_str();
}

int CPythonApplication::GetDX11FullRenderRemainingMajorStages()
{
	const DWORD dwMask = GetDX11FullRenderBlockMask();
	int iRemaining = 0;
	if (dwMask & DX11_FULL_BLOCK_RUNTIME)
		++iRemaining;
	if (dwMask & DX11_FULL_BLOCK_VISIBLE)
		++iRemaining;
	if (dwMask & DX11_FULL_BLOCK_WORLD)
		iRemaining += 2;
	if (dwMask & DX11_FULL_BLOCK_CUTOVER)
		++iRemaining;

	return iRemaining;
}

const char* CPythonApplication::GetDX11RuntimeStage()
{
	if (m_eRenderBackend != RENDER_BACKEND_DX11)
		return "dx9";

	if (!m_pySystem.IsDX11FirstPassActiveEnabled())
		return "dx11_cutover_runtime";

	if (m_pySystem.IsDX11VisibleBootstrapEnabled())
		return "dx11_visible_bootstrap";

	const bool bUINative = m_pySystem.IsDX11UINativeTestEnabled();
	const bool bWorldFinalcheck = m_pySystem.IsDX11WorldFinalcheckTestEnabled();
	if (bUINative && bWorldFinalcheck && m_bDX11RuntimeCompatMode)
	{
		if (m_bDX11WorldNativePass16Mode && !m_bDX11WorldHandoffProbeMode)
			return "dx11_world_native_stable";
		if (m_bDX11WorldNativePass16Mode)
			return "dx11_world_native_pass16";
		if (m_bDX11WorldNativePass15Mode)
			return "dx11_world_native_pass15";
		if (m_bDX11WorldNativePass14Mode)
			return "dx11_world_native_pass14";
		if (m_bDX11WorldNativePass13Mode)
			return "dx11_world_native_pass13";
		if (m_bDX11WorldNativePass12Mode)
			return "dx11_world_native_pass12";
		if (m_bDX11WorldNativePass11Mode)
			return "dx11_world_native_pass11";
		if (m_bDX11WorldNativePass10Mode)
			return "dx11_world_native_pass10";
		if (m_bDX11WorldNativePass9Mode)
			return "dx11_world_native_pass9";
		if (m_bDX11WorldHandoffProbeMode)
			return "dx11_world_handoff_probe";
		if (m_bDX11WorldNativePass8Mode)
			return "dx11_world_native_pass8";
		if (m_bDX11WorldNativePass7Mode)
			return "dx11_world_native_pass7";
		if (m_bDX11WorldNativePass6Mode)
			return "dx11_world_native_pass6";
		if (m_bDX11WorldNativePass5Mode)
			return "dx11_world_native_pass5";
		if (m_bDX11WorldNativePass4Mode)
			return "dx11_world_native_pass4";
		if (m_bDX11WorldNativePass3Mode)
			return "dx11_world_native_pass3";
		if (m_bDX11WorldNativePass2Mode)
			return "dx11_world_native_pass2";
		if (m_bDX11WorldNativePass1Mode)
			return "dx11_world_native_pass1";
		return "dx11_world_runtime_compat";
	}

	if (bUINative && bWorldFinalcheck)
		return "dx11_world_finalcheck_test";
	if (bUINative && m_pySystem.IsDX11WorldHandoffTestEnabled())
		return "dx11_world_handoff_test";
	if (bUINative && m_pySystem.IsDX11WorldSwapchainTestEnabled())
		return "dx11_world_swapchain_test";
	if (bUINative && m_pySystem.IsDX11WorldPresentPathTestEnabled())
		return "dx11_world_presentpath_test";
	if (bUINative && m_pySystem.IsDX11WorldComposerTestEnabled())
		return "dx11_world_composer_test";
	if (bUINative && m_pySystem.IsDX11WorldScenegraphTestEnabled())
		return "dx11_world_scenegraph_test";
	if (bUINative && m_pySystem.IsDX11WorldPipelineTestEnabled())
		return "dx11_world_pipeline_test";
	if (bUINative && m_pySystem.IsDX11WorldFramegraphTestEnabled())
		return "dx11_world_framegraph_test";
	if (bUINative && m_pySystem.IsDX11WorldInstanceFeedTestEnabled())
		return "dx11_world_instance_feed_test";
	if (bUINative && m_pySystem.IsDX11WorldMetricsTestEnabled())
		return "dx11_world_metrics_test";
	if (bUINative && m_pySystem.IsDX11WorldRealtimeTestEnabled())
		return "dx11_world_realtime_test";
	if (bUINative && m_pySystem.IsDX11WorldSubsystemTestEnabled())
		return "dx11_world_subsystem_test";
	if (bUINative && m_pySystem.IsDX11WorldBridgeTestEnabled())
		return "dx11_world_bridge_test";
	if (bUINative && m_pySystem.IsDX11WorldPassesTestEnabled())
		return "dx11_world_passes_test";
	if (bUINative && m_pySystem.IsDX11WorldStateTestEnabled())
		return "dx11_world_state_test";
	if (bUINative && m_pySystem.IsDX11WorldSpriteTestEnabled())
		return "dx11_world_sprite_test";
	if (bUINative && m_pySystem.IsDX11WorldBatchTestEnabled())
		return "dx11_world_batch_test";
	if (bUINative && m_pySystem.IsDX11WorldDepthTestEnabled())
		return "dx11_world_depth_test";
	if (bUINative && m_pySystem.IsDX11UITextureTestEnabled())
		return "dx11_ui_texture_test";
	if (bUINative)
		return "dx11_ui_native_test";
	if (m_pySystem.IsDX11UIPassOnlyEnabled())
		return "dx11_dx9_compat_ui_only";

	return "dx11_dx9_compat_full";
}

const char* CPythonApplication::GetRenderBackendFallbackReason() const
{
	switch (m_iRenderBackendFallbackReason)
	{
		case RENDER_BACKEND_FALLBACK_DX11_PORT_NOT_ENABLED:
			return "dx11_port_not_enabled";
		case RENDER_BACKEND_FALLBACK_OS_UNSUPPORTED:
			return "os_unsupported";
		case RENDER_BACKEND_FALLBACK_DX11_PROBE_FAILED:
			return "dx11_probe_failed";
		case RENDER_BACKEND_FALLBACK_DX11_BOOTSTRAP_CREATE_FAILED:
			return "dx11_bootstrap_create_failed";
		case RENDER_BACKEND_FALLBACK_NONE:
		default:
			return "none";
	}
}

void CPythonApplication::SetDX11ExperimentalPresent(bool isEnabled)
{
	m_dwDX11ExperimentalPresentFailCount = 0;
	if (m_eRenderBackend == RENDER_BACKEND_DX11)
	{
		m_bDX11ExperimentalPresent = false;
		TraceError("DX11_EXPERIMENTAL_PRESENT request=%d ignored while active renderer is DX11; runtime forced to 0", isEnabled ? 1 : 0);
		return;
	}

	m_bDX11ExperimentalPresent = isEnabled ? true : false;
	TraceError("DX11_EXPERIMENTAL_PRESENT set to %d (DX9 fallback uses Present(TEST) to avoid black flicker)", m_bDX11ExperimentalPresent ? 1 : 0);
}

void CPythonApplication::ApplyPerformanceConfig(int iProfile, bool bFXAdaptive, bool bAnimLOD, bool bTextTailOpt, int iShadowCadence, int iFXStrideBias, bool bShadowDynamicBoost, bool bTextTailGridOpt)
{
	m_iPerfProfile = __NormalizePerfProfile(iProfile);
	m_bFXAdaptive = bFXAdaptive ? true : false;
	m_bAnimLOD = bAnimLOD ? true : false;
	m_bTextTailOpt = bTextTailOpt ? true : false;
	m_iShadowCadence = __NormalizeShadowCadence(iShadowCadence);
	m_iFXStrideBias = __NormalizeFXStrideBias(iFXStrideBias);
	m_bShadowDynamicBoost = bShadowDynamicBoost ? true : false;
	m_bTextTailGridOpt = bTextTailGridOpt ? true : false;
	m_bPerfAutoReduced = false;
	m_dwPerfOverBudgetFrames = 0;
	m_dwPerfUnderBudgetFrames = 0;
	if (!m_bShadowDynamicBoost)
		m_bHasLastShadowCameraEye = false;

	__ApplyPerformanceSettings();
}

void CPythonApplication::SetTextTailOptRange(int iRange)
{
	if (iRange < 1500)
		iRange = 1500;
	else if (iRange > 9000)
		iRange = 9000;

	m_iTextTailOptRange = ((iRange + 50) / 100) * 100;
	m_pyTextTail.SetOptimizationRange(static_cast<float>(m_iTextTailOptRange));
}

void CPythonApplication::GetPerfStats(DWORD& rRenderMS, DWORD& rUpdateMS, DWORD& rActiveEffects, DWORD& rActiveParticles, DWORD& rVisibleTextTails, DWORD& rShadowMS, DWORD& rCharacterMS, DWORD& rMapMS, DWORD& rEffectUpdateMS, DWORD& rEffectRenderMS, DWORD& rTextTailMS, DWORD& rTextTailCollisionChecks) const
{
	rRenderMS = m_dwCurRenderTime;
	rUpdateMS = m_dwCurUpdateTime;
	rActiveEffects = m_dwPerfActiveEffects;
	rActiveParticles = m_dwPerfActiveParticles;
	rVisibleTextTails = m_dwPerfVisibleTextTails;
	rShadowMS = m_dwPerfShadowMS;
	rCharacterMS = m_dwPerfCharacterMS;
	rMapMS = m_dwPerfMapMS;
	rEffectUpdateMS = m_dwPerfEffectUpdateMS;
	rEffectRenderMS = m_dwPerfEffectRenderMS;
	rTextTailMS = m_dwPerfTextTailMS;
	rTextTailCollisionChecks = m_dwPerfTextTailCollisionChecks;
}

bool CPythonApplication::SetVSync(bool isEnabled)
{
	const bool desired = isEnabled ? true : false;
	if (desired == m_isVSyncEnabled)
		return true;

	switch (m_eRenderBackend)
	{
		case RENDER_BACKEND_DX11:
			if (!m_grpDeviceDX11.SetVSyncEnabled(desired))
				return false;
			if (m_pySystem.IsDX11FirstPassActiveEnabled() && !m_grpDevice.SetVSyncEnabled(desired))
				return false;
			break;
		case RENDER_BACKEND_DX9:
		default:
			if (!m_grpDevice.SetVSyncEnabled(desired))
				return false;
			if (m_iRequestedRenderAPI == 11 && m_grpDeviceDX11.IsValid())
				m_grpDeviceDX11.SetVSyncEnabled(desired);
			break;
	}

	m_isVSyncEnabled = desired;
	return true;
}

int CPythonApplication::GetWidth()
{
	return m_dwWidth;
}

int CPythonApplication::GetHeight()
{
	return m_dwHeight;
}

void CPythonApplication::SetConnectData(const char * c_szIP, int iPort)
{
	m_strIP = c_szIP;
	m_iPort = iPort;
}

void CPythonApplication::GetConnectData(std::string & rstIP, int & riPort)
{
	rstIP	= m_strIP;
	riPort	= m_iPort;
}

void CPythonApplication::EnableSpecialCameraMode()
{
	m_isSpecialCameraMode = TRUE;
}

void CPythonApplication::SetCameraSpeed(int iPercentage)
{
	m_fCameraRotateSpeed = c_fDefaultCameraRotateSpeed * float(iPercentage) / 100.0f;
	m_fCameraPitchSpeed = c_fDefaultCameraPitchSpeed * float(iPercentage) / 100.0f;
	m_fCameraZoomSpeed = c_fDefaultCameraZoomSpeed * float(iPercentage) / 100.0f;
}

void CPythonApplication::SetForceSightRange(int iRange)
{
	m_iForceSightRange = iRange;
}

void CPythonApplication::Clear()
{
	m_pySystem.Clear();
}

void CPythonApplication::Destroy()
{
	// SphereMap
	CGrannyMaterial::DestroySphereMap();

	m_kWndMgr.Destroy();

	CPythonSystem::Instance().SaveConfig();

	DestroyCollisionInstanceSystem();

	m_pySystem.SaveInterfaceStatus();

	m_pyEventManager.Destroy();	
	m_FlyingManager.Destroy();

	m_pyMiniMap.Destroy();

	m_pyTextTail.Destroy();
	m_pyChat.Destroy();	
	m_kChrMgr.Destroy();
	m_RaceManager.Destroy();

	m_pyItem.Destroy();
	m_kItemMgr.Destroy();

	m_pyBackground.Destroy();

	m_kEftMgr.DestroyDX11EffectResources();
	m_kEftMgr.Destroy();
	m_LightManager.Destroy();

	// Game Thread Pool
	CGameThreadPool::Instance().Destroy();

	// DEFAULT_FONT
	DefaultFont_Cleanup();
	// END_OF_DEFAULT_FONT

	GrannyDestroySharedDeformBuffer();

	m_pyGraphic.Destroy();
	
#if defined(ENABLE_DISCORD_RPC)
	m_pyNetworkStream.Discord_Close();
#endif	
	
	//m_pyNetworkDatagram.Destroy();	

	m_pyRes.Destroy();

	m_kGuildMarkDownloader.Disconnect();

	CGrannyModelInstance::DestroySystem();
	CGraphicImageInstance::DestroySystem();

	m_grpDevice.Destroy();
	m_grpDeviceDX11.Destroy();

	//CSpeedTreeForestDirectX::Instance().Clear();

	CAttributeInstance::DestroySystem();
	CTextFileLoader::DestroySystem();
	DestroyCursors();

	CMSApplication::Destroy();

	STICKYKEYS sStickKeys;
	memset(&sStickKeys, 0, sizeof(sStickKeys));
	sStickKeys.cbSize = sizeof(sStickKeys);
	sStickKeys.dwFlags = m_dwStickyKeysFlag;
	SystemParametersInfo( SPI_SETSTICKYKEYS, sizeof(sStickKeys), &sStickKeys, 0 );
}

#ifdef BUILD_DEBUG_UI
// DX11 Model Sync: ImGui Developer Monitoring Tool Implementation
bool CPythonApplication::InitializeImGui()
{
	// Create MetricsCollector singleton
	if (!CImGuiMetricsCollector::Create())
	{
		TraceError("InitializeImGui: Failed to create MetricsCollector singleton");
		return false;
	}

	// Create GraphicsMetrics singleton
	if (!CImGuiGraphicsMetrics::Create())
	{
		TraceError("InitializeImGui: Failed to create GraphicsMetrics singleton");
		CImGuiMetricsCollector::Destroy();
		return false;
	}

	// Create GraphPlotter singleton
	if (!CImGuiGraphPlotter::Create())
	{
		TraceError("InitializeImGui: Failed to create GraphPlotter singleton");
		CImGuiGraphicsMetrics::Destroy();
		CImGuiMetricsCollector::Destroy();
		return false;
	}

	// Create ImGuiManager singleton
	if (!CImGuiManager::Create())
	{
		TraceError("InitializeImGui: Failed to create ImGui singleton");
		CImGuiGraphPlotter::Destroy();
		CImGuiGraphicsMetrics::Destroy();
		CImGuiMetricsCollector::Destroy();
		return false;
	}

	HWND hWnd = GetWindowHandle();
	ID3D11Device* pDevice = m_grpDeviceDX11.GetDevice();
	ID3D11DeviceContext* pContext = m_grpDeviceDX11.GetContext();

	if (!hWnd || !pDevice || !pContext)
	{
		TraceError("InitializeImGui: Invalid parameters (hWnd=%p, device=%p, context=%p)",
			hWnd, pDevice, pContext);
		CImGuiManager::Destroy();
		CImGuiGraphPlotter::Destroy();
		CImGuiGraphicsMetrics::Destroy();
		CImGuiMetricsCollector::Destroy();
		return false;
	}

	if (!CImGuiManager::Instance()->Initialize(hWnd, pDevice, pContext))
	{
		TraceError("InitializeImGui: ImGuiManager initialization failed");
		CImGuiManager::Destroy();
		CImGuiGraphPlotter::Destroy();
		CImGuiGraphicsMetrics::Destroy();
		CImGuiMetricsCollector::Destroy();
		return false;
	}

	// Initialize GraphicsMetrics with DX11 device
	if (CImGuiGraphicsMetrics::Instance())
	{
		if (!CImGuiGraphicsMetrics::Instance()->Initialize(pDevice, pContext))
		{
			TraceError("InitializeImGui: GraphicsMetrics initialization failed (non-critical)");
		}
	}

	// Initialize DX11-specific metrics
	if (CImGuiMetricsCollector::Instance())
	{
		const uint32_t uFeatureLevel = m_grpDeviceDX11.GetFeatureLevel();
		const uint32_t uWorldPortMask = m_grpDeviceDX11.GetNativeWorldPortMask();
		const uint32_t uMissingPortMask = m_grpDeviceDX11.GetNativeWorldMissingPortMask();
		const uint32_t uWorldObservedMask = (m_grpDeviceDX11.GetNativeWorldObservedMask() & CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK);
		const uint32_t uWorldSubmittedMask = (m_grpDeviceDX11.GetNativeWorldSubmittedMask() & CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK);
		const uint32_t uWorldApplicableMask = (m_grpDeviceDX11.GetNativeWorldApplicableMask() & CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK);
		const uint32_t uWorldCommittedMask = (m_grpDeviceDX11.GetNativeWorldPortMask() & CGraphicDeviceDX11::WORLD_PORT_REQUIRED_MASK);
		const CLightManager::SLightTelemetry& rkLightTelemetry = CLightManager::Instance().GetTelemetry();
		const CPythonBackground::SDX11WorldSubmitTelemetry& rkWorldSubmitTelemetry =
			CPythonBackground::Instance().GetDX11WorldSubmitTelemetry();
		uint32_t uNoRTVWithPSCount = 0u;
		uint32_t uNoRTVWithPSIndexedCount = 0u;
		uint32_t uNoRTVWithPSNonIndexedCount = 0u;
		uint32_t uNoRTVWithPSLastTopology = 0u;
		uint32_t uNoRTVWithPSLastElements = 0u;
		uint32_t uNoRTVWithPSLastDepthBound = 0u;
		uint32_t uUnsupportedRenderStateCount = 0u;
		uint32_t uUnsupportedRenderStateLastType = 0u;
		uint32_t uUnsupportedRenderStateLastValue = 0u;
		uint32_t uFogEnable = 0u;
		uint32_t uFogMode = 0u;
		uint32_t uFogRangeEnable = 0u;
		uint32_t uFogColor = 0u;
		uint32_t uFogDensity = 0u;
		uint32_t uFogStart = 0u;
		uint32_t uFogEnd = 0u;
		uint32_t uVSConstClampCount = 0u;
		uint32_t uVSConstClampLastRegister = 0u;
		uint32_t uVSConstClampLastRequested = 0u;
		uint32_t uVSConstClampLastApplied = 0u;
		uint32_t uVSConstSetCallCount = 0u;
		uint32_t uVSConstSetRegisterCount = 0u;
		uint32_t uVSConstSetLastRegister = 0u;
		uint32_t uVSConstSetLastCount = 0u;
		uint32_t uVSConstUploadCount = 0u;
		uint32_t uVSConstUploadBytes = 0u;
		uint32_t uVSConstUploadStartRegister = 0u;
		uint32_t uVSConstUploadEndRegister = 0u;
		uint32_t uPSConstClampCount = 0u;
		uint32_t uPSConstClampLastRegister = 0u;
		uint32_t uPSConstClampLastRequested = 0u;
		uint32_t uPSConstClampLastApplied = 0u;
		uint32_t uPSConstSetCallCount = 0u;
		uint32_t uPSConstSetRegisterCount = 0u;
		uint32_t uPSConstSetLastRegister = 0u;
		uint32_t uPSConstSetLastCount = 0u;
		uint32_t uPSConstUploadCount = 0u;
		uint32_t uPSConstUploadBytes = 0u;
		uint32_t uPSConstUploadStartRegister = 0u;
		uint32_t uPSConstUploadEndRegister = 0u;
		const uint32_t uWorldSubmitMismatchCount = m_dwDX11WorldSubmitMaskMismatchCount;
		const uint32_t uWorldSubmitMismatchActive = m_bDX11WorldSubmitMaskMismatchActive ? 1u : 0u;
		const uint32_t uWorldSubmitMismatchTelemetryObserved = m_uDX11WorldSubmitMaskMismatchTelemetryObserved;
		const uint32_t uWorldSubmitMismatchTelemetrySubmitted = m_uDX11WorldSubmitMaskMismatchTelemetrySubmitted;
		const uint32_t uWorldSubmitMismatchTelemetryApplicable = m_uDX11WorldSubmitMaskMismatchTelemetryApplicable;
		const uint32_t uWorldSubmitMismatchTelemetryCommitted = m_uDX11WorldSubmitMaskMismatchTelemetryCommitted;
		const uint32_t uWorldSubmitMismatchGateObserved = m_uDX11WorldSubmitMaskMismatchGateObserved;
		const uint32_t uWorldSubmitMismatchGateSubmitted = m_uDX11WorldSubmitMaskMismatchGateSubmitted;
		const uint32_t uWorldSubmitMismatchGateApplicable = m_uDX11WorldSubmitMaskMismatchGateApplicable;
		const uint32_t uWorldSubmitMismatchGateCommitted = m_uDX11WorldSubmitMaskMismatchGateCommitted;
		const uint32_t uWorldSubmitMismatchLastReasonMask = m_uDX11WorldSubmitMaskMismatchLastReasonMask;
		const uint32_t uWorldSubmitMismatchLastPhaseActive = m_uDX11WorldSubmitMaskMismatchLastPhaseActive;
		const uint32_t uWorldSubmitMismatchLastFrame = m_dwDX11WorldSubmitMaskMismatchLastFrame;
		const uint32_t uWorldSubmitMismatchLastElapsedMS = m_dwDX11WorldSubmitMaskMismatchLastElapsedMS;
		if (CStateManager* pStateManager = CStateManager::InstancePtr())
		{
			const CStateManager::SDebugDrawDiagnostics& rkStateDrawDiag = pStateManager->GetDebugDrawDiagnostics();
			uNoRTVWithPSCount = rkStateDrawDiag.uNoRTVWithPSCount;
			uNoRTVWithPSIndexedCount = rkStateDrawDiag.uNoRTVWithPSIndexedCount;
			uNoRTVWithPSNonIndexedCount = rkStateDrawDiag.uNoRTVWithPSNonIndexedCount;
			uNoRTVWithPSLastTopology = rkStateDrawDiag.uNoRTVWithPSLastTopology;
			uNoRTVWithPSLastElements = rkStateDrawDiag.uNoRTVWithPSLastElements;
			uNoRTVWithPSLastDepthBound = rkStateDrawDiag.uNoRTVWithPSLastDepthBound;
			uUnsupportedRenderStateCount = rkStateDrawDiag.uUnsupportedRenderStateCount;
			uUnsupportedRenderStateLastType = rkStateDrawDiag.uUnsupportedRenderStateLastType;
			uUnsupportedRenderStateLastValue = rkStateDrawDiag.uUnsupportedRenderStateLastValue;
			uFogEnable = rkStateDrawDiag.uFogEnable;
			uFogMode = rkStateDrawDiag.uFogMode;
			uFogRangeEnable = rkStateDrawDiag.uFogRangeEnable;
			uFogColor = rkStateDrawDiag.uFogColor;
			uFogDensity = rkStateDrawDiag.uFogDensity;
			uFogStart = rkStateDrawDiag.uFogStart;
			uFogEnd = rkStateDrawDiag.uFogEnd;
			uVSConstClampCount = rkStateDrawDiag.uVSConstClampCount;
			uVSConstClampLastRegister = rkStateDrawDiag.uVSConstClampLastRegister;
			uVSConstClampLastRequested = rkStateDrawDiag.uVSConstClampLastRequested;
			uVSConstClampLastApplied = rkStateDrawDiag.uVSConstClampLastApplied;
			uVSConstSetCallCount = rkStateDrawDiag.uVSConstSetCallCount;
			uVSConstSetRegisterCount = rkStateDrawDiag.uVSConstSetRegisterCount;
			uVSConstSetLastRegister = rkStateDrawDiag.uVSConstSetLastRegister;
			uVSConstSetLastCount = rkStateDrawDiag.uVSConstSetLastCount;
			uVSConstUploadCount = rkStateDrawDiag.uVSConstUploadCount;
			uVSConstUploadBytes = rkStateDrawDiag.uVSConstUploadBytes;
			uVSConstUploadStartRegister = rkStateDrawDiag.uVSConstUploadStartRegister;
			uVSConstUploadEndRegister = rkStateDrawDiag.uVSConstUploadEndRegister;
			uPSConstClampCount = rkStateDrawDiag.uPSConstClampCount;
			uPSConstClampLastRegister = rkStateDrawDiag.uPSConstClampLastRegister;
			uPSConstClampLastRequested = rkStateDrawDiag.uPSConstClampLastRequested;
			uPSConstClampLastApplied = rkStateDrawDiag.uPSConstClampLastApplied;
			uPSConstSetCallCount = rkStateDrawDiag.uPSConstSetCallCount;
			uPSConstSetRegisterCount = rkStateDrawDiag.uPSConstSetRegisterCount;
			uPSConstSetLastRegister = rkStateDrawDiag.uPSConstSetLastRegister;
			uPSConstSetLastCount = rkStateDrawDiag.uPSConstSetLastCount;
			uPSConstUploadCount = rkStateDrawDiag.uPSConstUploadCount;
			uPSConstUploadBytes = rkStateDrawDiag.uPSConstUploadBytes;
			uPSConstUploadStartRegister = rkStateDrawDiag.uPSConstUploadStartRegister;
			uPSConstUploadEndRegister = rkStateDrawDiag.uPSConstUploadEndRegister;
		}

		const CEffectManager::SDX11TargetRingDiagnostics& rkTargetDiag = m_kEftMgr.GetDX11TargetRingDiagnostics();
		CImGuiMetricsCollector::Instance()->SetDX11Metrics(
			uFeatureLevel,
			uWorldPortMask,
			uMissingPortMask,
			uWorldObservedMask,
			uWorldSubmittedMask,
			uWorldApplicableMask,
			uWorldCommittedMask,
			rkLightTelemetry.dwRegisteredStaticCount,
			rkLightTelemetry.dwRegisteredDynamicCount,
			rkLightTelemetry.dwActiveStaticCount,
			rkLightTelemetry.dwActiveDynamicCount,
			rkLightTelemetry.dwRequestedActiveCount,
			rkLightTelemetry.dwBoundActiveCount,
			rkLightTelemetry.dwClippedBySlotCount,
			rkLightTelemetry.dwSlotCapacity,
			rkLightTelemetry.dwSkipIndex,
			static_cast<uint32_t>(std::max(0, rkWorldSubmitTelemetry.iTerrainPatches)),
			static_cast<uint32_t>(std::max(0, rkWorldSubmitTelemetry.iTerrainSplats)),
			static_cast<uint32_t>(std::max(0, rkWorldSubmitTelemetry.iWaterPatches)),
			rkWorldSubmitTelemetry.dwObjectSubmitted,
			rkWorldSubmitTelemetry.dwEffectSubmitted,
			rkWorldSubmitTelemetry.dwEffectParticleSubmitted,
			rkWorldSubmitTelemetry.dwEffectMeshSubmitted,
			rkTargetDiag.dwActiveInstanceCount,
			rkTargetDiag.dwSubmittedCount,
			rkTargetDiag.dwSkippedCount,
			rkTargetDiag.dwLastEffectCRC,
			rkTargetDiag.dwLastTimestampMS,
			rkTargetDiag.dwLastBlendState,
			rkTargetDiag.dwLastPipelineFlags,
			m_kEftMgr.GetDX11TargetRingAlphaClipThreshold(),
			rkTargetDiag.szLastReason,
			rkWorldSubmitTelemetry.dwSpeedTreeSubmitted,
			uNoRTVWithPSCount,
			uNoRTVWithPSIndexedCount,
			uNoRTVWithPSNonIndexedCount,
			uNoRTVWithPSLastTopology,
			uNoRTVWithPSLastElements,
			uNoRTVWithPSLastDepthBound,
			uUnsupportedRenderStateCount,
			uUnsupportedRenderStateLastType,
			uUnsupportedRenderStateLastValue,
			uFogEnable,
			uFogMode,
			uFogRangeEnable,
			uFogColor,
			uFogDensity,
			uFogStart,
			uFogEnd,
			uVSConstClampCount,
			uVSConstClampLastRegister,
			uVSConstClampLastRequested,
			uVSConstClampLastApplied,
			uVSConstSetCallCount,
			uVSConstSetRegisterCount,
			uVSConstSetLastRegister,
			uVSConstSetLastCount,
			uVSConstUploadCount,
			uVSConstUploadBytes,
			uVSConstUploadStartRegister,
			uVSConstUploadEndRegister,
			uPSConstClampCount,
			uPSConstClampLastRegister,
			uPSConstClampLastRequested,
			uPSConstClampLastApplied,
			uPSConstSetCallCount,
			uPSConstSetRegisterCount,
			uPSConstSetLastRegister,
			uPSConstSetLastCount,
			uPSConstUploadCount,
			uPSConstUploadBytes,
			uPSConstUploadStartRegister,
			uPSConstUploadEndRegister,
			uWorldSubmitMismatchCount,
			uWorldSubmitMismatchActive,
			uWorldSubmitMismatchTelemetryObserved,
			uWorldSubmitMismatchTelemetrySubmitted,
			uWorldSubmitMismatchTelemetryApplicable,
			uWorldSubmitMismatchTelemetryCommitted,
			uWorldSubmitMismatchGateObserved,
			uWorldSubmitMismatchGateSubmitted,
			uWorldSubmitMismatchGateApplicable,
			uWorldSubmitMismatchGateCommitted
		);
	}

	TraceError("InitializeImGui: Successfully initialized (F12 to toggle overlay)");
	return true;
}

void CPythonApplication::ShutdownImGui()
{
	if (CImGuiManager::Instance())
	{
		CImGuiManager::Destroy();
		TraceError("ShutdownImGui: ImGui manager destroyed");
	}

	if (CImGuiGraphPlotter::Instance())
	{
		CImGuiGraphPlotter::Destroy();
		TraceError("ShutdownImGui: GraphPlotter destroyed");
	}

	if (CImGuiGraphicsMetrics::Instance())
	{
		CImGuiGraphicsMetrics::Destroy();
		TraceError("ShutdownImGui: GraphicsMetrics destroyed");
	}

	if (CImGuiMetricsCollector::Instance())
	{
		CImGuiMetricsCollector::Destroy();
		TraceError("ShutdownImGui: MetricsCollector destroyed");
	}
}
#endif
