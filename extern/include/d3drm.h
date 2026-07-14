#pragma once

// Legacy DirectShow/qedit dependency shim.
// DX11 runtime does not use D3DRM; this header exists only to satisfy
// old SDK include chains (dxtrans.h -> d3drm.h) during migration.
#include "directx9_old_notused/d3drm.h"
