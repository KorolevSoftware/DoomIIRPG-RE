#include "domain/game/CombatEntity.h"

#include <algorithm>
#include <cstdio>

#include "domain/game/Combat.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"
#include "domain/game/Player.h"

namespace newcore {

CombatEntity::CombatEntity(int health, int armor, int defense, int strength, int accuracy, int agility) {
	setStat(1, health);
	setStat(0, health);
	setStat(2, armor);
	setStat(3, defense);
	setStat(4, strength);
	setStat(5, accuracy);
	setStat(6, agility);
}

void CombatEntity::clone(const CombatEntity& other) {
	stats[1] = other.stats[1];
	stats[2] = other.stats[2];
	stats[3] = other.stats[3];
	stats[4] = other.stats[4];
	stats[5] = other.stats[5];
	stats[6] = other.stats[6];
	setStat(0, other.stats[0]);
	weapon = other.weapon;
}

int CombatEntity::getStatPercent(int i) {
	return (stats[i] << 8) / 100;
}

int CombatEntity::getIQPercent() {
	return std::max(0, std::min((stats[7] - 100) * 100 / 100, 100));
}

int CombatEntity::setStat(int i, int value) {
	if (value < 0) value = 0;
	switch (i) {
	case 1: break;
	case 0: value = std::min(value, getStat(1)); break;
	default: value = std::min(value, 255); break;
	}
	return stats[i] = value;
}

int CombatEntity::addStat(int i, int delta) {
	delta += stats[i];
	setStat(i, delta);
	return stats[i];
}

int CombatEntity::calcXP() const {
	return ((stats[3] + stats[4]) * 5 + stats[5] * 6 + stats[1] * 5 + 49) / 50;
}

// ---- verbatim combat math (src/CombatEntity.cpp:130-378) ----

void CombatEntity::calcCombat(Combat& c, CombatEntity& attackerCe, Entity* targetEnt,
                              bool vsPlayer, int worldDist, int targetSubParm) {
	// src/CombatEntity.cpp:130-155.
	c.crDamage = 0;                                              // :133
	c.crArmorDamage = 0;                                         // :134
	CombatEntity* defenderCe;
	if (!vsPlayer) {
		defenderCe = &targetEnt->monster->ce;                    // :136-138
	} else {
		defenderCe = &c.env().player->ce;                        // :140 (monster attacks, Stage 2)
	}
	int hit = calcHit(c, attackerCe, *defenderCe, vsPlayer, worldDist, false); // :142
	if ((hit & 0x1007) == 0) {                                   // :143-145
		return;
	}
	// usedChainsaw(true) hook (:146-148) replaced by a log — no chainsaw on
	// the map00 route.
	if (!vsPlayer && attackerCe.weapon == 1) {
		std::fprintf(stderr, "[combat] usedChainsaw deferred\n");
	}
	int dmg = calcDamage(c, attackerCe, targetEnt, *defenderCe, vsPlayer, targetSubParm); // :149
	if (dmg == 0 && c.crArmorDamage == 0) {                      // :150-152
		hit |= 0x100;
	}
	c.crFlags = hit;                                             // :153
	c.crDamage = dmg;                                            // :154
}

int CombatEntity::calcHit(Combat& c, CombatEntity& attackerCe, CombatEntity& defenderCe,
                          bool vsPlayer, int worldDist, bool zoomB2) {
	// src/CombatEntity.cpp:157-298. Omitted: oneShotCheat (:169-171), the
	// sniper-zoom pixel-bbox block (:173-216), the punch branches
	// (:233-238 crFlags 0x10 gate, :253-261 punchingMonster).
	const int attackerWeapon = c.attackerWeapon;                 // :159
	const int attackerWeaponId = c.attackerWeaponId;             // :160
	Entity* curTarget = c.curTarget;                             // :161
	int eType;
	if (curTarget == nullptr || curTarget->def == nullptr) {
		eType = curTarget == nullptr ? 1 : 0;                    // :163-168 (null def = world slot)
	} else {
		eType = curTarget->def->eType;
	}
	// Zoom path (weapon mask 0x200, :173-216): pixel-bbox body-part test is
	// unsupported in the rewrite; log if ever reached.
	if (Combat::checkWeaponMask(attackerWeaponId, 0x200)) {
		std::fprintf(stderr, "[combat] zoom bbox path unsupported\n");
		return c.crFlags;
	}

	bool farCap = false;                                         // :218
	int td = c.worldDistToTileDist(worldDist);                   // :219
	const auto& weapons = c.weaponTable();                       // int8 stride-9 table
	int n7;
	if (td < weapons[attackerWeapon + Combat::kFieldRangeMin]) {           // :221-223
		n7 = weapons[attackerWeapon + Combat::kFieldRangeMin] - td;
	} else if (td > weapons[attackerWeapon + Combat::kFieldRangeMax]) {    // :224-229
		if (attackerWeaponId == 7 || attackerWeaponId == 2) {
			farCap = true;
		}
		n7 = td - weapons[attackerWeapon + Combat::kFieldRangeMax];
	} else {
		n7 = 0;                                                  // :230-232
	}
	// punch branch n7=0 (:233-235) omitted (crFlags 0x10 never set).
	if ((weapons[attackerWeapon + Combat::kFieldRangeMin] ==
	     weapons[attackerWeapon + Combat::kFieldRangeMax] ||
	     (c.crFlags & 0x40) != 0) && n7 > 0) {                    // :236-238
		return c.crFlags |= 0x400;
	}
	if (zoomB2 || attackerCe.weapon == 13) {                     // :239-241
		return c.crFlags |= 0x1;
	}
	int acc = attackerCe.getStat(Enums::STAT_ACCURACY);          // :242
	int agi = defenderCe.getStat(Enums::STAT_AGILITY);           // :243
	if (!Combat::checkWeaponMask(attackerWeaponId, 0x800)) {     // :244-246
		agi = agi * 96 >> 8;
	}
	c.crHitChance = ((acc - agi) << 8) / 100;                    // :247
	c.crHitChance -= 16 * n7;                                    // :248
	if (c.crHitChance < 1) {                                     // :249-251
		c.crHitChance = 1;
	}
	int roll = (int)c.nextByte();                                // :252
	// punchingMonster roll override (:253-261) omitted.
	constexpr int kPlayerMissLimit = 1;                          // :262 n8
	if (((!vsPlayer && c.playerMissRepetition < kPlayerMissLimit) ||
	     (vsPlayer && c.monsterMissRepetition < 2)) &&
	    roll > c.crHitChance &&
	    (vsPlayer || c.loadMapID < 8 || c.tileDist > 1)) {       // :263-271
		if (vsPlayer) ++c.monsterMissRepetition;
		else ++c.playerMissRepetition;
		return c.crFlags;
	}
	// Defender dodge statusEffects[18] branch (:272-278) omitted — Player has
	// no status effects in the rewrite yet.
	if (vsPlayer) {                                              // :279-284
		c.monsterMissRepetition = 0;
	} else {
		c.playerMissRepetition = 0;
	}
	if (farCap) {                                                // :285-287
		return c.crFlags |= 0x4;
	}
	if (!vsPlayer || c.difficulty() == 4) {                      // :288-290
		c.crCritChance = c.crHitChance / 20;
	} else {                                                     // :291-293
		c.crCritChance = 0;
	}
	if ((int)c.nextByte() < c.crCritChance) {                    // :294-296
		return c.crFlags |= 0x2;
	}
	return c.crFlags |= 0x1;                                     // :297
}

int CombatEntity::calcDamage(Combat& c, CombatEntity& attackerCe, Entity* targetEnt,
                             CombatEntity& defenderCe, bool vsPlayer, int parm) {
	// src/CombatEntity.cpp:300-378. Omitted: buffs (:311-313,:337-339,
	// :367-370) and the familiar split (:357-366 keeps only the normal armor
	// path). parm is the legacy n (unused by this body — kept for signature
	// parity with calcCombat's targetSubType pass-through).
	(void)parm;
	const int weapon = attackerCe.weapon * 9;                    // :302
	const auto& weapons = c.weaponTable();
	int dmgStrMin = weapons[weapon + Combat::kFieldStrMin] & 0xFF;   // :303
	int dmgStrMax = weapons[weapon + Combat::kFieldStrMax] & 0xFF;   // :304

	if (attackerCe.weapon == 13) {                               // :306-309 soul cube x2
		dmgStrMin *= 2;
		dmgStrMax *= 2;
	}

	// buffs[0] early-out (:311-313) omitted.
	if (c.difficulty() == 4) {                                   // :314-316 cut BEFORE crit doubling
		dmgStrMin -= dmgStrMin >> 2;
	}
	if ((c.crFlags & 0x2) != 0 || (c.crFlags & 0x2000) != 0) {   // :317-319
		dmgStrMin = dmgStrMax * 2;
	} else if ((c.crFlags & 0x4) != 0) {                         // :320-322
		dmgStrMin = dmgStrMax / 2;
	} else if (dmgStrMax != dmgStrMin) {                         // :323-325
		dmgStrMin += (int)c.nextByte() % (dmgStrMax - dmgStrMin);
	}
	if ((c.crFlags & 0x20) == 0) {                               // :326-333 strength bonus,
		if (!vsPlayer) {                                         // monster-vs-player asymmetry
			dmgStrMin += 3 * (attackerCe.getStatPercent(Enums::STAT_STRENGTH) * dmgStrMin >> 8);
		} else {
			dmgStrMin += attackerCe.getStatPercent(Enums::STAT_STRENGTH) * dmgStrMin >> 8;
		}
	}
	int armorDamage = 0;                                         // :334
	int damage;
	if (!vsPlayer) {                                             // :336-355 player attack
		// buffs[8] damage boost (:337-339) omitted.
		short weaponWeakness = c.getWeaponWeakness(attackerCe.weapon,
			targetEnt->def->eSubType, targetEnt->def->parm);      // :340
		if (attackerCe.weapon == 2) {                            // :341-353 holy water cases
			if (targetEnt->def->eSubType == 2) {
				if (c.difficulty() == 4) weaponWeakness = (short)(weaponWeakness << 1);
				else weaponWeakness = (short)(weaponWeakness << 2);
			} else if (targetEnt->def->eSubType == 0) {
				weaponWeakness = 0;
			}
		}
		damage = weaponWeakness * dmgStrMin >> 8;                // :354
	} else {                                                     // :356-371 monster attack
		armorDamage = std::min(((171 * dmgStrMin >> 8) + 1) / 2,   // :357-359 normal armor split
		                       defenderCe.getStat(Enums::STAT_ARMOR));
		damage = dmgStrMin - 2 * armorDamage;
		// familiar branches (:361-366) and buffs[9] absorb (:367-370) omitted.
	}
	int crDamage = damage -
	    (defenderCe.getStatPercent(Enums::STAT_DEFENSE) * damage >> 8);  // :372
	// oneShotCheat (:373-375) omitted.
	c.crArmorDamage = armorDamage;                               // :376
	return c.crDamage = crDamage;                                // :377
}

} // namespace newcore