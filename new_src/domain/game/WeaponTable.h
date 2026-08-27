#ifndef NEW_DOMAIN_GAME_WEAPONTABLE_H
#define NEW_DOMAIN_GAME_WEAPONTABLE_H

#include <cstdint>

namespace newcore {

// Parsed records of the two weapon tables in tables.bin. The legacy code
// indexes them with bare row arithmetic (`weapons[id * 9 + field]`,
// `wpinfo[w * 6 + n]`); here each row is parsed once by Tables::load
// (new_src/io/Tables.cpp) and read by name. Values stay bit-identical: the
// members are signed bytes in file order.

// tables.bin table 2, 9 signed bytes per weapon
// (src/Combat.h:26-35 WEAPON_FIELD_* / WEAPON_MAX_FIELDS = 9). Note that
// src/Combat.h:36 also declares WEAPON_STRIDE = 8; it is NOT this table's
// pitch and has no reader in the original code.
struct WeaponDef {
	int8_t strMin = 0;
	int8_t strMax = 0;
	int8_t rangeMin = 0;
	int8_t rangeMax = 0;
	int8_t ammoType = 0;
	int8_t ammoUsage = 0;
	int8_t projType = 0;
	int8_t numShots = 0;
	int8_t shothold = 0;
};

// Only the size is load-bearing: Tables::load uses sizeof(WeaponDef) as the
// record stride (bytes.size() / sizeof(...), i * sizeof(...)), so any padding
// would silently shift every row. Member ORDER is not part of the file format
// -- the loader assigns each field by name (new_src/io/Tables.cpp:69-77), so
// no offsetof asserts are needed here.
static_assert(sizeof(WeaponDef) == 9, "src/Combat.h:35 WEAPON_MAX_FIELDS");

// tables.bin table 1 (wpinfo), 6 signed bytes per weapon: idle/attack/flash
// screen offsets (src/Combat.h:53-59 FIELD_COUNT / FLD_WP*, read at
// src/Combat.cpp:705-719).
struct WeaponPose {
	int8_t idleX = 0;
	int8_t idleY = 0;
	int8_t atkX = 0;
	int8_t atkY = 0;
	int8_t flashX = 0;
	int8_t flashY = 0;
};

// Same reasoning as for WeaponDef: sizeof is the record stride used by
// Tables::load, member order is not (the pose loop assigns by name).
static_assert(sizeof(WeaponPose) == 6, "src/Combat.h:53 FIELD_COUNT");

} // namespace newcore

#endif // NEW_DOMAIN_GAME_WEAPONTABLE_H
