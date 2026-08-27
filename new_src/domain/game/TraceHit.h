#ifndef NEW_DOMAIN_GAME_TRACEHIT_H
#define NEW_DOMAIN_GAME_TRACEHIT_H

#include <cstdint>

namespace newcore {

class Entity;

// Typed result of one swept-capsule trace (ADR 0011). Replaces the naked
// (frac, Entity*) pair: the two legacy conventions it carries are
// docs/original-code/player-collision.md §2, src/Game.cpp:299-326,
// src/Render.cpp:1119-1125,1195-1209.
enum class TraceHitKind : uint8_t {
	None,   // nothing hit; entity == nullptr, frac == kFracMiss
	World,  // BSP line hit; entity == the world slot (def == nullptr by design)
	Ent,    // entityDb entity; entity->def != nullptr
};

struct TraceHit {
	static constexpr int kFracMiss    = 16384; // 1.0 in 14.14 = miss sentinel
	static constexpr int kFracAtStart = -1;    // closest point == ray start

	TraceHitKind kind = TraceHitKind::None;
	Entity* entity = nullptr;
	int frac  = kFracMiss;     // 14.14 along the sweep; kFracAtStart == start-inside
	int eType = -1;            // resolved ONCE by TraceSystem: ET_WORLD for World, -1 for None
	int eSubType = 0;          // 0 for World/None

	bool blocks() const      { return kind != TraceHitKind::None; }
	bool isWorld() const     { return kind == TraceHitKind::World; }
	bool isEntity() const    { return kind == TraceHitKind::Ent; }
	// The hit entity overlaps the sweep START (own tile / point-blank): the
	// legacy formula returns (0 >> 2) - 1, so it sorts before every real hit.
	bool startsInside() const { return frac == kFracAtStart; }
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_TRACEHIT_H
