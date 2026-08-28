#ifndef NEW_UI_VIEWWEAPON_H
#define NEW_UI_VIEWWEAPON_H

namespace newcore {

class Camera3D;
class Graphics2D;
class MediaLoader;
class World3D;

// Per-frame view model for the view weapon (ADR 0012 point 4, spec
// 2026-08-27-ui-layer §6): rebuilt from scratch by
// GameContext::buildViewWeaponModel() every frame, never fed by setters. It
// carries everything the draw call used to read out of Player / Game::combat /
// Tables, which is what removes the last domain/game includes from ui/.
struct ViewWeaponModel {
	// False for the two former early returns: no weapon / no weapons owned
	// (src/Combat.cpp:677,706-708) and a missing wpinfo row.
	bool visible = false;
	// Kept only for the per-weapon scrY bias (src/Combat.cpp:679-693), which
	// stays in the view together with the (196,131) anchors.
	int weapon = -1;
	// Sprite tiles resolved by the producer (Combat::getWeaponTileNum,
	// src/Combat.cpp:1766-1784): the gun art tile and the muzzle-flash tile
	// (always tile of weapon 0, src/Combat.cpp:826-834).
	int weaponTile = -1;
	int flashTile = -1;
	// Resolved pose: idle, held attack, or the SHOTHOLD return lerp
	// (src/Combat.cpp:735-767).
	int poseX = 0;
	int poseY = 0;
	// Both legacy flash gates already applied (src/Combat.cpp:741 and :826).
	bool muzzleFlash = false;
	int flashX = 0;
	int flashY = 0;
	// Canvas shake. shakeY is ALREADY the negated magnitude -|shakeY|, the way
	// legacy drawWeapon negates it before use (src/Combat.cpp:709), so the
	// view keeps its verbatim `scrY - (poseY + shakeY)`.
	int shakeX = 0;
	int shakeY = 0;
};

// First-person view weapon + muzzle flash (spec 2026-08-26-decomposition
// §P1-G5; legacy Combat::drawWeapon GL path src/Combat.cpp:621-844).
class ViewWeapon {
public:
	struct Env {
		World3D* world = nullptr;             // sprite texture lookups
		MediaLoader* media = nullptr;
	};

	void init(const Env& env);

	// Draws the weapon + muzzle flash from the frame's model. Magnification is
	// derived from the projection actually in use this frame
	// (cam.projectionInt()), so a cinematic fov cannot desync it. NO state gate
	// inside: the caller owns the "may the weapon draw" decision (spec §4).
	void draw(Graphics2D& g, const Camera3D& cam, const ViewWeaponModel& m);

private:
	Env env_;
};

} // namespace newcore

#endif // NEW_UI_VIEWWEAPON_H
