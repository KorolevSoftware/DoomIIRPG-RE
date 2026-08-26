#ifndef NEW_DOMAIN_GAME_COMBATENTITY_H
#define NEW_DOMAIN_GAME_COMBATENTITY_H

#include <cstdint>

namespace newcore {

class Entity;
class Combat;

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

	// Verbatim stat-math trio (src/CombatEntity.cpp:130-378). The leading
	// Combat& replaces legacy's implicit app->combat singleton; all math is
	// integer/fixed-point with no float anywhere.
	void calcCombat(Combat& c, CombatEntity& attackerCe, Entity* targetEnt,
	                bool vsPlayer, int worldDist, int targetSubParm);
	int calcHit(Combat& c, CombatEntity& attackerCe, CombatEntity& defenderCe,
	            bool vsPlayer, int worldDist, bool zoomB2);
	int calcDamage(Combat& c, CombatEntity& attackerCe, Entity* targetEnt,
	               CombatEntity& defenderCe, bool vsPlayer, int parm);
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_COMBATENTITY_H