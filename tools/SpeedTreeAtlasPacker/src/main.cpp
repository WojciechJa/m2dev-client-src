#include "AtlasPacker.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

struct CommandLineArgs
{
    std::wstring wsInputDir;
    std::wstring wsOutputDir;
    std::wstring wsAtlasName;
    uint32_t uAtlasWidth;
    uint32_t uAtlasHeight;
    uint32_t uPadding;
    bool bCompositeAtlas;  // true = composite textures, false = branch textures
    bool bShowHelp;

    CommandLineArgs()
        : uAtlasWidth(4096)
        , uAtlasHeight(4096)
        , uPadding(2)
        , bCompositeAtlas(true)
        , bShowHelp(false)
    {
    }
};

void PrintUsage()
{
    std::cout << "SpeedTreeAtlasPacker - Texture Atlas Generator for SpeedTree Textures\n\n";
    std::cout << "Usage:\n";
    std::cout << "  SpeedTreeAtlasPacker [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -input <dir>       Input directory containing DDS textures\n";
    std::cout << "  -output <dir>      Output directory for atlas and mapping JSON\n";
    std::cout << "  -name <name>       Atlas output name (without extension)\n";
    std::cout << "  -width <pixels>    Atlas width (default: 4096)\n";
    std::cout << "  -height <pixels>   Atlas height (default: 4096)\n";
    std::cout << "  -padding <pixels>  Padding between textures (default: 2)\n";
    std::cout << "  -type <composite|branch>  Texture type to pack (default: composite)\n";
    std::cout << "  -help              Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  SpeedTreeAtlasPacker -input textures/composite -output atlases -name speedtree_composite -type composite\n";
    std::cout << "  SpeedTreeAtlasPacker -input textures/branch -output atlases -name speedtree_branch -type branch -width 2048 -height 2048\n";
}

bool ParseCommandLine(int argc, char* argv[], CommandLineArgs& outArgs)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-help" || arg == "--help" || arg == "-h")
        {
            outArgs.bShowHelp = true;
            return true;
        }
        else if (arg == "-input" && i + 1 < argc)
        {
            outArgs.wsInputDir = std::wstring(argv[++i], argv[i] + strlen(argv[i]));
        }
        else if (arg == "-output" && i + 1 < argc)
        {
            outArgs.wsOutputDir = std::wstring(argv[++i], argv[i] + strlen(argv[i]));
        }
        else if (arg == "-name" && i + 1 < argc)
        {
            outArgs.wsAtlasName = std::wstring(argv[++i], argv[i] + strlen(argv[i]));
        }
        else if (arg == "-width" && i + 1 < argc)
        {
            outArgs.uAtlasWidth = static_cast<uint32_t>(std::stoi(argv[++i]));
        }
        else if (arg == "-height" && i + 1 < argc)
        {
            outArgs.uAtlasHeight = static_cast<uint32_t>(std::stoi(argv[++i]));
        }
        else if (arg == "-padding" && i + 1 < argc)
        {
            outArgs.uPadding = static_cast<uint32_t>(std::stoi(argv[++i]));
        }
        else if (arg == "-type" && i + 1 < argc)
        {
            std::string type = argv[++i];
            if (type == "composite")
                outArgs.bCompositeAtlas = true;
            else if (type == "branch")
                outArgs.bCompositeAtlas = false;
            else
            {
                std::cerr << "Invalid type: " << type << " (must be 'composite' or 'branch')" << std::endl;
                return false;
            }
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    return true;
}

bool ValidateArgs(const CommandLineArgs& args)
{
    if (args.wsInputDir.empty())
    {
        std::cerr << "Error: Input directory not specified (use -input)" << std::endl;
        return false;
    }

    if (args.wsOutputDir.empty())
    {
        std::cerr << "Error: Output directory not specified (use -output)" << std::endl;
        return false;
    }

    if (args.wsAtlasName.empty())
    {
        std::cerr << "Error: Atlas name not specified (use -name)" << std::endl;
        return false;
    }

    if (!fs::exists(args.wsInputDir))
    {
        std::wcerr << L"Error: Input directory does not exist: " << args.wsInputDir << std::endl;
        return false;
    }

    // Create output directory if it doesn't exist
    if (!fs::exists(args.wsOutputDir))
    {
        std::wcout << L"Creating output directory: " << args.wsOutputDir << std::endl;
        fs::create_directories(args.wsOutputDir);
    }

    return true;
}

