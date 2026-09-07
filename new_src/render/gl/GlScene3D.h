#ifndef NEW_RENDER_GL_GLSCENE3D_H
#define NEW_RENDER_GL_GLSCENE3D_H

#include <vector>

#include "render/api/Scene3D.h"
#include "render/gl/GlCommon.h"
#include "render/gl/Shader.h"

namespace newcore {

class GlTexture;
class GlTextureStore;

// The GL Scene3D device: the world shader (index expansion through the palette
// LUT + linear eye-space fog + the per-mode color modulation of GL_MODULATE),
// one dynamic vertex buffer for the painter-ordered triangle stream, and the
// sky quad. Moved here from render/World3D.cpp by spec group G3; World3D is
// geometry-only and never sees GL (ADR 0020).
class GlScene3D : public Scene3D {
public:
	// Batch capacity of the vertex buffer (was World3D::kMaxVerts).
	static constexpr int kMaxVerts = 65536;

	GlScene3D() = default;
	~GlScene3D() override;

	GlScene3D(const GlScene3D&) = delete;
	GlScene3D& operator=(const GlScene3D&) = delete;

	// Compiles the world shader and creates the VAO/VBO (needs a current GL
	// context). The store resolves the TextureIds handed to setTexture/drawSky.
	bool initialize(const GlTextureStore& store);

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
	const GlTexture* lookup(TextureId tex) const;
	// Binds the texture's index + palette objects to units 0/1 (0 for none).
	static void bindUnits(const GlTexture* gt);

	Shader shader_;
	const GlTextureStore* store_ = nullptr;

	GLuint vao_ = 0;
	GLuint vbo_ = 0;

	std::vector<WorldVertex> vertices_;

	// Fog state (uniforms set in beginScene).
	bool fogEnabled_ = false;
	float fogStart_ = 0.f;
	float fogEnd_ = 0.f;
	float fogColor_[4] = { 0.f, 0.f, 0.f, 1.f };

	// Currently bound texture (index + palette) so setTexture can flush on
	// change; both reset in beginScene.
	GLuint currentTex_ = 0;
	GLuint currentPal_ = 0;

	// Active legacy renderMode of the batch (gles renderMode tracker,
	// src/GLES.cpp:615-622) and whether the fog uniform is currently on.
	int currentRenderMode_ = -1;
	bool currentFogOn_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_GL_GLSCENE3D_H
