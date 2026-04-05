#include "TextureLoader.h"
#include <iostream>
#include <algorithm>

bool TextureLoader::LoadDDS(const std::wstring& wsFilePath, TextureInfo& outInfo)
{
    DirectX::TexMetadata metadata;
    HRESULT hr = DirectX::LoadFromDDSFile(
        wsFilePath.c_str(),
        DirectX::DDS_FLAGS_NONE,
        &metadata,
        outInfo.image);

    if (FAILED(hr))
    {
        std::wcerr << L"Failed to load DDS file: " << wsFilePath << L" (HRESULT: 0x"
                   << std::hex << hr << std::dec << L")" << std::endl;
        return false;
    }

    outInfo.uWidth = static_cast<uint32_t>(metadata.width);
    outInfo.uHeight = static_cast<uint32_t>(metadata.height);
    outInfo.uMipLevels = static_cast<uint32_t>(metadata.mipLevels);
    outInfo.format = metadata.format;

    // Convert wide string to narrow for storage
    std::wstring ws = wsFilePath;
    outInfo.strFilename = std::string(ws.begin(), ws.end());

    return true;
}

bool TextureLoader::SaveDDS(const std::wstring& wsFilePath, const DirectX::ScratchImage& image)
{
    const DirectX::TexMetadata& metadata = image.GetMetadata();

    HRESULT hr = DirectX::SaveToDDSFile(
        image.GetImages(),
        image.GetImageCount(),
        metadata,
        DirectX::DDS_FLAGS_NONE,
        wsFilePath.c_str());

    if (FAILED(hr))
    {
        std::wcerr << L"Failed to save DDS file: " << wsFilePath << L" (HRESULT: 0x"
                   << std::hex << hr << std::dec << L")" << std::endl;
        return false;
    }

    return true;
}

bool TextureLoader::CreateEmptyTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, TextureInfo& outInfo)
{
    // Create scratch image with single mip level (mipmaps will be generated later)
    HRESULT hr = outInfo.image.Initialize2D(format, width, height, 1, 1);

    if (FAILED(hr))
    {
        std::cerr << "Failed to create empty texture " << width << "x" << height
                  << " (HRESULT: 0x" << std::hex << hr << std::dec << ")" << std::endl;
        return false;
    }

    outInfo.uWidth = width;
    outInfo.uHeight = height;
    outInfo.uMipLevels = 1;
    outInfo.format = format;
    outInfo.strFilename = "<atlas>";

    // Clear to black (0, 0, 0, 0)
    const DirectX::Image* pImage = outInfo.image.GetImage(0, 0, 0);
    if (pImage)
    {
        memset(pImage->pixels, 0, pImage->slicePitch);
    }

    return true;
}

bool TextureLoader::CopyToAtlas(
    const DirectX::ScratchImage& srcImage,
    DirectX::ScratchImage& dstImage,
    uint32_t dstX,
    uint32_t dstY,
    uint32_t srcMipLevel,
    uint32_t dstMipLevel)
{
    const DirectX::Image* pSrcImg = srcImage.GetImage(srcMipLevel, 0, 0);
    const DirectX::Image* pDstImg = dstImage.GetImage(dstMipLevel, 0, 0);

    if (!pSrcImg || !pDstImg)
    {
        std::cerr << "Invalid mip level for copy operation" << std::endl;
        return false;
    }

    // Ensure formats match
    if (pSrcImg->format != pDstImg->format)
    {
        std::cerr << "Source and destination formats do not match" << std::endl;
        return false;
    }

    // Calculate bytes per pixel
    size_t bpp = DirectX::BitsPerPixel(pSrcImg->format) / 8;

    // Copy row by row
    uint32_t copyWidth = static_cast<uint32_t>(pSrcImg->width);
    uint32_t copyHeight = static_cast<uint32_t>(pSrcImg->height);

    for (uint32_t y = 0; y < copyHeight; ++y)
    {
        uint32_t dstYPos = dstY + y;
        if (dstYPos >= pDstImg->height)
            break;

        const uint8_t* pSrcRow = pSrcImg->pixels + (y * pSrcImg->rowPitch);
        uint8_t* pDstRow = pDstImg->pixels + (dstYPos * pDstImg->rowPitch) + (dstX * bpp);

        size_t copyBytes = std::min(copyWidth * bpp, pDstImg->rowPitch - (dstX * bpp));
        memcpy(pDstRow, pSrcRow, copyBytes);
    }

    return true;
}

