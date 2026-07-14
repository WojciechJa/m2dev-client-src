#include "StdAfx.h"
#include "EterBase/Stl.h"
#include "GrpIndexBuffer.h"
#include "GrpDeviceDX11.h"
#include "StateManager.h"

#include <algorithm>

namespace
{
	ID3D11Device* GetDX11Device()
	{
		CGraphicDeviceDX11* pDevice = CGraphicDeviceDX11::GetActiveDevice();
		if (!pDevice || !pDevice->IsValid())
			return nullptr;
		return pDevice->GetDevice();
	}

	ID3D11DeviceContext* GetDX11Context()
	{
		CGraphicDeviceDX11* pDevice = CGraphicDeviceDX11::GetActiveDevice();
		if (!pDevice || !pDevice->IsValid())
			return nullptr;
		return pDevice->GetContext();
	}
}

void CGraphicIndexBuffer::SetIndices(int startIndex) const
{
	if (!m_lpd3dIdxBuf)
		return;

	CStateManager* pStateManager = CStateManager::InstancePtr();
	if (!pStateManager)
		return;

	const UINT bytesPerIndex = (m_formatType == GRP_FMT_INDEX32) ? 4u : 2u;
	const UINT byteOffset = (startIndex > 0) ? static_cast<UINT>(startIndex) * bytesPerIndex : 0u;
	pStateManager->SetIndices(m_lpd3dIdxBuf, static_cast<int>(m_formatType), byteOffset);
}

bool CGraphicIndexBuffer::Lock(void** pretIndices) const
{
	if (!pretIndices || m_cpuShadowIndices.empty())
		return false;

	*pretIndices = const_cast<BYTE*>(m_cpuShadowIndices.data());
	m_bCpuLockActive = true;
	return true;
}

void CGraphicIndexBuffer::Unlock() const
{
	if (!m_bCpuLockActive)
		return;

	m_bCpuLockActive = false;

	if (!m_lpd3dIdxBuf || m_cpuShadowIndices.empty())
		return;

	ID3D11DeviceContext* pContext = GetDX11Context();
	if (!pContext)
		return;

	pContext->UpdateSubresource(m_lpd3dIdxBuf, 0, nullptr, m_cpuShadowIndices.data(), 0, 0);
}

bool CGraphicIndexBuffer::Lock(void** pretIndices)
{
	return static_cast<const CGraphicIndexBuffer*>(this)->Lock(pretIndices);
}

void CGraphicIndexBuffer::Unlock()
{
	static_cast<const CGraphicIndexBuffer*>(this)->Unlock();
}

bool CGraphicIndexBuffer::Copy(int bufSize, const void* srcIndices)
{
	if (!srcIndices || bufSize <= 0)
		return false;

	void* pDstIndices = nullptr;
	if (!Lock(&pDstIndices))
		return false;

	memcpy(pDstIndices, srcIndices, static_cast<size_t>(bufSize));
	Unlock();
	return true;
}

bool CGraphicIndexBuffer::Create(int faceCount, TFace* faces)
{
	const int idxCount = faceCount * 3;
	m_iidxCount = idxCount;
	if (!Create(idxCount, GRP_FMT_INDEX16))
		return false;

	WORD* pDstIndices = nullptr;
	if (!Lock(reinterpret_cast<void**>(&pDstIndices)))
		return false;

	for (int i = 0; i < faceCount; ++i, pDstIndices += 3)
	{
		const TFace* pFace = faces + i;
		pDstIndices[0] = pFace->indices[0];
		pDstIndices[1] = pFace->indices[1];
		pDstIndices[2] = pFace->indices[2];
	}

	Unlock();
	return true;
}

bool CGraphicIndexBuffer::CreateDeviceObjects()
{
	if (m_dwBufferSize == 0)
		return false;

	m_cpuShadowIndices.resize(m_dwBufferSize);
	m_bCpuLockActive = false;

	ID3D11Device* pDevice = GetDX11Device();
	if (!pDevice)
		return true;

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = m_dwBufferSize;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0u;

	D3D11_SUBRESOURCE_DATA initData = {};
	D3D11_SUBRESOURCE_DATA* pInitData = nullptr;
	if (!m_cpuShadowIndices.empty())
	{
		initData.pSysMem = m_cpuShadowIndices.data();
		pInitData = &initData;
	}

	return SUCCEEDED(pDevice->CreateBuffer(&desc, pInitData, &m_lpd3dIdxBuf));
}

void CGraphicIndexBuffer::DestroyDeviceObjects()
{
	safe_release(m_lpd3dIdxBuf);
	m_bCpuLockActive = false;
}

bool CGraphicIndexBuffer::Create(int idxCount, GrpFormatType formatType)
{
	Destroy();

	m_iidxCount = idxCount;
	const UINT bytesPerIndex = (formatType == GRP_FMT_INDEX32) ? 4u : 2u;
	m_dwBufferSize = bytesPerIndex * static_cast<UINT>(idxCount);
	m_formatType = formatType;

	return CreateDeviceObjects();
}

void CGraphicIndexBuffer::Destroy()
{
	DestroyDeviceObjects();
}

void CGraphicIndexBuffer::Initialize()
{
	m_lpd3dIdxBuf = nullptr;
	m_dwBufferSize = 0u;
	m_formatType = GRP_FMT_INDEX16;
	m_iidxCount = 0;
	m_bCpuLockActive = false;
	m_cpuShadowIndices.clear();
}

CGraphicIndexBuffer::CGraphicIndexBuffer()
{
	Initialize();
}

CGraphicIndexBuffer::~CGraphicIndexBuffer()
{
	Destroy();
}
