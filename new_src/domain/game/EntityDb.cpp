#include "domain/game/EntityDb.h"

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

void EntityDb::unlinkEntity(Entity* e) {
	if (!(e->info & Entity::kInfoLinked)) return;
	if (e->prevOnTile) e->prevOnTile->nextOnTile = e->nextOnTile;
	else {
		int idx = e->linkIndex;
		if (idx >= 0 && idx < 1024 && entityDb_[idx] == e)
			entityDb_[idx] = e->nextOnTile;
	}
	if (e->nextOnTile) e->nextOnTile->prevOnTile = e->prevOnTile;
	e->nextOnTile = e->prevOnTile = nullptr;
	e->info &= ~Entity::kInfoLinked;
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
