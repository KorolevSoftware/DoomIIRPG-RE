#include "render/World3D.h"

#include <cstdio>
#include <cstring>
#include <map>

#include "domain/world/MapData.h"

namespace newcore {

namespace {

// Saves an indexed texture (indices + RGB565 palette) as a 24-bit BMP so it
// can be inspected on disk before upload. Transparent color 0xF81F is written
// as magenta so the viewer can see where transparency would apply.
void saveIndexedBmp(const char* path, const std::vector<uint8_t>& indices, int w, int h,
	const std::vector<uint16_t>& palette) {
	if (w <= 0 || h <= 0) return;
	int rowSize = ((w * 3) + 3) & ~3;
	int imageSize = rowSize * h;
	int dataOff = 54;

	FILE* f = fopen(path, "wb");
	if (!f) return;
	uint8_t header[54] = { 0 };
	header[0] = 'B'; header[1] = 'M';
	uint32_t fileSize = dataOff + imageSize;
	std::memcpy(header + 2, &fileSize, 4);
	std::memcpy(header + 10, &dataOff, 4);
	uint32_t hdrSize = 40; std::memcpy(header + 14, &hdrSize, 4);
	int32_t w32 = w; std::memcpy(header + 18, &w32, 4);
	int32_t h32 = h; std::memcpy(header + 22, &h32, 4);
	uint16_t planes = 1; std::memcpy(header + 26, &planes, 2);
	uint16_t bpp = 24; std::memcpy(header + 28, &bpp, 2);
	fwrite(header, 1, 54, f);

	// BMP rows are bottom-up.
	std::vector<uint8_t> row(rowSize, 0);
	for (int y = h - 1; y >= 0; --y) {
		for (int x = 0; x < w; ++x) {
			uint8_t idx = indices[y * w + x];
			uint16_t c = (idx < palette.size()) ? palette[idx] : 0;
			uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
			uint8_t r = (r5 << 3) | (r5 >> 2);
			uint8_t g = (g6 << 2) | (g6 >> 4);
			uint8_t b = (b5 << 3) | (b5 >> 2);
			if (c == 0xF81F) { r = 0xFF; g = 0x00; b = 0xFF; } // magenta = transparent
			row[x * 3 + 0] = b;
			row[x * 3 + 1] = g;
			row[x * 3 + 2] = r;
		}
		fwrite(row.data(), 1, rowSize, f);
	}
	fclose(f);
}

const char* kWorldVertex = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
	vUV = aUV;
	gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kWorldFragment = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTexture; // R8 index texture
uniform sampler2D uPalette; // RGBA8 palette LUT (256x1)
out vec4 fragColor;
void main() {
	float index = texture(uTexture, vUV).r;
	fragColor = texture(uPalette, vec2(index, 0.5));
}
)";

// Column-RLE decode for sprite texels (Size != width*height). Faithful port of
// python tools/extract_textures.py decode_sprite. Bounds are the 4 mediaBounds
// values for the media (already uint8-cast). Returns a flat w*h index buffer.
std::vector<uint8_t> decodeSpriteRLE(const std::vector<uint8_t>& raw, int width, int height,
	const int16_t bounds[4]) {
	std::vector<uint8_t> out(width * height, 0);
	int size = (int)raw.size();
	if (size < 2) return out;

	int shapeMin = (uint8_t)bounds[0], shapeMax = (uint8_t)bounds[1];
	int minY = (uint8_t)bounds[2], maxY = (uint8_t)bounds[3];
	(void)minY; (void)maxY;

	int last2 = (raw[size - 1] << 8) | raw[size - 2];
	int nybbleStart = size - last2 - 2;
	int numCols = shapeMax - shapeMin;
	int nybbleBytes = (numCols + 1) >> 1;
	int runPairStart = nybbleStart + nybbleBytes;

	int pixelIdx = 0;
	int runPtr = runPairStart;
	for (int colOff = 0; colOff < numCols; ++colOff) {
		int col = shapeMin + colOff;
		uint8_t nb = raw[nybbleStart + (colOff >> 1)];
		int runCount = (colOff & 1) ? ((nb >> 4) & 0xF) : (nb & 0xF);
		for (int r = 0; r < runCount; ++r) {
			if (runPtr + 1 >= size) break;
			int yStart = raw[runPtr]; runPtr += 1;
			int runHeight = raw[runPtr]; runPtr += 1;
			for (int row = yStart; row < yStart + runHeight; ++row) {
				if (row < height && col < width && pixelIdx < nybbleStart) {
					out[row * width + col] = raw[pixelIdx];
				}
				pixelIdx += 1;
			}
		}
	}
	return out;
}

// Legacy Canvas::viewStepValues: 8 direction vectors (x,y), 45° apart.
constexpr int kViewStepValues[16] = { 64, 0, 64, -64, 0, -64, -64, -64, -64, 0, -64, 64, 0, 64, 64, 64 };
}

