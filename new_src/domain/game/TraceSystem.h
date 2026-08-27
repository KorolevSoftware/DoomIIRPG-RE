#ifndef NEW_DOMAIN_GAME_TRACESYSTEM_H
#define NEW_DOMAIN_GAME_TRACESYSTEM_H

#include <vector>

#include "domain/game/Entity.h"
#include "domain/game/TraceHit.h"

namespace newcore {

class EntityDb;
class MapData;

// Peer subsystem owning the swept-capsule traces (spec
// 2026-08-26-decomposition §P2-GA). Moved verbatim out of Game; the only
// behavioural addition is that the hit list is typed (TraceHit, ADR 0011).
class TraceSystem {
public:
	// Non-owning views on the world. db is the entity database: the entity
	// array (slot 0 = world) plus the 1024 tile heads (spec §P2-GF).
	struct Env {
		EntityDb* db = nullptr;
		MapData* map = nullptr;
	};

	void init(const Env& env) { env_ = env; }

	// Swept-capsule move trace: legacy Game::trace (7-arg wrapper
	// src/Game.cpp:195-197, body :199-327) + Render::traceWorld
	// (src/Render.cpp:1212-1283, flattened — see spec 2026-08-23
	// faithful-player-collision §3.3). Sweeps segment (x0,y0)->(x1,y1) as a
	// capsule of the given radius (canvas units, tile=64) against world lines
	// (if mask & 1) and all entityDb entities matching mask & (1<<eType),
	// skipping skipEnt. The returned hit is the closest one = lowest frac
	// (legacy traceEntity / traceFracs[0], src/Game.cpp:312-326); kind ==
	// None means nothing blocks and the caller may commit (legacy commits iff
	// traceEntity == nullptr, src/MovementController.cpp:332-333). frac is
	// 14.14 fixed point, 16384 == 1.0, hit <= 16382, start-inside == -1, miss
	// sentinel 16384 (src/Render.cpp:1119-1125,1195-1209).
	TraceHit trace(int x0, int y0, int x1, int y1, Entity* skipEnt, int mask, int radius);

	// Sorted (frac asc) hit list of the most recent trace — the legacy
	// traceEntities/traceFracs pair (src/Game.cpp:312-326). Consumed by the
	// facing probe's monster-preference rescan
	// (src/MovementController.cpp:43-86 subset).
	const std::vector<TraceHit>& hits() const { return traceHits_; }

	// World contact point of the most recent trace (legacy
	// Game::traceCollisionX/Y, src/Game.cpp:302-310). The fire path uses it
	// as the air-shot impact point (src/PlayingInputHandler.cpp:532-536).
	int collisionX() const { return traceCollisionX_; }
	int collisionY() const { return traceCollisionY_; }

	// Chebyshev^2 distance from (x,y) to the hit (src/Entity.cpp:1155-1158);
	// the World kind resolves through the last collision point.
	int distFrom(const TraceHit& h, int x, int y) const;
	int distFrom(const Entity* e, int x, int y) const;

	// Player position (canvas units) read by the trace's ET_PLAYER branch,
	// the door auto-close checks and the monster activate range check.
	void setPlayerPos(int x, int y) { playerX_ = x; playerY_ = y; }
	int playerX() const { return playerX_; }
	int playerY() const { return playerY_; }

private:
	Env env_;

	int playerX_ = -1, playerY_ = -1;

	// Trace scratch (reused buffers; single-threaded GL loop).
	int tracePoints_[4] = { 0, 0, 0, 0 };              // x0,y0,x1,y1 (src/Game.cpp:208-211)
	int traceBBox_[4]   = { 0, 0, 0, 0 };              // clamped bbox (src/Game.cpp:212-215)
	std::vector<TraceHit> traceHits_;
	// World contact point of the last trace (legacy Game::
	// traceCollisionX/Y fields, src/Game.cpp:302-310): lerp at the hit frac,
	// else the ray end. ET_WORLD distance queries resolve through it
	// (Entity::calcPosition src/Entity.cpp:1375-1378 analog).
	int traceCollisionX_ = 0, traceCollisionY_ = 0;

	// The ONE place where the "world hit == entities[0] with def == nullptr"
	// convention is decoded (ADR 0011 decision 2).
	void pushHit(int frac, Entity* ent);

	void traceEntityHits(const MapData& map, Entity* skipEnt, int mask, int radius); // src/Game.cpp:216-296
	int  traceWorldFrac(const MapData& map, int mask, int radius2);                  // src/Render.cpp:1212-1283 (flat)
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_TRACESYSTEM_H
