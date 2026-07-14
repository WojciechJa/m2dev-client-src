#include "StdAfx.h"
#include "Eterlib/ResourceManager.h"
#include "PackLib/PackManager.h"
#include "EffectMesh.h"
#include <cmath>
#include <unordered_set>

namespace
{
	bool HasJpegExtension(const std::string& stLowerPath)
	{
		const size_t stLen = stLowerPath.size();
		if (stLen >= 4 && 0 == stLowerPath.compare(stLen - 4, 4, ".jpg"))
			return true;
		if (stLen >= 5 && 0 == stLowerPath.compare(stLen - 5, 5, ".jpeg"))
			return true;
		return false;
	}

	std::string ResolveEffectTexturePath(const std::string& stPath)
	{
		if (stPath.empty())
			return stPath;

		std::string stLower = stPath;
		for (size_t i = 0; i < stLower.size(); ++i)
		{
			if (stLower[i] == '\\')
				stLower[i] = '/';
			stLower[i] = static_cast<char>(tolower(static_cast<unsigned char>(stLower[i])));
		}

		if (std::string::npos == stLower.find("/effect/etc/click/") || !HasJpegExtension(stLower))
			return stPath;

		std::string stDDS = stPath;
		const size_t stDotPos = stDDS.find_last_of('.');
		if (std::string::npos == stDotPos)
			return stPath;

		stDDS.replace(stDotPos, std::string::npos, ".dds");
		if (CPackManager::Instance().IsExist(stDDS))
		{
			static std::unordered_set<std::string> s_kLoggedRemaps;
			if (s_kLoggedRemaps.insert(stPath).second)
			{
				TraceError("DX11_TARGET_TEXTURE_REMAP source=%s remap=%s", stPath.c_str(), stDDS.c_str());
			}
			return stDDS;
		}

		return stPath;
	}

	bool ValidateEffectFrameIndices(const std::vector<int>& rkGeomIndices,
		const std::vector<int>& rkTexIndices,
		DWORD dwVertexCount,
		DWORD dwTexVertexCount,
		int* piBadGeomIndex,
		int* piBadTexIndex)
	{
		if (piBadGeomIndex)
			*piBadGeomIndex = -1;
		if (piBadTexIndex)
			*piBadTexIndex = -1;

		const size_t stIndexCount = rkGeomIndices.size();
		for (size_t i = 0; i < stIndexCount; ++i)
		{
			const int iGeomIndex = rkGeomIndices[i];
			if (iGeomIndex < 0 || iGeomIndex >= static_cast<int>(dwVertexCount))
			{
				if (piBadGeomIndex)
					*piBadGeomIndex = iGeomIndex;
				return false;
			}

			const int iTexIndex = rkTexIndices[i];
			if (iTexIndex < 0 || iTexIndex >= static_cast<int>(dwTexVertexCount))
			{
				if (piBadTexIndex)
					*piBadTexIndex = iTexIndex;
				return false;
			}
		}

		return true;
	}
}

CDynamicPool<CEffectMesh::SEffectMeshData> CEffectMesh::SEffectMeshData::ms_kPool;

CEffectMesh::SEffectMeshData* CEffectMesh::SEffectMeshData::New()
{
	return ms_kPool.Alloc();
}

void CEffectMesh::SEffectMeshData::Delete(SEffectMeshData* pkData)
{
	pkData->EffectFrameDataVector.clear();
	pkData->pImageVector.clear();

	ms_kPool.Free(pkData);
}

void CEffectMesh::SEffectMeshData::DestroySystem()
{
	ms_kPool.Destroy();
}


DWORD CEffectMesh::GetFrameCount()
{
	return m_iFrameCount;
}

DWORD CEffectMesh::GetMeshCount()
{
	return m_pEffectMeshDataVector.size();
}

CEffectMesh::TEffectMeshData * CEffectMesh::GetMeshDataPointer(DWORD dwMeshIndex)
{
	assert(dwMeshIndex < m_pEffectMeshDataVector.size());
	return m_pEffectMeshDataVector[dwMeshIndex];
}

std::vector<CGraphicImage*>* CEffectMesh::GetTextureVectorPointer(DWORD dwMeshIndex)
{
	if (dwMeshIndex>=m_pEffectMeshDataVector.size())
		return NULL;

	return &m_pEffectMeshDataVector[dwMeshIndex]->pImageVector;
}

