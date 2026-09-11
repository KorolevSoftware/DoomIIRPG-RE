#ifndef NEW_RENDER_SOKOL_SGCOMMON_H
#define NEW_RENDER_SOKOL_SGCOMMON_H

// The only header in the project that includes sokol_gfx.h. Declarations only:
// SOKOL_IMPL lives in SokolGfxImpl.cpp / SokolGfxImpl.mm and nowhere else
// (spec 2026-09-11-sokol-gfx-backend §2).

#if defined(SOKOL_GLCORE) + defined(SOKOL_METAL) + defined(SOKOL_D3D11) != 1
	#error "exactly one of SOKOL_GLCORE / SOKOL_METAL / SOKOL_D3D11 must be defined (see DOOM2RPG_SOKOL_BACKEND)"
#endif

#include <sokol_gfx.h>

#endif // NEW_RENDER_SOKOL_SGCOMMON_H
