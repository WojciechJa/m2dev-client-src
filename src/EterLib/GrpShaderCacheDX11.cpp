#include "StdAfx.h"
#include "GrpShaderCacheDX11.h"

#define DX11_SHADER_CACHE_ENABLED

#ifdef DX11_SHADER_CACHE_ENABLED

#include <direct.h>
#include <sys/stat.h>
#include <fstream>

// Global shader cache instance
CGraphicShaderCacheDX11* g_pkShaderCacheDX11 = nullptr;

CGraphicShaderCacheDX11::CGraphicShaderCacheDX11()
	: m_pDevice(nullptr)
	, m_cacheHits(0)
	, m_cacheMisses(0)
	, m_isDirty(false)
{
}

CGraphicShaderCacheDX11::~CGraphicShaderCacheDX11()
{
	Flush();
}

bool CGraphicShaderCacheDX11::Initialize(const char* cacheDir, ID3D11Device* pDevice)
{
	if (!cacheDir)
		return false;

	m_cacheDir = cacheDir;
	m_pDevice = pDevice;

	// Create cache directory if it doesn't exist
	_mkdir(m_cacheDir.c_str());

	// Build cache file path
	m_cacheFilePath = m_cacheDir + "/dx11_shader_cache.bin";

	// Try to load existing cache
	LoadFromFile(m_cacheFilePath.c_str());

	TraceError("DX11_SHADER_CACHE_INIT path=%s loaded=%u shaders",
		m_cacheFilePath.c_str(),
		static_cast<unsigned int>(m_cache.size()));

	return true;
}

uint64_t CGraphicShaderCacheDX11::HashShaderSource(
	const char* source, const char* entry, const char* target)
{
	uint64_t hash = 14695981039346656037ULL;  // FNV-1a offset basis

	// Hash shader source
	if (source)
	{
		for (const char* p = source; *p; ++p)
		{
			hash ^= static_cast<uint64_t>(*p);
			hash *= 1099511628211ULL;  // FNV prime
		}
	}

	// Hash entry point
	if (entry)
	{
		for (const char* p = entry; *p; ++p)
		{
			hash ^= static_cast<uint64_t>(*p);
			hash *= 1099511628211ULL;
		}
	}

	// Hash target profile
	if (target)
	{
		for (const char* p = target; *p; ++p)
		{
			hash ^= static_cast<uint64_t>(*p);
			hash *= 1099511628211ULL;
		}
	}

	return hash;
}

HRESULT CGraphicShaderCacheDX11::CompileShader(
	const char* source,
	const char* entry,
	const char* target,
	ID3DBlob** ppBlobOut,
	const char* shaderTag)
{
	if (!source || !entry || !target || !ppBlobOut)
		return E_INVALIDARG;

	*ppBlobOut = nullptr;
	ID3DBlob* pErrorBlob = nullptr;

	const UINT uFlags = D3DCOMPILE_ENABLE_STRICTNESS;
	HRESULT hr = D3DCompile(
		source,
		strlen(source),
		nullptr,
		nullptr,
		nullptr,
		entry,
		target,
		uFlags,
		0u,
		ppBlobOut,
		&pErrorBlob);

	if (FAILED(hr))
	{
		const char* szTag = (shaderTag && shaderTag[0]) ? shaderTag : "unknown";

		if (pErrorBlob && pErrorBlob->GetBufferPointer())
		{
			TraceError("DX11_SHADER_COMPILE_FAIL shader=%s entry=%s target=%s hr=0x%08X error=%s",
				szTag, entry, target,
				static_cast<unsigned int>(hr),
				static_cast<const char*>(pErrorBlob->GetBufferPointer()));
		}
		else
		{
			TraceError("DX11_SHADER_COMPILE_FAIL shader=%s entry=%s target=%s hr=0x%08X",
				szTag, entry, target,
				static_cast<unsigned int>(hr));
		}
	}

	if (pErrorBlob)
		pErrorBlob->Release();
	return hr;
}

HRESULT CGraphicShaderCacheDX11::GetShaderBytecode(
	const char* source,
	const char* entryPoint,
	const char* target,
	ID3DBlob** ppBlobOut,
	const char* shaderTag)
{
	if (!source || !entryPoint || !target || !ppBlobOut)
		return E_INVALIDARG;

	*ppBlobOut = nullptr;

	// Calculate cache key
	const uint64_t hash = HashShaderSource(source, entryPoint, target);

	// Check cache
	auto it = m_cache.find(hash);
	if (it != m_cache.end())
	{
		// Cache hit - return bytecode
		m_cacheHits++;

		DX11CachedShader& cached = it->second;

		// Create blob from cached bytecode
		HRESULT hr = D3DCreateBlob(cached.bytecodeSize, ppBlobOut);

		if (SUCCEEDED(hr))
		{
			memcpy((*ppBlobOut)->GetBufferPointer(),
				cached.bytecode.data(),
				cached.bytecodeSize);

			TraceError("DX11_SHADER_CACHE_HIT shader=%s size=%u",
				cached.shaderTag.c_str(),
				static_cast<unsigned int>(cached.bytecodeSize));

			return S_OK;
		}

		// If blob creation fails, remove from cache and recompile
		m_cache.erase(it);
	}

	// Cache miss - compile shader
	m_cacheMisses++;

	HRESULT hr = CompileShader(source, entryPoint, target, ppBlobOut, shaderTag);
	if (FAILED(hr))
		return hr;

	// Store in cache
	DX11CachedShader cached;
	cached.sourceHash = hash;
	cached.shaderTag = shaderTag ? shaderTag : "unknown";
	cached.bytecodeSize = static_cast<uint32_t>((*ppBlobOut)->GetBufferSize());
	cached.sourceLength = static_cast<uint32_t>(strlen(source));
	cached.bytecode.assign(
		static_cast<const uint8_t*>((*ppBlobOut)->GetBufferPointer()),
		static_cast<const uint8_t*>((*ppBlobOut)->GetBufferPointer()) + cached.bytecodeSize
	);

	m_cache[hash] = std::move(cached);
	m_isDirty = true;

	TraceError("DX11_SHADER_CACHE_MISS shader=%s compiled size=%u",
		cached.shaderTag.c_str(),
		static_cast<unsigned int>(cached.bytecodeSize));

	return S_OK;
}