bool TextureLoader::GenerateMipmaps(DirectX::ScratchImage& image, uint32_t maxMipLevels)
{
    const DirectX::TexMetadata& metadata = image.GetMetadata();

    DirectX::ScratchImage mipChain;
    HRESULT hr = DirectX::GenerateMipMaps(
        image.GetImages(),
        image.GetImageCount(),
        metadata,
        DirectX::TEX_FILTER_DEFAULT,
        maxMipLevels,
        mipChain);

    if (FAILED(hr))
    {
        std::cerr << "Failed to generate mipmaps (HRESULT: 0x"
                  << std::hex << hr << std::dec << ")" << std::endl;
        return false;
    }

    // Replace original image with mipmapped version
    image = std::move(mipChain);
    return true;
}

bool TextureLoader::AddPadding(
    DirectX::ScratchImage& image,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t padding)
{
    const DirectX::Image* pImg = image.GetImage(0, 0, 0);
    if (!pImg)
        return false;

    size_t bpp = DirectX::BitsPerPixel(pImg->format) / 8;

    // Replicate edge pixels into padding area
    // Top edge
    if (y >= padding)
    {
        for (uint32_t py = 0; py < padding; ++py)
        {
            const uint8_t* pSrcRow = pImg->pixels + (y * pImg->rowPitch) + (x * bpp);
            uint8_t* pDstRow = pImg->pixels + ((y - padding + py) * pImg->rowPitch) + (x * bpp);
            memcpy(pDstRow, pSrcRow, width * bpp);
        }
    }

    // Bottom edge
    if (y + height + padding <= pImg->height)
    {
        for (uint32_t py = 0; py < padding; ++py)
        {
            const uint8_t* pSrcRow = pImg->pixels + ((y + height - 1) * pImg->rowPitch) + (x * bpp);
            uint8_t* pDstRow = pImg->pixels + ((y + height + py) * pImg->rowPitch) + (x * bpp);
            memcpy(pDstRow, pSrcRow, width * bpp);
        }
    }

    // Left and right edges (including corners)
    for (uint32_t row = (y >= padding ? y - padding : y);
         row < std::min(y + height + padding, static_cast<uint32_t>(pImg->height));
         ++row)
    {
        uint8_t* pRow = pImg->pixels + (row * pImg->rowPitch);

        // Left edge
        if (x >= padding)
        {
            const uint8_t* pSrcPixel = pRow + (x * bpp);
            for (uint32_t px = 0; px < padding; ++px)
            {
                uint8_t* pDstPixel = pRow + ((x - padding + px) * bpp);
                memcpy(pDstPixel, pSrcPixel, bpp);
            }
        }

        // Right edge
        if (x + width + padding <= pImg->width)
        {
            const uint8_t* pSrcPixel = pRow + ((x + width - 1) * bpp);
            for (uint32_t px = 0; px < padding; ++px)
            {
                uint8_t* pDstPixel = pRow + ((x + width + px) * bpp);
                memcpy(pDstPixel, pSrcPixel, bpp);
            }
        }
    }

    return true;
}

std::string TextureLoader::GetFormatName(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_BC1_UNORM: return "BC1_UNORM (DXT1)";
    case DXGI_FORMAT_BC2_UNORM: return "BC2_UNORM (DXT3)";
    case DXGI_FORMAT_BC3_UNORM: return "BC3_UNORM (DXT5)";
    case DXGI_FORMAT_BC4_UNORM: return "BC4_UNORM";
    case DXGI_FORMAT_BC5_UNORM: return "BC5_UNORM";
    case DXGI_FORMAT_BC7_UNORM: return "BC7_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    default: return "UNKNOWN";
    }
}