World3D::World3D() = default;
World3D::~World3D() {
	if (vao_) glDeleteVertexArrays(1, &vao_);
	if (vbo_) glDeleteBuffers(1, &vbo_);
}

bool World3D::initialize() {
	std::string err;
	if (!shader_.compile(kWorldVertex, kWorldFragment, &err)) {
		fprintf(stderr, "World3D shader: %s\n", err.c_str());
		return false;
	}
	locMVP_ = shader_.uniform("uMVP");

	glGenVertexArrays(1, &vao_);
	glBindVertexArray(vao_);
	glGenBuffers(1, &vbo_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, kMaxVerts * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)12);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	initialized_ = true;
	return true;
}

void World3D::uploadMapTextures(const MapData& map, const MediaLoader& media) {
	media_ = &media;
	map_ = &map;
	textureByTile_.clear();

	// Collect unique texture ids used by polygons.
	std::vector<uint8_t> used(4096, 0);
	for (const auto& p : map.polygons) {
		if (p.textureId >= 0 && p.textureId < (int)used.size()) used[p.textureId] = 1;
	}

	for (int tile = 0; tile < (int)used.size(); ++tile) {
		if (!used[tile]) continue;
		const auto& m = media.mappings();
		if (tile >= (int)m.mappings.size()) continue;
		int mediaId = m.mappings[tile];
		if (mediaId < 0 || mediaId >= MediaMappings::kMaxMedia) continue;

		int texIdx = media.texelIndexFor(mediaId);
		int palIdx = media.paletteIndexFor(mediaId);
		if (texIdx < 0 || palIdx < 0) continue;
		const MediaTexel& tex = media.texel(texIdx);
		const MediaPalette& pal = media.palette(palIdx);

		Texture t;
		// Legacy CreateTextureForMediaID always maps palette color 0xF81F
		// (magenta 250,0,250) to fully transparent alpha — for world geometry
		// too, not just sprites. That is how wall/floor textures get their
		// transparent openings (windows, doorways).
		if (t.uploadIndexed(tex.data, tex.width, tex.height, pal.colors, true, true)) {
			textureByTile_[tile] = std::move(t);
		}
	}

	// ---- Sprite textures (may be column-RLE) ----
	spriteTexByMedia_.clear();
	for (int i = 0; i < map.numSprites; ++i) {
		int info = map.mapSpriteInfo[i];
		int tileNum = info & 0xFF;
		int frame = (info >> 8) & 0xFF;
		const auto& m = media.mappings();
		if (tileNum >= (int)m.mappings.size()) continue;
		int lo = m.mappings[tileNum];
		int hi = (tileNum + 1 < (int)m.mappings.size()) ? m.mappings[tileNum + 1] : lo + 1;
		if (lo < 0) continue;
		int mediaId = lo + frame;
		if (mediaId >= hi) mediaId = lo; // clamp frame
		if (mediaId < 0 || mediaId >= MediaMappings::kMaxMedia) continue;
		if (spriteTexByMedia_.count(mediaId)) continue;

		int texIdx = media.texelIndexFor(mediaId);
		int palIdx = media.paletteIndexFor(mediaId);
		if (texIdx < 0 || palIdx < 0) continue;
		const MediaTexel& tex = media.texel(texIdx);
		const MediaPalette& pal = media.palette(palIdx);

		std::vector<uint8_t> indices;
		bool isRle = tex.data.size() != (size_t)(tex.width * tex.height);
		if (!isRle) {
			indices = tex.data;
		} else {
			int16_t bounds[4] = {
				(int16_t)(m.bounds[mediaId * 4 + 0]),
				(int16_t)(m.bounds[mediaId * 4 + 1]),
				(int16_t)(m.bounds[mediaId * 4 + 2]),
				(int16_t)(m.bounds[mediaId * 4 + 3]),
			};
			indices = decodeSpriteRLE(tex.data, tex.width, tex.height, bounds);
		}
		Texture t;
		if (t.uploadIndexed(indices, tex.width, tex.height, pal.colors, true, true)) {
			spriteTexByMedia_[mediaId] = std::move(t);
			spriteIsRle_[mediaId] = isRle;
		}
	}
}

