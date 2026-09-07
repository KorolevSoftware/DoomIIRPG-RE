#include "render/sdl/SdlScene3D.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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

int floorInt(float v) {
	return (int)std::floor(v);
}

// SDL2 validates every UV of an SDL_RenderGeometry call and rejects the WHOLE
// call with "Values of 'uv' out of bounds" as soon as one leaves [0,1]
// (measured on SDL 2.32.10, metal driver). So the epsilon overshoot the tile
// split leaves behind (-1e-7, 1+1e-7) has to be clamped, and a texture whose
// UVs were not split — a non-tiled one, or a triangle past the tile-cell cap —
// gets the clamped mapping the spec asks for (§4.2.4 step 2a) rather than a
// dropped batch. GL does the same for a non-tiled texture: CLAMP_TO_EDGE.
float clampUv(float v) {
	if (v <= 0.f) return 0.f;
	if (v >= 1.f) return 1.f;
	return v;
}

WorldVertex lerpWorld(const WorldVertex& a, const WorldVertex& b, float t) {
	WorldVertex r;
	r.x = a.x + (b.x - a.x) * t;
	r.y = a.y + (b.y - a.y) * t;
	r.z = a.z + (b.z - a.z) * t;
	r.u = a.u + (b.u - a.u) * t;
	r.v = a.v + (b.v - a.v) * t;
	return r;
}

// Sutherland-Hodgman clip of a convex polygon against the object-space plane
// u = bound (axis 0) or v = bound (axis 1); keepAbove selects the >= half.
// UVs are affine in object space, so interpolating x,y,z,u,v linearly is exact
// (ADR 0021: this is what makes the tile split lossless).
int clipUv(const WorldVertex* in, int n, int axis, float bound, bool keepAbove,
	WorldVertex* out) {
	int m = 0;
	for (int i = 0; i < n; ++i) {
		const WorldVertex& a = in[i];
		const WorldVertex& b = in[(i + 1) % n];
		const float da = (axis == 0 ? a.u : a.v) - bound;
		const float db = (axis == 0 ? b.u : b.v) - bound;
		const bool insideA = keepAbove ? (da >= 0.f) : (da <= 0.f);
		const bool insideB = keepAbove ? (db >= 0.f) : (db <= 0.f);
		if (insideA) out[m++] = a;
		if (insideA != insideB) out[m++] = lerpWorld(a, b, da / (da - db));
	}
	return m;
}

} // namespace

void SdlScene3D::initialize(SDL_Renderer* renderer, const SdlTextureStore& store,
	const SdlBlendModes& blend) {
	renderer_ = renderer;
	store_ = &store;
	blend_ = &blend;
	// Tessellation adds ~1k leaf triangles per frame (spec §7); reserve enough
	// that neither vector re-grows every frame.
	vertices_.reserve(8192);
	haze_.reserve(8192);
	// Debug/measurement knob (spec §5): DOOM2RPG_SDL_TESS=1 disables the
	// affine-warp tessellation, 2..8 caps it lower than kMaxSubdivN.
	if (const char* env = std::getenv("DOOM2RPG_SDL_TESS")) {
		int n = std::atoi(env);
		if (n < 1) n = 1;
		if (n > kMaxSubdivN) n = kMaxSubdivN;
		maxSubdivN_ = n;
		std::fprintf(stderr, "SdlScene3D: DOOM2RPG_SDL_TESS=%s -> max subdivision n = %d%s\n",
			env, maxSubdivN_, maxSubdivN_ == 1 ? " (tessellation off)" : "");
	}
	updateBatchState();
}

void SdlScene3D::setViewportSize(int w, int h) {
	if (w <= 0 || h <= 0) return;
	vpW_ = (float)w;
	vpH_ = (float)h;
}

void SdlScene3D::beginScene(const SceneView& view) {
	if (view.mvp != nullptr) {
		for (int i = 0; i < 16; ++i) mvp_[i] = view.mvp[i];
	}
	if (view.view != nullptr) {
		for (int i = 0; i < 16; ++i) view_[i] = view.view[i];
	}
	vertices_.clear();
	haze_.clear();
	boundTex_ = nullptr;
	boundFlags_ = TextureFlags::None;
	renderMode_ = kRenderNormal;
	updateBatchState();
}

