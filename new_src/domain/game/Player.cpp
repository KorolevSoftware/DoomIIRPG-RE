#include "domain/game/Player.h"

#include <algorithm>
#include <cstdio>

#include "domain/game/Combat.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"

namespace newcore {

// 8 direction vectors (dx,dy) at 45 deg, canvas units (legacy Canvas.h:108).
const int Player::kViewStepValues[16] = {
	64, 0,  64, -64,  0, -64,  -64, -64,
	-64, 0,  -64, 64,  0, 64,   64, 64
};

void Player::startRotation() {
	animPos = (64 + animFrames - 1) / animFrames;     // 7
	animAngle = (256 + animFrames - 1) / animFrames; // 26
	// No angle normalization here: legacy startRotation never writes
	// viewAngle/destAngle (src/MovementController.cpp:226-282); both stay RAW
	// accumulated ints so plain <,>,== comparisons work across the 0/1023 wrap.
}

void Player::finishRotation() {
	viewAngle = destAngle; // snap (legacy relies on arrival equality, src/MovementController.cpp:514)
	int idx = ((destAngle & 0x3FF) >> 7) << 1;
	viewStepX = kViewStepValues[idx + 0];
	viewStepY = kViewStepValues[idx + 1];
	// right vector = direction - 90 deg.
	int r = (destAngle - 256) & 0x3FF;
	idx = (r >> 7) << 1;
	viewRightStepX = kViewStepValues[idx + 0];
	viewRightStepY = kViewStepValues[idx + 1];
	viewX = destX;
	viewY = destY;
	viewZ = destZ;
	zStep = 0;
}

bool Player::updateView() {
	bool moved = false;
	if (viewX < destX) { viewX += animPos; if (viewX > destX) viewX = destX; moved = true; }
	else if (viewX > destX) { viewX -= animPos; if (viewX < destX) viewX = destX; moved = true; }
	if (viewY < destY) { viewY += animPos; if (viewY > destY) viewY = destY; moved = true; }
	else if (viewY > destY) { viewY -= animPos; if (viewY < destY) viewY = destY; moved = true; }
	if (zStep != 0) {
		if (viewZ < destZ) { viewZ += zStep; if (viewZ > destZ) viewZ = destZ; moved = true; }
		else if (viewZ > destZ) { viewZ -= zStep; if (viewZ < destZ) viewZ = destZ; moved = true; }
	} else {
		viewZ = destZ;
	}
	// Angle: linear march toward destAngle with overshoot clamp, exactly like
	// legacy (src/MovementController.cpp:455-466). REGRESSION GUARD: never
	// mask or shortest-arc here. viewAngle/destAngle are RAW accumulated ints
	// (turns do destAngle += ±256 unmasked, src/PlayingInputHandler.cpp:118),
	// so raw equality gates work across the 0/1023 wrap: turning right from
	// 0 gives dest=-256 and must step down to -256. Masking to [0,1023] here
	// produced view=-256 vs dest=768 — masked code saw "arrived" while the
	// raw gate (GameContext::handlePlayingAction) blocked input forever.
	if (viewAngle < destAngle) {
		viewAngle += animAngle;
		if (viewAngle > destAngle) viewAngle = destAngle;
		moved = true;
	} else if (viewAngle > destAngle) {
		viewAngle -= animAngle;
		if (viewAngle < destAngle) viewAngle = destAngle;
		moved = true;
	}
	return !moved; // arrived when nothing moved
}

bool Player::attemptMove(int tx, int ty) {
	destX = tx;
	destY = ty;
	// Height interpolation: recompute per frame would need terrain access;
	// zStep is set by caller via setDestHeight + setZStep.
	startRotation();
	return true;
}

void Player::reset() {
	baseCe.stats[Enums::STAT_MAX_HEALTH] = 100;
	baseCe.stats[Enums::STAT_ARMOR] = 0;
	baseCe.stats[Enums::STAT_DEFENSE] = 5;
	baseCe.stats[Enums::STAT_STRENGTH] = 10;
	baseCe.stats[Enums::STAT_ACCURACY] = 100;
	baseCe.stats[Enums::STAT_AGILITY] = 100;
	baseCe.setStat(Enums::STAT_HEALTH, 100);
	ce.clone(baseCe);
	weapon = -1;
	ce.weapon = -1;                    // legacy single active-weapon field mirror
	activeWeaponDef = nullptr;
	weapons = 0;
	for (auto& v : inventory) v = 0;
	for (auto& v : ammo) v = 0;
	level = 1;
	currentXP = 0;
	nextLevelXP = calcLevelXP(level);
	xpGained = 0;
	disabledWeapons = 0;
	facingEntity = nullptr;
	god = false;
	give(0, 18, 1); // journal
}

bool Player::give(int kind, int slot, int amount) {
	if (amount == 0) return false;
	switch (kind) {
	case 1: { // weapon bitmask
		int bit = 1 << (slot & 0xFF);
		if (amount < 0) {
			weapons &= ~bit;
			if (slot == weapon) {              // legacy has ONE active-weapon
				weapon = -1;                   // field, player->ce->weapon; the
				ce.weapon = -1;                // rewrite mirrors it in both
			}
			return true;
		}
		bool had = (weapons & bit) != 0;
		weapons |= bit;
		if (!had && weapon == -1) {
			weapon = slot;                     // auto-equip keeps ce.weapon in
			ce.weapon = slot;                  // lockstep (Combat.cpp:126 reads it)
		}
		return true;
	}
	case 0: { // inventory slot
		int slotIdx = slot - 0;
		if (slotIdx < 0 || slotIdx >= 26) return false;
		int n = amount + inventory[slotIdx];
		if (slotIdx == 24) n = std::min(n, 9999);
		else n = std::min(n, 999);
		if (n < 0) return false;
		inventory[slotIdx] = (int16_t)n;
		return true;
	}
	case 2: { // ammo
		if (slot < 0 || slot >= 9) return false;
		int n = amount + ammo[slot];
		if (slot == 6) n = std::min(n, 5);
		else n = std::min(n, 100);
		if (n < 0) return false;
		ammo[slot] = (int16_t)n;
		return true;
	}
	case 3: { // health
		return addHealth(amount);
	}
	default:
		return false;
	}
}

bool Player::requireItem(int kind, int slot, int min, int max) const {
	if (kind != 1) {
		return kind == 0 && slot >= 0 && slot < 26 &&
			inventory[slot] >= min && inventory[slot] <= max;
	}
	int bit = 1 << slot;
	if (max != 0) return (weapons & bit) != 0;
	return (weapons & bit) == 0;
}

bool Player::addHealth(int amount) {
	int stat = ce.getStat(Enums::STAT_HEALTH);
	int stat2 = ce.getStat(Enums::STAT_MAX_HEALTH);
	if (amount > 0) {
		if (stat == stat2) return false;
	} else if (god) {
		return false;
	}
	ce.addStat(Enums::STAT_HEALTH, amount);
	return true;
}

// ---- XP / leveling ----

void Player::addXP(int xp) {
	// src/Player.cpp:264-281 state half (message composition moved to
	// Game::awardKillXP — spec deviation 14; counters[5] run-stat absent).
	currentXP += xp;
	xpGained += xp;
	while (currentXP >= nextLevelXP) {
		addLevel();
	}
}

int Player::calcLevelXP(int n) const {
	return 500 * n + 100 * ((n - 1) * (n - 1) * (n - 1) + (n - 1));   // src/Player.cpp:360-362
}

void Player::addLevel() {
	// src/Player.cpp:283-307,:342 subset. Presentation (msgs 104-110), the
	// level-up sound/dialog and the modifyStat DEF/STR/ACC/AGI bumps are
	// deferred.
	level++;
	nextLevelXP = calcLevelXP(level);                          // :288-289
	int n = 10;
	int stat = baseCe.getStat(Enums::STAT_MAX_HEALTH);         // :297-298
	if (stat + n > 999) n = 999 - stat;                        // :299-301
	if (n != 0) baseCe.setStat(Enums::STAT_MAX_HEALTH, stat + n);   // :302-303
	ce.setStat(Enums::STAT_HEALTH, ce.getStat(Enums::STAT_MAX_HEALTH)); // :342 refill
	std::fprintf(stderr, "[player] level %d (stat bumps deferred)\n", level);
}

bool Player::fireWeapon(Combat& combat, Entity* target, int x, int y) {
	// src/Player.cpp:754-799. The weaponDown / lower-raise lerp system is
	// absent from the rewrite (:761-771 skipped with citation).
	// Explicit unowned/no-weapon guard first — legacy reads 1 << -1 (UB).
	if (weapon < 0 || (weapons & (1 << weapon)) == 0) return false;
	if (weapon == 13 /*WP_SOUL_CUBE*/ &&
	    (target == nullptr || target->monster == nullptr)) {
		return false;                                          // :757-759
	}
	if (disabledWeapons != 0 && (weapons & (1 << weapon)) == 0) {
		return false;                                          // :761-763 op-60 mask
	}
	// Target flag clear (:773-775): flags &= 0xfff7 clears bit 0x8 only,
	// MFLAG_NOACTIVATE (src/Player.cpp:774, new_src/domain/game/Enums.h:279).
	if (target != nullptr && target->monster != nullptr) {
		target->monster->flags &= ~Enums::MFLAG_NOACTIVATE;
	}
	// Chainsaw gib branch (:777-779): WP_CHAINSAW is unreachable on the
	// map00 route; log if ever seen instead of porting the gib path.
	if (weapon == 1 && target != nullptr && target->isCorpse()) {
		std::fprintf(stderr, "[combat] chainsaw gib path deferred\n");
	}
	const int ammoType = combat.weaponField(weapon, Combat::kFieldAmmoType);
	const int usage = combat.weaponField(weapon, Combat::kFieldAmmoUsage);
	if (ammoType != 0) {                                       // :782-796
		const int have = ammo[ammoType];
		if (usage > 0 && have - usage < 0) {                   // :784
			combat.centerMessage(weapon == 13 ? 117 : (have == 0 ? 115 : 116));
			return false;
		}
	}
	// Projectile weapons are out of Stage-1 scope (research §6.3): refuse
	// like the soul-cube guard rather than mis-firing instant hitscan damage.
	const int proj = combat.weaponField(weapon, Combat::kFieldProjType);
	if (proj != 0) {
		std::fprintf(stderr, "[combat] projectile weapon %d unsupported (proj=%d)\n",
			weapon, proj);
		return false;
	}
	combat.performAttack(target, x, y, false);                 // :797
	return true;                                               // :798
}

} // namespace newcore