#include "StdAfx.h"
#include "PackLib/PackManager.h"
#include "GrpImageTexture.h"
#include "GrpDeviceDX11.h"
#include "GrpTextureDX11.h"
#include "DecodedImageData.h"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"

#include <stb_image.h>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_set>

#if defined(_M_IX86) || defined(_M_X64)
#include <emmintrin.h> // SSE2
#include <tmmintrin.h> // SSSE3 (for _mm_shuffle_epi8)
#endif

namespace
{
	CGraphicDeviceDX11* GetActiveDX11Device()
	{
		CGraphicDeviceDX11* pDevice = CGraphicDeviceDX11::GetActiveDevice();
		if (!pDevice || !pDevice->GetDevice() || !pDevice->GetContext())
			return nullptr;
		return pDevice;
	}

	bool ShouldUseDX11TexturePath()
	{
		CGraphicDeviceDX11* pDevice = GetActiveDX11Device();
		if (!pDevice)
			return false;

		return (pDevice->GetDX11TexturePipelineMode() != CGraphicDeviceDX11::DX11_TEXTURE_PIPELINE_LEGACY);
	}

	void LogDX11TextureFallbackOnce(const char* c_szPath, const std::string& rkFileName)
	{
		static bool s_bLogged = false;
		if (s_bLogged)
			return;

		s_bLogged = true;
		TraceError(
			"DX11_TEXTURE_PIPELINE_FALLBACK path=%s file=%s reason=dx11_decode_failed",
			(c_szPath && c_szPath[0]) ? c_szPath : "unknown",
			rkFileName.empty() ? "(font_atlas)" : rkFileName.c_str());
	}

	void LogDX11CompatTextureBlockOnce(const char* c_szPath, const std::string& rkFileName)
	{
		static bool s_bLogged = false;
		if (s_bLogged)
			return;

		s_bLogged = true;
		TraceError(
			"DX11_COMPAT_TEXTURE_BLOCK path=%s file=%s reason=dx9_texture_device_missing",
			(c_szPath && c_szPath[0]) ? c_szPath : "unknown",
			rkFileName.empty() ? "(font_atlas)" : rkFileName.c_str());
	}

	void LogDX11TextureMissingDeviceOnce(const char* c_szPath, const std::string& rkFileName)
	{
		static bool s_bLogged = false;
		if (s_bLogged)
			return;

		s_bLogged = true;
		TraceError(
			"DX11_TEXTURE_BLOCK path=%s file=%s reason=dx11_device_missing",
			(c_szPath && c_szPath[0]) ? c_szPath : "unknown",
			rkFileName.empty() ? "(font_atlas)" : rkFileName.c_str());
	}

	bool IsSuspiciousLegacyTexturePtr(ID3D11ShaderResourceView* pTexture)
	{
		const uintptr_t uPtr = reinterpret_cast<uintptr_t>(pTexture);
		return
			uPtr == 0xFFFFFFFFFFFFFFFFull ||
			uPtr == 0xCCCCCCCCCCCCCCCCull ||
			uPtr == 0xFEEEFEEEFEEEFEEEull ||
			uPtr == 0xCDCDCDCDCDCDCDCDull;
	}

	bool EndsWithNoCase(const std::string& text, const char* suffix)
	{
		if (!suffix)
			return false;
		const size_t textLen = text.size();
		const size_t suffixLen = strlen(suffix);
		if (textLen < suffixLen)
			return false;
		for (size_t i = 0; i < suffixLen; ++i)
		{
			const char a = static_cast<char>(tolower(static_cast<unsigned char>(text[textLen - suffixLen + i])));
			const char b = static_cast<char>(tolower(static_cast<unsigned char>(suffix[i])));
			if (a != b)
				return false;
		}
		return true;
	}

	bool ShouldLogTGAForFile(const std::string& fileName)
	{
		return EndsWithNoCase(fileName, ".tga");
	}

	std::string ToLowerASCII(const std::string& text)
	{
		std::string out = text;
		for (size_t i = 0; i < out.size(); ++i)
			out[i] = static_cast<char>(tolower(static_cast<unsigned char>(out[i])));
		return out;
	}

	bool ConsumeOneShotKey(std::unordered_set<std::string>& seen, const std::string& key)
	{
		static std::mutex s_logKeyMutex;
		const std::lock_guard<std::mutex> guard(s_logKeyMutex);
		const auto it = seen.insert(key);
		return it.second;
	}

