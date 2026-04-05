#include "AtlasPacker.h"
#include <iostream>
#include <fstream>
#include <algorithm>

AtlasPacker::AtlasPacker()
    : m_fPackingEfficiency(0.0f)
{
}

AtlasPacker::~AtlasPacker()
{
}

bool AtlasPacker::AddTexture(const std::wstring& wsSourcePath, const std::string& strTreeTypeName)
{
    TextureEntry entry;
    entry.wsSourcePath = wsSourcePath;
    entry.strTreeTypeName = strTreeTypeName;
    entry.bPacked = false;

    m_vTextures.push_back(entry);
    return true;
}

bool AtlasPacker::LoadAllTextures()
{
    std::cout << "Loading " << m_vTextures.size() << " textures..." << std::endl;

    for (auto& entry : m_vTextures)
    {
        if (!TextureLoader::LoadDDS(entry.wsSourcePath, entry.textureInfo))
        {
            std::wcerr << L"Warning: Failed to load texture: " << entry.wsSourcePath << std::endl;
            continue;
        }

        std::wcout << L"  Loaded: " << entry.wsSourcePath << L" ("
                   << entry.textureInfo.uWidth << L"x" << entry.textureInfo.uHeight << L", "
                   << TextureLoader::GetFormatName(entry.textureInfo.format).c_str() << L")"
                   << std::endl;
    }

    return true;
}

bool AtlasPacker::PackAndSave(const AtlasConfig& config)
{
    // Step 1: Load all textures
    if (!LoadAllTextures())
    {
        std::cerr << "Failed to load textures" << std::endl;
        return false;
    }

    // Step 2: Prepare rectangles for bin packing
    std::vector<BinPacker::Rect> rects;
    for (size_t i = 0; i < m_vTextures.size(); ++i)
    {
        auto& entry = m_vTextures[i];
        if (entry.textureInfo.uWidth == 0 || entry.textureInfo.uHeight == 0)
            continue;  // Skip failed loads

        // Add padding to dimensions
        BinPacker::Rect rect(
            entry.textureInfo.uWidth + config.uPadding * 2,
            entry.textureInfo.uHeight + config.uPadding * 2,
            reinterpret_cast<void*>(i));  // Store index as user data

        rects.push_back(rect);
    }

    // Step 3: Pack rectangles
    std::cout << "\nPacking " << rects.size() << " textures into "
              << config.uAtlasWidth << "x" << config.uAtlasHeight << " atlas..." << std::endl;

    BinPacker packer(config.uAtlasWidth, config.uAtlasHeight);
    uint32_t uPackedCount = packer.PackAll(rects);

    if (uPackedCount < rects.size())
    {
        std::cerr << "Warning: Only " << uPackedCount << " of " << rects.size()
                  << " textures fit in atlas!" << std::endl;
    }

    m_fPackingEfficiency = packer.GetEfficiency();
    std::cout << "Packing efficiency: " << (m_fPackingEfficiency * 100.0f) << "%" << std::endl;

    // Step 4: Create empty atlas texture
    TextureLoader::TextureInfo atlasInfo;
    if (!TextureLoader::CreateEmptyTexture(config.uAtlasWidth, config.uAtlasHeight, config.targetFormat, atlasInfo))
    {
        std::cerr << "Failed to create atlas texture" << std::endl;
        return false;
    }

    // Step 5: Copy textures into atlas
    std::cout << "\nCopying textures into atlas..." << std::endl;

    for (size_t i = 0; i < rects.size(); ++i)
    {
        const BinPacker::Rect& rect = rects[i];
        size_t texIdx = reinterpret_cast<size_t>(rect.pUserData);
        auto& entry = m_vTextures[texIdx];

        // Copy texture to atlas position (with padding offset)
        uint32_t dstX = rect.x + config.uPadding;
        uint32_t dstY = rect.y + config.uPadding;

        if (!TextureLoader::CopyToAtlas(
            entry.textureInfo.image,
            atlasInfo.image,
            dstX,
            dstY))
        {
            std::cerr << "Failed to copy texture " << entry.strTreeTypeName << " to atlas" << std::endl;
            continue;
        }

        // Add padding around texture (replicate edges)
        TextureLoader::AddPadding(
            atlasInfo.image,
            dstX,
            dstY,
            entry.textureInfo.uWidth,
            entry.textureInfo.uHeight,
            config.uPadding);

        // Store packed rect
        entry.packedRect = rect;
        entry.bPacked = true;

        std::cout << "  Packed: " << entry.strTreeTypeName << " at ("
                  << dstX << ", " << dstY << ")" << std::endl;
    }

    // Step 6: Generate mipmaps
    std::cout << "\nGenerating mipmaps..." << std::endl;
    if (!TextureLoader::GenerateMipmaps(atlasInfo.image, config.uMaxMipLevels))
    {
        std::cerr << "Warning: Failed to generate mipmaps" << std::endl;
    }
    else
    {
        std::cout << "Generated " << atlasInfo.image.GetMetadata().mipLevels << " mip levels" << std::endl;
    }

    // Step 7: Save atlas to file
    std::cout << "\nSaving atlas to: " << std::string(config.wsOutputPath.begin(), config.wsOutputPath.end()) << std::endl;
    if (!TextureLoader::SaveDDS(config.wsOutputPath, atlasInfo.image))
    {
        std::cerr << "Failed to save atlas texture" << std::endl;
        return false;
    }

    // Step 8: Generate UV mappings
    GenerateUVMappings(config.uAtlasWidth, config.uAtlasHeight);

    std::cout << "\nAtlas packing complete!" << std::endl;
    std::cout << "  Packed textures: " << uPackedCount << " / " << rects.size() << std::endl;
    std::cout << "  Efficiency: " << (m_fPackingEfficiency * 100.0f) << "%" << std::endl;

    return true;
}

