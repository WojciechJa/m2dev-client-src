#pragma once

#include "BinPacker.h"
#include "TextureLoader.h"
#include <string>
#include <vector>
#include <map>

// Main atlas packer - combines multiple textures into optimized atlases
class AtlasPacker
{
public:
    struct AtlasConfig
    {
        uint32_t uAtlasWidth;
        uint32_t uAtlasHeight;
        uint32_t uPadding;        // Padding between textures (for mipmap bleeding prevention)
        uint32_t uMaxMipLevels;   // Maximum mipmap levels to generate
        DXGI_FORMAT targetFormat; // Target atlas format
        std::wstring wsOutputPath;

        AtlasConfig()
            : uAtlasWidth(4096)
            , uAtlasHeight(4096)
            , uPadding(2)
            , uMaxMipLevels(0)  // 0 = auto (all mipmaps)
            , targetFormat(DXGI_FORMAT_BC3_UNORM)  // DXT5 default
        {
        }
    };

    struct TextureEntry
    {
        std::wstring wsSourcePath;
        std::string strTreeTypeName;  // e.g., "tree_01"
        TextureLoader::TextureInfo textureInfo;
        BinPacker::Rect packedRect;
        bool bPacked;

        TextureEntry()
            : bPacked(false)
        {
        }
    };

    struct UVMapping
    {
        std::string strTreeTypeName;
        float fOffsetU;    // UV offset (0.0 - 1.0)
        float fOffsetV;
        float fScaleU;     // UV scale (0.0 - 1.0)
        float fScaleV;
        uint32_t uAtlasX;  // Pixel coordinates in atlas
        uint32_t uAtlasY;
        uint32_t uWidth;
        uint32_t uHeight;
    };

    AtlasPacker();
    ~AtlasPacker();

    // Add texture to be packed
    bool AddTexture(const std::wstring& wsSourcePath, const std::string& strTreeTypeName);

    // Pack all textures into atlas and save
    bool PackAndSave(const AtlasConfig& config);

    // Get UV mappings for all packed textures
    const std::vector<UVMapping>& GetUVMappings() const { return m_vUVMappings; }

    // Export UV mappings to JSON file
    bool ExportMappingJSON(const std::wstring& wsOutputPath) const;

    // Get statistics
    void GetStatistics(
        uint32_t* pOutTotalTextures,
        uint32_t* pOutPackedTextures,
        float* pOutEfficiency) const;

private:
    std::vector<TextureEntry> m_vTextures;
    std::vector<UVMapping> m_vUVMappings;
    float m_fPackingEfficiency;

    // Load all textures from disk
    bool LoadAllTextures();

    // Generate UV mappings from packed rectangles
    void GenerateUVMappings(uint32_t atlasWidth, uint32_t atlasHeight);
};
