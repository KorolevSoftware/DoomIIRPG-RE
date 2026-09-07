#include "render/sdl/SdlScene3D.h"

#include <cstdio>

namespace newcore {

void SdlScene3D::beginScene(const SceneView&) {
	if (renderer_ == nullptr) return;
	if (!logged_) {
		logged_ = true;
		std::fprintf(stdout, "sdl render: 3D world not implemented yet (spec group G6); "
			"the world band is filled with a flat color\n");
		std::fflush(stdout);
	}
	SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(renderer_, 32, 32, 64, 255);
	// A null rect fills exactly the current viewport, which the backend has
	// set to the world band (measured on SDL 2.32.10; SDL_RenderClear would
	// ignore the viewport and wipe the whole window instead).
	SDL_RenderFillRect(renderer_, nullptr);
}

} // namespace newcore
