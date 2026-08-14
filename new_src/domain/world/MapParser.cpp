#include "domain/world/MapParser.h"

#include "io/DataReader.h"

namespace newcore {

namespace {
constexpr uint32_t kMarkerBeef = 0xDEADBEEF;

inline void skipMarker(DataReader& r) { r.skip(4); }
} // namespace

bool MapParser::parse(const std::vector<uint8_t>& data, MapData& out) {
	if (data.size() < 42) return false;

	DataReader r(data);

	// ---- Header (read as a 42-byte prefix) ----
	// The legacy code reads 42 bytes into an IO buffer, then shifts fields out.
	// DataReader already reads sequentially, so we just consume the same fields.
	out.version = r.readByte();
	out.compileDate = r.readInt();
	out.spawnIndex = r.readUShort();
	out.spawnDir = r.readUByte();
	out.flagsBitmask = r.readSByte();
	out.totalSecrets = r.readByte();
	out.totalLoot = r.readUByte();
	out.numNodes = r.readUShort();
	out.dataSizePolys = r.readUShort();
	out.numLines = r.readUShort();
	out.numNormals = r.readUShort();
	out.numNormalSprites = r.readUShort();
	out.numZSprites = r.readShort();
	int numMapSprites = out.numNormalSprites + out.numZSprites;
	out.numTileEvents = r.readShort();
	out.mapByteCodeSize = r.readShort();
	out.totalMayaCameras = r.readByte();
	out.totalMayaCameraKeys = r.readShort();

	int totalMayaTweens = 0;
	for (int l = 0; l < 6; ++l) {
		int t = r.readShort();
		if (t != -1) totalMayaTweens += t;
	}
	out.totalMayaTweens = totalMayaTweens;

	// ---- Media list ----
	if (r.readUInt() != kMarkerBeef) return false;
	out.mediaCount = r.readUShort();
	out.mediaIds.reserve(out.mediaCount);
	for (int i = 0; i < out.mediaCount; ++i) out.mediaIds.push_back(r.readUShort());
	if (r.readUInt() != kMarkerBeef) return false;

	// ---- Geometry ----
	out.normals.resize(out.numNormals * 3);
	for (auto& v : out.normals) v = r.readShort();
	skipMarker(r);

	out.nodeOffsets.resize(out.numNodes);
	for (auto& v : out.nodeOffsets) v = r.readShort();
	skipMarker(r);

	out.nodeNormalIdxs.resize(out.numNodes);
	for (auto& v : out.nodeNormalIdxs) v = r.readByte();
	skipMarker(r);

	out.nodeChildOffset1.resize(out.numNodes);
	for (auto& v : out.nodeChildOffset1) v = r.readShort();
	out.nodeChildOffset2.resize(out.numNodes);
	for (auto& v : out.nodeChildOffset2) v = r.readShort();
	skipMarker(r);

	out.nodeBounds.resize(out.numNodes * 4);
	for (auto& v : out.nodeBounds) v = r.readByte();
	skipMarker(r);

	out.nodePolys.resize(out.dataSizePolys);
	for (auto& v : out.nodePolys) v = r.readByte();
	skipMarker(r);

	out.lineFlags.resize((out.numLines + 1) / 2);
	for (auto& v : out.lineFlags) v = r.readByte();
	out.lineXs.resize(out.numLines * 2);
	for (auto& v : out.lineXs) v = r.readByte();
	out.lineYs.resize(out.numLines * 2);
	for (auto& v : out.lineYs) v = r.readByte();
	skipMarker(r);

	out.heightMap.resize(1024);
	for (auto& v : out.heightMap) v = r.readByte();
	skipMarker(r);

	// ---- Sprites ----
	out.numSprites = numMapSprites;
	out.mapSprites.assign(out.numSprites * 10, 0);

	// S_X and S_Y are stored as coords (byte * 8).
	for (int i = 0; i < numMapSprites; ++i) out.mapSprites[i + 0 * out.numSprites] = r.readCoord();
	for (int i = 0; i < numMapSprites; ++i) out.mapSprites[i + 1 * out.numSprites] = r.readCoord();

	// Default sprite fields.
	for (int i = 0; i < numMapSprites; ++i) {
		out.mapSprites[i + 4 * out.numSprites] = -1; // NODE
		out.mapSprites[i + 5 * out.numSprites] = -1; // NODENEXT
		out.mapSprites[i + 6 * out.numSprites] = -1; // VIEWNEXT
		out.mapSprites[i + 7 * out.numSprites] = -1; // ENT
		out.mapSprites[i + 8 * out.numSprites] = 64; // SCALEFACTOR
		out.mapSprites[i + 2 * out.numSprites] = 32; // Z
	}

	// mapSpriteInfo low byte (sprite info part 1).
	out.mapSpriteInfo.assign(out.numSprites * 2, 0);
	for (int i = 0; i < numMapSprites; ++i) out.mapSpriteInfo[i] = r.readByte();
	skipMarker(r);

	// mapSpriteInfo high word.
	for (int i = 0; i < numMapSprites; ++i) {
		out.mapSpriteInfo[i] |= (r.readUShort() & 0xFFFF) << 16;
	}
	skipMarker(r);

	// Z coords for z-sprites (normal sprites keep default Z=32).
	for (int i = 0; i < out.numZSprites; ++i) {
		out.mapSprites[(out.numNormalSprites + i) + 2 * out.numSprites] = r.readByte();
	}
	skipMarker(r);

	// Z-sprites' sprite info high byte.
	for (int i = 0; i < out.numZSprites; ++i) {
		int idx = out.numNormalSprites + i;
		out.mapSpriteInfo[idx] |= r.readUByte() << 8;
	}
	skipMarker(r);

	// ---- Misc ----
	out.staticFuncs.resize(12);
	for (auto& v : out.staticFuncs) v = r.readUShort();
	skipMarker(r);

	out.mapFlags.assign(1024, 0);

	out.tileEvents.resize(out.numTileEvents * 2);
	for (auto& v : out.tileEvents) v = r.readInt();
	for (int i = 0; i < out.numTileEvents; ++i) {
		int index = out.tileEvents[i << 1] & 0x3FF;
		if (index >= 0 && index < 1024) out.mapFlags[index] |= 0x40;
	}
	skipMarker(r);

	out.mapByteCode.resize(out.mapByteCodeSize);
	for (auto& v : out.mapByteCode) v = r.readByte();
	skipMarker(r);

	// ---- Maya cameras ----
	for (int i = 0; i < out.totalMayaCameras; ++i) {
		MapData::MayaCamera cam;
		cam.numKeys = r.readByte();
		cam.sampleRate = r.readShort();

		cam.keys.resize(7 * cam.numKeys);
		for (auto& v : cam.keys) v = r.readShort();

		cam.tweenIndices.resize(6 * cam.numKeys);
		for (auto& v : cam.tweenIndices) v = r.readShort();

		int perAxis[6];
		int totalTweenBytes = 0;
		for (int a = 0; a < 6; ++a) {
			perAxis[a] = r.readShort();
			if (perAxis[a] < 0) perAxis[a] = 0;
			totalTweenBytes += perAxis[a];
		}

		skipMarker(r);
		cam.tweens.resize(totalTweenBytes);
		for (auto& v : cam.tweens) v = r.readByte();

		out.mayaCameras.push_back(std::move(cam));
	}
	skipMarker(r);

	// Tile flags: 1024 tiles packed two per byte.
	std::vector<uint8_t> flagBytes(512);
	for (int i = 0; i < 512; ++i) flagBytes[i] = r.readByte();
	for (int i = 0; i < 512; ++i) {
		out.mapFlags[i * 2 + 0] |= (uint8_t)(flagBytes[i] & 0xF);
		out.mapFlags[i * 2 + 1] |= (uint8_t)((flagBytes[i] >> 4) & 0xF);
	}

	decodePolys(out);

	return true;
}

namespace {
constexpr uint8_t kPolyFlagVertsMask = 7;
constexpr uint8_t kPolyFlagAxisMask = 24;
constexpr uint8_t kPolyFlagAxisX = 0;
constexpr uint8_t kPolyFlagAxisY = 8;
constexpr uint8_t kPolyFlagAxisZ = 16;
constexpr uint8_t kPolyFlagSwapXY = 64;
constexpr uint8_t kPolyFlagUvDeltaX = 128;

// Expands a 2-vertex "edge" poly into a quad, mirroring the legacy
// drawNodeGeometry (Render.cpp ~line 997) and tools/map_to_obj.py.
void expandEdgePoly(MapData::Polygon& poly) {
	MapData::Vertex v0 = poly.verts[0];
	MapData::Vertex v1 = poly.verts[1];
	MapData::Vertex v2 = v0;
	MapData::Vertex v3 = v0;
	uint8_t axis = poly.flags & kPolyFlagAxisMask;

	switch (axis) {
		case kPolyFlagAxisX:
			v1.x = v0.x; v3.x = v0.x;
			break;
		case kPolyFlagAxisY:
			v1.y = v0.y; v3.y = v0.y;
			break;
		case kPolyFlagAxisZ:
			v1.z = v0.z; v3.z = v0.z;
			break;
		default:
			break;
	}

	if (poly.flags & kPolyFlagUvDeltaX) {
		v1.s = v2.s; v3.s = v0.s;
	} else {
		v1.t = v2.t; v3.t = v0.t;
	}

	poly.verts = { v0, v1, v2, v3 };
}
} // namespace

void MapParser::decodePolys(MapData& out) {
	const auto& raw = out.nodePolys;
	const int numNodes = out.numNodes;

	out.polygons.clear();

	// Only leaf nodes (nodeOffsets[n] == 0xFFFF) carry geometry.
	std::vector<int> visitedOffsets;

	for (int n = 0; n < numNodes; ++n) {
		if ((out.nodeOffsets[n] & 0xFFFF) != 0xFFFF) continue;

		int offset = out.nodeChildOffset1[n] & 0xFFFF;
		if (offset < 0 || offset >= (int)raw.size()) continue;
		bool seen = false;
		for (int v : visitedOffsets) if (v == offset) { seen = true; break; }
		if (seen) continue;
		visitedOffsets.push_back(offset);

		int meshCount = raw[offset++];
		if (meshCount > 64) continue;

		for (int m = 0; m < meshCount; ++m) {
			if (offset + 6 > (int)raw.size()) break;
			uint16_t packed = raw[offset + 4] | (raw[offset + 5] << 8);
			offset += 6;

			int textureId = packed >> 7;
			int polyCount = packed & 0x7F;
			if (polyCount == 0 || polyCount > 127) continue;

			for (int p = 0; p < polyCount; ++p) {
				if (offset >= (int)raw.size()) break;
				int polyFlags = raw[offset++];
				int numVerts = (polyFlags & kPolyFlagVertsMask) + 2;
				if (numVerts > 9) break;

				MapData::Polygon poly;
				poly.flags = polyFlags;
				poly.textureId = textureId;
				poly.leafNode = n;
				poly.verts.reserve(4);

				for (int v = 0; v < numVerts; ++v) {
					if (offset + 5 > (int)raw.size()) break;
					MapData::Vertex vert;
					vert.x = (raw[offset + 0] & 0xFF) << 7;
					vert.y = (raw[offset + 1] & 0xFF) << 7;
					vert.z = (raw[offset + 2] & 0xFF) << 7;
					vert.s = ((int8_t)raw[offset + 3]) << 6;
					vert.t = ((int8_t)raw[offset + 4]) << 6;
					offset += 5;
					poly.verts.push_back(vert);
				}

				if ((int)poly.verts.size() != numVerts) break;
				if (numVerts == 2) expandEdgePoly(poly);
				out.polygons.push_back(std::move(poly));
			}
		}
	}
}

} // namespace newcore