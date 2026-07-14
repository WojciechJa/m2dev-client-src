#pragma once

#include <d3d11.h>
#include <string>
#include <map>
#include <future>
#include <functional>
#include <deque>
#include <mutex>

// W3/Effects DX11 texture loading helper
// Provides unified DX11-native texture loading with cache and telemetry
// M3-TEXTURE-ASYNC-10: Added async loading, priority system, and memory management
class CGraphicTextureDX11
{
public:
	// M3-TEXTURE-ASYNC-10: Texture loading priority levels
	enum ETexturePriority
	{
		PRIORITY_CRITICAL = 0,  // UI, character textures - load immediately
		PRIORITY_HIGH = 1,      // Nearby objects, current area
		PRIORITY_NORMAL = 2,    // Terrain within view distance
		PRIORITY_LOW = 3,       // Distant objects, effects, preload
		PRIORITY_COUNT = 4
	};

	// M3-TEXTURE-ASYNC-10: Async texture loading result
	struct SAsyncTextureResult
	{
		ID3D11ShaderResourceView* pSRV;
		ID3D11Texture2D* pTexture;
		std::string strFilename;
		bool bSuccess;
		bool bFromCache;

		SAsyncTextureResult()
			: pSRV(nullptr)
			, pTexture(nullptr)
			, bSuccess(false)
			, bFromCache(false)
		{
		}
	};

	// M3-TEXTURE-ASYNC-10: Async texture loading callback
	typedef std::function<void(const SAsyncTextureResult&)> TextureLoadCallback;

	// Synchronous loading (original API - maintained for compatibility)
	// Load DDS texture from pack or file
	static ID3D11ShaderResourceView* LoadDDSTexture(
		ID3D11Device* pDevice,
		const char* szFilename,
		bool bEnableCache = true);

	// Load any texture format (DDS preferred, WIC fallback for PNG/JPG)
	static ID3D11ShaderResourceView* LoadTexture(
		ID3D11Device* pDevice,
		const char* szFilename,
		bool bEnableCache = true);

	// M3-TEXTURE-ASYNC-10: Asynchronous texture loading
	// Returns immediately with white fallback texture, calls callback when ready
	// Callback is invoked on main thread during ProcessAsyncResults()
	static ID3D11ShaderResourceView* LoadTextureAsync(
		ID3D11Device* pDevice,
		const char* szFilename,
		ETexturePriority ePriority,
		TextureLoadCallback callback,
		bool bEnableCache = true);

	// M3-TEXTURE-ASYNC-10: Process completed async texture loads
	// Must be called from main thread each frame
	// Returns: number of textures processed this frame
	static DWORD ProcessAsyncResults();

	// M3-TEXTURE-ASYNC-10: Get async loading statistics
	static void GetAsyncStats(DWORD* pdwPendingLoads, DWORD* pdwCompletedLoads, DWORD* pdwFailedLoads);

	// Clear texture cache
	static void ClearTextureCache();

	// Get cache statistics (for telemetry)
	static void GetCacheStats(DWORD* pdwCachedCount, DWORD* pdwTotalLoads, DWORD* pdwCacheHits);

	// Get 1x1 white fallback texture
	static ID3D11ShaderResourceView* GetWhiteFallbackTexture(ID3D11Device* pDevice);

	// M3-SKY-RESOURCE-DX11-72: Check if texture file exists (without loading)
	// Returns: true if file exists in pack or filesystem, false otherwise
	static bool DoesTextureFileExist(const char* szFilename);

	// M3-TEXTURE-ASYNC-10: Memory budget management
	static void SetMemoryBudgetMB(DWORD dwBudgetMB);
	static DWORD GetMemoryBudgetMB();
	static DWORD GetCurrentMemoryUsageMB();

private:
	struct STextureCacheEntry
	{
		ID3D11Texture2D* pTexture;
		ID3D11ShaderResourceView* pSRV;
		std::string strPath;
		bool bFallbackWhite;
		DWORD dwTextureSizeBytes;
		DWORD dwLastAccessFrame;
		ETexturePriority ePriority;

		STextureCacheEntry()
			: pTexture(nullptr)
			, pSRV(nullptr)
			, bFallbackWhite(false)
			, dwTextureSizeBytes(0)
			, dwLastAccessFrame(0)
			, ePriority(PRIORITY_NORMAL)
		{
		}
	};

	// M3-TEXTURE-ASYNC-10: Async loading structures
	struct SAsyncLoadTask
	{
		std::string strFilename;
		ID3D11Device* pDevice;
		ETexturePriority ePriority;
		TextureLoadCallback callback;
		bool bEnableCache;
		std::future<SAsyncTextureResult> future;
		DWORD dwSubmitFrame;

		SAsyncLoadTask()
			: pDevice(nullptr)
			, ePriority(PRIORITY_NORMAL)
			, bEnableCache(true)
			, dwSubmitFrame(0)
		{
		}
	};

	static std::map<std::string, STextureCacheEntry> ms_mapTextureCache;
	static ID3D11ShaderResourceView* ms_pWhiteFallbackSRV;
	static ID3D11Texture2D* ms_pWhiteFallbackTexture;

	// Telemetry counters
	static DWORD ms_dwTotalLoads;
	static DWORD ms_dwCacheHits;
	static DWORD ms_dwDDSMemoryLoads;
	static DWORD ms_dwDDSFileLoads;
	static DWORD ms_dwWICLoads;
	static DWORD ms_dwBridgeFallbacks;

	// M3-TEXTURE-ASYNC-10: Async loading state
	static std::deque<SAsyncLoadTask> ms_queueAsyncTasks[PRIORITY_COUNT];
	static std::mutex ms_mutexAsyncTasks;
	static DWORD ms_dwPendingAsyncLoads;
	static DWORD ms_dwCompletedAsyncLoads;
	static DWORD ms_dwFailedAsyncLoads;
	static DWORD ms_dwCurrentFrame;

	// M3-TEXTURE-ASYNC-10: Memory budget
	static UINT64 ms_dwMemoryBudgetBytes;
	static UINT64 ms_dwCurrentMemoryUsageBytes;

	// Internal loaders
	static ID3D11ShaderResourceView* __LoadDDSFromPack(
		ID3D11Device* pDevice,
		const char* szFilename,
		ID3D11Texture2D** ppTexture);

	static ID3D11ShaderResourceView* __LoadDDSFromFile(
		ID3D11Device* pDevice,
		const char* szFilename,
		ID3D11Texture2D** ppTexture);

	static ID3D11ShaderResourceView* __LoadWICFromFile(
		ID3D11Device* pDevice,
		const char* szFilename,
		ID3D11Texture2D** ppTexture);

	static ID3D11ShaderResourceView* __LoadSTBFromCandidates(
		ID3D11Device* pDevice,
		const char* szFilename,
		ID3D11Texture2D** ppTexture);

	static ID3D11ShaderResourceView* __CreateWhiteFallback(
		ID3D11Device* pDevice,
		ID3D11Texture2D** ppTexture);

	// M3-TEXTURE-ASYNC-10: Async loading helpers
	static SAsyncTextureResult __LoadTextureWorker(
		ID3D11Device* pDevice,
		const char* szFilename,
		bool bEnableCache);

	static DWORD __CalculateTextureSize(ID3D11Texture2D* pTexture);
	static void __EvictLRUTextures(DWORD dwBytesNeeded);
};
