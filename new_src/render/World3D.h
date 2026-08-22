#ifndef NEW_RENDER_WORLD3D_H
#define NEW_RENDER_WORLD3D_H

#include <cstdint>
#include <map>
#include <vector>

#include "render/gl/GlCommon.h"
#include "render/gl/Shader.h"
#include "render/gl/Texture.h"
#include "render/Camera3D.h"
#include "io/Media.h"

namespace newcore {

class MapData;

// Renders the decoded world geometry (MapData.polygons) with a perspective
// camera on the modern GL 3.3 backend. Faithful port of the legacy GL path:
// vertices in world units (VERT_COORDS_TO_FLOAT = /16384), UVs in 2.14
// texel units (TEXT_COORDS_TO_FLOAT = /1024, tiled via GL_REPEAT), indexed
// textures through an RGBA8 palette LUT. Lighting/fog optional.
class World3D {
public:
	struct Vertex {
		float x, y, z; // world coords (already VERT_COORDS_TO_FLOAT scaled)
		float u, v;    // texel coords (TEXT_COORDS_TO_FLOAT scaled)
	};

	static constexpr int kMaxVerts = 65536;

	World3D();
	~World3D();

	World3D(const World3D&) = delete;
	World3D& operator=(const World3D&) = delete;

	// Compiles the world shader. Must be called once (needs GL context).
	bool initialize();

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
	void drawBSP(const MapData& map, const MediaLoader& media, const Camera3D& camera,
		const int* spriteSortBias = nullptr);

	// Renders map sprite billboards (legacy renderSpriteObject/renderSprite).
	// Static sprites only for now; entity-driven monsters/NPCs come later.
	void drawSprites(const MapData& map, const MediaLoader& media, const Camera3D& camera);

	// Renders a single polygon list (used for per-node BSP traversal later).
	void drawPolys(const MapData& map, const std::vector<int>& polyIdx, const Camera3D& camera);

	// Sets fog. Legacy GL_FOG linear in eye space: fogStart = fogMin * (1/8000),
	// fogEnd = (fogRange/fogColor.a + fogMin) * (1/8000). alpha==0 disables fog.
	void setFog(int fogColorARGB, int fogMin, int fogRange);

	// Sets the game time (ms) used for animated textures/sprites (lava UV
	// shift, auto-animate sprite frames). Mirrors legacy app->time.
	void setTime(int timeMs) { timeMs_ = timeMs; }

	bool initialized() const { return initialized_; }

private:
	void begin(const Camera3D& camera);
	void end();
	void drawPoly(const MapData& map, int polyIdx);
	void drawSprite(const MapData& map, const MediaLoader& media, const Camera3D& camera, int i);
	void flush();
	bool walkNode(const MapData& map, int n, int viewX, int viewY, int viewZ);
	int nodeClassifyPoint(const MapData& map, int n, int x, int y, int z);
	int getNodeForPoint(const MapData& map, int x, int y, int z, int info);

	Shader shader_;

	GLuint vao_ = 0;
	GLuint vbo_ = 0;
	GLsizei vertexCount_ = 0;

	std::vector<Vertex> vertices_;
	GLint locMVP_ = -1;

	// Palette LUT textures (RGBA8) bound as uPalette for each texture.
	// The index texture is the texel R8; both come from Texture objects.
	Texture white_;
	std::map<int, Texture> textureByTile_;

	bool initialized_ = false;
	bool begun_ = false;

	const MediaLoader* media_ = nullptr;
	const MapData* map_ = nullptr;

	// BSP traversal state (mirrors legacy Render fields).
	std::vector<int> nodeIdxs_; // visible leaf node indices

	// Fog state (uniforms set in begin()).
	bool fogEnabled_ = false;
	float fogStart_ = 0.f;
	float fogEnd_ = 0.f;
	float fogColor_[4] = { 0.f, 0.f, 0.f, 1.f };
	GLint locFogEnabled_ = -1, locFogStart_ = -1, locFogEnd_ = -1, locFogColor_ = -1;
	GLint locView_ = -1;

	// Game time (ms) for animated textures/sprites.
	int timeMs_ = 0;

	// Current bound texture (index + palette) so drawPoly can flush on change.
	GLuint currentTex_ = 0;
	GLuint currentPal_ = 0;

	// Sprite textures keyed by mediaId (RLE-decoded where needed).
	std::map<int, Texture> spriteTexByMedia_;
	// Whether the media's texel was column-RLE (drives billboard branch: RLE
	// sprites use full-texture UVs + fixed 518/1036 size, raw use bounds UVs).
	std::map<int, bool> spriteIsRle_;

	Texture sky_;
};

} // namespace newcore

#endif // NEW_RENDER_WORLD3D_H