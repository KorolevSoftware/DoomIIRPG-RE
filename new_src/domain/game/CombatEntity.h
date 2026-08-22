#ifndef NEW_DOMAIN_GAME_COMBATENTITY_H
#define NEW_DOMAIN_GAME_COMBATENTITY_H

#include <cstdint>

namespace newcore {

class Entity;

// Battle statistics of an entity (player or monster). Mirrors the legacy
// CombatEntity: 8 stats (HEALTH..IQ) and the active weapon index.
class CombatEntity {
public:
	int stats[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int weapon = -1;

	CombatEntity() = default;
	CombatEntity(int health, int armor, int defense, int strength, int accuracy, int agility);

	void clone(const CombatEntity& other);
	int getStat(int i) const { return (i >= 0 && i < 8) ? stats[i] : 0; }
	int getStatPercent(int i);
	int getIQPercent();
	int addStat(int i, int delta);
	int setStat(int i, int value);
	int calcXP() const;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_COMBATENTITY_H