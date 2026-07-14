#pragma once

#include "GrpBase.h"
#include <vector>

class CGraphicVertexBuffer : public CGraphicBase
{
	public:
		CGraphicVertexBuffer();
		virtual ~CGraphicVertexBuffer();

		void	Destroy();
		virtual bool	Create(int vtxCount, DWORD fvf, DWORD usage, GrpPoolType poolType);
		bool	CreateWithStride(int vtxCount, DWORD vertexStride, DWORD usage, GrpPoolType poolType);

		bool	CreateDeviceObjects();
		void	DestroyDeviceObjects();

		bool	Copy(int bufSize, const void* srcVertices);

		bool	LockRange(unsigned count, void** pretVertices) const;
		bool	Lock(void** pretVertices) const;
		bool	Unlock() const;

		bool	LockDynamic(void** pretVertices);
		virtual bool	Lock(void** pretVertices);
		bool	Unlock();

		void	SetStream(int stride, int layer=0) const;
			
		int		GetVertexCount() const;
		int		GetVertexStride() const;
		DWORD	GetFlexibleVertexFormat() const;

		inline	ID3D11Buffer* GetVertexBuffer() const { return m_lpd3dVB; }
		inline	DWORD GetBufferSize() const	{ return m_dwBufferSize; }

		bool	IsEmpty() const;

	protected:
		void	Initialize();

	protected:
		ID3D11Buffer* m_lpd3dVB;

		DWORD					m_dwBufferSize;
		DWORD					m_dwFVF;
		DWORD					m_dwVertexStride;  // DX11: explicit stride (0 = use FVF)
		DWORD					m_dwUsage;
		GrpPoolType				m_poolType;
		int						m_vtxCount;
		DWORD					m_dwLockFlag;
		std::vector<BYTE>		m_cpuShadowVertices;
		mutable bool			m_bCpuLockActive;
};
