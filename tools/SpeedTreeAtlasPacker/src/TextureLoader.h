#pragma once

#include <DirectXTex.h>
#include <string>
#include <vector>
#include <cstdint>

// DDS texture loading and manipulation using DirectXTex
class TextureLoader
{
public:
    struct TextureInfo
    {
        std::string strFilename;
        uint32_t uWidth;
        uint32_t uHeight;
        uint32_t uMipLevels;
        DXGI_FORMAT format;
        DirectX::ScratchImage image;  // Holds loaded texture data

        TextureInfo()
            : uWidth(0)
            , uHeight(0)
            , uMipLevels(0)
            , format(DXGI_FORMAT_UNKNOWN)
        {
        }
    };

    // Load a DDS texture from file
    static bool LoadDDS(const std::wstring& wsFilePath, TextureInfo& outInfo);

    // Save a DDS texture to file with mipmaps
    static bool SaveDDS(const std::wstring& wsFilePath, const DirectX::ScratchImage& image);

    // Create empty texture with given dimensions and format
    static bool CreateEmptyTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, TextureInfo& outInfo);

    // Copy texture data from source to destination at specified position
    // Supports mipmap generation for the destination
    static bool CopyToAtlas(
        const DirectX::ScratchImage& srcImage,
        DirectX::ScratchImage& dstImage,
        uint32_t dstX,
        uint32_t dstY,
        uint32_t srcMipLevel = 0,
        uint32_t dstMipLevel = 0);

    // Generate mipmaps for an image
    static bool GenerateMipmaps(DirectX::ScratchImage& image, uint32_t maxMipLevels);

    // Add padding around copied region (for mipmap bleeding prevention)
    static bool AddPadding(
        DirectX::ScratchImage& image,
        uint32_t x,
        uint32_t y,
        uint32_t width,
        uint32_t height,
        uint32_t padding);

    // Get texture format name as string
    static std::string GetFormatName(DXGI_FORMAT format);
};
