#ifndef NEW_RENDER_SOKOL_SGSCENE3D_H
#define NEW_RENDER_SOKOL_SGSCENE3D_H

#include <vector>

#include "render/api/Scene3D.h"

namespace newcore {

class SgFrame;
class SgPipelines;
class SgTexture;
class SgTextureStore;

// The sokol Scene3D device: the world program (index expansion through the
// palette LUT + per-pixel linear eye-space fog + the per-mode color modulation
// of GL_MODULATE) and the sky quad, recorded into the frame command list
// (spec 2026-09-11-sokol-gfx-backend §5.7).
//
// A transliteration of GlScene3D, batch boundaries included: the boundaries are
// the draw order and the draw order is the picture. World3D hands over a
// painter-sorted triangle stream and this device never reorders it.
class SgScene3D : public Scene3D {
public:
	// Batch capacity of the host vertex vector (GlScene3D::kMaxVerts).
	static constexpr int kMaxVerts = 65536;

	SgScene3D() = default;

	SgScene3D(const SgScene3D&) = delete;
	SgScene3D& operator=(const SgScene3D&) = delete;

	// The store resolves the TextureIds handed to setTexture/drawSky; frame and
	// pipelines are owned by SgRenderBackend and outlive this device.
	bool initialize(const SgTextureStore& store, SgFrame& frame, SgPipelines& pipelines);

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
	const SgTexture* lookup(TextureId tex) const;

	const SgTextureStore* store_ = nullptr;
	SgFrame* frame_ = nullptr;
	SgPipelines* pipelines_ = nullptr;

	std::vector<WorldVertex> vertices_;

	// Matrices of the current scene, copied in beginScene: every recorded draw
	// carries its own uniform snapshot (ADR 0025).
	float mvp_[16] = {};
	float view_[16] = {};

	// Fog state (Scene3D::setFog).
	bool fogEnabled_ = false;
	float fogStart_ = 0.f;
	float fogEnd_ = 0.f;
	float fogColor_[4] = { 0.f, 0.f, 0.f, 1.f };

	// Currently bound texture so setTexture can flush on change; one SgTexture
	// is one (index view, palette view) pair, so the pointer is the exact
	// analog of GlScene3D's (currentTex_, currentPal_). Reset in beginScene.
	const SgTexture* currentTex_ = nullptr;

	// Active legacy renderMode of the batch (gles renderMode tracker,
	// src/GLES.cpp:615-622) and whether fog is on for it.
	int currentRenderMode_ = -1;
	bool currentFogOn_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_SOKOL_SGSCENE3D_H
