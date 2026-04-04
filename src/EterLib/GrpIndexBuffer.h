#pragma once

#include "GrpBase.h"
#include <vector>

class CGraphicIndexBuffer : public CGraphicBase
{
public:
	CGraphicIndexBuffer();
	virtual ~CGraphicIndexBuffer();

	void Destroy();
	bool Create(int idxCount, D3DFORMAT d3dFmt);
	bool Create(int faceCount, TFace* faces);

	bool CreateDeviceObjects();
	void DestroyDeviceObjects();

	bool Copy(int bufSize, const void* srcIndices);

	bool Lock(void** pretIndices) const;
	void Unlock() const;

	bool Lock(void** pretIndices);
	void Unlock();

	void SetIndices(int startIndex = 0) const;

	ID3D11Buffer* GetD3DIndexBuffer() const;
	// M2-GRPBASE-TYPE-CUT-73: DX11-native API (replaces legacy GetD3DIndexBuffer)
	inline ID3D11Buffer* GetD3D11Buffer() const { return m_lpd3dIdxBuf; }

	int GetIndexCount() const { return m_iidxCount; }

protected:
	void Initialize();

protected:
	ID3D11Buffer*	m_lpd3dIdxBuf;
	DWORD					m_dwBufferSize;
	D3DFORMAT				m_d3dFmt;
	int						m_iidxCount;
	std::vector<BYTE>		m_cpuShadowIndices;
	mutable bool			m_bCpuLockActive;
};
