#pragma once

#include <DirectXMath.h>

// SParticleVertex is shared between SnowParticle and RainParticle
// Defined in SnowParticle.h, included here for convenience
struct SParticleVertex
{
	DirectX::SimpleMath::Vector3 v3Pos;
	float u, v;
};

class CRainParticle
{
	public:
		CRainParticle();
		~CRainParticle();

		// Pool management
		static CRainParticle * New();
		static void Delete(CRainParticle * pRainParticle);
		static void DestroyPool();

		// Initialization with parameters (configurable, unlike snow)
		void Init(const DirectX::SimpleMath::Vector3 & c_rv3Pos,
				  float fFallSpeedMin,
				  float fFallSpeedMax,
				  float fParticleWidth,
				  float fParticleHeight,
				  float fSpawnRadius);

		// Camera alignment
		void SetCameraVertex(const DirectX::SimpleMath::Vector3 & rv3Up, const DirectX::SimpleMath::Vector3 & rv3Cross);

		// State query
		bool IsActivate();

		// Update with wind influence
		void Update(float fElapsedTime, const DirectX::SimpleMath::Vector3 & c_rv3Pos, const DirectX::SimpleMath::Vector3 & v3Wind);

		// Get quad vertices for rendering
		void GetVerticies(SParticleVertex & rv3Vertex1, SParticleVertex & rv3Vertex2,
						  SParticleVertex & rv3Vertex3, SParticleVertex & rv3Vertex4);

	protected:
		bool m_bActivate;           // Particle is alive
		float m_fHalfWidth;         // Half size of particle quad (width)
		float m_fHalfHeight;        // Half size of particle quad (height)

		DirectX::SimpleMath::Vector3 m_v3Velocity;   // Movement velocity (downward)
		DirectX::SimpleMath::Vector3 m_v3Position;   // World position

		DirectX::SimpleMath::Vector3 m_v3Up;         // Camera-aligned up vector
		DirectX::SimpleMath::Vector3 m_v3Cross;      // Camera-aligned right vector

	public:
		// Static pool for reusing particles
		static std::vector<CRainParticle*> ms_kVct_RainParticlePool;
};
