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
	// info flags. This word is NOT mapSpriteInfo: its low 16 bits are the
	// sprite index + 1, so a bit value coinciding with a SPRITE_FLAG_* value
	// means something else entirely (docs/original-code/entities.md §5.4).
	static constexpr int kInfoActive = 0x20000;      // alive / takes damage
	static constexpr int kInfoOnActiveList = 0x40000; // on activeMonsters ring (src/Game.cpp:798)
	static constexpr int kInfoLinked = 0x100000;     // linked in entityDb
	// State differs from the map default, so a full save record must be
	// written (src/Entity.cpp:1816-1821: without it the record is short and
	// non-statue ET_DECOR is skipped). Unrelated to mapSpriteInfo's
	// SPRITE_FLAG_TILE, which has the same value.
	static constexpr int kInfoDirty = 0x400000;
	static constexpr int kInfoCorpse = 0x1000000;    // died corpse marker
	// Idle breathing/bob is SUPPRESSED while this bit is SET. Note the
	// inversion: the EV_ENTITY_BREATHES opcode argument 1 ("breathe") CLEARS
	// it, argument 0 sets it (src/ScriptThread.cpp:1915-1918). Readers zero
	// the idle bob (src/Render.cpp:3195-3198) and the fear-eye Z offset
	// (src/Render.cpp:3054).
	static constexpr int kInfoNoBreathe = 0x20000000;
	static constexpr int kInfoHidden = 0x10000;      // hidden/dead sprite flag

	Entity() = default;

	static constexpr int kMaxCorpseLoot = 3;         // MAX_CORPSE_LOOT (src/Enums.h:1388)

	const EntityDef* def = nullptr;
	EntityMonster* monster = nullptr;
	Entity* nextOnTile = nullptr;
	Entity* prevOnTile = nullptr;
	short linkIndex = -1;
	int info = 0;
	int param = 0;
	int pos[2] = { 0, 0 };

	// Loot table: legacy Entity::lootSet is an allocated int[3] (src/Entity.h:24),
	// entries are u16 (class<<12 | idx<<6 | count). hasLootSet mirrors the
	// non-null pointer (allocated only for ET_MONSTER/ET_CORPSE at spawn,
	// src/Entity.cpp:103-111).
	int lootSet[kMaxCorpseLoot] = { 0, 0, 0 };
	bool hasLootSet = false;

	int getSprite() const { return (info & 0xFFFF) - 1; }
	void setSprite(int sprite) { info = (info & ~0xFFFF) | ((sprite + 1) & 0xFFFF); }
	bool isMonster() const { return def && def->eType == Enums::ET_MONSTER; }
	bool isDoor() const { return def && def->eType == Enums::ET_DOOR; }
	bool isItem() const { return def && def->eType == Enums::ET_ITEM; }
	bool isCorpse() const { return def && def->eType == Enums::ET_CORPSE; }
	// src/Entity.cpp:2309-2311.
	bool hasEmptyLootSet() const { return !hasLootSet || lootSet[0] == 0; }
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_ENTITY_H