bool CGraphicShaderCacheDX11::LoadFromFile(const char* cachePath)
{
	if (!cachePath)
		return false;

	FILE* f = fopen(cachePath, "rb");
	if (!f)
		return false;  // File doesn't exist yet, not an error

	// Read header
	struct CacheHeader
	{
		uint32_t magic;
		uint32_t version;
		uint32_t shaderCount;
		uint32_t reserved;
	} header;

	if (fread(&header, sizeof(header), 1, f) != 1)
	{
		fclose(f);
		return false;
	}

	// Validate header
	if (header.magic != CACHE_MAGIC || header.version != CACHE_VERSION)
	{
		fclose(f);
		return false;
	}

	// Read shader entries
	for (uint32_t i = 0; i < header.shaderCount; ++i)
	{
		// Read entry header
		struct ShaderEntryHeader
		{
			uint64_t sourceHash;
			uint32_t bytecodeSize;
			uint32_t sourceLength;
			uint32_t tagLength;
		} entry;

		if (fread(&entry, sizeof(entry), 1, f) != 1)
			break;

		DX11CachedShader shader;
		shader.sourceHash = entry.sourceHash;
		shader.bytecodeSize = entry.bytecodeSize;
		shader.sourceLength = entry.sourceLength;

		// Read tag string
		if (entry.tagLength > 0)
		{
			shader.shaderTag.resize(entry.tagLength);
			if (fread(&shader.shaderTag[0], 1, entry.tagLength, f) != entry.tagLength)
				break;
		}

		// Read bytecode
		shader.bytecode.resize(entry.bytecodeSize);
		if (fread(shader.bytecode.data(), 1, entry.bytecodeSize, f) != entry.bytecodeSize)
			break;

		// Store in cache
		m_cache[shader.sourceHash] = std::move(shader);
	}

	fclose(f);
	m_isDirty = false;

	return true;
}

bool CGraphicShaderCacheDX11::SaveToFile(const char* cachePath)
{
	if (!cachePath)
		return false;

	FILE* f = fopen(cachePath, "wb");
	if (!f)
		return false;

	// Write header
	struct CacheHeader
	{
		uint32_t magic;
		uint32_t version;
		uint32_t shaderCount;
		uint32_t reserved;
	} header;

	header.magic = CACHE_MAGIC;
	header.version = CACHE_VERSION;
	header.shaderCount = static_cast<uint32_t>(m_cache.size());
	header.reserved = 0;

	if (fwrite(&header, sizeof(header), 1, f) != 1)
	{
		fclose(f);
		return false;
	}

	// Write each shader entry
	for (const auto& pair : m_cache)
	{
		const DX11CachedShader& shader = pair.second;

		// Write entry header
		struct ShaderEntryHeader
		{
			uint64_t sourceHash;
			uint32_t bytecodeSize;
			uint32_t sourceLength;
			uint32_t tagLength;
		} entry;

		entry.sourceHash = shader.sourceHash;
		entry.bytecodeSize = shader.bytecodeSize;
		entry.sourceLength = shader.sourceLength;
		entry.tagLength = static_cast<uint32_t>(shader.shaderTag.length());

		if (fwrite(&entry, sizeof(entry), 1, f) != 1)
		{
			fclose(f);
			return false;
		}

		// Write tag string
		if (entry.tagLength > 0)
		{
			if (fwrite(shader.shaderTag.c_str(), 1, entry.tagLength, f) != entry.tagLength)
			{
				fclose(f);
				return false;
			}
		}

		// Write bytecode
		if (fwrite(shader.bytecode.data(), 1, shader.bytecodeSize, f) != shader.bytecodeSize)
		{
			fclose(f);
			return false;
		}
	}

	fclose(f);
	m_isDirty = false;

	return true;
}

bool CGraphicShaderCacheDX11::Flush()
{
	if (!m_isDirty)
		return true;  // Nothing to save

	return SaveToFile(m_cacheFilePath.c_str());
}

void CGraphicShaderCacheDX11::Invalidate()
{
	m_cache.clear();
	m_isDirty = false;

	// Delete cache file
	remove(m_cacheFilePath.c_str());

	TraceError("DX11_SHADER_CACHE_INVALIDATE cleared=%u shaders",
		static_cast<unsigned int>(m_cache.size()));
}

#endif  // DX11_SHADER_CACHE_ENABLED