	uint16_t ReadU16LE(const uint8_t* p)
	{
		return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
	}

	struct DecodedRGBAImage
	{
		int width = 0;
		int height = 0;
		std::vector<uint8_t> pixels;
	};

	bool DecodeTGAFromMemory(const uint8_t* data, size_t dataSize, DecodedRGBAImage& outImage, std::string* pReason)
	{
		auto fail = [&](const char* reason) -> bool
		{
			if (pReason)
				*pReason = reason ? reason : "unknown";
			return false;
		};

		if (!data || dataSize < 18)
			return fail("buffer_too_small");

		const uint8_t idLen = data[0];
		const uint8_t colorMapType = data[1];
		const uint8_t imageType = data[2];
		const uint16_t colorMapLength = ReadU16LE(data + 5);
		const uint8_t colorMapEntryBits = data[7];
		const uint16_t width = ReadU16LE(data + 12);
		const uint16_t height = ReadU16LE(data + 14);
		const uint8_t pixelDepth = data[16];
		const uint8_t imageDesc = data[17];

		if (width == 0 || height == 0)
			return fail("invalid_dimensions");
		if (imageType != 2 && imageType != 10)
			return fail("unsupported_image_type");
		if (pixelDepth != 24 && pixelDepth != 32)
			return fail("unsupported_bpp");
		if (colorMapType != 0)
			return fail("unsupported_colormap");

		size_t offset = 18u + static_cast<size_t>(idLen);
		if (offset > dataSize)
			return fail("invalid_id_offset");

		// Consume color map data if present (currently unsupported payload, but validate bounds).
		if (colorMapType != 0 && colorMapLength > 0)
		{
			const size_t colorMapBytes = static_cast<size_t>(colorMapLength) * ((colorMapEntryBits + 7u) / 8u);
			if (offset + colorMapBytes > dataSize)
				return fail("invalid_colormap_size");
			offset += colorMapBytes;
		}

		const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
		const size_t bytesPerPixel = static_cast<size_t>(pixelDepth / 8u);
		outImage.width = static_cast<int>(width);
		outImage.height = static_cast<int>(height);
		outImage.pixels.assign(pixelCount * 4u, 0u);

		const bool originTop = (imageDesc & 0x20u) != 0;
		const bool originRight = (imageDesc & 0x10u) != 0;

		size_t srcPixelIndex = 0;
		size_t srcPos = offset;

		auto writePixel = [&](size_t dstLogicalIndex, const uint8_t* srcPx)
		{
			const size_t srcX = dstLogicalIndex % static_cast<size_t>(width);
			const size_t srcY = dstLogicalIndex / static_cast<size_t>(width);
			const size_t dstX = originRight ? (static_cast<size_t>(width) - 1u - srcX) : srcX;
			const size_t dstY = originTop ? srcY : (static_cast<size_t>(height) - 1u - srcY);
			const size_t dstOffset = (dstY * static_cast<size_t>(width) + dstX) * 4u;

			// Output RGBA
			outImage.pixels[dstOffset + 0] = srcPx[2];
			outImage.pixels[dstOffset + 1] = srcPx[1];
			outImage.pixels[dstOffset + 2] = srcPx[0];
			outImage.pixels[dstOffset + 3] = (bytesPerPixel == 4u) ? srcPx[3] : 0xFFu;
		};

		if (imageType == 2)
		{
			const size_t needed = pixelCount * bytesPerPixel;
			if (srcPos + needed > dataSize)
				return fail("truncated_uncompressed_data");

			for (; srcPixelIndex < pixelCount; ++srcPixelIndex, srcPos += bytesPerPixel)
				writePixel(srcPixelIndex, data + srcPos);
		}
		else
		{
			while (srcPixelIndex < pixelCount)
			{
				if (srcPos >= dataSize)
					return fail("truncated_rle_header");
				const uint8_t packet = data[srcPos++];
				const size_t runLength = static_cast<size_t>((packet & 0x7Fu) + 1u);
				if (runLength == 0u || srcPixelIndex + runLength > pixelCount)
					return fail("invalid_rle_run");

				if (packet & 0x80u)
				{
					if (srcPos + bytesPerPixel > dataSize)
						return fail("truncated_rle_pixel");
					const uint8_t* px = data + srcPos;
					srcPos += bytesPerPixel;
					for (size_t j = 0; j < runLength; ++j)
						writePixel(srcPixelIndex++, px);
				}
				else
				{
					const size_t literalBytes = runLength * bytesPerPixel;
					if (srcPos + literalBytes > dataSize)
						return fail("truncated_rle_literals");
					for (size_t j = 0; j < runLength; ++j, srcPos += bytesPerPixel)
						writePixel(srcPixelIndex++, data + srcPos);
				}
			}
		}

		return true;
	}