std::vector<CGraphicImage*> & CEffectMesh::GetTextureVectorReference(DWORD dwMeshIndex)
{
	return m_pEffectMeshDataVector[dwMeshIndex]->pImageVector;
}

CEffectMesh::TType CEffectMesh::Type()
{
	static TType s_type = StringToType("CEffectMesh");
	return s_type;
}

bool CEffectMesh::OnIsType(TType type)
{
	if (CEffectMesh::Type() == type)
		return true;

	return CResource::OnIsType(type);
}

bool CEffectMesh::OnLoad(int iSize, const void * c_pvBuf)
{
	if (!c_pvBuf)
		return false;

	const BYTE * c_pbBuf = static_cast<const BYTE *> (c_pvBuf);

	char szHeader[10+1];
	memcpy(szHeader, c_pbBuf, 10+1);
	c_pbBuf += 10+1;

	if (0 == strcmp("EffectData", szHeader))
	{
		if (!__LoadData_Ver001(iSize, c_pbBuf))
			return false;
	}
	else if (0 == strcmp("MDEData002", szHeader))
	{
		if (!__LoadData_Ver002(iSize, c_pbBuf))
			return false;
	}
	else
	{
		return false;
	}

	m_isData = true;
	return true;
}

BOOL CEffectMesh::__LoadData_Ver002(int iSize, const BYTE * c_pbBuf)
{
	std::vector<D3DXVECTOR3> v3VertexVector;
	std::vector<int> iIndexVector;
	std::vector<D3DXVECTOR2> v3TextureVertexVector;
	std::vector<int> iTextureIndexVector;

	m_iGeomCount = *(int *)c_pbBuf;
	c_pbBuf += 4;
	m_iFrameCount = *(int *)c_pbBuf;
	c_pbBuf += 4;

	m_pEffectMeshDataVector.clear();
	m_pEffectMeshDataVector.resize(m_iGeomCount);

	for (short n = 0; n < m_iGeomCount; ++n)
	{
		SEffectMeshData * pMeshData = SEffectMeshData::New();

		memcpy(pMeshData->szObjectName, c_pbBuf, 32);
		c_pbBuf += 32;
		memcpy(pMeshData->szDiffuseMapFileName, c_pbBuf, 128);
		c_pbBuf += 128;

		pMeshData->EffectFrameDataVector.clear();
		pMeshData->EffectFrameDataVector.resize(m_iFrameCount);

		for(int i = 0; i < m_iFrameCount; ++i)
		{
			TEffectFrameData & rFrameData = pMeshData->EffectFrameDataVector[i];

			memcpy(&rFrameData.byChangedFrame, c_pbBuf, sizeof(BYTE));
			c_pbBuf += sizeof(BYTE);

			memcpy(&rFrameData.fVisibility, c_pbBuf, sizeof(float));
			c_pbBuf += sizeof(float);

			memcpy(&rFrameData.dwVertexCount, c_pbBuf, sizeof(DWORD));
			c_pbBuf += sizeof(DWORD);

			memcpy(&rFrameData.dwIndexCount, c_pbBuf, sizeof(DWORD));
			c_pbBuf += sizeof(DWORD);

			memcpy(&rFrameData.dwTextureVertexCount, c_pbBuf, sizeof(DWORD));
			c_pbBuf += sizeof(DWORD);

			v3VertexVector.clear();
			v3VertexVector.resize(rFrameData.dwVertexCount);
			iIndexVector.clear();
			iIndexVector.resize(rFrameData.dwIndexCount);
			v3TextureVertexVector.clear();
			v3TextureVertexVector.resize(rFrameData.dwTextureVertexCount);
			iTextureIndexVector.clear();
			iTextureIndexVector.resize(rFrameData.dwIndexCount);

			memcpy(&v3VertexVector[0], c_pbBuf, rFrameData.dwVertexCount*sizeof(D3DXVECTOR3));
			c_pbBuf += rFrameData.dwVertexCount*sizeof(D3DXVECTOR3);
			memcpy(&iIndexVector[0], c_pbBuf, rFrameData.dwIndexCount*sizeof(int));
			c_pbBuf += rFrameData.dwIndexCount*sizeof(int);
			memcpy(&v3TextureVertexVector[0], c_pbBuf, rFrameData.dwTextureVertexCount*sizeof(D3DXVECTOR2));
			c_pbBuf += rFrameData.dwTextureVertexCount*sizeof(D3DXVECTOR2);
			memcpy(&iTextureIndexVector[0], c_pbBuf, rFrameData.dwIndexCount*sizeof(int));
			c_pbBuf += rFrameData.dwIndexCount*sizeof(int);

			///////////////////////////////
			int iBadGeomIndex = -1;
			int iBadTexIndex = -1;
			if (!ValidateEffectFrameIndices(iIndexVector,
				iTextureIndexVector,
				rFrameData.dwVertexCount,
				rFrameData.dwTextureVertexCount,
				&iBadGeomIndex,
				&iBadTexIndex))
			{
				TraceError(
					"DX11_EFFECT_MESH_DATA_SKIP reason=invalid_index mesh=%d frame=%d geom_index=%d tex_index=%d file=%s",
					static_cast<int>(n),
					i,
					iBadGeomIndex,
					iBadTexIndex,
					GetFileName());
				rFrameData.fVisibility = 0.0f;
				rFrameData.dwIndexCount = 0u;
				rFrameData.PDTVertexVector.clear();
				continue;
			}

			rFrameData.PDTVertexVector.clear();
			rFrameData.PDTVertexVector.resize(rFrameData.dwIndexCount);
			bool bInvalidTexCoord = false;
			for (DWORD j = 0; j < rFrameData.dwIndexCount; ++j)
			{
				TPTVertex & rVertex = rFrameData.PDTVertexVector[j];

				DWORD dwIndex = iIndexVector[j];
				DWORD dwTextureIndex = iTextureIndexVector[j];

				assert(dwIndex < v3VertexVector.size());
				assert(dwTextureIndex < v3TextureVertexVector.size());

				rVertex.position = v3VertexVector[dwIndex];
				rVertex.texCoord = v3TextureVertexVector[dwTextureIndex];
				if (!std::isfinite(rVertex.texCoord.x) || !std::isfinite(rVertex.texCoord.y))
				{
					bInvalidTexCoord = true;
					break;
				}
				rVertex.texCoord.y *= -1;
			}
			if (bInvalidTexCoord)
			{
				TraceError(
					"DX11_EFFECT_MESH_DATA_SKIP reason=invalid_texcoord mesh=%d frame=%d file=%s",
					static_cast<int>(n),
					i,
					GetFileName());
				rFrameData.fVisibility = 0.0f;
				rFrameData.dwIndexCount = 0u;
				rFrameData.PDTVertexVector.clear();
				continue;
			}
		}

		////////////////////////////////////

		pMeshData->pImageVector.clear();

		std::string strExtension;
		GetFileExtension(pMeshData->szDiffuseMapFileName, strlen(pMeshData->szDiffuseMapFileName), &strExtension);
		stl_lowers(strExtension);

		if (0 == strExtension.compare("ifl"))
		{
			TPackFile File;

			if (CPackManager::Instance().GetFile(pMeshData->szDiffuseMapFileName, File))
			{
				CMemoryTextFileLoader textFileLoader;
				std::vector<std::string> stTokenVector;

				textFileLoader.Bind(File.size(), File.data());

				std::string strPathName;
				GetOnlyPathName(pMeshData->szDiffuseMapFileName, strPathName);

				std::string strTextureFileName;
				for (DWORD i = 0; i < textFileLoader.GetLineCount(); ++i)
				{
					const std::string & c_rstrFileName = textFileLoader.GetLineString(i);

					if (c_rstrFileName.empty())
						continue;

					strTextureFileName = strPathName;
					strTextureFileName += c_rstrFileName;
					const std::string stResolvedPath = ResolveEffectTexturePath(strTextureFileName);
					CGraphicImage * pImage = (CGraphicImage *)CResourceManager::Instance().GetResourcePointer(stResolvedPath.c_str());

					pMeshData->pImageVector.push_back(pImage);
				}
			}
		}
		else
		{
			const std::string stResolvedPath = ResolveEffectTexturePath(pMeshData->szDiffuseMapFileName);
			CGraphicImage * pImage = (CGraphicImage *)CResourceManager::Instance().GetResourcePointer(stResolvedPath.c_str());

			pMeshData->pImageVector.push_back(pImage);
		}

		////////////////////////////////////

		m_pEffectMeshDataVector[n] = pMeshData;
	}

	return TRUE;
}

