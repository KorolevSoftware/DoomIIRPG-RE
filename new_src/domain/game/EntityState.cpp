#include "domain/game/EntityState.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/world/MapData.h"
#include "io/DataReader.h"
#include "io/DataWriter.h"
#include "io/EntityDefs.h"

namespace newcore {
namespace EntityState {

namespace {

constexpr uint32_t kMarker = 0xCAFEBABE;   // Resource::writeMarker (src/Resource.cpp:92-102)

// Save-handle bits above the entity index (src/Entity.cpp:1787-1857).
constexpr int kHBinaryOn = 0x10000;        // isBinaryEntity's state bit
constexpr int kHVisible = 0x40000;         // sprite not hidden
constexpr int kHNoData = 0x80000;          // not dirty: no payload
constexpr int kHCorpse = 0x100000;
constexpr int kHShort = 0x200000;          // brief monster record / locked door / NPC param >= 1

// Sprite field rows (MapData.h:72-75).
constexpr int kFX = 0, kFY = 1, kFZ = 2, kFScale = 8;

int16_t& spr(MapData& m, int field, int sprite) { return m.mapSprites[sprite + field * m.numSprites]; }

// Legacy getHeight(x, y) + 32, converted from the original's terrain-baked Z
// to the rewrite's stored Z (SpriteLerps::spriteZBias).
int16_t floorZ(const Env& env, int sprite, int x, int y) {
	return static_cast<int16_t>(env.map->heightAt(x, y) + 32 - env.game->lerps.spriteZBias(sprite, x, y));
}

void linkAtIndex(Game& g, Entity& e) { g.db.linkEntity(&e, e.linkIndex % 32, e.linkIndex / 32); }

// Entity::isBinaryEntity (src/Entity.cpp:1412-1482). isDroppedEntity() is
// always false: no drop slots (EV_DROPITEM not ported).
bool isBinaryEntity(const Env& env, const Entity& e, int* array) {
	if (e.def == nullptr) return false;
	bool b = false;
	switch (e.def->eType) {
	case Enums::ET_ITEM:
	case Enums::ET_ATTACK_INTERACTIVE:
	case Enums::ET_MONSTERBLOCK_ITEM:
		b = e.def->eSubType != Enums::INTERACT_PICKUP && (e.info & Entity::kInfoLinked) != 0;
		break;
	case Enums::ET_DOOR:
		b = spr(*env.map, kFScale, e.getSprite()) != 64;
		if (e.def->eSubType == Enums::DOOR_LOCKED && array != nullptr) array[1] |= kHShort;
		break;
	case Enums::ET_NONOBSTRUCTING_SPRITEWALL: {
		// Other tiles return true with b = false (kept as in the original).
		const int info = env.map->mapSpriteInfo[e.getSprite()];
		if ((info & 0xFF) == Enums::TILENUM_SPRITEWALL_NOCLIP_B) b = (info & 0x10000) != 0;
		break;
	}
	case Enums::ET_CORPSE:
		if (e.def->eSubType != Enums::CORPSE_SKELETON) return false;
		b = e.param != 0;
		break;
	case Enums::ET_DECOR_NOCLIP:
		if (e.def->eSubType != Enums::DECOR_WATER_SPOUT) return false;
		b = (e.info & Entity::kInfoDirty) != 0;
		break;
	default:
		return false;
	}
	if (array != nullptr) array[0] = b ? 1 : 0;
	return true;
}

// Entity::getSaveHandle(true) (src/Entity.cpp:1787-1857). Dropped-entity and
// named-entity branches are gone: no drop slots, Entity::name not ported.
int getSaveHandle(const Env& env, const Entity& e, int index) {
	int buf[2] = { 1, index };
	const bool binary = isBinaryEntity(env, e, buf);
	if (((e.info & 0xFFFF) == 0 || e.def == nullptr) && !binary) return -1;
	int n = buf[1];
	if (binary && buf[0] != 0) n |= kHBinaryOn;
	if ((env.map->mapSpriteInfo[e.getSprite()] & 0x10000) == 0) n |= kHVisible;
	if ((e.info & 0x2000000) != 0) n |= 0x1000000;
	if ((e.info & Entity::kInfoDirty) == 0) {
		n |= kHNoData;
		if (e.def->eType == Enums::ET_DECOR && e.def->eSubType != Enums::DECOR_STATUE) return -1;
	}
	if ((e.info & (Entity::kInfoCorpse | Entity::kInfoHidden)) != 0) n |= kHCorpse;
	if ((e.info & 0x80000) != 0) n |= 0x2000000;
	if ((e.info & Entity::kInfoHidden) != 0) n = (n & ~kHVisible) | kHCorpse;
	if (e.def->eType == Enums::ET_NPC && e.param != 0) {
		n |= kHShort;
		if (e.param == 2) n |= 0x4000000;
	}
	if (e.monster != nullptr) {
		if ((e.monster->flags & Enums::MFLAG_NORESPAWN) != 0) n |= 0x800000;
		if ((e.monster->flags & Enums::MFLAG_NOTRACK) != 0) n |= 0x400000;
		if ((e.monster->flags & Enums::MFLAG_LOOTED) != 0) n |= 0x8000000;
	}
	// Brief variant: ordinary corpses are cleaned up (hidden, no payload).
	// Legacy dereferences monster unguarded; placed corpse props have none.
	if (e.def->eType == Enums::ET_CORPSE && e.def->eSubType != Enums::CORPSE_SKELETON &&
	    e.monster != nullptr && (e.monster->flags & Enums::MFLAG_NOTRACK) == 0) {
		n = (n & ~kHVisible) | kHNoData;
	} else if (e.def->eType == Enums::ET_MONSTER) {
		n |= kHShort;
	}
	return n;
}

// Entity::saveState (src/Entity.cpp:1497-1572), brief subset.
void saveState(const Env& env, DataWriter& w, const Entity& e, int n) {
	MapData& m = *env.map;
	if ((n & kHNoData) != 0) return;
	const int sprite = e.getSprite();
	if (e.def->eType == Enums::ET_ATTACK_INTERACTIVE && e.def->eSubType == Enums::INTERACT_BARRICADE) {
		w.writeShort(spr(m, kFScale, sprite));
		return;
	}
	const int info = m.mapSpriteInfo[sprite];
	if (e.def->eType == Enums::ET_MONSTER && (n & kHShort) != 0) {
		w.writeByte(static_cast<uint8_t>(spr(m, kFX, sprite) >> 3));
		w.writeByte(static_cast<uint8_t>(spr(m, kFY, sprite) >> 3));
		w.writeByte(static_cast<uint8_t>((info & 0xFF00) >> 8));
		w.writeShort(e.monster->flags);
		return;
	}
	w.writeByte(static_cast<uint8_t>((e.info >> 16) & 0xFF));
	if ((e.info & Entity::kInfoHidden) == 0) {
		w.writeByte(static_cast<uint8_t>((info & 0xFF0000) >> 16));
		w.writeByte(static_cast<uint8_t>((info & 0xFF00) >> 8));
		if ((info & 0xF000000) == 0) {
			w.writeByte(static_cast<uint8_t>(spr(m, kFX, sprite) >> 3));
			w.writeByte(static_cast<uint8_t>(spr(m, kFY, sprite) >> 3));
		} else {
			w.writeShort(e.linkIndex);
		}
	}
	if (e.monster != nullptr) {
		w.writeShort(e.monster->flags);
		if ((e.info & Entity::kInfoHidden) == 0 && (e.monster->flags & Enums::MFLAG_SCALED) != 0)
			w.writeByte(static_cast<uint8_t>(spr(m, kFScale, sprite)));
		// shortcut: the monsterEffects + ce + goal block (n & kHCorpse == 0) is
		// unreachable here: a monster entity is ET_MONSTER (short record) or a
		// corpse (kHCorpse or no data). Port it with the full save system.
	} else {
		w.writeShort(spr(m, kFZ, sprite));                     // stored Z both ways
	}
}

// Entity::restoreBinaryState (src/Entity.cpp:1859-1934).
void restoreBinaryState(const Env& env, Entity& e, int n) {
	Game& g = *env.game;
	const bool b = (n & kHBinaryOn) != 0;
	switch (e.def->eType) {
	case Enums::ET_ITEM:
	case Enums::ET_ATTACK_INTERACTIVE:
	case Enums::ET_MONSTERBLOCK_ITEM:
		if (e.def->eSubType == Enums::INTERACT_PICKUP) {
			if (b) g.turnEntityIntoWaterSpout(&e);
			break;
		}
		g.db.unlinkEntity(&e);
		if (b) linkAtIndex(g, e);
		if (!b && (e.def->eSubType == Enums::INTERACT_BARRICADE || e.def->eSubType == Enums::INTERACT_CRATE)) {
			int& info = env.map->mapSpriteInfo[e.getSprite()];
			info = (info & 0xFFFF00FF) | ((e.def->eSubType == Enums::INTERACT_CRATE ? 3 : 1) << 8);
		}
		break;
	case Enums::ET_DOOR: {
		const bool locked = e.def->eSubType == Enums::DOOR_LOCKED;
		g.setLineLocked(&e, false);
		g.doors.performDoorEvent(b ? 0 : 1, &e, 0);
		if ((n & kHShort) != 0 || locked) g.setLineLocked(&e, (n & kHShort) != 0);
		break;
	}
	case Enums::ET_NONOBSTRUCTING_SPRITEWALL:
		// DoorSystem animates ET_DOOR only, so this is a no-op until sprite-wall doors exist.
		g.doors.performDoorEvent(b ? 0 : 1, &e, 0);
		break;
	case Enums::ET_CORPSE:
		if (b) {
			++e.param;
			e.hasLootSet = false;
		}
		break;
	default:
		break;
	}
}

// Entity::loadState (src/Entity.cpp:1574-1786), brief subset.
void loadState(const Env& env, DataReader& r, Entity& e, int n) {
	Game& g = *env.game;
	MapData& m = *env.map;
	const int sprite = e.getSprite();
	int& msi = m.mapSpriteInfo[sprite];
	int tile = msi & 0xFF;
	if ((msi & Enums::SPRITE_FLAG_TILE) != 0) tile += 257;
	if ((n & kHVisible) != 0) {
		msi &= ~0x10000;
		if ((n & kHNoData) != 0 && (e.info & Entity::kInfoLinked) == 0)
			g.db.linkEntity(&e, spr(m, kFX, sprite) >> 6, spr(m, kFY, sprite) >> 6);
	} else {
		msi |= 0x10000;
		if ((e.info & Entity::kInfoLinked) != 0 && e.def->eType != Enums::ET_DOOR &&
		    (tile < Enums::TILENUM_DUMMY_START || tile > Enums::TILENUM_DUMMY_END)) {
			g.db.unlinkEntity(&e);
		} else if (e.def->eType == Enums::ET_ATTACK_INTERACTIVE && e.def->eSubType != Enums::INTERACT_PICKUP) {
			++g.destroyedObj;
		}
	}
	if ((n & 0x1000000) != 0) e.info |= 0x2000000;
	if (isBinaryEntity(env, e, nullptr)) {
		restoreBinaryState(env, e, n);
		if ((n & kHNoData) != 0) return;
	}
	if (e.def->eType == Enums::ET_ATTACK_INTERACTIVE && e.def->eSubType == Enums::INTERACT_BARRICADE) {
		const int16_t s = r.readShort();
		if (s != spr(m, kFScale, sprite)) {
			spr(m, kFScale, sprite) = s;
			e.info |= Entity::kInfoDirty;
		}
		return;
	}
	if ((n & kHCorpse) != 0) {
		if ((n & kHVisible) == 0) {
			e.info |= Entity::kInfoHidden;
			if ((e.info & Entity::kInfoLinked) != 0) g.db.unlinkEntity(&e);
		} else if ((n & kHNoData) != 0) {
			std::fprintf(stderr, "[world] ERR_CLEANCORPSE sprite=%d\n", sprite);
		}
		msi = (msi & 0xFFFF00FF) | 0x7000;
		if (const EntityDef* d = env.defs->find(Enums::ET_CORPSE, e.def->eSubType, e.def->parm)) e.def = d;
	}
	if (e.def->eType == Enums::ET_NPC)
		e.param = ((n & kHShort) != 0 ? 1 : 0) + ((n & 0x4000000) != 0 ? 1 : 0);
	if (e.monster != nullptr) {
		if ((n & 0x800000) != 0) { e.monster->flags |= Enums::MFLAG_NORESPAWN; e.info |= Entity::kInfoDirty; }
		if ((n & 0x400000) != 0) { e.monster->flags |= Enums::MFLAG_NOTRACK; e.info |= Entity::kInfoDirty; }
		if ((n & 0x8000000) != 0) { e.monster->flags |= Enums::MFLAG_LOOTED; e.info |= Entity::kInfoDirty; }
	}
	if ((n & kHNoData) != 0) return;
	if ((e.info & Entity::kInfoLinked) != 0) g.db.unlinkEntity(&e);

	if (e.def->eType == Enums::ET_MONSTER && (n & kHShort) != 0) {
		const int x = r.readByte() << 3;
		const int y = r.readByte() << 3;
		const int frame = r.readByte() << 8;
		spr(m, kFX, sprite) = static_cast<int16_t>(x);
		spr(m, kFY, sprite) = static_cast<int16_t>(y);
		spr(m, kFZ, sprite) = floorZ(env, sprite, x, y);
		msi = (msi & 0xFFFF00FF) | frame;
		if ((e.info & Entity::kInfoLinked) != 0) g.db.unlinkEntity(&e);
		if ((n & kHVisible) == 0) g.monsters.deactivate(&e);
		else g.db.linkEntity(&e, x >> 6, y >> 6);
		e.monster->flags = r.readShort();
		if (e.monster->flags != 0 || frame != 0) e.info |= Entity::kInfoDirty;
		return;
	}
	e.info = (e.info & 0xFF00FFFF) | (r.readByte() << 16);
	if ((e.info & Entity::kInfoHidden) == 0) {
		const int n8 = r.readByte();
		const int n9 = r.readByte();
		msi = (msi & 0xFF0000FF) | (n9 << 8) | (n8 << 16);
		if ((msi & 0xF000000) == 0) {
			spr(m, kFX, sprite) = static_cast<int16_t>(r.readByte() << 3);
			spr(m, kFY, sprite) = static_cast<int16_t>(r.readByte() << 3);
			if (e.monster != nullptr)
				spr(m, kFZ, sprite) = floorZ(env, sprite, spr(m, kFX, sprite), spr(m, kFY, sprite));
		}
		if ((msi & 0xF000000) != 0) e.linkIndex = r.readShort();
		else e.linkIndex = static_cast<short>((spr(m, kFX, sprite) >> 6) + (spr(m, kFY, sprite) >> 6) * 32);
		const int orient = msi & 0xF000000;
		if ((orient == 0xC000000 || orient == 0x3000000) &&
		    (e.def->eType == Enums::ET_NONOBSTRUCTING_SPRITEWALL || e.def->eType == Enums::ET_SPRITEWALL)) {
			spr(m, kFX, sprite) = static_cast<int16_t>(((e.linkIndex % 32) << 6) + 32);
			spr(m, kFY, sprite) = static_cast<int16_t>(((e.linkIndex / 32) << 6) + 32);
		}
		if (n9 != 0) e.info |= Entity::kInfoDirty;
	}
	if ((n & 0x2000000) != 0) e.info |= 0x80000;
	if (e.monster != nullptr) {
		e.monster->flags = r.readShort();
		if ((e.info & Entity::kInfoHidden) == 0) {
			if ((e.monster->flags & Enums::MFLAG_SCALED) != 0) spr(m, kFScale, sprite) = r.readByte();
			if ((n & kHCorpse) != 0) e.info |= Entity::kInfoCorpse;
			// else: ce + goal block, see saveState's shortcut note.
		}
		if ((e.info & Entity::kInfoOnActiveList) != 0) {
			e.info &= ~Entity::kInfoOnActiveList;
			g.monsters.activate(&e, false, false, false, true);
		}
		if ((e.info & Entity::kInfoCorpse) != 0) {
			if (const EntityDef* d = env.defs->find(Enums::ET_CORPSE, e.def->eSubType, e.def->parm)) e.def = d;
		}
	} else {
		spr(m, kFZ, sprite) = r.readShort();                  // stored Z both ways
	}
	if ((e.info & Entity::kInfoLinked) != 0) {
		if ((msi & 0xF000000) != 0) linkAtIndex(g, e);
		else g.db.linkEntity(&e, spr(m, kFX, sprite) >> 6, spr(m, kFY, sprite) >> 6);
	}
}

} // namespace

void save(const Env& env, DataWriter& w) {
	std::vector<int> handles;
	std::vector<Entity>& entities = env.game->db.entities();
	for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
		const int h = getSaveHandle(env, entities[i], i);
		if (h != -1) handles.push_back(h);
	}
	w.writeShort(static_cast<int16_t>(handles.size()));
	for (int h : handles) {
		w.writeUInt(kMarker);
		w.writeInt(h);
		saveState(env, w, entities[h & 0xFFFF], h);
	}
}

bool load(const Env& env, DataReader& r) {
	std::vector<Entity>& entities = env.game->db.entities();
	const int count = r.readShort();
	for (int i = 0; i < count; ++i) {
		r.readUInt();                                          // marker, ignored
		const int h = r.readInt();
		const int idx = h & 0xFFFF;
		if (idx >= static_cast<int>(entities.size()) || entities[idx].def == nullptr) {
			std::fprintf(stderr, "[world] bad entity record %d (handle 0x%X)\n", i, h);
			return false;
		}
		loadState(env, r, entities[idx], h);
	}
	return true;
}

} // namespace EntityState
} // namespace newcore