	struct Soil2Api
	{
		bool initialized = false;
		HMODULE module = nullptr;
		unsigned char* (__cdecl *loadFromMemory)(const unsigned char*, int, int*, int*, int*, int) = nullptr;
		void (__cdecl *freeImageData)(unsigned char*) = nullptr;
		const char* (__cdecl *lastResult)(void) = nullptr;
	};

	Soil2Api& GetSoil2Api()
	{
		static Soil2Api api;
		if (api.initialized)
			return api;

		api.initialized = true;
		api.module = ::LoadLibraryA("SOIL2.dll");
		if (!api.module)
			api.module = ::LoadLibraryA("soil2.dll");
		if (!api.module)
			return api;

		api.loadFromMemory = reinterpret_cast<unsigned char* (__cdecl *)(const unsigned char*, int, int*, int*, int*, int)>(
			::GetProcAddress(api.module, "SOIL_load_image_from_memory"));
		api.freeImageData = reinterpret_cast<void (__cdecl *)(unsigned char*)>(
			::GetProcAddress(api.module, "SOIL_free_image_data"));
		api.lastResult = reinterpret_cast<const char* (__cdecl *)(void)>(
			::GetProcAddress(api.module, "SOIL_last_result"));

		if (!api.loadFromMemory || !api.freeImageData)
		{
			::FreeLibrary(api.module);
			api = Soil2Api();
			api.initialized = true;
		}

		return api;
	}
}

void CGraphicImageTexture::DestroyDX11Texture()
{
	if (m_pDX11TextureSRV)
	{
		m_pDX11TextureSRV->Release();
		m_pDX11TextureSRV = nullptr;
	}
	if (m_pDX11Texture)
	{
		m_pDX11Texture->Release();
		m_pDX11Texture = nullptr;
	}

	m_kDX11LockedPixels.clear();
	m_uDX11LockPitch = 0;
	m_bDX11LockActive = false;
	m_bDX11DynamicTexture = false;
}

bool CGraphicImageTexture::CreateDX11DynamicTexture(UINT width, UINT height)
{
	CGraphicDeviceDX11* pDX11Device = GetActiveDX11Device();
	if (!pDX11Device)
	{
		LogDX11TextureMissingDeviceOnce("CreateDX11DynamicTexture", m_stFileName);
		return false;
	}

	DestroyDX11Texture();

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	HRESULT hr = pDX11Device->GetDevice()->CreateTexture2D(&desc, nullptr, &m_pDX11Texture);
	if (FAILED(hr) || !m_pDX11Texture)
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	hr = pDX11Device->GetDevice()->CreateShaderResourceView(m_pDX11Texture, &srvDesc, &m_pDX11TextureSRV);
	if (FAILED(hr) || !m_pDX11TextureSRV)
	{
		DestroyDX11Texture();
		return false;
	}

	m_uDX11LockPitch = width * 4u;
	m_kDX11LockedPixels.resize(static_cast<size_t>(m_uDX11LockPitch) * static_cast<size_t>(height), 0);
	m_bDX11DynamicTexture = true;
	return true;
}

bool CGraphicImageTexture::BindDX11LoadedTexture(ID3D11ShaderResourceView* pSRV)
{
	if (!pSRV)
		return false;

	DestroyDX11Texture();

	pSRV->AddRef();
	m_pDX11TextureSRV = pSRV;

	ID3D11Resource* pResource = nullptr;
	pSRV->GetResource(&pResource);
	if (!pResource)
	{
		DestroyDX11Texture();
		return false;
	}

	HRESULT hr = pResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&m_pDX11Texture));
	pResource->Release();
	if (FAILED(hr) || !m_pDX11Texture)
	{
		DestroyDX11Texture();
		return false;
	}

	D3D11_TEXTURE2D_DESC desc = {};
	m_pDX11Texture->GetDesc(&desc);
	m_width = static_cast<int>(desc.Width);
	m_height = static_cast<int>(desc.Height);
	m_bDX11DynamicTexture = false;
	return true;
}

