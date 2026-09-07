#ifndef NEW_RENDER_WORLD3D_H
#define NEW_RENDER_WORLD3D_H

#include <cstdint>
#include <map>
#include <vector>

#include "render/api/Texture.h"
#include "render/Camera3D.h"
#include "io/Media.h"

namespace newcore {

class MapData;
class Scene3D;
class TextureStore;

// Renders the decoded world geometry (MapData.polygons) with a perspective
// camera. Backend-neutral geometry/ordering module (ADR 0020): it produces the
// painter-ordered triangle stream and hands it to a Scene3D device. Faithful
// port of the legacy GL path: vertices in world units
// (VERT_COORDS_TO_FLOAT = /16384), UVs in 2.14 texel units
// (TEXT_COORDS_TO_FLOAT = /1024, tiled), indexed textures through a palette.
class World3D {
public:
	World3D();
	~World3D();

	World3D(const World3D&) = delete;
	World3D& operator=(const World3D&) = delete;

	// Latches the device every triangle goes to and the store every texture of
	// the world is created in. Must be called once, after the backend is
	// initialized.
	bool initialize(Scene3D& scene, TextureStore& store);

	// Builds GPU textures for all media ids referenced by map.polygons
	// (via media.mappings()[textureId] -> mediaId -> texel/palette). Reuses
	// the MediaLoader's already-loaded palettes/texels.
	void uploadMapTextures(const MapData& map, const MediaLoader& media);

	// Renders all polygons. camera provides the MVP. If sortBackToFront is
	// true, polygons are depth-sorted by view distance (painter's algorithm);
	// otherwise draw order follows map order (caller must have pre-sorted).
	void drawWorld(const MapData& map, const Camera3D& camera);

	// Uploads the sky texture from tables.bin (256x256 indexed, palette A or B).
	// Mirrors legacy LoadingManager sky loading (skyMapTexels/skyMapPalette).
	void uploadSky(const std::vector<uint8_t>& texel, const std::vector<uint16_t>& palette);

	// Renders the full-screen sky quad (legacy gles::DrawSkyMap): identity
	// projection, NDC quad, UV from NDC + viewYaw/256 shift.
	void drawSky(const Camera3D& camera);

	// Renders the visible world via BSP traversal (walkNode), drawing only
	// leaves in near-to-far order (painter's algorithm) with each leaf's
	// sprites interleaved after its geometry — faithful to legacy renderBSP
	// (no depth buffer). Ports walkNode/nodeClassifyPoint/getNodeForPoint.
	// spriteSortBias: optional per-sprite extra depth bias (+1/-1 hook,
	// src/Render.cpp:856-862), indexed by sprite index; may be null.
	// charClass: optional per-sprite stacked-character classification bytes
	// (ADR 0005; 1 = draw via the leg/torso/head stack path); may be null.
	void drawBSP(const MapData& map, const MediaLoader& media, const Camera3D& camera,
		const int* spriteSortBias = nullptr, const uint8_t* charClass = nullptr);

	// Renders map sprite billboards (legacy renderSpriteObject/renderSprite).
	// Static sprites only for now; entity-driven monsters/NPCs come later.
	void drawSprites(const MapData& map, const MediaLoader& media, const Camera3D& camera);

	// Returns the cached (uploading lazily) texture for
	// mediaMappings[tileNum] + frame, or nullptr when the mapping/media is
	// missing. Screen-space consumers (view weapon, spec combat-stage1
	// §6.1); mediaId resolution mirrors the draw path (src/Render.cpp:2047).
	const Texture* spriteTexture(const MediaLoader& media, int tileNum, int frame);

	// Renders a single polygon list (used for per-node BSP traversal later).
	void drawPolys(const MapData& map, const std::vector<int>& polyIdx, const Camera3D& camera);

	// Sets fog on the device. Legacy GL_FOG linear in eye space:
	// fogStart = fogMin * (1/8000), fogEnd = (fogRange/fogColor.a + fogMin) *
	// (1/8000). alpha==0 disables fog. Requires initialize().
	void setFog(int fogColorARGB, int fogMin, int fogRange);

	// Sets the game time (ms) used for animated textures/sprites (lava UV
	// shift, auto-animate sprite frames). Mirrors legacy app->time.
	void setTime(int timeMs) { timeMs_ = timeMs; }

