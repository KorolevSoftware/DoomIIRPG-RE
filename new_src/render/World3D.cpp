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
uniform mat4 uView; // view matrix for eye-space depth (fog)
out vec2 vUV;
out float vFogDepth; // eye-space depth (positive forward)
void main() {
	vUV = aUV;
	vec4 eye = uView * vec4(aPos, 1.0);
	vFogDepth = -eye.z;
	gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kWorldFragment = R"(
#version 330 core
in vec2 vUV;
in float vFogDepth;
uniform sampler2D uTexture; // R8 index texture
uniform sampler2D uPalette; // RGBA8 palette LUT (256x1)
uniform int uFogEnabled;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec4 uFogColor;
out vec4 fragColor;
void main() {
	float index = texture(uTexture, vUV).r;
	vec4 col = texture(uPalette, vec2(index, 0.5));
	if (uFogEnabled != 0) {
		// Fog affects RGB only (GL fog leaves alpha untouched) so transparent
		// billboard texels stay transparent instead of fogging into a box.
		float f = clamp((uFogEnd - vFogDepth) / max(uFogEnd - uFogStart, 1e-6), 0.0, 1.0);
		col.rgb = mix(uFogColor.rgb, col.rgb, f);
	}
	fragColor = col;
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

// Character constants for the stacked-billboard path (src/Enums.h:567-580,
// :708-719, :830). Kept as local values — render/ must not include
// domain/game headers (spec 2026-08-25-character-animation §10).
constexpr int kManimMask = 0xF0;
constexpr int kMframeMask = 0x0F;
constexpr int kManimIdle = 0;
constexpr int kManimIdleBack = 16;
constexpr int kManimWalkFront = 32;
constexpr int kManimWalkBack = 48;
constexpr int kManimAttack1 = 64;
constexpr int kManimAttack2 = 80;
constexpr int kManimPain = 96;
constexpr int kManimDead = 112;
constexpr int kManimSlap = 128;
constexpr int kManimNpcTalk = 160;
constexpr int kManimNpcBackAction = 176;
constexpr int kTileNumFirstNpc = 65;
constexpr int kTileNumLastNpc = 80;
	constexpr int kTileNumNpcRileyOconnor = 66;
	constexpr int kTileNumNpcScientist = 75;
	constexpr int kTileNumNpcCivilian2 = 77;
	constexpr int kTileNumNpcSarge = 72;
constexpr int kTileNumShadow = 232;
// Monster art tiles for the ATTACK deltas (spec 2026-08-26 §2) — same
// duplication pattern as the NPC values above; render/ stays decoupled from
// domain enums (ADR 0007 point 5).
constexpr int kTileNumMonsterImpFirst = 23;    // src/Enums.h:671-673
constexpr int kTileNumMonsterImpLast = 25;
constexpr int kTileNumMonsterZombieFirst = 20; // src/Enums.h:668-670
constexpr int kTileNumMonsterZombieLast = 22;
constexpr int kTileNumMonsterRedSentryBot = 18;// src/Enums.h:666-667
constexpr int kTileNumMonsterSentryBot = 19;
// hasGunFlare set (src/Render.cpp:3019-3021): mancubus 38-40, revenant
// 35-37, sentry 18/19, cyberdemon 54, mastermind 57.
constexpr int kTileNumMonsterRevenantFirst = 35; // src/Enums.h:683-685
constexpr int kTileNumMonsterRevenantLast = 37;
constexpr int kTileNumMonsterMancubusFirst = 38; // src/Enums.h:686-688
constexpr int kTileNumMonsterMancubusLast = 40;
constexpr int kTileNumBossCyberdemon = 54;       // src/Enums.h:699
constexpr int kTileNumBossMastermind = 57;       // src/Enums.h:701

bool isImpFamily(int n) {
	return n >= kTileNumMonsterImpFirst && n <= kTileNumMonsterImpLast;
}

bool isZombieFamily(int n) {
	return n >= kTileNumMonsterZombieFirst && n <= kTileNumMonsterZombieLast;
}

bool hasGunFlareFamily(int n) {
	return (n >= kTileNumMonsterMancubusFirst && n <= kTileNumMonsterMancubusLast) ||
	       (n >= kTileNumMonsterRevenantFirst && n <= kTileNumMonsterRevenantLast) ||
	       n == kTileNumMonsterSentryBot || n == kTileNumMonsterRedSentryBot ||
	       n == kTileNumBossCyberdemon || n == kTileNumBossMastermind;
}
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
	locView_ = shader_.uniform("uView");
	locFogEnabled_ = shader_.uniform("uFogEnabled");
	locFogStart_ = shader_.uniform("uFogStart");
	locFogEnd_ = shader_.uniform("uFogEnd");
	locFogColor_ = shader_.uniform("uFogColor");

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

// Legacy gles fog setup (GLES.cpp:178-190). fogColor packed ARGB; alpha==0
// disables fog (buildFogTables:1857-1861). fogScale = 1/8000.
void World3D::setFog(int fogColorARGB, int fogMin, int fogRange) {
	int a = (fogColorARGB >> 24) & 0xFF;
	int r = (fogColorARGB >> 16) & 0xFF;
	int g = (fogColorARGB >> 8) & 0xFF;
	int b = fogColorARGB & 0xFF;
	const float fogScale = 1.f / 8000.f;

	if (a == 0) {
		fogEnabled_ = false;
		return;
	}
	fogEnabled_ = true;
	float alpha = (float)a / 255.f;
	fogColor_[0] = (float)b / 255.f; // legacy swaps R/B
	fogColor_[1] = (float)g / 255.f;
	fogColor_[2] = (float)r / 255.f;
	fogColor_[3] = alpha;
	fogStart_ = (float)fogMin * fogScale;
	fogEnd_ = ((float)fogRange / alpha + (float)fogMin) * fogScale;
	if (fogEnd_ > 0.499f) { fogStart_ = 9999.f; fogEnd_ = 10000.f; }
}

void World3D::uploadMapTextures(const MapData& map, const MediaLoader& media) {	media_ = &media;
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

		if (tile == 275 || tile == 276)
			fprintf(stderr, "WALL tex tile=%d mediaId=%d texIdx=%d palIdx=%d %dx%d data=%zu\n",
				tile, mediaId, texIdx, palIdx, tex.width, tex.height, tex.data.size());

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
	// TILE-flagged sprites (0x400000) are wall/doors: legacy adds 257 to map
	// the tile into the wall range, so their textures (mediaMappings[tile+257])
	// must be loaded too.
	spriteTexByMedia_.clear();
	for (int i = 0; i < map.numSprites; ++i) {
		int info = map.mapSpriteInfo[i];
		int tileNum = info & 0xFF;
		if (info & 0x400000) tileNum += 257;
		const auto& m = media.mappings();
		if (tileNum >= (int)m.mappings.size()) continue;
		int lo = m.mappings[tileNum];
		int hi = (tileNum + 1 < (int)m.mappings.size()) ? m.mappings[tileNum + 1] : lo + 1;
		if (lo < 0) continue;
		// Upload the WHOLE mapping range for each encountered sprite tileNum:
		// doors switch to media frame 1 while open/animating and AUTO_ANIMATE
		// frames cycle over time; the draw path resolves
		// mediaId = mappings[tileNum] + frame from the sprite info bits.
		for (int mediaId = lo; mediaId < hi && mediaId < MediaMappings::kMaxMedia; ++mediaId) {
			ensureSpriteTexture(media, tileNum, mediaId);
		}
	}
}

// Decodes/uploads one sprite mediaId and caches it (dedup inside via count()).
// Mirrors legacy Render::setupTexture: textures are created lazily on first
// use, so a scripted setLineLocked flipping a sprite's tileNum after load
// still resolves its (new) mediaId at draw time instead of vanishing.
bool World3D::ensureSpriteTexture(const MediaLoader& media, int tileNum, int mediaId) {
	if (mediaId < 0 || mediaId >= MediaMappings::kMaxMedia) return false;
	if (spriteTexByMedia_.count(mediaId)) return true;

	int texIdx = media.texelIndexFor(mediaId);
	int palIdx = media.paletteIndexFor(mediaId);
	if (texIdx < 0 || palIdx < 0) {
		return false;
	}
	const MediaTexel& tex = media.texel(texIdx);
	const MediaPalette& pal = media.palette(palIdx);

	std::vector<uint8_t> indices;
	bool isRle = tex.data.size() != (size_t)(tex.width * tex.height);
	if (!isRle) {
		indices = tex.data;
	} else {
		int16_t bounds[4] = {
			(int16_t)(media.mappings().bounds[mediaId * 4 + 0]),
			(int16_t)(media.mappings().bounds[mediaId * 4 + 1]),
			(int16_t)(media.mappings().bounds[mediaId * 4 + 2]),
			(int16_t)(media.mappings().bounds[mediaId * 4 + 3]),
		};
		indices = decodeSpriteRLE(tex.data, tex.width, tex.height, bounds);
	}
	Texture t;
	bool upOk = t.uploadIndexed(indices, tex.width, tex.height, pal.colors, true, true);
	if (upOk) {
		spriteTexByMedia_[mediaId] = std::move(t);
		spriteIsRle_[mediaId] = isRle;
	}
	return upOk;
}

void World3D::begin(const Camera3D& camera) {
	cam_ = &camera;
	// Original GL path (GLES::SetGLState) disables depth test and relies on
	// painter's algorithm (BSP order + sprite depth sort). Keep depth off.
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	shader_.use();
	shader_.setMat4("uMVP", camera.mvp());
	shader_.setMat4("uView", camera.viewFloat());
	shader_.setInt("uTexture", 0);
	shader_.setInt("uPalette", 1);
	shader_.setInt("uFogEnabled", fogEnabled_ ? 1 : 0);
	shader_.setFloat("uFogStart", fogStart_);
	shader_.setFloat("uFogEnd", fogEnd_);
	shader_.setVec4("uFogColor", fogColor_[0], fogColor_[1], fogColor_[2], fogColor_[3]);
	glBindVertexArray(vao_);

	vertices_.clear();
	vertexCount_ = 0;
	currentTex_ = 0;
	currentPal_ = 0;
	// begin() set standard alpha blending + fog above; sync the trackers
	// (applyBatchState early-outs while they match).
	currentRenderMode_ = 0;
	currentFogOn_ = fogEnabled_;
	begun_ = true;
}

void World3D::end() {
	flush();
	glBindVertexArray(0);
	// Restore the standard blend equation so code outside begin()/end()
	// never sees an ADD/SUB leftover (src/GLES.cpp:626).
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	currentRenderMode_ = -1;
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

// Legacy gles::SetupTexture renderMode switch (src/GLES.cpp:623-715): the
// blend equation and fog toggle change per sprite mode; a pending batch is
// flushed first so its vertices draw under their own state.
void World3D::applyBatchState(int renderMode) {
	// ADD/SUB run with fog off (fogMode = 0, src/GLES.cpp:651,675); fog is
	// only ever enabled globally when fogEnabled_ is set.
	const bool fogOn = (renderMode != 3 && renderMode != 7) && fogEnabled_;
	if (renderMode == currentRenderMode_ && fogOn == currentFogOn_) return;
	flush();
	switch (renderMode) {
	case 3:  // RENDER_ADD: dst += src (black fire backing adds nothing)
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);                     // src/GLES.cpp:649
		break;
	case 7:  // RENDER_SUB: scorch marks darken
		glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);          // src/GLES.cpp:673
		break;
	default: // RENDER_NORMAL / alpha blends
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		break;
	}
	shader_.setInt("uFogEnabled", fogOn ? 1 : 0);
	currentRenderMode_ = renderMode;
	currentFogOn_ = fogOn;
}

void World3D::drawPoly(const MapData& map, int polyIdx) {
	const auto& p = map.polygons[polyIdx];
	if (p.verts.size() < 3) return;

	// Geometry always composites with standard alpha + global fog; a previous
	// sprite batch may have left ADD/SUB state active (drawBSP interleaves).
	applyBatchState(0);

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

	// Animated flat textures (legacy drawNodeGeometry:968-977): lava scrolls
	// its UVs over time. Only applies to the base frame.
	int lavaS = 0, lavaT = 0;
	if (p.textureId == 479 /* FLAT_LAVA */) {
		lavaS = (timeMs_ / 16) & 0x3FF;
		lavaT = (timeMs_ / 32) & 0x3FF;
	} else if (p.textureId == 480 /* FLAT_LAVA2 */) {
		lavaT = (timeMs_ / 4) & 0x3FF;
	}

	// Fan triangulation around vertex 0 (matches legacy quad_indexes).
	for (size_t i = 1; i + 1 < p.verts.size(); ++i) {
		const auto& v0 = p.verts[0];
		const auto& v1 = p.verts[i];
		const auto& v2 = p.verts[i + 1];
		Vertex tri[3] = {
			{ (float)v0.x * (1.f/16384.f), (float)v0.y * (1.f/16384.f), (float)v0.z * (1.f/16384.f),
			  (float)(v0.s + lavaS) * (1.f/1024.f),  (float)(v0.t + lavaT) * (1.f/1024.f) },
			{ (float)v1.x * (1.f/16384.f), (float)v1.y * (1.f/16384.f), (float)v1.z * (1.f/16384.f),
			  (float)(v1.s + lavaS) * (1.f/1024.f),  (float)(v1.t + lavaT) * (1.f/1024.f) },
			{ (float)v2.x * (1.f/16384.f), (float)v2.y * (1.f/16384.f), (float)v2.z * (1.f/16384.f),
			  (float)(v2.s + lavaS) * (1.f/1024.f),  (float)(v2.t + lavaT) * (1.f/1024.f) },
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
		drawSprite(map, media, camera, i, nullptr);
	}
	end();
}

// Renders one map sprite (billboard, stacked character, or wall decal). Must
// be called between begin()/end(). No depth test: correct ordering is ensured
// by the caller (drawSprites sorts by mvp depth; drawBSP interleaves per leaf).
void World3D::drawSprite(const MapData& map, const MediaLoader& media, const Camera3D& camera, int i,
	const uint8_t* charClass) {
	const int n = map.numSprites;
	int info = map.mapSpriteInfo[i];

	int tileNum = info & 0xFF;
	// SPRITE_FLAG_TILE (0x400000): the sprite is a wall/door decoration; legacy
	// renderSpriteObject adds 257 to map tileNum into the wall range (271-278
	// are doors). Monsters/pickups never carry this bit.
	if (info & 0x400000) tileNum += 257;

	// TEMP [dbg] lift one-shot diagnostics (remove once elevator-glass is
	// visually confirmed, docs/research/2026-08-25-elevator-glass.md): dumps
	// identity + draw-branch decision + resolved mediaId + BSP leaf for the
	// elevator shaft sprite group {149,152,153,154,155} on FIRST processing
	// of each index. NOENTITY keeps these sprites off the stacked-character
	// route below.
	static const int kDbgLiftSprites[5] = { 149, 152, 153, 154, 155 };
	static bool dbgLiftFired[5] = {};
	for (int k = 0; k < 5 && i >= 149 && i <= 155; ++k) {
		if (i != kDbgLiftSprites[k] || dbgLiftFired[k]) continue;
		dbgLiftFired[k] = true;
		int dFrame = (info >> 8) & 0xFF;
		if ((info & 0x80000) && dFrame > 0) dFrame = (i + timeMs_ / 100) % dFrame;
		const char* dBranch = "billboard";
		if (tileNum == 240 /* WATER_STREAM */) dBranch = "none (water)";
		else if ((info & 0x2F000000) != 0)
			dBranch = (info & 0x20000000) ? "flat-plane" : "vertical-wall";
		int dMedia = -1;
		const auto& dm = media.mappings();
		if (tileNum < (int)dm.mappings.size() && dm.mappings[tileNum] >= 0) {
			int dLo = dm.mappings[tileNum];
			int dHi = (tileNum + 1 < (int)dm.mappings.size())
				? dm.mappings[tileNum + 1] : dLo + 1;
			dMedia = dLo + dFrame;
			if (dMedia >= dHi) dMedia = dLo;       // same clamp as the draw path
		}
		// BSP leaf exactly as drawBSP feeds getNodeForPoint (height-snapped Z).
		int dx = map.mapSprites[i + 0 * n], dy = map.mapSprites[i + 1 * n];
		int dz = map.mapSprites[i + 2 * n] << 4;
		if (!map.heightMap.empty()) {
			int h = (map.heightMap[(((dy & 0x7FF) >> 6) * 32 + ((dx & 0x7FF) >> 6))] << 3) << 4;
			if (i >= map.numNormalSprites) h -= 32 << 4;
			dz += h;
		}
		int dLeaf = getNodeForPoint(map, dx << 4, dy << 4, dz, info);
		fprintf(stderr,
			"[dbg] lift spr=%d first processed: info=0x%08X tileNum=%d frame=%d branch=%s mediaId=%d leaf=%d\n",
			i, (unsigned)info, tileNum, dFrame, dBranch, dMedia, dLeaf);
	}

	// Blend/fog state for this sprite's legacy renderMode (S_RENDERMODE),
	// applied BEFORE any emission so characters inherit a sane state too.
	const int renderMode = map.mapSprites[i + 3 * n];
	applyBatchState(renderMode);

	// Stacked-character path (ADR 0005/0007): tested BEFORE the AUTO_ANIMATE
	// frame override so bits 8-15 can never be double-consumed (spec §1).
	// Exclusive of doors/walls by construction: classified sprites carry no
	// TILE flag. Classification is purely entity-def driven (see the
	// GameContext classification loop); legacy has NO anim-byte routing —
	// entity-less sprites carrying a MANIM_DEAD byte intentionally fall back
	// to the single-quad billboard below (raw-frame clamp vs legacy
	// flat-array bleed, known edge divergence).
	if (charClass != nullptr && charClass[i] != 0) {
		drawCharacter(map, media, i);
		return;
	}

	// TEMP [dbg] dead-fallback tripwire (remove at sign-off, spec
	// 2026-08-26 §4): fires once per sprite when a MANIM_DEAD-byte sprite
	// draws through this fallback — proves the retired anim-byte routing
	// hack caught nothing on map00 during acceptance.
	{
		static std::vector<uint8_t> dbgDeadFired;
		if ((int)dbgDeadFired.size() != n) dbgDeadFired.assign(n, 0);
		if ((info >> 8 & kManimMask) == kManimDead && !dbgDeadFired[i]) {
			dbgDeadFired[i] = 1;
			fprintf(stderr, "[dbg] dead-fallback spr=%d tile=%d\n", i, tileNum);
		}
	}

	int frame = (info >> 8) & 0xFF;
	// AUTO_ANIMATE (0x80000): frame cycles over time; the stored value is the
	// number of frames (legacy renderSpriteObject:1544-1546).
	if ((info & 0x80000) && frame > 0) {
		frame = (i + timeMs_ / 100) % frame;
	}
	if (tileNum == 240 /* WATER_STREAM */) return; // needs special handling

	int x = map.mapSprites[i + 0 * n];
	int y = map.mapSprites[i + 1 * n];
	int z = map.mapSprites[i + 2 * n] << 4;
	int scaleFactor = map.mapSprites[i + 8 * n] << 10;

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
	// Lazy-create-and-cache (legacy Render::setupTexture): a scripted
	// setLineLocked can flip tileNum (e.g. blue locked 273 -> unlocked 274)
	// after uploadMapTextures preloaded only the load-time range, so draw may
	// resolve a mediaId that was never uploaded.
	if (!spriteTexByMedia_.count(mediaId))
		ensureSpriteTexture(media, tileNum, mediaId);
	auto it = spriteTexByMedia_.find(mediaId);
	if (it == spriteTexByMedia_.end()) return;

	// Flush on texture change (wall branch binds here; billboard quads bind
	// inside drawBillboardPart — idempotent when unchanged).
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
	// Billboards: sprites WITHOUT wall/plane flags (0x2F000000 == 0). Wall
	// decals AND TILE sprites (doors 271-278, wall decorations) use the wall
	// branch. Monsters/pickups have no such flags -> billboards.
	const bool isWall = (info & 0x2F000000) != 0;
	if (!isWall) {
		drawBillboardPart(map, media, x, y, z, tileNum, mediaId, info, scaleFactor);
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
		// Door-lerp (legacy renderSprite 585-599): while a door opens/closes,
		// geometry is drawn at FULL size (scale forced to 65536) and only one
		// axis collapses by the real scale, with matching UV compensation.
		int realScale = scaleFactor;
		bool doorLerp = (info & 0x80000000) != 0;
		if (doorLerp) scaleFactor = 65536;
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
		int n28 = ((n23 + 4) & 0x7) << 1; // perpendicular axis for FLAT (src/Render.cpp:560)
		int n30 = n27 << 4;               // width extent  (<<4 render units)
		int n31 = n26 << 4;               // height extent (<<4 render units)

		// Legacy terminal/portal Z corrections (renderSprite ~517-523).
		if (tileNum >= 179 && tileNum <= 183) z -= 256; // terminals
		if (tileNum >= 155 && tileNum <= 156) z -= 128; // portal eye (socket 157 excluded, src/Render.cpp:521-523)

		// UVs (bounds-based, s/t flips for 0x20000/0x40000) — computed BEFORE
		// the doorLerp block so lerp can shrink/shift the windows
		// (src/Render.cpp:443-448,576-583).
		int s13 = (b[0] << 10) / sWidth;
		int s14 = (n11 << 10) / sWidth;
		int t15 = ((tHeight - b[3]) << 10) / tHeight;
		int t16 = (n12 << 10) / tHeight;
		if (info & 0x20000) { s13 += s14; s14 = -s14; }
		if (info & 0x40000) { t15 += t16; t16 = -t16; }

		// DoorLerp two-case block (src/Render.cpp:585-599): red/blue slip
		// doors collapse VERTICALLY with the v-window re-centered by half
		// delta; slide doors / other walls collapse WIDTH with the u-window
		// shifted by the FULL delta (texture stays glued to the jamb).
		int n32 = n31; // FULL height extent saved before collapse
		if (doorLerp) {
			if (tileNum >= 271 && tileNum <= 274) {   // red/blue slip doors: VERTICAL collapse (R12)
				int n33 = t16;                        // src/Render.cpp:588
				n31 = (realScale * n31) / 65536;      // height extent *= lerp fraction   :589
				t16 = (realScale * t16) / 65536;      // v-window shrinks                 :590
				t15 += (n33 - t16) >> 1;              // v-window RE-CENTERED, half delta :591
			} else {                                  // slide doors / other walls: WIDTH collapse (R13)
				int n34 = s14;                        // src/Render.cpp:594
				n30 = (realScale * n30) / 65536;      // width extent *= lerp fraction   :595
				s14 = (realScale * s14) / 65536;      // u-window shrinks                :596
				s13 += n34 - s14;                     // u-window shifted by FULL delta  :597
			}
		}

		if (tileNum >= 271 && tileNum <= 274) {
			// Slip-door split (src/Render.cpp:601-623): triggers on TILE RANGE
			// ALONE (even closed, even without DOORLERP). Two stacked
			// half-quads with a growing gap between them; local z/t15 copies
			// (legacy mutates them per j). Closed: gap 0 -> seamless panel.
			// Fully open: degenerate invisible quads.
			int n35 = t16 >> 1;    // quarter v-window            :602
			int n36 = n31 >> 1;    // lerped half-height          :603
			int n37 = n32 >> 1;    // ORIGINAL half-height        :604
			int zLocal = z;
			int t15Local = t15;
			for (int j = 0; j < 2; ++j) {
				Vertex quad[4];
				for (int k = 0; k < 4; ++k) {
					int n38 = (k & 2) >> 1;              // row
					int n39 = (k & 1) ^ n38 ^ 1;         // col (1 = right)
					int n40 = (n39 * 2 - 1) * n30;       // along-wall offset
					int n41 = (n38 * 2 - 1) * n36;       // vertical offset
					float px = (float)(x << 4) + (float)(kViewStepValues[n29 + 0] >> 6) * n40;
					float py = (float)(y << 4) + (float)(kViewStepValues[n29 + 1] >> 6) * n40;
					float pz = (float)zLocal + (float)(n38 * n41)
						+ (float)(j * ((n37 - n36) << 1)); // growing gap (C2)
					float s = (float)(s13 + n39 * s14) * (1.f / 1024.f);
					float t = (float)(t15Local + n38 * n35) * (1.f / 1024.f);
					quad[k] = { px * k1, py * k1, pz * k1, s, t };
				}
				Vertex tri[6] = { quad[0], quad[1], quad[2], quad[0], quad[2], quad[3] };
				if (vertices_.size() + 6 > kMaxVerts) flush();
				vertices_.insert(vertices_.end(), tri, tri + 6);
				zLocal += n36;     // src/Render.cpp:618
				t15Local += n35;   // src/Render.cpp:619
			}
		} else if (info & 0x20000000) {
			// FLAT plane quad (src/Render.cpp:639-661): horizontal quad using
			// BOTH viewStepValues axes; swapXY=false irrelevant here (C8).
			// Lava scroll omitted (out of scope).
			Vertex quad[4];
			for (int k = 0; k < 4; ++k) {
				int n46 = (k & 2) >> 1;
				int n47 = (k & 1) ^ n46 ^ 1;
				int n48 = (n47 * 2 - 1) * n30;            // along-axis ±(w<<4)
				int n49 = ((n46 * 2 - 1) * n31) >> 1;     // perpendicular ±(h<<4 >> 1)
				float px = (float)(x << 4)
					+ (float)(kViewStepValues[n29 + 0] >> 6) * n48
					+ (float)(kViewStepValues[n28 + 0] >> 6) * n49;
				float py = (float)(y << 4)
					+ (float)(kViewStepValues[n29 + 1] >> 6) * n48
					+ (float)(kViewStepValues[n28 + 1] >> 6) * n49;
				float pz = (float)z;
				float s = (float)(s13 + n47 * s14) * (1.f / 1024.f);
				float t = (float)(t15 + n46 * t16) * (1.f / 1024.f);
				quad[k] = { px * k1, py * k1, pz * k1, s, t };
			}
			Vertex tri[6] = { quad[0], quad[1], quad[2], quad[0], quad[2], quad[3] };
			if (vertices_.size() + 6 > kMaxVerts) flush();
			vertices_.insert(vertices_.end(), tri, tri + 6);
		} else {
			// Wall single quad: along viewStepValues[n29] (wall surface
			// direction), carrying the lerped n30/n31/s13/s14/t15/t16 values.
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
}

// One camera-facing billboard quad; extracted verbatim from drawSprite's
// billboard body (C1a pure refactor). Legacy Render::renderSprite billboard
// branch (src/Render.cpp:455-513 GL sub-path).
void World3D::drawBillboardPart(const MapData& map, const MediaLoader& media,
	int x, int y, int zRenderUnits, int tileNum, int mediaId,
	int flags, int scaleFactor) {
	(void)map;
	auto it = spriteTexByMedia_.find(mediaId);
	if (it == spriteTexByMedia_.end()) return;

	// Image bounds for UVs and billboard size. Legacy (Render.cpp:461/492)
	// uses bounds-based UVs/size when the sprite has the TILE flag (0x400000)
	// OR the texture is raw (Size == w*h). Otherwise (RLE, no TILE) it uses
	// full-texture UVs with a fixed 518/1036 size and crops to 176 rows
	// (DrawWorldSpaceSpriteLine:176).
	bool isRle = spriteIsRle_.count(mediaId) ? spriteIsRle_[mediaId] : false;
	bool useBounds = (flags & 0x400000) != 0 || !isRle;
	int n13, n14, n15, n16, n19, n20;
	int sWidth = 1024, tHeight = 1024;
	const auto& m = media.mappings();
	if (useBounds) {
		int16_t b[4] = {
			(int16_t)(m.bounds[mediaId * 4 + 0]),
			(int16_t)(m.bounds[mediaId * 4 + 1]),
			(int16_t)(m.bounds[mediaId * 4 + 2]),
			(int16_t)(m.bounds[mediaId * 4 + 3]),
		};
		int wb = (media.mappings().dimensions[mediaId] >> 4) & 0xF;
		int hb = media.mappings().dimensions[mediaId] & 0xF;
		sWidth = 1 << wb;
		tHeight = 1 << hb;
		int n11 = b[1] - b[0];
		int n12 = b[3] - b[2];
		n13 = (b[0] << 10) / sWidth;
		n14 = (n11 << 10) / sWidth;
		n15 = ((tHeight - b[3]) << 10) / tHeight;
		n16 = (n12 << 10) / tHeight;
		n19 = ((((n11 >> 2) << 4) + 7) * scaleFactor) / 0x10000;
		n20 = ((((n12 >> 1) << 4) + 7) * scaleFactor) / 0x10000;
	} else {
		int wb = (media.mappings().dimensions[mediaId] >> 4) & 0xF;
		int hb = media.mappings().dimensions[mediaId] & 0xF;
		sWidth = 1 << wb;
		tHeight = 1 << hb;
		n13 = 0; n14 = 1024; n15 = 0; n16 = 1024;
		n19 = (518 * scaleFactor) / 0x10000;
		n20 = (1036 * scaleFactor) / 0x10000;
	}

	// Flush on texture change (no-op when the caller pre-bound this texture).
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
	const int* st = cam_ ? cam_->viewInt() : nullptr;
	if (!st) return;
	int z = zRenderUnits;
	// Legacy: z -= 512; position nudged back by n17 (10 raw, 12 RLE).
	z -= 512;
	int n17 = isRle ? 12 : 10;
	// viewSin/viewCos come from the sine table (NOT the view matrix!).
	const int32_t* sinTbl = cam_->sinTable();
	int yaw = cam_->viewYaw() & 0x3FF;
	int viewSin = sinTbl[yaw];
	int viewCos = sinTbl[(yaw + 256) & 0x3FF];
	x -= n17 * viewCos >> 16;
	y += n17 * viewSin >> 16;
	// Crates sit lower (legacy renderSprite:476-478).
	if (tileNum == 152 /* TILENUM_OBJ_CRATE */) z -= 224;

	// Four billboard corners (viewMtxMove: right += view[i]*n2>>14,
	// up: n3=-n3). view_ row0=(view[0],view[4],view[8]) right axis,
	// row1=(view[1],view[5],view[9]) up axis (14.14).
	float wx[4], wy[4], wz[4];
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
		wx[ci] = px; wy[ci] = py; wz[ci] = pz;
	}

	Vertex quad[4];
	for (int ci = 0; ci < 4; ++ci) {
		int n21 = (ci & 2) >> 1;
		int n22 = (ci & 1) ^ n21 ^ 1;
		float s, t;
		if (!useBounds) {
			// GL-path billboard UV override (src/GLES.cpp:515-542): s/t
			// derive from the corner index; flip tests XOR 0x60000 so the
			// default (no flags) takes the "flipped" branch, which yields
			// the NORMAL orientation. Integer math first, then a single
			// float conversion (as legacy: (s*176)/sWidth then /1024). This
			// override is also what applies the 0x20000 H-flip used by the
			// walk-cycle leg mirroring.
			int sW = (((flags ^ 0x60000) & 0x20000) != 0) ? n22 : (n22 ^ 1);
			int tW = (((flags ^ 0x60000) & 0x40000) != 0) ? (n21 ^ 1) : n21;
			s = (float)(((sW * 1024) * 176) / sWidth) * (1.f / 1024.f);
			t = (float)(((tW * 1024) * 176) / tHeight) * (1.f / 1024.f);
		} else {
			// Bounds-based billboards keep the legacy ClipQuad window UVs
			// with NO flips (src/Render.cpp:479-493). Vertical flip for
			// billboards: texel data is top-down but GL v=0 is the bottom
			// texel row.
			s = (float)(n13 + n22 * n14) * (1.f / 1024.f);
			t = 1.f - (float)(n15 + n21 * n16) * (1.f / 1024.f);
		}
		quad[ci] = { wx[ci] * k1, wy[ci] * k1, wz[ci] * k1, s, t };
	}
	// Fan triangles 0,1,2 and 0,2,3 (matches quad_indexes).
	Vertex tri[6] = { quad[0], quad[1], quad[2], quad[0], quad[2], quad[3] };
	if (vertices_.size() + 6 > kMaxVerts) flush();
	vertices_.insert(vertices_.end(), tri, tri + 6);
}

// Stacked leg/torso/head character renderer. Port of legacy
// Render::renderSpriteAnim (src/Render.cpp:3144-3488) for NPCs and the
// non-diverted monster families (ADR 0007); floater/special-boss families
// stay on the billboard path until their renderers land. ATTACK carries the
// monster deltas of spec 2026-08-26 §2. Emission order gives the painter
// stacking: shadow first, then legs -> torso -> head.
void World3D::drawCharacter(const MapData& map, const MediaLoader& media, int i) {
	const int n = map.numSprites;
	const int info = map.mapSpriteInfo[i];
	const int tileNum = info & 0xFF;              // characters never carry 0x400000
	const int animByte = (info >> 8) & 0xFF;      // ONE packed state byte
	const int anim = animByte & kManimMask;
	const int frame = animByte & kMframeMask;
	int x = map.mapSprites[i + 0 * n];
	int y = map.mapSprites[i + 1 * n];
	int zR = map.mapSprites[i + 2 * n] << 4;
	const int scaleFactor = map.mapSprites[i + 8 * n] << 10;

	// Terrain add of the drawSprite preamble (World3D.cpp postProcessSprites
	// port); groundZ below uses raw terrain only (src/Render.cpp:3177).
	if (!map.heightMap.empty()) {
		int hx = x & 0x7FF, hy = y & 0x7FF;
		zR += (map.heightMap[((hy >> 6) * 32 + (hx >> 6))] << 3) << 4;
		if (i >= map.numNormalSprites) zR -= 32 << 4;
	}

	// Shadow + ground (src/Render.cpp:3172-3180): NPC art glues the shadow to
	// the feet (full scale at own z), everything else shrinks with altitude.
	bool npc = tileNum >= kTileNumFirstNpc && tileNum <= kTileNumLastNpc;
	int groundZ = zR;
	int min = 0;
	if (!npc) {
		int hCanvas = 0;
		if (!map.heightMap.empty()) {
			int hx = x & 0x7FF, hy = y & 0x7FF;
			hCanvas = map.heightMap[((hy >> 6) * 32 + (hx >> 6))] << 3;
		}
		groundZ = (hCanvas + 32) << 4;
		min = zR - groundZ;
		if (min < 0) min = 0;
		if (min > 256) min = 256;
	}
	int shadowScale = scaleFactor * (256 - min) / 256;

	// Idle bob, phase-staggered per sprite (src/Render.cpp:3195-3199). The
	// entity->info & 0x20000000 suppression has no carrier in the rewrite.
	int bob = (((timeMs_ + i * 1337) / 1024) & 1) * 26;

	// Camera-right step for lateral sway (canvas units; src/Render.cpp:2276-2278).
	const int32_t* sinTbl = cam_ ? cam_->sinTable() : nullptr;
	if (!sinTbl || !cam_) return;
	int yaw = cam_->viewYaw();
	int viewRightStepX = sinTbl[yaw & 0x3FF] >> 10;
	int viewRightStepY = -sinTbl[(yaw - 256) & 0x3FF] >> 10;

	// Emits one part quad: resolves mediaId = mappings[tile] + fIdx with
	// the same clamp/lazy-upload as drawSprite (:559-582), then billboards it.
	const auto& mappings = media.mappings();
	auto emitTile = [&](int tile, int fIdx, int px, int py, int pz, int pFlags, int pScale) {
		if (tile >= (int)mappings.mappings.size()) return;
		int lo = mappings.mappings[tile];
		int hi = (tile + 1 < (int)mappings.mappings.size()) ? mappings.mappings[tile + 1] : lo + 1;
		if (lo < 0) return;
		int mediaId = lo + fIdx;
		if (mediaId >= hi) mediaId = lo;
		if (!spriteTexByMedia_.count(mediaId))
			ensureSpriteTexture(media, tile, mediaId);
		drawBillboardPart(map, media, px, py, pz, tile, mediaId, pFlags, pScale);
	};
	auto emitPart = [&](int fIdx, int px, int py, int pz, int pFlags, int pScale) {
		emitTile(tileNum, fIdx, px, py, pz, pFlags, pScale);
	};
	auto emitShadow = [&]() {
		emitTile(kTileNumShadow, 0, x, y, groundZ, info, shadowScale); // :3202
	};

	switch (anim) {
	case kManimIdleBack:
	case kManimIdle: {
		int base = (anim == kManimIdleBack) ? 4 : 0;
		// Riley/Civilian2/Scientist idle offsets (src/Render.cpp:3210-3216, :3246-3248).
		int rileyTorso = (anim == kManimIdle &&
			(tileNum == kTileNumNpcRileyOconnor || tileNum == kTileNumNpcCivilian2 ||
			 tileNum == kTileNumNpcScientist)) ? -18 : 0;
		int rileyHead = (tileNum == kTileNumNpcRileyOconnor && anim == kManimIdle) ? -32 : 0;
		emitShadow();                                          // :3202
		emitPart(base + 0, x, y, zR, info, scaleFactor);       // legs :3204
		emitPart(base + 2, x, y, zR + bob + rileyTorso, info, scaleFactor); // torso :3231
		emitPart(base + 3, x, y, zR + bob + rileyHead, info, scaleFactor);  // head :3252
		break;
	}
	case kManimWalkBack:
	case kManimWalkFront: {
		int base = (anim == kManimWalkBack) ? 4 : 0;
		// Two art frames x mirror bit = 4 visual phases (src/Render.cpp:3261).
		int mirror = (((frame >> 1) ^ 1)) << 17;
		int legFlags = info ^ mirror;
		emitShadow();                                                          // :3263
		emitPart(base + (frame & 1), x, y, zR, legFlags, scaleFactor);         // legs :3265
		int sway = frame & 1;
		if ((frame & 2) == 0) sway = -sway;                                    // :3266-3269
		int sx = x + (sway * viewRightStepX >> 6);                             // :3270
		int sy = y + (sway * viewRightStepY >> 6);                             // :3271
		int vBobZ = zR + ((frame & 1) << 4);                                   // +16 on odd phases
		// NPC torsos (except Sarge) mirror with the legs (:3277).
		int torsoFlags = (npc && tileNum != kTileNumNpcSarge) ? legFlags : info;
		emitPart(base + 2, sx, sy, vBobZ, torsoFlags, scaleFactor);            // torso
		emitPart(base + 3, sx, sy, vBobZ, info, scaleFactor);                  // head :3290
		break;
	}
	case kManimAttack1:
	case kManimAttack2: {
		// Monster-family deltas (spec 2026-08-26 §2). Zombie frame-1 flip
		// rides the LEGS only — n25 is consumed at src/Render.cpp:3333 alone;
		// torso/head take raw flags (:3359/:3409, erratum E1).
		int legFlags = info;
		if (isZombieFamily(tileNum) && frame == 1) legFlags ^= 0x20000; // :3299-3301
		int pose = (anim == kManimAttack1) ? 8 : 10;                    // :3303-3308
		if (frame == 1 && !hasGunFlareFamily(tileNum)) ++pose;          // :3355-3357, set :3019-3021
		emitShadow();                                                   // :3310
		emitPart(0, x, y, zR, legFlags ^ 0x20000, scaleFactor);         // legs, n24=0 :3333
		emitPart(pose, x, y, zR, info, scaleFactor);                    // torso, n15=0 on map00 :3359
		if (!isZombieFamily(tileNum) && !isImpFamily(tileNum))          // head gate :3376
			emitPart(3, x, y, zR, info, scaleFactor);                   // head, n18=n16=0 on map00 :3409
		break;
	}
	case kManimPain:
	case kManimSlap: {
		emitShadow();                                          // :3466
		emitPart(12, x, y, zR, info, scaleFactor);             // :3467
		break;
	}
	case kManimDead: {
		// Single corpse quad, NO shadow; lootable pulsate halo deferred (§8).
		emitPart(13, x, y, zR, info, scaleFactor);             // :3470-3475
		break;
	}
	case kManimNpcTalk: {
		emitPart(frame, x, y, zR, info, scaleFactor);          // :3482-3484
		break;
	}
	case kManimNpcBackAction: {
		emitShadow();                                          // :3478
		emitPart(8 + frame, x, y, zR, info, scaleFactor);      // :3479
		break;
	}
	default:
		break; // DODGE etc.: faithful empty fall-through (switch :3190-3486)
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

void World3D::drawBSP(const MapData& map, const MediaLoader& media, const Camera3D& camera,
	const int* spriteSortBias, const uint8_t* charClass) {
	if (!initialized_ || map.numNodes == 0) return;

	nodeIdxs_.clear();
	walkNode(map, 0, camera.viewX(), camera.viewY(), camera.viewZ());

	// Precompute the BSP leaf for every sprite (legacy relinkSprite does this
	// once at level load, AFTER postProcessSprites added terrain height to Z).
	// Sprites get drawn right after their leaf's geometry, so nearer leaves
	// overdraw farther sprites (painter's algorithm).
	std::vector<int> spriteLeaf(map.numSprites, -1);
	for (int i = 0; i < map.numSprites; ++i) {
		if (map.mapSpriteInfo[i] & 0x10000) continue; // invisible
		int x = map.mapSprites[i + 0 * map.numSprites];
		int y = map.mapSprites[i + 1 * map.numSprites];
		int z = map.mapSprites[i + 2 * map.numSprites];
		// Match legacy: Z includes terrain height before node lookup.
		int hz = z << 4;
		if (!map.heightMap.empty()) {
			int hx = x & 0x7FF, hy = y & 0x7FF;
			int h = (map.heightMap[((hy >> 6) * 32 + (hx >> 6))] << 3) << 4;
			if (i >= map.numNormalSprites) h -= 32 << 4;
			hz += h;
		}
		spriteLeaf[i] = getNodeForPoint(map, x << 4, y << 4, hz, map.mapSpriteInfo[i]);
	}

	begin(camera);
	// Legacy renderBSP draws nodeIdxs in reverse (far leaves first, so that
	// nearer leaves overdraw them — painter's algorithm). All sprites of a
	// leaf (decals + billboards) are drawn right after its geometry, sorted
	// by mvp depth (legacy addSprite) so nearer sprites overdraw farther ones
	// (e.g. a billboard is not overdrawn by a wall decal behind it).
	const int* mvp = camera.mvpInt();
	std::vector<int> leafSprites;
	std::vector<int> leafDepth;
	for (int k = (int)nodeIdxs_.size() - 1; k >= 0; --k) {
		int leaf = nodeIdxs_[k];
		for (size_t i = 0; i < map.polygons.size(); ++i) {
			if (map.polygons[i].leafNode == leaf) drawPoly(map, (int)i);
		}
		// Sprites of this leaf, drawn after its geometry.
		leafSprites.clear();
		leafDepth.clear();
		for (int i = 0; i < map.numSprites; ++i) {
			if (spriteLeaf[i] != leaf) continue;
			int info = map.mapSpriteInfo[i];
			if (info & 0x10000) continue;
			int x = map.mapSprites[i + 0 * map.numSprites];
			int y = map.mapSprites[i + 1 * map.numSprites];
			int z = map.mapSprites[i + 2 * map.numSprites];
			// Sort by HEIGHT-SNAPPED Z (post-snap, in map units — legacy sorts
			// the stored S_Z after postProcessSprites baked terrain height in,
			// src/Render.cpp:2459-2467,846).
			int zsnapped = z;
			if (!map.heightMap.empty()) {
				int hx = x & 0x7FF, hy = y & 0x7FF;
				zsnapped += map.heightMap[((hy >> 6) * 32 + (hx >> 6))] << 3;
				if (i >= map.numNormalSprites) zsnapped -= 32;
			}
			int d = (x * mvp[2] + y * mvp[6] + zsnapped * mvp[10] >> 14) + mvp[14];
			int tn = info & 0xFF;
			if (info & 0x10000000) d = (int)0x7FFFFFFF;   // DECAL bias (src/Render.cpp:839-841)
			else if (info & 0x400000) d += 6;             // TILE (src/Render.cpp:847-849)
			else if (tn == 240 || tn == 246 || tn == 245 || tn == 247)
				d = (int)0x80000000;                      // water (src/Render.cpp:850-852)
			else if (info & 0xF000000) d += 5;            // oriented (src/Render.cpp:853-855)
			else {
				// Entity bias hook (+1 corpse/linked, -1 monsters), supplied by
				// the caller. A biased sprite skips the tileNum biases like the
				// original else-if chain (src/Render.cpp:856-874).
				bool biased = false;
				if (spriteSortBias && spriteSortBias[i] != 0) { d += spriteSortBias[i]; biased = true; }
				if (!biased) {
					if ((tn >= 240 && tn <= 244) || tn == 255) d -= 3; // src/Render.cpp:863-865
					else if (tn >= 137 && tn <= 139) d += 2;           // :866-868
					else if (tn == 152) d += 5;                        // crate, :869-871
					else if (tn == 239) d -= 3;                        // :872-874
				}
			}
			leafSprites.push_back(i);
			leafDepth.push_back(d);
		}
		// Sort descending by depth (larger = farther, drawn first).
		for (size_t a = 0; a < leafSprites.size(); ++a) {
			for (size_t b = a + 1; b < leafSprites.size(); ++b) {
				if (leafDepth[b] > leafDepth[a]) {
					std::swap(leafSprites[a], leafSprites[b]);
					std::swap(leafDepth[a], leafDepth[b]);
				}
			}
		}
		for (int i : leafSprites) drawSprite(map, media, camera, i, charClass);
	}
	end();
}

} // namespace newcore