void World3D::begin(const Camera3D& camera) {
	// Original GL path (GLES::SetGLState) disables depth test and relies on
	// painter's algorithm (BSP order + sprite depth sort). Keep depth off.
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	shader_.use();
	shader_.setMat4("uMVP", camera.mvp());
	shader_.setInt("uTexture", 0);
	shader_.setInt("uPalette", 1);
	glBindVertexArray(vao_);

	vertices_.clear();
	vertexCount_ = 0;
	currentTex_ = 0;
	currentPal_ = 0;
	begun_ = true;
}

void World3D::end() {
	flush();
	glBindVertexArray(0);
	glDisable(GL_BLEND);
	begun_ = false;
}

void World3D::flush() {
	if (vertices_.empty()) return;

	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices_.size());
	vertices_.clear();
	vertexCount_ = 0;
}

void World3D::drawPoly(const MapData& map, int polyIdx) {
	const auto& p = map.polygons[polyIdx];
	if (p.verts.size() < 3) return;

	auto it = textureByTile_.find(p.textureId);
	GLuint tex = 0, pal = 0;
	if (it != textureByTile_.end()) {
		tex = it->second.id();
		pal = it->second.paletteId();
	} else {
		// Fallback: solid white quad.
		if (!white_.valid()) {
			std::vector<uint8_t> idx(1, 0);
			std::vector<uint16_t> pal1(256, 0xFFFF);
			white_.uploadIndexed(idx, 1, 1, pal1, false, false);
		}
		tex = white_.id();
		pal = white_.paletteId();
	}

	// If the texture changed, flush the accumulated batch first: the batch is
	// drawn as one glDrawArrays, so all vertices in it must share one texture.
	if (currentTex_ != tex || currentPal_ != pal) {
		flush();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, pal);
		currentTex_ = tex;
		currentPal_ = pal;
	}

	// Fan triangulation around vertex 0 (matches legacy quad_indexes).
	for (size_t i = 1; i + 1 < p.verts.size(); ++i) {
		const auto& v0 = p.verts[0];
		const auto& v1 = p.verts[i];
		const auto& v2 = p.verts[i + 1];
		Vertex tri[3] = {
			{ (float)v0.x * (1.f/16384.f), (float)v0.y * (1.f/16384.f), (float)v0.z * (1.f/16384.f),
			  (float)v0.s * (1.f/1024.f),  (float)v0.t * (1.f/1024.f) },
			{ (float)v1.x * (1.f/16384.f), (float)v1.y * (1.f/16384.f), (float)v1.z * (1.f/16384.f),
			  (float)v1.s * (1.f/1024.f),  (float)v1.t * (1.f/1024.f) },
			{ (float)v2.x * (1.f/16384.f), (float)v2.y * (1.f/16384.f), (float)v2.z * (1.f/16384.f),
			  (float)v2.s * (1.f/1024.f),  (float)v2.t * (1.f/1024.f) },
		};
		if (vertices_.size() + 3 > kMaxVerts) flush();
		vertices_.insert(vertices_.end(), tri, tri + 3);
		vertexCount_ += 3;
	}
}

