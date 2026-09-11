// The sokol_gfx implementation, non-Apple hosts only. On macOS and iOS the
// implementation must be compiled as Objective-C (sokol_gfx.h:83), so CMake
// swaps this file for SokolGfxImpl.mm there. Nothing else belongs here.

#define SOKOL_IMPL
#include "render/sokol/SgCommon.h"
