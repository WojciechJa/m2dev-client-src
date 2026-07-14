#include "StdAfx.h"
#include "GrpTextureDX11.h"
#include "EterBase/CRC32.h"
#include "PackLib/PackManager.h"

// DirectXTK headers
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include <stb_image.h>
#include <set>
#include <vector>
#include <algorithm>
#include <cctype>

// Static members
std::map<std::string, CGraphicTextureDX11::STextureCacheEntry> CGraphicTextureDX11::ms_mapTextureCache;
ID3D11ShaderResourceView* CGraphicTextureDX11::ms_pWhiteFallbackSRV = nullptr;
ID3D11Texture2D* CGraphicTextureDX11::ms_pWhiteFallbackTexture = nullptr;

DWORD CGraphicTextureDX11::ms_dwTotalLoads = 0;
DWORD CGraphicTextureDX11::ms_dwCacheHits = 0;
DWORD CGraphicTextureDX11::ms_dwDDSMemoryLoads = 0;
DWORD CGraphicTextureDX11::ms_dwDDSFileLoads = 0;
DWORD CGraphicTextureDX11::ms_dwWICLoads = 0;
DWORD CGraphicTextureDX11::ms_dwBridgeFallbacks = 0;

// M3-TEXTURE-ASYNC-10: Async loading state static members
std::deque<CGraphicTextureDX11::SAsyncLoadTask> CGraphicTextureDX11::ms_queueAsyncTasks[CGraphicTextureDX11::PRIORITY_COUNT];
std::mutex CGraphicTextureDX11::ms_mutexAsyncTasks;
DWORD CGraphicTextureDX11::ms_dwPendingAsyncLoads = 0;
DWORD CGraphicTextureDX11::ms_dwCompletedAsyncLoads = 0;
DWORD CGraphicTextureDX11::ms_dwFailedAsyncLoads = 0;
DWORD CGraphicTextureDX11::ms_dwCurrentFrame = 0;

// M3-TEXTURE-ASYNC-10: Memory budget static members
UINT64 CGraphicTextureDX11::ms_dwMemoryBudgetBytes = 2048ULL * 1024 * 1024;  // 2GB default budget
UINT64 CGraphicTextureDX11::ms_dwCurrentMemoryUsageBytes = 0;

// Helper to convert std::string to std::wstring
static std::wstring StringToWString(const std::string& str)
{
	if (str.empty())
		return std::wstring();

	int sizeNeeded = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.length(), nullptr, 0);
	std::wstring wstr(sizeNeeded, 0);
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.length(), &wstr[0], sizeNeeded);
	return wstr;
}

namespace
{
	// DX11_MODERNIZE_NEXT:
	// 1) Move texture decode/upload to an async worker + copy queue staging path.
	// 2) Generate mip chains on GPU for non-DDS content instead of single-mip uploads.
	// 3) Replace ad-hoc fallback ladder with centralized format policy (DDS/WIC/STB/SOIL2).
	// 4) Track residency/lifetime with explicit budget + LRU eviction instead of flat map cache.
	static const bool kDX11LogTextureLoadSuccess = false;

	void AddTexturePathCandidate(std::vector<std::string>& rkOut, const std::string& rkCandidate)
	{
		if (rkCandidate.empty())
			return;
		if (std::find(rkOut.begin(), rkOut.end(), rkCandidate) == rkOut.end())
			rkOut.push_back(rkCandidate);
	}

	void AddCopySuffixFallbackCandidate(std::vector<std::string>& rkOut, const std::string& rkCandidate)
	{
		if (rkCandidate.empty())
			return;

		const std::string kNeedle = " copy.";
		const std::string::size_type pos = rkCandidate.rfind(kNeedle);
		if (pos == std::string::npos)
			return;

		std::string rkFallback = rkCandidate;
		rkFallback.erase(pos, 5u); // erase " copy"
		AddTexturePathCandidate(rkOut, rkFallback);
	}

	std::string NormalizeTexturePath(const char* szFilename)
	{
		if (!szFilename)
			return std::string();

		std::string strPath(szFilename);
		std::replace(strPath.begin(), strPath.end(), '\\', '/');
		for (char& ch : strPath)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return strPath;
	}