void World3D::drawWorld(const MapData& map, const Camera3D& camera) {
	if (!initialized_ || map.polygons.empty()) return;
	begin(camera);
	for (size_t i = 0; i < map.polygons.size(); ++i) {
		drawPoly(map, (int)i);
	}
	end();
}

void World3D::uploadSky(const std::vector<uint8_t>& texel, const std::vector<uint16_t>& palette) {
	if (!initialized_ || texel.size() < 256 * 256 || palette.size() < 256) return;
	if (sky_.uploadIndexed(texel, 256, 256, palette, true, true)) {
		fprintf(stderr, "World3D: sky texture uploaded (256x256)\n");
		fflush(stderr);
	}
}

void World3D::drawSky(const Camera3D& camera) {
	if (!initialized_ || !sky_.valid()) return;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	shader_.use();
	// Identity MVP: sky quad is drawn directly in clip space.
	float identity[16] = {
		1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1,
	};
	shader_.setMat4("uMVP", identity);
	shader_.setInt("uTexture", 0);
	shader_.setInt("uPalette", 1);
	glBindVertexArray(vao_);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sky_.id());
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, sky_.paletteId());

	// Legacy DrawSkyMap quad in NDC (xyzw after divide): corners ±1.
	// st = (ndc.x*0.5, ndc.y*-0.5 + 0.5) then st[0] -= viewYaw/256.
	const float yawShift = (float)camera.viewYaw() / 256.f;
	struct SkyV { float x, y, z, u, v; };
	SkyV q[4] = {
		{ -1.f, -1.f, 0.f, -0.5f - yawShift, 1.f },
		{  1.f, -1.f, 0.f,  0.5f - yawShift, 1.f },
		{  1.f,  1.f, 0.f,  0.5f - yawShift, 0.f },
		{ -1.f,  1.f, 0.f, -0.5f - yawShift, 0.f },
	};
	Vertex tri[6] = {
		{ q[0].x, q[0].y, q[0].z, q[0].u, q[0].v },
		{ q[1].x, q[1].y, q[1].z, q[1].u, q[1].v },
		{ q[2].x, q[2].y, q[2].z, q[2].u, q[2].v },
		{ q[0].x, q[0].y, q[0].z, q[0].u, q[0].v },
		{ q[2].x, q[2].y, q[2].z, q[2].u, q[2].v },
		{ q[3].x, q[3].y, q[3].z, q[3].u, q[3].v },
	};
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 6);

	glBindVertexArray(0);
	glDisable(GL_BLEND);
}

void World3D::drawSprites(const MapData& map, const MediaLoader& media, const Camera3D& camera) {
	if (!initialized_ || map.numSprites == 0) return;

	begin(camera);
	// Sprites rely on painter's algorithm (sorted far-to-near), no depth test.
	const int n = map.numSprites;
	const int* st = camera.viewInt(); // 14.14 view matrix (row-major)

	// Collect billboard sprites and depth-sort exactly like legacy addSprite:
	// n3 = (x*mvp[2] + y*mvp[6] + z*mvp[10] >> 14) + mvp[14] using the raw
	// sprite coords (not shifted), then draw in descending n3 order.
	const int* mvp = camera.mvpInt();
	std::vector<int> order;
	std::vector<int> depth;
	for (int i = 0; i < map.numSprites; ++i) {
		int info = map.mapSpriteInfo[i];
		if (info & 0x10000) continue;                 // invisible
		int tileNum = info & 0xFF;
		if (tileNum == 240 /* WATER_STREAM */) continue;
		// Only billboards here; wall decals are drawn interleaved in drawBSP.
		bool isWall = (info & 0x2F000000) != 0 && (info & 0x400000) == 0;
		if (isWall) continue;
		order.push_back(i);
		int x = map.mapSprites[i + 0 * n];
		int y = map.mapSprites[i + 1 * n];
		int z = map.mapSprites[i + 2 * n];
		int d = (x * mvp[2] + y * mvp[6] + z * mvp[10] >> 14) + mvp[14];
		if (info & 0x400000) d += 6;
		else if (tileNum == 240 || tileNum == 246 || tileNum == 245 || tileNum == 247) d = (int)0x80000000;
		else if (info & 0xF000000) d += 5;
		depth.push_back(d);
	}
	// Sort descending by depth (legacy list order: larger n3 drawn first).
	for (size_t a = 0; a < order.size(); ++a) {
		for (size_t b = a + 1; b < order.size(); ++b) {
			if (depth[b] > depth[a]) {
				std::swap(order[a], order[b]);
				std::swap(depth[a], depth[b]);
			}
		}
	}

	for (int i : order) {
		drawSprite(map, media, camera, i);
	}
	end();
}