void SdlScene3D::endScene() {
	flush();
}

void SdlScene3D::updateBatchState() {
	const RenderModeRow& row = kRenderModes[renderMode_];
	// The row's modulation color, which the GL device applies as the
	// uColorMod uniform: SDL multiplies the texel by the vertex color, so the
	// two match (SDL_SetTextureColorMod is ignored by SDL_RenderGeometry).
	baseColor_.r = toByte(row.mod[0]);
	baseColor_.g = toByte(row.mod[1]);
	baseColor_.b = toByte(row.mod[2]);
	baseColor_.a = toByte(row.mod[3]);

	tileSplit_ = hasFlag(boundFlags_, TextureFlags::Tiled);

	// The ADD/SUB/NONE rows run with fog off (fogMode = 0,
	// src/GLES.cpp:709-715), and fog only ever exists when it is enabled.
	const bool fogOn = row.fog && fogEnabled_;
	// ADR 0022: the exact mix(fogColor, texel, f) needs a second untextured
	// pass, which is impossible over a keyed texture (it would paint the fog
	// color over the transparent texels). With a (near) black fog color
	// mix(black, c, f) == c*f exactly, so the cheap path is also the exact one.
	const bool keyed = hasFlag(boundFlags_, TextureFlags::TransparentKey);
	const bool blackFog = fogColor_[0] + fogColor_[1] + fogColor_[2] < 3.f / 255.f;
	hazeDue_ = fogOn && !keyed && !blackFog;
	fogMultiply_ = fogOn && !hazeDue_;
}

void SdlScene3D::setTexture(TextureId tex) {
	SdlTextureStore::Entry entry;
	const bool ok = store_ != nullptr && store_->lookup(tex, entry);
	SDL_Texture* t = ok ? entry.tex : nullptr;
	const TextureFlags flags = ok ? entry.flags : TextureFlags::None;
	if (!ok && !texMissLogged_) {
		texMissLogged_ = true;
		std::fprintf(stderr, "SdlScene3D: texture id %u does not resolve, its "
			"geometry is skipped\n", tex.value);
	}
	if (t == boundTex_ && flags == boundFlags_) return;
	// One SDL_RenderGeometry call per batch, so all its vertices must share
	// one texture: the pending batch belongs to the old one.
	flush();
	boundTex_ = t;
	boundFlags_ = flags;
	updateBatchState();
}

void SdlScene3D::setRenderMode(int renderMode) {
	const int mode = clampRenderMode(renderMode);
	if (mode == renderMode_) return;
	// Vertex colors carry the row's modulation (and the fog), so pending
	// triangles must rasterize under the old row.
	flush();
	renderMode_ = mode;
	updateBatchState();
}

void SdlScene3D::setFog(bool enabled, float start, float end, const float rgba[4]) {
	flush();
	fogEnabled_ = enabled;
	fogStart_ = start;
	fogEnd_ = end;
	for (int i = 0; i < 4; ++i) fogColor_[i] = rgba[i];
	updateBatchState();
}

float SdlScene3D::fogFactor(float depth) const {
	// The GL world fragment shader's factor (render/gl/GlScene3D.cpp:47),
	// evaluated per vertex instead of per pixel (ADR 0022).
	const float span = fogEnd_ - fogStart_;
	const float f = (fogEnd_ - depth) / (span > 1e-6f ? span : 1e-6f);
	if (f <= 0.f) return 0.f;
	if (f >= 1.f) return 1.f;
	return f;
}

void SdlScene3D::reserveFor(int verts) {
	if ((int)vertices_.size() + verts > kMaxVerts) flush();
}