	void CollectTexturePathCandidates(const char* szFilename, std::vector<std::string>& rkOut)
	{
		rkOut.clear();
		if (!szFilename || !szFilename[0])
			return;

		const std::string strOriginal(szFilename);
		AddTexturePathCandidate(rkOut, strOriginal);
		AddCopySuffixFallbackCandidate(rkOut, strOriginal);

		const std::string strNormalized = NormalizeTexturePath(szFilename);
		AddTexturePathCandidate(rkOut, strNormalized);
		AddCopySuffixFallbackCandidate(rkOut, strNormalized);

		const std::string kYmirMarker = "ymir work/";
		const std::string::size_type ymirPos = strNormalized.find(kYmirMarker);
		if (ymirPos != std::string::npos)
		{
			const std::string strYmirRelative = strNormalized.substr(ymirPos + kYmirMarker.size());
			AddTexturePathCandidate(rkOut, strYmirRelative);
			AddCopySuffixFallbackCandidate(rkOut, strYmirRelative);

			// Dev-layout fallback: unpacked effect assets can live under
			// <client>/assets/Effect/ymir work/<relative>.
			if (0 == strYmirRelative.find("effect/"))
			{
				const std::string strEffectMirror = std::string("assets/effect/ymir work/") + strYmirRelative;
				AddTexturePathCandidate(rkOut, strEffectMirror);
				AddCopySuffixFallbackCandidate(rkOut, strEffectMirror);
			}
		}

		const std::string kPackRoot = "pack/";
		const std::string::size_type packPos = strNormalized.find(kPackRoot);
		if (packPos != std::string::npos)
		{
			const std::string strPackRelative = strNormalized.substr(packPos + kPackRoot.size());
			AddTexturePathCandidate(rkOut, strPackRelative);
			AddCopySuffixFallbackCandidate(rkOut, strPackRelative);
		}
	}

	bool LoadTextureBytesFromCandidates(const char* szFilename, std::vector<uint8_t>& outBytes)
	{
		outBytes.clear();
		if (!szFilename || !szFilename[0])
			return false;

		std::vector<std::string> kPathCandidates;
		CollectTexturePathCandidates(szFilename, kPathCandidates);

		TPackFile kPackFile;
		for (const std::string& rkPath : kPathCandidates)
		{
			if (CPackManager::Instance().GetFile(rkPath.c_str(), kPackFile) && !kPackFile.empty())
			{
				outBytes.resize(kPackFile.size());
				memcpy(outBytes.data(), kPackFile.data(), kPackFile.size());
				return true;
			}
		}

		for (const std::string& rkPath : kPathCandidates)
		{
			FILE* fp = nullptr;
			if (0 != fopen_s(&fp, rkPath.c_str(), "rb") || !fp)
				continue;

			if (0 != fseek(fp, 0, SEEK_END))
			{
				fclose(fp);
				continue;
			}
			const long fileSize = ftell(fp);
			if (fileSize <= 0)
			{
				fclose(fp);
				continue;
			}
			if (0 != fseek(fp, 0, SEEK_SET))
			{
				fclose(fp);
				continue;
			}

			outBytes.resize(static_cast<size_t>(fileSize));
			const size_t readBytes = fread(outBytes.data(), 1, outBytes.size(), fp);
			fclose(fp);
			if (readBytes == outBytes.size())
				return true;

			outBytes.clear();
		}

		return false;
	}

	// M3-SKY-RESOURCE-DX11-72: Check if texture file exists (without loading)
	// Returns: true if file exists, false if not found
	// This helps distinguish "file not found" from "file exists but decoder failed"
	bool DoesTextureFileExist(const char* szFilename)
	{
		if (!szFilename || !szFilename[0])
			return false;

		std::vector<std::string> kPathCandidates;
		CollectTexturePathCandidates(szFilename, kPathCandidates);

		// Check pack files first
		TPackFile kPackFile;
		for (const std::string& rkPath : kPathCandidates)
		{
			if (CPackManager::Instance().GetFile(rkPath.c_str(), kPackFile) && !kPackFile.empty())
				return true;
		}

		// Check filesystem
		for (const std::string& rkPath : kPathCandidates)
		{
			FILE* fp = nullptr;
			if (0 == fopen_s(&fp, rkPath.c_str(), "rb") && fp)
			{
				fclose(fp);
				return true;
			}
		}

		return false;
	}