void AtlasPacker::GenerateUVMappings(uint32_t atlasWidth, uint32_t atlasHeight)
{
    m_vUVMappings.clear();

    for (const auto& entry : m_vTextures)
    {
        if (!entry.bPacked)
            continue;

        UVMapping mapping;
        mapping.strTreeTypeName = entry.strTreeTypeName;

        // Pixel coordinates (excluding padding)
        mapping.uAtlasX = entry.packedRect.x;
        mapping.uAtlasY = entry.packedRect.y;
        mapping.uWidth = entry.textureInfo.uWidth;
        mapping.uHeight = entry.textureInfo.uHeight;

        // Normalized UV coordinates (0.0 - 1.0)
        mapping.fOffsetU = static_cast<float>(mapping.uAtlasX) / static_cast<float>(atlasWidth);
        mapping.fOffsetV = static_cast<float>(mapping.uAtlasY) / static_cast<float>(atlasHeight);
        mapping.fScaleU = static_cast<float>(mapping.uWidth) / static_cast<float>(atlasWidth);
        mapping.fScaleV = static_cast<float>(mapping.uHeight) / static_cast<float>(atlasHeight);

        m_vUVMappings.push_back(mapping);
    }
}

bool AtlasPacker::ExportMappingJSON(const std::wstring& wsOutputPath) const
{
    std::ofstream ofs(wsOutputPath);
    if (!ofs.is_open())
    {
        std::wcerr << L"Failed to open output file: " << wsOutputPath << std::endl;
        return false;
    }

    ofs << "{\n";
    ofs << "  \"atlas_mappings\": [\n";

    for (size_t i = 0; i < m_vUVMappings.size(); ++i)
    {
        const auto& mapping = m_vUVMappings[i];

        ofs << "    {\n";
        ofs << "      \"tree_type\": \"" << mapping.strTreeTypeName << "\",\n";
        ofs << "      \"uv_offset\": [" << mapping.fOffsetU << ", " << mapping.fOffsetV << "],\n";
        ofs << "      \"uv_scale\": [" << mapping.fScaleU << ", " << mapping.fScaleV << "],\n";
        ofs << "      \"atlas_pos\": [" << mapping.uAtlasX << ", " << mapping.uAtlasY << "],\n";
        ofs << "      \"size\": [" << mapping.uWidth << ", " << mapping.uHeight << "]\n";
        ofs << "    }";

        if (i < m_vUVMappings.size() - 1)
            ofs << ",";
        ofs << "\n";
    }

    ofs << "  ]\n";
    ofs << "}\n";

    ofs.close();

    std::wcout << L"Exported UV mappings to: " << wsOutputPath << std::endl;
    return true;
}

void AtlasPacker::GetStatistics(
    uint32_t* pOutTotalTextures,
    uint32_t* pOutPackedTextures,
    float* pOutEfficiency) const
{
    if (pOutTotalTextures)
        *pOutTotalTextures = static_cast<uint32_t>(m_vTextures.size());

    if (pOutPackedTextures)
    {
        uint32_t uPacked = 0;
        for (const auto& entry : m_vTextures)
        {
            if (entry.bPacked)
                ++uPacked;
        }
        *pOutPackedTextures = uPacked;
    }

    if (pOutEfficiency)
        *pOutEfficiency = m_fPackingEfficiency;
}
