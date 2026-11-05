#include "domain/game/Combat.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "core/Sound.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapData.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "ui/Hud.h"

namespace newcore {

// ---- setup ----

void Combat::init(const Env& env) {
	env_ = env;
	// tileDistances[j] = (64*(j+1))^2 (src/Combat.cpp:41-44).
	for (int j = 0; j < kMaxTileDistances; ++j) {
		tileDistances[j] = 64 * (j + 1) * (64 * (j + 1));
	}
	// Monster templates (src/Combat.cpp:37-39): hp stored as byte*5.
	monsterTemplates.clear();
	if (env_.tables != nullptr &&
	    env_.tables->monsterStats.size() >= (size_t)(51 * 6)) {
		monsterTemplates.reserve(51);
		for (int i = 0; i < 51; ++i) {
			const int8_t* row = &env_.tables->monsterStats[i * 6];
			monsterTemplates.emplace_back(
				5 * (row[0] & 0xFF), row[1], row[2], row[3], row[4], row[5]);
		}
	} else {
		std::fprintf(stderr, "[combat] monsterStats table missing, templates not built\n");
	}
	std::fprintf(stderr, "[combat] init: %zu templates, td(1)=%d td(4)=%d\n",
		monsterTemplates.size(), tileDistSq(1), tileDistSq(4));
}

// ---- helpers ----

uint32_t Combat::nextByte() {
	return (uint32_t)std::rand() & 0xFF;   // src/App.cpp:506-512
}

short Combat::getWeaponWeakness(int w, int sub, int parm) const {
	// src/Combat.cpp:29-31. Out-of-range rows fall back to the x1.0 nibble
	// (table is verified complete for the monster subtypes on the route).
	const size_t idx = (size_t)((sub * 3 + parm) * 8 + w / 2);
	if (env_.tables == nullptr || parm < 0 ||
	    idx >= env_.tables->monsterWeakness.size()) {
		return (short)(1 << 5);
	}
	const int8_t b = env_.tables->monsterWeakness[idx];
	return (short)((((b >> ((w & 0x1) << 2)) & 0xF) + 1) << 5);
}

int Combat::worldDistToTileDist(int n) const {
	// src/Combat.cpp:1235-1242.
	for (int i = 0; i < kMaxTileDistances - 1; ++i) {
		if (n < tileDistances[i]) return i;
	}
	return kMaxTileDistances - 1;
}

int Combat::tileDistSq(int tiles) const {
	const int idx = std::clamp(tiles, 1, kMaxTileDistances) - 1;
	return tileDistances[idx];
}

void Combat::shiftWeapon(bool down) {
	if (down == lerpWpDown || down == weaponDown) return;     // src/Combat.cpp:848-850
	lerpingWeapon = true;
	lerpWpDown = down;
	lerpWpStartTime = (int)*env_.gameTime;
	lerpWpDur = kLowerWeaponTime;
}

void Combat::latchWeaponLerp(int w) {
	if (!lerpingWeapon || playerAttackPose(w) ||
	    lerpWpStartTime + lerpWpDur > (int)*env_.gameTime) return;
	lerpWpStartTime = 0;
	lerpingWeapon = false;
	weaponDown = lerpWpDown;
}

int Combat::getWeaponTileNum(int n) {
	// src/Combat.cpp:1766-1784.
	if (n < 5) return 1 + n;
	switch (n) {
	case 5:  return 13;
	case 6:  return 14;
	case 14: return 15;
	default: return 1 + n - 2;
	}
}

const WeaponDef& Combat::weaponDef(int weaponId) const {
	// Callers guarantee a loaded table (init logs otherwise); a missing table
	// degrades to an all-zero row, i.e. to misses, not to a crash.
	static const WeaponDef kMissing{};
	if (env_.tables == nullptr) return kMissing;
	return env_.tables->weaponDef(weaponId);
}

int Combat::difficulty() const {
	return env_.game != nullptr ? env_.game->difficulty() : 2;
}

void Combat::centerMessage(int mainStrId, const std::string* args, int numArgs) const {
	if (env_.loc == nullptr || env_.hud == nullptr) return;
	std::string text = env_.loc->get(kTextMain, mainStrId);
	if (args != nullptr && numArgs > 0) composeArgs(text, args, numArgs);
	env_.hud->showCenterMessage(text, 0xAA000000, 3500); // message-log stand-in
}

// ---- attack sequence ----

void Combat::performAttack(Entity* target, int attackX, int attackY, bool scripted) {
	// src/Combat.cpp:54-166 with curAttacker == nullptr (the player). The b
	// parameter and attackX/Y are consumed by projectile/BFG paths only.
	(void)attackX;
	(void)attackY;
	(void)scripted;

	curAttacker = nullptr;                             // :59
	attackerMonster = nullptr;                         // :87
	curTarget = target;
	accumRoundDamage = 0;                              // :61
	// player->updateStats / lastCombatTurn / inCombat skipped: no rewrite
	// consumers (:63-75).
	if (curTarget != nullptr) {
		curTarget->info |= Entity::kInfoCombatMark;    // :69-71
		targetType = curTarget->def ? curTarget->def->eType : 0;      // :77
		targetSubType = curTarget->def ? curTarget->def->eSubType : 0; // :78
		targetMonster = curTarget->monster;                            // :79
	} else {
		targetType = targetSubType = 0;
		targetMonster = nullptr;
	}
	attackerWeaponId = env_.player->ce.weapon;         // :86
	if (attackerWeaponId < 0) {
		std::fprintf(stderr, "[combat] performAttack refused (no weapon equipped)\n");
		return;
	}
	// :102 attackerWeapon = attackerWeaponId * 9 — the row is looked up by id.
	// punchingMonster staging (:103-107) omitted — no punching Stage 1.
	stage = 0;                                         // :109
	nextStageTime = 0;                                 // :110
	animEndTime = 0;                                   // :112
	const WeaponDef& row = weaponDef(attackerWeaponId);
	animLoopCount = row.numShots;                      // :113
	{                                                  // :114-120 ammo-pool cap
		const int ammoType = row.ammoType;
		const int usage = row.ammoUsage;
		if (usage > 0 && ammoType >= 0 && ammoType < 9) {
			animLoopCount = std::min(env_.player->ammo[ammoType] / usage, animLoopCount);
		}
	}
	attackerWeaponProj = row.projType;                 // :121
	// Melee lunge for monster weapon 18 omitted (src/Combat.cpp:131-164):
	// unreachable while only the player attacks.
	worldDist = env_.game->trace.distFrom(curTarget,
		env_.player->viewX, env_.player->viewY);       // :127 Chebyshev^2
	tileDist = worldDistToTileDist(worldDist);         // :128
	shotsFired = false;    // fresh shot batch (render->shotsFired analog :891)
	active = true;         // setState(ST_COMBAT) analog (:165), spec §0.D
	std::fprintf(stderr,
		"[combat] performAttack sprite=%d type=%d sub=%d dist=%d tileDist=%d shots=%d proj=%d\n",
		curTarget ? curTarget->getSprite() : -1, targetType, targetSubType,
		worldDist, tileDist, animLoopCount, attackerWeaponProj);
}

void Combat::performMonsterAttack(Entity* attacker, Entity* target) {
	// src/Combat.cpp:54-166 with curAttacker != nullptr. attackX/Y and b are
	// zero/false at every monster call site.
	curAttacker = attacker;                            // :59
	curTarget = target;                                // :60
	accumRoundDamage = 0;                              // :61
	// player->updateStats / lastCombatTurn / inCombat (:63-65, :72-75): no
	// rewrite consumers.
	curAttacker->info |= Entity::kInfoCombatMark;      // :66-68
	if (curTarget != nullptr) {                        // :69-84
		curTarget->info |= Entity::kInfoCombatMark;
		targetType = curTarget->def->eType;
		targetSubType = curTarget->def->eSubType;
		targetMonster = curTarget->monster;
	} else {
		targetType = targetSubType = 0;
		targetMonster = nullptr;
	}
	attackerMonster = curAttacker->monster;            // :90-100
	attackerWeaponId = attackerMonster->ce.weapon;
	if (attackerWeaponId == Enums::WP_MONSTER_CHARGE) {
		attackFrame = Enums::MANIM_ATTACK1;
	} else if (attackerWeaponId == getMonsterField(curAttacker->def, 1)) {
		attackFrame = Enums::MANIM_ATTACK2;
	} else {
		attackFrame = Enums::MANIM_ATTACK1;
	}
	// punchingMonster staging (:103-107): no punching.
	stage = 0;                                         // :109
	nextStageTime = 0;                                 // :110
	animEndTime = 0;                                   // :112
	const WeaponDef& row = weaponDef(attackerWeaponId);
	animLoopCount = row.numShots;                      // :113 (no ammo cap for monsters, :114-120)
	attackerWeaponProj = row.projType;                 // :121
	worldDist = env_.game->trace.distFrom(curAttacker,
		env_.player->viewX, env_.player->viewY);       // :123
	tileDist = worldDistToTileDist(worldDist);         // :124
	if (attackerWeaponId == Enums::WP_MONSTER_CHARGE) {
		// Lunge lerp toward the player (:131-164): deferred, the attack runs
		// as a plain melee attack (spec deviation D8).
		static bool logged = false;
		if (!logged) {
			logged = true;
			std::fprintf(stderr, "[combat] weapon 18 lunge deferred\n");
		}
	}
	active = true;                                     // setState(ST_COMBAT) analog (:165)
	std::fprintf(stderr, "[combat] monster attack spr=%d w=%d proj=%d shots=%d tileDist=%d\n",
		curAttacker->getSprite(), attackerWeaponId, attackerWeaponProj, animLoopCount, tileDist);
}

int Combat::getMonsterField(const EntityDef* def, int k) const {
	if (env_.tables == nullptr || def == nullptr || def->parm < 0) return 0;
	const size_t idx = (size_t)(def->eSubType * 9 + def->parm * 3 + k);   // src/Combat.cpp:1059
	if (idx >= env_.tables->monsterAttacks.size()) return 0;
	return env_.tables->monsterAttacks[idx];
}

bool Combat::tick() {
	// runFrame (src/Combat.cpp:857-862).
	return curAttacker == nullptr ? playerSeq() : monsterSeq();
}

bool Combat::monsterSeq() {
	// src/Combat.cpp:427-610. Pose clocks (frameTime) run on the SpriteLerps
	// clock, stage clocks on gameTime (spec 2026-09-22-monster-attacks §1.1).
	const int sprite = curAttacker->getSprite();
	int32_t& info = env_.map->mapSpriteInfo[sprite];
	const int clock = env_.game->lerps.clockMs();
	const int gt = (int)*env_.gameTime;
	const int shotHold = weaponDef(attackerWeaponId).shothold * 10;
	const int n2 = (info & 0xFF00) >> 8;                         // :429-433
	const int anim = n2 & 0xF0;
	const int frame = n2 & 0xF;
	if ((anim == Enums::MANIM_ATTACK1 || anim == Enums::MANIM_ATTACK2) && frame == 0) {
		if (clock >= attackerMonster->frameTime) {               // :434-441
			attackerMonster->frameTime = clock + shotHold;
			nextStageTime = gt + shotHold;
			info = (info & 0xFFFF00FF) | ((anim | (frame + 1)) << 8);
			launchProjectile();
		}
	} else if (nextStageTime != 0 && gt > nextStageTime && numActiveMissiles == 0 &&
	           env_.game->lerps.animatingEffects() == 0) {      // :442-462
		// Reflection branch (:443-455): buffs absent, reflectionDmg is always 0.
		stage = nextStage;
		nextStageTime = 0;
		nextStage = -1;
	}
	if (stage == 0) {                                            // :462-571
		totalDamage = 0;
		totalArmorDamage = 0;
		hitType = 0;
		crFlags = 0;
		gotCrit = false;
		gotHit = false;
		targetKilled = false;
		const EntityDef* def = curAttacker->def;
		const int soundType = (attackerWeaponId == getMonsterField(def, 1))
			? Enums::MSOUND_ATTACK2 : Enums::MSOUND_ATTACK1; // :479-485
		if (weaponDef(attackerWeaponId).strMax > 0) {            // :487-511
			if (curTarget == nullptr) {
				env_.player->ce.calcCombat(*this, attackerMonster->ce,
					env_.game->db.playerEntity(), true,
					env_.game->trace.distFrom(curAttacker, env_.player->destX, env_.player->destY),
					-1);
				if ((crFlags & Enums::CR_HITMASK) != 0) {
					if ((crFlags & 0x2) != 0) {
						gotCrit = true;
						hitType = 2;
					} else {
						hitType = 1;
					}
					gotHit = true;
					damage = crDamage;
				} else {
					damage = 0;
				}
			} else {
				hitType = 1;
				damage = 1;
			}
			totalDamage += damage;
			totalArmorDamage += crArmorDamage;
		}
		int snd;                                                 // :512-522
		if (attackerWeaponId == 17 && (crFlags & Enums::CR_HITMASK) == 0) {
			snd = 1100;
		} else if (attackerWeaponId == 28) {
			snd = 1087;
		} else {
			snd = monsterSound(def->eSubType, def->parm, soundType);
		}
		Sound::instance().playSound(snd, 0, 4);
		--animLoopCount;                                        // :524-527
		animTime = shotHold;
		animStartTime = gt;
		animEndTime = animStartTime + animTime;
		if (attackerWeaponId != Enums::WP_MONSTER_CHARGE) {      // :528-531
			info = (info & 0xFFFF00FF) | (attackFrame << 8);
			attackerMonster->frameTime = clock + animTime;
		}
		if (curTarget != nullptr) targetKilled = true;           // :532-534
		if (attackerWeaponId == Enums::WP_MONSTER_CHARGE) {      // :535-551, return lerp deferred (D8)
			info &= 0xFFFF00FF;
			attackerMonster->frameTime = clock + 500;            // lerpSprite->travelTime
			launchProjectile();
		}
		if (gotHit && (attackerWeaponId == 23 || def->eSubType == 2)) {   // :552-555
			std::fprintf(stderr, "[combat] burn status deferred\n");
		}
		stage = -1;                                              // :556-563
		nextStage = (animLoopCount <= 0) ? 1 : 0;
		nextStageTime = animEndTime;
		if (def->tileIndex >= Enums::TILENUM_MONSTER_IMP &&      // :564-566 isImp
		    def->tileIndex <= Enums::TILENUM_MONSTER_IMP3 &&
		    attackerWeaponId == 16 && animLoopCount == 0) {
			info ^= 0x20000;                                     // sprite H-flip bit
		}
		updateProjectile();                                      // :567
		// crFlags 0x80 message 67 (:568-570): no calcHit path sets 0x80.
	} else if (stage == 1) {                                     // :574-597
		if (curAttacker->def->eType == Enums::ET_MONSTER) {
			info &= 0xFFFF00FF;
		}
		if (targetKilled) env_.game->entityDied(curTarget, false);
		// Reflect-buff message 131 (:582-585): buffs absent.
		if (accumRoundDamage > 0) {                              // :586-590
			std::string args[2] = { entityName(curAttacker), std::to_string(accumRoundDamage) };
			centerMessage(112, args, 2);
		}
		env_.hud->stopShake();                                   // :591-593
		env_.hud->setDamageDir(0);
		env_.hud->setDamageTime(0);
		attackerMonster->flags |= Enums::MFLAG_ATTACKING;        // :594
		env_.game->sprites.clear(GameSprites::kFlagAnim);       // gsprite_clear(64) (:595)
		return false;
	} else if (stage == -1) {                                    // :599-604
		if (numActiveMissiles != 0 || exploded) updateProjectile();
	}
	if (env_.player->getHealth() <= 0) return false;            // ST_DYING analog (:606-608)
	return true;
}

void Combat::explodeOnPlayer() {
	// src/Combat.cpp:948-1056.
	if (curTarget != nullptr) return;                            // :950-952
	const int64_t now = *env_.upTimeMs;
	const int sprite = curAttacker->getSprite();
	const int ax = env_.map->mapSprites[sprite + 0 * env_.map->numSprites];   // S_X
	const int ay = env_.map->mapSprites[sprite + 1 * env_.map->numSprites];   // S_Y
	// :953-967; zoom absent, the staleTime repaint stamp has no counterpart.
	env_.hud->setDamageDir(Player::calcDamageDir(env_.player->viewX, env_.player->viewY,
		env_.player->viewAngle, ax, ay));
	env_.hud->setDamageTime(now + (animLoopCount > 0 ? 150 : 1000));
	// Reflect buff (buffs[0], :968-971) absent: always the non-reflect path.
	if (gotHit && (totalDamage > 0 || totalArmorDamage > 0)) {  // :972
		playerPainEvent(curAttacker, true);                      // :973
		env_.game->db.unlinkEntity(curAttacker);                 // :976 (linkIndex is kept)
		if (attackerWeaponProj == 7) {                           // :977-980
			std::fprintf(stderr, "[combat] burn status deferred\n");
		}
		env_.player->addArmor(-totalArmorDamage);                // :981
		if (totalDamage > 0) {
			if (difficulty() != 1) {                             // :984-990
				int mapId = env_.game->loadMapID;
				if (difficulty() == 4) mapId *= 2;
				// Legacy divides by numShots unguarded; every attack weapon has >= 1.
				totalDamage += (totalDamage >> 1) +
					mapId / std::max(1, (int)weaponDef(attackerWeaponId).numShots);
			}
			// VIOS bonus (:991-999): deferred (boss).
			accumRoundDamage += totalDamage;                     // :1000
			playerPain(totalDamage, curAttacker, false);         // :1001
			// :1002-1004 reads the player's ce health, not getHealth(): a dead bot
			// must not leave its attacker unlinked (attacker stays unlinked: verbatim).
			if (env_.player->ce.getStat(Enums::STAT_HEALTH) == 0) return;
			// knockbackDist is always 0 (:1005-1016).
			const int dx = env_.player->viewX - ax;
			const int dy = env_.player->viewY - ay;
			const int sx = (dx > 0) - (dx < 0);
			const int sy = (dy > 0) - (dy < 0);
			env_.hud->startRockView(now, 200, env_.player->viewX + sx * 6,
				env_.player->viewY + sy * 6, env_.player->viewZ);
			// Pinky charge knockback (:1017-1019): deferred.
		} else {
			env_.hud->setDamageCount(0);                         // :1028
			totalDamage = 0;                                     // :1029
		}
		if (totalDamage <= 0) centerMessage(72);                 // :1031-1033
		env_.game->db.linkEntity(curAttacker,
			curAttacker->linkIndex % 32, curAttacker->linkIndex / 32);   // :1034
	} else {                                                     // :1040-1054
		env_.hud->setDamageCount(0);                             // :1041
		if ((crFlags & 0x100) != 0) {
			centerMessage(73);
		} else if ((crFlags & 0x80) == 0) {
			std::string args[1] = { entityName(curAttacker) };
			centerMessage(69, args, 1);
		}
		env_.hud->setDamageCount(-1);                            // :1053
	}
}

void Combat::playerPainEvent(Entity* src, bool fromMonster) {
	// src/Player.cpp:624-650
	if (src == nullptr) env_.hud->setDamageDir(3);
	Sound::instance().playSound(env_.player->isFamiliar ? 1111                      // :631-640
		: env_.player->characterChoice == 1 ? 1094 : 1093, 0, 3);
	env_.hud->setDamageCount(1);
	// startShake(500, 2, 150) -> intensity 2*2; the monster case
	// startShake(0, 0, 150) only vibrates.
	if (!fromMonster) env_.hud->startShake(*env_.upTimeMs, 500, 4);
}

void Combat::playerPain(int n, Entity* src, bool showMsg) {
	// src/Player.cpp:652-709, presentation half; the state half is Player::pain.
	if (env_.player->god) return;                               // :654-656
	if (showMsg) {                                               // :657-671
		if (src != nullptr) {
			std::string args[2] = { entityName(src), std::to_string(n) };
			centerMessage(112, args, 2);
		} else {
			std::string args[1] = { std::to_string(n) };
			centerMessage(71, args, 1);
		}
	}
	int warnId = -1;
	switch (env_.player->pain(n)) {                              // :683-697
	case Player::PainBand::NearDeath: warnId = 113; break;
	case Player::PainBand::LowHealth: warnId = 114; break;
	case Player::PainBand::HalfHealth:
		std::fprintf(stderr, "[help] 10/11 deferred\n");         // showHelp not ported
		break;
	case Player::PainBand::None: break;
	}
	if (warnId != -1 && env_.loc != nullptr && env_.hud != nullptr) {
		env_.hud->addMessage(env_.loc->get(kTextMain, warnId), 0xAA000000,
			Hud::kMsgFlagForce | Hud::kMsgFlagCenter);           // addMessage(id, 3)
	}
	// ST_AUTOMAP -> ST_PLAYING (:703-705) lives at the script pain sites
	// (ScriptVM EV_DAMAGEPLAYER / EV_DESTROY_PLAYER). Death (:706-708)
	// is detected by the caller (ADR 0028 decision 4).
}

// Game::getMonsterSound (src/Game.cpp:3771-3845).
int Combat::monsterSound(int sub, int parm, int soundType) {
	const int rnd = soundType == Enums::MSOUND_ALERT1 ? (int)(nextByte() % 3) : 0;   // :3776-3778
	int id = 0;
	bool table = false;
	if (sub == Enums::MONSTER_PINKY && parm == 0) {             // :3780-3800 small pinky
		switch (soundType) {
		case Enums::MSOUND_ALERT1: case Enums::MSOUND_ALERT2: case Enums::MSOUND_ALERT3: id = 1085; break;
		case Enums::MSOUND_ATTACK1: case Enums::MSOUND_ATTACK2: id = 1082; break;
		case Enums::MSOUND_IDLE: id = 1026; break;
		case Enums::MSOUND_PAIN: id = 1084; break;
		case Enums::MSOUND_DEATH: id = 1083; break;
		}
	} else if (sub == Enums::BOSS_MASTERMIND && parm == 1) {    // :3804-3824
		switch (soundType) {
		case Enums::MSOUND_ALERT1: case Enums::MSOUND_ALERT2: case Enums::MSOUND_ALERT3: id = 1116; break;
		case Enums::MSOUND_ATTACK1: case Enums::MSOUND_ATTACK2: table = true; break;   // goto table read
		case Enums::MSOUND_IDLE: id = 1024; break;
		case Enums::MSOUND_PAIN: id = 1025; break;
		case Enums::MSOUND_DEATH: id = 1115; break;
		}
	} else {
		table = true;
	}
	if (table) {                                                 // :3826-3831
		const size_t idx = (size_t)(sub * Enums::MSOUND_TYPES + soundType + rnd);
		id = Enums::MSOUND_NONE;                                 // rewrite guard for a bad index
		if (env_.tables != nullptr && idx < env_.tables->monsterSounds.size()) {
			id = env_.tables->monsterSounds[idx];
			if (id != Enums::MSOUND_NONE) id += 1000;
		}
	}
	if (sub == Enums::MONSTER_IMP && soundType == Enums::MSOUND_DEATH) {
		id = std::rand() % 2 + 1050;                             // :3836-3838
	}
	if (sub == Enums::MONSTER_ZOMBIE && soundType == Enums::MSOUND_DEATH) {
		id = std::rand() % 3 + 1139;                             // :3840-3842
	}
	return id;
}

void Combat::radiusHurtEntities(int tx, int ty, int knock, int dmg, Entity* attacker, Entity* skip) {
	const MapData& map = *env_.map;                                   // src/Combat.cpp:1085-1108
	const int cx = (tx << 6) + 32;
	const int cy = (ty << 6) + 32;
	const int cz = map.heightAt(tx << 6, ty << 6) + 32;
	for (int i = ty - 1; i <= ty + 1; ++i) {
		for (int j = tx - 1; j <= tx + 1; ++j) {
			const TraceHit h = env_.game->trace.trace3D(cx, cy, cz, (j << 6) + 32, (i << 6) + 32,
				map.heightAt(j << 6, i << 6) + 32, nullptr, 4129, 1);
			if (!h.blocks()) hurtEntityAt(j, i, tx, ty, (j == tx && i == ty) ? 0 : knock, dmg, attacker, false, skip);
		}
	}
	// doesScriptExist + queueTile + run: executeTile does both.
	for (int k = ty - 1; k <= ty + 1; ++k) {
		for (int l = tx - 1; l <= tx + 1; ++l) {
			const int mask = 0x4004 | Game::eventFlagForDirection(tx - l, ty - k);
			env_.game->vm()->executeTile(l, k, mask, true);
		}
	}
}

void Combat::hurtEntityAt(int x, int y, int cx, int cy, int knock, int dmg, Entity* attacker, bool b, Entity* skip) {
	crFlags = 16;                                                     // src/Combat.cpp:1114-1196
	shotsFired = true;
	Game& g = *env_.game;
	Player& p = *env_.player;
	Entity* playerEnt = g.db.playerEntity();
	// findMapEntity(x<<6, y<<6, 30383) (src/Game.cpp:738-750): the player
	// entity when it stands there (mask bit 2), else the first entity of a
	// masked eType; the walk then follows nextOnTile unfiltered.
	constexpr int kMask = 30383;
	Entity* e = nullptr;
	if ((kMask & 0x2) != 0 && x == (p.destX >> 6) && y == (p.destY >> 6)) {
		e = playerEnt;
	} else {
		for (Entity* t = g.db.findMapEntity(x, y); t != nullptr; t = t->nextOnTile) {
			if (t->def != nullptr && (kMask & (1 << t->def->eType)) != 0) { e = t; break; }
		}
	}
	EntityDb::TileWalk walk("Combat::hurtEntityAt");
	Entity* next = nullptr;
	for (; e != nullptr && walk.ok(e); e = next) {
		next = e->nextOnTile;
		if (e == skip || (e->info & Entity::kInfoActive) == 0 || e->def == nullptr) continue;
		if (e == playerEnt) {
			if (attackerWeaponProj == 5) continue;
			dmg >>= 1;
			const int armor = std::min(dmg * 10 / 100, p.ce.getStat(Enums::STAT_ARMOR));
			dmg -= armor;
			dmg -= dmg * p.ce.getStat(Enums::STAT_DEFENSE) / 100;
			playerPainEvent(nullptr, false);
			std::fprintf(stderr, "[combat] splash knockback %d on the player not applied\n", knock);
			if (dmg > 0) {
				crArmorDamage = armor;
				playerPain(dmg, nullptr, true);
				p.addArmor(-crArmorDamage);
				p.addStatusEffect(13, 5, 3);
				p.translateStatusEffects();
			}
		} else if (e->def->eType == Enums::ET_CORPSE) {
			if (!b) return;                                           // verbatim early return (:1141-1143)
			g.entityDied(e, false);
			e->info |= Entity::kInfoHidden;
			std::fprintf(stderr, "[combat] monster blood not ported\n");
		} else if (e->monster != nullptr) {
			e->info |= Entity::kInfoCombatMark;
			if ((e->info & Entity::kInfoOnActiveList) == 0) g.monsters.activate(e, true, false, true, true);
			int n7 = getWeaponWeakness(attackerWeaponId, e->def->eSubType, e->def->parm) * dmg >> 8;
			const int n8 = n7 - (e->monster->ce.getStatPercent(Enums::STAT_DEFENSE) * n7 >> 8);
			if (n8 <= 0) continue;
			if (attackerWeaponProj == 5) {                            // :1156-1166
				const int s = e->getSprite();
				const int ns = env_.map->numSprites;
				const int sx = env_.map->mapSprites[s], sy = env_.map->mapSprites[s + ns];
				const int sz = env_.map->mapSprites[s + 2 * ns] + g.lerps.spriteZBias(s, sx, sy);   // baked
				if (GameSprites::Sprite* fx = g.sprites.allocAnim(244, sx, sy, sz + (sz >> 1), *env_.upTimeMs)) {
					fx->flags |= GameSprites::kFlagUnlink;
					nextStageTime = (int)*env_.gameTime + fx->duration;
					env_.map->mapSprites[fx->slot + 3 * ns] = 4;
					env_.map->mapSprites[fx->slot + 8 * ns] = 32;
				}
			}
			const bool pain = g.entityPain(e, n8);
			if ((e->monster->flags & 0x1000) == 0 && !pain)
				std::fprintf(stderr, "[combat] splash knockback %d not applied\n", knock);
			std::string args[2] = { entityName(e), std::to_string(n8) };
			if (e->monster->ce.getStat(Enums::STAT_HEALTH) <= 0) {
				centerMessage(74, args, 2);
				if (attackerWeaponProj != 5) {
					if (b && attacker != nullptr && attacker->def != nullptr &&
					    attacker->def->eType == Enums::ET_DECOR_NOCLIP && attacker->def->eSubType == Enums::DECOR_DYNAMITE) {
						e->info |= Entity::kInfoHidden;
						std::fprintf(stderr, "[combat] monster blood not ported\n");
					}
					g.entityDied(e, true);
				}
			} else {
				centerMessage(75, args, 2);
			}
		} else if (e->def->eType != Enums::ET_ENV_DAMAGE) {
			g.entityPain(e, dmg);
			g.entityDied(e, true);
		}
	}
}

std::string Combat::entityName(const Entity* e) const {
	if (env_.loc == nullptr || e == nullptr || e->def == nullptr) return std::string();
	return Localization::titleOf(env_.loc->get(kTextIngame, e->def->name & 0x3FF));
}

bool Combat::playerSeq() {
	// Muzzle-flash latch (src/Combat.cpp:742-744). The original — and the
	// rewrite until now (new_src/ui/ViewWeapon.cpp:135) — flips it from
	// drawWeapon, i.e. the draw path mutates combat state. Moved here next to
	// the state it belongs to (spec 2026-08-27-ui-layer §6 deviation D2, ADR
	// 0012). Both conditions of the original are kept; only the two enclosing
	// draw-side gates are not re-tested: `active` is guaranteed by the sole
	// caller (GameContext::tickPlaying) and `attackerWeaponId == player weapon`
	// is not needed here, since a weapon mismatch hides the whole pose block
	// anyway and stage 0 re-arms flashDone for every shot.
	// TIMING CONSEQUENCE (knowingly accepted): the flip now lands on the 15 ms
	// tick that first satisfies the comparison instead of on the draw that
	// follows it, so the flash and the held attack pose are one rendered frame
	// shorter and the return lerp starts one frame earlier. The lerp itself is
	// keyed off animStartTime, so its shape is unchanged.
	if (!flashDone && *env_.gameTime >= flashDoneTime) flashDone = true;

	// playerSeq (src/Combat.cpp:178-425) reduced to the hitscan path.
	// Head stage advance (:182-187): the numActiveMissiles / animatingEffects
	// terms have no rewrite counterparts yet.
	if (nextStageTime != 0 && *env_.gameTime > nextStageTime) {
		stage = nextStage;
		nextStageTime = 0;
		nextStage = -1;
	}

	if (stage == 0) {                                            // :188-366
		isGibbed = false;                                        // :189
		totalDamage = 0;                                         // :190
		totalArmorDamage = 0;                                    // :191
		hitType = 0;                                             // :192
		deathAmt = 0;                                            // :193
		gotCrit = false;                                         // :194
		gotHit = false;                                          // :195
		crFlags = 0;                                             // :196
		damage = 0;                                              // :197
		targetKilled = false;                                    // :198
		flashTime = 1;                                           // :199
		// counters[6] run-stat increment (:200): run-stats absent.
		if (((1 << attackerWeaponId) & 0x77FF) == 0) {           // :204-206
			crFlags |= 0x40;
		}
		const int ammoType = weaponDef(attackerWeaponId).ammoType;
		const int usage = weaponDef(attackerWeaponId).ammoUsage;
		const bool hasAmmo = (ammoType != 0);                    // :207-208
		int ammoPool = 0;
		if (hasAmmo && usage != 0 && ammoType > 0 && ammoType < 9) {
			ammoPool = env_.player->ammo[ammoType] / usage;      // :209-211
		}
		if (hasAmmo && targetType != Enums::ET_MONSTER && attackerWeaponId != 14 &&
		    ((1 << attackerWeaponId) & Enums::WP_SENTRY_BOT_MASK) == 0) {   // :213
			if (ammoPool == 2) {                                 // :214-216
				centerMessage(62);
			} else if (ammoPool < 5 && ammoPool > 1) {           // :217-221
				std::string args[1] = { std::to_string(ammoPool - 1) };
				centerMessage(63, args, 1);
			}
		}
		if (targetType == Enums::ET_MONSTER) {                   // :223-247
			if (((1 << attackerWeaponId) & 0x2) == 0) {          // :224-226
				crFlags |= 0x20;
			}
			env_.player->ce.calcCombat(*this, env_.player->ce, curTarget,
				false, worldDist, targetSubType);                // :227
			if ((crFlags & 0x1007) != 0) {                       // :228
				// info bit 0x4000000: write-only in the original (set on a hit
				// roll, cleared when the attack animation leaves stage 1;
				// nothing in src/ ever reads it), meaning unknown, so it stays
				// a literal — docs/original-code/entities.md §5.
				curTarget->info |= 0x4000000;                    // :229
				if ((crFlags & 0x2) != 0) {                      // :230-233
					gotCrit = true;
					hitType = 2;
				} else if (((crFlags & 0x1) != 0) || ((crFlags & 0x4) != 0)) { // :234-236
					hitType = 1;
				}
				gotHit = true;                                   // :237
				damage = crDamage;                               // :238
				deathAmt = targetMonster->ce.getStat(Enums::STAT_HEALTH) - damage; // :239
				// damage-stat counters (:240-245): run-stats absent.
			}
		} else {                                                 // :248-259
			hitType = calcHitEntity(curTarget);                  // :249
			int dmg = weaponDef(attackerWeaponId).strMin;        // :250
			const int dmgMax = weaponDef(attackerWeaponId).strMax;   // :251
			if (dmg != dmgMax) {
				dmg += (int)nextByte() % (dmgMax - dmg);         // :253
			}
			if (env_.game->difficulty() == 4) {
				dmg -= dmg >> 2;                                 // :255-257
			}
			damage = dmg;                                        // :258
		}
		{   // per-weapon shot sound (:260-308)
			int snd = -1;
			switch (attackerWeaponId) {
			case 3: case 5: case 9: snd = 1086; break;
			case 2:                     snd = 1045; break;
			case 7:                     snd = 1117; break;
			case 0: case 8:             snd = 1014; break;
			case 10:                    snd = 1087; break;
			case 11:                    snd = 1101; break;
			case 12:                    snd = 1009; break;
			case 13:                    snd = 1114; break;
			case 14:                    snd = env_.player->characterChoice == 1 ? 1137 : 1136; break;   // :298
			case 1:                     snd = 1015; break;
			}
			if (snd != -1) Sound::instance().playSound(snd, 0, 4);
		}
		totalDamage += damage;                                   // :309
		totalArmorDamage += crArmorDamage;                       // :310
		animTime = weaponDef(attackerWeaponId).shothold;         // :311
		animTime *= (env_.player != nullptr && env_.player->statusEffects[2] > 0) ? 5 : 10;   // :312-317 haste
		animStartTime = (int)*env_.gameTime;                     // :318
		animEndTime = animStartTime + animTime;                  // :319
		flashDone = false;                                       // :320
		flashDoneTime = animStartTime + flashTime;               // :321
		if (hasAmmo && ammoPool < animLoopCount) {               // :322-324
			animLoopCount = ammoPool;
		}
		launchProjectile();                                      // :325
		if ((1 << attackerWeaponId & 0x2406) == 0 &&             // :326-332, WP_NORECOIL, src/Enums.h:162
				env_.hud != nullptr && env_.player != nullptr && env_.upTimeMs != nullptr) {
			const int kick = attackerWeaponId == 11 ? 6 : 2;
			env_.hud->startRockView(*env_.upTimeMs, animTime,
				env_.player->viewX - kick * (env_.player->viewStepX >> 6),
				env_.player->viewY - kick * (env_.player->viewStepY >> 6), env_.player->viewZ);
		}
		if (totalDamage == 0) {                                  // :333-345
			if (targetType == Enums::ET_MONSTER) {
				if ((crFlags & 0x100) != 0) {
					centerMessage(59);                           // :336
				} else if ((crFlags & 0x400) != 0) {
					centerMessage(64);                           // :339
				} else if ((crFlags & 0x1) == 0) {
					centerMessage(68);                           // :342
				}
			}
		} else if (targetType == Enums::ET_MONSTER) {            // :346-348
			accumRoundDamage += totalDamage;
		} else if (hitType != 0) {                               // :349-351
			targetKilled = true;
		}
		stage = -1;                                              // :352
		// hud repaintFlags |= 0x4 (:353): HUD redraws every frame here.
		if (ammoType > 0 && ammoType < 9) {                      // :355-358
			env_.player->ammo[ammoType] =
				(int16_t)std::max(env_.player->ammo[ammoType] - usage, 0);
		}
		nextStage = 1;                                           // :359
		nextStageTime = animEndTime;                             // :360
		updateProjectile();                                      // :361
		if (totalDamage == 0 || hitType == 0) {                  // :363-365
			// ++counters[7]: run-stats absent, log only.
			std::fprintf(stderr, "[combat] miss counter++\n");
		}
	} else if (stage == 1 && *env_.gameTime >= nextStageTime) {  // :368-416
		// dynamite flags (:369-370) and targetType-9/17 executeTile (:375-378)
		// have no Stage-1 consumers; BFG check (:379-381) is out of scope.
		if (targetType == Enums::ET_MONSTER) {
			// Clears the write-only 0x4000000 mark set at :229 (see above).
			curTarget->info &= ~0x4000000;                       // :372-374
		}
		if (targetKilled ||
		    (targetType == Enums::ET_MONSTER &&
		     targetMonster->ce.getStat(Enums::STAT_HEALTH) <= 0)) {
			env_.game->entityDied(curTarget, true);              // :382-386 (ADR 0018)
			targetKilled = true;
		} else if (--animLoopCount > 0 &&
		           ((1 << targetType) & 4385) == 0 &&           // :387 n6 mask
		           targetType != Enums::ET_ATTACK_INTERACTIVE) {
			stage = 0;                                           // :388-391 multi-shot
			animEndTime = 0;
			animTime = 0;
			nextStageTime = 0;
			return true;
		}
		// punch tails (:393-400): punching absent.
		if (targetType == Enums::ET_MONSTER && accumRoundDamage != 0 &&
		    (curTarget->info & Entity::kInfoActive) != 0) {      // :401
			std::string msg;
			if (gotCrit) msg += env_.loc->get(kTextMain, 70);    // :404-406
			std::string line = env_.loc->get(kTextMain, 71);     // :407-408
			std::string args[1] = { std::to_string(accumRoundDamage) };
			composeArgs(line, args, 1);
			msg += line;
			env_.hud->showCenterMessage(msg, 0xAA000000, 3500);  // finishMessageBuffer stand-in
		}
		// weapon-14 unequip-back (:411-414): world-weapon pickup deferred.
		return false;          // seq done -> caller advances the turn (spec §0.C)
	}
	// stage == -1 waits on the clock (:417-422 staleView branch is implicit).
	return true;
}

int Combat::calcHitEntity(Entity* e) {
	// src/Combat.cpp:864-883. def == nullptr (air-shot world slot entities_[0])
	// reads as eType 0 so the range gate applies (spec deviation 13).
	if (env_.player->ce.weapon < 0) {
		crFlags |= 0x400;
		return 0;
	}
	const WeaponDef& row = weaponDef(env_.player->ce.weapon);        // :866
	const int td = worldDistToTileDist(
		env_.game->trace.distFrom(e, env_.player->destX, env_.player->destY)); // :867
	const int8_t rangeLo = row.rangeMin;
	const int8_t rangeHi = row.rangeMax;                             // :868
	if (td < rangeLo || td > rangeHi) {                              // :869-872
		crFlags |= 0x400;
		return 0;
	}
	if (e == nullptr || (e->info & Entity::kInfoActive) == 0) {      // :873-875
		return 0;
	}
	const int eType = e->def ? e->def->eType : 0;
	if (eType != Enums::ET_ATTACK_INTERACTIVE) {                     // :876-878
		return 1;
	}
	const auto& masks = env_.tables->combatMasks;                    // :879-882
	const int parm = e->def->parm;
	if (parm >= 0 && (size_t)parm < masks.size() &&
	    (masks[(size_t)parm] & (1 << env_.player->ce.weapon)) != 0) {
		return 1;
	}
	return 0;
}

void Combat::explodeOnMonster() {
	// src/Combat.cpp:885-946 subset. The explodeThread/shouldFakeCombat hook
	// is deferred (no doesScriptExist API, spec deviation 5).
	shotsFired = true;                                   // :891
	if (checkWeaponMask(attackerWeaponId, 0x2) && hitType == 0) {
		// A missed chainsaw swing makes no noise, so it must not wake the
		// target either: legacy chains this as `else if` (:892-894).
		shotsFired = false;
	} else if (curTarget != nullptr && curTarget->monster != nullptr &&
	    curTarget->def != nullptr &&
	    curTarget->def->eType == Enums::ET_MONSTER &&
	    (curTarget->info & Entity::kInfoOnActiveList) == 0) {
		env_.game->monsters.activate(curTarget, true, false, true, true);   // :895-897
	}
	if (hitType == 0) {
		// rocket-splash radiusHurtEntities tail (:899-901): projectiles deferred.
		return;                                          // :898-903
	}
	if (targetType == Enums::ET_MONSTER) {               // :904-931
		if (totalDamage > 0) {
			// checkMonsterFX (:168-176, :906): WP_POISON marks the target. The
			// poison tick (updateMonsterFX) is unported (deviation U3).
			if (env_.player != nullptr && env_.player->statusEffects[12] > 0) {
				curTarget->monster->monsterEffects |= ((env_.player->statusEffects[30] - 1) << 5) | 0x1;
			}
			env_.game->entityPain(curTarget, totalDamage);       // :907 (ADR 0018)
			if (attackerWeaponId != 14) env_.game->particles.spawnMonsterBlood(curTarget, false);   // :908-910
			// knockback (:911-921), splash radius (:922-924) and negative-damage
			// healing cap (:926-931) skipped: systems absent.
		}
	} else if (targetType == Enums::ET_ATTACK_INTERACTIVE) {  // :933-937
		if (totalDamage > 0) env_.game->entityPain(curTarget, totalDamage);
	} else if (targetType == Enums::ET_CORPSE) {             // :938-945 gib
		const int s = curTarget->getSprite();
		if (s >= 0 && s < env_.map->numSprites) env_.map->mapSpriteInfo[s] |= Enums::SPRITE_FLAG_HIDDEN;
		isGibbed = true;
		env_.game->particles.spawnMonsterBlood(curTarget, true);
		env_.game->spawnDropItem(curTarget);
		Sound::instance().playSound(1037, 0, 3);
	}
}

void Combat::launchProjectile() {
	// src/Combat.cpp:1433-1668, cases 6 and 7 (:1527-1538). Every other type
	// takes the default: arm (ADR 0017, :1572-1576): no missile, detonate on
	// the target in the same frame. The dodgeDir roll (:1444) runs on the
	// missile path only (spec D7); it has no reader besides n5 below.
	int renderMode;
	if (attackerWeaponProj == 6) {
		renderMode = 0;
		missileAnim = Enums::TILENUM_FLESH;
	} else if (attackerWeaponProj == 7) {
		renderMode = 3;
		missileAnim = Enums::TILENUM_FIRE_BALL;
	} else {
		missileAnim = 0;
		exploded = true;
		if (attackerWeaponProj > 0) {
			std::fprintf(stderr, "[combat] projType %d not implemented, treated as instant\n",
				attackerWeaponProj);
		}
		return;
	}
	const int dodgeDir = (std::rand() & 0x7FFFFFFF) > 0x3FFFFFF ? 1 : 0;   // nextInt (:1444)
	int n5 = (dodgeDir * 2 - 1) * 16;                                   // :1445
	int n3 = 16;
	const int n = 256;                                                  // missile speed, units/s
	const int ns = env_.map->numSprites;
	int x1, y1, z1, n2;                                                 // :1449-1461
	if (curAttacker == nullptr) {
		x1 = env_.player->viewX;
		y1 = env_.player->viewY;
		z1 = env_.map->heightAt(x1, y1) + 32;
		n2 = 0;
	} else {
		const int sprite = curAttacker->getSprite();
		x1 = env_.map->mapSprites[sprite + 0 * ns];
		y1 = env_.map->mapSprites[sprite + 1 * ns];
		z1 = env_.map->mapSprites[sprite + 2 * ns] + env_.game->lerps.spriteZBias(sprite, x1, y1);
		n2 = 16;
	}
	// The targetType 0 world hit point (:1470-1474) needs a player shot,
	// and Player::fireWeapon refuses projType > 0.
	int x2, y2, z2;                                                     // :1465-1479
	if (curTarget == nullptr) {
		x2 = env_.player->viewX;
		y2 = env_.player->viewY;
		z2 = env_.map->heightAt(x2, y2) + 32;
	} else {
		const int sprite = curTarget->getSprite();
		x2 = env_.map->mapSprites[sprite + 0 * ns];
		y2 = env_.map->mapSprites[sprite + 1 * ns];
		z2 = env_.map->mapSprites[sprite + 2 * ns] + env_.game->lerps.spriteZBias(sprite, x2, y2);
	}
	int dx = std::abs(x2 - x1);
	int dy = std::abs(y2 - y1);
	if ((crFlags & 0x400) != 0) {                                       // :1580-1592 out of range
		const int n11 = weaponDef(attackerWeaponId).rangeMax * 64;
		n3 = 0;
		if (dx != 0 && dx > dy) {
			x2 = x1 + (x2 - x1) * n11 / dx;
		} else if (dy != 0) {
			y2 = y1 + (y2 - y1) * n11 / dy;
		}
		dx = std::abs(x2 - x1);
		dy = std::abs(y2 - y1);
		n5 = 0;
	} else if (hitType == 0) {                                          // :1593-1600 miss
		if (targetType == Enums::ET_MONSTER || curTarget == nullptr) {
			n3 = 0;
		} else {
			n5 = 0;
		}
	} else {
		n5 = 0;
	}
	if (dy > dx) {                                                      // :1605-1616 major-axis push-out
		const int sy = (y2 - y1 < 0) ? -1 : 1;
		y1 += n2 * sy;
		y2 += -(n3 * sy);
		x2 += n5;
	} else {
		const int sx = (x2 - x1 < 0) ? -1 : 1;
		x1 += n2 * sx;
		x2 += -(n3 * sx);
		y2 += n5;
	}
	if (allocMissile(x1, y1, z1, x2, y2, z2, 1000 * std::max(dx, dy) / n, renderMode) == nullptr) {
		missileAnim = 0;                       // legacy fatal (Error 34 / ERR_MAX_MISSILES): hit at once
		exploded = true;
		return;
	}
	// The frame count and AUTO_ANIMATE of allocMissile (:1676, :1687) and the
	// 242 frame (:1639-1641) are all cleared again by the !b mask (:1663-1666),
	// so for types 6/7 the sprite info stays the bare tile written by alloc.
	exploded = false;                                                   // :1667
}

GameSprites::Sprite* Combat::allocMissile(int x1, int y1, int z1, int x2, int y2, int z2,
                                          int duration, int renderMode) {
	// src/Combat.cpp:1670-1717 for projType 6/7 (no 0x4000 arc, missileAnim != 0).
	if (numActiveMissiles == kMaxActiveMissiles) {
		std::fprintf(stderr, "[combat] ERR_MAX_MISSILES: %d missiles in flight\n", kMaxActiveMissiles);
		return nullptr;
	}
	GameSprites::Sprite* s = env_.game->sprites.alloc(missileAnim, 0,
		GameSprites::kFlagUnlink | GameSprites::kFlagNoExpire | GameSprites::kFlagPropagate,
		*env_.upTimeMs);                                                // gsprite_alloc(.., 2562) (:1678)
	if (s == nullptr) return nullptr;
	activeMissiles[numActiveMissiles++] = s;
	env_.map->mapSprites[s->slot + 3 * env_.map->numSprites] = (int16_t)renderMode;
	s->pos[0] = (int16_t)x1;
	s->pos[1] = (int16_t)y1;
	s->pos[2] = (int16_t)z1;
	env_.game->sprites.write(*s, s->pos[0], s->pos[1], s->pos[2]);
	s->pos[3] = (int16_t)x2;
	s->pos[4] = (int16_t)y2;
	s->pos[5] = (int16_t)z2;
	s->duration = duration;
	if (duration != 0) {                                                // :1703-1710
		s->vel[0] = 1000 * (s->pos[3] - s->pos[0]) / duration;
		s->vel[1] = 1000 * (s->pos[4] - s->pos[1]) / duration;
		s->vel[2] = 1000 * (s->pos[5] - s->pos[2]) / duration;
	} else {
		s->vel[0] = s->vel[1] = s->vel[2] = 0;
	}
	return s;
}

bool Combat::isInFront(int tx, int ty) const {
	// src/Game.cpp:3583-3594.
	const int n3 = 256 - (env_.player->viewAngle & 0x3FF);
	const int n4 = (tx << 6) + 32 - env_.player->viewX;
	const int n5 = (ty << 6) + 32 - env_.player->viewY;
	if (n4 == 0 && n5 == 0) return true;
	const int n6 = (SpriteLerps::vecToDir(n4, n5) + n3) & 0x3FF;
	return n6 >= 128 && n6 <= 384;
}

void Combat::updateProjectile() {
	// src/Combat.cpp:1249-1431 for the ported projectile types.
	GameSprites& gs = env_.game->sprites;
	const int ns = env_.map->numSprites;
	if (numActiveMissiles > 0) {                                        // :1251-1400
		int renderMode = 0;
		for (int i = 0; i < numActiveMissiles; ++i) {
			GameSprites::Sprite* m = activeMissiles[i];
			// In flight: only the 248 / 171 branches act (:1255-1284), not ported.
			if ((m->flags & GameSprites::kFlagPropagate) != 0) continue;
			const int x = m->pos[3];                                    // :1287-1291 (b = true)
			const int y = m->pos[4];
			const int z = m->pos[5];
			if (attackerWeaponProj == 7) {                              // :1329-1340
				Sound::instance().playSound(1034, 0, 4);
				// antifire buffs[9] fog branch (:1331-1337): buffs absent.
				missileAnim = Enums::TILENUM_FIRE_BALL;
				renderMode = 4;
			} else {
				missileAnim = 0;                                        // default: (:1369-1372)
			}
			gs.destroy(m);                                              // :1379
			if (missileAnim != 0 &&
			    (curAttacker == nullptr || (curTarget == nullptr && hitType != 0))) {   // :1380-1391
				GameSprites::Sprite* fx = gs.allocAnim(missileAnim, x, y, z, *env_.upTimeMs);
				if (fx != nullptr) {
					fx->flags |= GameSprites::kFlagUnlink;
					nextStageTime = (int)*env_.gameTime + fx->duration;
					env_.map->mapSprites[fx->slot + 3 * ns] = (int16_t)renderMode;
					env_.map->mapSprites[fx->slot + 8 * ns] = 64;
					if (curAttacker != nullptr &&
					    (env_.map->mapSpriteInfo[curAttacker->getSprite()] & Enums::SPRITE_FLAG_FLIP_HORIZONTAL) != 0) {
						env_.map->mapSpriteInfo[fx->slot] |= Enums::SPRITE_FLAG_FLIP_HORIZONTAL;
					}
				}
			}
			for (int k = i + 1; k < numActiveMissiles; ++k) {           // :1393-1397
				activeMissiles[k - 1] = activeMissiles[k];
			}
			--numActiveMissiles;
			exploded = true;
		}
	} else if (gotHit && (totalDamage > 0 || totalArmorDamage > 0) && exploded &&
	           (attackerWeaponId == 16 || attackerWeaponId == 15 ||
	            attackerWeaponId == 18 || attackerWeaponId == 17)) {        // :1401-1420 melee overlay
		if (attackerWeaponId == 16) {
			missileAnim = Enums::TILENUM_MONSTER_CLAW;
		} else if (attackerWeaponId == 15) {
			missileAnim = Enums::TILENUM_MONSTER_BITE;
		} else {
			missileAnim = Enums::TILENUM_MONSTER_BLUNT_TRAUMA;
		}
		const int sprite = curAttacker->getSprite();                    // calcPosition (src/Entity.cpp:1383-1386)
		if (isInFront(env_.map->mapSprites[sprite + 0 * ns] >> 6, env_.map->mapSprites[sprite + 1 * ns] >> 6)) {
			// (destZ, destY, destZ) is verbatim; the face-player case overwrites X/Y.
			GameSprites::Sprite* fx = gs.allocAnim(missileAnim, env_.player->destZ,
				env_.player->destY, env_.player->destZ, *env_.upTimeMs);
			if (fx != nullptr) {
				fx->flags |= GameSprites::kFlagUnlink;
				env_.map->mapSprites[fx->slot + 3 * ns] = 5;
				if ((env_.map->mapSpriteInfo[sprite] & Enums::SPRITE_FLAG_FLIP_HORIZONTAL) != 0 &&
				    missileAnim == Enums::TILENUM_MONSTER_CLAW) {
					env_.map->mapSpriteInfo[fx->slot] |= Enums::SPRITE_FLAG_FLIP_HORIZONTAL;
				}
			}
		}
	}
	if (!exploded) return;
	// Legacy clears the flag inside each arm AFTER the explode call (:1424,:1428);
	// clearing it up front is equivalent (neither arm re-reads it) and safe
	// against re-entry.
	exploded = false;
	if (curTarget == nullptr) {                           // :1422-1424
		explodeOnPlayer();
		return;
	}
	explodeOnMonster();                                  // :1426-1427
}

} // namespace newcore
