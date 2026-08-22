#ifndef NEW_DOMAIN_GAME_GAME_H
#define NEW_DOMAIN_GAME_GAME_H

#include <cstdint>
#include <vector>

#include "domain/game/Entity.h"
#include "domain/game/Player.h"
#include "io/EntityDefs.h"
#include "domain/world/MapData.h"

namespace newcore {

// World simulation: entity database (32x32 tiles), door open/close, item
// pickup. Minimal modern port of the legacy Game focused on the interactive
// subset (doors + items). Monsters/combat come later.
class Game {
public:
	static constexpr int kEntities = 275;
	static constexpr int kOpenDoors = 6;

	Game() = default;

	// Builds door/item entities from the map's sprite table. Must be called
	// once per level load, after MapData is parsed.
	void loadEntities(MapData& map, const EntityDefs& defs);

	// Tick called every frame (dtMs). Advances door animations.
	void update(int dtMs);

	// Returns the player entity (entities[1]).
	Entity* playerEntity() { return entities_.empty() ? nullptr : &entities_[1]; }

	// Entities at a world tile (head of list).
	Entity* findMapEntity(int x, int y);
	void linkEntity(Entity* e, int tx, int ty);
	void unlinkEntity(Entity* e);

	const std::vector<Entity>& entities() const { return entities_; }

	// Open/close a door entity (n=0 open, n=1 close). Animates scale 64->0.
	bool performDoorEvent(int n, Entity* door);

	// Legacy interact: trace along the view ray, first ET_DOOR hit within
	// Chebyshev distance² <= tileDistances[0] = 4096 (1 tile)
	// (src/PlayingInputHandler.cpp:445-459, src/Combat.cpp:42,
	// src/Entity.cpp:1155-1158). Trace-free simplification: candidates are
	// LINKED doors on the player's tile and on the adjacent tile in the
	// facing direction (both satisfy dist² <= 4096 by construction); own
	// tile wins (ray fraction ~0). Locked refusals handled inside
	// performDoorEvent. Returns the attempted entity or nullptr.
	Entity* useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY);

	// Returns true if the player can move from (x1,y1) to (x2,y2) (canvas
	// units, tile=64). Blocks on walls and closed/locked doors.
	bool canPlayerStep(const MapData& map, int x1, int y1, int x2, int y2);

	// Door auto-close on turn advance (legacy CanCloseDoor + advanceTurn).
	void advanceTurnDoors();

	// Player position (canvas units) used by door auto-close checks.
	void setPlayerPos(int x, int y) { playerX_ = x; playerY_ = y; }

private:
	void updateDoors();
	int playerX_ = -1, playerY_ = -1;

	std::vector<Entity> entities_;
	Entity* entityDb_[1024] = { nullptr }; // 32x32 tile lists
	MapData* map_ = nullptr;

	struct DoorAnim {
		Entity* door = nullptr;
		int sprite = -1;
		int srcX = 0, srcY = 0, dstX = 0, dstY = 0; // slide position (canvas units)
		int startScale = 64, endScale = 0;
		int t = 0;
		int dur = 750;
		bool active = false;
		bool opening = false; // true = opening, false = closing
	};
	DoorAnim doorAnims_[kOpenDoors];
	// Open doors that can auto-close (legacy openDoors[6]).
	Entity* openDoors_[kOpenDoors] = { nullptr };
	void unlinkDoor(Entity* door);
	bool doorRegistered(Entity* door) const;
	void registerOpenDoor(Entity* door);
	void unregisterOpenDoor(Entity* door);

	// Door auto-close on turn advance (legacy CanCloseDoor + advanceTurn).
	bool canCloseDoor(Entity* door);
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_GAME_H