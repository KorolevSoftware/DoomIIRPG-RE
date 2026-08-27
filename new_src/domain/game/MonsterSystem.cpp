#include "domain/game/MonsterSystem.h"

#include <cstdio>
#include <string>

#include "domain/game/Combat.h"
#include "domain/game/Enums.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/game/TraceSystem.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "ui/Hud.h"

namespace newcore {

// Defined in Game.cpp (declared in Game.h): %NN argument substitution over
// Localization strings. Redeclared here so the XP message reuses the single
// copy of the decoder without pulling the Game container into this module.
void composeArgs(std::string& text, const std::string* args, int numArgs);

void MonsterSystem::init(const Env& env) {
	env_ = env;
	numMonsters_ = 0;                       // pool lifetime = one map load (spec §0.B)
	activeMonsters = inactiveMonsters = nullptr;
	combatMonsters = nullptr;
	interpolatingMonsters = false;
}

// ---- entityDb tile-list access (copies of Game's; see MonsterSystem.h) ----

void MonsterSystem::linkEntity(Entity* e, int tx, int ty) {
	if (tx < 0 || ty < 0 || tx >= 32 || ty >= 32) return;
	unlinkEntity(e);
	int idx = ty * 32 + tx;
	e->nextOnTile = env_.entityDb[idx];
	if (e->nextOnTile) e->nextOnTile->prevOnTile = e;
	e->prevOnTile = nullptr;
	env_.entityDb[idx] = e;
	e->linkIndex = (short)idx;
	e->info |= Entity::kInfoLinked;
}

void MonsterSystem::unlinkEntity(Entity* e) {
	if (!(e->info & Entity::kInfoLinked)) return;
	if (e->prevOnTile) e->prevOnTile->nextOnTile = e->nextOnTile;
	else {
		int idx = e->linkIndex;
		if (idx >= 0 && idx < 1024 && env_.entityDb[idx] == e)
			env_.entityDb[idx] = e->nextOnTile;
	}
	if (e->nextOnTile) e->nextOnTile->prevOnTile = e->prevOnTile;
	e->nextOnTile = e->prevOnTile = nullptr;
	e->info &= ~Entity::kInfoLinked;
}

// ---- Monsters / combat (spec 2026-08-26-combat-stage1 §0.B, §3.2) ----

EntityMonster* MonsterSystem::allocMonster() {
	if (numMonsters_ >= kMaxMonsters) {
		// Legacy Error(37) ERR_MAX_MONSTERS (src/Game.cpp:431-434).
		std::fprintf(stderr, "[monster] ERR_MAX_MONSTERS (37): pool exhausted\n");
		return nullptr;
	}
	return &entityMonsters_[numMonsters_++];     // :435
}

void MonsterSystem::setXPSystems(Player* player, const Localization* loc, Hud* hud) {
	xpPlayer_ = player;
	xpLoc_ = loc;
	xpHud_ = hud;
}

bool MonsterSystem::isBossDef(const EntityDef* def) {
	// src/Entity.cpp:1399 shape: eSubType within [FIRSTBOSS..LASTBOSS].
	return def != nullptr &&
	       def->eSubType >= Enums::FIRSTBOSS && def->eSubType <= Enums::LASTBOSS;
}

// Faithful port of Game::activate (src/Game.cpp:752-808). The render-side
// shotsFired latch lives on Combat now (same suppression window).
void MonsterSystem::activate(Entity* e, bool runStaticFunc, bool rangeCheck, bool alertSound, bool b4) {
	(void)b4;                                  // legacy unused parameter
	if (e == nullptr || e->monster == nullptr || env_.map == nullptr) return;
	EntityMonster* monster = e->monster;
	const int sprite = e->getSprite();
	if (((env_.map->mapSpriteInfo[sprite] & 0xFF00) >> 8 & 0xF0) == Enums::MANIM_IDLE_BACK &&
	    !env_.combat->shotsFired) {
		return;                                // :760-762 back-turned wake guard
	}
	if (rangeCheck && env_.trace->distFrom(e, env_.trace->playerX(), env_.trace->playerY()) >
	                  env_.combat->tileDistances[3]) {
		return;                                // :763-765 (> tileDistances[3] = 4 tiles)
	}
	e->info |= Entity::kInfoActivated;         // :766
	// noclip early-out (:767-769): no noclip cheat in the rewrite.
	if ((e->info & Entity::kInfoOnActiveList) != 0) {
		return;                                // :770-772 already active
	}
	env_.map->mapSpriteInfo[sprite] &= 0xFFFF00FF; // :774 clear anim byte | 0x0
	if (monster->nextOnList != nullptr) {      // :775-786 unhook from inactive ring
		if (e == inactiveMonsters && monster->nextOnList == inactiveMonsters) {
			inactiveMonsters = nullptr;
		} else {
			if (e == inactiveMonsters) inactiveMonsters = monster->nextOnList;
			monster->nextOnList->monster->prevOnList = monster->prevOnList;
			monster->prevOnList->monster->nextOnList = monster->nextOnList;
		}
	}
	if (activeMonsters == nullptr) {           // :787-797 append to active ring
		monster->nextOnList = e;
		monster->prevOnList = e;
		activeMonsters = e;
	} else {
		monster->prevOnList = activeMonsters->monster->prevOnList;
		monster->nextOnList = activeMonsters;
		activeMonsters->monster->prevOnList->monster->nextOnList = e;
		activeMonsters->monster->prevOnList = e;
	}
	e->info |= Entity::kInfoOnActiveList;      // :798
	monster->flags &= ~Enums::MFLAG_NOACTIVATE;               // :799
	if (runStaticFunc && (monster->flags & Enums::MFLAG_TRIGGERONACTIVATE) != 0) {
		if (env_.vm) env_.vm->executeStaticFunc(Enums::SCR_MONSTER_ACTIVATE);   // :800-802
		monster->flags &= ~Enums::MFLAG_TRIGGERONACTIVATE;
	}
	if (alertSound) {                          // :804-807 MSOUND_ALERT1, no audio backend
		std::fprintf(stderr, "[monster] alert sound sub=%d parm=%d\n",
			e->def ? e->def->eSubType : -1, e->def ? e->def->parm : -1);
	}
	std::fprintf(stderr, "[monster] activate sprite=%d\n", sprite);
}

// Faithful port of Game::deactivate (src/Game.cpp:825-855).
void MonsterSystem::deactivate(Entity* e) {
	if (e == nullptr || e->monster == nullptr) return;
	EntityMonster* monster = e->monster;
	if ((e->info & Entity::kInfoOnActiveList) == 0) {
		return;                                // :827-829 not on any ring we manage
	}
	if (monster->nextOnList != nullptr) {      // :830-841 unhook from active ring
		if (e == activeMonsters && monster->nextOnList == activeMonsters) {
			activeMonsters = nullptr;
		} else {
			if (e == activeMonsters) activeMonsters = monster->nextOnList;
			monster->nextOnList->monster->prevOnList = monster->prevOnList;
			monster->prevOnList->monster->nextOnList = monster->nextOnList;
		}
	}
	if (inactiveMonsters == nullptr) {         // :842-853 append to inactive ring
		monster->nextOnList = e;
		monster->prevOnList = e;
		inactiveMonsters = e;
	} else {
		monster->prevOnList = inactiveMonsters->monster->prevOnList;
		monster->nextOnList = inactiveMonsters;
		inactiveMonsters->monster->prevOnList->monster->nextOnList = e;
		inactiveMonsters->monster->prevOnList = e;
	}
	e->info &= ~Entity::kInfoOnActiveList;     // :854
}

// Stage-1 stub (spec §0.B): placed where legacy runs AI + lerps
// (src/Game.cpp:2458-2474); monsters never move or attack, so the window
// just closes.
void MonsterSystem::updateMonsters() {
	if (*env_.monstersTurn != 0) endMonstersTurn();
}

// src/Game.cpp:2452-2456. canvas->startRotation(true) has no rewrite
// counterpart (input gating is idle-based).
void MonsterSystem::endMonstersTurn() {
	*env_.monstersTurn = 0;
}

// Stage-1 stub (spec §0.B): no lerps exist, so snapping degenerates to
// driving/closing the turn — the only externally visible part of
// src/Game.cpp:2411-2449.
void MonsterSystem::snapMonsters(bool b) {
	(void)b;
	if (*env_.monstersTurn != 0) endMonstersTurn();
}

// Non-boss ET_MONSTER subset of Entity::pain (src/Entity.cpp:281-394).
bool MonsterSystem::painMonster(Entity* e, int dmg, int attackerWeaponId) {
	if (e == nullptr || e->monster == nullptr || !e->isMonster() || env_.map == nullptr) return false;
	EntityMonster* m = e->monster;
	const int sprite = e->getSprite();
	if (sprite < 0 || sprite >= env_.map->numSprites) return false;
	if (!(e->info & Entity::kInfoActive)) return false;        // :286-288
	// Boss phase hooks at 75/50/25% with staticFuncs 2/3/4 (:293-339):
	// deferred (no bosses on the map00 route).
	int n2 = m->ce.getStat(Enums::STAT_HEALTH) - dmg;          // :290-292
	if ((m->flags & Enums::MFLAG_NOKILL) != 0 && n2 <= 0) {    // :341-343
		n2 = 1;
	}
	m->ce.setStat(Enums::STAT_HEALTH, n2);                     // :344
	if (n2 > 0) {
		// MSOUND_PAIN (:347-348) logged — no audio backend.
		std::fprintf(stderr, "[monster] pain sound sub=%d parm=%d hp=%d\n",
			e->def->eSubType, e->def->parm, n2);
		env_.map->mapSpriteInfo[sprite] =
			(env_.map->mapSpriteInfo[sprite] & 0xFFFF00FF) | 0x6000;    // :350-353 MANIM_PAIN
		m->frameTime = *env_.clockMs + 250;    // nowMs() = Game's lerp clock (deviation D-6)
		if (attackerWeaponId != 2 /*holy water*/) m->resetGoal();   // :354-356
	} else {
		env_.map->mapSpriteInfo[sprite] =
			(env_.map->mapSpriteInfo[sprite] & 0xFFFF00FF) | 0x6000;    // :358-359 lethal hold pose
		m->frameTime = *env_.clockMs + 450;    // :360-368 (250 + 200 lethal hold)
	}
	return false;                              // boss staticFunc return value; always false here
}

// ET_MONSTER subset of Entity::died (src/Entity.cpp:459-521).
void MonsterSystem::diedMonster(Entity* e, bool giveXP) {
	if (e == nullptr || e->monster == nullptr || !e->isMonster() ||
	    env_.map == nullptr || env_.defs == nullptr) return;
	EntityMonster* m = e->monster;
	const int sprite = e->getSprite();
	if (sprite < 0 || sprite >= env_.map->numSprites) return;
	if (!(e->info & Entity::kInfoActive)) return;              // :431 guard
	e->info &= ~Entity::kInfoActive;                           // :434
	e->info |= Entity::kInfoActivated;                         // :460
	m->resetGoal();                                            // :461
	// Snap script lerps of this sprite (corpsifyMonster pattern,
	// src/Game.cpp:620-631) so a running lerp can't fight the death pose.
	if (env_.snapLerps) env_.snapLerps(sprite);
	int info = env_.map->mapSpriteInfo[sprite];
	info = (info & 0xFFFF00FF) | 0x7000;                       // :463 death-frame overlay
	m->frameTime = *env_.clockMs;                              // :464
	if ((env_.map->mapSpriteInfo[sprite] & 0x10000) != 0) {    // :465-471 hidden branch
		info |= 0x17000;
	} else {
		e->info |= Entity::kInfoCorpse | Entity::kInfoActive;  // :469 (0x1020000); trimCorpsePile skipped
	}
	env_.map->mapSpriteInfo[sprite] = info;
	// monsterEffects re-stamp (:472-484) and Lost Soul/Cacodemon poof
	// (:491-495): deferred (absent on the map00 route).
	deactivate(e);                                             // :485
	if (giveXP) awardKillXP(*m);                               // :496 (+ :407-413)
	const EntityDef* corpseDef =
		env_.defs->find(Enums::ET_CORPSE, e->def ? e->def->eSubType : 0,
		                e->def ? e->def->parm : -1);                // :501 def swap
	if (corpseDef != nullptr) e->def = corpseDef;
	*env_.facingDirty = true;                  // :527 canvas updateFacingEntity analog
	std::fprintf(stderr, "[monster] died sprite=%d xpGiven=%d\n", sprite, giveXP ? 1 : 0);
}

// checkMonsterDeath(b=true) XP half (src/Entity.cpp:407-413) plus the msg-103
// composition split out of Player::addXP (spec deviation 14).
void MonsterSystem::awardKillXP(const EntityMonster& m) {
	if (xpPlayer_ == nullptr) return;
	int xp = m.ce.calcXP();                    // :408
	// boss +130 (:409-411): unreachable while no boss is killable.
	if (xpLoc_ != nullptr && xpHud_ != nullptr) {
		std::string msg = xpLoc_->get(kTextMain, 103);
		std::string args[1] = { std::to_string(xp) };
		composeArgs(msg, args, 1);
		xpHud_->showCenterMessage(msg, 0xAA000000, 3500);
	}
	xpPlayer_->addXP(xp);                      // :412
}

// Port of ScriptThread::corpsifyMonster (src/ScriptThread.cpp:2249-2266),
// see MonsterSystem.h for the elided parts. Callers guarantee a
// monster-family entity (legacy requires entity->monster != nullptr,
// src/ScriptThread.cpp:1618-1620).
void MonsterSystem::corpsifyMonster(Entity* e, int x, int y) {
	if (!e || !e->isMonster() || !env_.map || !env_.defs) return;
	int s = e->getSprite();
	if (s < 0 || s >= env_.map->numSprites) return;
	int n = env_.map->numSprites;

	// snapLerpSprites(sprite) analog (src/ScriptThread.cpp:2250 -> src/
	// Game.cpp:1149-1166): force-complete any active lerp of this sprite so
	// its per-tick position writes + relink and its completion snap can no
	// longer fight the corpse placement below. Owner-thread resume omitted:
	// MAKE_CORPSE runs mid-dispatch of some thread and re-entrant run() is
	// unsafe in the rewrite VM.
	if (env_.snapLerps) env_.snapLerps(s);

	// Visual death state: anim/frame overlay bits 8-14 = 0x7000, low byte
	// keeps the original art tileNum (src/ScriptThread.cpp:2253-2255).
	env_.map->mapSpriteInfo[s] = (env_.map->mapSpriteInfo[s] & 0xFFFE00FF) | 0x7000;

	// Position to the tile center; stored S_Z is raw-relative in this
	// rewrite (the renderer adds terrain per frame), so write the bare
	// +32 offset — legacy writes getHeight+32 into its terrain-baked
	// storage (src/ScriptThread.cpp:2256-2257).
	env_.map->mapSprites[s + 0 * n] = (int16_t)x;
	env_.map->mapSprites[s + 1 * n] = (int16_t)y;
	env_.map->mapSprites[s + 2 * n] = 32;

	// Corpse entity info: keep the sprite id, add corpse/inactive marker +
	// active visibility + activated (src/ScriptThread.cpp:2258-2259).
	e->info = (e->info & 0xFFFF) | Entity::kInfoCorpse | Entity::kInfoActive |
		Entity::kInfoActivated;

	// Def swap: same subtype/parm, now an ET_CORPSE def
	// (src/ScriptThread.cpp:2261-2263).
	const EntityDef* corpseDef =
		env_.defs->find(Enums::ET_CORPSE, e->def ? e->def->eSubType : 0,
		                e->def ? e->def->parm : -1);
	if (corpseDef != nullptr) e->def = corpseDef;

	// Relink at the new tile (:2264-2265). checkMonsterDeath sound omitted.
	linkEntity(e, x >> 6, y >> 6);
	// TEMP [dbg] corpsify audit (remove after user confirms): exactly ONE
	// solid blocker (this linked corpse) must remain on the tile.
	std::fprintf(stderr,
		"[dbg] corpsify spr=%d tile=%d,%d linked=%d corpse=%d anim=0x%X\n",
		s, x >> 6, y >> 6, (e->info & Entity::kInfoLinked) != 0 ? 1 : 0,
		(e->info & Entity::kInfoCorpse) != 0 ? 1 : 0,
		env_.map->mapSpriteInfo[s] & 0xFF00);
}

} // namespace newcore
