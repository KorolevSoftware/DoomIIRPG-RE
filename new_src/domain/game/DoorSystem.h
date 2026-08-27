#ifndef NEW_DOMAIN_GAME_DOORSYSTEM_H
#define NEW_DOMAIN_GAME_DOORSYSTEM_H

#include "domain/game/Entity.h"

namespace newcore {

class MapData;
class ScriptVM;
class TraceSystem;
struct ScriptThread;

// Peer subsystem owning door open/close events, their slide/scale animations
// and the turn auto-close sweep (spec 2026-08-26-decomposition §P2-GB). Moved
// verbatim out of Game.
class DoorSystem {
public:
	static constexpr int kOpenDoors = 6;

	// Non-owning views on the world. entityDb holds the 1024 tile heads
	// (P2-GF replaces it with one EntityDb*); trace is the single owner of
	// the player position read by the auto-close checks (spec §P2-GA).
	struct Env {
		Entity** entityDb = nullptr;
		MapData* map = nullptr;
		ScriptVM* vm = nullptr;
		const TraceSystem* trace = nullptr;
	};

	// Wiring + per-level reset (called from Game::loadEntities).
	void init(const Env& env);

	// Plain-door use outcome (legacy hud msg44 / open+advanceTurn split,
	// src/PlayingInputHandler.cpp:445-453).
	enum class DoorUseResult { None, Opened, Locked };

	// Legacy interact: trace along the view ray, first ET_DOOR hit within
	// Chebyshev distance² <= tileDistances[0] = 4096 (1 tile)
	// (src/PlayingInputHandler.cpp:445-459, src/Combat.cpp:42,
	// src/Entity.cpp:1155-1158). Trace-free simplification: candidates are
	// LINKED doors on the player's tile and on the adjacent tile in the
	// facing direction (both satisfy dist² <= 4096 by construction); own
	// tile wins (ray fraction ~0). Returns the outcome; locked doors are
	// refused without animating.
	DoorUseResult useDoorFacing(const MapData& map, int px, int py, int stepX, int stepY);

	// Open/close a door entity (n=0 open, n=1 close). n2 is the legacy snap
	// selector (src/Game.cpp:1153-1155): 0 = finish the animation instantly,
	// 1 = animate fully, 2 = snap only when offscreen (turn auto-close passes
	// 2, src/Game.cpp:1275) — cullBoundingBox is not ported so 2 animates
	// like 1 (documented deviation). Player use passes n2=1
	// (src/PlayingInputHandler.cpp:451); scripted opens pass the quiet-bit
	// derived value (src/ScriptThread.cpp:751,760). A snapped open still
	// registers the door in openDoors_ for auto-close (registration precedes
	// the snap decision, src/Game.cpp:1141-1155). ownerThread names the
	// script thread to resume when an OPEN animation completes (blocking
	// EV_DOOROP); nullptr = fire-and-forget. The legacy mapping guarantees
	// ownerThread == nullptr whenever n2 == 0.
	bool performDoorEvent(int n, Entity* door, int n2, ScriptThread* ownerThread = nullptr);

	// Door auto-close on turn advance (legacy CanCloseDoor + advanceTurn).
	void advanceTurnDoors();

	// Per-frame animation step (was the doorAnims_ clock loop + updateDoors
	// call at the tail of Game::update).
	void update(int dtMs);

private:
	Env env_;

	struct DoorAnim {
		Entity* door = nullptr;
		int sprite = -1;
		int srcX = 0, srcY = 0, dstX = 0, dstY = 0; // slide position (canvas units)
		int startScale = 64, endScale = 0;
		int t = 0;
		int dur = 750;
		bool active = false;
		bool opening = false; // true = opening, false = closing
		ScriptThread* ownerThread = nullptr; // resumed once when the OPEN completes
	};
	DoorAnim doorAnims_[kOpenDoors];
	// Open doors that can auto-close (legacy openDoors[6]).
	Entity* openDoors_[kOpenDoors] = { nullptr };
	void updateDoors();
	void unlinkDoor(Entity* door);
	bool doorRegistered(Entity* door) const;
	void registerOpenDoor(Entity* door);
	void unregisterOpenDoor(Entity* door);

	// Door auto-close on turn advance (legacy CanCloseDoor + advanceTurn).
	bool canCloseDoor(Entity* door);

	// entityDb tile-list access. Copies of Game::findMapEntity /
	// linkEntity / unlinkEntity until P2-GF makes EntityDb their single
	// owner; do not add logic here.
	Entity* findMapEntity(int x, int y) const;
	void linkEntity(Entity* e, int tx, int ty);
	void unlinkEntity(Entity* e);
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_DOORSYSTEM_H
