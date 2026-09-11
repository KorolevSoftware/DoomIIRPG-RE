#ifndef NEW_RENDER_SOKOL_SGPIPELINES_H
#define NEW_RENDER_SOKOL_SGPIPELINES_H

#include "render/sokol/SgCommon.h"

namespace newcore {

// The four shader programs of the rewrite (spec §4): three 2D variants sharing
// one vertex shader, plus the world program.
enum class SgProgram { Quad2dIndexed, Quad2dRgba, Quad2dColor, World, Count };

// The 14-row RENDER_* table (render/api/RenderModes.cpp) has only four distinct
// (src, dst) blend pairs (spec §5.4).
enum class SgBlend { AlphaBlend, Additive, Subtractive, WriteNothing, Count };

// Which blend variant a legacy RENDER_* value maps to, derived from
// kRenderModes[mode].src/.dst so it can never drift from the shared table.
SgBlend sgBlendVariant(int renderMode);

// The 4x4 pipeline cache. Pipelines are immutable state objects, so the blend
// function is no longer mutable GPU state: every draw command names the
// pipeline it needs (ADR 0025 consequence 5).
class SgPipelines {
public:
	// colorFormat must be the swapchain's color format (RGBA8 on GL, BGRA8 on
	// Metal/D3D11) — a pipeline whose color format differs from the pass is a
	// sokol validation error.
	bool initialize(sg_pixel_format colorFormat);
	// Destroys the shaders and every created pipeline. Must run before
	// sg_shutdown().
	void shutdown();

	// Created on first use; SG_INVALID_ID only if creation failed.
	sg_pipeline get(SgProgram program, SgBlend blend);

private:
	sg_pipeline create(SgProgram program, SgBlend blend);

	sg_shader shaders_[(int)SgProgram::Count] = {};
	sg_pipeline pips_[(int)SgProgram::Count][(int)SgBlend::Count] = {};
	sg_pixel_format colorFormat_ = SG_PIXELFORMAT_NONE;
};

} // namespace newcore

#endif // NEW_RENDER_SOKOL_SGPIPELINES_H
