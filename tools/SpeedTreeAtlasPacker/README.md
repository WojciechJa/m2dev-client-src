# SpeedTree Atlas Packer Tool

Offline texture atlas generator for SpeedTree textures in Metin2 DX11.

## Purpose

Packs multiple SpeedTree textures (composite and branch) into optimized atlases to reduce GPU texture binding overhead.

## Dependencies

**DirectXTex** is required for DDS texture loading, manipulation, and mipmap generation.

### Installing DirectXTex

#### Option 1: vcpkg (Recommended)
```bash
# Install vcpkg if not already installed
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# Install DirectXTex
.\vcpkg install directxtex:x64-windows

# Integrate with CMake
.\vcpkg integrate install
```

#### Option 2: Manual Build
Download and build DirectXTex from: https://github.com/microsoft/DirectXTex

## Building the Tool

Once DirectXTex is installed:

1. Uncomment the tool in `tools/CMakeLists.txt`:
   ```cmake
   add_subdirectory(SpeedTreeAtlasPacker)
   ```

2. Reconfigure CMake:
   ```bash
   cd build
   cmake ..
   ```

3. Build the tool:
   ```bash
   cmake --build . --target SpeedTreeAtlasPacker --config Debug
   ```

## Usage

```bash
# Pack composite textures
SpeedTreeAtlasPacker -input "path/to/composite/textures" -output "path/to/output" -name speedtree_composite -type composite -width 4096 -height 4096

# Pack branch textures
SpeedTreeAtlasPacker -input "path/to/branch/textures" -output "path/to/output" -name speedtree_branch -type branch -width 2048 -height 2048
```

### Options

- `-input <dir>` - Input directory containing DDS textures
- `-output <dir>` - Output directory for atlas and mapping JSON
- `-name <name>` - Atlas output name (without extension)
- `-width <pixels>` - Atlas width (default: 4096)
- `-height <pixels>` - Atlas height (default: 4096)
- `-padding <pixels>` - Padding between textures (default: 2)
- `-type <composite|branch>` - Texture type to pack
- `-help` - Show help message

## Output Files

The tool generates two files:

1. **Atlas DDS file**: `<name>.dds` - Packed texture atlas with mipmaps
2. **Mapping JSON**: `<name>_mapping.json` - UV coordinate mappings for runtime

Example mapping JSON structure:
```json
{
  "atlas_mappings": [
    {
      "tree_type": "tree_01",
      "uv_offset": [0.0, 0.0],
      "uv_scale": [0.25, 0.25],
      "atlas_pos": [0, 0],
      "size": [1024, 1024]
    }
  ]
}
```

## Integration with Runtime

After generating atlases:

1. Copy atlas DDS files to game assets directory
2. Copy mapping JSON to game data directory
3. Runtime will automatically load atlases and apply UV transforms

## Performance Impact

Expected improvements after atlas packing:
- **Texture binds**: -96% to -99% (from ~183,000 to ~100-200 per frame)
- **SpeedTree render time**: -50-60% (from 2-3ms to 0.8-1.2ms)
- **VRAM usage**: -30-40%
- **GPU state changes**: -90%
