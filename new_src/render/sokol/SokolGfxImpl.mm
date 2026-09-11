// The sokol_gfx implementation, Apple hosts only: on macOS and iOS it must be
// compiled as Objective-C (sokol_gfx.h:83), for the Metal backend as well as
// for the macOS GL backend. Only one of SokolGfxImpl.cpp / .mm is compiled.

#define SOKOL_IMPL
#include "render/sokol/SgCommon.h"
