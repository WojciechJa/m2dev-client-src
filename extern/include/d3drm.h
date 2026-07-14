#pragma once

// DirectShow's legacy dxtrans.h still includes d3drm.h, but the classic DX11
// client does not use retained-mode Direct3D.  Keep the SDK include chain
// satisfied without depending on the removed d3drmobj.h header.
#ifndef __D3DRM_H__
#define __D3DRM_H__
#endif
