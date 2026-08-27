#ifndef NEW_UI_VIEWWEAPON_H
#define NEW_UI_VIEWWEAPON_H

#include <cstdint>

namespace newcore {

class Camera3D;
class Game;
class Graphics2D;
class Hud;
class MediaLoader;
class Player;
class Tables;
class World3D;

// First-person view weapon + muzzle flash (spec 2026-08-26-decomposition
// §P1-G5; legacy Combat::drawWeapon GL path src/Combat.cpp:621-844).
class ViewWeapon {
public:
	struct Env {
		Player* player = nullptr;
		Game* game = nullptr;                 // -> combat pose/flash state
		const Tables* tables = nullptr;
		World3D* world = nullptr;
		MediaLoader* media = nullptr;
		Hud* hud = nullptr;                   // shake
		const int64_t* gameTime = nullptr;
	};

	void init(const Env& env);

	// Draws the weapon + muzzle flash. Magnification is derived from the
	// projection actually in use this frame (cam.projectionInt()), so a
	// cinematic fov cannot desync it. NO state gate inside: the caller owns
	// the "may the weapon draw" decision (spec §4).
	void draw(Graphics2D& g, const Camera3D& cam);

private:
	Env env_;
};

} // namespace newcore

#endif // NEW_UI_VIEWWEAPON_H
