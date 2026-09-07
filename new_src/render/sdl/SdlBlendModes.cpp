#include "render/sdl/SdlBlendModes.h"

#include <cstdio>

namespace newcore {

void SdlBlendModes::initialize(SDL_Renderer* renderer) {
	// A throwaway texture is the only way to ask SDL whether it accepts a
	// blend mode: SDL_SetTextureBlendMode fails on the drivers that cannot do
	// custom modes (software, D3D9).
	SDL_Texture* probe = nullptr;
	if (renderer != nullptr) {
		probe = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STATIC, 1, 1);
	}

	for (int i = 0; i < kRenderModeCount; ++i) {
		const RenderModeRow& row = kRenderModes[i];
		skip_[i] = false;

		if (row.src == BlendFactor::SrcAlpha && row.dst == BlendFactor::OneMinusSrcAlpha) {
			modes_[i] = SDL_BLENDMODE_BLEND;
			continue;
		}
		if (row.src == BlendFactor::SrcAlpha && row.dst == BlendFactor::One) {
			modes_[i] = SDL_BLENDMODE_ADD;
			continue;
		}
		if (row.src == BlendFactor::Zero && row.dst == BlendFactor::One) {
			modes_[i] = SDL_BLENDMODE_NONE;
			skip_[i] = true;
			continue;
		}
		if (row.src == BlendFactor::Zero && row.dst == BlendFactor::OneMinusSrcColor) {
			const SDL_BlendMode sub = SDL_ComposeCustomBlendMode(
				SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
				SDL_BLENDOPERATION_ADD,
				SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
				SDL_BLENDOPERATION_ADD);
			bool ok = probe != nullptr;
			if (ok && SDL_SetTextureBlendMode(probe, sub) != 0) {
				SDL_RendererInfo info;
				const char* driver = (renderer != nullptr &&
					SDL_GetRendererInfo(renderer, &info) == 0) ? info.name : "?";
				std::fprintf(stderr, "sdl render: driver '%s' refuses the subtractive "
					"custom blend mode (%s); RENDER_SUB degrades to SDL_BLENDMODE_MOD\n",
					driver, SDL_GetError());
				ok = false;
			}
			modes_[i] = ok ? sub : SDL_BLENDMODE_MOD;
			continue;
		}

		// The table has no other factor pair; a future row must be mapped
		// deliberately rather than silently blended.
		std::fprintf(stderr, "sdl render: render mode %d has no SDL blend mode, "
			"using SDL_BLENDMODE_BLEND\n", i);
		modes_[i] = SDL_BLENDMODE_BLEND;
	}

	if (probe != nullptr) SDL_DestroyTexture(probe);
}

} // namespace newcore
