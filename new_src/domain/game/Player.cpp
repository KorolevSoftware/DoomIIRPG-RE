#include "domain/game/Player.h"

#include <algorithm>
#include <cstdio>

#include "domain/game/Combat.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"
#include "domain/game/WeaponTable.h"

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

// src/Player.cpp:970-1063. The isFamiliar mirror arrays (weaponsCopy /
// inventoryCopy / ammoCopy, :995-1000,:1010,:1036) are not ported — no
// familiar system exists — so every branch takes the real-array side.
bool Player::give(int kind, int slot, int amount, bool quiet) {
	if (amount == 0) return false;
	switch (kind) {
	case 1: { // weapon bitmask
		int bit = 1 << (slot & 0xFF);
		// Sentry-bot pre-step (:978-984): a new bot refills its ammo pool and
		// evicts the previously owned bot bits, so the acquisition below always
		// counts as new. Runs BEFORE `isNew` is sampled, as in legacy.
		if ((bit & Enums::WP_SENTRY_BOT_MASK) != 0 && amount > 0) {
			give(Enums::ITEM_CLASS_AMMO, Enums::AMMO_SENTRY_BOT, 100, true);
			weapons &= ~Enums::WP_SENTRY_BOT_MASK;
		}
		bool isNew = (weapons & bit) == 0;  // :985
		if (amount < 0) {
			weapons &= ~bit;
			if (slot == weapon) {              // legacy has ONE active-weapon
				weapon = -1;                   // field, player->ce->weapon; the
				ce.weapon = -1;                // rewrite mirrors it in both
			}
			// Legacy reselects via selectNextWeapon() (:991-992), which is not
			// ported; the -1 fallback stands in for it.
			return true;
		}
		weapons |= bit;                        // :1000
		if (!quiet) {
			// showWeaponHelp(slot, false) (:1001-1003) — no help-popup
			// plumbing for gameplay grants (spec §5).
		}
		if (isNew) {                           // :1004-1006
			selectWeapon(slot, defs_ != nullptr
				? defs_->find(Enums::ET_ITEM, Enums::ITEM_CLASS_WEAPON, slot)
				: nullptr);
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
		// Bottled water is not stored: the whole NEW total (not the delta) is
		// converted to holy water at x20 and inventory[13] stays 0
		// (:1023-1025). Legacy quirk, reproduced verbatim.
		if (slotIdx == Enums::INV_BOTTLED_WATER) {
			give(Enums::ITEM_CLASS_AMMO, Enums::AMMO_HOLY_WATER, n * 20, true);
		} else {
			inventory[slotIdx] = (int16_t)n;
		}
		return true;
	}
	case 2: { // ammo
		if (slot < 0 || slot >= 9) return false;
		int n = amount + ammo[slot];
		if (slot == 6) n = std::min(n, 5);
		else n = std::min(n, 100);
		if (n < 0) return false;
		ammo[slot] = (int16_t)n;
		// hud->repaintFlags |= 0x4 (:1051): the HUD model is rebuilt per frame.
		return true;
	}
	case 3: { // health
		return addHealth(amount);
	}
	default:
		return false;
	}
}

void Player::selectWeapon(int i, const EntityDef* def) {
	if (i != Enums::WP_ITEM) {                 // (src/Player.cpp:138-141)
		weapons &= ~(1 << Enums::WP_ITEM);
		ammo[Enums::AMMO_ITEM] = 0;
	}
	weapon = i;                                // (:153) ce->weapon = i; the
	ce.weapon = i;                             // rewrite mirrors both fields
	activeWeaponDef = def;                     // (:160)
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
	const WeaponDef& wdef = combat.weaponDef(weapon);
	const int ammoType = wdef.ammoType;
	const int usage = wdef.ammoUsage;
	if (ammoType != 0) {                                       // :782-796
		const int have = ammo[ammoType];
		if (usage > 0 && have - usage < 0) {                   // :784
			combat.centerMessage(weapon == 13 ? 117 : (have == 0 ? 115 : 116));
			return false;
		}
	}
	// Projectile weapons are out of Stage-1 scope (research §6.3): refuse
	// like the soul-cube guard rather than mis-firing instant hitscan damage.
	const int proj = wdef.projType;
	if (proj != 0) {
		std::fprintf(stderr, "[combat] projectile weapon %d unsupported (proj=%d)\n",
			weapon, proj);
		return false;
	}
	combat.performAttack(target, x, y, false);                 // :797
	return true;                                               // :798
}

} // namespace newcore