// Renders one map sprite (billboard or wall decal). Must be called between
// begin()/end(). No depth test: correct ordering is ensured by the caller
// (drawSprites sorts by mvp depth; drawBSP interleaves per leaf).
void World3D::drawSprite(const MapData& map, const MediaLoader& media, const Camera3D& camera, int i) {
	const int n = map.numSprites;
	const int* st = camera.viewInt(); // 14.14 view matrix (row-major)
	int info = map.mapSpriteInfo[i];

	int tileNum = info & 0xFF;
	int frame = (info >> 8) & 0xFF;
	if (tileNum == 240 /* WATER_STREAM */) return; // needs special handling

	int x = map.mapSprites[i + 0 * n];
	int y = map.mapSprites[i + 1 * n];
	int z = map.mapSprites[i + 2 * n] << 4;
	int renderMode = map.mapSprites[i + 3 * n];
	int scaleFactor = map.mapSprites[i + 8 * n] << 10;
	(void)renderMode;

	// Legacy postProcessSprites adds terrain height to the sprite Z so it
	// stands on the floor (Z stored in byte*8 units, like X/Y coords).
	// getHeight: heightMap[(y>>6)*32 + (x>>6)] << 3.
	{
		int hx = x & 0x7FF, hy = y & 0x7FF;
		if (!map.heightMap.empty()) {
			int h = (map.heightMap[((hy >> 6) * 32 + (hx >> 6))] << 3) << 4;
			if (i >= map.numNormalSprites) h -= 32 << 4; // z-sprites nudge down
			z += h;
		}
	}

	// Resolve texture (mediaId = mediaMappings[tileNum] + frame).
	const auto& m = media.mappings();
	if (tileNum >= (int)m.mappings.size()) return;
	int lo = m.mappings[tileNum];
	int hi = (tileNum + 1 < (int)m.mappings.size()) ? m.mappings[tileNum + 1] : lo + 1;
	if (lo < 0) return;
	int mediaId = lo + frame;
	if (mediaId >= hi) mediaId = lo;
	auto it = spriteTexByMedia_.find(mediaId);
	if (it == spriteTexByMedia_.end()) return;

	// Image bounds for UVs and billboard size. Legacy uses two branches:
	// RLE sprites (Size != w*h) draw with FULL-texture UVs (0..1024) and a
	// fixed 518/1036 size; raw textures / z-sprites use bounds-based UVs
	// and bounds-based size. Using bounds UVs on RLE sprites samples the
	// mirror region -> flipped/invisible sprites.
	bool isRle = spriteIsRle_.count(mediaId) ? spriteIsRle_[mediaId] : false;
	int n13, n14, n15, n16, n19, n20;
	if (isRle) {
		n13 = 0; n14 = 1024; n15 = 0; n16 = 1024;
		n19 = (518 * scaleFactor) / 0x10000;
		n20 = (1036 * scaleFactor) / 0x10000;
	} else {
		int16_t b[4] = {
			(int16_t)(m.bounds[mediaId * 4 + 0]),
			(int16_t)(m.bounds[mediaId * 4 + 1]),
			(int16_t)(m.bounds[mediaId * 4 + 2]),
			(int16_t)(m.bounds[mediaId * 4 + 3]),
		};
		int wb = (media.mappings().dimensions[mediaId] >> 4) & 0xF;
		int hb = media.mappings().dimensions[mediaId] & 0xF;
		int sWidth = 1 << wb;
		int tHeight = 1 << hb;
		int n11 = b[1] - b[0];
		int n12 = b[3] - b[2];
		n13 = (b[0] << 10) / sWidth;
		n14 = (n11 << 10) / sWidth;
		n15 = ((tHeight - b[3]) << 10) / tHeight;
		n16 = (n12 << 10) / tHeight;
		n19 = ((((n11 >> 2) << 4) + 7) * scaleFactor) / 0x10000;
		n20 = ((((n12 >> 1) << 4) + 7) * scaleFactor) / 0x10000;
	}

	// Flush on texture change.
	GLuint tex = it->second.id();
	GLuint pal = it->second.paletteId();
	if (currentTex_ != tex || currentPal_ != pal) {
		flush();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, pal);
		currentTex_ = tex;
		currentPal_ = pal;
	}

	const float k1 = 1.f / 16384.f;
	// Billboards: sprites without wall/plane flags, OR z-sprites (0x400000,
	// monsters/pickups — legacy routes these through renderSpriteAnim and
	// the (flags & 0x400000) quad-billboard branch). Wall decals are those
	// with wall/plane direction bits but WITHOUT the z-sprite bit.
	const bool isWall = (info & 0x2F000000) != 0 && (info & 0x400000) == 0;

	if (!isWall) {
		// ---- Billboard (flags & 0x2F000000) == 0 ----
		// Legacy: z -= 512; position nudged back by n17 (10 raw, 12 RLE).
		z -= 512;
		int n17 = isRle ? 12 : 10;
		int viewSin = st[(camera.viewYaw() + 0) & 0x3FF];
		int viewCos = st[(camera.viewYaw() + 256) & 0x3FF];
		x -= n17 * viewCos >> 16;
		y += n17 * viewSin >> 16;

		// Four billboard corners (viewMtxMove: right += view[i]*n2>>14,
		// up: n3=-n3). view_ row0=(view[0],view[4],view[8]) right axis,
		// row1=(view[1],view[5],view[9]) up axis (14.14).
		Vertex quad[4];
		for (int ci = 0; ci < 4; ++ci) {
			int n21 = (ci & 2) >> 1;
			int n22 = (ci & 1) ^ n21 ^ 1;
			float px = (float)(x << 4);
			float py = (float)(y << 4);
			float pz = (float)(z - 84);

			int n2 = (n22 * 2 - 1) * n19;
			int m3 = -(n21 * n20);
			px += (st[0] * n2 + st[1] * m3) * k1;
			py += (st[4] * n2 + st[5] * m3) * k1;
			pz += (st[8] * n2 + st[9] * m3) * k1;

			float s = (float)(n13 + n22 * n14) * (1.f / 1024.f);
			// Vertical flip for billboards: texel data is top-down (row 0 =
			// top of image) but GL v=0 is the bottom texel row.
			float t = (1.f - (float)(n15 + n21 * n16) * (1.f / 1024.f));
			quad[ci] = { px * k1, py * k1, pz * k1, s, t };
		}
		// Fan triangles 0,1,2 and 0,2,3 (matches quad_indexes).
		Vertex tri[6] = { quad[0], quad[1], quad[2], quad[0], quad[2], quad[3] };
		if (vertices_.size() + 6 > kMaxVerts) flush();
		vertices_.insert(vertices_.end(), tri, tri + 6);
	} else {
		// ---- Wall decal / plane (flags & 0x2F000000) != 0 ----
		// Legacy renderSprite "Wall" branch: quad laid in the wall plane.
		// n23 = wall direction from mapSpriteInfo bits.
		int n23;
		if (info & 0x4000000) n23 = 0;
		else if (info & 0x1000000) n23 = 2;
		else if (info & 0x8000000) n23 = 4;
		else if (info & 0x2000000) n23 = 6;
		else n23 = 0;

		int wb = (media.mappings().dimensions[mediaId] >> 4) & 0xF;
		int hb = media.mappings().dimensions[mediaId] & 0xF;
		int sWidth = 1 << wb;
		int tHeight = 1 << hb;
		int16_t b[4] = {
			(int16_t)(m.bounds[mediaId * 4 + 0]),
			(int16_t)(m.bounds[mediaId * 4 + 1]),
			(int16_t)(m.bounds[mediaId * 4 + 2]),
			(int16_t)(m.bounds[mediaId * 4 + 3]),
		};
		int n11 = b[1] - b[0];
		int n12 = b[3] - b[2];
		int n24 = (tHeight == 256 && sWidth == 256) ? 64 : (n12 >> 1);
		int n25 = (tHeight == 256 && sWidth == 256) ? 32 : (n11 >> 2);
		int n26 = n24 * scaleFactor / 65536;
		int n27 = n25 * scaleFactor / 65536;

		// Z correction: walls rise from floor, planes lower.
		if ((info & 0x20000000) == 0) {
			z += (tHeight - b[3]) << 4;
			z -= 16 * (scaleFactor / 2048);
		} else {
			z -= 512;
		}

		int n29 = ((n23 + 2) & 0x7) << 1;
		int n30 = n27 << 4;
		int n31 = n26 << 4;

		// Legacy terminal/portal Z corrections (renderSprite ~517-523).
		if (tileNum >= 179 && tileNum <= 183) z -= 256; // terminals
		if (tileNum >= 155 && tileNum <= 157) z -= 128; // portal eye

		// UVs (bounds-based, s/t flips for 0x20000/0x40000).
		int s13 = (b[0] << 10) / sWidth;
		int s14 = (n11 << 10) / sWidth;
		int t15 = ((tHeight - b[3]) << 10) / tHeight;
		int t16 = (n12 << 10) / tHeight;
		if (info & 0x20000) { s13 += s14; s14 = -s14; }
		if (info & 0x40000) { t15 += t16; t16 = -t16; }

		// Wall quad: along viewStepValues[n29] (wall surface direction).
		Vertex quad[4];
		for (int l = 0; l < 4; ++l) {
			int n42 = (l & 2) >> 1;
			int n43 = (l & 1) ^ n42 ^ 1;
			int n44 = (n43 * 2 - 1) * n30;
			float px = (float)(x << 4) + (kViewStepValues[n29 + 0] >> 6) * (float)n44;
			float py = (float)(y << 4) + (kViewStepValues[n29 + 1] >> 6) * (float)n44;
			float pz = (float)z + (float)(n42 * n31);
			float s = (float)(s13 + n43 * s14) * (1.f / 1024.f);
			float t = (float)(t15 + n42 * t16) * (1.f / 1024.f);
			quad[l] = { px * k1, py * k1, pz * k1, s, t };
		}
		Vertex tri[6] = { quad[0], quad[1], quad[2], quad[0], quad[2], quad[3] };
		if (vertices_.size() + 6 > kMaxVerts) flush();
		vertices_.insert(vertices_.end(), tri, tri + 6);
	}
}