void SdlScene3D::appendVertex(const ClipVertex& cv) {
	const float inv = 1.f / cv.w;
	SDL_Vertex out;
	// NDC -> canvas pixels of the latched viewport; y flips because the canvas
	// origin is top-left while NDC +y is up.
	out.position.x = (cv.x * inv * 0.5f + 0.5f) * vpW_;
	out.position.y = (0.5f - cv.y * inv * 0.5f) * vpH_;
	// Affine UVs: no division by w (ADR 0021 step 4).
	out.tex_coord.x = clampUv(cv.u);
	out.tex_coord.y = clampUv(cv.v);
	out.color = baseColor_;

	if (fogMultiply_ || hazeDue_) {
		const float f = fogFactor(cv.depth);
		if (fogMultiply_) {
			// RGB only, like GL fog: a keyed billboard's transparent texels
			// must stay transparent instead of fogging into a box.
			out.color.r = (Uint8)lroundf((float)out.color.r * f);
			out.color.g = (Uint8)lroundf((float)out.color.g * f);
			out.color.b = (Uint8)lroundf((float)out.color.b * f);
		} else {
			// Haze twin (ADR 0022): same position, fog color, alpha 1-f.
			SDL_Vertex hz = out;
			hz.tex_coord.x = 0.f;
			hz.tex_coord.y = 0.f;
			hz.color.r = toByte(fogColor_[0]);
			hz.color.g = toByte(fogColor_[1]);
			hz.color.b = toByte(fogColor_[2]);
			hz.color.a = toByte(1.f - f);
			haze_.push_back(hz);
		}
	}
	vertices_.push_back(out);
}

void SdlScene3D::projectTriangle(const WorldVertex tri[3]) {
	ClipVertex in[3];
	for (int i = 0; i < 3; ++i) {
		const WorldVertex& s = tri[i];
		// Column-major MVP, i.e. the GL vertex shader's uMVP * vec4(pos, 1).
		in[i].x = mvp_[0] * s.x + mvp_[4] * s.y + mvp_[8] * s.z + mvp_[12];
		in[i].y = mvp_[1] * s.x + mvp_[5] * s.y + mvp_[9] * s.z + mvp_[13];
		in[i].w = mvp_[3] * s.x + mvp_[7] * s.y + mvp_[11] * s.z + mvp_[15];
		// Eye-space depth, the GL vertex shader's -(uView * pos).z.
		in[i].depth = -(view_[2] * s.x + view_[6] * s.y + view_[10] * s.z + view_[14]);
		in[i].u = s.u;
		in[i].v = s.v;
	}

	// Near-plane clip, w > kNearW: no depth buffer means this is the only
	// clip that must happen on the CPU (the sides are clipped by the SDL
	// viewport during rasterization).
	ClipVertex out[4];
	int n = 0;
	for (int i = 0; i < 3; ++i) {
		const ClipVertex& a = in[i];
		const ClipVertex& b = in[(i + 1) % 3];
		const float da = a.w - kNearW;
		const float db = b.w - kNearW;
		if (da >= 0.f) out[n++] = a;
		if ((da >= 0.f) != (db >= 0.f)) {
			const float t = da / (da - db);
			ClipVertex& c = out[n++];
			c.x = a.x + (b.x - a.x) * t;
			c.y = a.y + (b.y - a.y) * t;
			c.w = a.w + (b.w - a.w) * t;
			c.u = a.u + (b.u - a.u) * t;
			c.v = a.v + (b.v - a.v) * t;
			c.depth = a.depth + (b.depth - a.depth) * t;
		}
	}
	if (n < 3) return;

	for (int i = 1; i + 1 < n; ++i) {
		const ClipVertex fan[3] = { out[0], out[i], out[i + 1] };
		emitClipTriangle(fan);
	}
}

SdlScene3D::ClipVertex SdlScene3D::lerpClip(const ClipVertex& a, const ClipVertex& b,
	float t) {
	ClipVertex r;
	r.x = a.x + (b.x - a.x) * t;
	r.y = a.y + (b.y - a.y) * t;
	r.w = a.w + (b.w - a.w) * t;
	r.u = a.u + (b.u - a.u) * t;
	r.v = a.v + (b.v - a.v) * t;
	r.depth = a.depth + (b.depth - a.depth) * t;
	return r;
}

