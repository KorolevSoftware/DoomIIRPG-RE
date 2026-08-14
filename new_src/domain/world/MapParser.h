#ifndef NEW_DOMAIN_WORLD_MAPPARSER_H
#define NEW_DOMAIN_WORLD_MAPPARSER_H

#include <cstdint>
#include <vector>

#include "domain/world/MapData.h"

namespace newcore {

// Parses a mapXX.bin level file into a MapData structure. Mirrors the legacy
// LoadingManager::loadMapData field-by-field (map version 3). Reading is done
// over an in-memory buffer via DataReader.
class MapParser {
public:
	// Returns true on success and fills `out`.
	static bool parse(const std::vector<uint8_t>& data, MapData& out);

	// Decodes world polygons from nodePolys (leaf nodes only) into out.polygons.
	// Called by parse; exposed for incremental debugging.
	static void decodePolys(MapData& out);

private:
	// A marker is a 4-byte value (0xCAFEBABE or 0xDEADBEEF) between sections.
	// It is read and discarded.
	// (No state required; helper lives in the .cpp.)
};

} // namespace newcore

#endif // NEW_DOMAIN_WORLD_MAPPARSER_H