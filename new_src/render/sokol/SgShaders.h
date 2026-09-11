#ifndef NEW_RENDER_SOKOL_SGSHADERS_H
#define NEW_RENDER_SOKOL_SGSHADERS_H

// The single point of contact with the sokol-shdc output (spec
// 2026-09-11-sokol-gfx-backend §2). Both headers are build-tree artifacts
// generated from render/sokol/shaders/*.glsl; they require sokol_gfx.h first,
// which SgCommon.h provides.
//
// The two headers define VIEW_tex / VIEW_pal / SMP_smp identically, so the
// repeated #defines are not a redefinition conflict.

#include "render/sokol/SgCommon.h"

#include "quad2d.glsl.h"
#include "world.glsl.h"

#endif // NEW_RENDER_SOKOL_SGSHADERS_H
