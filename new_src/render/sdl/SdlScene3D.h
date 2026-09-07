#ifndef NEW_RENDER_SDL_SDLSCENE3D_H
#define NEW_RENDER_SDL_SDLSCENE3D_H

#include "render/api/Scene3D.h"
#include "render/sdl/SdlCommon.h"

namespace newcore {

// Placeholder Scene3D for the SDL backend (spec group G5): the 2D layer is
// complete, the world arrives with G6. beginScene fills the current canvas
// viewport — the world band the backend has just set — with one flat color, so
// the band reads as "not implemented" rather than as a black failure; the
// triangle stream and the sky are dropped. The color is the one the world-less
// path of the game already uses (render/SceneRenderer.cpp:159).
class SdlScene3D : public Scene3D {
public:
	void initialize(SDL_Renderer* renderer) { renderer_ = renderer; }

	void beginScene(const SceneView& view) override;
	void endScene() override {}
	void setTexture(TextureId tex) override {}
	void setRenderMode(int renderMode) override {}
	void submitTriangles(const WorldVertex* verts, int count) override {}
	void setFog(bool enabled, float start, float end, const float rgba[4]) override {}
	void drawSky(TextureId sky, float uOffset) override {}
	void flush() override {}

private:
	SDL_Renderer* renderer_ = nullptr;
	bool logged_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_SDL_SDLSCENE3D_H
