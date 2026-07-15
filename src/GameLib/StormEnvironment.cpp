#include "StdAfx.h"
#include "StormEnvironment.h"

#include "RainEnvironment.h"

CStormEnvironment::CStormEnvironment()
{
	__Initialize();
}

CStormEnvironment::~CStormEnvironment()
{
	Destroy();
}

void CStormEnvironment::__Initialize()
{
	m_bStormEnabled = false;
	m_fStormIntensity = 0.0f;
	m_fStormRampUpTime = 5.0f;  // 5 seconds to reach full intensity

	m_bAutoLightning = true;

	m_pRainEnvironment = nullptr;
	m_fStormRainIntensity = 0.8f;   // During storm: 80% of max rain
	m_dwBaseRainParticleCount = 3000; // Before storm: 30% of max rain

	m_bWindGustActive = false;
	m_fWindGustIntensity = 0.0f;
	m_fWindGustTimer = 0.0f;
	m_fWindGustDuration = 2.0f;     // Gusts last 2 seconds
	m_fWindGustChance = 0.1f;       // 10% chance per second
	m_fWindGustStrength = 1.0f;
	m_v3BaseWind = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	m_v3GustWind = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

	m_bInitialized = false;
}

void CStormEnvironment::Create()
{
	m_LightningFlash.Create();
	m_bInitialized = true;
}

void CStormEnvironment::Destroy()
{
	if (m_bStormEnabled)
		__RestoreBaseWeather();

	m_LightningFlash.Destroy();
	__Initialize();
}

void CStormEnvironment::SetRainEnvironment(CRainEnvironment* pRainEnv)
{
	if (m_pRainEnvironment == pRainEnv)
	{
		if (!m_bStormEnabled)
			__CaptureBaseWeather();
		return;
	}

	if (m_bStormEnabled)
		__RestoreBaseWeather();

	m_pRainEnvironment = pRainEnv;
	__CaptureBaseWeather();
}

void CStormEnvironment::Enable()
{
	if (!m_bInitialized)
		Create();

	if (m_bStormEnabled)
		return;

	__CaptureBaseWeather();
	m_bStormEnabled = true;
	m_LightningFlash.SetAutoTrigger(m_bAutoLightning);
}

void CStormEnvironment::Disable()
{
	if (!m_bStormEnabled)
		return;

	m_bStormEnabled = false;
	m_fStormIntensity = 0.0f;
	m_bWindGustActive = false;
	m_LightningFlash.SetAutoTrigger(false);
	__RestoreBaseWeather();
}

void CStormEnvironment::__CaptureBaseWeather()
{
	if (!m_pRainEnvironment)
		return;

	m_dwBaseRainParticleCount = m_pRainEnvironment->GetParticleCount();
	m_v3BaseWind = m_pRainEnvironment->GetWindVector();
}

void CStormEnvironment::__RestoreBaseWeather()
{
	if (!m_pRainEnvironment)
		return;

	m_pRainEnvironment->SetParticleCount(m_dwBaseRainParticleCount);
	m_pRainEnvironment->SetWindVector(m_v3BaseWind);
}

bool CStormEnvironment::RunWeatherRestoreDiagnostic()
{
	if (!m_pRainEnvironment || m_bStormEnabled)
	{
		TraceError("STORM_RESTORE_DIAGNOSTIC result=skipped reason=%s",
			m_bStormEnabled ? "storm_already_enabled" : "rain_environment_missing");
		return false;
	}

	const DWORD dwExpectedParticleCount = m_pRainEnvironment->GetParticleCount();
	const DirectX::SimpleMath::Vector3 v3ExpectedWind = m_pRainEnvironment->GetWindVector();
	Enable();

	const DWORD dwDiagnosticParticleCount = dwExpectedParticleCount == 4321u ? 8765u : 4321u;
	const DirectX::SimpleMath::Vector3 v3DiagnosticWind(
		v3ExpectedWind.x + 137.0f,
		v3ExpectedWind.y - 251.0f,
		v3ExpectedWind.z + 389.0f);
	m_pRainEnvironment->SetParticleCount(dwDiagnosticParticleCount);
	m_pRainEnvironment->SetWindVector(v3DiagnosticWind);
	const DWORD dwStormParticleCount = m_pRainEnvironment->GetParticleCount();
	const DirectX::SimpleMath::Vector3 v3StormWind = m_pRainEnvironment->GetWindVector();
	Disable();

	const DWORD dwRestoredParticleCount = m_pRainEnvironment->GetParticleCount();
	const DirectX::SimpleMath::Vector3 v3RestoredWind = m_pRainEnvironment->GetWindVector();
	const bool bParticleCountRestored = dwRestoredParticleCount == dwExpectedParticleCount;
	const bool bWindRestored = v3RestoredWind.x == v3ExpectedWind.x &&
		v3RestoredWind.y == v3ExpectedWind.y &&
		v3RestoredWind.z == v3ExpectedWind.z;
	const bool bPassed = bParticleCountRestored && bWindRestored;

	TraceError(
		"STORM_RESTORE_DIAGNOSTIC result=%s particles_before=%u particles_storm=%u particles_after=%u "
		"wind_before=(%.3f,%.3f,%.3f) wind_storm=(%.3f,%.3f,%.3f) wind_after=(%.3f,%.3f,%.3f)",
		bPassed ? "pass" : "fail",
		dwExpectedParticleCount,
		dwStormParticleCount,
		dwRestoredParticleCount,
		v3ExpectedWind.x, v3ExpectedWind.y, v3ExpectedWind.z,
		v3StormWind.x, v3StormWind.y, v3StormWind.z,
		v3RestoredWind.x, v3RestoredWind.y, v3RestoredWind.z);
	return bPassed;
}

