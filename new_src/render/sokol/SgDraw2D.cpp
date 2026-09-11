#include "render/sokol/SgDraw2D.h"

#include <cmath>

#include "render/api/QuadUV.h"
#include "render/api/RenderModes.h"
#include "render/sokol/SgFrame.h"
#include "render/sokol/SgPipelines.h"
#include "render/sokol/SgTexture.h"
#include "render/sokol/SgTextureStore.h"

namespace newcore {

static_assert(sizeof(SgDraw2D::Vertex) == 32,
	"the 2D pipelines declare a 32-byte vertex stride (spec §5.4)");

SgDraw2D::SgDraw2D() {
	vertices_.reserve(kMaxVertices);
}

bool SgDraw2D::initialize(int canvasWidth, int canvasHeight, const SgTextureStore& store,
	SgFrame& frame, SgPipelines& pipelines) {
	canvasWidth_ = canvasWidth;
	canvasHeight_ = canvasHeight;
	store_ = &store;
	frame_ = &frame;
	pipelines_ = &pipelines;
	return true;
}

void SgDraw2D::begin() {
	vertices_.clear();
	boundTex_ = nullptr;
	boundIndexed_ = false;
	renderMode_ = kRenderNormal;
	begun_ = true;
	// No frame may inherit a clip from the previous one. sg_begin_pass resets
	// the scissor to the full framebuffer (sokol_gfx.h:290-291), and begin()
	// runs before any command of this frame, so clearing the flag is enough —
	// there is no scissor command to undo.
	scissorActive_ = false;
}

void SgDraw2D::end() {
	flush();
	// Drop any clip a screen left behind, so the next frame starts clean.
	scissorActive_ = false;
	begun_ = false;
}

void SgDraw2D::setRenderMode(int renderMode) {
	const int mode = clampRenderMode(renderMode);
	if (mode == renderMode_) return;
	flush();
	renderMode_ = mode;
}

void SgDraw2D::setLetterbox(int x, int y, int w, int h) {
	if (letterbox_[0] == x && letterbox_[1] == y &&
		letterbox_[2] == w && letterbox_[3] == h) return;
	// Only matters for a live scissor: the pending quads were meant to be
	// clipped in the old frame of reference, so flush before moving it.
	if (scissorActive_) flush();
	letterbox_[0] = x;
	letterbox_[1] = y;
	letterbox_[2] = w;
	letterbox_[3] = h;
	if (scissorActive_) applyScissor();
}

void SgDraw2D::setDrawableSize(int w, int h) {
	drawableWidth_ = w;
	drawableHeight_ = h;
}

void SgDraw2D::setClipCanvas(int x, int y, int w, int h) {
	if (scissorActive_ && scissorRect_[0] == x && scissorRect_[1] == y &&
		scissorRect_[2] == w && scissorRect_[3] == h) return;
	// Same shape as setRenderMode above: quads rasterize at replay time, so the
	// already-batched ones must land under the previous clip.
	flush();
	scissorRect_[0] = x;
	scissorRect_[1] = y;
	scissorRect_[2] = w;
	scissorRect_[3] = h;
	scissorActive_ = true;
	applyScissor();
}

void SgDraw2D::clearClip() {
	if (!scissorActive_) return;
	flush();
	scissorActive_ = false;
	// There is no "disable scissor" in sokol: the whole drawable is the
	// equivalent of GlDraw2D's glDisable(GL_SCISSOR_TEST).
	SgFrame::Cmd cmd;
	cmd.kind = SgFrame::Kind::Scissor;
	cmd.x = 0;
	cmd.y = 0;
	cmd.w = drawableWidth_;
	cmd.h = drawableHeight_;
	frame_->record(cmd);
}

void SgDraw2D::applyScissor() {
	const int x = scissorRect_[0];
	const int y = scissorRect_[1];
	const int w = scissorRect_[2];
	const int h = scissorRect_[3];

	SgFrame::Cmd cmd;
	cmd.kind = SgFrame::Kind::Scissor;

	if (w <= 0 || h <= 0 || canvasWidth_ <= 0 || canvasHeight_ <= 0) {
		// Degenerate rect clips everything away (not "no clip").
		frame_->record(cmd); // x = y = w = h = 0
		return;
	}

	// Copied from GlDraw2D::applyScissor (GlDraw2D.cpp:229-239) with the y flip
	// removed: sokol takes top-left rects and flips per backend itself. The
	// lroundf is load-bearing — CanvasViewport::canvasSubRect truncates
	// instead, and a one-pixel difference in the menu scrollbar clip is a false
	// positive in the F12 diff (spec §5.6).
	const float sx = (float)letterbox_[2] / (float)canvasWidth_;
	const float sy = (float)letterbox_[3] / (float)canvasHeight_;
	cmd.x = letterbox_[0] + (int)lroundf(x * sx);
	cmd.y = letterbox_[1] + (int)lroundf(y * sy);
	cmd.w = (int)lroundf(w * sx);
	cmd.h = (int)lroundf(h * sy);
	if (cmd.w < 0) cmd.w = 0;
	if (cmd.h < 0) cmd.h = 0;
	frame_->record(cmd);
}

void SgDraw2D::emitQuad(const Vertex* v) {
	if ((int)vertices_.size() + 6 > kMaxVertices) flush();
	// Two triangles: (0,1,2) and (0,2,3) for quad TL,TR,BR,BL.
	Vertex tri[6] = { v[0], v[1], v[2], v[0], v[2], v[3] };
	vertices_.insert(vertices_.end(), tri, tri + 6);
}

void SgDraw2D::flush() {
	if (vertices_.empty()) return;

	// Both the blend FUNCTION (via the pipeline variant) and the row's `mod`
	// (via the uniform) are applied; `fog` has no meaning in the 2D path.
	// Without `mod` the modes that differ from RENDER_NORMAL only in the
	// modulation color (BLEND25/50/75, ADD25/75) would be silent no-ops.
	const RenderModeRow& row = kRenderModes[renderMode_];

	const SgProgram program = boundIndexed_ ? SgProgram::Quad2dIndexed
		: (boundTex_ ? SgProgram::Quad2dRgba : SgProgram::Quad2dColor);

	const size_t bytes = vertices_.size() * sizeof(Vertex);
	const int offset = frame_->appendVertices(vertices_.data(), bytes);
	if (offset >= 0) {
		SgFrame::Uniforms u;
		u.canvasSize[0] = (float)canvasWidth_;
		u.canvasSize[1] = (float)canvasHeight_;
		u.colorMod[0] = row.mod[0];
		u.colorMod[1] = row.mod[1];
		u.colorMod[2] = row.mod[2];
		u.colorMod[3] = row.mod[3];

		SgFrame::Cmd cmd;
		cmd.kind = SgFrame::Kind::Draw;
		cmd.pip = pipelines_->get(program, sgBlendVariant(renderMode_));
		if (boundTex_) {
			cmd.texView = boundTex_->view();
			cmd.smp = boundTex_->sampler();
			// Invalid for an RGBA texture, which is exactly what quad_rgba wants.
			cmd.palView = boundTex_->paletteView();
		}
		cmd.vertexOffset = offset;
		cmd.vertexCount = (int)vertices_.size();
		cmd.uniformIndex = frame_->addUniforms(u);
		cmd.world = false;
		frame_->record(cmd);
	}

	vertices_.clear();
	boundTex_ = nullptr;
	boundIndexed_ = false;
}

void SgDraw2D::drawQuad(TextureId texId, const SrcRect& src, const DstRect& dst,
	int rotateMode, const ColorF& color) {
	const SgTexture* tex = store_ ? store_->lookup(texId) : nullptr;
	if (!begun_ || tex == nullptr || !tex->valid()) return;

	bool indexed = (tex->format() == SgTexture::Format::Indexed);
	if (boundTex_ != tex || boundIndexed_ != indexed) {
		flush();
		boundTex_ = tex;
		boundIndexed_ = indexed;
	}

	float texW = (float)tex->width();
	float texH = (float)tex->height();
	if (texW <= 0 || texH <= 0) return;

	float u0 = (float)src.x / texW;
	float v0 = (float)src.y / texH;
	float u1 = (float)(src.x + src.w) / texW;
	float v1 = (float)(src.y + src.h) / texH;

	float hw = (float)dst.w * 0.5f;
	float hh = (float)dst.h * 0.5f;
	float cx = (float)dst.x + hw;
	float cy = (float)dst.y + hh;

	// Destination corner offsets (TL,TR,BR,BL) relative to the center, plus the
	// matching UVs: shared with every other backend (render/api/QuadUV.h).
	float pos[4][2];
	float uv[4][2];
	quadCorners(rotateMode, hw, hh, u0, v0, u1, v1, pos, uv);

	Vertex v[4];
	for (int i = 0; i < 4; ++i) {
		v[i].x = cx + pos[i][0];
		v[i].y = cy + pos[i][1];
		v[i].u = uv[i][0];
		v[i].v = uv[i][1];
		v[i].r = color.r; v[i].g = color.g; v[i].b = color.b; v[i].a = color.a;
	}
	emitQuad(v);
}

void SgDraw2D::fillQuad(const DstRect& dst, const ColorF& c) {
	if (!begun_ || dst.w <= 0 || dst.h <= 0) return;

	if (boundTex_) flush();

	const float x = (float)dst.x;
	const float y = (float)dst.y;
	Vertex v[4] = {
		{ x, y, 0, 0, c.r, c.g, c.b, c.a },
		{ x + dst.w, y, 0, 0, c.r, c.g, c.b, c.a },
		{ x + dst.w, y + dst.h, 0, 0, c.r, c.g, c.b, c.a },
		{ x, y + dst.h, 0, 0, c.r, c.g, c.b, c.a },
	};
	emitQuad(v);
}

} // namespace newcore
