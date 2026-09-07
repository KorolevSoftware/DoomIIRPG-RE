#ifndef NEW_RENDER_SDL_SDLSCENE3D_H
#define NEW_RENDER_SDL_SDLSCENE3D_H

#include <vector>

#include "render/api/Scene3D.h"
#include "render/sdl/SdlCommon.h"

namespace newcore {

class SdlBlendModes;
class SdlTextureStore;

// The SDL_Render Scene3D device: the CPU vertex pipeline of ADR 0021 (spec
// group G6). Triangles arrive in world units, in painter's order, and are
// split per texture tile (SDL textures are clamp-only, so GL_REPEAT has to be
// done in geometry), transformed by the camera MVP, clipped against the near
// plane, projected into canvas pixels of the viewport the backend has set and
// batched into SDL_RenderGeometry calls keyed by (texture, RENDER_* mode).
//
// Accepted deviations from the GL device: SDL_Vertex has no w component, so
// texture interpolation is affine (ADR 0021: PlayStation-style warping on
// large polygons seen at a steep angle), and fog is per-vertex instead of
// per-pixel (ADR 0022).
class SdlScene3D : public Scene3D {
public:
	// Same batch budget as GlScene3D::kMaxVerts.
	static constexpr int kMaxVerts = 65536;
	// Tile cells one triangle may be split into before the split is given up
	// (spec §4.2.4 step 2a). 289 is derived, not tuned: it is a strict upper
	// bound over everything the map data can express, so the "gave up and got a
	// stretched texture" path is unreachable.
	//   - texture coordinates are stored as int8 << 6 and divided by 1024
	//     (domain/world/MapParser.cpp:288, render/World3D.cpp:317), i.e. they
	//     live in [-8, 8) and any span obeys maxU - minU <= 15.9375;
	//   - the animated lava scroll adds one common offset of up to +0.999 to
	//     all vertices of a polygon (render/World3D.cpp:302-307), widening the
	//     effective range to [-8, 8.999). The span is unchanged, but an
	//     unaligned interval may straddle one extra integer boundary;
	//   - cells per axis = floor(max) - floor(min) + 1, and a span below 16
	//     crosses at most 16 boundaries, so the worst case is 17 (e.g.
	//     min -7.5 -> floor -8, max 8.4375 -> floor 8).
	// 17x17 = 289. The spec fixed 64 as an estimate made before the data range
	// was worked out; the divergence is deliberate.
	static constexpr int kMaxTileCells = 289;
	// Near plane of the clip stage (ADR 0021 step 2).
	static constexpr float kNearW = 1.f / 1024.f;
	// Accepted residual affine texture displacement, in canvas pixels: a
	// triangle whose peak displacement is below this is emitted whole
	// (spec 2026-09-07-sdl-tessellation §3, ADR 0023). Tuned by eye, not
	// derived; halving it costs ~4x the triangles on the affected geometry.
	static constexpr float kWarpTolerancePx = 2.f;
	// Hard cap on the n of the n x n split: 64 sub-triangles. n = 8 clears a
	// metric of 128 px; beyond that the triangle straddles the eye plane and a
	// uniform split cannot help (spec §7).
	static constexpr int kMaxSubdivN = 8;

	void initialize(SDL_Renderer* renderer, const SdlTextureStore& store,
		const SdlBlendModes& blend);

	// Canvas-space size of the viewport the backend has currently set (the
	// world band 478x248 of ADR 0009, or the full canvas outside it). NDC maps
	// onto this rect; the SDL viewport + scale place it on the drawable, just
	// as glViewport does for the GL device.
	void setViewportSize(int w, int h);

	// --- Scene3D ---
	void beginScene(const SceneView& view) override;
	void endScene() override;
	void setTexture(TextureId tex) override;
	void setRenderMode(int renderMode) override;
	void submitTriangles(const WorldVertex* verts, int count) override;
	void setFog(bool enabled, float start, float end, const float rgba[4]) override;
	void drawSky(TextureId sky, float uOffset) override;
	void flush() override;

private:
	// A vertex after the MVP transform: clip coordinates plus the affine UVs
	// and the eye-space depth the fog factor comes from.
	struct ClipVertex { float x, y, w, u, v, depth; };