void World3D::drawPolys(const MapData& map, const std::vector<int>& polyIdx, const Camera3D& camera) {
	if (!initialized_) return;
	begin(camera);
	for (int idx : polyIdx) drawPoly(map, idx);
	end();
}

int World3D::nodeClassifyPoint(const MapData& map, int n, int x, int y, int z) {
	int ni = (map.nodeNormalIdxs[n] & 0xFF) * 3;
	return (((x * map.normals[ni]) + (y * map.normals[ni + 1]) + (z * map.normals[ni + 2])) >> 14)
		+ (map.nodeOffsets[n] & 0xFFFF);
}

// Legacy Render::getNodeForPoint: descend the BSP to find the leaf containing
// a world point. Returns -1 if the point falls outside the leaf's bounds.
int World3D::getNodeForPoint(const MapData& map, int x, int y, int z, int info) {
	int n = 0;
	int i = map.nodeOffsets[n] & 0xFFFF;
	bool onSplit = (info & 0xF000000) != 0;
	int n6 = info & 0xFF;
	if (info & 0x400000) n6 += 256 + 1;
	bool isWater = n6 == 240;

	while (i != 0xFFFF) {
		int c = nodeClassifyPoint(map, n, x, y, z);
		if (c == 0 && onSplit) {
			n = (info & 0x9000000) != 0 ? map.nodeChildOffset1[n] : map.nodeChildOffset2[n];
		} else {
			if (!isWater && c > -128 && c < 128) return n;
			n = c > 0 ? map.nodeChildOffset1[n] : map.nodeChildOffset2[n];
		}
		i = map.nodeOffsets[n] & 0xFFFF;
	}
	// Check point is within leaf bounds.
	int x1 = (map.nodeBounds[(n << 2) + 0] & 0xFF) << 7;
	int y1 = (map.nodeBounds[(n << 2) + 1] & 0xFF) << 7;
	int x2 = (map.nodeBounds[(n << 2) + 2] & 0xFF) << 7;
	int y2 = (map.nodeBounds[(n << 2) + 3] & 0xFF) << 7;
	if (x < x1 || y < y1 || x > x2 || y > y2) return -1;
	return n;
}