BOOL CEffectMesh::__LoadData_Ver001(int iSize, const BYTE * c_pbBuf)
{
	std::vector<D3DXVECTOR3> v3VertexVector;
	std::vector<int> iIndexVector;
	std::vector<D3DXVECTOR2> v3TextureVertexVector;
	std::vector<int> iTextureIndexVector;

	m_iGeomCount = *(int *)c_pbBuf;
	c_pbBuf += 4;
	m_iFrameCount = *(int *)c_pbBuf;
	c_pbBuf += 4;

	m_pEffectMeshDataVector.clear();
	m_pEffectMeshDataVector.resize(m_iGeomCount);

	for (short n = 0; n < m_iGeomCount; ++n)
	{
		SEffectMeshData * pMeshData = SEffectMeshData::New();

		memcpy(pMeshData->szObjectName, c_pbBuf, 32);
		c_pbBuf += 32;
		memcpy(pMeshData->szDiffuseMapFileName, c_pbBuf, 128);
		c_pbBuf += 128;

		//

		DWORD dwVertexCount;
		DWORD dwIndexCount;
		DWORD dwTextureVertexCount;

		memcpy(&dwVertexCount, c_pbBuf, sizeof(DWORD));
		c_pbBuf += sizeof(DWORD);

		memcpy(&dwIndexCount, c_pbBuf, sizeof(DWORD));
		c_pbBuf += sizeof(DWORD);

		memcpy(&dwTextureVertexCount, c_pbBuf, sizeof(DWORD));
		c_pbBuf += sizeof(DWORD);

		pMeshData->EffectFrameDataVector.clear();
		pMeshData->EffectFrameDataVector.resize(m_iFrameCount);

		for(int i = 0; i < m_iFrameCount; ++i)
		{
			TEffectFrameData & rFrameData = pMeshData->EffectFrameDataVector[i];

			rFrameData.dwVertexCount = dwVertexCount;
			rFrameData.dwIndexCount = dwIndexCount;
			rFrameData.dwTextureVertexCount = dwTextureVertexCount;

			v3VertexVector.clear();
			v3VertexVector.resize(rFrameData.dwVertexCount);
			iIndexVector.clear();
			iIndexVector.resize(rFrameData.dwIndexCount);
			v3TextureVertexVector.clear();
			v3TextureVertexVector.resize(rFrameData.dwTextureVertexCount);
			iTextureIndexVector.clear();
			iTextureIndexVector.resize(rFrameData.dwIndexCount);

			memcpy(&rFrameData.fVisibility, c_pbBuf, sizeof(float));
			c_pbBuf += sizeof(float);
			memcpy(&v3VertexVector[0], c_pbBuf, rFrameData.dwVertexCount*sizeof(D3DXVECTOR3));
			c_pbBuf += rFrameData.dwVertexCount*sizeof(D3DXVECTOR3);
			memcpy(&iIndexVector[0], c_pbBuf, rFrameData.dwIndexCount*sizeof(int));
			c_pbBuf += rFrameData.dwIndexCount*sizeof(int);
			memcpy(&v3TextureVertexVector[0], c_pbBuf, rFrameData.dwTextureVertexCount*sizeof(D3DXVECTOR2));
			c_pbBuf += rFrameData.dwTextureVertexCount*sizeof(D3DXVECTOR2);
			memcpy(&iTextureIndexVector[0], c_pbBuf, rFrameData.dwIndexCount*sizeof(int));
			c_pbBuf += rFrameData.dwIndexCount*sizeof(int);

			///////////////////////////////
			int iBadGeomIndex = -1;
			int iBadTexIndex = -1;
			if (!ValidateEffectFrameIndices(iIndexVector,
				iTextureIndexVector,
				rFrameData.dwVertexCount,
				rFrameData.dwTextureVertexCount,
				&iBadGeomIndex,
				&iBadTexIndex))
			{
				TraceError(
					"DX11_EFFECT_MESH_DATA_SKIP reason=invalid_index mesh=%d frame=%d geom_index=%d tex_index=%d file=%s",
					static_cast<int>(n),
					i,
					iBadGeomIndex,
					iBadTexIndex,
					GetFileName());
				rFrameData.fVisibility = 0.0f;
				rFrameData.dwIndexCount = 0u;
				rFrameData.PDTVertexVector.clear();
				continue;
			}

			rFrameData.PDTVertexVector.clear();
			rFrameData.PDTVertexVector.resize(rFrameData.dwIndexCount);
			bool bInvalidTexCoord = false;
			for (DWORD j = 0; j < rFrameData.dwIndexCount; ++j)
			{
				TPTVertex & rVertex = rFrameData.PDTVertexVector[j];

				DWORD dwIndex = iIndexVector[j];
				DWORD dwTextureIndex = iTextureIndexVector[j];

				assert(dwIndex < v3VertexVector.size());
				assert(dwTextureIndex < v3TextureVertexVector.size());

				rVertex.position = v3VertexVector[dwIndex];
				rVertex.texCoord = v3TextureVertexVector[dwTextureIndex];
				if (!std::isfinite(rVertex.texCoord.x) || !std::isfinite(rVertex.texCoord.y))
				{
					bInvalidTexCoord = true;
					break;
				}
				rVertex.texCoord.y *= -1;
			}
			if (bInvalidTexCoord)
			{
				TraceError(
					"DX11_EFFECT_MESH_DATA_SKIP reason=invalid_texcoord mesh=%d frame=%d file=%s",
					static_cast<int>(n),
					i,
					GetFileName());
				rFrameData.fVisibility = 0.0f;
				rFrameData.dwIndexCount = 0u;
				rFrameData.PDTVertexVector.clear();
				continue;
			}
		}

		////////////////////////////////////

		pMeshData->pImageVector.clear();

		std::string strExtension;
		GetFileExtension(pMeshData->szDiffuseMapFileName, strlen(pMeshData->szDiffuseMapFileName), &strExtension);
		stl_lowers(strExtension);

		if (0 == strExtension.compare("ifl"))
		{
			TPackFile File;

			if (CPackManager::Instance().GetFile(pMeshData->szDiffuseMapFileName, File))
			{
				CMemoryTextFileLoader textFileLoader;
				std::vector<std::string> stTokenVector;

				textFileLoader.Bind(File.size(), File.data());

				std::string strPathName;
				GetOnlyPathName(pMeshData->szDiffuseMapFileName, strPathName);

				std::string strTextureFileName;
				for (DWORD i = 0; i < textFileLoader.GetLineCount(); ++i)
				{
					const std::string & c_rstrFileName = textFileLoader.GetLineString(i);

					if (c_rstrFileName.empty())
						continue;

					strTextureFileName = strPathName;
					strTextureFileName += c_rstrFileName;
					const std::string stResolvedPath = ResolveEffectTexturePath(strTextureFileName);
					CGraphicImage * pImage = (CGraphicImage *)CResourceManager::Instance().GetResourcePointer(stResolvedPath.c_str());

					pMeshData->pImageVector.push_back(pImage);
				}
			}
		}
		else
		{
			const std::string stResolvedPath = ResolveEffectTexturePath(pMeshData->szDiffuseMapFileName);
			CGraphicImage * pImage = (CGraphicImage *)CResourceManager::Instance().GetResourcePointer(stResolvedPath.c_str());

			pMeshData->pImageVector.push_back(pImage);
		}

		////////////////////////////////////

		m_pEffectMeshDataVector[n] = pMeshData;
	}

	return TRUE;
}

