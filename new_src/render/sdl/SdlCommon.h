#ifndef NEW_RENDER_SDL_SDLCOMMON_H
#define NEW_RENDER_SDL_SDLCOMMON_H

#include <SDL.h>

namespace newcore {

// The store and the 2D device deliberately outlive the window: AppContext's
// shutdown() destroys the Window (SDL_DestroyRenderer + SDL_Quit) while the
// backend and the Texture handles owned by main() die later, at singleton
// teardown (core/AppContext.cpp:87-95). Every SDL_Texture of a destroyed
// renderer is already gone at that point, so releasing it again would touch
// freed memory. SDL_Quit clears the subsystem flags, which is the one signal
// available after the fact.
inline bool sdlVideoAlive() {
	return SDL_WasInit(SDL_INIT_VIDEO) != 0;
}

} // namespace newcore

#endif // NEW_RENDER_SDL_SDLCOMMON_H