	// Prevent log spam: for each missing/broken texture path emit only one fallback_white log.
	bool ShouldLogTextureFallbackOnce(const char* szFilename)
	{
		if (!szFilename || !szFilename[0])
			return true;

		static std::set<std::string> s_kLoggedFallbackFiles;
		const std::string strKey = NormalizeTexturePath(szFilename);
		return s_kLoggedFallbackFiles.insert(strKey.empty() ? std::string(szFilename) : strKey).second;
	}
}

ID3D11ShaderResourceView* CGraphicTextureDX11::LoadDDSTexture(
	ID3D11Device* pDevice,
	const char* szFilename,
	bool bEnableCache)
{
	if (!pDevice || !szFilename || !szFilename[0])
		return GetWhiteFallbackTexture(pDevice);

	++ms_dwTotalLoads;

	// Check cache
	if (bEnableCache)
	{
		auto it = ms_mapTextureCache.find(szFilename);
		if (it != ms_mapTextureCache.end())
		{
			++ms_dwCacheHits;
			return it->second.pSRV;
		}
	}

	ID3D11Texture2D* pTexture = nullptr;
	ID3D11ShaderResourceView* pSRV = nullptr;

	// Try pack/VFS first
	pSRV = __LoadDDSFromPack(pDevice, szFilename, &pTexture);
	if (pSRV)
	{
		++ms_dwDDSMemoryLoads;
		if (kDX11LogTextureLoadSuccess)
			TraceError("DX11_TEXTURE_LOAD path=dds_memory file=%s", szFilename);
	}
	else
	{
		// Fallback to file system
		pSRV = __LoadDDSFromFile(pDevice, szFilename, &pTexture);
		if (pSRV)
		{
			++ms_dwDDSFileLoads;
			if (kDX11LogTextureLoadSuccess)
				TraceError("DX11_TEXTURE_LOAD path=dds_file file=%s", szFilename);
		}
	}

	// If failed, return white fallback
	if (!pSRV)
	{
		if (ShouldLogTextureFallbackOnce(szFilename))
			TraceError("DX11_TEXTURE_LOAD path=fallback_white file=%s (DDS load failed)", szFilename);
		return GetWhiteFallbackTexture(pDevice);
	}

	// Cache result
	if (bEnableCache)
	{
		STextureCacheEntry entry;
		entry.pTexture = pTexture;
		entry.pSRV = pSRV;
		entry.strPath = szFilename;
		entry.bFallbackWhite = false;
		ms_mapTextureCache[szFilename] = entry;
	}

	return pSRV;
}

ID3D11ShaderResourceView* CGraphicTextureDX11::LoadTexture(
	ID3D11Device* pDevice,
	const char* szFilename,
	bool bEnableCache)
{
	if (!pDevice || !szFilename || !szFilename[0])
		return GetWhiteFallbackTexture(pDevice);

	++ms_dwTotalLoads;

	// Check cache
	if (bEnableCache)
	{
		auto it = ms_mapTextureCache.find(szFilename);
		if (it != ms_mapTextureCache.end())
		{
			++ms_dwCacheHits;
			return it->second.pSRV;
		}
	}

	ID3D11Texture2D* pTexture = nullptr;
	ID3D11ShaderResourceView* pSRV = nullptr;

	// Try DDS first (preferred format)
	pSRV = __LoadDDSFromPack(pDevice, szFilename, &pTexture);
	if (pSRV)
	{
		++ms_dwDDSMemoryLoads;
		if (kDX11LogTextureLoadSuccess)
			TraceError("DX11_TEXTURE_LOAD path=dds_memory file=%s", szFilename);
	}
	else
	{
		pSRV = __LoadDDSFromFile(pDevice, szFilename, &pTexture);
		if (pSRV)
		{
			++ms_dwDDSFileLoads;
			if (kDX11LogTextureLoadSuccess)
				TraceError("DX11_TEXTURE_LOAD path=dds_file file=%s", szFilename);
		}
	}

	// Try WIC fallback (PNG/JPG/BMP/TIFF depending on system codecs).
	// NOTE: TGA is not guaranteed via WIC.
	if (!pSRV)
	{
		pSRV = __LoadWICFromFile(pDevice, szFilename, &pTexture);
		if (pSRV)
		{
			++ms_dwWICLoads;
			if (kDX11LogTextureLoadSuccess)
				TraceError("DX11_TEXTURE_LOAD path=wic_file file=%s", szFilename);
		}
	}

	// STB fallback from pack/file path candidates (covers TGA and uncommon WIC-missing formats).
	if (!pSRV)
	{
		pSRV = __LoadSTBFromCandidates(pDevice, szFilename, &pTexture);
		if (pSRV && kDX11LogTextureLoadSuccess)
			TraceError("DX11_TEXTURE_LOAD path=stb_memory file=%s", szFilename);
	}

	// If all failed, return white fallback
	if (!pSRV)
	{
		if (ShouldLogTextureFallbackOnce(szFilename))
			TraceError("DX11_TEXTURE_LOAD path=fallback_white file=%s (all loaders failed)", szFilename);
		return GetWhiteFallbackTexture(pDevice);
	}

	// Cache result
	if (bEnableCache)
	{
		STextureCacheEntry entry;
		entry.pTexture = pTexture;
		entry.pSRV = pSRV;
		entry.strPath = szFilename;
		entry.bFallbackWhite = false;
		ms_mapTextureCache[szFilename] = entry;
	}

	return pSRV;
}

