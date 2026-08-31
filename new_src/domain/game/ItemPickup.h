#ifndef NEW_DOMAIN_GAME_ITEMPICKUP_H
#define NEW_DOMAIN_GAME_ITEMPICKUP_H

#include <string>

namespace newcore {

class Entity;
class EntityDb;
class EntityDefs;
class Game;
class Hud;
class Localization;
class MapData;
class Player;
class ScriptVM;
class Tables;

// Peer subsystem owning the world-item touch slice: Entity::touched /
// Entity::touchedItem (src/Entity.cpp:118-278) plus the SCR_ITEM_PICKUP hook.
// Game::touchTile walks the tile chain and delegates every entity here
// (ADR 0014: Entity stays a passive record, behaviour lives in peers).
class ItemPickup {
public:
	// Non-owning views on the world, as for the other peers.
	struct Env {
		EntityDb* db = nullptr;            // removeEntity
		const EntityDefs* defs = nullptr;  // reserved: dropped-item defs
		MapData* map = nullptr;            // reserved: sprite backlinks on drops
		Player* player = nullptr;
		Hud* hud = nullptr;
		const Localization* loc = nullptr;
		const Tables* tables = nullptr;    // weapon rows for starter ammo
		ScriptVM* vm = nullptr;            // vars[11] + executeStaticFunc(11)
		Game* game = nullptr;              // foundLoot counter
	};

	void init(const Env& env) { env_ = env; }

	// src/Entity.cpp:118-149, ET_ITEM / ET_MONSTERBLOCK_ITEM branch only.
	// Returns true when the item was consumed (sprite hidden + unlinked).
	bool touched(Entity* e);

	// Grant by item class (src/Entity.cpp:152-278).
	bool touchedItem(Entity* e);

private:
	// Applet::nextInt() analog: std::rand() & INT32_MAX (src/App.cpp:506-508).
	static int nextInt();
	// getMessageBuffer(flags) + composeText + finishMessageBuffer in one:
	// main-text string `stringIndex` with %NN args, posted at legacy flags.
	void message(int stringIndex, const std::string* args, int numArgs, int flags);
	// titleOf(loc(kTextIngame, id)) — the entity-string title, as CorpseLoot
	// does (new_src/domain/game/CorpseLoot.cpp:77-82).
	std::string ingameTitle(int stringId) const;

	Env env_;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_ITEMPICKUP_H
