#include "domain/game/MonsterAI.h"

#include <cstdio>
#include <cstdlib>

#include "domain/game/Combat.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityDb.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"
#include "domain/game/MonsterSystem.h"
#include "domain/game/Player.h"
#include "domain/game/TraceSystem.h"
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

// aiGoal_MOVE (src/Entity.cpp:1030-1116) arrives with group G6; until then a
// chasing monster stays where it is.
bool MonsterAI::goalMove(Entity* e) {
	const EntityMonster* m = e->monster;
	int tx = m->goalX;
	int ty = m->goalY;
	if (m->goalType == EntityMonster::kGoalChase && m->goalParam == EntityMonster::kGoalParamPlayer) {
		tx = env_.player->destX >> 6;                          // :1051-1056
		ty = env_.player->destY >> 6;
	}
	std::fprintf(stderr, "[ai] spr=%d chase tile=%d,%d (movement: G6)\n", e->getSprite(), tx, ty);
	return false;
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
