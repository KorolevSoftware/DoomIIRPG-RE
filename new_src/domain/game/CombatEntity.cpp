#include "domain/game/CombatEntity.h"

#include <algorithm>

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

} // namespace newcore