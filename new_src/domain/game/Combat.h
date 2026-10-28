#ifndef NEW_DOMAIN_GAME_COMBAT_H
#define NEW_DOMAIN_GAME_COMBAT_H

#include <cstdint>
#include <string>
#include <vector>

#include "domain/game/CombatEntity.h"
#include "domain/game/GameSprites.h"
#include "domain/game/WeaponTable.h"

namespace newcore {

class Entity;
class EntityMonster;
class Game;
class Hud;
class Localization;
class MapData;
class Player;
struct EntityDef;
struct Tables;

// Peer combat subsystem mirroring legacy app->combat (src/Combat.h:12-176):
// per-shot sequence state + the table lookups. Owned by Game (Game::combat),
// wired once from Main.cpp via init(Env) — same shape as vm.init/dialogs.init.
// All math is integer/fixed-point verbatim from src/CombatEntity.cpp /
// src/Combat.cpp; rolls use std::rand() & 0xFF like the RE port
// (src/App.cpp:506-512 — roll-bit-exactness is not a legacy invariant).
class Combat {
public:
	// Weapon rows live in WeaponTable.h as WeaponDef / WeaponPose.

	static constexpr int kMaxTileDistances = 16;   // MAX_TILEDISTANCES (src/Combat.h:19)

	// CR flags consumed by calcHit/calcDamage/playerSeq:
	// 0x001 hit, 0x002 crit, 0x004 far-shot cap, 0x010 punch (omitted),
	// 0x020 no-strength-bonus, 0x040 punch-flag ranged gate,
	// 0x100 dodge/no-damage, 0x400 out-of-range auto-miss,
	// 0x2000 forced-crit (unused Stage 1). Legacy OR-s into crFlags and
	// early-returns WITHOUT clearing; playerSeq clears at stage-0 entry
	// (src/Combat.cpp:196).

	struct Env {                       // wired once from Main.cpp
		Game* game = nullptr;
		Player* player = nullptr;
		Hud* hud = nullptr;
		Localization* loc = nullptr;
		const Tables* tables = nullptr;
		MapData* map = nullptr;
		const int64_t* gameTime = nullptr;   // ctx->gameTime
		const int64_t* upTimeMs = nullptr;   // ctx->upTimeMs: legacy app->time for HUD/camera effects
	};

	void init(const Env& env);         // builds tileDistances + monsterTemplates[51]

	// --- state (subset of src/Combat.h:61-139 used by the hitscan path) ---
	bool active = false;               // rewrite-only: "we are in ST_COMBAT"
	// Attacking monster; nullptr = the player's own attack (runFrame dispatch,
	// src/Combat.cpp:857-862, ADR 0028).
	Entity* curAttacker = nullptr;
	EntityMonster* attackerMonster = nullptr;
	int attackFrame = 64;              // MANIM_ATTACK1 / MANIM_ATTACK2 pose (src/Combat.cpp:89-101)
	Entity* curTarget = nullptr;
	int targetType = 0, targetSubType = 0;
	EntityMonster* targetMonster = nullptr;
	// attackerWeapon (src/Combat.cpp:102, the pre-scaled attackerWeaponId * 9)
	// has no counterpart here: every reader looks the row up by weapon id.
	int attackerWeaponId = -1, attackerWeaponProj = 0;
	int stage = -1, nextStage = -1, nextStageTime = 0;
	int animStartTime = 0, animTime = 0, animEndTime = 0;
	int flashTime = 0;
	bool flashDone = false;
	int flashDoneTime = 0;
	int animLoopCount = 0;
	int damage = 0, totalDamage = 0, accumRoundDamage = 0, deathAmt = 0;
	int totalArmorDamage = 0;
	int hitType = 0;
	bool gotCrit = false, gotHit = false, targetKilled = false;
	int crFlags = 0, crDamage = 0, crArmorDamage = 0, crHitChance = 0, crCritChance = 0;
	int worldDist = 0, tileDist = 0;
	int playerMissRepetition = 0, monsterMissRepetition = 0;
	bool shotsFired = false;           // sight-wake suppression flag for Stage 2
	// Degenerate-projectile state (src/Combat.h:99,105 exploded/missileAnim).
	bool exploded = false;
	int missileAnim = 0;
	static constexpr int kMaxActiveMissiles = 8;   // MAX_ACTIVE_MISSILES (src/Combat.h:21)
	GameSprites::Sprite* activeMissiles[kMaxActiveMissiles] = {};
	int numActiveMissiles = 0;         // src/Combat.h:81
	int tileDistances[kMaxTileDistances] = {};
	std::vector<CombatEntity> monsterTemplates;   // 51 entries (src/Combat.cpp:37-39)
	int loadMapID = 1;                 // miss-guard gate (b||mapID<8||tileDist>1); map00 => <8

