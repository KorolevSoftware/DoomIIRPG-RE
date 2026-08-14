#ifndef NEW_IO_ENTITYDEFS_H
#define NEW_IO_ENTITYDEFS_H

#include <cstdint>
#include <vector>

namespace newcore {

// Definition of a game object type (items, monsters, etc.) parsed from
// entities.bin. Same binary layout as the legacy EntityDef.
struct EntityDef {
	int16_t tileIndex = 0;
	int16_t name = 0;
	int16_t longName = 0;
	int16_t description = 0;
	uint8_t eType = 0;
	uint8_t eSubType = 0;
	uint8_t parm = 0;
	uint8_t touchMe = 0;
};

class EntityDefs {
public:
	bool load(const std::vector<uint8_t>& data);

	const std::vector<EntityDef>& defs() const { return defs_; }
	int count() const { return static_cast<int>(defs_.size()); }

	const EntityDef* find(int eType, int eSubType, int parm = -1) const;
	const EntityDef* lookup(int tileIndex) const;

private:
	std::vector<EntityDef> defs_;
};

} // namespace newcore

#endif // NEW_IO_ENTITYDEFS_H