#include "render/sokol/SgScene3D.h"

#include <cstdio>
#include <cstring>

#include "render/api/RenderModes.h"
#include "render/sokol/SgFrame.h"
#include "render/sokol/SgPipelines.h"
#include "render/sokol/SgTexture.h"
#include "render/sokol/SgTextureStore.h"

namespace newcore {

static_assert(sizeof(WorldVertex) == 20,
	"the world pipeline declares a 20-byte vertex stride (spec §5.4)");

namespace {

// Camera3D emits a GL-style projection (clip -w <= z <= w) while Metal and
// D3D11 clip 0 <= z <= w; the world vertex shader remaps clip z with this pair
// (spec §0.4). On GL it must be the exact identity, or the byte-for-byte diff
// against tests/golden/frames would drown in rounding noise.
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
constexpr float kDepthFixX = 1.0f;
constexpr float kDepthFixY = 0.0f;
#else
constexpr float kDepthFixX = 0.5f;
constexpr float kDepthFixY = 0.5f;
#endif

const float kIdentity4x4[16] = {
	1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1,
};

} // namespace

bool SgScene3D::initialize(const SgTextureStore& store, SgFrame& frame,
	SgPipelines& pipelines) {
	store_ = &store;
	frame_ = &frame;
	pipelines_ = &pipelines;
	vertices_.reserve(kMaxVerts);
	return true;
}

const SgTexture* SgScene3D::lookup(TextureId tex) const {
	return store_ != nullptr ? store_->lookup(tex) : nullptr;
}

void SgScene3D::beginScene(const SceneView& view) {
	// No GPU state is touched here at all: the pipeline carries the blend
	// function, there is no depth buffer to disable (spec §0.3) and every draw
	// command brings its own uniforms. The GL device's glDisable/glBlendFunc
	// preamble (GlScene3D.cpp:105-108) has no sokol counterpart.
	std::memcpy(mvp_, view.mvp, sizeof(mvp_));
	std::memcpy(view_, view.view, sizeof(view_));

	vertices_.clear();
	currentTex_ = nullptr;
	// GlScene3D sets the identity modulation + the global fog flag on the
	// program here and syncs the trackers to match (setRenderMode early-outs
	// while they do); the trackers are what the recorded uniforms read.
	currentRenderMode_ = kRenderNormal;
	currentFogOn_ = fogEnabled_;
}

void SgScene3D::endScene() {
	flush();
	// Nothing to restore: a leftover ADD/SUB blend equation cannot exist when
	// the equation lives in the pipeline of each draw command. The tracker
	// still goes invalid, exactly as in GlScene3D::endScene.
	currentRenderMode_ = -1;
}

void SgScene3D::flush() {
	if (vertices_.empty()) return;

	// A batch only ever accumulates inside a scene, where the mode is valid;
	// the clamp keeps the table access in range after endScene set -1.
	const int mode = (currentRenderMode_ >= 0 && currentRenderMode_ < kRenderModeCount)
		? currentRenderMode_ : kRenderNormal;
	const RenderModeRow& row = kRenderModes[mode];

	if (currentTex_ == nullptr || !currentTex_->valid()) {
		// GL binds texture object 0 here and samples black; sokol has no such
		// thing, and fs_world declares both tex and pal, so a draw without
		// them is a validation error. World3D always sets a texture (it falls
		// back to a white 1x1), so this is a loud "should not happen".
		static bool logged = false;
		if (!logged) {
			logged = true;
			std::fprintf(stderr, "sokol: world batch with no texture, %zu vertices dropped\n",
				vertices_.size());
			std::fflush(stderr);
		}
		vertices_.clear();
		return;
	}

	const size_t bytes = vertices_.size() * sizeof(WorldVertex);
	const int offset = frame_->appendVertices(vertices_.data(), bytes);
	if (offset >= 0) {
		SgFrame::Uniforms u;
		std::memcpy(u.mvp, mvp_, sizeof(u.mvp));
		std::memcpy(u.view, view_, sizeof(u.view));
		u.depthFix[0] = kDepthFixX;
		u.depthFix[1] = kDepthFixY;
		for (int i = 0; i < 4; ++i) u.colorMod[i] = row.mod[i];
		for (int i = 0; i < 4; ++i) u.fogColor[i] = fogColor_[i];
		u.fogParams[0] = fogStart_;
		u.fogParams[1] = fogEnd_;
		u.fogParams[2] = currentFogOn_ ? 1.f : 0.f;

		SgFrame::Cmd cmd;
		cmd.kind = SgFrame::Kind::Draw;
		cmd.pip = pipelines_->get(SgProgram::World, sgBlendVariant(mode));
		cmd.texView = currentTex_->view();
		cmd.palView = currentTex_->paletteView();
		cmd.smp = currentTex_->sampler();
		cmd.palSmp = currentTex_->paletteSampler();
		cmd.vertexOffset = offset;
		cmd.vertexCount = (int)vertices_.size();
		cmd.uniformIndex = frame_->addUniforms(u);
		cmd.world = true;
		frame_->record(cmd);
	}

	vertices_.clear();
}

void SgScene3D::setTexture(TextureId tex) {
	const SgTexture* st = lookup(tex);
	// All vertices of a batch share one texture, so a change flushes first.
	if (currentTex_ == st) return;
	flush();
	currentTex_ = st;
}

// Legacy gles::SetupTexture renderMode switch (src/GLES.cpp:615-715), a
// kRenderModes lookup: the blend equation, the color modulation factor and the
// fog toggle change per sprite mode; a pending batch is flushed first so its
// vertices draw under their own pipeline and uniforms.
void SgScene3D::setRenderMode(int renderMode) {
	const int mode = clampRenderMode(renderMode);
	const RenderModeRow& bm = kRenderModes[mode];
	// The ADD/SUB/NONE rows run with fog off (fogMode = 0, src/GLES.cpp:709-715);
	// fog is only ever enabled globally when fogEnabled_ is set.
	const bool fogOn = bm.fog && fogEnabled_;
	// Compare the clamped mode so an out-of-range value cannot defeat the cache.
	if (mode == currentRenderMode_ && fogOn == currentFogOn_) return;
	flush();
	currentRenderMode_ = mode;
	currentFogOn_ = fogOn;
}

void SgScene3D::submitTriangles(const WorldVertex* verts, int count) {
	if (verts == nullptr || count <= 0) return;
	if ((int)vertices_.size() + count > kMaxVerts) flush();
	vertices_.insert(vertices_.end(), verts, verts + count);
}

void SgScene3D::setFog(bool enabled, float start, float end, const float rgba[4]) {
	fogEnabled_ = enabled;
	fogStart_ = start;
	fogEnd_ = end;
	for (int i = 0; i < 4; ++i) fogColor_[i] = rgba[i];
}

void SgScene3D::drawSky(TextureId sky, float uOffset) {
	const SgTexture* st = lookup(sky);
	if (st == nullptr || !st->valid()) return;

	// Legacy DrawSkyMap quad in NDC (xyzw after divide): corners +-1.
	// st = (ndc.x*0.5, ndc.y*-0.5 + 0.5) then st[0] -= viewYaw/256.
	// Verbatim from GlScene3D::drawSky (GlScene3D.cpp:232-249).
	const float yawShift = uOffset;
	struct SkyV { float x, y, z, u, v; };
	SkyV q[4] = {
		{ -1.f, -1.f, 0.f, -0.5f - yawShift, 1.f },
		{  1.f, -1.f, 0.f,  0.5f - yawShift, 1.f },
		{  1.f,  1.f, 0.f,  0.5f - yawShift, 0.f },
		{ -1.f,  1.f, 0.f, -0.5f - yawShift, 0.f },
	};
	WorldVertex tri[6] = {
		{ q[0].x, q[0].y, q[0].z, q[0].u, q[0].v },
		{ q[1].x, q[1].y, q[1].z, q[1].u, q[1].v },
		{ q[2].x, q[2].y, q[2].z, q[2].u, q[2].v },
		{ q[0].x, q[0].y, q[0].z, q[0].u, q[0].v },
		{ q[2].x, q[2].y, q[2].z, q[2].u, q[2].v },
		{ q[3].x, q[3].y, q[3].z, q[3].u, q[3].v },
	};

	const int offset = frame_->appendVertices(tri, sizeof(tri));
	if (offset < 0) return;

	SgFrame::Uniforms u;
	// Identity MVP: the sky quad is already in clip space. The identity view
	// makes fog_depth meaningless, which is why the sky is drawn with fog off —
	// GlScene3D leaves the previous scene's uView in the program instead, and
	// that is equally meaningless; with fog off neither is ever read.
	std::memcpy(u.mvp, kIdentity4x4, sizeof(u.mvp));
	std::memcpy(u.view, kIdentity4x4, sizeof(u.view));
	u.depthFix[0] = kDepthFixX;
	u.depthFix[1] = kDepthFixY;
	// drawSky runs outside beginScene/endScene: without this reset a modulated
	// sprite mode from the previous frame (e.g. ADD50) would darken the sky
	// (ADR 0019 point 4).
	u.colorMod[0] = 1.f;
	u.colorMod[1] = 1.f;
	u.colorMod[2] = 1.f;
	u.colorMod[3] = 1.f;

	SgFrame::Cmd cmd;
	cmd.kind = SgFrame::Kind::Draw;
	cmd.pip = pipelines_->get(SgProgram::World, SgBlend::AlphaBlend);
	cmd.texView = st->view();
	cmd.palView = st->paletteView();
	cmd.smp = st->sampler();
	cmd.palSmp = st->paletteSampler();
	cmd.vertexOffset = offset;
	cmd.vertexCount = 6;
	cmd.uniformIndex = frame_->addUniforms(u);
	cmd.world = true;
	frame_->record(cmd);
}

} // namespace newcore
