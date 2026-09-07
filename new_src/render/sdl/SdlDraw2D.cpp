#include "render/sdl/SdlDraw2D.h"

#include <cmath>
#include <cstdio>

#include "render/api/QuadUV.h"
#include "render/api/RenderModes.h"
#include "render/sdl/SdlBlendModes.h"
#include "render/sdl/SdlTextureStore.h"

namespace newcore {

namespace {

Uint8 toByte(float v) {
	if (v <= 0.f) return 0;
	if (v >= 1.f) return 255;
	return (Uint8)lroundf(v * 255.f);
}

} // namespace

SdlDraw2D::~SdlDraw2D() {
	if (whiteTex_ != nullptr && sdlVideoAlive()) SDL_DestroyTexture(whiteTex_);
	whiteTex_ = nullptr;
}

bool SdlDraw2D::initialize(SDL_Renderer* renderer, const SdlTextureStore& store,
	const SdlBlendModes& blend) {
	renderer_ = renderer;
	store_ = &store;
	blend_ = &blend;
	if (renderer_ == nullptr) return false;

	vertices_.reserve((size_t)kMaxQuads * 4);
	indices_.reserve((size_t)kMaxQuads * 6);

	// 1x1 white texture for solid fills (spec §4.2.4): keeps fills in the same
	// SDL_RenderGeometry batch as textured quads.
	whiteTex_ = SDL_CreateTexture(renderer_, SdlTextureStore::kPixelFormat,
		SDL_TEXTUREACCESS_STATIC, 1, 1);
	if (whiteTex_ == nullptr) {
		std::fprintf(stderr, "SdlDraw2D: white texture failed: %s\n", SDL_GetError());
		return false;
	}
	const Uint32 white = 0xFFFFFFFFu;
	if (SDL_UpdateTexture(whiteTex_, nullptr, &white, 4) != 0) {
		std::fprintf(stderr, "SdlDraw2D: white texture upload failed: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetTextureScaleMode(whiteTex_, SDL_ScaleModeNearest);
	SDL_SetTextureBlendMode(whiteTex_, SDL_BLENDMODE_BLEND);
	return true;
}

void SdlDraw2D::begin() {
	vertices_.clear();
	indices_.clear();
	boundTex_ = nullptr;
	renderMode_ = kRenderNormal;
	begun_ = true;
	// No frame may inherit a clip from the previous one.
	clipActive_ = false;
	SDL_RenderSetClipRect(renderer_, nullptr);
}

void SdlDraw2D::end() {
	flush();
	// Drop any clip a screen left behind, so the next frame starts clean.
	clipActive_ = false;
	SDL_RenderSetClipRect(renderer_, nullptr);
	begun_ = false;
}

void SdlDraw2D::setRenderMode(int renderMode) {
	const int mode = clampRenderMode(renderMode);
	if (mode == renderMode_) return;
	// Vertex colors carry the row's modulation, so pending quads must be
	// rasterized under the old row.
	flush();
	renderMode_ = mode;
}

void SdlDraw2D::setClipCanvas(int x, int y, int w, int h) {
	// Quads rasterize at flush time, so the already-batched ones must land
	// under the previous clip.
	flush();
	// Canvas coords are the renderer's coordinate space (viewport + scale are
	// the letterbox), and SDL clips relative to the viewport origin.
	SDL_Rect rect;
	rect.x = x;
	rect.y = y;
	// A degenerate rect clips everything away (not "no clip").
	rect.w = w > 0 ? w : 0;
	rect.h = h > 0 ? h : 0;
	SDL_RenderSetClipRect(renderer_, &rect);
	clipActive_ = true;
}

void SdlDraw2D::clearClip() {
	if (!clipActive_) return;
	flush();
	clipActive_ = false;
	SDL_RenderSetClipRect(renderer_, nullptr);
}

SDL_Color SdlDraw2D::modulatedColor(const ColorF& color) const {
	// Both the blend function and the row's `mod` apply; `fog` has no meaning
	// in the 2D path. Without `mod` the modes that differ from RENDER_NORMAL
	// only in the modulation color (BLEND25/50/75, ADD25/75) would be silent
	// no-ops. This is the GL fragment shader's `vColor * uColorMod`.
	const RenderModeRow& row = kRenderModes[renderMode_];
	SDL_Color out;
	out.r = toByte(color.r * row.mod[0]);
	out.g = toByte(color.g * row.mod[1]);
	out.b = toByte(color.b * row.mod[2]);
	out.a = toByte(color.a * row.mod[3]);
	return out;
}

void SdlDraw2D::emitQuad(const SDL_Vertex* v) {
	if ((int)vertices_.size() + 4 > kMaxQuads * 4) {
		// flush() clears the bound texture; the quad being emitted still
		// belongs to it, so put it back.
		SDL_Texture* keep = boundTex_;
		flush();
		boundTex_ = keep;
	}
	const int base = (int)vertices_.size();
	vertices_.insert(vertices_.end(), v, v + 4);
	const int tri[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
	indices_.insert(indices_.end(), tri, tri + 6);
}

void SdlDraw2D::flush() {
	if (vertices_.empty()) {
		return;
	}
	SDL_Texture* tex = boundTex_ != nullptr ? boundTex_ : whiteTex_;
	// Row 10 (RENDER_NONE) computes dst = dst: skip the draw entirely.
	if (!blend_->skips(renderMode_)) {
		SDL_SetTextureBlendMode(tex, blend_->modeFor(renderMode_));
		if (SDL_RenderGeometry(renderer_, tex, vertices_.data(), (int)vertices_.size(),
				indices_.data(), (int)indices_.size()) != 0) {
			std::fprintf(stderr, "SdlDraw2D: SDL_RenderGeometry failed: %s\n", SDL_GetError());
		}
	}
	vertices_.clear();
	indices_.clear();
	boundTex_ = nullptr;
}

void SdlDraw2D::drawQuad(TextureId texId, const SrcRect& src, const DstRect& dst,
	int rotateMode, const ColorF& color) {
	SdlTextureStore::Entry entry;
	if (!begun_ || store_ == nullptr || !store_->lookup(texId, entry)) return;

	if (boundTex_ != entry.tex) {
		flush();
		boundTex_ = entry.tex;
	}

	const float texW = (float)entry.w;
	const float texH = (float)entry.h;
	if (texW <= 0.f || texH <= 0.f) return;

	const float u0 = (float)src.x / texW;
	const float v0 = (float)src.y / texH;
	const float u1 = (float)(src.x + src.w) / texW;
	const float v1 = (float)(src.y + src.h) / texH;

	const float hw = (float)dst.w * 0.5f;
	const float hh = (float)dst.h * 0.5f;
	const float cx = (float)dst.x + hw;
	const float cy = (float)dst.y + hh;

	// Destination corner offsets (TL,TR,BR,BL) plus the matching UVs, shared
	// with the GL backend (render/api/QuadUV.h).
	float pos[4][2];
	float uv[4][2];
	quadCorners(rotateMode, hw, hh, u0, v0, u1, v1, pos, uv);

	const SDL_Color c = modulatedColor(color);
	SDL_Vertex v[4];
	for (int i = 0; i < 4; ++i) {
		v[i].position.x = cx + pos[i][0];
		v[i].position.y = cy + pos[i][1];
		v[i].tex_coord.x = uv[i][0];
		v[i].tex_coord.y = uv[i][1];
		v[i].color = c;
	}
	emitQuad(v);
}

void SdlDraw2D::fillQuad(const DstRect& dst, const ColorF& color) {
	if (!begun_ || dst.w <= 0 || dst.h <= 0) return;

	if (boundTex_ != nullptr && boundTex_ != whiteTex_) flush();
	boundTex_ = whiteTex_;

	const float x = (float)dst.x;
	const float y = (float)dst.y;
	const float x1 = x + (float)dst.w;
	const float y1 = y + (float)dst.h;
	const SDL_Color c = modulatedColor(color);

	SDL_Vertex v[4];
	const float px[4] = { x, x1, x1, x };
	const float py[4] = { y, y, y1, y1 };
	for (int i = 0; i < 4; ++i) {
		v[i].position.x = px[i];
		v[i].position.y = py[i];
		// Any texel of the 1x1 white texture will do.
		v[i].tex_coord.x = 0.f;
		v[i].tex_coord.y = 0.f;
		v[i].color = c;
	}
	emitQuad(v);
}

} // namespace newcore