bool CGraphicImageTexture::Lock(int* pRetPitch, void** ppRetPixels, int level)
{
	if (!m_pDX11Texture || !m_bDX11DynamicTexture || level != 0 || !pRetPitch || !ppRetPixels)
		return false;

	*pRetPitch = static_cast<int>(m_uDX11LockPitch);
	*ppRetPixels = m_kDX11LockedPixels.data();
	m_bDX11LockActive = true;
	return true;
}

void CGraphicImageTexture::Unlock(int level)
{
	if (!m_pDX11Texture || !m_bDX11DynamicTexture || !m_bDX11LockActive || level != 0)
		return;

	CGraphicDeviceDX11* pDX11Device = GetActiveDX11Device();
	if (!pDX11Device)
	{
		m_bDX11LockActive = false;
		return;
	}

	pDX11Device->GetContext()->UpdateSubresource(
		m_pDX11Texture,
		0,
		nullptr,
		m_kDX11LockedPixels.data(),
		m_uDX11LockPitch,
		0);
	m_bDX11LockActive = false;
}

void CGraphicImageTexture::Initialize()
{
	CGraphicTexture::Initialize();

	m_stFileName = "";

	m_d3dFmt=D3DFMT_UNKNOWN;
	m_dwFilter=0;
	m_pDX11Texture = nullptr;
	m_pDX11TextureSRV = nullptr;
	m_uDX11LockPitch = 0;
	m_bDX11LockActive = false;
	m_bDX11DynamicTexture = false;
	m_kDX11LockedPixels.clear();
}

void CGraphicImageTexture::Destroy()
{
	DestroyDX11Texture();
	CGraphicTexture::Destroy();

	Initialize();
}

bool CGraphicImageTexture::CreateDeviceObjects()
{
	// Font atlas pages are runtime-generated textures; keep them on DX11 whenever
	// the DX11 device is alive so they never depend on legacy DX9 texture creation.
	const bool bForceDX11DynamicAtlasPath = (m_stFileName.empty() && GetActiveDX11Device() != nullptr);
	const bool bUseDX11TexturePath = bForceDX11DynamicAtlasPath || ShouldUseDX11TexturePath();
	if (bUseDX11TexturePath)
	{
		CGraphicDeviceDX11* pDX11Device = GetActiveDX11Device();
		if (!pDX11Device)
		{
			LogDX11TextureMissingDeviceOnce("CreateDeviceObjects", m_stFileName);
			return false;
		}

		bool bDX11CreateSuccess = false;
		if (m_stFileName.empty())
		{
			bDX11CreateSuccess = CreateDX11DynamicTexture(static_cast<UINT>(m_width), static_cast<UINT>(m_height));
		}
		else
		{
			ID3D11ShaderResourceView* pSRV = CGraphicTextureDX11::LoadTexture(pDX11Device->GetDevice(), m_stFileName.c_str(), true);
			bDX11CreateSuccess = (pSRV && BindDX11LoadedTexture(pSRV));
		}

		if (bDX11CreateSuccess)
		{
			m_bEmpty = false;
			return true;
		}

		// M3-SKY-RESOURCE-DX11-72: Distinguish file not found from decoder failure
		const bool bFileExists = CGraphicTextureDX11::DoesTextureFileExist(m_stFileName.c_str());
		if (bFileExists)
		{
			TraceError("DX11_TEXTURE_LOAD_FAIL path=CreateDeviceObjects file=%s reason=decoder_failed file_exists=true", m_stFileName.c_str());
		}
		else
		{
			TraceError("DX11_TEXTURE_LOAD_FAIL path=CreateDeviceObjects file=%s reason=file_not_found file_exists=false", m_stFileName.c_str());
		}
	}

	return false;
}

bool CGraphicImageTexture::Create(UINT width, UINT height, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	Destroy();

	m_width = width;
	m_height = height;
	m_d3dFmt = d3dFmt;
	m_dwFilter = dwFilter;

	return CreateDeviceObjects();
}