	// Player attack entry (src/Combat.cpp:54-166, curAttacker==nullptr form;
	// melee-lunge block omitted — weapon 18 unreachable Stage 1).
	void performAttack(Entity* target, int attackX, int attackY, bool scripted);

	// performAttack with curAttacker != nullptr (src/Combat.cpp:54-166). The
	// weapon-18 lunge (:131-164) is deferred; the attack then runs as a plain
	// melee attack.
	void performMonsterAttack(Entity* attacker, Entity* target);

	// runFrame (src/Combat.cpp:857-862): playerSeq or monsterSeq by
	// curAttacker; returns true while the seq still runs. Called once per
	// Playing tick.
	bool tick();

	// src/Combat.cpp:1058-1060: monsterAttacks[sub*9 + parm*3 + k]
	// (k 0 = primary, 1 = secondary weapon, 2 = primary chance); out of range
	// reads 0 ("no weapon").
	int getMonsterField(const EntityDef* def, int k) const;

	// Non-monster target hit gate (src/Combat.cpp:864-883).
	int calcHitEntity(Entity* e);

	// Hitscan impact application (src/Combat.cpp:885-946 subset).
	void explodeOnMonster();

	// Projectile launch (src/Combat.cpp:1433-1668). Flying missiles for
	// projType 6 (flesh) and 7 (fireball); every other type takes the default:
	// arm (ADR 0017): no missile, exploded = true, so updateProjectile applies
	// the hit in the same frame.
	void launchProjectile();

	// Missile arrival + melee overlay + the exploded dispatch tail
	// (src/Combat.cpp:1249-1431), for the ported projectile types.
	void updateProjectile();

	static int getWeaponTileNum(int n);       // src/Combat.cpp:1766-1784
	short getWeaponWeakness(int w, int sub, int parm) const;  // src/Combat.cpp:29-31
	int worldDistToTileDist(int n) const;     // src/Combat.cpp:1235-1242

	// Chebyshev^2 distance of `tiles` tiles (tiles >= 1), i.e.
	// tileDistances[tiles - 1] = (64*tiles)^2 (src/Combat.cpp:41-44). Exists
	// because the raw array is off by one: tileDistances[0] means ONE tile.
	int tileDistSq(int tiles) const;
	uint32_t nextByte();                      // src/App.cpp:506-512

	// Accessors for the CombatEntity stat-math trio (they replace legacy's
	// app singleton reaches into app->combat / app->game / weapons table).
	const Env& env() const { return env_; }
	int difficulty() const;                           // game->difficulty()

	// Entity::CheckWeaponMask twin (src/Entity.h:52-54).
	static bool checkWeaponMask(int weaponId, int mask) { return ((1 << weaponId) & mask) != 0; }

	// Weapon row lookup for this module and for consumers outside it
	// (Player::fireWeapon guards, Targeting range gate). A missing table or an
	// out-of-range id reads as an all-zero record.
	const WeaponDef& weaponDef(int weaponId) const;

	// Center-message stand-in for legacy hud->addMessage(str[, args])
	// (spec §3.3): composes kTextMain str with %NN args via Game::composeArgs.
	void centerMessage(int mainStrId, const std::string* args = nullptr, int numArgs = 0) const;

	// Player::painEvent (src/Player.cpp:624-650): HUD damage flag, pain sound
	// log, and the screen shake for non-monster damage.
	void playerPainEvent(Entity* src, bool fromMonster);
	// Presentation half of Player::pain (src/Player.cpp:652-709): optional
	// damage message, the health-band warnings, then Player::pain.
	void playerPain(int n, Entity* src, bool showMsg);

private:
	// Two-stage playerSeq timer (src/Combat.cpp:178-425 hitscan reduction).
	bool playerSeq();
	// src/Combat.cpp:427-610: attack poses, hit roll and per-shot timing.
	bool monsterSeq();
	// Monster hit/miss applied to the player (src/Combat.cpp:948-1056).
	void explodeOnPlayer();
	// src/Combat.cpp:1670-1717; nullptr when no missile or sprite slot is free.
	GameSprites::Sprite* allocMissile(int x1, int y1, int z1, int x2, int y2, int z2,
	                                  int duration, int renderMode);
	// Game::isInFront (src/Game.cpp:3583-3594): tile (tx,ty) within ±90° of the view.
	bool isInFront(int tx, int ty) const;
	// Game::getMonsterSound (src/Game.cpp:3771-3845) for the attack types
	// (MSOUND_ATTACK1/2) only, which never roll.
	int monsterSound(int sub, int parm, int soundType) const;
	// Ingame name of an entity's def (legacy Entity::name, def->name | 0x400).
	std::string entityName(const Entity* e) const;

	Env env_;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_COMBAT_H
