// LightningFlash.h: interface for the CLightningFlash class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LIGHTNINGFLASH_H__INCLUDED_)
#define AFX_LIGHTNINGFLASH_H__INCLUDED_

#include <DirectXMath.h>
#include <vector>

// Forward declarations
class CGraphicImageInstance;
class CGraphicTexture;

class CLightningFlash
{
public:
	CLightningFlash();
	virtual ~CLightningFlash();

	void Create();
	void Destroy();
	void Update(float fElapsedTime);
	void Render();

	// Lightning control
	void TriggerFlash(float fIntensity = 1.0f);
	bool IsActive() const { return m_bFlashActive; }

	// Configuration
	void SetMinInterval(float fSeconds);     // Min time between strikes
	void SetMaxInterval(float fSeconds);     // Max time between strikes
	void SetFlashDuration(float fSeconds);   // How long flash lasts
	void SetFlashIntensity(float fIntensity); // Max brightness
	void SetAutoTrigger(bool bEnable);       // Auto-trigger lightning

	// State query
	float GetCurrentIntensity() const { return m_fCurrentIntensity; }
	float GetTimeUntilNextStrike() const { return m_fTimeUntilNextStrike; }

private:
	void __Initialize();
	void __TriggerRandomFlash();

private:
	// Flash state
	bool m_bFlashActive;
	float m_fCurrentIntensity;      // Current flash intensity (0.0 - 1.0)
	float m_fFlashTimer;            // Time since flash started
	float m_fFlashDuration;         // How long flash lasts
	float m_fMaxIntensity;          // Maximum flash intensity

	// Auto-trigger system
	bool m_bAutoTrigger;
	float m_fTimeUntilNextStrike;   // Time until next automatic strike
	float m_fMinInterval;           // Minimum interval between strikes
	float m_fMaxInterval;           // Maximum interval between strikes

	// Graphics resources
	CGraphicImageInstance* m_pLightningTexture;
	bool m_bInitialized;

	// Static flash timer (shared across all instances for global timing)
	static float s_fLastTime;
};

#endif // !defined(AFX_LIGHTNINGFLASH_H__INCLUDED_)