ID3D11ShaderResourceView* CGraphicTextureDX11::__LoadSTBFromCandidates(
	ID3D11Device* pDevice,
	const char* szFilename,
	ID3D11Texture2D** ppTexture)
{
	if (!pDevice || !szFilename)
		return nullptr;

	std::vector<uint8_t> encodedBytes;
	if (!LoadTextureBytesFromCandidates(szFilename, encodedBytes) || encodedBytes.empty())
		return nullptr;

	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* pPixels = stbi_load_from_memory(
		reinterpret_cast<const stbi_uc*>(encodedBytes.data()),
		static_cast<int>(encodedBytes.size()),
		&width,
		&height,
		&channels,
		4);
	if (!pPixels || width <= 0 || height <= 0)
	{
		if (pPixels)
			stbi_image_free(pPixels);
		return nullptr;
	}

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = static_cast<UINT>(width);
	texDesc.Height = static_cast<UINT>(height);
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = pPixels;
	initData.SysMemPitch = static_cast<UINT>(width * 4);

	ID3D11Texture2D* pTexture = nullptr;
	HRESULT hr = pDevice->CreateTexture2D(&texDesc, &initData, &pTexture);
	stbi_image_free(pPixels);
	if (FAILED(hr) || !pTexture)
		return nullptr;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	ID3D11ShaderResourceView* pSRV = nullptr;
	hr = pDevice->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
	if (FAILED(hr) || !pSRV)
	{
		pTexture->Release();
		return nullptr;
	}

	if (ppTexture)
		*ppTexture = pTexture;
	else
		pTexture->Release();

	return pSRV;
}

void CGraphicTextureDX11::ClearTextureCache()
{
	for (auto& pair : ms_mapTextureCache)
	{
		if (pair.second.pSRV)
			pair.second.pSRV->Release();
		if (pair.second.pTexture)
			pair.second.pTexture->Release();
	}
	ms_mapTextureCache.clear();

	if (ms_pWhiteFallbackSRV)
	{
		ms_pWhiteFallbackSRV->Release();
		ms_pWhiteFallbackSRV = nullptr;
	}
	if (ms_pWhiteFallbackTexture)
	{
		ms_pWhiteFallbackTexture->Release();
		ms_pWhiteFallbackTexture = nullptr;
	}

	TraceError("DX11_TEXTURE_CACHE_CLEARED count=%u", (DWORD)ms_mapTextureCache.size());
}

void CGraphicTextureDX11::GetCacheStats(DWORD* pdwCachedCount, DWORD* pdwTotalLoads, DWORD* pdwCacheHits)
{
	if (pdwCachedCount)
		*pdwCachedCount = (DWORD)ms_mapTextureCache.size();
	if (pdwTotalLoads)
		*pdwTotalLoads = ms_dwTotalLoads;
	if (pdwCacheHits)
		*pdwCacheHits = ms_dwCacheHits;
}

ID3D11ShaderResourceView* CGraphicTextureDX11::GetWhiteFallbackTexture(ID3D11Device* pDevice)
{
	if (!pDevice)
		return nullptr;

	if (ms_pWhiteFallbackSRV)
		return ms_pWhiteFallbackSRV;

	ms_pWhiteFallbackSRV = __CreateWhiteFallback(pDevice, &ms_pWhiteFallbackTexture);
	return ms_pWhiteFallbackSRV;
}

