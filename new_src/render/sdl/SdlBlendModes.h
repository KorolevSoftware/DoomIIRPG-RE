#ifndef NEW_RENDER_SDL_SDLBLENDMODES_H
#define NEW_RENDER_SDL_SDLBLENDMODES_H

#include "render/api/RenderModes.h"
#include "render/sdl/SdlCommon.h"

namespace newcore {

// The legacy RENDER_* table (render/api/RenderModes.h) translated into
// SDL_BlendMode once, at init (spec §4.2.4, §5):
//   {SrcAlpha, OneMinusSrcAlpha} -> SDL_BLENDMODE_BLEND
//   {SrcAlpha, One}              -> SDL_BLENDMODE_ADD (dst += src*srcA, the
//                                   exact GL row of src/GLES.cpp:648-650)
//   {Zero, OneMinusSrcColor}     -> SDL_ComposeCustomBlendMode(...)
//   {Zero, One}                  -> nothing is drawn at all (dst = dst)
// The color modulation of a row is NOT here: it is baked into the vertex
// colors by the devices, because SDL_SetTextureColorMod is ignored by
// SDL_RenderGeometry (measured on the metal driver).
class SdlBlendModes {
public:
	// Probes every composed mode once on a throwaway 1x1 texture; a driver
	// that refuses one gets SDL_BLENDMODE_MOD plus a log naming it.
	void initialize(SDL_Renderer* renderer);

	// renderMode must be a clamped row index (clampRenderMode).
	SDL_BlendMode modeFor(int renderMode) const { return modes_[renderMode]; }

	// True for the rows whose result is dst = dst, so the batch is skipped
	// entirely instead of drawn (row 10, RENDER_NONE).
	bool skips(int renderMode) const { return skip_[renderMode]; }

private:
	SDL_BlendMode modes_[kRenderModeCount] = {};
	bool skip_[kRenderModeCount] = {};
};

} // namespace newcore

#endif // NEW_RENDER_SDL_SDLBLENDMODES_H
