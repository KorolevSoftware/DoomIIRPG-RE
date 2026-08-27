#ifndef NEW_RENDER_SCENERENDERER_H
#define NEW_RENDER_SCENERENDERER_H

#include <cstdint>
#include <vector>

#include "core/MayaCamera.h"       // MayaPose (plain value; pulls only MapData.h)
#include "render/Camera3D.h"

namespace newcore {

class Game;
class Hud;
class MapData;
class MediaLoader;
class Player;
class RenderBackend;
class Tables;
class Window;
class World3D;

// The world pass: viewport, camera (player view or cinematic pose), shake,
// sky, BSP traversal and the per-sprite sort-bias / stacked-character
// classification (spec 2026-08-26-decomposition §P1-G6). Moved verbatim out
// of GameContext::render.
class SceneRenderer {
public:
	struct Env {
		MapData* map = nullptr;
		MediaLoader* media = nullptr;
		World3D* world = nullptr;
		Game* game = nullptr;
		Player* player = nullptr;
		Hud* hud = nullptr;                   // shake
		const Tables* tables = nullptr;       // sin table
		const int64_t* upTimeMs = nullptr;    // world anim clock
	};

	void init(const Env& env);

	// One world pass. cinePose != nullptr => cinematic takeover (fov 315, or
	// 290 when a dialog runs inside it: underDialog). Falls back to the flat
	// fill when the world is not initialized.
	void drawWorld(RenderBackend& renderer, Window& window, const MayaPose* cinePose,
		bool underDialog);

	// The camera actually used this frame (the view weapon derives its
	// magnification from it).
	const Camera3D& camera() const { return camera_; }

private:
	Env env_;
	Camera3D camera_;
	// Per-frame classification buffers, refilled with assign(numSprites, 0)
	// (same values as the former per-frame vectors, no allocation per frame).
	std::vector<int> spriteSortBias_;
	std::vector<uint8_t> spriteCharClass_;
};

} // namespace newcore

#endif // NEW_RENDER_SCENERENDERER_H
