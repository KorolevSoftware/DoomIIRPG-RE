#ifndef NEW_DOMAIN_WORLD_MAPDATA_H
#define NEW_DOMAIN_WORLD_MAPDATA_H

#include <cstdint>
#include <vector>

namespace newcore {

// Raw parsed representation of a mapXX.bin level file. Mirrors the fields that
// the legacy LoadingManager reads out of the map stream. Geometry is expressed
// in world units where MAPTILE_SIZE == 64 (a 32x32 tile grid = 2048 units).
class MapData {
public:
	static constexpr int kMapSize = 32;
	static constexpr int kMapTileSize = 64;
	static constexpr int kMapWorldMax = 2047;
	static constexpr int kMaxMediaImages = 256;
	static constexpr int kMaxMediaMappings = 32768;

	// ---- Header ----
	uint8_t version = 0;
	int32_t compileDate = 0;
	int spawnIndex = 0;
	uint8_t spawnDir = 0;
	int8_t flagsBitmask = 0;
	uint8_t totalSecrets = 0;
	uint8_t totalLoot = 0;
	int numNodes = 0;
	int dataSizePolys = 0;
	int numLines = 0;
	int numNormals = 0;
	int numNormalSprites = 0;
	int numZSprites = 0;
	int numTileEvents = 0;
	int mapByteCodeSize = 0;
	int totalMayaCameras = 0;
	int totalMayaCameraKeys = 0;
	int totalMayaTweens = 0;

	// ---- Media table (loaded from newMappings.bin + registerMapMedia) ----
	std::vector<uint16_t> mediaMappings;   // kMaxMediaMappings entries
	std::vector<uint8_t>  mediaDimensions; // kMaxMediaImages entries (bytes per texel / 2)
	std::vector<int16_t>  mediaBounds;     // kMaxMediaImages * 4 (x, y, w, h)
	std::vector<int32_t>  mediaPalColors;
	std::vector<int32_t>  mediaPaletteSizes;
	std::vector<int32_t>  mediaTexelSizes;
	std::vector<int32_t>  mediaTexelSizes2;
	int mediaCount = 0;
	std::vector<uint16_t> mediaIds; // the media indices referenced by this map

	// ---- Geometry ----
	std::vector<int16_t> normals;          // numNormals * 3 (x,y,z)
	std::vector<int16_t> nodeOffsets;      // numNodes
	std::vector<uint8_t> nodeNormalIdxs;   // numNodes
	std::vector<int16_t> nodeChildOffset1; // numNodes
	std::vector<int16_t> nodeChildOffset2; // numNodes
	std::vector<uint8_t> nodeBounds;       // numNodes * 4
	std::vector<uint8_t> nodePolys;        // dataSizePolys
	std::vector<uint8_t> lineFlags;        // (numLines + 1) / 2
	std::vector<uint8_t> lineXs;           // numLines * 2
	std::vector<uint8_t> lineYs;           // numLines * 2
	std::vector<uint8_t> heightMap;        // 1024 (32x32 tiles)

	// Terrain height in canvas units (legacy MovementController::getHeight;
	// port of new_src/core/GameContext.cpp:299-304).
	int heightAt(int x, int y) const {
		const int hx = x & 0x7FF, hy = y & 0x7FF;
		return heightMap[(hy >> 6) * 32 + (hx >> 6)] << 3;
	}

	// Legacy baked S_Z (postProcessSprites, src/Render.cpp:2459-2467): stored Z + terrain,
	// z-sprites -32. The rewrite stores raw Z and bakes at use (SpriteLerps::spriteZBias).
	int spriteZ(int s) const {
		const int x = mapSprites[s], y = mapSprites[numSprites + s];
		return mapSprites[2 * numSprites + s] + heightAt(x, y) - (s >= numNormalSprites ? 32 : 0);
	}

	// ---- Sprites ----
	// mapSprites is a flat array with 9 fields per sprite (X,Y,Z,RENDERMODE,
	// NODE,NODENEXT,VIEWNEXT,ENT,SCALEFACTOR). mapSpriteInfo carries per-sprite
	// packed flags (see Render::postProcessSprites).
	std::vector<int16_t> mapSprites;    // numSprites * 10 (9 fields + slack)
	std::vector<int32_t> mapSpriteInfo; // numSprites * 2 (low word + high word)
	// numSprites = the file's sprites (numMapSprites) + kMaxCustomSprites
	// hidden runtime slots appended after them (src/Render.cpp:2497-2508, ADR 0030).
	static constexpr int kMaxCustomSprites = 48;   // src/Render.h:53
	int numMapSprites = 0;
	int numSprites = 0;
	int firstCustomSprite() const { return numMapSprites; }

	// ---- Misc ----
	std::vector<uint32_t> staticFuncs; // 12 ints
	std::vector<int32_t> tileEvents;   // numTileEvents * 2
	std::vector<uint8_t> mapByteCode;  // mapByteCodeSize
	std::vector<uint8_t> mapFlags;     // 1024 tile flags (two tiles per byte packed)
	// Automap markers written by EV_MARKTILE/EV_UNMARKTILE; -1 = none. Reset by every
	// map load through the fresh MapData (src/LoadingManager.cpp:317-322).
	static constexpr int kMaxLadders = 10;   // Render::MAX_LADDERS_PER_MAP (src/Render.h:71)
	int16_t mapEntranceAutomap = -1;          // src/Render.h:180
	int16_t mapExitAutomap = -1;              // src/Render.h:181
	int16_t mapLadders[kMaxLadders] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

	// ---- Maya cameras (raw) ----
	struct MayaCamera {
		int numKeys = 0;
		int sampleRate = 0;
		std::vector<int16_t> keys;   // 7 fields * numKeys
		std::vector<int16_t> tweenIndices; // 6 * numKeys
		std::vector<uint8_t> tweens; // per-axis tween bytes
		int tweenCounts[6] = {}; // per-channel tween byte counts (channel layout of `tweens`)
	};
	std::vector<MayaCamera> mayaCameras;

	// ---- Decoded world geometry ----
	// A vertex in world units. x,y,z use the game coordinate system (1 byte
	// step = 128 world units = 2 tiles); s,t are texture UVs in 2.14 units.
	struct Vertex {
		int x = 0, y = 0, z = 0;
		int s = 0, t = 0;
	};

	struct Polygon {
		std::vector<Vertex> verts;
		int textureId = 0;
		int flags = 0;
		int leafNode = -1; // BSP leaf node this polygon belongs to
	};

	// Polygons extracted from nodePolys for LEAF nodes (all meshes).
	std::vector<Polygon> polygons;
};

} // namespace newcore

#endif // NEW_DOMAIN_WORLD_MAPDATA_H