void CStormEnvironment::SetLightningFrequency(float fMinInterval, float fMaxInterval)
{
	m_LightningFlash.SetMinInterval(fMinInterval);
	m_LightningFlash.SetMaxInterval(fMaxInterval);
}

void CStormEnvironment::SetLightningIntensity(float fIntensity)
{
	m_LightningFlash.SetFlashIntensity(fIntensity);
}

void CStormEnvironment::SetLightningDuration(float fDuration)
{
	m_LightningFlash.SetFlashDuration(fDuration);
}

void CStormEnvironment::SetAutoLightning(bool bEnable)
{
	m_bAutoLightning = bEnable;
	if (m_bStormEnabled)
	{
		m_LightningFlash.SetAutoTrigger(bEnable);
	}
}

void CStormEnvironment::SetStormRainIntensity(float fIntensity)
{
	m_fStormRainIntensity = std::max(0.0f, std::min(1.0f, fIntensity));
}

void CStormEnvironment::SetWindGustChance(float fChance)
{
	m_fWindGustChance = std::max(0.0f, std::min(1.0f, fChance));
}

void CStormEnvironment::SetWindGustStrength(float fStrength)
{
	m_fWindGustStrength = std::max(0.0f, std::min(2.0f, fStrength));
}

void CStormEnvironment::TriggerLightning()
{
	m_LightningFlash.TriggerFlash();
}

bool CStormEnvironment::IsLightningActive() const
{
	return m_LightningFlash.IsActive();
}

void CStormEnvironment::__TriggerWindGust()
{
	if (!m_pRainEnvironment)
		return;

	m_bWindGustActive = true;
	m_fWindGustTimer = 0.0f;
	m_fWindGustIntensity = 1.0f;

	// Random gust direction (primarily horizontal with slight vertical)
	float fAngle = frandom(0.0f, 6.28318f); // 0 to 2π
	float fStrength = m_fWindGustStrength * 1000.0f; // Scale to rain wind units

	m_v3GustWind = DirectX::SimpleMath::Vector3(
		cosf(fAngle) * fStrength,
		sinf(fAngle) * fStrength,
		frandom(-100.0f, 100.0f)  // Slight vertical variation
	);

	// Apply gust to rain environment
	m_pRainEnvironment->SetWindVector(m_v3GustWind);
}

void CStormEnvironment::__UpdateWindGusts(float fElapsedTime)
{
	if (!m_pRainEnvironment)
		return;

	// Update active gust
	if (m_bWindGustActive)
	{
		m_fWindGustTimer += fElapsedTime;
		float fGustProgress = m_fWindGustTimer / m_fWindGustDuration;

		if (fGustProgress >= 1.0f)
		{
			// Gust ended
			m_bWindGustActive = false;
			m_fWindGustIntensity = 0.0f;
			m_pRainEnvironment->SetWindVector(m_v3BaseWind);
		}
		else
		{
			// Gust is active, interpolate intensity
			m_fWindGustIntensity = 1.0f - fGustProgress;

			// Add some turbulence
			DirectX::SimpleMath::Vector3 v3Turbulence = DirectX::SimpleMath::Vector3(
				frandom(-50.0f, 50.0f),
				frandom(-50.0f, 50.0f),
				frandom(-20.0f, 20.0f)
			);

			DirectX::SimpleMath::Vector3 v3CurrentWind = m_v3GustWind + v3Turbulence * m_fWindGustIntensity;
			m_pRainEnvironment->SetWindVector(v3CurrentWind);
		}
	}
	else
	{
		// Check if we should trigger a new gust
		float fRoll = frandom(0.0f, 1.0f);
		if (fRoll < m_fWindGustChance * fElapsedTime)
		{
			__TriggerWindGust();
		}
	}
}

void CStormEnvironment::__UpdateStormIntensity(float fElapsedTime)
{
	if (!m_pRainEnvironment)
		return;

	// Ramp up storm intensity
	if (m_fStormIntensity < 1.0f)
	{
		m_fStormIntensity += fElapsedTime / m_fStormRampUpTime;
		m_fStormIntensity = std::min(1.0f, m_fStormIntensity);
	}

	// Interpolate rain intensity based on storm intensity
	const float fBaseRainIntensity = static_cast<float>(m_dwBaseRainParticleCount) / 10000.0f;
	float fCurrentRainIntensity = fBaseRainIntensity +
		(m_fStormRainIntensity - fBaseRainIntensity) * m_fStormIntensity;

	// Apply to rain environment
	DWORD dwParticleCount = (DWORD)(fCurrentRainIntensity * 10000.0f);
	m_pRainEnvironment->SetParticleCount(dwParticleCount);
}

void CStormEnvironment::Update(float fElapsedTime)
{
	if (!m_bInitialized || !m_bStormEnabled)
		return;

	// Update lightning
	m_LightningFlash.Update(fElapsedTime);

	// Update storm intensity (rain ramp-up)
	__UpdateStormIntensity(fElapsedTime);

	// Update wind gusts
	__UpdateWindGusts(fElapsedTime);
}

void CStormEnvironment::Render()
{
	if (!m_bInitialized || !m_bStormEnabled)
		return;

	// Render lightning flash
	m_LightningFlash.Render();
}
