#include "domain/game/Targeting.h"

#include <vector>

#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/TraceSystem.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Tables.h"
#include "ui/Hud.h"

namespace newcore {

void Targeting::init(const Env& env) {
	env_ = env;
}

// Forward vector of the current player view in 16.16 (legacy -view[2]/-view[6],
// src/MovementController.cpp:38, src/PlayingInputHandler.cpp:218). Same
// derivation as the camera pull-back in GameContext.cpp (render block).
void Targeting::viewForward(int& fwdX, int& fwdY) const {
	const std::vector<int32_t>& sinTable = env_.tables->sinTable;
	const int a = env_.player->viewAngle & 0x3FF;
	fwdX = sinTable[(a + 256) & 0x3FF];        // cos
	fwdY = -sinTable[a];                       // -sin
}

// Ordered target election over the sorted fire-trace hit list — port of the
// ACTION_FIRE scan (src/PlayingInputHandler.cpp:218-385, docs/original-code/
// combat.md §8). The legacy loot election (n6) can never fire here: lootable
// corpses are preempted by findLootableCorpseFacing in Action::Use, so this
// walk only elects attack targets. Deliberately NOT ported (silent legacy
// special cases): barricade unlink (:387-393), sentry-bot pickup (:405-431),
// water-spout refill (:433-447).
TraceHit Targeting::electFireTarget(int weapon) {
	Player& p = *env_.player;
	Combat& combat = env_.game->combat;
	const bool melee = Combat::checkWeaponMask(weapon, 2);  // WP_MELEEMASK = chainsaw only (src/Enums.h:155)
	int mask = 13997;                                       // CONTENTS_WEAPONSOLID (src/Enums.h:31)
	if (weapon == 2) mask |= 0x4100;                        // holy water: ENV_DAMAGE + DECOR_NOCLIP (:203-205)
	if (melee) mask |= 0x10;                                // chainsaw: PLAYERCLIP (:212-215)
	const int tiles = melee ? 1 : 6;                        // :208-215
	int fwdX = 0, fwdY = 0;
	viewForward(fwdX, fwdY);
	const int endX = p.viewX + ((tiles * 64 * fwdX) >> 16); // :218 n7*-view[2]>>8 = n7 tiles
	const int endY = p.viewY + ((tiles * 64 * fwdY) >> 16);
	TraceSystem& trace = env_.game->trace;
	trace.trace(p.viewX, p.viewY, endX, endY,
		env_.game->db.playerEntity(), mask, 2);

	TraceHit elected;            // legacy entity (kind None = nothing elected)
	TraceHit melee13;            // legacy entity2 (:249-254)
	for (const TraceHit& h : trace.hits()) {
		Entity* ent = h.entity;
		const int dist = trace.distFrom(h, p.viewX, p.viewY);
		const int et = h.eType;   // resolved once by TraceSystem (ADR 0011)
		const int sub = h.eSubType;
		if (et == Enums::ET_WORLD || et == Enums::ET_SPRITEWALL ||
		    et == Enums::ET_PLAYERCLIP) {                       // :229-236
			if (!elected.blocks()) { elected = h; }
			break;                                              // blocking: always ends the walk
		}
		if (et == Enums::ET_ATTACK_INTERACTIVE) {               // :238-247
			if (((1 << sub) & 0x1) == 0 || weapon == 1) {        // eSubType != 0 FURNITURE, or chainsaw
				elected = h;
				break;
			}
			continue;
		}
		if (et == Enums::ET_NONOBSTRUCTING_SPRITEWALL) {        // :248-254
			if (melee) melee13 = h;
			continue;
		}
		if (et == Enums::ET_NPC) {                              // :255-263
			if (dist >= 8192) { elected = h; break; }           // adjacent NPCs are transparent
			continue;
		}
		if (et == Enums::ET_MONSTER) {                          // :264-269
			elected = h;
			break;                                              // wins over anything collected
		}
		if (et == Enums::ET_DOOR) {                             // :270-277
			if (!elected.blocks()) { elected = h; }
			break;
		}
		if (et == Enums::ET_CORPSE) {                           // :278-334
			// EXACT one-tile equality and NO break — an own-tile corpse
			// (dist 0) is skipped so the monster behind it still wins
			// (combat.md §8.4). The non-chainsaw branch elects a LOOT target
			// (:318-334), already handled by findLootableCorpseFacing before
			// the fire branch, so only the chainsaw attack pick lives here.
			if (dist == combat.tileDistances[0] && weapon == 1) {
				// isWorld() is the transfer of the old null-def test on the
				// elected entity; it is provably redundant here (the world
				// branch always breaks above) but kept so the condition still
				// matches :306-311 term by term.
				if (!elected.blocks() || elected.isWorld() ||
				    elected.eType != Enums::ET_CORPSE ||
				    elected.entity->linkIndex < ent->linkIndex) { // :306-311 highest linkIndex of the pile
					elected = h;
				}
			}
			continue;
		}
		if (et == Enums::ET_ENV_DAMAGE) {                       // :335-338
			if (sub == 1 && weapon == 2 && p.ammo[3] >= 2) { elected = h; break; }
			continue;
		}
		if (et == Enums::ET_DECOR) {                            // :339-343
			const int si = ent->getSprite();
			if (si >= 0 && si < env_.map->numSprites &&
			    (env_.map->mapSpriteInfo[si] & 0xFF) == 0x95) { // TILENUM_PRACTICE_TARGET
				elected = h;
				break;
			}
			continue;
		}
		if (et == Enums::ET_DECOR_NOCLIP) {                     // :344-348
			if (sub == 7 && dist == combat.tileDistances[0]) { elected = h; break; }
			continue;
		}
		if (et != Enums::ET_ITEM && !elected.blocks()) {        // :350 fallback (ITEM never elected)
			elected = h;
		}
	}

	int dist2 = combat.tileDistances[9];                        // :356-359
	if (elected.blocks()) dist2 = trace.distFrom(elected, p.viewX, p.viewY);
	// eType 10 out of weapon range (:379-381).
	if (elected.isEntity() &&                                   // was: entity != nullptr && entity->def != nullptr
	    elected.eType == Enums::ET_ATTACK_INTERACTIVE &&
	    ((1 << elected.eSubType) & 0x1) == 0 &&
	    combat.worldDistToTileDist(dist2) > combat.weaponDef(weapon).rangeMax) {
		elected = TraceHit();
	}
	// Melee promotion of the remembered eType 13 (:383-385).
	if (melee13.blocks() && (!elected.blocks() || elected.isWorld() ||
	    (elected.eType != Enums::ET_MONSTER &&
	     elected.eType != Enums::ET_CORPSE))) {
		elected = melee13;
	}
	return elected;
}

// Facing probe feeding the health-bar readout — port of
// MovementController::checkFacingEntity (src/MovementController.cpp:28-93,
// docs/original-code/combat.md §7.1). Single 6-tile ray from the logical tile
// centre pushed 28 units forward, mask 21741 (WORLD/MONSTER/NPC/DOOR/ITEM/
// DECOR/ATTACK_INTERACTIVE/SPRITEWALL/DECOR_NOCLIP), radius 2. No Z check:
// legacy tests Z only when zoomed in and there is no zoom system.
void Targeting::updateFacingProbe() {
	Player& p = *env_.player;
	constexpr int kFacingMask = 21741;         // src/MovementController.cpp:38
	int fwdX = 0, fwdY = 0;
	viewForward(fwdX, fwdY);
	const int startX = p.destX + ((28 * fwdX) >> 16);   // :38 -view[2]*28 >> 14
	const int startY = p.destY + ((28 * fwdY) >> 16);
	const int endX = p.destX + ((384 * fwdX) >> 16);    // :38 6*-view[2] >> 8 = 6 tiles
	const int endY = p.destY + ((384 * fwdY) >> 16);
	TraceSystem& trace = env_.game->trace;
	TraceHit hit = trace.trace(startX, startY, endX, endY,
		env_.game->db.playerEntity(), kFacingMask, 2);
	// Monster promotion re-scan (:41-86): entered only when the nearest hit is
	// ITEM / MONSTERBLOCK_ITEM / SPRITEWALL / ATTACK_INTERACTIVE / DECOR_NOCLIP,
	// then the sorted hit list is walked from index 0 (the nearest hit itself
	// included) and the pick may move further along the ray. eType 11 is
	// unreachable with this mask (legacy dead branch) but kept for fidelity.
	if (hit.isEntity()) {                     // was: hit != nullptr && hit->def != nullptr
		const int t0 = hit.eType;
		const int t0Sub = hit.eSubType;
		if (t0 == Enums::ET_ITEM || t0 == Enums::ET_MONSTERBLOCK_ITEM ||
		    t0 == Enums::ET_SPRITEWALL || t0 == Enums::ET_ATTACK_INTERACTIVE ||
		    t0 == Enums::ET_DECOR_NOCLIP) {
			for (const TraceHit& h : trace.hits()) {
				Entity* ent = h.entity;
				const int et = h.eType;   // resolved once by TraceSystem (ADR 0011)
				const int sub = h.eSubType;
				if (et == Enums::ET_MONSTER) {                          // :47-53
					if (t0 != Enums::ET_SPRITEWALL) hit = h;
					break;
				}
				if (et == Enums::ET_DOOR || et == Enums::ET_PLAYERCLIP ||
				    et == Enums::ET_WORLD) break;                       // :56-62
				if (et == Enums::ET_SPRITEWALL) {                       // :63-65
					const int li = ent->linkIndex;
					if (li >= 0 && li < (int)env_.map->mapFlags.size() &&
					    (env_.map->mapFlags[li] & 0x2) != 0) break;  // opaque tile flag
					continue;
				}
				if (et == Enums::ET_DECOR) {                            // :66-72
					if (t0 == Enums::ET_SPRITEWALL) hit = h;
					break;
				}
				if (et == Enums::ET_DECOR_NOCLIP) {                     // :74-79
					if (t0Sub != 6) { hit = h; break; }
					continue;
				}
				if (et == Enums::ET_ATTACK_INTERACTIVE &&
				    (sub == 1 || sub == 2 || sub == 3)) {               // :80-83
					if (t0 != Enums::ET_ITEM) { hit = h; break; }
					continue;
				}
			}
		}
	}
	// The HUD contract is unchanged: facingEntity stays an Entity* (a world hit
	// still parks the world slot here, exactly as before).
	p.facingEntity = hit.entity;
	if (hit.isEntity()) {          // was: facingEntity != nullptr && ->def != nullptr
		// Distance gate (:88-93): non-monsters beyond Chebyshev^2 36864 (3 tiles)
		// drop; monsters are never distance-gated. showHelp branches absent.
		// DEVIATION: legacy measures from destX/destY, we use the interpolated
		// eye (identical while idle, <=1 tile apart mid-lerp).
		const int dist = trace.distFrom(hit, p.viewX, p.viewY);
		if (hit.eType != Enums::ET_MONSTER &&
		    dist > env_.game->combat.tileDistances[2]) {
			p.facingEntity = nullptr;
		}
	}
}

// Resolve the facing probe into LIVE ET_MONSTER stats every frame; -1 hides
// the bar (legacy gates src/Hud.cpp:825-835). Called by GameContext::render
// right after updateFacingProbe (spec §P1-G4).
void Targeting::feedHealthBar() const {
	int feedId = -1, feedHp = 0, feedMaxHp = 0;
	bool feedLowBar = false, feedBoss = false;
	Entity* fe = env_.player->facingEntity;
	if (fe != nullptr && fe->monster != nullptr && fe->isMonster() &&
	    (fe->info & Entity::kInfoActive) != 0) {
		feedHp = fe->monster->ce.getStat(Enums::STAT_HEALTH);
		if (feedHp > 0) {
			feedId = fe->getSprite();
			feedMaxHp = fe->monster->ce.getStat(Enums::STAT_MAX_HEALTH);
			// n3 = 50 for a PINKY with parm 0 (src/Hud.cpp:866-868);
			// n4 += 1 for a boss (:874, Entity::isBoss()).
			feedLowBar = fe->def != nullptr && fe->def->eSubType == 5 &&
				fe->def->parm == 0;
			feedBoss = Game::isBossDef(fe->def);
		}
	}
	env_.hud->feedMonsterHealth(feedId, feedHp, feedMaxHp, feedLowBar, feedBoss);
}

} // namespace newcore