void SdlScene3D::projectForMetric(const ClipVertex tri[3], float sx[3],
	float sy[3]) const {
	// Screen positions without the +0.5 offset and the y flip of appendVertex:
	// the metric only uses differences, so both cancel (spec §3), and the
	// off-screen test compensates by using the box centred on the viewport.
	for (int i = 0; i < 3; ++i) {
		const float w = tri[i].w > kNearW ? tri[i].w : kNearW; // the clip guarantees it
		const float inv = 1.f / w;
		sx[i] = tri[i].x * inv * 0.5f * vpW_;
		sy[i] = tri[i].y * inv * 0.5f * vpH_;
	}
}

bool SdlScene3D::offScreenBox(const float sx[3], const float sy[3]) const {
	float minX = sx[0], maxX = sx[0];
	float minY = sy[0], maxY = sy[0];
	for (int i = 1; i < 3; ++i) {
		if (sx[i] < minX) minX = sx[i];
		if (sx[i] > maxX) maxX = sx[i];
		if (sy[i] < minY) minY = sy[i];
		if (sy[i] > maxY) maxY = sy[i];
	}
	// projectForMetric drops the +0.5 offset and flips no y, so the visible
	// rect [0, vpW_] x [0, vpH_] becomes the symmetric box around the origin:
	// x_screen = sx + vpW_/2 and y_screen = vpH_/2 - sy.
	const float hw = 0.5f * vpW_;
	const float hh = 0.5f * vpH_;
	// Strict comparisons, so a box that only touches the border is kept. A NaN
	// coordinate (impossible after the near clip, but cheap to be sure) makes
	// every comparison false, i.e. "not off screen": the fallback is the
	// pre-cull behaviour.
	return minX > hw || maxX < -hw || minY > hh || maxY < -hh;
}

float SdlScene3D::warpMetric(const ClipVertex tri[3], const float sx[3],
	const float sy[3]) const {
	float worst = 0.f;
	for (int i = 0; i < 3; ++i) {
		const int j = (i + 1) % 3;
		// Chebyshev edge length, no sqrt, times the peak parametric error
		// |w_i - w_j| / (2 * (w_i + w_j)).
		const float dx = std::fabs(sx[i] - sx[j]);
		const float dy = std::fabs(sy[i] - sy[j]);
		const float len = dx > dy ? dx : dy;
		const float sum = tri[i].w + tri[j].w;
		if (sum <= 0.f) continue;
		const float m = 0.5f * len * std::fabs(tri[i].w - tri[j].w) / sum;
		if (m > worst) worst = m;
	}
	return worst;
}

int SdlScene3D::subdivisionSteps(const ClipVertex tri[3]) const {
	if (maxSubdivN_ <= 1) return 1;
	float sx[3], sy[3];
	projectForMetric(tri, sx, sy);
	// Off-screen cull: a triangle whose projected bounding box does not
	// intersect the output rect rasterizes to nothing at any n, so skipping
	// its split can not change a single visible pixel — this is not a
	// quality-for-speed trade. A partially visible triangle has an
	// intersecting box and is split exactly as before. Measured on map00
	// without the cull: off-screen geometry produced 31584 of 38258 leaves
	// standing still and up to 98% of them while moving.
	if (offScreenBox(sx, sy)) return 1;
	const float m = warpMetric(tri, sx, sy);
	if (m <= kWarpTolerancePx) return 1;
	// The error falls as 1/n^2 under an n-way split (ADR 0023 decision 4).
	const int n = (int)std::ceil(std::sqrt(m / kWarpTolerancePx));
	if (n >= maxSubdivN_) return maxSubdivN_;
	return n < 2 ? 2 : n;
}

void SdlScene3D::emitClipTriangle(const ClipVertex tri[3]) {
	const int n = subdivisionSteps(tri);
	if (n <= 1) {
		reserveFor(3);
		appendVertex(tri[0]);
		appendVertex(tri[1]);
		appendVertex(tri[2]);
		return;
	}
	emitSubdivided(tri, n);
}