BOOL CEffectMesh::GetMeshElementPointer(DWORD dwMeshIndex, TEffectMeshData ** ppMeshData)
{
	if (dwMeshIndex >= m_pEffectMeshDataVector.size())
		return FALSE;

	*ppMeshData = m_pEffectMeshDataVector[dwMeshIndex];

	return TRUE;
}

void CEffectMesh::OnClear()
{
	if (!m_isData)
		return;

	for (DWORD i = 0; i < m_pEffectMeshDataVector.size(); ++i)
	{
		m_pEffectMeshDataVector[i]->pImageVector.clear();
		m_pEffectMeshDataVector[i]->EffectFrameDataVector.clear();

		SEffectMeshData::Delete(m_pEffectMeshDataVector[i]);
	}
	m_pEffectMeshDataVector.clear();

	m_isData = false;
}

bool CEffectMesh::OnIsEmpty() const
{
	return !m_isData;
}

CEffectMesh::CEffectMesh(const char * c_szFileName) : CResource(c_szFileName)
{
	m_iGeomCount = 0;
	m_iFrameCount = 0;
	m_isData = false;
}

CEffectMesh::~CEffectMesh()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

CDynamicPool<CEffectMeshScript> CEffectMeshScript::ms_kPool;

void CEffectMeshScript::DestroySystem()
{
	ms_kPool.Destroy();
}