void CGraphicImageTexture::CreateFromTexturePointer(const CGraphicTexture* c_pSrcTexture)
{
	DestroyDX11Texture();

	m_width = c_pSrcTexture->GetWidth();
	m_height = c_pSrcTexture->GetHeight();
	m_lpd3dTexture = nullptr;
	ID3D11ShaderResourceView* pDX11SRV = c_pSrcTexture->GetD3D11TextureSRV();

	if (pDX11SRV)
		BindDX11LoadedTexture(pDX11SRV);

	m_bEmpty = false;
}

bool CGraphicImageTexture::CreateFromDDSTexture(UINT bufSize, const void* c_pvBuf)
{
	if (ShouldUseDX11TexturePath())
	{
		CGraphicDeviceDX11* pDX11Device = GetActiveDX11Device();
		if (!pDX11Device || !c_pvBuf || !bufSize)
		{
			LogDX11TextureMissingDeviceOnce("CreateFromDDSTexture", m_stFileName);
			return false;
		}

		DestroyDX11Texture();

		ID3D11Resource* pResource = nullptr;
		ID3D11ShaderResourceView* pSRV = nullptr;
		HRESULT hr = DirectX::CreateDDSTextureFromMemory(
			pDX11Device->GetDevice(),
			reinterpret_cast<const uint8_t*>(c_pvBuf),
			bufSize,
			&pResource,
			&pSRV);
		if (FAILED(hr) || !pSRV || !pResource)
		{
			if (pSRV)
				pSRV->Release();
			if (pResource)
				pResource->Release();
			return false;
		}

		ID3D11Texture2D* pTex2D = nullptr;
		hr = pResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pTex2D));
		pResource->Release();
		if (FAILED(hr) || !pTex2D)
		{
			pSRV->Release();
			return false;
		}

		D3D11_TEXTURE2D_DESC desc = {};
		pTex2D->GetDesc(&desc);
		m_width = static_cast<int>(desc.Width);
		m_height = static_cast<int>(desc.Height);
		m_pDX11Texture = pTex2D;
		m_pDX11TextureSRV = pSRV;
		m_bDX11DynamicTexture = false;
		m_bEmpty = false;
		return true;
	}

	// M3-SPEEDTREE-IMAGE-48: DX9 fallback removed - DX11 native path only
	TraceError("GrpImageTexture::CreateFromMemoryPointer DX11 DDS load failed for %s", m_stFileName.c_str());
	return false;
}

