#pragma once

#include "GrpBase.h"
#include <vector>

class CGraphicIndexBuffer : public CGraphicBase
{
public:
	CGraphicIndexBuffer();
	virtual ~CGraphicIndexBuffer();

	void Destroy();
	bool Create(int idxCount, GrpFormatType formatType);
	bool Create(int faceCount, TFace* faces);

	bool CreateDeviceObjects();
	void DestroyDeviceObjects();

	bool Copy(int bufSize, const void* srcIndices);

	bool Lock(void** pretIndices) const;
	void Unlock() const;

	bool Lock(void** pretIndices);
	void Unlock();

	void SetIndices(int startIndex = 0) const;

	inline ID3D11Buffer* GetIndexBuffer() const { return m_lpd3dIdxBuf; }

	int GetIndexCount() const { return m_iidxCount; }

protected:
	void Initialize();

protected:
	ID3D11Buffer*	m_lpd3dIdxBuf;
	DWORD					m_dwBufferSize;
	GrpFormatType			m_formatType;
	int						m_iidxCount;
	std::vector<BYTE>		m_cpuShadowIndices;
	mutable bool			m_bCpuLockActive;
};
