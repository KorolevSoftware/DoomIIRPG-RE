#ifndef NEW_RENDER_SOKOL_SGFRAME_H
#define NEW_RENDER_SOKOL_SGFRAME_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "render/sokol/SgCommon.h"

namespace newcore {

// The per-frame command list (ADR 0025). Nothing in the renderer talks to the
// GPU while the frame is being built: both devices append vertex bytes to one
// CPU staging vector and record ordered commands here. SgRenderBackend then
// does a single sg_write_buffer_transient, opens the pass, replays and commits.
//
// This is not a convenience. sg_update_buffer allows one update per frame and
// sg_write_buffer_transient only allows writes until the buffer is bound
// (sokol_gfx.h:310-331), while both devices flush dozens of times per frame.
class SgFrame {
public:
	static constexpr size_t kVertexBytes = 4u * 1024u * 1024u; // GPU + CPU staging

	// One copy per draw command: a command physically cannot be drawn under
	// another command's uniforms (ADR 0025 point 4).
	struct Uniforms {
		float mvp[16] = {};        // world only
		float view[16] = {};       // world only, eye-space depth for the fog
		float depthFix[4] = {};    // world only, clip-z remap (spec §0.4)
		float colorMod[4] = {};
		float fogColor[4] = {};
		float fogParams[4] = {};   // .x start, .y end, .z enabled
		float canvasSize[4] = {};  // 2D only
	};

	enum class Kind : uint8_t { Viewport, Scissor, Draw };

	struct Cmd {
		Kind kind = Kind::Draw;
		// Viewport / Scissor, in drawable pixels with TOP-LEFT origin.
		int x = 0, y = 0, w = 0, h = 0;
		// Draw:
		sg_pipeline pip = {};
		sg_view texView = {};      // invalid => no texture binding (quad_color)
		sg_view palView = {};      // invalid => no palette binding (quad_rgba)
		sg_sampler smp = {};
		sg_sampler palSmp = {};   // the LUT's own clamping sampler
		int vertexOffset = 0;      // BYTE offset into the frame vertex buffer
		int vertexCount = 0;
		int uniformIndex = 0;      // index into uniforms_
		bool world = false;        // which uniform-block pair to apply
	};

	// Creates the write-transient vertex buffer. Must run after sg_setup().
	bool initialize();
	// Destroys it. Must run before sg_shutdown().
	void shutdown();

	void beginFrame();

	// Copies `bytes` into the staging vector; returns the byte offset, or -1 on
	// overflow (the caller must then record no command).
	int appendVertices(const void* data, size_t bytes);
	int addUniforms(const Uniforms& u);
	void record(const Cmd& c);

	// One sg_write_buffer_transient plus a replay of every command. The only
	// code in the program that calls sg_apply_* / sg_draw. Must run inside the
	// frame's pass.
	void replay();

private:
	sg_buffer buf_ = {};
	std::vector<uint8_t> staging_;
	std::vector<Uniforms> uniforms_;
	std::vector<Cmd> cmds_;
	bool overflowLogged_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_SOKOL_SGFRAME_H