void SdlScene3D::emitSubdivided(const ClipVertex tri[3], int n) {
	// Regular barycentric grid, row by row: row i (i = 0..n) holds i + 1
	// vertices between A + (B-A)*i/n and A + (C-A)*i/n. n*n leaves total.
	ClipVertex rowA[kMaxSubdivN + 1];
	ClipVertex rowB[kMaxSubdivN + 1];
	const float invN = 1.f / (float)n;
	rowA[0] = tri[0]; // row 0 = the apex
	for (int i = 0; i < n; ++i) {
		const float t = (float)(i + 1) * invN;
		const ClipVertex left = lerpClip(tri[0], tri[1], t);
		const ClipVertex right = lerpClip(tri[0], tri[2], t);
		const int count = i + 2; // vertices in row i+1
		for (int k = 0; k < count; ++k) {
			rowB[k] = lerpClip(left, right, (float)k / (float)(count - 1));
		}
		// reserveFor(3) per leaf, before its three vertices: a flush is only
		// safe between complete triangles (it clears vertices_ and haze_), and
		// a mid-run flush is invisible because the whole run shares one texture
		// and one render mode.
		// Up-triangles keep the input winding.
		for (int k = 0; k <= i; ++k) {
			reserveFor(3);
			appendVertex(rowA[k]);
			appendVertex(rowB[k]);
			appendVertex(rowB[k + 1]);
		}
		for (int k = 0; k < i; ++k) {
			reserveFor(3);
			appendVertex(rowA[k]);
			appendVertex(rowB[k + 1]);
			appendVertex(rowA[k + 1]);
		}
		for (int k = 0; k < count; ++k) rowA[k] = rowB[k];
	}
}

void SdlScene3D::emitScreenTriangle(const WorldVertex tri[3]) {
	reserveFor(3);
	for (int i = 0; i < 3; ++i) {
		SDL_Vertex out;
		out.position.x = tri[i].x;
		out.position.y = tri[i].y;
		out.tex_coord.x = clampUv(tri[i].u);
		out.tex_coord.y = clampUv(tri[i].v);
		out.color = baseColor_;
		vertices_.push_back(out);
	}
}

bool SdlScene3D::splitTiles(const WorldVertex tri[3]) {
	float minU = tri[0].u, maxU = tri[0].u;
	float minV = tri[0].v, maxV = tri[0].v;
	for (int i = 1; i < 3; ++i) {
		if (tri[i].u < minU) minU = tri[i].u;
		if (tri[i].u > maxU) maxU = tri[i].u;
		if (tri[i].v < minV) minV = tri[i].v;
		if (tri[i].v > maxV) maxV = tri[i].v;
	}
	// Inside one tile already: GL_REPEAT never repeats there, so there is
	// nothing to split.
	const float eps = 1e-5f;
	if (minU >= -eps && maxU <= 1.f + eps && minV >= -eps && maxV <= 1.f + eps) {
		return false;
	}

	const int u0 = floorInt(minU + eps);
	const int v0 = floorInt(minV + eps);
	int u1 = floorInt(maxU - eps);
	int v1 = floorInt(maxV - eps);
	if (u1 < u0) u1 = u0;
	if (v1 < v0) v1 = v0;
	if ((u1 - u0 + 1) * (v1 - v0 + 1) > kMaxTileCells) {
		// Spec §4.2.4 step 2a: give up beyond the cell cap. The triangle is
		// then drawn with the clamped UVs of clampUv above — the texture is
		// stretched over it instead of repeating.
		if (!tileOverflowLogged_) {
			tileOverflowLogged_ = true;
			std::fprintf(stderr, "SdlScene3D: a triangle spans %dx%d texture tiles "
				"(cap %d); its UVs are clamped instead of split\n",
				u1 - u0 + 1, v1 - v0 + 1, kMaxTileCells);
		}
		return false;
	}

	tileBuf_.clear();
	for (int iu = u0; iu <= u1; ++iu) {
		for (int iv = v0; iv <= v1; ++iv) {
			// 3 input vertices + one plane each -> at most 7 out.
			WorldVertex a[8], b[8];
			for (int i = 0; i < 3; ++i) a[i] = tri[i];
			int n = clipUv(a, 3, 0, (float)iu, true, b);
			if (n < 3) continue;
			n = clipUv(b, n, 0, (float)(iu + 1), false, a);
			if (n < 3) continue;
			n = clipUv(a, n, 1, (float)iv, true, b);
			if (n < 3) continue;
			n = clipUv(b, n, 1, (float)(iv + 1), false, a);
			if (n < 3) continue;
			// The piece lives in cell (iu, iv): shift its UVs into [0,1].
			for (int i = 0; i < n; ++i) {
				a[i].u -= (float)iu;
				a[i].v -= (float)iv;
			}
			for (int i = 1; i + 1 < n; ++i) {
				tileBuf_.push_back(a[0]);
				tileBuf_.push_back(a[i]);
				tileBuf_.push_back(a[i + 1]);
			}
		}
	}
	return true;
}