CEffectMeshScript* CEffectMeshScript::New()
{
	return ms_kPool.Alloc();
}

void CEffectMeshScript::Delete(CEffectMeshScript* pkData)
{
	pkData->Clear();
	ms_kPool.Free(pkData);
}

void CEffectMeshScript::ReserveMeshData(DWORD dwMeshCount)
{
	if (m_MeshDataVector.size() == dwMeshCount)
		return;

	m_MeshDataVector.clear();
	m_MeshDataVector.resize(dwMeshCount);

	for (DWORD i = 0; i < m_MeshDataVector.size(); ++i)
	{
		TMeshData & rMeshData = m_MeshDataVector[i];

		rMeshData.byBillboardType = MESH_BILLBOARD_TYPE_NONE;
		rMeshData.bBlendingEnable = TRUE;
		rMeshData.byBlendingSrcType = GRP_BLEND_SRCCOLOR;
		rMeshData.byBlendingDestType = GRP_BLEND_ONE;
		rMeshData.bTextureAlphaEnable = TRUE;

		rMeshData.byColorOperationType = GRP_TOP_MODULATE;
		rMeshData.ColorFactor = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);

		rMeshData.bTextureAnimationLoopEnable = true;
		rMeshData.fTextureAnimationFrameDelay = 0.02f;
		rMeshData.dwTextureAnimationStartFrame = 0;
	}
}

