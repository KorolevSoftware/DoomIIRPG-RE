#include "io/EntityDefs.h"

#include "io/DataReader.h"

namespace newcore {

bool EntityDefs::load(const std::vector<uint8_t>& data) {
	DataReader reader(data);
	if (data.size() < 2) return false;

	int count = reader.readShort();
	defs_.clear();
	defs_.reserve(static_cast<size_t>(count));

	for (int i = 0; i < count; ++i) {
		if (reader.remaining() < 8) return false;
		EntityDef def;
		def.tileIndex = reader.readShort();
		def.eType = reader.readUByte();
		def.eSubType = reader.readUByte();
		def.parm = reader.readUByte();
		def.name = static_cast<int16_t>(reader.readUByte());
		def.longName = static_cast<int16_t>(reader.readUByte());
		def.description = static_cast<int16_t>(reader.readUByte());
		defs_.push_back(def);
	}
	return true;
}

const EntityDef* EntityDefs::find(int eType, int eSubType, int parm) const {
	for (const auto& def : defs_) {
		if (def.eType == eType && def.eSubType == eSubType &&
			(parm == -1 || def.parm == parm)) {
			return &def;
		}
	}
	return nullptr;
}

const EntityDef* EntityDefs::lookup(int tileIndex) const {
	for (const auto& def : defs_) {
		if (def.tileIndex == tileIndex) return &def;
	}
	return nullptr;
}

} // namespace newcore