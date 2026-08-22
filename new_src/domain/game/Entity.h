#ifndef NEW_DOMAIN_GAME_ENTITY_H
#define NEW_DOMAIN_GAME_ENTITY_H

#include <cstdint>

#include "io/EntityDefs.h"
#include "domain/game/CombatEntity.h"
#include "domain/game/Enums.h"

namespace newcore {

class EntityMonster;
class Game;

// A game-world entity (player, monster, door, item, decor). Modern minimal
// port of the legacy Entity: keeps the sprite binding (info low 16 bits =
// sprite index + 1) so it interoperates with MapData.mapSprites, plus a
// def and per-entity state.
class Entity {
public:
	// info flags.
	static constexpr int kInfoActive = 0x20000;      // alive / takes damage
	static constexpr int kInfoLinked = 0x100000;     // linked in entityDb
	static constexpr int kInfoActivated = 0x400000;  // activated/changed
	static constexpr int kInfoCorpse = 0x1000000;    // died corpse marker
	static constexpr int kInfoHidden = 0x10000;      // hidden/dead sprite flag

	Entity() = default;

	const EntityDef* def = nullptr;
	EntityMonster* monster = nullptr;
	Entity* nextOnTile = nullptr;
	Entity* prevOnTile = nullptr;
	short linkIndex = -1;
	int info = 0;
	int param = 0;
	int pos[2] = { 0, 0 };

	int getSprite() const { return (info & 0xFFFF) - 1; }
	void setSprite(int sprite) { info = (info & ~0xFFFF) | ((sprite + 1) & 0xFFFF); }
	bool isMonster() const { return def && def->eType == Enums::ET_MONSTER; }
	bool isDoor() const { return def && def->eType == Enums::ET_DOOR; }
	bool isItem() const { return def && def->eType == Enums::ET_ITEM; }
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_ENTITY_H