const char * CEffectMeshScript::GetMeshFileName()
{
	return m_strMeshFileName.c_str();
}

bool CEffectMeshScript::CheckMeshIndex(DWORD dwMeshIndex)
{
	if (dwMeshIndex >= m_MeshDataVector.size())
		return false;

	return true;
}

bool CEffectMeshScript::GetMeshDataPointer(DWORD dwMeshIndex, TMeshData ** ppMeshData)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return false;

	*ppMeshData = &m_MeshDataVector[dwMeshIndex];

	return true;
}

int CEffectMeshScript::GetMeshDataCount()
{
	return m_MeshDataVector.size();
}

int CEffectMeshScript::GetBillboardType(DWORD dwMeshIndex)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return 0;

	return m_MeshDataVector[dwMeshIndex].byBillboardType;
}
BOOL CEffectMeshScript::isBlendingEnable(DWORD dwMeshIndex)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return FALSE;

	return m_MeshDataVector[dwMeshIndex].bBlendingEnable;
}
BYTE CEffectMeshScript::GetBlendingSrcType(DWORD dwMeshIndex)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return false;

	return m_MeshDataVector[dwMeshIndex].byBlendingSrcType;
}
BYTE CEffectMeshScript::GetBlendingDestType(DWORD dwMeshIndex)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return false;

	return m_MeshDataVector[dwMeshIndex].byBlendingDestType;
}
BOOL CEffectMeshScript::isTextureAlphaEnable(DWORD dwMeshIndex)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return false;

	return m_MeshDataVector[dwMeshIndex].bTextureAlphaEnable;
}

BOOL CEffectMeshScript::GetColorOperationType(DWORD dwMeshIndex, BYTE * pbyType)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return FALSE;

	*pbyType = m_MeshDataVector[dwMeshIndex].byColorOperationType;

	return TRUE;
}
BOOL CEffectMeshScript::GetColorFactor(DWORD dwMeshIndex, D3DXCOLOR * pColor)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return FALSE;

	*pColor = m_MeshDataVector[dwMeshIndex].ColorFactor;

	return TRUE;
}

BOOL CEffectMeshScript::GetTimeTableAlphaPointer(DWORD dwMeshIndex, TTimeEventTableFloat ** pTimeEventAlpha)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return FALSE;

	*pTimeEventAlpha = &m_MeshDataVector[dwMeshIndex].TimeEventAlpha;

	return TRUE;
}


BOOL CEffectMeshScript::isMeshAnimationLoop()
{
	return m_isMeshAnimationLoop;
}
int CEffectMeshScript::GetMeshAnimationLoopCount()
{
	return m_iMeshAnimationLoopCount;
}
float CEffectMeshScript::GetMeshAnimationFrameDelay()
{
	return m_fMeshAnimationFrameDelay;
}

BOOL CEffectMeshScript::isTextureAnimationLoop(DWORD dwMeshIndex)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return 0.0f;

	return m_MeshDataVector[dwMeshIndex].bTextureAnimationLoopEnable;
}
float CEffectMeshScript::GetTextureAnimationFrameDelay(DWORD dwMeshIndex)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return 0.0f;

	return m_MeshDataVector[dwMeshIndex].fTextureAnimationFrameDelay;
}

