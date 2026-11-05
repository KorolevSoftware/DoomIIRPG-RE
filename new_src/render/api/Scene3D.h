#ifndef NEW_RENDER_API_SCENE3D_H
#define NEW_RENDER_API_SCENE3D_H

#include "render/api/TextureId.h"

namespace newcore {

// World-space vertex, already scaled by the legacy constants:
// position = raw >> VERT_COORDS_TO_FLOAT (/16384), uv = raw / 1024
// (World3D.cpp:565-575, TEXT_COORDS_TO_FLOAT).
struct WorldVertex { float x, y, z, u, v; };

struct SceneView {
	const float* mvp;   // float[16], column-major (Camera3D::mvp())
	const float* view;  // float[16], column-major (Camera3D::viewFloat())
};

// The world device (ADR 0020): a stream of {x,y,z,u,v} triangles in world
// units, in painter's order, grouped by texture and legacy RENDER_* mode.
class Scene3D {
public:
	virtual ~Scene3D() = default;

	// A scene is one painter-ordered pass inside the current canvas viewport
	// (set by RenderBackend::setCanvasViewport before the call). No depth test,
	// no culling — order is the caller's contract (legacy GLES::SetGLState).
	virtual void beginScene(const SceneView& view) = 0;
	virtual void endScene() = 0;

	// Sticky state; both flush the pending batch on change.
	virtual void setTexture(TextureId tex) = 0;
	virtual void setRenderMode(int renderMode) = 0;  // legacy RENDER_*
	// Legacy RENDER_FLAG_PULSATE state as the software (TinyGL) path draws it: a flat opaque
	// grey silhouette of the texel shape, grey level a in [0,1]. Sticky until the next
	// setRenderMode; flushes the pending batch on change.
	virtual void setPulsate(float a) = 0;

	// count must be a multiple of 3. Triangles are consumed immediately (the
	// implementation may copy them into its own batch), so the caller's buffer
	// is free after the call.
	virtual void submitTriangles(const WorldVertex* verts, int count) = 0;

	// Legacy TinyGL software depth fog (ADR 0049): fogColorARGB = tinyGL->fogColor,
	// fogMin / fogRange = tinyGL->fogMin / fogRange, verbatim ints. Alpha 0 = fog off
	// (src/Render.cpp:1857-1861). Applies to every render mode, before the colour
	// modulation; the sky draws at the fixed legacy depth 2047. Call outside a scene.
	virtual void setFog(int fogColorARGB, int fogMin, int fogRange) = 0;

	// The sky band: a viewport-filling quad with u in [-0.5+uOffset, 0.5+uOffset]
	// and v in [0,1] (legacy gles::DrawSkyMap; World3D.cpp:629-646). uOffset is
	// viewYaw/256. Independent of beginScene/endScene.
	virtual void drawSky(TextureId sky, float uOffset) = 0;

	// A tile-301 sky wall (legacy src/GLES.cpp:325-343): a convex polygon of
	// count NDC (x,y) pairs, already near-clipped and divided by w, textured
	// with the drawSky recipe so it shows exactly the backdrop. Inside a scene
	// it keeps its painter position (the pending batch is drawn first).
	// count <= kMaxSkyPolygonVerts. Fogged at the fixed sky depth like drawSky
	// (src/Render.cpp:2133).
	static constexpr int kMaxSkyPolygonVerts = 10;
	virtual void drawSkyPolygon(TextureId sky, const float* ndcXY, int count, float uOffset) = 0;

	virtual void flush() = 0;
};

} // namespace newcore

#endif // NEW_RENDER_API_SCENE3D_H
