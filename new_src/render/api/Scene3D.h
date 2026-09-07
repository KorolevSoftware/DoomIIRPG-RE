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

	// count must be a multiple of 3. Triangles are consumed immediately (the
	// implementation may copy them into its own batch), so the caller's buffer
	// is free after the call.
	virtual void submitTriangles(const WorldVertex* verts, int count) = 0;

	// Linear eye-space fog request. start/end are in the same units as the view
	// matrix output; rgba is the fog color, alpha unused for blending
	// (World3D::setFog converts fogMin/fogRange). Per-mode fog on/off comes
	// from the RenderModes table, not from here.
	virtual void setFog(bool enabled, float start, float end, const float rgba[4]) = 0;

	// The sky band: a viewport-filling quad with u in [-0.5+uOffset, 0.5+uOffset]
	// and v in [0,1] (legacy gles::DrawSkyMap; World3D.cpp:629-646). uOffset is
	// viewYaw/256. Independent of beginScene/endScene.
	virtual void drawSky(TextureId sky, float uOffset) = 0;

	virtual void flush() = 0;
};

} // namespace newcore

#endif // NEW_RENDER_API_SCENE3D_H