// M3-SKY-RESOURCE-DX11-72: Check if texture file exists (without loading)
bool CGraphicTextureDX11::DoesTextureFileExist(const char* szFilename)
{
	if (!szFilename || !szFilename[0])
		return false;

	std::vector<std::string> kPathCandidates;
	CollectTexturePathCandidates(szFilename, kPathCandidates);

	// Check pack files first
	TPackFile kPackFile;
	for (const std::string& rkPath : kPathCandidates)
	{
		if (CPackManager::Instance().GetFile(rkPath.c_str(), kPackFile) && !kPackFile.empty())
			return true;
	}

	// Check filesystem
	for (const std::string& rkPath : kPathCandidates)
	{
		FILE* fp = nullptr;
		if (0 == fopen_s(&fp, rkPath.c_str(), "rb") && fp)
		{
			fclose(fp);
			return true;
		}
	}

	return false;
}

// Internal loaders

ID3D11ShaderResourceView* CGraphicTextureDX11::__LoadDDSFromPack(
	ID3D11Device* pDevice,
	const char* szFilename,
	ID3D11Texture2D** ppTexture)
{
	if (!pDevice || !szFilename)
		return nullptr;

	// Try to load from pack/VFS using normalized path candidates.
	TPackFile kDDSFile;
	std::vector<std::string> kPathCandidates;
	CollectTexturePathCandidates(szFilename, kPathCandidates);
	bool bPackHit = false;
	for (const std::string& rkPath : kPathCandidates)
	{
		if (CPackManager::Instance().GetFile(rkPath.c_str(), kDDSFile) && !kDDSFile.empty())
		{
			bPackHit = true;
			break;
		}
	}
	if (!bPackHit)
		return nullptr;

	// Use DirectXTK to create DDS from memory
	ID3D11Resource* pResource = nullptr;
	ID3D11ShaderResourceView* pSRV = nullptr;

	HRESULT hr = DirectX::CreateDDSTextureFromMemory(
		pDevice,
		reinterpret_cast<const uint8_t*>(kDDSFile.data()),
		kDDSFile.size(),
		&pResource,
		&pSRV);

	if (FAILED(hr) || !pSRV)
		return nullptr;

	// Get texture interface
	if (ppTexture && pResource)
	{
		hr = pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)ppTexture);
		if (FAILED(hr))
			*ppTexture = nullptr;
	}

	if (pResource)
		pResource->Release();

	return pSRV;
}

ID3D11ShaderResourceView* CGraphicTextureDX11::__LoadDDSFromFile(
	ID3D11Device* pDevice,
	const char* szFilename,
	ID3D11Texture2D** ppTexture)
{
	if (!pDevice || !szFilename)
		return nullptr;

	ID3D11Resource* pResource = nullptr;
	ID3D11ShaderResourceView* pSRV = nullptr;
	HRESULT hr = E_FAIL;

	std::vector<std::string> kPathCandidates;
	CollectTexturePathCandidates(szFilename, kPathCandidates);
	for (const std::string& rkPath : kPathCandidates)
	{
		std::wstring wFilename = StringToWString(rkPath);
		if (wFilename.empty())
			continue;

		hr = DirectX::CreateDDSTextureFromFile(
			pDevice,
			wFilename.c_str(),
			&pResource,
			&pSRV);
		if (SUCCEEDED(hr) && pSRV)
			break;
		if (pResource)
		{
			pResource->Release();
			pResource = nullptr;
		}
		if (pSRV)
		{
			pSRV->Release();
			pSRV = nullptr;
		}
	}

	if (FAILED(hr) || !pSRV)
		return nullptr;

	// Get texture interface
	if (ppTexture && pResource)
	{
		hr = pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)ppTexture);
		if (FAILED(hr))
			*ppTexture = nullptr;
	}

	if (pResource)
		pResource->Release();

	return pSRV;
}