bool CGraphicImageTexture::CreateFromSTB(UINT bufSize, const void* c_pvBuf)
{
	if (ShouldUseDX11TexturePath())
	{
		CGraphicDeviceDX11* pDX11Device = GetActiveDX11Device();
		if (!pDX11Device || !c_pvBuf || !bufSize)
		{
			LogDX11TextureMissingDeviceOnce("CreateFromSTB", m_stFileName);
			return false;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		unsigned char* data = stbi_load_from_memory((stbi_uc*)c_pvBuf, bufSize, &width, &height, &channels, 4);
		if (!data || width <= 0 || height <= 0)
		{
			if (data)
				stbi_image_free(data);
			return false;
		}

		std::vector<uint8_t> bgraData;
		bgraData.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
		for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i)
		{
			const size_t base = i * 4u;
			bgraData[base + 0] = data[base + 2];
			bgraData[base + 1] = data[base + 1];
			bgraData[base + 2] = data[base + 0];
			bgraData[base + 3] = data[base + 3];
		}
		stbi_image_free(data);

		DestroyDX11Texture();

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = bgraData.data();
		initData.SysMemPitch = static_cast<UINT>(width * 4);

		HRESULT hr = pDX11Device->GetDevice()->CreateTexture2D(&desc, &initData, &m_pDX11Texture);
		if (FAILED(hr) || !m_pDX11Texture)
		{
			DestroyDX11Texture();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		hr = pDX11Device->GetDevice()->CreateShaderResourceView(m_pDX11Texture, &srvDesc, &m_pDX11TextureSRV);
		if (FAILED(hr) || !m_pDX11TextureSRV)
		{
			DestroyDX11Texture();
			return false;
		}

		m_width = width;
		m_height = height;
		m_bDX11DynamicTexture = false;
		m_bEmpty = false;
		return true;
	}

	int width, height, channels;
	unsigned char* data = stbi_load_from_memory((stbi_uc*)c_pvBuf, bufSize, &width, &height, &channels, 4); // force RGBA
	if (!data)
		return false;

	TDecodedImageData decodedImage;
	decodedImage.width = width;
	decodedImage.height = height;
	decodedImage.format = TDecodedImageData::FORMAT_RGBA8;
	decodedImage.isDDS = false;
	decodedImage.mipLevels = 1;
	decodedImage.pixels.assign(data, data + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
	stbi_image_free(data);

	return CreateFromDecodedData(decodedImage, m_d3dFmt, m_dwFilter);
}

bool CGraphicImageTexture::CreateFromTGA(UINT bufSize, const void* c_pvBuf)
{
	if (!c_pvBuf || !bufSize)
		return false;

	static std::unordered_set<std::string> s_tgaOkFiles;
	static std::unordered_set<std::string> s_tgaFailFiles;

	DecodedRGBAImage decodedTGA;
	std::string reason;
	if (!DecodeTGAFromMemory(reinterpret_cast<const uint8_t*>(c_pvBuf), static_cast<size_t>(bufSize), decodedTGA, &reason))
	{
		if (ShouldLogTGAForFile(m_stFileName) && ConsumeOneShotKey(s_tgaFailFiles, ToLowerASCII(m_stFileName)))
		{
			TraceError("DX11_TGA_DECODE_FAIL file=%s reason=%s", m_stFileName.c_str(), reason.c_str());
		}
		return false;
	}

	TDecodedImageData decodedImage;
	decodedImage.width = decodedTGA.width;
	decodedImage.height = decodedTGA.height;
	decodedImage.format = TDecodedImageData::FORMAT_RGBA8;
	decodedImage.isDDS = false;
	decodedImage.mipLevels = 1;
	decodedImage.pixels.swap(decodedTGA.pixels);

	const bool ok = CreateFromDecodedData(decodedImage, m_d3dFmt, m_dwFilter);
	if (ok)
	{
		if (ShouldLogTGAForFile(m_stFileName) && ConsumeOneShotKey(s_tgaOkFiles, ToLowerASCII(m_stFileName)))
		{
			TraceError("DX11_TGA_DECODE_OK file=%s w=%d h=%d", m_stFileName.c_str(), decodedImage.width, decodedImage.height);
		}
	}
	else if (ShouldLogTGAForFile(m_stFileName))
	{
		if (ConsumeOneShotKey(s_tgaFailFiles, ToLowerASCII(m_stFileName) + "|upload"))
		{
			TraceError("DX11_TGA_DECODE_FAIL file=%s reason=upload_failed", m_stFileName.c_str());
		}
	}

	return ok;
}

bool CGraphicImageTexture::CreateFromSOIL2(UINT bufSize, const void* c_pvBuf)
{
	if (!c_pvBuf || !bufSize)
		return false;

	static std::unordered_set<std::string> s_soilOkFiles;
	static std::unordered_set<std::string> s_soilFailFiles;

	Soil2Api& soil = GetSoil2Api();
	if (!soil.module || !soil.loadFromMemory || !soil.freeImageData)
	{
		if (ShouldLogTGAForFile(m_stFileName) && ConsumeOneShotKey(s_soilFailFiles, ToLowerASCII(m_stFileName) + "|module_missing"))
			TraceError("DX11_SOIL2_DECODE_FAIL file=%s reason=module_missing", m_stFileName.c_str());
		return false;
	}

	int width = 0;
	int height = 0;
	int channels = 0;
	const int kLoadRGBA = 4;
	unsigned char* pPixels = soil.loadFromMemory(reinterpret_cast<const unsigned char*>(c_pvBuf), static_cast<int>(bufSize), &width, &height, &channels, kLoadRGBA);
	if (!pPixels || width <= 0 || height <= 0)
	{
		if (ConsumeOneShotKey(s_soilFailFiles, ToLowerASCII(m_stFileName)))
		{
			const char* reason = (soil.lastResult ? soil.lastResult() : "unknown");
			TraceError("DX11_SOIL2_DECODE_FAIL file=%s reason=%s", m_stFileName.c_str(), reason ? reason : "unknown");
		}
		return false;
	}

	TDecodedImageData decodedImage;
	decodedImage.width = width;
	decodedImage.height = height;
	decodedImage.format = TDecodedImageData::FORMAT_RGBA8;
	decodedImage.isDDS = false;
	decodedImage.mipLevels = 1;
	decodedImage.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
	memcpy(decodedImage.pixels.data(), pPixels, decodedImage.pixels.size());
	soil.freeImageData(pPixels);

	const bool ok = CreateFromDecodedData(decodedImage, m_d3dFmt, m_dwFilter);
	if (ok)
	{
		if (ConsumeOneShotKey(s_soilOkFiles, ToLowerASCII(m_stFileName)))
		{
			TraceError("DX11_SOIL2_DECODE_OK file=%s w=%d h=%d", m_stFileName.c_str(), decodedImage.width, decodedImage.height);
		}
	}

	return ok;
}

bool CGraphicImageTexture::CreateFromMemoryFile(UINT bufSize, const void * c_pvBuf, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	(void)d3dFmt;
	(void)dwFilter;

	if (!c_pvBuf || !bufSize)
		return false;

	CGraphicDeviceDX11* pDX11Device = GetActiveDX11Device();
	if (!pDX11Device)
	{
		LogDX11TextureMissingDeviceOnce("CreateFromMemoryFile", m_stFileName);
		return false;
	}

	m_bEmpty = true;

	if (CreateFromDDSTexture(bufSize, c_pvBuf))
	{
		m_bEmpty = false;
		return true;
	}

	if (CreateFromTGA(bufSize, c_pvBuf))
	{
		m_bEmpty = false;
		return true;
	}

	if (CreateFromSTB(bufSize, c_pvBuf))
	{
		m_bEmpty = false;
		return true;
	}

	if (CreateFromSOIL2(bufSize, c_pvBuf))
	{
		m_bEmpty = false;
		return true;
	}

	TraceError("CreateFromMemoryFile: Cannot create DX11 texture (%s, %u bytes) reason=all_decoders_failed",
		m_stFileName.c_str(), bufSize);
	return false;
}

void CGraphicImageTexture::SetFileName(const char * c_szFileName)
{
	m_stFileName=c_szFileName;
}

bool CGraphicImageTexture::CreateFromDiskFile(const char * c_szFileName, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	Destroy();

	SetFileName(c_szFileName);

	m_d3dFmt = d3dFmt;
	m_dwFilter = dwFilter;
	return CreateDeviceObjects();
}

bool CGraphicImageTexture::CreateFromDecodedData(const TDecodedImageData& decodedImage, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	if (ShouldUseDX11TexturePath())
	{
		CGraphicDeviceDX11* pDX11Device = GetActiveDX11Device();
		if (!pDX11Device)
		{
			LogDX11TextureMissingDeviceOnce("CreateFromDecodedData", m_stFileName);
			return false;
		}

		if (!decodedImage.IsValid())
			return false;

		if (decodedImage.isDDS)
			return CreateFromMemoryFile(decodedImage.pixels.size(), decodedImage.pixels.data(), d3dFmt, dwFilter);

		if (decodedImage.format != TDecodedImageData::FORMAT_RGBA8)
		{
			TraceError("CreateFromDecodedData(DX11): Unsupported decoded image format");
			return false;
		}

		DestroyDX11Texture();

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = decodedImage.width;
		desc.Height = decodedImage.height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = decodedImage.pixels.data();
		initData.SysMemPitch = decodedImage.width * 4;

		HRESULT hr = pDX11Device->GetDevice()->CreateTexture2D(&desc, &initData, &m_pDX11Texture);
		if (FAILED(hr) || !m_pDX11Texture)
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		hr = pDX11Device->GetDevice()->CreateShaderResourceView(m_pDX11Texture, &srvDesc, &m_pDX11TextureSRV);
		if (FAILED(hr) || !m_pDX11TextureSRV)
		{
			DestroyDX11Texture();
			return false;
		}

		m_width = decodedImage.width;
		m_height = decodedImage.height;
		m_bDX11DynamicTexture = false;
		m_bEmpty = false;
		return true;
	}

	if (!decodedImage.IsValid())
		return false;

	TraceError("CreateFromDecodedData(DX11): failed to upload decoded texture w=%d h=%d format=%d",
		decodedImage.width, decodedImage.height, static_cast<int>(decodedImage.format));
	return false;
}

CGraphicImageTexture::CGraphicImageTexture()
{
	Initialize();
}

CGraphicImageTexture::~CGraphicImageTexture()
{
	Destroy();
}
