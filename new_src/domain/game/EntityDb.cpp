#include "domain/game/EntityDb.h"

#include <cstdio>

#include "domain/game/Player.h"
#include "domain/world/MapData.h"

namespace newcore {

void EntityDb::resetEntities() {
	entities_.clear();
	entities_.resize(kEntities);
}

// ---- entityDb (32x32 tile lists) ----

Entity* EntityDb::findMapEntity(int x, int y) const {
	if (x < 0 || y < 0 || x >= 32 || y >= 32) return nullptr;
	return entityDb_[y * 32 + x];
}

void EntityDb::linkEntity(Entity* e, int tx, int ty) {
	if (tx < 0 || ty < 0 || tx >= 32 || ty >= 32) return;
	unlinkEntity(e);
	int idx = ty * 32 + tx;
	e->nextOnTile = entityDb_[idx];
	if (e->nextOnTile) e->nextOnTile->prevOnTile = e;
	e->prevOnTile = nullptr;
	entityDb_[idx] = e;
	e->linkIndex = (short)idx;
	e->info |= Entity::kInfoLinked;
}

// Port of Game::unlinkEntity (src/Game.cpp:70-93). The splice is
// UNCONDITIONAL: legacy never consults an info bit here. A rewrite-era
// `if (!(info & kInfoLinked)) return;` gate used to sit at the top and was a
// hang bug — MonsterSystem::corpsifyMonster drops the bit while the entity is
// still physically linked (mirroring src/ScriptThread.cpp:2258-2259) and then
// relinks, so the gate skipped the splice and the old links plus the new head
// link formed a cycle that froze every tile-list walk.
void EntityDb::unlinkEntity(Entity* e) {
	if (!e) return;
	if (!entities_.empty() && e == &entities_[0]) {
		// Legacy raises ERR_BADUNLINKWORLD (src/Game.cpp:74-77). We keep
		// running but say so once: the world slot is never linked, so
		// splicing it could only corrupt a tile head.
		static bool reported = false;
		if (!reported) {
			reported = true;
			std::fprintf(stderr, "[err] unlinkEntity on the world slot (entities[0]) - ignored\n");
		}
		return;
	}
	// Legacy branch order: head test FIRST, prevOnTile only as the else
	// (src/Game.cpp:79-84). Our linkIndex bounds check is a safe superset of
	// legacy's unchecked entityDb[linkIndex] read: idx is in [0,1024) for every
	// linked entity, and an unlinked one (idx == -1) has nothing to splice.
	const int idx = e->linkIndex;
	if (idx >= 0 && idx < 1024 && entityDb_[idx] == e) {
		entityDb_[idx] = e->nextOnTile;
	} else if (e->prevOnTile) {
		e->prevOnTile->nextOnTile = e->nextOnTile;
	}
	if (e->nextOnTile) e->nextOnTile->prevOnTile = e->prevOnTile;   // :86-88
	e->nextOnTile = e->prevOnTile = nullptr;                        // :90-91
	e->info &= ~Entity::kInfoLinked;                                // :92
}

// See EntityDb::TileWalk in the header.
bool EntityDb::TileWalk::ok(const Entity* e) {
	if (++steps_ <= kEntities) return true;
	static int reports = 0;
	if (reports < 4) {
		++reports;
		std::fprintf(stderr, "[err] %s: tile list cycle after %d steps, chain from linkIndex=%d:",
			where_, steps_ - 1, (int)e->linkIndex);
		const Entity* p = e;
		for (int n = 0; n < 8 && p != nullptr; ++n, p = p->nextOnTile) {
			std::fprintf(stderr, " {spr=%d link=%d eType=%d}", p->getSprite(), (int)p->linkIndex,
				p->def ? p->def->eType : -1);
		}
		std::fprintf(stderr, "\n");
	}
	return false;
}

Entity* EntityDb::findEntityBySprite(int sprite) {
	for (Entity& e : entities_) {
		if (e.def != nullptr && e.getSprite() == sprite) return &e;
	}
	return nullptr;
}

// Port of Game::removeEntity (src/Game.cpp:183-193); see EntityDb.h.
void EntityDb::removeEntity(Entity* e) {
	if (!e || !env_.map) return;
	int s = e->getSprite();
	if ((e->info & 0xFFFF) != 0 && s >= 0 && s < env_.map->numSprites) { // :186-188
		env_.map->mapSpriteInfo[s] |= 0x10000;
	}
	if ((e->info & Entity::kInfoLinked) != 0) {                          // :189-191
		unlinkEntity(e);
	}
	if (player_ != nullptr) player_->facingEntity = nullptr;             // :192
}

} // namespace newcore
