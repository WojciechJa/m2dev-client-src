#include "StdAfx.h"
#include "RainParticle.h"

const float c_fRainDistance = 70000.0f;

// Static pool initialization
std::vector<CRainParticle*> CRainParticle::ms_kVct_RainParticlePool;

// Camera alignment - cache camera vectors for quad generation
void CRainParticle::SetCameraVertex(const DirectX::SimpleMath::Vector3 & rv3Up, const DirectX::SimpleMath::Vector3 & rv3Cross)
{
	m_v3Up = rv3Up * m_fHalfWidth;
	m_v3Cross = rv3Cross * m_fHalfHeight;
}

// Check if particle is still alive
bool CRainParticle::IsActivate()
{
	return m_bActivate;
}

// Update particle with wind influence (no sway like snow)
void CRainParticle::Update(float fElapsedTime, const DirectX::SimpleMath::Vector3 & c_rv3Pos, const DirectX::SimpleMath::Vector3 & v3Wind)
{
	// Apply velocity (downward motion)
	m_v3Position += m_v3Velocity * fElapsedTime;

	// Apply wind influence (lateral motion)
	m_v3Position += v3Wind * fElapsedTime;

	// Kill particle if too low (500 units below spawn point)
	if (m_v3Position.z < c_rv3Pos.z - 500.0f)
		m_bActivate = false;
	// Kill particle if too far horizontally (70000 units)
	else if (abs(m_v3Position.x - c_rv3Pos.x) > c_fRainDistance)
		m_bActivate = false;
	else if (abs(m_v3Position.y - c_rv3Pos.y) > c_fRainDistance)
		m_bActivate = false;
}

// Generate camera-facing quad vertices
void CRainParticle::GetVerticies(SParticleVertex & rv3Vertex1, SParticleVertex & rv3Vertex2,
								 SParticleVertex & rv3Vertex3, SParticleVertex & rv3Vertex4)
{
	// Bottom-left
	rv3Vertex1.v3Pos = m_v3Position - m_v3Cross - m_v3Up;
	rv3Vertex1.u = 0.0f;
	rv3Vertex1.v = 0.0f;

	// Bottom-right
	rv3Vertex2.v3Pos = m_v3Position + m_v3Cross - m_v3Up;
	rv3Vertex2.u = 1.0f;
	rv3Vertex2.v = 0.0f;

	// Top-left
	rv3Vertex3.v3Pos = m_v3Position - m_v3Cross + m_v3Up;
	rv3Vertex3.u = 0.0f;
	rv3Vertex3.v = 1.0f;

	// Top-right
	rv3Vertex4.v3Pos = m_v3Position + m_v3Cross + m_v3Up;
	rv3Vertex4.u = 1.0f;
	rv3Vertex4.v = 1.0f;
}

// Initialize particle with configurable parameters
void CRainParticle::Init(const DirectX::SimpleMath::Vector3 & c_rv3Pos,
						  float fFallSpeedMin,
						  float fFallSpeedMax,
						  float fParticleWidth,
						  float fParticleHeight,
						  float fSpawnRadius)
{
	// Random position in circle around center point
	float fRot = frandom(0.0f, 36000.0f) / 100.0f;
	float fDistance = frandom(0.0f, fSpawnRadius) / 10.0f;

	m_v3Position.x = c_rv3Pos.x + fDistance * sin((double)D3DXToRadian(fRot));
	m_v3Position.y = c_rv3Pos.y + fDistance * cos((double)D3DXToRadian(fRot));
	m_v3Position.z = c_rv3Pos.z + frandom(1000.0f, 1500.0f);  // Spawn height: 1000-1500 units

	// Downward velocity (much faster than snow: 500-1500 vs 50-200)
	m_v3Velocity.x = 0.0f;
	m_v3Velocity.y = 0.0f;
	m_v3Velocity.z = frandom(-fFallSpeedMin, -fFallSpeedMax);

	// Size (stretched vertically: 2x8 to 4x20 units)
	m_fHalfWidth = fParticleWidth / 2.0f;
	m_fHalfHeight = fParticleHeight / 2.0f;

	// Activate particle
	m_bActivate = true;
}

// Allocate from pool or create new
CRainParticle * CRainParticle::New()
{
	if (ms_kVct_RainParticlePool.empty())
	{
		return new CRainParticle;
	}

	CRainParticle * pParticle = ms_kVct_RainParticlePool.back();
	ms_kVct_RainParticlePool.pop_back();
	return pParticle;
}

// Return to pool for reuse
void CRainParticle::Delete(CRainParticle * pRainParticle)
{
	ms_kVct_RainParticlePool.push_back(pRainParticle);
}

// Clean up entire pool
void CRainParticle::DestroyPool()
{
	stl_wipe(ms_kVct_RainParticlePool);
}

// Constructor
CRainParticle::CRainParticle()
{
}

// Destructor
CRainParticle::~CRainParticle()
{
}
