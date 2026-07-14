// StormEnvironment.h: interface for the CStormEnvironment class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_STORMENVIRONMENT_H__INCLUDED_)
#define AFX_STORMENVIRONMENT_H__INCLUDED_

#include "LightningFlash.h"
#include <DirectXMath.h>

// Forward declarations
class CRainEnvironment;

class CStormEnvironment
{
public:
	CStormEnvironment();
	virtual ~CStormEnvironment();

	void Create();
	void Destroy();
	void Update(float fElapsedTime);
	void Render();

	// Storm control
	void Enable();
	void Disable();
	bool IsEnabled() const { return m_bStormEnabled; }

	// Lightning control
	void TriggerLightning();
	bool IsLightningActive() const;

	// Configuration
	void SetLightningFrequency(float fMinInterval, float fMaxInterval);
	void SetLightningIntensity(float fIntensity);
	void SetLightningDuration(float fDuration);
	void SetAutoLightning(bool bEnable);

	void SetStormRainIntensity(float fIntensity);  // 0.0 - 1.0 (how heavy rain gets during storm)
	void SetWindGustChance(float fChance);          // 0.0 - 1.0 (probability per second)
	void SetWindGustStrength(float fStrength);      // How strong wind gusts are

	// State query
	float GetStormIntensity() const { return m_fStormIntensity; } // 0.0 - 1.0 (current storm strength)
	bool IsWindGustActive() const { return m_bWindGustActive; }
	float GetWindGustIntensity() const { return m_fWindGustIntensity; }
	bool RunWeatherRestoreDiagnostic();

	// Link to rain environment
	void SetRainEnvironment(class CRainEnvironment* pRainEnv);

private:
	void __Initialize();
	void __UpdateWindGusts(float fElapsedTime);
	void __UpdateStormIntensity(float fElapsedTime);
	void __TriggerWindGust();
	void __CaptureBaseWeather();
	void __RestoreBaseWeather();

private:
	// Storm state
	bool m_bStormEnabled;
	float m_fStormIntensity;        // Current storm strength (0.0 - 1.0)
	float m_fStormRampUpTime;       // How long storm takes to reach full intensity

	// Lightning system
	CLightningFlash m_LightningFlash;
	bool m_bAutoLightning;

	// Rain integration
	CRainEnvironment* m_pRainEnvironment;
	float m_fStormRainIntensity;    // Rain intensity during storm (0.0 - 1.0)
	DWORD m_dwBaseRainParticleCount; // Exact particle count before storm

	// Wind gust system
	bool m_bWindGustActive;
	float m_fWindGustIntensity;     // Current gust intensity (0.0 - 1.0)
	float m_fWindGustTimer;         // How long current gust lasts
	float m_fWindGustDuration;      // Base gust duration
	float m_fWindGustChance;        // Chance per second to trigger gust
	float m_fWindGustStrength;      // Max gust strength
	DirectX::SimpleMath::Vector3 m_v3BaseWind;       // Wind before gust
	DirectX::SimpleMath::Vector3 m_v3GustWind;       // Wind during gust

	bool m_bInitialized;
};

#endif // !defined(AFX_STORMENVIRONMENT_H__INCLUDED_)
