#include "io/Tables.h"

#include "io/DataReader.h"

namespace newcore {

bool Tables::load(const std::vector<uint8_t>& data) {
	if (data.size() < 80) return false;

	DataReader reader(data);
	std::vector<uint32_t> offsets(20);
	for (int i = 0; i < 20; ++i) offsets[i] = reader.readUInt();

	constexpr size_t kHeaderSize = 80;

	// Table i spans [kHeaderSize + (i==0 ? 0 : offsets[i-1]), kHeaderSize + offsets[i]).
	bool ok = true;
	auto range = [&](int i, size_t& start, size_t& count) {
		size_t base = (i == 0) ? kHeaderSize : (kHeaderSize + offsets[i - 1]);
		size_t end = kHeaderSize + offsets[i];
		if (end > data.size() || base > end) { ok = false; return; }
		start = base;
		count = end - base;
	};

	// Tables 1 and 2 are read as raw signed bytes and then split into rows;
	// the byte vectors themselves have no readers left.
	std::vector<int8_t> weaponInfoBytes;
	std::vector<int8_t> weaponDataBytes;

	size_t s, c;
	range(0, s, c); ok = ok && loadTable(data, s, c, monsterAttacks);
	range(1, s, c); ok = ok && loadTable(data, s, c, weaponInfoBytes);
	range(2, s, c); ok = ok && loadTable(data, s, c, weaponDataBytes);
	range(3, s, c); ok = ok && loadTable(data, s, c, monsterStats);
	range(4, s, c); ok = ok && loadTable(data, s, c, combatMasks);
	range(5, s, c); ok = ok && loadTable(data, s, c, keysNumeric);
	range(6, s, c); ok = ok && loadTable(data, s, c, oscCycle);
	range(7, s, c); ok = ok && loadTable(data, s, c, levelNames);
	range(8, s, c); ok = ok && loadTable(data, s, c, monsterColors);
	range(9, s, c); ok = ok && loadTable(data, s, c, sinTable);
	range(10, s, c); ok = ok && loadTable(data, s, c, energyDrinkData);
	range(11, s, c); ok = ok && loadTable(data, s, c, monsterWeakness);
	range(12, s, c); ok = ok && loadTable(data, s, c, movieEffects);
	range(13, s, c); ok = ok && loadTable(data, s, c, monsterSounds);
	range(16, s, c); ok = ok && loadTable(data, s, c, skyPaletteA);
	range(17, s, c); ok = ok && loadTable(data, s, c, skyTexelA);
	range(18, s, c); ok = ok && loadTable(data, s, c, skyPaletteB);
	range(19, s, c); ok = ok && loadTable(data, s, c, skyTexelB);

	// Row split. A trailing partial row is dropped: the legacy reader clamped
	// every out-of-table field to 0 anyway, and both tables are an exact
	// multiple of their row size in the shipped tables.bin.
	weaponPoses.resize(weaponInfoBytes.size() / sizeof(WeaponPose));
	for (size_t i = 0; i < weaponPoses.size(); ++i) {
		const int8_t* r = weaponInfoBytes.data() + i * sizeof(WeaponPose);
		WeaponPose& p = weaponPoses[i];
		p.idleX = r[0];
		p.idleY = r[1];
		p.atkX = r[2];
		p.atkY = r[3];
		p.flashX = r[4];
		p.flashY = r[5];
	}
	weaponDefs.resize(weaponDataBytes.size() / sizeof(WeaponDef));
	for (size_t i = 0; i < weaponDefs.size(); ++i) {
		const int8_t* r = weaponDataBytes.data() + i * sizeof(WeaponDef);
		WeaponDef& w = weaponDefs[i];
		w.strMin = r[0];
		w.strMax = r[1];
		w.rangeMin = r[2];
		w.rangeMax = r[3];
		w.ammoType = r[4];
		w.ammoUsage = r[5];
		w.projType = r[6];
		w.numShots = r[7];
		w.shothold = r[8];
	}

	return ok;
}

const WeaponDef& Tables::weaponDef(int weaponId) const {
	static const WeaponDef kMissing{};
	if (weaponId < 0 || (size_t)weaponId >= weaponDefs.size()) return kMissing;
	return weaponDefs[(size_t)weaponId];
}

const WeaponPose* Tables::weaponPose(int weaponId) const {
	if (weaponId < 0 || (size_t)weaponId >= weaponPoses.size()) return nullptr;
	return &weaponPoses[(size_t)weaponId];
}

template <typename T>
static std::vector<T> loadRaw(const std::vector<uint8_t>& data, size_t start, size_t count) {
	std::vector<T> out;
	size_t n = count / sizeof(T);
	out.resize(n);
	if (n > 0) {
		std::memcpy(out.data(), data.data() + start, n * sizeof(T));
	}
	return out;
}

// Each table is prefixed by an int32 element count (in elements of the
// table's own type). Convert to bytes to read the same range the legacy code
// does, then reinterpret. Values are little-endian.
template <typename T>
static bool loadTypedTable(const std::vector<uint8_t>& data, size_t start, size_t byteCount, std::vector<T>& out) {
	if (byteCount < 4) return false;
	DataReader reader(data);
	reader.seek(start);
	int32_t elementCount = reader.readInt();

	// Data payload starts after the 4-byte prefix.
	size_t payloadBytes = byteCount - 4;
	size_t wantBytes = static_cast<size_t>(elementCount) * sizeof(T);
	if (wantBytes > payloadBytes) wantBytes = payloadBytes;

	size_t n = wantBytes / sizeof(T);
	out.resize(n);
	for (size_t i = 0; i < n; ++i) {
		if constexpr (sizeof(T) == 1) {
			out[i] = static_cast<T>(reader.readUByte());
		} else if constexpr (sizeof(T) == 2) {
			out[i] = static_cast<T>(reader.readUShort());
		} else {
			out[i] = static_cast<T>(reader.readUInt());
		}
	}
	return true;
}

bool Tables::loadTable(const std::vector<uint8_t>& data, size_t start, size_t byteCount, std::vector<int8_t>& out) {
	return loadTypedTable(data, start, byteCount, out);
}
bool Tables::loadTable(const std::vector<uint8_t>& data, size_t start, size_t byteCount, std::vector<int16_t>& out) {
	return loadTypedTable(data, start, byteCount, out);
}
bool Tables::loadTable(const std::vector<uint8_t>& data, size_t start, size_t byteCount, std::vector<int32_t>& out) {
	return loadTypedTable(data, start, byteCount, out);
}
bool Tables::loadTable(const std::vector<uint8_t>& data, size_t start, size_t byteCount, std::vector<uint8_t>& out) {
	return loadTypedTable(data, start, byteCount, out);
}
bool Tables::loadTable(const std::vector<uint8_t>& data, size_t start, size_t byteCount, std::vector<uint16_t>& out) {
	return loadTypedTable(data, start, byteCount, out);
}

} // namespace newcore