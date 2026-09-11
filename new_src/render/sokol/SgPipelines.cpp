#include "render/sokol/SgPipelines.h"

#include <cstdio>

#include "render/api/RenderModes.h"
#include "render/sokol/SgShaders.h"

namespace newcore {

namespace {

// Per-variant blend state (spec §5.4). For three of the four variants the
// alpha factors equal the RGB factors, which is what glBlendFunc means.
// Subtractive is the one deviation: RGB keeps (ZERO, ONE_MINUS_SRC_COLOR) but
// alpha becomes (ZERO, ONE_MINUS_SRC_ALPHA), because D3D11 rejects a
// color-typed factor in the alpha slot. Nothing ever reads the swapchain's
// destination alpha, so the visible RGB result is bit-identical.
sg_blend_state blendStateOf(SgBlend variant) {
	sg_blend_state bs = {};
	bs.enabled = true;
	bs.op_rgb = SG_BLENDOP_ADD;
	bs.op_alpha = SG_BLENDOP_ADD;
	switch (variant) {
		case SgBlend::AlphaBlend:
			bs.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
			bs.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
			bs.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
			bs.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
			break;
		case SgBlend::Additive:
			bs.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
			bs.dst_factor_rgb = SG_BLENDFACTOR_ONE;
			bs.src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA;
			bs.dst_factor_alpha = SG_BLENDFACTOR_ONE;
			break;
		case SgBlend::Subtractive:
			bs.src_factor_rgb = SG_BLENDFACTOR_ZERO;
			bs.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
			bs.src_factor_alpha = SG_BLENDFACTOR_ZERO;
			bs.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
			break;
		case SgBlend::WriteNothing:
			bs.src_factor_rgb = SG_BLENDFACTOR_ZERO;
			bs.dst_factor_rgb = SG_BLENDFACTOR_ONE;
			bs.src_factor_alpha = SG_BLENDFACTOR_ZERO;
			bs.dst_factor_alpha = SG_BLENDFACTOR_ONE;
			break;
		case SgBlend::Count:
			break;
	}
	return bs;
}

const sg_shader_desc* shaderDescOf(SgProgram program) {
	const sg_backend backend = sg_query_backend();
	switch (program) {
		case SgProgram::Quad2dIndexed: return quad_indexed_shader_desc(backend);
		case SgProgram::Quad2dRgba:    return quad_rgba_shader_desc(backend);
		case SgProgram::Quad2dColor:   return quad_color_shader_desc(backend);
		case SgProgram::World:         return world_shader_desc(backend);
		case SgProgram::Count:         break;
	}
	return nullptr;
}

const char* labelOf(SgProgram program, SgBlend blend) {
	static const char* kNames[(int)SgProgram::Count][(int)SgBlend::Count] = {
		{ "pip-indexed-blend", "pip-indexed-add", "pip-indexed-sub", "pip-indexed-none" },
		{ "pip-rgba-blend",    "pip-rgba-add",    "pip-rgba-sub",    "pip-rgba-none" },
		{ "pip-color-blend",   "pip-color-add",   "pip-color-sub",   "pip-color-none" },
		{ "pip-world-blend",   "pip-world-add",   "pip-world-sub",   "pip-world-none" },
	};
	return kNames[(int)program][(int)blend];
}

} // namespace

SgBlend sgBlendVariant(int renderMode) {
	if (renderMode < 0 || renderMode >= kRenderModeCount) renderMode = kRenderNormal;
	const RenderModeRow& row = kRenderModes[renderMode];
	if (row.src == BlendFactor::SrcAlpha && row.dst == BlendFactor::OneMinusSrcAlpha)
		return SgBlend::AlphaBlend;
	if (row.src == BlendFactor::SrcAlpha && row.dst == BlendFactor::One)
		return SgBlend::Additive;
	if (row.src == BlendFactor::Zero && row.dst == BlendFactor::OneMinusSrcColor)
		return SgBlend::Subtractive;
	if (row.src == BlendFactor::Zero && row.dst == BlendFactor::One)
		return SgBlend::WriteNothing;
	// A fifth (src, dst) pair would need a fifth pipeline variant above.
	static bool logged = false;
	if (!logged) {
		logged = true;
		std::fprintf(stderr, "sokol: render mode %d has no pipeline blend variant, "
			"drawing it as alpha blend\n", renderMode);
		std::fflush(stderr);
	}
	return SgBlend::AlphaBlend;
}

bool SgPipelines::initialize(sg_pixel_format colorFormat) {
	colorFormat_ = colorFormat;
	for (int p = 0; p < (int)SgProgram::Count; ++p) {
		const sg_shader_desc* desc = shaderDescOf((SgProgram)p);
		if (desc == nullptr) return false;
		shaders_[p] = sg_make_shader(desc);
		if (sg_query_shader_state(shaders_[p]) != SG_RESOURCESTATE_VALID) {
			std::fprintf(stderr, "sokol: sg_make_shader failed for program %d\n", p);
			return false;
		}
	}
	return true;
}

void SgPipelines::shutdown() {
	for (int p = 0; p < (int)SgProgram::Count; ++p) {
		for (int b = 0; b < (int)SgBlend::Count; ++b) {
			if (pips_[p][b].id != SG_INVALID_ID) sg_destroy_pipeline(pips_[p][b]);
			pips_[p][b] = sg_pipeline{};
		}
		if (shaders_[p].id != SG_INVALID_ID) sg_destroy_shader(shaders_[p]);
		shaders_[p] = sg_shader{};
	}
}

sg_pipeline SgPipelines::create(SgProgram program, SgBlend blend) {
	sg_pipeline_desc desc = {};
	desc.shader = shaders_[(int)program];
	desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
	desc.index_type = SG_INDEXTYPE_NONE;
	desc.cull_mode = SG_CULLMODE_NONE;   // src/GLES.cpp:89, no culling ever
	desc.sample_count = 1;
	// No depth buffer exists anywhere in this renderer: it is painter's-order
	// (spec §0.3). Do not "helpfully" add one back.
	desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
	desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
	desc.depth.write_enabled = false;
	desc.color_count = 1;
	desc.colors[0].pixel_format = colorFormat_;
	desc.colors[0].write_mask = SG_COLORMASK_RGBA;
	desc.colors[0].blend = blendStateOf(blend);
	desc.label = labelOf(program, blend);

	if (program == SgProgram::World) {
		// newcore::WorldVertex: vec3 pos + vec2 uv0, 20 bytes.
		desc.layout.buffers[0].stride = 20;
		desc.layout.attrs[ATTR_world_pos].offset = 0;
		desc.layout.attrs[ATTR_world_pos].format = SG_VERTEXFORMAT_FLOAT3;
		desc.layout.attrs[ATTR_world_uv0].offset = 12;
		desc.layout.attrs[ATTR_world_uv0].format = SG_VERTEXFORMAT_FLOAT2;
	} else {
		// SgDraw2D::Vertex: vec2 pos + vec2 uv0 + vec4 color0, 32 bytes.
		// The three 2D programs share vs2d, so ATTR_quad_indexed_* ==
		// ATTR_quad_rgba_* == ATTR_quad_color_* (0, 1, 2) in quad2d.glsl.h.
		desc.layout.buffers[0].stride = 32;
		desc.layout.attrs[ATTR_quad_indexed_pos].offset = 0;
		desc.layout.attrs[ATTR_quad_indexed_pos].format = SG_VERTEXFORMAT_FLOAT2;
		desc.layout.attrs[ATTR_quad_indexed_uv0].offset = 8;
		desc.layout.attrs[ATTR_quad_indexed_uv0].format = SG_VERTEXFORMAT_FLOAT2;
		desc.layout.attrs[ATTR_quad_indexed_color0].offset = 16;
		desc.layout.attrs[ATTR_quad_indexed_color0].format = SG_VERTEXFORMAT_FLOAT4;
	}

	const sg_pipeline pip = sg_make_pipeline(&desc);
	if (sg_query_pipeline_state(pip) != SG_RESOURCESTATE_VALID) {
		std::fprintf(stderr, "sokol: sg_make_pipeline failed for %s\n", desc.label);
		sg_destroy_pipeline(pip);
		return sg_pipeline{};
	}
	return pip;
}

sg_pipeline SgPipelines::get(SgProgram program, SgBlend blend) {
	sg_pipeline& slot = pips_[(int)program][(int)blend];
	if (slot.id == SG_INVALID_ID) slot = create(program, blend);
	return slot;
}

} // namespace newcore