ID3D11ShaderResourceView* CGraphicTextureDX11::__LoadWICFromFile(
	ID3D11Device* pDevice,
	const char* szFilename,
	ID3D11Texture2D** ppTexture)
{
	if (!pDevice || !szFilename)
		return nullptr;

	ID3D11Resource* pResource = nullptr;
	ID3D11ShaderResourceView* pSRV = nullptr;
	HRESULT hr = E_FAIL;

	std::vector<std::string> kPathCandidates;
	CollectTexturePathCandidates(szFilename, kPathCandidates);
	for (const std::string& rkPath : kPathCandidates)
	{
		std::wstring wFilename = StringToWString(rkPath);
		if (wFilename.empty())
			continue;

		hr = DirectX::CreateWICTextureFromFile(
			pDevice,
			wFilename.c_str(),
			&pResource,
			&pSRV);
		if (SUCCEEDED(hr) && pSRV)
			break;
		if (pResource)
		{
			pResource->Release();
			pResource = nullptr;
		}
		if (pSRV)
		{
			pSRV->Release();
			pSRV = nullptr;
		}
	}

	if (FAILED(hr) || !pSRV)
		return nullptr;

	// Get texture interface
	if (ppTexture && pResource)
	{
		hr = pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)ppTexture);
		if (FAILED(hr))
			*ppTexture = nullptr;
	}

	if (pResource)
		pResource->Release();

	return pSRV;
}

ID3D11ShaderResourceView* CGraphicTextureDX11::__CreateWhiteFallback(
	ID3D11Device* pDevice,
	ID3D11Texture2D** ppTexture)
{
	if (!pDevice)
		return nullptr;

	unsigned char aWhitePixel[4] = { 255, 255, 255, 255 };
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = 1;
	texDesc.Height = 1;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA texInit = {};
	texInit.pSysMem = aWhitePixel;
	texInit.SysMemPitch = 4;

	ID3D11Texture2D* pTexture = nullptr;
	HRESULT hr = pDevice->CreateTexture2D(&texDesc, &texInit, &pTexture);
	if (FAILED(hr) || !pTexture)
		return nullptr;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	ID3D11ShaderResourceView* pSRV = nullptr;
	hr = pDevice->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);

	if (ppTexture)
		*ppTexture = pTexture;
	else if (pTexture)
		pTexture->Release();

	return pSRV;
}

// M3-TEXTURE-ASYNC-10: Async texture loading implementation

CGraphicTextureDX11::SAsyncTextureResult CGraphicTextureDX11::__LoadTextureWorker(
	ID3D11Device* pDevice,
	const char* szFilename,
	bool bEnableCache)
{
	SAsyncTextureResult result;
	result.strFilename = szFilename ? szFilename : "";
	
	if (!pDevice || !szFilename || !szFilename[0])
	{
		result.bSuccess = false;
		return result;
	}

	// Check cache first (thread-safe read)
	{
		std::lock_guard<std::mutex> lock(ms_mutexAsyncTasks);
		auto it = ms_mapTextureCache.find(szFilename);
		if (it != ms_mapTextureCache.end())
		{
			result.pSRV = it->second.pSRV;
			result.pTexture = it->second.pTexture;
			result.bSuccess = true;
			result.bFromCache = true;
			return result;
		}
	}

	// Load texture using existing synchronous loaders
	ID3D11Texture2D* pTexture = nullptr;
	ID3D11ShaderResourceView* pSRV = nullptr;

	// Try DDS first (preferred format)
	pSRV = __LoadDDSFromPack(pDevice, szFilename, &pTexture);
	if (!pSRV)
		pSRV = __LoadDDSFromFile(pDevice, szFilename, &pTexture);
	
	// Try WIC fallback
	if (!pSRV)
		pSRV = __LoadWICFromFile(pDevice, szFilename, &pTexture);
	
	// Try STB fallback
	if (!pSRV)
		pSRV = __LoadSTBFromCandidates(pDevice, szFilename, &pTexture);

	if (!pSRV)
	{
		result.bSuccess = false;
		return result;
	}

	result.pSRV = pSRV;
	result.pTexture = pTexture;
	result.bSuccess = true;
	result.bFromCache = false;
	return result;
}

