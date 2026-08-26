#include "domain/game/Combat.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
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
	std::fprintf(stderr, "[combat] init: %zu templates, td[0]=%d td[3]=%d\n",
		monsterTemplates.size(), tileDistances[0], tileDistances[3]);
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

int8_t Combat::weaponField(int weaponId, int field) const {
	const size_t idx = (size_t)(weaponId * 9 + field);
	if (env_.tables == nullptr || idx >= env_.tables->weaponData.size()) return 0;
	return env_.tables->weaponData[idx];
}

const std::vector<int8_t>& Combat::weaponTable() const {
	// Callers guarantee a loaded table (init logs otherwise); fall back to a
	// static empty so a failed tables load degrades to misses, not crashes.
	static const std::vector<int8_t> kEmpty;
	return env_.tables != nullptr ? env_.tables->weaponData : kEmpty;
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

	curTarget = target;
	accumRoundDamage = 0;                              // :61
	// player->updateStats / lastCombatTurn / inCombat skipped: no rewrite
	// consumers (:63-75).
	if (curTarget != nullptr) {
		curTarget->info |= 0x200000;                   // :69-71
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
	attackerWeapon = attackerWeaponId * 9;             // :102
	// punchingMonster staging (:103-107) omitted — no punching Stage 1.
	stage = 0;                                         // :109
	nextStageTime = 0;                                 // :110
	animEndTime = 0;                                   // :112
	animLoopCount = weaponField(attackerWeaponId, kFieldNumShots);        // :113
	{                                                  // :114-120 ammo-pool cap
		const int ammoType = weaponField(attackerWeaponId, kFieldAmmoType);
		const int usage = weaponField(attackerWeaponId, kFieldAmmoUsage);
		if (usage > 0 && ammoType >= 0 && ammoType < 9) {
			animLoopCount = std::min(env_.player->ammo[ammoType] / usage, animLoopCount);
		}
	}
	attackerWeaponProj = weaponField(attackerWeaponId, kFieldProjType);   // :121
	// Melee lunge for monster weapon 18 omitted (src/Combat.cpp:131-164):
	// unreachable while only the player attacks.
	worldDist = env_.game->entityDistFrom(curTarget,
		env_.player->viewX, env_.player->viewY);       // :127 Chebyshev^2
	tileDist = worldDistToTileDist(worldDist);         // :128
	shotsFired = false;    // fresh shot batch (render->shotsFired analog :891)
	active = true;         // setState(ST_COMBAT) analog (:165), spec §0.D
	std::fprintf(stderr,
		"[combat] performAttack sprite=%d type=%d sub=%d dist=%d tileDist=%d shots=%d proj=%d\n",
		curTarget ? curTarget->getSprite() : -1, targetType, targetSubType,
		worldDist, tileDist, animLoopCount, attackerWeaponProj);
}

bool Combat::tick() {
	// playerSeq (src/Combat.cpp:178-425) reduced to the hitscan path.
	// Head stage advance (:182-187): the numActiveMissiles / animatingEffects
	// terms have no rewrite counterparts yet.
	if (nextStageTime != 0 && *env_.gameTime > nextStageTime) {
		stage = nextStage;
		nextStageTime = 0;
		nextStage = -1;
	}

	if (stage == 0) {                                            // :188-366
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
		const int ammoType = weaponField(attackerWeaponId, kFieldAmmoType);
		const int usage = weaponField(attackerWeaponId, kFieldAmmoUsage);
		const bool hasAmmo = (ammoType != 0);                    // :207-208
		int ammoPool = 0;
		if (hasAmmo && usage != 0 && ammoType > 0 && ammoType < 9) {
			ammoPool = env_.player->ammo[ammoType] / usage;      // :209-211
		}
		// Sentry-bot check of :213 has no counterpart (no sentry bots).
		if (hasAmmo && targetType != Enums::ET_MONSTER && attackerWeaponId != 14) {
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
				curTarget->info |= 0x4000000;                    // :229 highlight marker
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
			int dmg = weaponField(attackerWeaponId, kFieldStrMin);   // :250
			const int dmgMax = weaponField(attackerWeaponId, kFieldStrMax); // :251
			if (dmg != dmgMax) {
				dmg += (int)nextByte() % (dmgMax - dmg);         // :253
			}
			if (env_.game->difficulty() == 4) {
				dmg -= dmg >> 2;                                 // :255-257
			}
			damage = dmg;                                        // :258
		}
		{   // per-weapon fire sound (:260-308): ids logged, no audio backend
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
			case 14:                    snd = 1136; break; // characterChoice==2/3 -> 1137
			case 1:                     snd = 1015; break;
			}
			if (snd != -1) std::fprintf(stderr, "[combat] fire sound %d\n", snd);
		}
		totalDamage += damage;                                   // :309
		totalArmorDamage += crArmorDamage;                       // :310
		animTime = weaponField(attackerWeaponId, kFieldShothold);// :311
		// haste statusEffects[2] x5 branch absent -> always x10 (:312-317).
		animTime *= 10;
		animStartTime = (int)*env_.gameTime;                     // :318
		animEndTime = animStartTime + animTime;                  // :319
		flashDone = false;                                       // :320
		flashDoneTime = animStartTime + flashTime;               // :321
		if (hasAmmo && ammoPool < animLoopCount) {               // :322-324
			animLoopCount = ammoPool;
		}
		// launchProjectile (:325): PROJTYPE 0 allocates no missile and marks
		// exploded immediately (src/Combat.cpp:1572-1576), so updateProjectile
		// calls explodeOnMonster in the same frame (:1421-1430) — inlined
		// below. Projectile weapons are refused in Player::fireWeapon.
		// rockView recoil skipped (camera-rock system absent) :326-332.
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
		explodeOnMonster();                                      // :361
		if (totalDamage == 0 || hitType == 0) {                  // :363-365
			// ++counters[7]: run-stats absent, log only.
			std::fprintf(stderr, "[combat] miss counter++\n");
		}
	} else if (stage == 1 && *env_.gameTime >= nextStageTime) {  // :368-416
		// dynamite flags (:369-370) and targetType-9/17 executeTile (:375-378)
		// have no Stage-1 consumers; BFG check (:379-381) is out of scope.
		if (targetType == Enums::ET_MONSTER) {
			curTarget->info &= ~0x4000000;                       // :372-374
		}
		if (targetKilled ||
		    (targetType == Enums::ET_MONSTER &&
		     targetMonster->ce.getStat(Enums::STAT_HEALTH) <= 0)) {
			env_.game->diedMonster(curTarget, true);             // :382-386
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
	const int w = env_.player->ce.weapon * 9;                        // :866
	const int td = worldDistToTileDist(
		env_.game->entityDistFrom(e, env_.player->destX, env_.player->destY)); // :867
	const int8_t rangeLo = env_.tables->weaponData[w + kFieldRangeMin];
	const int8_t rangeHi = env_.tables->weaponData[w + kFieldRangeMax];   // :868
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
	// chainsaw-miss suppression (:892-894): dead code without the chainsaw.
	if (curTarget != nullptr && curTarget->monster != nullptr &&
	    curTarget->def != nullptr &&
	    curTarget->def->eType == Enums::ET_MONSTER &&
	    (curTarget->info & Entity::kInfoOnActiveList) == 0) {
		env_.game->activate(curTarget, true, false, true, true);   // :895-897
	}
	if (hitType == 0) {
		// rocket-splash radiusHurtEntities tail (:899-901): projectiles deferred.
		return;                                          // :898-903
	}
	if (targetType == Enums::ET_MONSTER) {               // :904-931
		if (totalDamage > 0) {
			// checkMonsterFX skipped: status effects absent (:168-176,:906).
			env_.game->painMonster(curTarget, totalDamage, attackerWeaponId); // :907
			// blood particles (:908-910), knockback (:911-921), splash radius
			// (:922-924) and negative-damage healing cap (:926-931) skipped:
			// systems absent.
		}
	} else if (targetType == Enums::ET_ATTACK_INTERACTIVE) {  // :933-937
		std::fprintf(stderr, "[combat] ATTACK_INTERACTIVE pain branch deferred\n");
	} else if (targetType == Enums::ET_CORPSE) {             // :938-945 gib
		std::fprintf(stderr, "[combat] corpse gib branch deferred (chainsaw-only)\n");
	}
}

} // namespace newcore
