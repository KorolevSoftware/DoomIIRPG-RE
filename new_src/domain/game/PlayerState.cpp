#include "domain/game/PlayerState.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "domain/game/Enums.h"
#include "domain/game/Player.h"
#include "io/DataReader.h"
#include "io/DataWriter.h"

namespace newcore {
namespace PlayerState {

namespace {

constexpr int kNumMaps = 10;
constexpr int kMaxEffects = Enums::MAX_STATUS_EFFECTS;   // 18

// CombatEntity::saveState(OS, true) (src/CombatEntity.cpp:118-129).
void saveCe(const CombatEntity& ce, DataWriter& w) {
	w.writeShort(static_cast<int16_t>(ce.stats[0]));
	w.writeShort(static_cast<int16_t>(ce.stats[1]));
	for (int i = 2; i < 8; ++i) w.writeByte(static_cast<uint8_t>(ce.stats[i]));
	w.writeByte(static_cast<uint8_t>(ce.weapon));
}

// CombatEntity::loadState(IS, true) (src/CombatEntity.cpp:96-107); legacy
// readByte is unsigned. An out-of-range weapon byte (0xFF = none) becomes -1.
void loadCe(DataReader& r, CombatEntity& ce) {
	ce.stats[0] = r.readShort();
	ce.stats[1] = r.readShort();
	for (int i = 2; i < 8; ++i) ce.stats[i] = r.readByte();
	const int wp = r.readByte();
	ce.weapon = wp < Enums::WP_PLAYERMAX ? wp : -1;
}

} // namespace

void save(const Player& p, const Header& h, DataWriter& w) {
	w.writeShort(static_cast<int16_t>(h.mapId));               // src/Game.cpp:2157-2169
	w.writeShort(static_cast<int16_t>(h.viewX));
	w.writeShort(static_cast<int16_t>(h.viewY));
	w.writeShort(static_cast<int16_t>(h.viewAngle));
	w.writeShort(static_cast<int16_t>(h.viewPitch));
	w.writeInt(h.prevX);
	w.writeInt(h.prevY);
	w.writeInt(h.saveX);
	w.writeInt(h.saveY);
	w.writeInt(h.saveZ);
	w.writeShort(static_cast<int16_t>(h.saveAngle));
	w.writeShort(static_cast<int16_t>(h.savePitch));

	saveCe(p.baseCe, w);                                      // src/Player.cpp:1351-1420
	saveCe(p.ce, w);
	w.writeShort(static_cast<int16_t>(p.characterChoice));
	w.writeInt(p.weapons);
	w.writeInt(p.weaponsCopy);
	w.writeByte(static_cast<uint8_t>(p.level));
	w.writeInt(p.currentXP);
	w.writeInt(0);                                            // totalTime: not tracked
	w.writeInt(0);                                            // totalMoves
	w.writeInt(p.completedLevels);
	w.writeInt(0);                                            // killedMonstersLevels
	w.writeInt(p.foundSecretsLevels);
	w.writeInt(p.disabledWeapons);
	w.writeByte(0);                                           // prevWeapon
	w.writeInt(0);                                            // gamePlayedMask
	w.writeInt(0);                                            // lastCombatTurn
	w.writeBool(false);                                       // inCombat
	w.writeShort(static_cast<int16_t>(p.highestMap));
	w.writeBool(false);                                       // isFamiliar
	w.writeShort(0);                                          // familiarType
	w.writeInt(0);                                            // playerEntityCopyIndex
	w.writeInt(p.hackedVendingMachines);
	w.writeInt(p.vendingMachineHackTriesLeft1);
	w.writeInt(p.vendingMachineHackTriesLeft2);
	w.writeInt(p.chainsawStrengthBonusCount);
	w.writeByte(0);                                           // lastSkipCode
	for (int i = 0; i < 9; ++i) w.writeShort(p.ammo[i]);
	for (int i = 0; i < 9; ++i) w.writeShort(p.ammoCopy[i]);
	for (int i = 0; i < 26; ++i) w.writeShort(p.inventory[i]);
	for (int i = 0; i < 26; ++i) w.writeShort(0);             // inventoryCopy: no field
	w.writeByte(static_cast<uint8_t>(p.numStatusEffects));
	if (p.numStatusEffects != 0) {
		for (int i = 0; i < kMaxEffects; ++i) {
			w.writeShort(static_cast<int16_t>(p.statusEffects[36 + i]));
			w.writeShort(static_cast<int16_t>(p.statusEffects[0 + i]));
			w.writeShort(static_cast<int16_t>(p.statusEffects[18 + i]));
		}
	}
	for (int i = 0; i < 8; ++i) w.writeInt(0);                // counters
	w.writeBool(p.gameCompleted);
}

bool load(DataReader& r, Player& p, Header& h) {
	Header th;
	th.mapId = r.readShort();                                 // src/Game.cpp:2173-2189
	th.viewX = r.readShort();
	th.viewY = r.readShort();
	th.viewAngle = r.readShort();
	th.viewPitch = r.readShort();
	th.prevX = r.readInt();
	th.prevY = r.readInt();
	th.saveX = r.readInt();
	th.saveY = r.readInt();
	th.saveZ = r.readInt();
	th.saveAngle = r.readShort();
	th.savePitch = r.readShort();
	if (th.mapId < 1 || th.mapId > kNumMaps) {
		std::fprintf(stderr, "[player] bad mapId %d\n", th.mapId);
		return false;
	}

	Player tp = p;                                            // src/Player.cpp:1270-1349
	loadCe(r, tp.baseCe);
	loadCe(r, tp.ce);
	tp.characterChoice = std::min(std::max<int>(r.readShort(), 1), 3);
	tp.weapons = r.readInt();
	tp.weaponsCopy = r.readInt();
	tp.level = r.readByte();
	tp.currentXP = r.readInt();
	tp.nextLevelXP = tp.calcLevelXP(tp.level);
	r.skip(4 + 4);                                            // totalTime, totalMoves
	tp.completedLevels = r.readInt();
	r.readInt();                                              // killedMonstersLevels
	tp.foundSecretsLevels = r.readInt();
	tp.disabledWeapons = r.readInt();
	r.skip(1 + 4 + 4 + 1);                                    // prevWeapon, gamePlayedMask, lastCombatTurn, inCombat
	const int highest = r.readShort();
	tp.highestMap = highest < 1 ? 1 : (highest > kNumMaps ? kNumMaps : highest);
	const bool isFamiliar = r.readBool();
	r.skip(2 + 4);                                            // familiarType, playerEntityCopyIndex
	tp.hackedVendingMachines = r.readInt();
	tp.vendingMachineHackTriesLeft1 = r.readInt();
	tp.vendingMachineHackTriesLeft2 = r.readInt();
	tp.chainsawStrengthBonusCount = r.readInt();
	r.readByte();                                             // lastSkipCode
	for (int i = 0; i < 9; ++i) tp.ammo[i] = r.readShort();
	for (int i = 0; i < 9; ++i) tp.ammoCopy[i] = r.readShort();
	for (int i = 0; i < 26; ++i) tp.inventory[i] = r.readShort();
	r.skip(26 * 2);                                           // inventoryCopy
	tp.numStatusEffects = r.readByte();
	if (tp.numStatusEffects > kMaxEffects) {
		std::fprintf(stderr, "[player] bad status effect count %d\n", tp.numStatusEffects);
		return false;
	}
	for (int i = 0; i < kMaxEffects; ++i) {
		const bool any = tp.numStatusEffects != 0;
		tp.statusEffects[36 + i] = any ? r.readShort() : 0;
		tp.statusEffects[0 + i] = any ? r.readShort() : 0;
		tp.statusEffects[18 + i] = any ? r.readShort() : 0;
	}
	if (isFamiliar) {                                         // statusEffectsCopy: read and discarded
		const int n = r.readByte();
		if (n > kMaxEffects) {
			std::fprintf(stderr, "[player] bad status effect copy count %d\n", n);
			return false;
		}
		if (n != 0) r.skip(kMaxEffects * 3 * 2);
	}
	r.skip(8 * 4);                                            // counters
	tp.gameCompleted = r.readBool();
	if (r.overrun() || r.remaining() != 0) {
		std::fprintf(stderr, "[player] bad record size %zu\n", r.size());
		return false;
	}
	tp.translateStatusEffects();
	tp.updateStats();
	p = tp;
	h = th;
	return true;
}

#ifndef NDEBUG
// Debug round trip: save, load into a fresh Player, compare; then a record
// one byte short and one byte long must both be rejected without side effects.
void selfCheck() {
	Player src;
	src.baseCe.stats[0] = 87;
	src.baseCe.stats[1] = 120;
	src.baseCe.stats[5] = 200;
	src.baseCe.weapon = 2;
	src.ce = src.baseCe;
	src.ce.weapon = -1;
	src.weapons = 0x1A5;
	src.weaponsCopy = 0x3;
	src.characterChoice = 3;
	src.level = 4;
	src.currentXP = 1234;
	src.completedLevels = 0x7;
	src.foundSecretsLevels = 0x2;
	src.disabledWeapons = 0x10;
	src.highestMap = 5;
	src.hackedVendingMachines = 0x48;
	src.vendingMachineHackTriesLeft1 = 0x1FF;
	src.vendingMachineHackTriesLeft2 = 0x24;
	src.chainsawStrengthBonusCount = 2;
	for (int i = 0; i < 9; ++i) {
		src.ammo[i] = static_cast<int16_t>(i * 11);
		src.ammoCopy[i] = static_cast<int16_t>(i + 1);
	}
	for (int i = 0; i < 26; ++i) src.inventory[i] = static_cast<int16_t>(i * 3);
	src.numStatusEffects = 2;
	src.statusEffects[0 + 3] = 5;     // turns
	src.statusEffects[18 + 3] = 20;   // amount
	src.statusEffects[36 + 3] = 1;    // stack count
	src.statusEffects[0 + 14] = 9;
	src.statusEffects[18 + 14] = -4;
	src.statusEffects[36 + 14] = 2;
	src.gameCompleted = true;
	Header hs;
	hs.mapId = 3;
	hs.viewX = 352;
	hs.viewY = -96;
	hs.viewAngle = 768;
	hs.viewPitch = -12;
	hs.prevX = 100000;
	hs.saveZ = -7;
	hs.savePitch = 3;

	DataWriter w;
	save(src, hs, w);
	const std::vector<uint8_t> blob = w.buffer();
	assert(blob.size() == 305 + 108);

	Player dst;
	Header hd;
	DataReader r(blob);
	const bool ok = load(r, dst, hd);
	assert(ok);
	(void)ok;
	assert(hd.mapId == 3 && hd.viewX == 352 && hd.viewY == -96 && hd.viewAngle == 768 &&
	       hd.viewPitch == -12 && hd.prevX == 100000 && hd.saveZ == -7 && hd.savePitch == 3);
	for (int i = 0; i < 8; ++i) assert(dst.baseCe.stats[i] == src.baseCe.stats[i]);
	assert(dst.baseCe.weapon == 2 && dst.ce.weapon == -1 && dst.ce.stats[0] == 87);
	assert(dst.weapons == src.weapons && dst.weaponsCopy == src.weaponsCopy && dst.characterChoice == 3);
	assert(dst.level == 4 && dst.currentXP == 1234 && dst.nextLevelXP == dst.calcLevelXP(4));
	assert(dst.completedLevels == 0x7 && dst.foundSecretsLevels == 0x2 && dst.disabledWeapons == 0x10);
	assert(dst.highestMap == 5 && dst.hackedVendingMachines == 0x48 &&
	       dst.vendingMachineHackTriesLeft1 == 0x1FF && dst.vendingMachineHackTriesLeft2 == 0x24 &&
	       dst.chainsawStrengthBonusCount == 2);
	for (int i = 0; i < 9; ++i) assert(dst.ammo[i] == src.ammo[i] && dst.ammoCopy[i] == src.ammoCopy[i]);
	for (int i = 0; i < 26; ++i) assert(dst.inventory[i] == src.inventory[i]);
	assert(dst.numStatusEffects == 2);
	for (int i = 0; i < 54; ++i) assert(dst.statusEffects[i] == src.statusEffects[i]);
	assert(dst.gameCompleted);

	const std::vector<uint8_t> shortBlob(blob.begin(), blob.end() - 1);
	Player untouched;
	Header hu;
	DataReader rs(shortBlob);
	const bool okShort = load(rs, untouched, hu);
	assert(!okShort && untouched.level == 1 && untouched.weapons == 0 && hu.mapId == 0);
	(void)okShort;

	std::vector<uint8_t> longBlob = blob;
	longBlob.push_back(0);
	DataReader rl(longBlob);
	const bool okLong = load(rl, untouched, hu);
	assert(!okLong && untouched.level == 1 && hu.mapId == 0);
	(void)okLong;
	std::fprintf(stderr, "[player] state self-check OK\n");
}
#endif

} // namespace PlayerState
} // namespace newcore
