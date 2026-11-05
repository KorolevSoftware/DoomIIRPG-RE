#ifndef NEW_DOMAIN_GAME_FOGSTATE_H
#define NEW_DOMAIN_GAME_FOGSTATE_H

#include <cstdint>

namespace newcore {

// Legacy depth-fog state: tinyGL->fogColor/fogMin/fogRange + Render's lerp
// fields (src/TinyGL.cpp:34-36, src/Render.cpp:1801-1820). Owned by Game::fog;
// written by EV_SET_FOG_COLOR / EV_LERP_FOG, ticked + read by SceneRenderer.
struct FogState {
	int color = 0;        // ARGB; alpha 0 = fog off
	int min = 32752;
	int range = 1;
	int baseMin = 0, baseRange = 0, destMin = 0, destRange = 0;
	int64_t lerpStart = 0;
	int lerpTime = 0;     // ms, 0 = no lerp running

	void buildFogTables(int argb);                      // src/Render.cpp:1852-1861 (state part)
	void startLerp(int n, int n2, int ms, int64_t now); // src/Render.cpp:1801-1820
	void tick(int64_t now);                             // src/Render.cpp:2300-2314
	void unloadMap() { startLerp(32752, 32752, 0, 0); } // src/Render.cpp:214
#ifndef NDEBUG
	static void selfCheck();                            // asserts, Debug boot
#endif
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_FOGSTATE_H
