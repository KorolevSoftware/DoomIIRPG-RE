#include "domain/game/WorldState.h"

#include <cassert>
#include <cstdio>
#include <memory>

#include "domain/game/Entity.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/EntityState.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapData.h"
#include "io/DataReader.h"
#include "io/DataWriter.h"
#include "io/EntityDefs.h"

namespace newcore {

namespace {

constexpr int kStateVar15 = 15;          // travel direction, global (MC §5)
constexpr int kTileEventDisabled = 0x80000;

#ifndef NDEBUG
// Debug round trip on a synthetic map: save, scramble every restored field,
// restore, compare. Exercises the bit packers' partial-byte/word edges.
void roundTripSelfCheck() {
	MapData m;
	m.compileDate = 0x1234;
	m.flagsBitmask = 5;
	m.numLines = 11;                                   // partial trailing byte
	m.lineFlags = { 0x08, 0x80, 0x88, 0x00, 0x80, 0x08 };   // line 10 = last line, bit 0 of the partial byte
	m.numMapSprites = 5;                               // last byte covers slots 4..7
	m.numSprites = m.numMapSprites + MapData::kMaxCustomSprites;
	m.mapSpriteInfo.assign(m.numSprites * 2, 0);
	m.mapSpriteInfo[0] = 0x10000;
	m.mapSpriteInfo[3] = 0x200000;
	m.mapSpriteInfo[6] = 0x10000 | 0x200000;
	m.mapFlags.assign(1024, 0);
	m.mapFlags[0] = 0x80;
	m.mapFlags[1023] = 0x81;
	m.numTileEvents = 33;                              // second, partial word
	m.tileEvents.assign(66, 0);
	m.tileEvents[2 * 0 + 1] = kTileEventDisabled;
	m.tileEvents[2 * 31 + 1] = kTileEventDisabled | 3;
	m.tileEvents[2 * 32 + 1] = kTileEventDisabled;
	auto g = std::make_unique<Game>();
	g->mapSecretsFound = 2;
	g->lootFound = 7;
	g->destroyedObj = 3;
	auto p = std::make_unique<Player>();
	p->moves = 42;
	auto vm = std::make_unique<ScriptVM>();
	for (int i = 0; i < ScriptVM::kNumStateVars; ++i) vm->vars[i] = static_cast<short>(i * 3 - 100);
	int64_t now = 5000;
	g->curLevelTime = 4000;

	// Entity records: a picked item, a moved monster (short record), a damaged
	// barricade. spawn() is the fresh map state, then the "played" edits.
	m.heightMap.assign(1024, 0);
	m.mapSprites.assign(m.numSprites * 10, 0);
	EntityDef itemDef, monDef, barDef;
	itemDef.eType = Enums::ET_ITEM;
	monDef.eType = Enums::ET_MONSTER;
	monDef.eSubType = 1;
	barDef.eType = Enums::ET_ATTACK_INTERACTIVE;
	barDef.eSubType = Enums::INTERACT_BARRICADE;
	EntityMonster mon;
	g->db.init({ &m });
	auto spr = [&m](int field, int sprite) -> int16_t& { return m.mapSprites[sprite + field * m.numSprites]; };
	auto spawn = [&] {
		g->db.resetEntities();
		const EntityDef* defs[3] = { &itemDef, &monDef, &barDef };
		const int sprites[3] = { 1, 2, 4 };
		for (int i = 0; i < 3; ++i) {
			Entity& e = g->db.entities()[2 + i];
			e.def = defs[i];
			e.setSprite(sprites[i]);
			spr(0, sprites[i]) = static_cast<int16_t>(96 + 64 * i);
			spr(1, sprites[i]) = 96;
			spr(8, sprites[i]) = 64;
			g->db.linkEntity(&e, (96 + 64 * i) >> 6, 1);
		}
		g->db.entities()[3].monster = &mon;
		mon.flags = 0;
	};
	spawn();
	std::vector<Entity>& ents = g->db.entities();
	m.mapSpriteInfo[1] |= 0x10000;                     // item picked up
	g->db.unlinkEntity(&ents[2]);
	ents[2].info |= Entity::kInfoDirty;
	spr(0, 2) = 320;                                   // monster moved to tile (5,1)
	m.mapSpriteInfo[2] = (m.mapSpriteInfo[2] & 0xFFFF00FF) | 0x300;
	mon.flags = Enums::MFLAG_NOTRACK;
	ents[3].info |= Entity::kInfoDirty;
	g->db.linkEntity(&ents[3], 5, 1);
	spr(8, 4) = 20;                                    // barricade damaged
	ents[4].info |= Entity::kInfoDirty;
	g->entityDeathFunctions[3 * 2 + 0] = 2;            // pair in slot 3: restored compacted into slot 0
	g->entityDeathFunctions[3 * 2 + 1] = 7693;
	g->useCaldexHack = true;
	g->useMastermindHack = true;
	g->angryVIOS = true;

	WorldState ws;
	ws.init({ g.get(), p.get(), vm.get(), &m, nullptr, &now });
	ws.saveLevel(1);
	assert(g->totalLevelTime == 1000 && g->curLevelTime == 5000);

	const MapData want = m;
	spawn();
	m.lineFlags.assign(m.lineFlags.size(), 0);
	for (int32_t& v : m.mapSpriteInfo) v ^= 0x10000 | 0x200000;
	for (uint8_t& f : m.mapFlags) f &= 0x7F;
	for (int i = 0; i < m.numTileEvents; ++i) m.tileEvents[2 * i + 1] ^= kTileEventDisabled;
	m.flagsBitmask = 0;
	for (short& v : vm->vars) v = 0;
	g->totalLevelTime = 0;
	g->mapSecretsFound = 0;
	g->lootFound = 0;
	g->destroyedObj = 0;
	std::fill(std::begin(g->entityDeathFunctions), std::end(g->entityDeathFunctions), int16_t(-1));
	g->useCaldexHack = false;
	g->useMastermindHack = false;
	g->angryVIOS = false;
	p->moves = 0;

	const bool ok = ws.restoreLevel(1);
	assert(ok);
	(void)ok;
	assert(m.lineFlags == want.lineFlags);
	for (int k = 0; k < 8; ++k) assert(m.mapSpriteInfo[k] == want.mapSpriteInfo[k]);
	assert(m.mapFlags == want.mapFlags);
	assert(m.tileEvents == want.tileEvents);
	assert(m.flagsBitmask == want.flagsBitmask);
	for (int i = 0; i < ScriptVM::kNumStateVars; ++i)
		assert(vm->vars[i] == (i == kStateVar15 ? 0 : static_cast<short>(i * 3 - 100)));
	assert(g->totalLevelTime == 1000 && g->mapSecretsFound == 2 && g->lootFound == 7 &&
	       g->destroyedObj == 3 && p->moves == 42);
	assert((ents[2].info & Entity::kInfoLinked) == 0);
	assert(spr(0, 2) == 320 && spr(1, 2) == 96 && spr(2, 2) == 32 && mon.flags == Enums::MFLAG_NOTRACK);
	assert((ents[3].info & Entity::kInfoLinked) != 0 && ents[3].linkIndex == 1 * 32 + 5);
	assert(spr(8, 4) == 20);
	assert(g->entityDeathFunctions[0] == 2 && g->entityDeathFunctions[1] == 7693 &&
	       g->entityDeathFunctions[2] == -1 && g->entityDeathFunctions[7] == -1 && g->useCaldexHack);
	assert(g->useMastermindHack && g->angryVIOS);
	std::fprintf(stderr, "[world] round-trip self-check OK\n");
}
#endif

} // namespace

void WorldState::init(const Env& env) {
	env_ = env;
#ifndef NDEBUG
	static bool checked = false;
	if (!checked) {
		checked = true;
		roundTripSelfCheck();
	}
#endif
}

void WorldState::saveLevel(int mapId) {
	if (mapId < 1 || mapId > kNumMaps) return;               // src/Game.cpp:1741-1743
	Game& game = *env_.game;
	Player& player = *env_.player;
	MapData& map = *env_.map;
	ScriptVM& vm = *env_.vm;
	const int64_t now = *env_.gameTime;
	DataWriter w;

	w.writeInt(map.compileDate);                             // :1583
	game.totalLevelTime += static_cast<int>(now - game.curLevelTime);   // :1585-1587
	game.curLevelTime = now;
	w.writeInt(game.totalLevelTime);
	w.writeByte(game.mapSecretsFound);
	for (int i = 0; i < 6; ++i) w.writeInt(0);               // fog (:1591-1596): no fog state
	if (player.ce.weapon == 14)                              // :1597-1599
		w.writeByte(player.activeWeaponDef != nullptr ? static_cast<uint8_t>(player.activeWeaponDef->tileIndex) : 0);
	for (int i = 0; i < 4; ++i) w.writeShort(0);             // placedBombs, brief (:1600-1607)
	EntityState::save({ &game, &map, env_.defs }, w);        // saveEntityStates (:1608)

	int bits = 0, n = 0;                                     // line bits (:1609-1624)
	for (int j = 0; j < map.numLines; ++j) {
		if (n == 8) { w.writeByte(static_cast<uint8_t>(bits)); n = 0; bits = 0; }
		if ((map.lineFlags[j >> 1] & (8 << ((j & 1) << 2))) != 0) bits |= 1 << n;
		++n;
	}
	if (n != 0) w.writeByte(static_cast<uint8_t>(bits));
	int numFuncs = 0;                                        // death funcs (:1625-1636)
	for (int k = 0; k < 64; ++k)
		if (game.entityDeathFunctions[k * 2] != -1) ++numFuncs;
	w.writeByte(static_cast<uint8_t>(numFuncs));
	for (int k = 0; k < 64; ++k) {
		if (game.entityDeathFunctions[k * 2] == -1) continue;
		w.writeShort(game.entityDeathFunctions[k * 2 + 0]);
		w.writeShort(game.entityDeathFunctions[k * 2 + 1]);
	}

	for (int k = 0; k < map.numMapSprites;) {                // sprite bits, 4 per byte (:1637-1662)
		int v = 0;
		for (int b = 0; b < 8; b += 2, ++k) {
			bool hidden = false;
			const Entity* e = game.db.findEntityBySprite(k);
			// Brief corpse rule (:1643-1650); legacy dereferences monster unguarded.
			if (e != nullptr && e->def->eType == Enums::ET_CORPSE && e->def->eSubType != Enums::CORPSE_SKELETON &&
			    e->monster != nullptr && (e->monster->flags & 0x80) == 0) hidden = true;
			if ((map.mapSpriteInfo[k] & 0x10000) != 0) hidden = true;
			if (hidden) v |= 1 << b;
			if ((map.mapSpriteInfo[k] & 0x200000) != 0) v |= 1 << (b + 1);
		}
		w.writeByte(static_cast<uint8_t>(v));
	}
	for (int i = 0; i < 1024; i += 8) {                      // tile bits (:1663-1671)
		int v = 0;
		for (int b = 0; b < 8; ++b)
			if ((map.mapFlags[i + b] & 0x80) != 0) v |= 1 << b;
		w.writeByte(static_cast<uint8_t>(v));
	}
	for (int i = 0; i < ScriptVM::kNumStateVars; ++i)        // :1672-1677
		if (i != kStateVar15) w.writeShort(vm.vars[i]);

	uint32_t word = 0;                                       // tile-event disabled bits (:1679-1692)
	int slot = 0;
	for (int i = 0; i < map.numTileEvents; ++i) {
		slot = i % 32;
		word |= static_cast<uint32_t>((map.tileEvents[i * 2 + 1] & kTileEventDisabled) >> 19) << slot;
		if (slot == 31) { w.writeUInt(word); word = 0; }
	}
	if (slot != 31) w.writeUInt(word);

	w.writeByte(0);                                          // numLerpSprites: pool empty at both save points (ADR 0034)
	w.writeByte(static_cast<uint8_t>(vm.numThreads()));      // :1704-1707
	for (int i = 0; i < ScriptVM::kMaxThreads; ++i) {        // ScriptThread::saveState (src/ScriptThread.cpp:30-43)
		const ScriptThread& t = *vm.threadAt(i);
		w.writeInt(t.unpauseTime == -1 || t.unpauseTime == 0 ? t.unpauseTime
		                                                    : static_cast<int>(t.unpauseTime - now));
		w.writeByte(static_cast<uint8_t>(t.state));
		w.writeInt(t.IP);
		w.writeInt(t.FP);
		for (int s = 0; s < t.FP; ++s) w.writeInt(t.scriptStack[s]);
	}
	w.writeInt(0);                                           // dropIndex: no drops
	w.writeInt(player.moves);
	for (int i = 0; i < 3; ++i) w.writeByte(0);              // notebook count, questComplete, questFailed
	for (int i = 0; i < 16; ++i) w.writeShort(0);            // 8 x (notebookIndex, notebookPosition)
	w.writeByte(static_cast<uint8_t>(map.flagsBitmask));
	w.writeByte(game.useMastermindHack ? 1 : 0);             // :1718
	w.writeByte(game.useCaldexHack ? 1 : 0);                 // :1719
	w.writeShort(-1);                                        // watchLine
	w.writeInt(game.angryVIOS ? 0x40000000 : 0);             // VIOS mallocs (none) | angryVIOS (:1726-1730)
	w.writeShort(game.lootFound);
	w.writeShort(static_cast<int16_t>(game.destroyedObj));
	levels_[mapId - 1] = w.buffer();
}

bool WorldState::restoreLevel(int mapId) {
	if (mapId < 1 || mapId > kNumMaps) return false;         // src/Game.cpp:1741-1743
	const std::vector<uint8_t>& blob = levels_[mapId - 1];
	if (blob.empty()) return false;
	Game& game = *env_.game;
	Player& player = *env_.player;
	MapData& map = *env_.map;
	ScriptVM& vm = *env_.vm;
	DataReader r(blob);

	const int stored = r.readInt();
	if (stored != map.compileDate) {                         // :1902-1909
		if (stored != 0) game.spawnParam = -1;               // legacy spawnParam = 0, its "none"
		game.totalLevelTime = 0;
		return false;
	}
	game.totalLevelTime = r.readInt();
	game.mapSecretsFound = r.readByte();
	r.skip(6 * 4);                                           // fog: no fog state
	if (player.ce.weapon == 14) r.readByte();                // setPickUpWeapon not ported
	r.skip(4 * 2);                                           // placedBombs
	if (!EntityState::load({ &game, &map, env_.defs }, r)) return false;   // loadEntityStates (:1771)

	int bits = 0, n = 8;                                     // line bits, OR only (:1784-1795)
	for (int j = 0; j < map.numLines; ++j) {
		if (n == 8) { bits = r.readByte(); n = 0; }
		if ((bits & (1 << n)) != 0) map.lineFlags[j >> 1] |= static_cast<uint8_t>(8 << ((j & 1) << 2));
		++n;
	}
	for (int b = 0, numFuncs = r.readByte(); b < numFuncs; ++b) {   // death funcs (:1797-1800)
		game.entityDeathFunctions[b * 2 + 0] = r.readShort();
		game.entityDeathFunctions[b * 2 + 1] = r.readShort();
	}

	for (int k = 0; k < map.numMapSprites;) {                // sprite bits (:1800-1820)
		const int v = r.readByte();
		for (int b = 0; b < 8; b += 2, ++k) {
			int32_t& info = map.mapSpriteInfo[k];
			info = (v & (1 << b)) != 0 ? (info | 0x10000) : (info & ~0x10000);
			info = (v & (1 << (b + 1))) != 0 ? (info | 0x200000) : (info & ~0x200000);
		}
	}
	for (int i = 0; i < 1024; i += 8) {                      // tile bits, OR only
		const int v = r.readByte();
		for (int b = 0; b < 8; ++b)
			if ((v & (1 << b)) != 0) map.mapFlags[i + b] |= 0x80;
	}
	for (int i = 0; i < ScriptVM::kNumStateVars; ++i)
		if (i != kStateVar15) vm.vars[i] = r.readShort();

	uint32_t word = r.readUInt();                            // tile-event disabled bits (:1838-1847)
	for (int i = 0; i < map.numTileEvents; ++i) {
		const int slot = i % 32;
		map.tileEvents[i * 2 + 1] = (map.tileEvents[i * 2 + 1] & ~kTileEventDisabled) |
		                            static_cast<int32_t>(((word >> slot) & 1) << 19);
		if (slot == 31 && i < map.numTileEvents - 1) word = r.readUInt();
	}

	if (r.readByte() != 0) {                                 // numLerpSprites
		std::fprintf(stderr, "[world] map %d: lerp records not supported\n", mapId);
		return false;
	}
	// Thread records are read and dropped: legacy never restores stackPtr, so
	// every slot ends up free (src/ScriptThread.cpp:45-57, src/Game.cpp:1858-1868).
	r.readByte();
	for (int i = 0; i < ScriptVM::kMaxThreads; ++i) {
		r.skip(4 + 1 + 4);                                   // unpauseTime, state, IP
		r.skip(static_cast<size_t>(r.readInt()) * 4);        // FP + stack
	}
	r.readInt();                                             // dropIndex
	player.moves = r.readInt();
	r.skip(3 + 16 * 2);                                      // notebook / quest
	map.flagsBitmask = static_cast<int8_t>(r.readByte());
	game.useMastermindHack = r.readByte() == 1;              // :1886
	game.useCaldexHack = r.readByte() == 1;                  // :1887
	r.skip(2);                                               // watchLine
	game.angryVIOS = (r.readInt() & 0x40000000) != 0;        // :1892-1894
	game.lootFound = r.readShort();
	game.destroyedObj = r.readShort();

	if (r.position() != blob.size())
		std::fprintf(stderr, "[world] size mismatch map %d: read %zu of %zu\n", mapId, r.position(), blob.size());
	std::fprintf(stderr, "[world] map %d restored; prepareMonsters deferred\n", mapId);   // src/Game.cpp:701-727, deviation MD3
	return true;
}

void WorldState::savePlayer() {
	player_ = *env_.player;
	hasPlayer_ = true;
}

bool WorldState::restorePlayer() {
	if (!hasPlayer_) return false;
	Player& p = *env_.player;
	const int deaths = p.totalDeaths;                        // config-file fields (ADR 0034 §5)
	const int currentGrades = p.currentGrades;
	const int bestGrades = p.bestGrades;
	p = player_;
	p.totalDeaths = deaths;
	p.currentGrades = currentGrades;
	p.bestGrades = bestGrades;
	p.currentLevelDeaths = 0;                                // src/Game.cpp:2200-2202
	p.facingEntity = nullptr;                                // entity array is rebuilt by the load
	return true;
}

void WorldState::clear() {
	for (std::vector<uint8_t>& l : levels_) l.clear();
	hasPlayer_ = false;
}

} // namespace newcore
