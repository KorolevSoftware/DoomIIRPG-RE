#include "domain/game/MonsterAI.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "domain/game/Combat.h"
#include "domain/game/DoorSystem.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityDb.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"
#include "domain/game/MonsterSystem.h"
#include "domain/game/Player.h"
#include "domain/game/SpriteLerps.h"
#include "domain/game/TraceSystem.h"
#include "domain/game/ViewVisibility.h"
#include "domain/world/MapBits.h"
#include "domain/world/MapData.h"

namespace newcore {

namespace {

// One-shot log for a deferred legacy branch.
void logOnce(bool& done, const char* msg) {
	if (done) return;
	done = true;
	std::fprintf(stderr, "%s\n", msg);
}

bool isEvading(int sub) {
	return ((1 << sub) & Enums::EVADING_MONSTERS) != 0;
}

} // namespace

void MonsterAI::init(const Env& env) {
	env_ = env;
	// Game::baseVisitedTiles (src/Game.h:153): linkWorldEntity sets the bit on
	// every tile whose file flag 0x1 is set (src/Game.cpp:497-507, :179).
	int solid = 0;
	for (uint32_t& row : baseVisitedTiles_) row = 0;
	for (int i = 0; i < 1024 && i < (int)env_.map->mapFlags.size(); ++i) {
		if ((env_.map->mapFlags[i] & TileFlag::SOLID_AT_LOAD) != 0) {
			baseVisitedTiles_[i / 32] |= 1u << (i % 32);
			++solid;
		}
	}
	std::fprintf(stderr, "[ai] init: %d solid-at-load tiles in baseVisitedTiles\n", solid);
}

bool MonsterAI::baseVisited(int x, int y) const {
	if (x < 0 || y < 0 || x >= 32 || y >= 32) return false;
	return ((baseVisitedTiles_[y] >> x) & 0x1) != 0;
}

Entity* MonsterAI::firstOnTile(int tx, int ty, int mask) {
	// The player is found by its destination tile before the list walk (:741-743).
	if ((mask & Contents::PLAYER) != 0 &&
	    tx == env_.player->destX >> 6 && ty == env_.player->destY >> 6) {
		return env_.db->playerEntity();
	}
	EntityDb::TileWalk walk("MonsterAI::firstOnTile");
	for (Entity* t = env_.db->findMapEntity(tx, ty); t != nullptr && walk.ok(t); t = t->nextOnTile) {
		if (t->def != nullptr && (mask & (1 << t->def->eType)) != 0) return t;   // :744-748
	}
	return nullptr;
}

// ---- decision ----

void MonsterAI::think(Entity* e, bool b) {
	EntityMonster* m = e->monster;
	m->flags &= ~Enums::MFLAG_ATTACKING;                       // :675-677
	if ((m->flags & Enums::MFLAG_NOTHINK) == 0) {              // :678
		if (!isValidGoal(e)) chooseNewGoal(e, b);
		++m->goalTurns;
		moveToGoal(e);
	}
}

bool MonsterAI::isValidGoal(Entity* e) {
	EntityMonster* m = e->monster;
	const int goalType = m->goalType;
	if (m->goalTurns >= 16 || goalType == EntityMonster::kGoalAttack ||
	    goalType == EntityMonster::kGoalNone) {
		return false;                                          // :609-611
	}
	if (goalType == EntityMonster::kGoalChase) {               // :612-620
		if (m->goalParam != EntityMonster::kGoalParamPlayer &&
		    m->goalParam >= 0 && m->goalParam < (int)env_.db->entities().size()) {
			Entity* k = &env_.db->entities()[m->goalParam];
			const int sprite = k->getSprite();
			if (sprite < 0) return false;
			const int n = env_.map->numSprites;
			Entity* blocker = firstOnTile(env_.map->mapSprites[sprite] >> 6,
				env_.map->mapSprites[sprite + n] >> 6, Contents::MONSTERSOLID);
			if (k->linkIndex != e->linkIndex && (blocker == nullptr || blocker == k)) {
				return true;
			}
		}
	} else if (goalType == EntityMonster::kGoalMoveTile) {     // :622-626
		if (e->linkIndex != m->goalX + m->goalY * 32) return true;
	} else {
		if (goalType == EntityMonster::kGoalFlee || goalType == EntityMonster::kGoalRetreat) {
			return m->goalTurns < m->goalParam;                // :628-630
		}
		if (goalType == EntityMonster::kGoalWait) {            // :631-639
			if (m->goalTurns < m->goalParam) return true;
			if (m->goalTurns == m->goalParam) {
				m->resetGoal();
				return false;
			}
		}
	}
	return false;
}

void MonsterAI::chooseNewGoal(Entity* e, bool b) {
	const int sub = e->def->eSubType;
	e->monster->resetGoal();
	calcSimpleGoal(e, b);
	if (isEvading(sub) && e->monster->goalType == EntityMonster::kGoalAttack) {
		e->monster->goalFlags |= EntityMonster::kGoalFlagRetreat;   // :600-602
	}
}

void MonsterAI::calcSimpleGoal(Entity* e, bool b) {
	(void)b;                                   // forwarded but unused in legacy
	EntityMonster* m = e->monster;
	const int sub = e->def->eSubType;
	if (sub == Enums::MONSTER_ARCH_VILE) {
		// aiCalcArchVileGoal / findRaiseTarget (:538-540): deferred, reads as
		// "no raise target" and the normal goal logic runs.
		static bool logged = false;
		logOnce(logged, "[ai] arch-vile raise deferred");
	}
	// Fear flee (player buffs[11], :541-545): no player buffs in the rewrite.
	const int w = weaponForTarget(e, env_.db->playerEntity());
	if (w != -1) {                                             // :546-558
		m->ce.weapon = w;
		m->goalType = EntityMonster::kGoalAttack;
		m->goalParam = EntityMonster::kGoalParamPlayer;
		if (isEvading(sub)) m->flags |= Enums::MFLAG_ABILITY;
	} else {                                                   // :559-565
		m->goalType = EntityMonster::kGoalChase;
		m->goalParam = EntityMonster::kGoalParamPlayer;
		if (isEvading(sub)) m->flags &= ~Enums::MFLAG_ABILITY;
	}
	std::fprintf(stderr, "[ai] spr=%d sub=%d parm=%d goal=%d w=%d\n",
		e->getSprite(), sub, e->def->parm, m->goalType, w);
}

void MonsterAI::moveToGoal(Entity* e) {
	EntityMonster* m = e->monster;
	const int goalType = m->goalType;
	if (goalType == EntityMonster::kGoalChase || goalType == EntityMonster::kGoalMoveTile ||
	    goalType == EntityMonster::kGoalFlee || goalType == EntityMonster::kGoalRetreat) {
		goalMove(e);                                           // :576-578
	} else if (goalType == EntityMonster::kGoalAttack) {       // :579-587
		m->target = (m->goalParam == EntityMonster::kGoalParamPlayer)
			? nullptr : &env_.db->entities()[m->goalParam];
		env_.monsters->attack(e);
	}
	if (e->def->eSubType == Enums::BOSS_PINKY) {
		// scriptStateVars[9..10] = own position (:588-593): no VM in this Env.
		static bool logged = false;
		logOnce(logged, "[ai] boss pinky position vars deferred");
	}
}

// ---- movement ----

void MonsterAI::initLerp(Entity* e, int travelTime) {
	EntityMonster* m = e->monster;
	const int sprite = e->getSprite();
	SpriteLerps::SpriteLerp* ls = env_.lerps->allocLerpSprite(nullptr, sprite, true);
	MapData& map = *env_.map;
	const int n = map.numSprites;
	if (ls == nullptr) {
		// Deviation D13: pool exhausted (legacy fatal Error 36,
		// src/Game.cpp:3064-3066): place the sprite where freeLerpSprite would
		// and complete the step at once (clears kInfoOnScreenStep; attack()
		// only enqueues). kGoalFlagLerping stays clear.
		const int dstX = 32 + (m->goalX << 6);
		const int dstY = 32 + (m->goalY << 6);
		map.mapSprites[sprite] = (int16_t)dstX;
		map.mapSprites[sprite + n] = (int16_t)dstY;
		map.mapSprites[sprite + 2 * n] =
			(int16_t)(32 + map.heightAt(dstX, dstY) - env_.lerps->spriteZBias(sprite, dstX, dstY));
		reachedGoalMove(e);
		return;
	}
	ls->srcX = map.mapSprites[sprite];                         // :858-860
	ls->srcY = map.mapSprites[sprite + n];
	// Lerps run in legacy baked Z space (SpriteLerps::spriteZBias).
	ls->srcZ = map.mapSprites[sprite + 2 * n] + env_.lerps->spriteZBias(sprite, ls->srcX, ls->srcY);
	ls->dstX = 32 + (m->goalX << 6);                           // :861-863
	ls->dstY = 32 + (m->goalY << 6);
	ls->dstZ = 32 + map.heightAt(ls->dstX, ls->dstY);
	ls->srcScale = ls->dstScale = map.mapSprites[sprite + 8 * n];   // :864
	ls->startTime = env_.lerps->clockMs();                     // :865-866
	ls->travelTime = travelTime;
	ls->flags |= SpriteLerps::SpriteLerp::kFlagEntNoRelink | SpriteLerps::SpriteLerp::kFlagAsync;
	m->frameTime = env_.lerps->clockMs() + travelTime;         // :868
	ls->calcDist();
	m->goalFlags |= EntityMonster::kGoalFlagLerping;           // :870
}

void MonsterAI::finishLerp(Entity* e) {
	EntityMonster* m = e->monster;
	m->goalFlags &= ~EntityMonster::kGoalFlagLerping;          // :874
	if ((m->flags & Enums::MFLAG_KNOCKBACK) != 0) {            // :875-878
		m->flags &= ~Enums::MFLAG_KNOCKBACK;
		e->info &= ~Entity::kInfoOnScreenStep;
	} else {
		reachedGoalMove(e);
	}
}

// Verbatim, including the unreachable loop: one axis delta is zero past the
// first test, so the && condition never holds (MM §6).
bool MonsterAI::checkLineOfSight(int n, int n2, int n3, int n4, int n5) {
	int a = n3 - n;
	int a2 = n4 - n2;
	if (a != 0 && a2 != 0) return false;                       // :889-891
	if (a != 0) a /= std::abs(a);
	if (a2 != 0) a2 /= std::abs(a2);
	while (n != n3 && n2 != n4) {                              // :898-904
		n += a;
		n2 += a2;
		if (firstOnTile(n, n2, n5) != nullptr) return false;
	}
	return true;
}

bool MonsterAI::calcPath(int n, int n2, int n3, int n4, int n5, bool b) {
	PathSearch& ps = ps_;
	const int n6 = n3 - n;
	const int n7 = n4 - n2;
	int closestPathDist = n6 * n6 + n7 * n7;                   // :912-914
	uint8_t visitOrder[4];
	int visitDist[4];
	ps.visitedTiles[n2] |= 1u << n;                            // :916
	const bool los = checkLineOfSight(n, n2, n3, n4, n5 | 0x100);
	if ((ps.lineOfSight == 0 && !los) || (ps.lineOfSight == 1 && los)) {   // :918-925
		if (b) closestPathDist -= 30;
		else closestPathDist += 30;
	}
	if (los) closestPathDist += ps.lineOfSightWeight;
	if (ps.pathDepth > 0 && ((!b && closestPathDist < ps.closestPathDist) ||
	                         (b && closestPathDist > ps.closestPathDist))) {   // :927-931
		ps.closestPath = ps.curPath;
		ps.closestPathDepth = ps.pathDepth;
		ps.closestPathDist = closestPathDist;
	}
	if (n == n3 && n2 == n4) {                                 // :932-937
		ps.closestPath = ps.curPath;
		ps.closestPathDepth = ps.pathDepth;
		ps.closestPathDist = closestPathDist;
		return true;
	}
	if (ps.pathDepth == ps.pathSearchDepth) return false;      // :938-940
	const int* viewStepValues = Player::kViewStepValues;       // == Canvas::viewStepValues (src/Canvas.h:108)
	int n8 = 0;
	int n9 = 4;
	uint8_t b2 = (uint8_t)(env_.combat->nextByte() & 0x3);     // :946
	while (--n9 >= 0) {                                        // :947-975
		b2 = (uint8_t)((b2 + 1) & 0x3);
		const int n10 = n + (viewStepValues[(b2 << 2) + 0] >> 6);
		const int n11 = n2 + (viewStepValues[(b2 << 2) + 1] >> 6);
		if (n11 >= 0 && n11 < 32 && n10 >= 0) {
			if (n10 >= 32) continue;
			if (firstOnTile(n10, n11, Contents::ENV_DAMAGE) != nullptr) continue;   // :955
			if ((ps.visitedTiles[n11] & (1u << n10)) != 0) continue;
			visitOrder[n8] = b2;
			const int n12 = n3 - n10;
			const int n13 = n4 - n11;
			visitDist[n8] = n12 * n12 + n13 * n13;
			const bool los2 = checkLineOfSight(n10, n11, n3, n4, n5 | 0x100);
			if ((ps.lineOfSight == 0 && !los2) || (ps.lineOfSight == 1 && los2)) {
				if (b) visitDist[n8] -= 30;
				else visitDist[n8] += 30;
			}
			if (los2) visitDist[n8] += ps.lineOfSightWeight;
			++n8;
		}
	}
	for (int i = 0; i < n8; ++i) {                             // :976-989 bubble sort
		for (int j = 0; j < n8 - i - 1; ++j) {
			const int n17 = visitDist[j] - visitDist[j + 1];
			if ((!b && n17 > 0) || (b && n17 < 0)) {
				std::swap(visitDist[j], visitDist[j + 1]);
				std::swap(visitOrder[j], visitOrder[j + 1]);
			}
		}
	}
	int n19 = 0;                                               // :990-995
	int n20 = 0;
	for (int k = 0; k < n8; ++k) {
		n19 |= (visitOrder[k] & 0x3) << n20;
		n20 += 2;
	}
	for (int l = 0; l < n8; ++l) {                             // :996-1026
		const int n21 = n19 & 0x3;
		n19 >>= 2;
		const int n22 = n + (viewStepValues[(n21 << 2) + 0] >> 6);
		const int n23 = n2 + (viewStepValues[(n21 << 2) + 1] >> 6);
		const TraceHit h = env_.trace->trace((n << 6) + 32, (n2 << 6) + 32,
			(n22 << 6) + 32, (n23 << 6) + 32, ps.skipEnt, n5, 16);
		if (ps.findEnt != nullptr && h.entity == ps.findEnt) { // :1005-1010
			ps.closestPath = ps.curPath;
			ps.closestPathDepth = ps.pathDepth;
			ps.closestPathDist = closestPathDist;
			return true;
		}
		// A world hit is 1 << ET_WORLD, like the legacy world entity (:1011-1014).
		const int clip = h.blocks() ? (1 << h.eType) : ps.interactClipMask;
		if (clip == 0 || (clip & ps.interactClipMask) != 0) {  // :1015-1025
			++ps.pathDepth;
			ps.curPath = (ps.curPath >> 2) | ((uint64_t)n21 << 62);
			if (calcPath(n22, n23, n3, n4, n5, b)) return true;
			--ps.pathDepth;
			ps.curPath <<= 2;
		}
	}
	return false;
}

bool MonsterAI::goalMove(Entity* e) {
	EntityMonster* m = e->monster;
	PathSearch& ps = ps_;
	bool b = false;
	const int sprite = e->getSprite();
	env_.lerps->snap(sprite);                                  // :1035
	const int n = env_.map->numSprites;
	const int sX = env_.map->mapSprites[sprite];
	const int sY = env_.map->mapSprites[sprite + n];
	ps.closestPath = 0;                                        // :1038-1049
	ps.closestPathDepth = 0;
	ps.closestPathDist = 999999999;
	ps.curPath = 0;
	ps.pathDepth = 0;
	ps.pathSearchDepth = 8;
	ps.findEnt = nullptr;
	ps.skipEnt = e;
	ps.lineOfSight = 2;
	ps.lineOfSightWeight = 0;
	ps.interactClipMask = Contents::DOOR;
	std::memcpy(ps.visitedTiles, baseVisitedTiles_, sizeof(ps.visitedTiles));
	if (m->goalType == EntityMonster::kGoalChase && m->goalParam == EntityMonster::kGoalParamPlayer) {
		ps.findEnt = env_.db->playerEntity();                  // :1050-1055
		m->goalX = env_.player->destX >> 6;
		m->goalY = env_.player->destY >> 6;
		ps.lineOfSightWeight = -4;
	} else if (m->goalType == EntityMonster::kGoalRetreat) {   // :1056-1063
		m->goalX = env_.player->destX >> 6;
		m->goalY = env_.player->destY >> 6;
		ps.interactClipMask = 0;
		b = true;
		ps.lineOfSight = 1;
		ps.pathSearchDepth = m->goalParam;
	} else if (m->goalType == EntityMonster::kGoalFlee) {      // :1064-1069
		m->goalX = env_.player->destX >> 6;
		m->goalY = env_.player->destY >> 6;
		b = true;
		ps.lineOfSight = 1;
	} else if (m->goalType == EntityMonster::kGoalChase) {     // :1070-1074
		ps.findEnt = &env_.db->entities()[m->goalParam];
		m->goalX = ps.findEnt->linkIndex % 32;
		m->goalY = ps.findEnt->linkIndex / 32;
	}
	if (b) ps.closestPathDist = 0;                             // :1075-1077
	bool ok = calcPath(sX >> 6, sY >> 6, m->goalX, m->goalY, Contents::MONSTERSOLID, b);
	if (!ok && ps.closestPathDist < 999999999) {               // :1079-1083
		ok = true;
		ps.curPath = ps.closestPath;
		ps.pathDepth = ps.closestPathDepth;
	}
	if (!ok || ps.pathDepth <= 0) return false;                // :1084, :1115
	ps.curPath >>= 64 - ps.pathDepth * 2;                      // :1085
	e->info &= ~Entity::kInfoOnScreenStep;
	const int dir = (int)(ps.curPath & 0x3);
	const int dX = sX + Player::kViewStepValues[(dir << 2) + 0];   // :1087-1088
	const int dY = sY + Player::kViewStepValues[(dir << 2) + 1];
	m->goalX = dX >> 6;
	m->goalY = dY >> 6;
	const TraceHit h = env_.trace->trace(sX, sY, dX, dY, e, ps.interactClipMask, 25);   // :1091
	if (!h.blocks()) {                                         // :1092-1104
		env_.db->unlinkEntity(e);
		env_.db->linkEntity(e, dX >> 6, dY >> 6);
		// Deviation D4: tile visibility of src or dst instead of the legacy
		// cullBoundingBox of the ±16 step box (:1095-1097).
		const ViewVisibility* vis = env_.visibility;
		if (vis != nullptr && vis->valid &&
		    (vis->tileVisible(sX >> 6, sY >> 6) || vis->tileVisible(dX >> 6, dY >> 6))) {
			e->info |= Entity::kInfoOnScreenStep;
		}
		env_.monsters->interpolatingMonsters = true;
		const int sub = e->def->eSubType;
		initLerp(e, ((sub == Enums::BOSS_MASTERMIND && e->def->parm != 0) || sub == Enums::BOSS_PINKY)
			? 500 : 275);
		std::fprintf(stderr, "[ai] spr=%d step %d,%d -> %d,%d%s\n", sprite, sX >> 6, sY >> 6,
			m->goalX, m->goalY, (e->info & Entity::kInfoOnScreenStep) != 0 ? " (on screen)" : "");
	} else {                                                   // :1105-1111
		m->goalX = sX >> 6;
		m->goalY = sY >> 6;
		if (h.isEntity() && h.eType == Enums::ET_DOOR) {
			env_.doors->performDoorEvent(0, h.entity, 2);
			std::fprintf(stderr, "[ai] spr=%d opens door\n", sprite);
		}
	}
	return true;
}

void MonsterAI::reachedGoalMove(Entity* e) {
	EntityMonster* m = e->monster;
	const EntityDef* def = e->def;
	e->info &= ~Entity::kInfoOnScreenStep;                     // :1122
	const int bit = 1 << def->eSubType;
	if (m->goalType != EntityMonster::kGoalFlee && m->goalType != EntityMonster::kGoalRetreat &&
	    ((bit & Enums::MOVE2ATTACK_MONSTERS) != 0 ||
	     ((bit & Enums::EVADING_MONSTERS) != 0 && (m->flags & Enums::MFLAG_ABILITY) == 0))) {   // :1123
		Entity* target = env_.db->playerEntity();
		const int w = weaponForTarget(e, target);
		if (w != -1) {                                         // :1126-1136
			m->target = nullptr;               // target is always the player (:1127-1132)
			m->ce.weapon = w;
			env_.monsters->attack(e);
			return;
		}
	}
	if ((bit & Enums::EVADING_MONSTERS) != 0) m->flags |= Enums::MFLAG_ABILITY;   // :1138-1140
	if ((m->goalFlags & EntityMonster::kGoalFlagRepath) != 0) {   // :1141-1151, no setter in src/
		m->goalFlags &= ~EntityMonster::kGoalFlagRepath;
		calcSimpleGoal(e, false);
		if (m->goalType == EntityMonster::kGoalMoveTile || m->goalType == EntityMonster::kGoalChase) {
			// tileObstructsAttack is not ported; read as true -> resetGoal.
			static bool logged = false;
			logOnce(logged, "[ai] repath tileObstructsAttack stubbed");
			m->resetGoal();
		}
	}
}

// ---- weapon choice / re-validation ----

int MonsterAI::weaponForTarget(Entity* e, Entity* target) {
	if (target == nullptr || target->def == nullptr) return -1;
	const Combat& combat = *env_.combat;
	const MapData& map = *env_.map;
	const int sprite = e->getSprite();
	const int n = map.numSprites;
	const int sx = map.mapSprites[sprite];
	const int sy = map.mapSprites[sprite + n];
	const int sub = e->def->eSubType;
	const bool targetIsPlayer = target->def->eType == Enums::ET_PLAYER;
	int tx;
	int ty;
	if (targetIsPlayer) {                                      // :692-697
		tx = env_.player->viewX;
		ty = env_.player->viewY;
	} else {
		// Legacy reads the attacker's OWN sprite here (:698-701), so a
		// non-player target always looks co-located; kept verbatim.
		tx = sx;
		ty = sy;
	}
	const int a = tx - sx;
	const int a2 = ty - sy;
	// calcPosition of the target (src/Entity.cpp:1371-1395): the player
	// resolves to its destination.
	int px = sx;
	int py = sy;
	if (targetIsPlayer) {
		px = env_.player->destX;
		py = env_.player->destY;
	} else if (target->getSprite() >= 0) {
		px = map.mapSprites[target->getSprite()];
		py = map.mapSprites[target->getSprite() + n];
	}
	auto hits = [target](const TraceHit& h) { return h.isEntity() && h.entity == target; };
	auto inRange = [&combat](int w, int td) {
		const WeaponDef& wd = combat.weaponDef(w);
		return wd.rangeMin <= td && wd.rangeMax >= td;
	};

	if (sub == Enums::BOSS_PINKY) {                            // :704-714
		if (a != 0 && a2 != 0) return -1;
		const TraceHit h = env_.trace->trace(sx, sy, px, py, e, Contents::BOSS_ATTACK_LOS, 2);
		return hits(h) ? combat.getMonsterField(e->def, 0) : -1;
	}
	if (sub == Enums::BOSS_VIOS) {
		// Random teleport pre-step (:716-745): deferred.
		static bool logged = false;
		logOnce(logged, "[ai] Vios teleport deferred");
	}
	if (a != 0 && a2 != 0) {                                   // :752-766
		if (sub == Enums::MONSTER_ARCH_VILE || (sub == Enums::BOSS_VIOS && e->def->parm != 0)) {
			const int f = combat.getMonsterField(e->def, (sub == Enums::BOSS_VIOS) ? 1 : 0);
			const int td = combat.worldDistToTileDist(env_.trace->distFrom(target, sx, sy));
			if (inRange(f, td)) {
				const TraceHit h = env_.trace->trace(sx, sy, px, py, e, Contents::BOSS_ATTACK_LOS, 2);
				if (hits(h)) return f;
			}
			return -1;
		}
		return -1;
	}
	{                                                          // :769-772
		const TraceHit h = env_.trace->trace(sx, sy, tx, ty, e, Contents::MONSTER_ATTACK_LOS, 2);
		if (!hits(h)) return -1;
	}
	int n9 = a;                                                // :773-779
	int n10 = a2;
	if (n9 != 0) n9 /= std::abs(a);
	if (n10 != 0) n10 /= std::abs(a2);
	const TraceHit clear = env_.trace->trace(sx + n9 * 18, sy + n10 * 18, tx, ty, e,
		Contents::MONSTER_ATTACK_CLEAR, 2);                    // :781-782
	const bool b = hits(clear);
	int f0 = combat.getMonsterField(e->def, 0);
	int f1 = combat.getMonsterField(e->def, 1);
	if (!b) {                                                  // :785-792
		if (Combat::checkWeaponMask(f0, Enums::MELEE_WEAPON_MASK)) f0 = 0;
		if (Combat::checkWeaponMask(f1, Enums::MELEE_WEAPON_MASK)) f1 = 0;
	}
	bool ok0 = false;
	bool ok1 = false;
	int td = combat.worldDistToTileDist(env_.trace->distFrom(target, sx, sy));   // :795
	if (f0 == f1) f1 = 0;                                      // :796-798
	if (f0 != 0 && inRange(f0, td)) ok0 = true;                // :799-804
	if (f1 != 0 && inRange(f1, td)) ok1 = true;                // :805-810
	if (f0 == Enums::WP_MONSTER_CHARGE || f1 == Enums::WP_MONSTER_CHARGE) {   // :812-830
		bool laneClear = true;
		int x = sx >> 6;
		int y = sy >> 6;
		do {
			if (baseVisited(x, y)) {
				laneClear = false;
				break;
			}
			x += n9;
			y += n10;
		} while (--td > 0);
		if (f0 == Enums::WP_MONSTER_CHARGE && !laneClear) ok0 = false;
		if (f1 == Enums::WP_MONSTER_CHARGE && !laneClear) ok1 = false;
	}
	if (ok1 && ok0) {                                          // :832-846
		return ((int)env_.combat->nextByte() <= combat.getMonsterField(e->def, 2)) ? f0 : f1;
	}
	if (ok1) return f1;
	if (!ok0) return -1;
	return f0;
}

bool MonsterAI::isAttackValid(Entity* e) {
	EntityMonster* m = e->monster;
	const int w = m->ce.weapon;
	if (w <= -1) return false;                                 // :649-651
	const int sprite = e->getSprite();
	const int sx = env_.map->mapSprites[sprite];
	const int sy = env_.map->mapSprites[sprite + env_.map->numSprites];
	const int destX = env_.player->destX;
	const int destY = env_.player->destY;
	const TraceHit h = env_.trace->trace(sx, sy, destX, destY, e, Contents::MONSTER_ATTACK_LOS, 2);
	if (!h.isEntity()) return false;                           // :653-655 (a world hit fails both tests below)
	const bool inRange = env_.combat->weaponDef(w).rangeMax >=
		env_.combat->worldDistToTileDist(env_.trace->distFrom(e, destX, destY));   // :656
	bool axis = false;
	if (inRange) {                                             // :657-662
		const int dx = sx - destX;
		const int dy = sy - destY;
		axis = (dx != 0 || dy != 0) && (dx == 0 || dy == 0);
	}
	if (m->target == nullptr && h.eType == Enums::ET_PLAYER && inRange && axis) return true;
	if (m->target == h.entity && inRange && axis) return true;
	return false;
}

} // namespace newcore
