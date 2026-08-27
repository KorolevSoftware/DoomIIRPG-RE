#ifndef NEW_IO_TABLES_H
#define NEW_IO_TABLES_H

#include <cstdint>
#include <vector>

#include "domain/game/WeaponTable.h"

namespace newcore {

// Indexes into the tables.bin offset table. Element types per table:
//   0  short   combat monster attacks
//   1  byte    combat weapon info
//   2  byte    combat weapon data
//   3  byte    combat monster stats
//   4  int     combat masks
//   5  byte    canvas keys numeric
//   6  byte    canvas OSC cycle
//   7  short   game level names
//   8  byte    monster colors
//   9  int     render sin table
//   10 short   energy drink data
//   11 byte    monster weakness
//   12 int     canvas movie effects
//   13 byte    monster sounds
//   16/18 ushort sky palettes, 17/19 ubyte sky texels (per map group)
enum TableId {
	kMonsterAttacks = 0,
	kWeaponInfo = 1,
	kWeaponData = 2,
	kMonsterStats = 3,
	kCombatMasks = 4,
	kKeysNumeric = 5,
	kOscCycle = 6,
	kLevelNames = 7,
	kMonsterColors = 8,
	kSinTable = 9,
	kEnergyDrinkData = 10,
	kMonsterWeakness = 11,
	kMovieEffects = 12,
	kMonsterSounds = 13,
	kSkyPaletteA = 16,
	kSkyTexelA = 17,
	kSkyPaletteB = 18,
	kSkyTexelB = 19,
};

// Parsed copy of tables.bin. Value semantics, movable.
class Tables {
public:
	bool load(const std::vector<uint8_t>& data);

	// Weapon row lookups. An out-of-range id (including a negative one) reads
	// as an all-zero record / a null pose, which is what the former per-field
	// bounds clamp produced for a missing row.
	const WeaponDef& weaponDef(int weaponId) const;
	const WeaponPose* weaponPose(int weaponId) const;

	std::vector<int16_t> monsterAttacks;
	std::vector<WeaponPose> weaponPoses;   // table 1 (wpinfo), 6 bytes/row
	std::vector<WeaponDef> weaponDefs;     // table 2, 9 bytes/row
	std::vector<int8_t> monsterStats;
	std::vector<int32_t> combatMasks;
	std::vector<int8_t> keysNumeric;
	std::vector<int8_t> oscCycle;
	std::vector<int16_t> levelNames;
	std::vector<uint8_t> monsterColors;
	std::vector<int32_t> sinTable;
	std::vector<int16_t> energyDrinkData;
	std::vector<int8_t> monsterWeakness;
	std::vector<int32_t> movieEffects;
	std::vector<uint8_t> monsterSounds;
	std::vector<uint16_t> skyPaletteA;
	std::vector<uint8_t> skyTexelA;
	std::vector<uint16_t> skyPaletteB;
	std::vector<uint8_t> skyTexelB;

private:
	bool loadTable(const std::vector<uint8_t>& data, size_t offset,
		size_t byteCount, std::vector<int8_t>& out);
	bool loadTable(const std::vector<uint8_t>& data, size_t offset,
		size_t byteCount, std::vector<int16_t>& out);
	bool loadTable(const std::vector<uint8_t>& data, size_t offset,
		size_t byteCount, std::vector<int32_t>& out);
	bool loadTable(const std::vector<uint8_t>& data, size_t offset,
		size_t byteCount, std::vector<uint8_t>& out);
	bool loadTable(const std::vector<uint8_t>& data, size_t offset,
		size_t byteCount, std::vector<uint16_t>& out);
};

} // namespace newcore

#endif // NEW_IO_TABLES_H