DWORD CEffectMeshScript::GetTextureAnimationStartFrame(DWORD dwMeshIndex)
{
	if (!CheckMeshIndex(dwMeshIndex))
		return 0;

	return m_MeshDataVector[dwMeshIndex].dwTextureAnimationStartFrame;
}

BOOL CEffectMeshScript::OnLoadScript(CTextFileLoader & rTextFileLoader)
{
	if (rTextFileLoader.GetTokenString("meshfilename", &m_strMeshFileName))
	{
		if (!IsGlobalFileName(m_strMeshFileName.c_str()))
		{
			m_strMeshFileName = GetOnlyPathName(rTextFileLoader.GetFileName()) + m_strMeshFileName;
		}
	}
	else
	{
		return FALSE;
	}

	if (!rTextFileLoader.GetTokenInteger("meshanimationloopenable", &m_isMeshAnimationLoop))
		return FALSE;
	if (!rTextFileLoader.GetTokenInteger("meshanimationloopcount", &m_iMeshAnimationLoopCount))
	{
		m_iMeshAnimationLoopCount = 0;
	}
	if (!rTextFileLoader.GetTokenFloat("meshanimationframedelay", &m_fMeshAnimationFrameDelay))
		return FALSE;

	DWORD dwMeshElementCount;
	if (!rTextFileLoader.GetTokenDoubleWord("meshelementcount", &dwMeshElementCount))
		return FALSE;

	m_MeshDataVector.clear();
	m_MeshDataVector.resize(dwMeshElementCount);
	for (DWORD i = 0; i < m_MeshDataVector.size(); ++i)
	{
		CTextFileLoader::CGotoChild GotoChild(&rTextFileLoader, i);

		TMeshData & rMeshData = m_MeshDataVector[i];

		if (!rTextFileLoader.GetTokenByte("billboardtype", &rMeshData.byBillboardType))
			return FALSE;
		if (!rTextFileLoader.GetTokenBoolean("blendingenable", &rMeshData.bBlendingEnable))
			return FALSE;
		if (!rTextFileLoader.GetTokenByte("blendingsrctype", &rMeshData.byBlendingSrcType))
			return FALSE;
		if (!rTextFileLoader.GetTokenByte("blendingdesttype", &rMeshData.byBlendingDestType))
			return FALSE;
		if (!rTextFileLoader.GetTokenBoolean("texturealphaenable", &rMeshData.bTextureAlphaEnable))
		{
			// Legacy scripts may miss this token; default to enabled alpha sampling in DX11.
			rMeshData.bTextureAlphaEnable = TRUE;
		}

		if (!rTextFileLoader.GetTokenBoolean("textureanimationloopenable", &rMeshData.bTextureAnimationLoopEnable))
			return FALSE;
		if (!rTextFileLoader.GetTokenFloat("textureanimationframedelay", &rMeshData.fTextureAnimationFrameDelay))
			return FALSE;
		if (!rTextFileLoader.GetTokenDoubleWord("textureanimationstartframe", &rMeshData.dwTextureAnimationStartFrame))
		{
			rMeshData.dwTextureAnimationStartFrame = 0;
		}

		if (!rTextFileLoader.GetTokenByte("coloroperationtype", &rMeshData.byColorOperationType))
		{
			rMeshData.byColorOperationType = GRP_TOP_MODULATE;
		}
		if (!rTextFileLoader.GetTokenColor("colorfactor", &rMeshData.ColorFactor))
		{
			rMeshData.ColorFactor = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
		}

		if (!GetTokenTimeEventFloat(rTextFileLoader, "timeeventalpha", &rMeshData.TimeEventAlpha))
		{
			rMeshData.TimeEventAlpha.clear();
		}
	}

	return TRUE;
}

bool CEffectMeshScript::OnIsData()
{
	if (0 == m_strMeshFileName.length())
		return false;

	return true;
}

void CEffectMeshScript::OnClear()
{
	m_isMeshAnimationLoop = false;
	m_iMeshAnimationLoopCount = 0;
	m_fMeshAnimationFrameDelay = 0.02f;

	m_MeshDataVector.clear();
	m_strMeshFileName = "";
}

CEffectMeshScript::CEffectMeshScript()
{
}
CEffectMeshScript::~CEffectMeshScript()
{
}
