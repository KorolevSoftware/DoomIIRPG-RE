#include "render/SceneRenderer.h"

#include <vector>

#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Media.h"
#include "io/Tables.h"
#include "render/Graphics2D.h"
#include "render/RenderBackend.h"
#include "render/World3D.h"
#include "ui/Hud.h"

namespace newcore {

void SceneRenderer::init(const Env& env) {
	env_ = env;
	if (env_.tables) {
		camera_.setSinTable(env_.tables->sinTable.data());
	}
}

// Floater family (src/Render.cpp:3023-3025): Sentinel/Lost Soul/Cacodemon.
// Legacy diverts these to renderFloaterAnim before the shared anim switch
// (src/Render.cpp:3157-3161); drawCharacter has no counterpart, so they stay
// on the billboard path (ADR 0007).
static bool isFloaterTile(int n) {
	return (n >= Enums::TILENUM_MONSTER_SENTINEL && n <= Enums::TILENUM_MONSTER_SENTINEL3) ||
	       (n >= Enums::TILENUM_MONSTER_LOST_SOUL && n <= Enums::TILENUM_MONSTER_LOST_SOUL3) ||
	       (n >= Enums::TILENUM_MONSTER_CACODEMON && n <= Enums::TILENUM_MONSTER_CACODEMON3);
}

// Special-boss family (src/Render.cpp:3027-3029): Mastermind/Arachnotron/
// Boss Pinky/VIOS, diverted to renderSpecialBossAnim (src/Render.cpp:3162-3166).
static bool isSpecialBossTile(int n) {
	return n == Enums::TILENUM_BOSS_MASTERMIND || n == Enums::TILENUM_MONSTER_ARACHNOTRON ||
	       n == Enums::TILENUM_BOSS_PINKY ||
	       (n >= Enums::TILENUM_BOSS_VIOS && n <= Enums::TILENUM_BOSS_VIOS5);
}

void SceneRenderer::drawWorld(RenderBackend& renderer, Window& window,
                              const MayaPose* cinePose, bool underDialog) {
	env_.world->setTime((int)*env_.upTimeMs);

	// World band, used by BOTH gameplay and cinematics: the GL path snaps the
	// viewport to glViewport(1, 65, 478, 248) = canvas rect (1, 7, 478, 248),
	// centre (240,131), whatever y the raster rect carried
	// (src/GLES.cpp:119-127 discards it, src/TinyGL.cpp:149-167;
	// rendering.md §6.1, ADR 0009). The cinematic letterbox is not a viewport
	// change either: it is two opaque black fills painted over the finished
	// world band (src/Hud.cpp:455-456, see the StateId::Camera block in
	// GameContext::render).
	static constexpr int kWorldRect[4] = { 1, 7, 478, 248 };

	// Screen shake offsets (src/Render.cpp:2265-2270): lateral offset along
	// the view right vector + vertical offset, canvas units -> <<4 render
	// units. Applied to whichever view renders this frame — player OR maya
	// camera, both go through legacy Render::render (:2208).
	const std::vector<int32_t>& sinTable = env_.tables->sinTable;
	int shakeX = env_.hud->shakeX();
	int shakeY = env_.hud->shakeY();

	// Camera from the player view + render pull-back (src/Render.cpp:2279-
	// 2282; magnitude <= 2.5 map units). Gameplay keeps using player view coords.
	if (cinePose != nullptr) {
		// Cinematic takeover (legacy MayaCamera::Render, src/MayaCamera.cpp:
		// 302-310): the maya pose IS the view; FOV 315 (290 under a dialog).
		const MayaPose& mp = *cinePose;
		int cyaw = mp.yaw & 0x3FF;
		int msin = sinTable[cyaw];
		int mcos = sinTable[(cyaw + 256) & 0x3FF];
		int mx = mp.x + 8 - (160 * mcos >> 16);
		int my = mp.y + 8 + (160 * msin >> 16);
		int mz = mp.z + 8;
		if (shakeX != 0 || shakeY != 0) {
			mx += (shakeX << 4) * sinTable[(cyaw + 512) & 0x3FF] >> 16;
			my += (shakeX << 4) * -msin >> 16;
			mz += shakeY << 4;
		}
		// viewAspect over the same 478x248 viewport as gameplay
		// (src/Render.cpp:2223).
		// fov 290 while a dialog runs inside the cinematic, 315 otherwise
		// (src/MayaCamera.cpp:305-310 canvas->state == ST_DIALOG).
		int fov = underDialog ? 290 : 315;
		camera_.setView(mx, my, mz, cyaw, mp.pitch, mp.roll, fov,
			(fov << 14) / ((478 << 14) / 248));
	} else {
	int yaw = env_.player->viewAngle & 0x3FF;
	int viewSin = sinTable[yaw];
	int viewCos = sinTable[(yaw + 256) & 0x3FF];
	int rvx = (env_.player->viewX << 4) + 8 - (160 * viewCos >> 16);
	int rvy = (env_.player->viewY << 4) + 8 + (160 * viewSin >> 16);
	int rvz = (env_.player->viewZ << 4) + 8;
	if (shakeX != 0 || shakeY != 0) {
		rvx += (shakeX << 4) * sinTable[(yaw + 512) & 0x3FF] >> 16;
		rvy += (shakeX << 4) * -viewCos >> 16;
		rvz += shakeY << 4;
	}
	// Pitch feeds the view matrix (positive = up; the loot crouch writes
	// player->viewPitch). FOV stays at the documented 290: legacy widened by
	// |pitch| only on the mvp2D billboard path (src/TinyGL.cpp:195-200), the
	// world/GL projection kept viewFov — the rewrite has a single projection.
	// viewAspect over the 478x248 world viewport (src/Render.cpp:2223).
	camera_.setView(rvx, rvy, rvz, env_.player->viewAngle, env_.player->viewPitch, 0, 290,
		(290 << 14) / ((478 << 14) / 248));
	}

	if (env_.world->initialized() && env_.map->numNodes > 0) {
		renderer.setCanvasViewport(kWorldRect[0], kWorldRect[1], kWorldRect[2], kWorldRect[3]);
		env_.world->drawSky(camera_);
		// Per-sprite sort-bias hooks (src/Render.cpp:856-862): corpse/linked
		// entities draw nearer (+1), monsters (-1).
		spriteSortBias_.assign(env_.map->numSprites, 0);
		// Stacked-character classification (ADR 0005/0007, spec
		// 2026-08-26 §1): entity-def driven — live NPCs, monsters whose tile
		// is outside the diverted floater/special-boss families (their
		// renderers are not ported), corpsified NPCs whose art tile stayed in
		// the NPC range after the def swap (src/Game.cpp:567-569), and
		// corpsified monsters via the kInfoCorpse clause below. Legacy gate:
		// renderSpriteAnim runs for every entity with monster != nullptr
		// (src/Render.cpp:1622-1626); ET_MONSTER is its exact proxy
		// (allocated iff eType == 2, src/Game.cpp:430-436).
		spriteCharClass_.assign(env_.map->numSprites, 0);
		for (const Entity& ent : env_.game->entities()) {
			int si = ent.getSprite();
			if (!ent.def || si < 0 || si >= env_.map->numSprites) continue;
			if (ent.info & 0x1010000) spriteSortBias_[si] = +1;
			else if (ent.def->eType == Enums::ET_MONSTER) spriteSortBias_[si] = -1;
			const int tile = env_.map->mapSpriteInfo[si] & 0xFF; // monsters never carry SPRITE_FLAG_TILE (+257)
			if (ent.def->eType == Enums::ET_NPC ||
			    (ent.def->eType == Enums::ET_CORPSE &&
			     tile >= Enums::TILENUM_FIRST_NPC &&
			     tile <= Enums::TILENUM_LAST_NPC)) {
				spriteCharClass_[si] = 1;
			}
			// Corpsified monsters keep their character-sheet art tile, so the
			// death pose must render through the stacked path's MANIM_DEAD
			// single-corpse-quad branch (src/Render.cpp:3466-3475); the
			// billboard fallback would clamp frame 0x70 back onto the
			// standing base frame (bug: "standing imp remains"). Gated on
			// the died-marker so placed corpse props stay on the billboard
			// path.
			else if ((ent.info & Entity::kInfoCorpse) != 0 &&
			         ((env_.map->mapSpriteInfo[si] >> 8) & Enums::MANIM_MASK) == Enums::MANIM_DEAD) {
				spriteCharClass_[si] = 1;
			}
			else if (ent.def->eType == Enums::ET_MONSTER &&
			         !isFloaterTile(tile) && !isSpecialBossTile(tile)) {
				spriteCharClass_[si] = 1;
			}
		}
		env_.world->drawBSP(*env_.map, *env_.media, camera_, spriteSortBias_.data(),
		                    spriteCharClass_.data());
		renderer.restoreCanvasViewport(window);
	} else {
		renderer.g2d().fillRect(0, 0, 480, 320, 32, 32, 64);
	}
}

} // namespace newcore