ID3D11ShaderResourceView* CGraphicTextureDX11::LoadTextureAsync(
	ID3D11Device* pDevice,
	const char* szFilename,
	ETexturePriority ePriority,
	TextureLoadCallback callback,
	bool bEnableCache)
{
	if (!pDevice || !szFilename || !szFilename[0])
		return GetWhiteFallbackTexture(pDevice);

	++ms_dwTotalLoads;

	// Check cache immediately (synchronous fast path)
	if (bEnableCache)
	{
		auto it = ms_mapTextureCache.find(szFilename);
		if (it != ms_mapTextureCache.end())
		{
			++ms_dwCacheHits;
			// Update access time
			it->second.dwLastAccessFrame = ms_dwCurrentFrame;
			return it->second.pSRV;
		}
	}

	// Create async task
	SAsyncLoadTask task;
	task.strFilename = szFilename;
	task.pDevice = pDevice;
	task.ePriority = ePriority;
	task.callback = callback;
	task.bEnableCache = bEnableCache;
	task.dwSubmitFrame = ms_dwCurrentFrame;

	// Launch async load using std::async
	task.future = std::async(std::launch::async,
		__LoadTextureWorker,
		pDevice,
		szFilename,
		bEnableCache);

	// Add to priority queue
	{
		std::lock_guard<std::mutex> lock(ms_mutexAsyncTasks);
		ms_queueAsyncTasks[ePriority].push_back(std::move(task));
		++ms_dwPendingAsyncLoads;
	}

	// Return white fallback immediately while loading
	return GetWhiteFallbackTexture(pDevice);
}

