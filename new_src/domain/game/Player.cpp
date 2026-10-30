#include "domain/game/Player.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>

#include "domain/game/Combat.h"
#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/WeaponTable.h"
#include "io/Localization.h"
#include "ui/Hud.h"

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
	int step = animPos;
	int ang = animAngle;
	if (statusEffects[2] > 0) {   // haste x1.5 (src/MovementController.cpp:405-410; knockbackDist is always 0)
		step += step / 2;
		ang += ang / 2;
	}
	if (viewX < destX) { viewX += step; if (viewX > destX) viewX = destX; moved = true; }
	else if (viewX > destX) { viewX -= step; if (viewX < destX) viewX = destX; moved = true; }
	if (viewY < destY) { viewY += step; if (viewY > destY) viewY = destY; moved = true; }
	else if (viewY > destY) { viewY -= step; if (viewY < destY) viewY = destY; moved = true; }
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
		viewAngle += ang;
		if (viewAngle > destAngle) viewAngle = destAngle;
		moved = true;
	} else if (viewAngle > destAngle) {
		viewAngle -= ang;
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
	foundSecretsLevels = 0;            // src/Player.cpp:557
	hackedVendingMachines = 0;         // src/Player.cpp:506-518: every tries field = 4
	vendingMachineHackTriesLeft1 = 0;
	vendingMachineHackTriesLeft2 = 0;
	for (int i = 0; i < 9; ++i) {
		vendingMachineHackTriesLeft1 += 4;
		vendingMachineHackTriesLeft2 += 4;
		if (i < 8) {
			vendingMachineHackTriesLeft1 <<= 3;
			vendingMachineHackTriesLeft2 <<= 3;
		}
	}
	disabledWeapons = 0;
	facingEntity = nullptr;
	god = false;
	give(0, 18, 1); // journal
	numbuffs = 0;                      // src/Player.cpp:535-544
	for (auto& v : buffs) v = 0;
	numStatusEffects = 0;
	for (auto& v : statusEffects) v = 0;
	// src/Player.cpp:558-563, after the inventory clear so the credits stick.
	// shortcut: the difficulty == 2 DEFENSE 0 override is skipped (Player has
	// no Game access); add it when a difficulty other than 1 becomes selectable.
	setStatsAccordingToCharacterChoice();
	updateStats();
	completedLevels = 0;
	highestMap = 1;
	moves = 0;
	currentGrades = 0;
	bestGrades = 0;
	chainsawStrengthBonusCount = 0;    // src/Player.cpp:519
	gameCompleted = false;             // :572
}

void Player::setStatsAccordingToCharacterChoice() {
	int n = 0;
	switch (characterChoice) {
	case 1:
		baseCe.setStat(Enums::STAT_DEFENSE, 8);
		baseCe.setStat(Enums::STAT_STRENGTH, 9);
		baseCe.setStat(Enums::STAT_ACCURACY, 97);
		baseCe.setStat(Enums::STAT_AGILITY, 12);
		baseCe.setStat(Enums::STAT_IQ, 110);
		n = 30;
		break;
	case 2:
		baseCe.setStat(Enums::STAT_DEFENSE, 12);
		baseCe.setStat(Enums::STAT_STRENGTH, 14);
		baseCe.setStat(Enums::STAT_ACCURACY, 92);
		baseCe.setStat(Enums::STAT_AGILITY, 6);
		baseCe.setStat(Enums::STAT_IQ, 100);
		n = 10;
		break;
	case 3:
		baseCe.setStat(Enums::STAT_DEFENSE, 8);
		baseCe.setStat(Enums::STAT_STRENGTH, 8);
		baseCe.setStat(Enums::STAT_ACCURACY, 87);
		baseCe.setStat(Enums::STAT_AGILITY, 6);
		baseCe.setStat(Enums::STAT_IQ, 150);
		n = 80;
		break;
	}
	give(0, 24, n, true);
}

void Player::levelInit() {
	moves = 0;
	if (clock_) turnTime = *clock_;
	if (getHealth() == 0) ce.setStat(Enums::STAT_HEALTH, 1);
}

