#include "domain/game/Player.h"

#include <algorithm>

namespace newcore {

// 8 direction vectors (dx,dy) at 45 deg, canvas units (legacy Canvas.h:108).
const int Player::kViewStepValues[16] = {
	64, 0,  64, -64,  0, -64,  -64, -64,
	-64, 0,  -64, 64,  0, 64,   64, 64
};

void Player::startRotation() {
	animPos = (64 + animFrames - 1) / animFrames;     // 7
	animAngle = (256 + animFrames - 1) / animFrames; // 26
	destAngle &= 0x3FF;
	viewAngle &= 0x3FF;
}

void Player::finishRotation() {
	destAngle &= 0x3FF;
	viewAngle = destAngle;
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
	// Angle: shortest arc, masked 0-1023.
	{
		int a = destAngle & 0x3FF;
		int cur = viewAngle & 0x3FF;
		int delta = ((a - cur + 512) & 0x3FF) - 512; // -512..511
		if (delta > 0) {
			viewAngle += (delta < animAngle) ? delta : animAngle;
			moved = true;
		} else if (delta < 0) {
			viewAngle += (delta > -animAngle) ? delta : -animAngle;
			moved = true;
		}
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
	activeWeaponDef = nullptr;
	weapons = 0;
	for (auto& v : inventory) v = 0;
	for (auto& v : ammo) v = 0;
	level = 1;
	currentXP = 0;
	nextLevelXP = 0;
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
			if (slot == weapon) weapon = -1;
			return true;
		}
		bool had = (weapons & bit) != 0;
		weapons |= bit;
		if (!had && weapon == -1) weapon = slot;
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

} // namespace newcore