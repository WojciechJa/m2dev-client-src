#include "StdAfx.h"
#include "EterBase/Stl.h"
#include "GrpVertexBuffer.h"
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

int CGraphicVertexBuffer::GetVertexStride() const
{
	if (m_dwVertexStride != 0)
		return static_cast<int>(m_dwVertexStride);

	return static_cast<int>(D3DXGetFVFVertexSize(m_dwFVF));
}

DWORD CGraphicVertexBuffer::GetFlexibleVertexFormat() const
{
	return m_dwFVF;
}

int CGraphicVertexBuffer::GetVertexCount() const
{
	return m_vtxCount;
}

void CGraphicVertexBuffer::SetStream(int stride, int layer) const
{
	if (!m_lpd3dVB)
		return;

	const UINT resolvedStride = (stride > 0) ? static_cast<UINT>(stride) : static_cast<UINT>(GetVertexStride());
	STATEMANAGER.SetStreamSource(static_cast<UINT>(layer), m_lpd3dVB, resolvedStride);
}

bool CGraphicVertexBuffer::LockRange(unsigned count, void** pretVertices) const
{
	if (!pretVertices || count == 0)
		return false;

	const size_t lockSize = static_cast<size_t>(GetVertexStride()) * static_cast<size_t>(count);
	if (m_cpuShadowVertices.size() < lockSize)
		return false;

	*pretVertices = const_cast<BYTE*>(m_cpuShadowVertices.data());
	m_bCpuLockActive = true;
	return true;
}

bool CGraphicVertexBuffer::Lock(void** pretVertices) const
{
	if (!pretVertices || m_cpuShadowVertices.empty())
		return false;

	*pretVertices = const_cast<BYTE*>(m_cpuShadowVertices.data());
	m_bCpuLockActive = true;
	return true;
}

bool CGraphicVertexBuffer::Unlock() const
{
	if (!m_bCpuLockActive)
		return false;

	m_bCpuLockActive = false;

	if (!m_lpd3dVB || m_cpuShadowVertices.empty())
		return true;

	ID3D11DeviceContext* pContext = GetDX11Context();
	if (!pContext)
		return false;

	if (m_dwUsage & GRP_USAGE_DYNAMIC)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(pContext->Map(m_lpd3dVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return false;

		memcpy(mapped.pData, m_cpuShadowVertices.data(), m_dwBufferSize);
		pContext->Unmap(m_lpd3dVB, 0);
	}
	else
	{
		pContext->UpdateSubresource(m_lpd3dVB, 0, nullptr, m_cpuShadowVertices.data(), 0, 0);
	}

	return true;
}

bool CGraphicVertexBuffer::LockDynamic(void** pretVertices)
{
	return Lock(pretVertices);
}

bool CGraphicVertexBuffer::Lock(void** pretVertices)
{
	return static_cast<const CGraphicVertexBuffer*>(this)->Lock(pretVertices);
}

bool CGraphicVertexBuffer::Unlock()
{
	return static_cast<const CGraphicVertexBuffer*>(this)->Unlock();
}

bool CGraphicVertexBuffer::Copy(int bufSize, const void* srcVertices)
{
	if (!srcVertices || bufSize <= 0)
		return false;

	void* dstVertices = nullptr;
	if (!Lock(&dstVertices))
		return false;

	memcpy(dstVertices, srcVertices, static_cast<size_t>(bufSize));
	return Unlock();
}

bool CGraphicVertexBuffer::CreateDeviceObjects()
{
	if (m_dwBufferSize == 0)
		return false;

	m_cpuShadowVertices.resize(m_dwBufferSize);
	m_bCpuLockActive = false;

	ID3D11Device* pDevice = GetDX11Device();
	if (!pDevice)
		return true;

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = m_dwBufferSize;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.Usage = (m_dwUsage & GRP_USAGE_DYNAMIC) ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = (desc.Usage == D3D11_USAGE_DYNAMIC) ? D3D11_CPU_ACCESS_WRITE : 0u;

	D3D11_SUBRESOURCE_DATA initData = {};
	D3D11_SUBRESOURCE_DATA* pInitData = nullptr;
	if (!m_cpuShadowVertices.empty())
	{
		initData.pSysMem = m_cpuShadowVertices.data();
		pInitData = &initData;
	}

	return SUCCEEDED(pDevice->CreateBuffer(&desc, pInitData, &m_lpd3dVB));
}

void CGraphicVertexBuffer::DestroyDeviceObjects()
{
	safe_release(m_lpd3dVB);
	m_bCpuLockActive = false;
}

bool CGraphicVertexBuffer::Create(int vtxCount, DWORD fvf, DWORD usage, GrpPoolType poolType)
{
	assert(vtxCount > 0);

	Destroy();

	m_vtxCount = vtxCount;
	m_dwVertexStride = D3DXGetFVFVertexSize(fvf);
	m_dwBufferSize = m_dwVertexStride * static_cast<DWORD>(m_vtxCount);
	m_poolType = poolType;
	m_dwUsage = usage;
	m_dwFVF = fvf;
	m_dwLockFlag = (usage & GRP_USAGE_DYNAMIC) ? GRP_LOCK_DISCARD : 0u;

	return CreateDeviceObjects();
}

bool CGraphicVertexBuffer::CreateWithStride(int vtxCount, DWORD vertexStride, DWORD usage, GrpPoolType poolType)
{
	assert(vtxCount > 0);
	assert(vertexStride > 0);

	Destroy();

	m_vtxCount = vtxCount;
	m_dwVertexStride = vertexStride;
	m_dwBufferSize = vertexStride * static_cast<DWORD>(m_vtxCount);
	m_poolType = poolType;
	m_dwUsage = usage;
	m_dwFVF = 0u;
	m_dwLockFlag = (usage & GRP_USAGE_DYNAMIC) ? GRP_LOCK_DISCARD : 0u;

	return CreateDeviceObjects();
}

void CGraphicVertexBuffer::Destroy()
{
	DestroyDeviceObjects();
}

void CGraphicVertexBuffer::Initialize()
{
	m_lpd3dVB = nullptr;
	m_dwBufferSize = 0u;
	m_dwFVF = 0u;
	m_dwVertexStride = 0u;
	m_dwUsage = 0u;
	m_poolType = GRP_POOL_DEFAULT;
	m_vtxCount = 0;
	m_dwLockFlag = 0u;
	m_bCpuLockActive = false;
	m_cpuShadowVertices.clear();
}

CGraphicVertexBuffer::CGraphicVertexBuffer()
{
	Initialize();
}

CGraphicVertexBuffer::~CGraphicVertexBuffer()
{
	Destroy();
}

bool CGraphicVertexBuffer::IsEmpty() const
{
	return !m_lpd3dVB && m_cpuShadowVertices.empty();
}