int Player::modifyStat(int n, int n2) {
	int before;
	int after;
	if (n == 0) {
		// shortcut: painEvent on n2 < 0 (:229-231) is not ported; no caller
		// passes n 0 yet (vending IQ bonus only).
		before = getHealth();
		addHealth(n2);
		after = getHealth();
	} else {
		before = baseCe.getStat(n);
		const int cap = (n == 7) ? 200 : 99;
		if (before + n2 > cap) n2 = cap - before;
		if (n2 != 0) baseCe.setStat(n, before + n2);
		updateStats();
		after = baseCe.getStat(n);
	}
	return after - before;
}

bool Player::vendingMachineIsHacked(int id) const {
	return (hackedVendingMachines & 1 << id) == 1 << id;
}

void Player::setVendingMachineHack(int id) {
	hackedVendingMachines |= 1 << id;
}

int Player::getVendingMachineTriesLeft(int id) const {
	id = std::max(std::min(id, 18), 1);
	int n;
	if (id > 9) {
		n = vendingMachineHackTriesLeft2;
		id -= 9;
	} else {
		n = vendingMachineHackTriesLeft1;
	}
	return (n >> (3 * (id - 1))) & 0x7;
}

void Player::removeOneVendingMachineTry(int id) {
	id = std::max(std::min(id, 18), 1);
	if (getVendingMachineTriesLeft(id) > 0) {
		if (id > 9) {
			id -= 9;
			vendingMachineHackTriesLeft2 -= 1 << (3 * (id - 1));
		} else {
			vendingMachineHackTriesLeft1 -= 1 << (3 * (id - 1));
		}
	}
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

void Player::stripInventoryForViosBattle() {   // src/Player.cpp:2664-2692
	weaponsCopy = 0;
	ammoCopy[3] = ammo[3];
	ammo[3] = 0;
	if ((weapons & 0x4) != 0) { give(1, 2, -1); weaponsCopy |= 0x4; }
	if ((weapons & 0x2000) != 0) { give(1, 13, -1); weaponsCopy |= 0x2000; }
	if ((weapons & 0x8) != 0) { give(1, 3, -1); weaponsCopy |= 0x8; }
	else if ((weapons & 0x10) != 0) { give(1, 4, -1); weaponsCopy |= 0x10; }
	else if ((weapons & 0x20) != 0) { give(1, 5, -1); weaponsCopy |= 0x20; }
	else if ((weapons & 0x40) != 0) { give(1, 6, -1); weaponsCopy |= 0x40; }
}

void Player::stripInventoryForTargetPractice() {   // :2694-2705
	currentWeaponCopy = weapon;
	std::copy(std::begin(ammo), std::end(ammo), ammoCopy);
	weaponsCopy = weapons;
	for (auto& v : ammo) v = 0;
	weapons = 0;
	give(1, 9, 1, true);
	give(2, 1, 8, true);
}

// forceRemoveFromScopeZoom (:2755) is done by the callers (TargetPractice::exit, EV_INVENTORY_OP 2).
void Player::restoreInventory() {   // :2707-2759
	if ((weapons & 0x1) != 0) {
		currentWeaponCopy = weapon;
		ammo[3] = ammoCopy[3];
		if ((weaponsCopy & 0x4) != 0) give(1, 2, 1, true);
		if ((weaponsCopy & 0x2000) != 0) give(1, 13, 1, true);
		const int16_t bot = ammo[7];
		int botSlot = -1;
		if ((weaponsCopy & 0x8) != 0) botSlot = 3;
		else if ((weaponsCopy & 0x10) != 0) botSlot = 4;
		else if ((weaponsCopy & 0x20) != 0) botSlot = 5;
		else if ((weaponsCopy & 0x40) != 0) botSlot = 6;
		if (botSlot != -1) {
			give(1, botSlot, 1, true);
			ammo[7] = bot;
		}
	} else {
		const bool hadWeapon9 = (weaponsCopy & 0x200) != 0;   // the practice weapon
		for (int i = 0; i < 9; ++i) {
			ammo[i] = 0;
			give(2, i, ammoCopy[i], true);
		}
		for (int j = 0; j < 15; ++j)
			if (((1 << j) & weaponsCopy) != 0) give(1, j, 1, true);
		if (!hadWeapon9) give(1, 9, -1);
	}
	// shortcut: selectNextWeapon not ported (src/Player.cpp:192-215), upgrade with it
	if (currentWeaponCopy < 0) return;
	selectWeapon(currentWeaponCopy, defs_ != nullptr
		? defs_->find(Enums::ET_ITEM, Enums::ITEM_CLASS_WEAPON, currentWeaponCopy)
		: nullptr);
}

void Player::usedChainsaw() {   // src/Player.cpp:2636-2652
	++chainsawStrengthBonusCount;
	if (chainsawStrengthBonusCount >= 30) {
		chainsawStrengthBonusCount %= 30;
		const int gained = modifyStat(Enums::STAT_STRENGTH, 2);
		if (gained != 0 && hud_ != nullptr && loc_ != nullptr) {
			std::string text = loc_->get(kTextMain, 241);
			std::string args[1] = { std::to_string(gained) };
			composeArgs(text, args, 1);
			hud_->addMessage(text, 0xAA000000, 3);
		}
	}
	if (hud_ != nullptr) hud_->startShake(clock_ != nullptr ? *clock_ : 0, 666, 2);   // startShake(666, 1, 0)
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

bool Player::addHealth(int amount, bool showMessage) {
	int stat = ce.getStat(Enums::STAT_HEALTH);
	int stat2 = ce.getStat(Enums::STAT_MAX_HEALTH);
	if (amount > 0) {
		if (stat == stat2) return false;
	} else if (god) {
		return false;
	}
	ce.addStat(Enums::STAT_HEALTH, amount);
	const int now = ce.getStat(Enums::STAT_HEALTH);
	if (showMessage && now > stat && hud_ != nullptr && loc_ != nullptr) {   // src/Player.cpp:445-449
		std::string text = loc_->get(kTextMain, 111);   // "Gained %01 health"
		std::string args[1] = { std::to_string(now - stat) };
		composeArgs(text, args, 1);
		hud_->addMessage(text, 0xAA000000, 0);          // Hud::addMessage(short) = flags 0
	}
	return true;
}

UseResult Player::useItem(int n) {
	if (n < 0 || n >= 26) return UseResult::Refused;   // rewrite bound; legacy indexes unchecked
	if (inventory[n] == 0 && n != Enums::INV_OTHER_HOLY_WATER) return UseResult::Refused;   // src/Player.cpp:804-806
	bool turn = true, consume = true;                  // b, b2 (:807-808)
	switch (n) {
	case 16: if (!addHealth(20)) return UseResult::Refused; break;   // :810-815
	case 17: if (!addHealth(80)) return UseResult::Refused; break;   // :816-821
	case 11: if (!addArmor(50)) return UseResult::Refused; break;    // :822-827
	case 12: if (!addArmor(10)) return UseResult::Refused; break;    // :828-833
	case 7:                                                           // :834-840
		if (!addStatusEffect(6, 25, 31)) return UseResult::Refused;
		translateStatusEffects();
		break;
	case 6:                                                           // :841-847
		if (!addStatusEffect(4, 25, 31)) return UseResult::Refused;
		translateStatusEffects();
		break;
	case 1:                                                           // :848-854
		if (!addStatusEffect(7, 5, 31)) return UseResult::Refused;
		translateStatusEffects();
		break;
	case 3: {                                                         // :855-868
		bool any = false;
		if (addStatusEffect(8, 25, 31)) any = true;
		if (addStatusEffect(5, 20, 31)) any = true;
		if (!any) return UseResult::Refused;
		translateStatusEffects();
		break;
	}
	case 2:                                                           // :869-876
		if (!addStatusEffect(9, 0, 11)) return UseResult::Refused;
		removeStatusEffect(13);
		translateStatusEffects();
		break;
	case 9:                                                           // :877-883
		if (!addStatusEffect(8, 100, 31)) return UseResult::Refused;
		translateStatusEffects();
		break;
	case 8:                                                           // :884-892
		if (statusEffects[2] != 0) return UseResult::Refused;
		turn = false;
		addStatusEffect(2, 0, 21);
		translateStatusEffects();
		break;
	case 0:                                                           // :893-900
		if (!addStatusEffect(10, 20, 31)) return UseResult::Refused;
		translateStatusEffects();
		addHealth(20);
		break;
	case 5:                                                           // :901-907
		if (!addStatusEffect(3, 5, 31)) return UseResult::Refused;
		translateStatusEffects();
		break;
	case 10: {                                                        // :908-927
		bool any = false;
		if (addStatusEffect(6, 25, 31)) any = true;
		if (addStatusEffect(8, 25, 31)) any = true;
		if (addStatusEffect(4, 25, 31)) any = true;
		if (addStatusEffect(7, 5, 31)) any = true;
		if (!any) return UseResult::Refused;
		translateStatusEffects();
		break;
	}
	case 4:                                                           // :928-934
		if (!addStatusEffect(12, 10, 11)) return UseResult::Refused;
		translateStatusEffects();
		break;
	case 22: {                                                        // :935-944
		const bool ok = addHealth(25) | addStatusEffect(11, 100, 3);  // bitwise: both run
		consume = false;
		if (!ok) return UseResult::Refused;
		ammo[Enums::AMMO_HOLY_WATER] -= 25;
		translateStatusEffects();
		break;
	}
	case 23: return UseResult::UsedNoTurn;   // :945-947, no sound/consume/turn
	default: return UseResult::Refused;      // 13, 18-21, 24, 25 (:948)
	}
	std::fprintf(stderr, "[sound] 1124\n");  // :952
	if (consume) --inventory[n];             // :953-955
	return turn ? UseResult::Used : UseResult::UsedNoTurn;
}

// ---- Status effects / buffs ----

namespace {
// COLD's render->startFogLerp (src/Player.cpp:1550-1552, :1583-1589) has no
// rewrite counterpart (fog is disabled, core/Main.cpp).
void logFogLerpOnce() {
	static bool logged = false;
	if (!logged) std::fprintf(stderr, "[buff] cold fog lerp not ported\n");
	logged = true;
}
} // namespace

void Player::advanceTurn() {
	++moves;                                                // :53
	// src/Player.cpp:50-92 subset. Deferred: totalMoves (:54),
	// COLD/DRUNK drain statusEffects[53] (:63-66) and FIRE -3 + msg 82/71
	// (:68-77) — no producer (EV_STATUS_EFFECT op 70 and monster fire effects
	// src/Combat.cpp:553,978,1135 unported) — inCombat decay (:79-81),
	// counters[3] (:83-85) and the automap exit (:88-90).
	updateStatusEffects();                                  // :55
	if (buffs[Enums::BUFF_REGEN] > 0) {                     // :59-61, silent
		addHealth(buffs[Enums::BUFF_MAX + Enums::BUFF_REGEN], false);
	}
	turnTime = clock_ != nullptr ? *clock_ : 0;             // :87 app->time
}

void Player::updateStats() {                                // src/Player.cpp:1448-1456
	ce.setStat(Enums::STAT_MAX_HEALTH, baseCe.getStat(Enums::STAT_MAX_HEALTH) + buffs[25]);
	ce.setStat(Enums::STAT_HEALTH, ce.getStat(Enums::STAT_HEALTH));   // re-clamp
	ce.setStat(Enums::STAT_STRENGTH, baseCe.getStat(Enums::STAT_STRENGTH) + buffs[20]);
	ce.setStat(Enums::STAT_ACCURACY, baseCe.getStat(Enums::STAT_ACCURACY) + buffs[22] - buffs[28]);
	ce.setStat(Enums::STAT_DEFENSE, baseCe.getStat(Enums::STAT_DEFENSE) + buffs[19]);
	ce.setStat(Enums::STAT_AGILITY, baseCe.getStat(Enums::STAT_AGILITY) + buffs[21]);
	ce.setStat(Enums::STAT_IQ, baseCe.getStat(Enums::STAT_IQ));
}

void Player::updateStatusEffects() {                        // src/Player.cpp:1458-1472
	if (numStatusEffects == 0) return;
	for (int i = 0; i < Enums::MAX_STATUS_EFFECTS; ++i) {
		if (statusEffects[0 + i] != 0) {
			// Legacy `turns <= 5 && turns == 1`: the first half is redundant.
			if (statusEffects[0 + i] <= 5 && statusEffects[0 + i] == 1) {
				removeStatusEffect(i);
			} else if (statusEffects[0 + i] != 0) {
				--statusEffects[0 + i];
			}
		}
	}
	translateStatusEffects();
}

void Player::translateStatusEffects() {                     // src/Player.cpp:1474-1531
	for (int i = 0; i < Enums::BUFF_MAX; ++i) {
		buffs[0 + i] = (buffs[15 + i] = 0);
	}
	numbuffs = 0;
	for (int j = 0; j < Enums::MAX_STATUS_EFFECTS; ++j) {
		const int n = statusEffects[0 + j];
		const int n2 = statusEffects[18 + j];
		if (n > 0) {
			switch (j) {
			case 14:
				buffs[20] -= (int16_t)n2;
				buffs[5] = (int16_t)n;
				buffs[19] -= (int16_t)n2;
				buffs[4] = (int16_t)n;
				buffs[29] -= (int16_t)(statusEffects[36 + j] * 4);
				buffs[14] = (int16_t)n;
				break;
			case 15:
			case 17:
				buffs[20] += (int16_t)n2;
				buffs[5] = (int16_t)n;
				buffs[19] += (int16_t)n2;
				buffs[4] = (int16_t)n;
				buffs[22] -= (int16_t)(n2 + (n2 / 2));
				buffs[7] = (int16_t)n;
				break;
			case 16:
				buffs[22] -= (int16_t)n2;
				buffs[7] = (int16_t)n;
				break;
			default:
				buffs[15 + j] += (int16_t)n2;
				buffs[0 + j] = (int16_t)n;
				break;
			}
		}
	}
	for (int k = 0; k < Enums::BUFF_MAX; ++k) {
		if (buffs[0 + k] > 0) {
			if (((1 << k) & Enums::BUFF_NO_AMOUNT) == 0 && buffs[15 + k] == 0) {
				buffs[0 + k] = 0;
			} else {
				++numbuffs;
			}
		}
	}
	updateStats();
}

void Player::removeStatusEffect(int n) {                    // src/Player.cpp:1534-1560
	if (n == Enums::STATUS_EFFECT_ALL) {
		numStatusEffects = 0;
		for (int i = 0; i < Enums::MAX_STATUS_EFFECTS; ++i) {
			statusEffects[0 + i] = 0;
			statusEffects[18 + i] = 0;
			statusEffects[36 + i] = 0;
		}
	} else {
		if (statusEffects[36 + n] == 0) return;
		if (n == Enums::STATUS_EFFECT_COLD) logFogLerpOnce();   // :1550-1552
		statusEffects[18 + n] = 0;
		statusEffects[36 + n] = 0;
		statusEffects[0 + n] = 0;
		--numStatusEffects;
	}
	std::fprintf(stderr, "[buff] -effect %d\n", n);
	translateStatusEffects();
}

bool Player::addStatusEffect(int n, int amount, int turns) {   // src/Player.cpp:1562-1595
	if (n == Enums::STATUS_EFFECT_FIRE) {                   // :1568-1573
		if (buffs[Enums::BUFF_ANTIFIRE] > 0) return false;
		std::fprintf(stderr, "[sound] 1034\n");
	}
	const int count = statusEffects[36 + n] + 1;
	if (count > 3 || ((n == 12 || n == 9) && count > 1)) {
		if (n == 14) statusEffects[0 + n] = turns;          // DISEASE refreshes (:1576-1578)
		return false;
	}
	if (count == 1) {
		++numStatusEffects;
		if (n == Enums::STATUS_EFFECT_COLD) logFogLerpOnce();   // :1583-1589
	}
	statusEffects[18 + n] += amount;
	statusEffects[0 + n] = turns;
	statusEffects[36 + n] = count;
	std::fprintf(stderr, "[buff] +effect %d amount %d turns %d (stack %d)\n", n, amount, turns, count);
	return true;
}

Player::PainBand Player::pain(int n) {
	if (god) return PainBand::None;                        // src/Player.cpp:654-656
	if (n == 0) return PainBand::None;                     // :672-674
	const int stat = ce.getStat(Enums::STAT_HEALTH);
	const int maxHp = ce.getStat(Enums::STAT_MAX_HEALTH);
	const int n2 = (stat << 16) / (maxHp << 8);            // :683-684
	const int n3 = ((stat - n) << 16) / (maxHp << 8);
	PainBand band = PainBand::None;
	if (n3 > 0) {                                          // :685-697
		if (n2 > 26 && n3 <= 26) {
			band = PainBand::NearDeath;
		} else if (n2 > 78 && n3 <= 78) {
			band = PainBand::LowHealth;
		} else if (n2 > 128 && n3 <= 128) {
			band = PainBand::HalfHealth;                   // helpBitmask & 0xC00 absent
		}
	}
	addHealth(-n);                                         // :701
	return band;
}

int Player::calcDamageDir(int x1, int y1, int angle, int x2, int y2) {
	// src/Player.cpp:576-622. Angles: E 0, NE 128, N 256, NW 384, W 512,
	// SW 640, S 768, SE 896 (y grows south); dirs DIR_NORTHWEST 3, DIR_SOUTHEAST 7.
	const int dx = x2 - x1;
	const int dy = y2 - y1;
	angle &= 1023;
	if (dx == 0 && dy == 0) return 3;
	int ang;
	if (dx > 0) {
		ang = (dy < 0) ? 128 : (dy > 0) ? 896 : 0;
	} else if (dx < 0) {
		ang = (dy < 0) ? 384 : (dy > 0) ? 640 : 512;
	} else {
		ang = (dy > 0) ? 768 : 256;
	}
	int dAng = (ang - angle) & 1023;
	if (dAng > 512) dAng = -(1024 - dAng);
	int dir = dAng / 128 + 3;
	if (dir < 0) dir = 7;
	return dir;
}

bool Player::addArmor(int n) {
	const int stat = ce.getStat(Enums::STAT_ARMOR);        // src/Player.cpp:1933-1946
	if (stat >= 200 && n > 0) return false;
	if (n < 0 && god) return false;
	ce.setStat(Enums::STAT_ARMOR, std::max(0, std::min(200, stat + n)));
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
	// src/Player.cpp:754-799.
	// Explicit unowned/no-weapon guard first — legacy reads 1 << -1 (UB).
	if (weapon < 0 || (weapons & (1 << weapon)) == 0) return false;
	if (weapon == 13 /*WP_SOUL_CUBE*/ &&
	    (target == nullptr || target->monster == nullptr)) {
		return false;                                          // :757-759
	}
	if (combat.weaponDown || (disabledWeapons != 0 && (weapons & (1 << weapon)) == 0)) {
		return false;                                          // :761-763 lowered / op-60 mask
	}
	if (combat.lerpingWeapon) {                                // :765-771
		if (combat.lerpWpDown) return false;                   // still lowering
		combat.lerpingWeapon = false;                          // snap a raise to finished
		combat.weaponDown = false;
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
	// PROJTYPE -1 (WP_PROJ_NONE) and 0 (WP_PROJ_BULLET) share the degenerate
	// launchProjectile default: arm (src/Combat.cpp:1566-1570, ADR 0017) and are
	// fully supported. Positive types need real missiles (spec group G5): refuse
	// them like the soul-cube guard instead of mis-firing instant damage.
	const int proj = wdef.projType;
	if (proj > 0) {
		std::fprintf(stderr, "[combat] projectile weapon %d unsupported (proj=%d)\n",
			weapon, proj);
		return false;
	}
	combat.performAttack(target, x, y, false);                 // :797
	return true;                                               // :798
}

} // namespace newcore