std::string ExtractTreeTypeName(const std::wstring& wsFilename)
{
    // Extract tree type from filename
    // Example: "tree_01_composite.dds" -> "tree_01"
    // Example: "tree_03_branch.dds" -> "tree_03"

    fs::path p(wsFilename);
    std::wstring wsStem = p.stem().wstring();

    // Remove "_composite" or "_branch" suffix
    size_t compositePos = wsStem.find(L"_composite");
    if (compositePos != std::wstring::npos)
        wsStem = wsStem.substr(0, compositePos);

    size_t branchPos = wsStem.find(L"_branch");
    if (branchPos != std::wstring::npos)
        wsStem = wsStem.substr(0, branchPos);

    return std::string(wsStem.begin(), wsStem.end());
}

int main(int argc, char* argv[])
{
    std::cout << "=================================================\n";
    std::cout << "   SpeedTree Atlas Packer for Metin2 DX11\n";
    std::cout << "   M3-SPEEDTREE-ATLAS-09\n";
    std::cout << "=================================================\n\n";

    CommandLineArgs args;
    if (!ParseCommandLine(argc, argv, args))
    {
        PrintUsage();
        return 1;
    }

    if (args.bShowHelp)
    {
        PrintUsage();
        return 0;
    }

    if (!ValidateArgs(args))
    {
        PrintUsage();
        return 1;
    }

    // Scan input directory for DDS files
    std::vector<fs::path> texturePaths;
    std::wcout << L"Scanning input directory: " << args.wsInputDir << std::endl;

    for (const auto& entry : fs::directory_iterator(args.wsInputDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == L".dds")
        {
            texturePaths.push_back(entry.path());
        }
    }

    if (texturePaths.empty())
    {
        std::cerr << "Error: No DDS files found in input directory" << std::endl;
        return 1;
    }

    std::cout << "Found " << texturePaths.size() << " DDS textures\n\n";

    // Create atlas packer
    AtlasPacker packer;

    // Add textures to packer
    for (const auto& texPath : texturePaths)
    {
        std::string strTreeType = ExtractTreeTypeName(texPath.filename().wstring());
        packer.AddTexture(texPath.wstring(), strTreeType);
    }

    // Configure atlas
    AtlasPacker::AtlasConfig config;
    config.uAtlasWidth = args.uAtlasWidth;
    config.uAtlasHeight = args.uAtlasHeight;
    config.uPadding = args.uPadding;
    config.uMaxMipLevels = 0;  // Auto-generate all mipmaps
    config.targetFormat = DXGI_FORMAT_BC3_UNORM;  // DXT5 (common for tree textures)

    // Output paths
    fs::path outputAtlasPath = fs::path(args.wsOutputDir) / (args.wsAtlasName + L".dds");
    fs::path outputMappingPath = fs::path(args.wsOutputDir) / (args.wsAtlasName + L"_mapping.json");

    config.wsOutputPath = outputAtlasPath.wstring();

    // Pack and save atlas
    if (!packer.PackAndSave(config))
    {
        std::cerr << "Error: Atlas packing failed" << std::endl;
        return 1;
    }

    // Export UV mapping JSON
    if (!packer.ExportMappingJSON(outputMappingPath.wstring()))
    {
        std::cerr << "Error: Failed to export UV mapping JSON" << std::endl;
        return 1;
    }

    // Print final statistics
    uint32_t uTotal, uPacked;
    float fEfficiency;
    packer.GetStatistics(&uTotal, &uPacked, &fEfficiency);

    std::cout << "\n=================================================\n";
    std::cout << "Atlas Generation Summary:\n";
    std::cout << "  Total textures: " << uTotal << "\n";
    std::cout << "  Packed textures: " << uPacked << "\n";
    std::cout << "  Packing efficiency: " << (fEfficiency * 100.0f) << "%\n";
    std::cout << "  Output atlas: " << std::string(outputAtlasPath.string().begin(), outputAtlasPath.string().end()) << "\n";
    std::cout << "  Output mapping: " << std::string(outputMappingPath.string().begin(), outputMappingPath.string().end()) << "\n";
    std::cout << "=================================================\n";

    return 0;
}
