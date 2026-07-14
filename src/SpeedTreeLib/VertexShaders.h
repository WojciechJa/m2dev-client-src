#pragma once

// DX11-native SpeedTree rendering uses the shader and vertex-layout
// definitions in `SpeedTreeForestDirectX.cpp`.
//
// The old Direct3D 9 FVF / vertex-declaration helper layer was removed from
// the active runtime path during the DX11 cutover. This header remains as a
// minimal marker so include graphs can be simplified without reintroducing
// DX9 symbols.