	bool initialized() const { return initialized_; }

private:
	void begin(const Camera3D& camera);
	void end();
	void drawPoly(const MapData& map, int polyIdx);
	void drawSprite(const MapData& map, const MediaLoader& media, const Camera3D& camera, int i,
		const uint8_t* charClass);
	// Emits one camera-facing billboard quad (legacy Render::renderSprite
	// billboard branch, src/Render.cpp:455-513 GL sub-path). All offsets in
	// the units of the drawSprite preamble. Must be called between begin()/end().
	void drawBillboardPart(const MapData& map, const MediaLoader& media,
	                       int x, int y, int zRenderUnits,   // canvas x/y; z already <<4
	                       int tileNum, int mediaId,         // texture resolved by caller
	                       int flags,                        // info low bits (0x20000 etc.)
	                       int scaleFactor);                 // byte<<10
	// Emits the torchiere's additive light-glow quad (tile 193
	// SFX_LIGHTGLOW1, frame 0) in RENDER_ADD50, BEFORE the lamp body — legacy
	// tile-136 branch src/Render.cpp:1642-1647, which has no return and falls
	// through to the body draw (docs/original-code/rendering.md §8.5). Leaves
	// the batch state on ADD50; the caller restores its own mode. Must be
	// called between begin()/end().
	void drawTorchiereGlow(const MapData& map, const MediaLoader& media,
	                       int x, int y, int zRenderUnits,   // same units as drawBillboardPart
	                       int flags, int scaleFactor);
	// Stacked leg/torso/head character renderer (legacy renderSpriteAnim,
	// src/Render.cpp:3144-3488; NPCs + non-diverted monster families per
	// ADR 0007, ATTACK deltas in spec 2026-08-26 §2). Must be called
	// between begin()/end().
	void drawCharacter(const MapData& map, const MediaLoader& media, int i);
	// Decodes/uploads the sprite texture for one mediaId on first use and
	// caches it (lazy like legacy Render::setupTexture). Returns false if the
	// media has no texel/palette or the upload failed; dedups against
	// spriteTexByMedia_ internally.
	bool ensureSpriteTexture(const MediaLoader& media, int tileNum, int mediaId);
	bool walkNode(const MapData& map, int n, int viewX, int viewY, int viewZ);
	void addSplitSprite(const MapData& map, size_t firstVisibleLeaf, int sprite);
	int nodeClassifyPoint(const MapData& map, int n, int x, int y, int z);
	int getNodeForPoint(const MapData& map, int x, int y, int z, int info);

	// The device every triangle of the world goes to and the store every
	// texture below lives in (both latched by initialize).
	Scene3D* scene_ = nullptr;
	TextureStore* store_ = nullptr;

	// Fallback texture for polygons whose tile has no media, plus the world
	// wall/floor/ceiling textures keyed by tile number.
	Texture white_;
	std::map<int, Texture> textureByTile_;

	bool initialized_ = false;

	const MediaLoader* media_ = nullptr;
	const MapData* map_ = nullptr;
	// Camera of the active begin()/end() batch (billboard corner build +
	// character sway/bob math read it; borrowed, not owned).
	const Camera3D* cam_ = nullptr;

	// BSP traversal state (mirrors legacy Render fields).
	std::vector<int> nodeIdxs_; // visible leaf node indices

	// Split-sprite rescue pipeline (legacy addSplitSprite/addNodeSprites,
	// src/Render.cpp:896-924): sprites classified onto INTERNAL BSP nodes by
	// getNodeForPoint's ±128 band early-out (src/Render.cpp:2422-2424) get
	// re-listed under the first visible leaf whose bounds overlap their
	// ±8-unit center box, so they stop being dropped by the leaf-only filter.
	// All buffers preallocated/resized once; counts reset per frame.
	static constexpr int kMaxSplitSprites = 8; // MAX_SPLIT_SPRITES, src/Render.h:121
	std::vector<int> spriteLeaf_;                    // per-sprite owner (leaf OR internal node index)
	std::vector<std::vector<int>> internalSprites_;  // per-node list of internally-attached sprites
	std::vector<int> splitPairs_;                    // flat [leaf,sprite]*kMaxSplitSprites pairs
	std::vector<int> leafSprites_, leafDepth_;       // per-leaf sorted draw lists (hoisted locals)

	// Game time (ms) for animated textures/sprites.
	int timeMs_ = 0;

	// Sprite textures keyed by mediaId (RLE-decoded where needed).
	std::map<int, Texture> spriteTexByMedia_;
	// Whether the media's texel was column-RLE (drives billboard branch: RLE
	// sprites use full-texture UVs + fixed 518/1036 size, raw use bounds UVs).
	std::map<int, bool> spriteIsRle_;

	Texture sky_;
};

} // namespace newcore

#endif // NEW_RENDER_WORLD3D_H