void SdlScene3D::submitTriangles(const WorldVertex* verts, int count) {
	if (verts == nullptr || count <= 0) return;
	for (int i = 0; i + 2 < count; i += 3) {
		const WorldVertex* tri = verts + i;
		if (tileSplit_ && splitTiles(tri)) {
			for (size_t j = 0; j + 2 < tileBuf_.size(); j += 3) {
				projectTriangle(&tileBuf_[j]);
			}
		} else {
			projectTriangle(tri);
		}
	}
}

void SdlScene3D::drawSky(TextureId sky, float uOffset) {
	if (renderer_ == nullptr || store_ == nullptr) return;
	SdlTextureStore::Entry entry;
	if (!store_->lookup(sky, entry)) return;

	// Independent of beginScene/endScene: whatever is pending belongs to
	// another texture and mode.
	flush();
	boundTex_ = entry.tex;
	boundFlags_ = entry.flags;
	renderMode_ = kRenderNormal;
	updateBatchState();
	// The sky band is drawn in screen space, so it has no eye-space depth to
	// fog with (spec §4.2.4 step 4).
	fogMultiply_ = false;
	hazeDue_ = false;

	// The GL device's NDC sky quad (render/gl/GlScene3D.cpp:229-249) expressed
	// in canvas pixels of the current viewport: u spans one full texture width
	// shifted by -uOffset (legacy DrawSkyMap st[0] -= viewYaw/256), v runs
	// from 0 at the top row to 1 at the bottom.
	const WorldVertex quad[4] = {
		{ 0.f,  0.f,  0.f, -0.5f - uOffset, 0.f },
		{ vpW_, 0.f,  0.f,  0.5f - uOffset, 0.f },
		{ vpW_, vpH_, 0.f,  0.5f - uOffset, 1.f },
		{ 0.f,  vpH_, 0.f, -0.5f - uOffset, 1.f },
	};
	const WorldVertex tri[6] = { quad[0], quad[1], quad[2], quad[0], quad[2], quad[3] };
	for (int i = 0; i < 6; i += 3) {
		// The sky texture is Tiled and its u leaves [0,1] at every yaw.
		if (splitTiles(tri + i)) {
			for (size_t j = 0; j + 2 < tileBuf_.size(); j += 3) {
				emitScreenTriangle(&tileBuf_[j]);
			}
		} else {
			emitScreenTriangle(tri + i);
		}
	}
	flush();
}

void SdlScene3D::flush() {
	if (vertices_.empty()) {
		haze_.clear();
		return;
	}
	// Row 10 (RENDER_NONE) computes dst = dst: skip the draw entirely.
	if (boundTex_ != nullptr && !blend_->skips(renderMode_)) {
		SDL_SetTextureBlendMode(boundTex_, blend_->modeFor(renderMode_));
		if (SDL_RenderGeometry(renderer_, boundTex_, vertices_.data(),
				(int)vertices_.size(), nullptr, 0) != 0 && !geomErrorLogged_) {
			geomErrorLogged_ = true;
			std::fprintf(stderr, "SdlScene3D: SDL_RenderGeometry failed: %s\n", SDL_GetError());
		}
		if (!haze_.empty()) {
			// Untextured geometry takes the renderer's draw blend mode, not a
			// texture's.
			SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
			if (SDL_RenderGeometry(renderer_, nullptr, haze_.data(),
					(int)haze_.size(), nullptr, 0) != 0 && !geomErrorLogged_) {
				geomErrorLogged_ = true;
				std::fprintf(stderr, "SdlScene3D: haze SDL_RenderGeometry failed: %s\n",
					SDL_GetError());
			}
		}
	}
	vertices_.clear();
	haze_.clear();
}

} // namespace newcore