	// Recomputes what depends on the bound texture and the latched render mode
	// (vertex color, fog treatment, tile split).
	void updateBatchState();
	// Splits `tri` against integer u/v boundaries into tile-local triangles in
	// tileBuf_. False means "not needed" (or given up), tileBuf_ untouched.
	bool splitTiles(const WorldVertex tri[3]);
	// Transform + near clip + projection of one tile-local world triangle.
	void projectTriangle(const WorldVertex tri[3]);
	// Clip-space counterpart of lerpWorld: all six components are linear, which
	// is what makes a clip-space cut the object-space cut (ADR 0023).
	static ClipVertex lerpClip(const ClipVertex& a, const ClipVertex& b, float t);
	// Projects a near-clipped triangle into canvas pixels measured from the
	// viewport centre (no +0.5 offset, no y flip): the frame warpMetric and
	// offScreenBox work in.
	void projectForMetric(const ClipVertex tri[3], float sx[3], float sy[3]) const;
	// True when the projected bounding box misses the output rect, i.e. the
	// triangle rasterizes to nothing however finely it is split.
	bool offScreenBox(const float sx[3], const float sy[3]) const;
	// Peak affine texture displacement over the three edges of an already
	// transformed, near-clipped triangle, in canvas pixels (spec §3), from the
	// positions of projectForMetric.
	float warpMetric(const ClipVertex tri[3], const float sx[3],
		const float sy[3]) const;
	// n for the n x n barycentric split: 1 when the triangle is off screen or
	// already inside the tolerance, else ceil(sqrt(M / kWarpTolerancePx))
	// clamped to maxSubdivN_.
	int subdivisionSteps(const ClipVertex tri[3]) const;
	// One near-clipped triangle: measures, subdivides in clip space when it
	// pays, emits through appendVertex.
	void emitClipTriangle(const ClipVertex tri[3]);
	// Regular n x n barycentric split of a clip triangle (n >= 2). Splitting in
	// clip space is exactly the object-space split, because MVP is linear in
	// homogeneous coordinates (ADR 0023).
	void emitSubdivided(const ClipVertex tri[3], int n);
	// The sky path: vertices already in canvas pixels, no transform.
	void emitScreenTriangle(const WorldVertex tri[3]);
	// Appends a projected vertex (plus its haze twin when a haze pass is due).
	void appendVertex(const ClipVertex& v);
	// Flushes when `verts` more vertices would overflow the batch.
	void reserveFor(int verts);
	float fogFactor(float depth) const;

	SDL_Renderer* renderer_ = nullptr;
	const SdlTextureStore* store_ = nullptr;
	const SdlBlendModes* blend_ = nullptr;

	// Latched by beginScene: column-major, exactly as the GL device hands them
	// to glUniformMatrix4fv (Camera3D.cpp:94-102).
	float mvp_[16] = {};
	float view_[16] = {};
	float vpW_ = 480.f;
	float vpH_ = 320.f;

	bool fogEnabled_ = false;
	float fogStart_ = 0.f;
	float fogEnd_ = 0.f;
	float fogColor_[4] = { 0.f, 0.f, 0.f, 1.f };

	// One batch = one texture + one render mode. haze_ mirrors vertices_
	// position by position when fog needs the untextured second pass.
	std::vector<SDL_Vertex> vertices_;
	std::vector<SDL_Vertex> haze_;
	// Scratch for the tile split: a multiple of 3 vertices.
	std::vector<WorldVertex> tileBuf_;

	SDL_Texture* boundTex_ = nullptr;
	TextureFlags boundFlags_ = TextureFlags::None;
	int renderMode_ = 0;

	// Derived from (boundFlags_, renderMode_, fog state) by updateBatchState.
	SDL_Color baseColor_ = { 255, 255, 255, 255 };
	bool tileSplit_ = false;
	bool fogMultiply_ = false; // fog folded into the vertex color
	bool hazeDue_ = false;     // fog drawn as the second untextured pass

	// Runtime override of kMaxSubdivN, from DOOM2RPG_SDL_TESS (spec §5).
	// 1 disables tessellation entirely.
	int maxSubdivN_ = kMaxSubdivN;

	bool tileOverflowLogged_ = false;
	bool texMissLogged_ = false;
	bool geomErrorLogged_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_SDL_SDLSCENE3D_H