DWORD CGraphicTextureDX11::ProcessAsyncResults()
{
	++ms_dwCurrentFrame;
	DWORD dwProcessed = 0;

	// Process each priority queue in order (CRITICAL first, LOW last)
	for (int iPriority = 0; iPriority < PRIORITY_COUNT; ++iPriority)
	{
		std::lock_guard<std::mutex> lock(ms_mutexAsyncTasks);
		auto& queue = ms_queueAsyncTasks[iPriority];

		// Process completed tasks
		auto it = queue.begin();
		while (it != queue.end())
		{
			auto& task = *it;
			
			// Check if future is ready
			if (task.future.valid() && 
				task.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
			{
				// Get result
				SAsyncTextureResult result = task.future.get();
				
				--ms_dwPendingAsyncLoads;

				if (result.bSuccess && !result.bFromCache)
				{
					// Add to cache
					if (task.bEnableCache && result.pTexture && result.pSRV)
					{
						STextureCacheEntry entry;
						entry.pTexture = result.pTexture;
						entry.pSRV = result.pSRV;
						entry.strPath = result.strFilename;
						entry.bFallbackWhite = false;
						entry.dwTextureSizeBytes = __CalculateTextureSize(result.pTexture);
						entry.dwLastAccessFrame = ms_dwCurrentFrame;
						entry.ePriority = static_cast<ETexturePriority>(iPriority);
						
						ms_dwCurrentMemoryUsageBytes += entry.dwTextureSizeBytes;
						ms_mapTextureCache[result.strFilename] = entry;

						// Check if we need to evict textures
						if (ms_dwCurrentMemoryUsageBytes > ms_dwMemoryBudgetBytes)
						{
							DWORD dwBytesToFree = ms_dwCurrentMemoryUsageBytes - ms_dwMemoryBudgetBytes;
							__EvictLRUTextures(dwBytesToFree);
						}
					}

					++ms_dwCompletedAsyncLoads;
					++dwProcessed;
				}
				else if (!result.bSuccess)
				{
					++ms_dwFailedAsyncLoads;
				}

				// Invoke callback on main thread
				if (task.callback)
				{
					task.callback(result);
				}

				// Remove from queue
				it = queue.erase(it);
			}
			else
			{
				++it;
			}
		}

		// Limit processing per frame to avoid stalls
		if (dwProcessed >= 10)
			break;
	}

	return dwProcessed;
}

void CGraphicTextureDX11::GetAsyncStats(DWORD* pdwPendingLoads, DWORD* pdwCompletedLoads, DWORD* pdwFailedLoads)
{
	if (pdwPendingLoads)
		*pdwPendingLoads = ms_dwPendingAsyncLoads;
	if (pdwCompletedLoads)
		*pdwCompletedLoads = ms_dwCompletedAsyncLoads;
	if (pdwFailedLoads)
		*pdwFailedLoads = ms_dwFailedAsyncLoads;
}

DWORD CGraphicTextureDX11::__CalculateTextureSize(ID3D11Texture2D* pTexture)
{
	if (!pTexture)
		return 0;

	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc(&desc);

	// Calculate approximate size based on format and dimensions
	DWORD dwBytesPerPixel = 4;  // Most common: RGBA8
	switch (desc.Format)
	{
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
		dwBytesPerPixel = 0;  // Compressed, 0.5 bytes per pixel
		break;
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
		dwBytesPerPixel = 1;  // Compressed, 1 byte per pixel
		break;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		dwBytesPerPixel = 4;
		break;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		dwBytesPerPixel = 8;
		break;
	default:
		dwBytesPerPixel = 4;
		break;
	}

	DWORD dwSize = desc.Width * desc.Height * desc.ArraySize * dwBytesPerPixel;
	
	// Account for mip levels (add 33% for full mip chain)
	if (desc.MipLevels > 1)
		dwSize = dwSize * 4 / 3;

	return dwSize;
}

void CGraphicTextureDX11::__EvictLRUTextures(DWORD dwBytesNeeded)
{
	if (dwBytesNeeded == 0)
		return;

	// Build list of evictable textures (sorted by last access time)
	struct SEvictionCandidate
	{
		std::string strKey;
		DWORD dwLastAccessFrame;
		DWORD dwSize;
		ETexturePriority ePriority;
	};
	std::vector<SEvictionCandidate> candidates;

	for (auto& pair : ms_mapTextureCache)
	{
		// Never evict CRITICAL priority textures
		if (pair.second.ePriority == PRIORITY_CRITICAL)
			continue;
		
		// Never evict fallback texture
		if (pair.second.bFallbackWhite)
			continue;

		SEvictionCandidate candidate;
		candidate.strKey = pair.first;
		candidate.dwLastAccessFrame = pair.second.dwLastAccessFrame;
		candidate.dwSize = pair.second.dwTextureSizeBytes;
		candidate.ePriority = pair.second.ePriority;
		candidates.push_back(candidate);
	}

	// Sort by access time (oldest first)
	std::sort(candidates.begin(), candidates.end(),
		[](const SEvictionCandidate& a, const SEvictionCandidate& b) {
			// Lower priority textures evicted first
			if (a.ePriority != b.ePriority)
				return a.ePriority > b.ePriority;
			// Then by access time
			return a.dwLastAccessFrame < b.dwLastAccessFrame;
		});

	// Evict textures until we freed enough space
	DWORD dwFreed = 0;
	for (const auto& candidate : candidates)
	{
		auto it = ms_mapTextureCache.find(candidate.strKey);
		if (it != ms_mapTextureCache.end())
		{
			// Release resources
			if (it->second.pSRV)
				it->second.pSRV->Release();
			if (it->second.pTexture)
				it->second.pTexture->Release();

			dwFreed += it->second.dwTextureSizeBytes;
			ms_dwCurrentMemoryUsageBytes -= it->second.dwTextureSizeBytes;
			ms_mapTextureCache.erase(it);

			TraceError("DX11_TEXTURE_EVICTED path=%s size=%u priority=%d",
				candidate.strKey.c_str(),
				candidate.dwSize,
				candidate.ePriority);

			if (dwFreed >= dwBytesNeeded)
				break;
		}
	}
}

void CGraphicTextureDX11::SetMemoryBudgetMB(DWORD dwBudgetMB)
{
	UINT64 dwOldBytes = ms_dwMemoryBudgetBytes;
	ms_dwMemoryBudgetBytes = (UINT64)dwBudgetMB * 1024 * 1024;
	TraceError("DX11_TEXTURE_BUDGET_SET budget_mb=%u old_bytes=%llu new_bytes=%llu",
		dwBudgetMB, dwOldBytes, ms_dwMemoryBudgetBytes);

	// Trigger eviction if current usage exceeds new budget
	if (ms_dwCurrentMemoryUsageBytes > ms_dwMemoryBudgetBytes)
	{
		DWORD dwBytesToFree = (DWORD)(ms_dwCurrentMemoryUsageBytes - ms_dwMemoryBudgetBytes);
		__EvictLRUTextures(dwBytesToFree);
	}
}

DWORD CGraphicTextureDX11::GetMemoryBudgetMB()
{
	DWORD dwResult = (DWORD)(ms_dwMemoryBudgetBytes / (1024 * 1024));
	static DWORD s_dwCallCount = 0;
	++s_dwCallCount;
	if (s_dwCallCount <= 5 || dwResult == 0)
	{
		TraceError("DX11_TEXTURE_BUDGET_GET call=%u result_mb=%u bytes=%llu",
			s_dwCallCount, dwResult, ms_dwMemoryBudgetBytes);
	}
	return dwResult;
}

DWORD CGraphicTextureDX11::GetCurrentMemoryUsageMB()
{
	return ms_dwCurrentMemoryUsageBytes / (1024 * 1024);
}
