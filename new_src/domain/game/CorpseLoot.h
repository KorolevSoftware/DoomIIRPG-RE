#ifndef NEW_DOMAIN_GAME_CORPSELOOT_H
#define NEW_DOMAIN_GAME_CORPSELOOT_H

#include "domain/game/Entity.h"
#include "text/Text.h"

namespace newcore {

class EntityDb;
class EntityDefs;
class Localization;
class Player;
class Tables;

// Peer subsystem owning the corpse-loot slice: the default loot sets written
// at spawn, the faced-corpse lookup, the pooling of a tile's loot into one
// display buffer and the grant pass (spec 2026-08-26-decomposition §P2-GE).
// Moved verbatim out of Game. See docs/original-code/loot-inventory.md.
class CorpseLoot {
public:
	// Non-owning views on the world. db owns the entity array and the 1024
	// tile heads (spec §P2-GF); defs resolves item long names.
	struct Env {
		EntityDb* db = nullptr;
		const EntityDefs* defs = nullptr;
	};

	// Wiring (called from Game::loadEntities). No per-level state to reset:
	// the pool lives on the caller (LootSession).
	void init(const Env& env);

	// Port of Entity::populateDefaultLootSet (src/Entity.cpp:1997-2048),
	// called at spawn time for every monster/corpse entity.
	static void populateDefaultLootSet(Entity& e);

	// Faced lootable corpse: legacy ACTION_FIRE traces forward and selects an
	// ET_CORPSE candidate exactly one tile away (dist == tileDistSq(1))
	// that is not yet looted and owns a lootSet (src/PlayingInputHandler.cpp:
	// 279-335). Trace-free simplification mirroring useDoorFacing: candidates
	// are LINKED corpses on the adjacent tile in the facing direction (own
	// tile is dist 0, never selected by legacy).
	Entity* findLootableCorpseFacing(int px, int py, int stepX, int stepY);

	// Pooled corpse-loot display state — legacy LootingSystem fields folded
	// into one struct (lootPool / numPoolItems / numLootItems /
	// lootPoolCredits / lootText / lootPoolIndices / lootLineNum).
	struct Pool {
		static constexpr int kMaxLines = 9;      // lootPoolIndices[18] / 2 pairs
		int entries[Entity::kMaxCorpseLoot] = { 0, 0, 0 }; // packed u16 (cls<<12|idx<<6|cnt)
		int numEntries = 0;                      // numPoolItems (incl. class-6 flavor lines)
		int numItems   = 0;                      // numLootItems (stat only, counts pre-merge)
		int credits   = 0;                       // lootPoolCredits
		Text text;                               // lootText: '|'-separated lines
		short lineIndex[2 * kMaxLines] = { 0 };  // lootPoolIndices: <start,len> per line
		int topLine = 0;                         // lootLineNum (scroll pos, reset by pool)
		static int lineCount(const Pool& p) { return p.numEntries + (p.credits != 0); }
	};

	// Mark-looted + pool + compose the loot list for ALL eType==9 entities on
	// tile (tx,ty) (src/LoothingSystem.cpp:154-278). Marks BEFORE reading
	// loot sets, per entity: prop ++param (skip when already != 0), monster
	// flag 0x800 (unified into ++param — see Deviations #1 of spec
	// 2026-08-25-loot-dwell-ui), info |= kInfoActivated.
	void poolLootCorpse(int tx, int ty, const Localization& loc, Pool& out);

	// Grant pass (src/LoothingSystem.cpp:281-307): give() per non-class-6
	// entry, weapon starter ammo max(usage,10) of the weapon row's AmmoType,
	// credits give(0,24,credits), foundLoot stderr stub, resets pool counters
	// + text. tables may be null (skips starter ammo).
	void giveLootPool(Pool& pool, Player& player, const Tables* tables);

private:
	Env env_;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_CORPSELOOT_H
