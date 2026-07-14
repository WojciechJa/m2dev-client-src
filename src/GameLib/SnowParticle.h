#pragma once

#include <DirectXMath.h>

struct SParticleVertex
{
	DirectX::SimpleMath::Vector3 v3Pos;
	float u, v;
};

struct BlurVertex
{
	DirectX::SimpleMath::Vector3 pos;
	FLOAT       rhw;
    DWORD       color;
	FLOAT		tu, tv;

	static const DWORD FVF;

	BlurVertex(DirectX::SimpleMath::Vector3 p, float w,DWORD c,float u,float v):pos(p),rhw(w),color(c),tu(u),tv(v) {}
	~BlurVertex(){};
};

class CSnowParticle
{
	public:
		CSnowParticle();
		~CSnowParticle();

		static CSnowParticle * New();
		static void Delete(CSnowParticle * pSnowParticle);
		static void DestroyPool();

		void Init(const DirectX::SimpleMath::Vector3 & c_rv3Pos,
				  float fFallSpeedMin,
				  float fFallSpeedMax,
				  float fParticleSize);

		void SetCameraVertex(const DirectX::SimpleMath::Vector3 & rv3Up, const DirectX::SimpleMath::Vector3 & rv3Cross);
		bool IsActivate();

		void Update(float fElapsedTime, const DirectX::SimpleMath::Vector3 & c_rv3Pos);
		void GetVerticies(SParticleVertex & rv3Vertex1, SParticleVertex & rv3Vertex2,
						  SParticleVertex & rv3Vertex3, SParticleVertex & rv3Vertex4);

	protected:
		bool m_bActivate;
		bool m_bChangedSize;
		float m_fHalfWidth;
		float m_fHalfHeight;

		DirectX::SimpleMath::Vector3 m_v3Velocity;
		DirectX::SimpleMath::Vector3 m_v3Position;

		DirectX::SimpleMath::Vector3 m_v3Up;
		DirectX::SimpleMath::Vector3 m_v3Cross;

		float m_fPeriod;
		float m_fcurRadian;
		float m_fAmplitude;

	public:
		static std::vector<CSnowParticle*> ms_kVct_SnowParticlePool;
};