bool World3D::walkNode(const MapData& map, int n, int viewX, int viewY, int viewZ) {
	// Note: the legacy cullBoundingBox early-out is skipped here. Its cheap
	// 2D test rejects boxes the camera is outside of, but the full version
	// then re-checks via screen projection (transform2DVerts). On the GL path
	// the painter's-order BSP traversal already produces correct results, so
	// we simply walk every node (cheap for this map size).

	if ((map.nodeOffsets[n] & 0xFFFF) == 0xFFFF) {
		// Leaf: queue it for drawing.
		nodeIdxs_.push_back(n);
		return true;
	}

	if (nodeClassifyPoint(map, n, viewX, viewY, viewZ) >= 0) {
		walkNode(map, map.nodeChildOffset1[n], viewX, viewY, viewZ);
		walkNode(map, map.nodeChildOffset2[n], viewX, viewY, viewZ);
	} else {
		walkNode(map, map.nodeChildOffset2[n], viewX, viewY, viewZ);
		walkNode(map, map.nodeChildOffset1[n], viewX, viewY, viewZ);
	}
	return true;
}

void World3D::drawBSP(const MapData& map, const MediaLoader& media, const Camera3D& camera) {
	if (!initialized_ || map.numNodes == 0) return;

	nodeIdxs_.clear();
	walkNode(map, 0, camera.viewX(), camera.viewY(), camera.viewZ());

	// Precompute the BSP leaf for every sprite (legacy relinkSprite does this
	// once at level load). Sprites get drawn right after their leaf's geometry,
	// so nearer leaves overdraw farther sprites (painter's algorithm).
	std::vector<int> spriteLeaf(map.numSprites, -1);
	for (int i = 0; i < map.numSprites; ++i) {
		if (map.mapSpriteInfo[i] & 0x10000) continue; // invisible
		int x = map.mapSprites[i + 0 * map.numSprites];
		int y = map.mapSprites[i + 1 * map.numSprites];
		int z = map.mapSprites[i + 2 * map.numSprites];
		spriteLeaf[i] = getNodeForPoint(map, x << 4, y << 4, z << 4, map.mapSpriteInfo[i]);
	}

	begin(camera);
	// Legacy renderBSP draws nodeIdxs in reverse (far leaves first, so that
	// nearer leaves overdraw them — painter's algorithm). All sprites of a
	// leaf are drawn right after its geometry so walls of nearer leaves
	// correctly occlude them (culling).
	for (int k = (int)nodeIdxs_.size() - 1; k >= 0; --k) {
		int leaf = nodeIdxs_[k];
		for (size_t i = 0; i < map.polygons.size(); ++i) {
			if (map.polygons[i].leafNode == leaf) drawPoly(map, (int)i);
		}
		// Sprites of this leaf, drawn after its geometry.
		for (int i = 0; i < map.numSprites; ++i) {
			if (spriteLeaf[i] == leaf) drawSprite(map, media, camera, i);
		}
	}
	end();
}

} // namespace newcore