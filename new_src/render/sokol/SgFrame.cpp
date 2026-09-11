#include "render/sokol/SgFrame.h"

#include <cstdio>
#include <cstring>

#include "render/sokol/SgShaders.h"

namespace newcore {

bool SgFrame::initialize() {
	sg_buffer_desc desc = {};
	desc.size = kVertexBytes;
	desc.usage.vertex_buffer = true;
	// Written exactly once per frame, before anything binds it: precisely the
	// write-transient contract (sokol_gfx.h:319-323).
	desc.usage.write_transient = true;
	desc.label = "doom2rpg-frame-vertices";
	buf_ = sg_make_buffer(&desc);
	if (sg_query_buffer_state(buf_) != SG_RESOURCESTATE_VALID) {
		std::fprintf(stderr, "sokol: frame vertex buffer creation failed\n");
		return false;
	}

	// Reserved once so no reallocation ever happens mid-frame.
	staging_.reserve(kVertexBytes);
	uniforms_.reserve(512);
	cmds_.reserve(512);
	return true;
}

void SgFrame::shutdown() {
	if (buf_.id != SG_INVALID_ID) sg_destroy_buffer(buf_);
	buf_ = sg_buffer{};
	staging_.clear();
	uniforms_.clear();
	cmds_.clear();
}

void SgFrame::beginFrame() {
	staging_.clear();
	uniforms_.clear();
	cmds_.clear();
	overflowLogged_ = false;
}

int SgFrame::appendVertices(const void* data, size_t bytes) {
	if (bytes == 0) return -1;
	if (staging_.size() + bytes > kVertexBytes) {
		// Dropping geometry is visible, which is what we want here; silence
		// would not be (ADR 0025 point 6).
		if (!overflowLogged_) {
			overflowLogged_ = true;
			std::fprintf(stderr, "sokol: frame vertex buffer overflow (%zu + %zu > %zu), "
				"dropping geometry\n", staging_.size(), bytes, kVertexBytes);
			std::fflush(stderr);
		}
		return -1;
	}
	const size_t offset = staging_.size();
	staging_.resize(offset + bytes);
	std::memcpy(staging_.data() + offset, data, bytes);
	return (int)offset;
}

int SgFrame::addUniforms(const Uniforms& u) {
	uniforms_.push_back(u);
	return (int)(uniforms_.size() - 1);
}

void SgFrame::record(const Cmd& c) {
	cmds_.push_back(c);
}

void SgFrame::replay() {
	if (!staging_.empty()) {
		sg_write_buffer_desc wd = {};
		wd.src.data.ptr = staging_.data();
		wd.src.data.size = staging_.size();
		wd.dst.buffer = buf_;
		wd.dst.offset = 0;
		wd.size = staging_.size();
		sg_write_buffer_transient(&wd);
	}

	for (const Cmd& c : cmds_) {
		switch (c.kind) {
			case Kind::Viewport:
				sg_apply_viewport(c.x, c.y, c.w, c.h, /*origin_top_left=*/true);
				break;
			case Kind::Scissor:
				sg_apply_scissor_rect(c.x, c.y, c.w, c.h, /*origin_top_left=*/true);
				break;
			case Kind::Draw: {
				if (c.vertexCount <= 0) break;
				sg_apply_pipeline(c.pip);

				sg_bindings bind = {};
				bind.vertex_buffers[0] = buf_;
				bind.vertex_buffer_offsets[0] = c.vertexOffset;
				// A program without a texture (quad_color) must get no view and
				// no sampler at all, or sokol reports an unexpected binding.
				if (c.texView.id != SG_INVALID_ID) {
					bind.views[VIEW_tex] = c.texView;
					bind.samplers[SMP_smp] = c.smp;
					if (c.palView.id != SG_INVALID_ID) {
						bind.views[VIEW_pal] = c.palView;
						bind.samplers[SMP_smp_pal] = c.palSmp;
					}
				}
				sg_apply_bindings(&bind);

				const Uniforms& u = uniforms_[(size_t)c.uniformIndex];
				if (c.world) {
					vs_world_params_t vsp = {};
					std::memcpy(vsp.mvp, u.mvp, sizeof(vsp.mvp));
					std::memcpy(vsp.view, u.view, sizeof(vsp.view));
					std::memcpy(vsp.depth_fix, u.depthFix, sizeof(vsp.depth_fix));
					fs_world_params_t fsp = {};
					std::memcpy(fsp.color_mod, u.colorMod, sizeof(fsp.color_mod));
					std::memcpy(fsp.fog_color, u.fogColor, sizeof(fsp.fog_color));
					std::memcpy(fsp.fog_params, u.fogParams, sizeof(fsp.fog_params));
					sg_range vsr = { &vsp, sizeof(vsp) };
					sg_range fsr = { &fsp, sizeof(fsp) };
					sg_apply_uniforms(UB_vs_world_params, &vsr);
					sg_apply_uniforms(UB_fs_world_params, &fsr);
				} else {
					vs2d_params_t vsp = {};
					std::memcpy(vsp.canvas_size, u.canvasSize, sizeof(vsp.canvas_size));
					fs2d_params_t fsp = {};
					std::memcpy(fsp.color_mod, u.colorMod, sizeof(fsp.color_mod));
					sg_range vsr = { &vsp, sizeof(vsp) };
					sg_range fsr = { &fsp, sizeof(fsp) };
					sg_apply_uniforms(UB_vs2d_params, &vsr);
					sg_apply_uniforms(UB_fs2d_params, &fsr);
				}

				sg_draw(0, c.vertexCount, 1);
				break;
			}
		}
	}
}

} // namespace newcore
