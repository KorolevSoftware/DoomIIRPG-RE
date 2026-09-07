#ifndef NEW_RENDER_API_DRAW2D_H
#define NEW_RENDER_API_DRAW2D_H

#include "render/api/TextureId.h"

namespace newcore {

struct SrcRect { int x, y, w, h; };  // texels
struct DstRect { int x, y, w, h; };  // canvas pixels (480x320, origin top-left)
struct ColorF  { float r = 1.f, g = 1.f, b = 1.f, a = 1.f; };

// The 2D output device (ADR 0020). Derived from the actual call inventory of
// Graphics2D, Ui, Font, Hud*, Menu*, Dialog* and ViewWeapon: nothing else is
// used by anybody.
class Draw2D {
public:
	virtual ~Draw2D() = default;

	// Textured quad. rotateMode 0..8 exactly as legacy Image::DrawTexture
	// (0=none, 1=90, 2=180, 3=270, 4=mirror, 5..7=mirror+rotate, 8=flip V).
	// color multiplies the sampled texel (vertex color / GL_MODULATE analog).
	virtual void drawQuad(TextureId tex, const SrcRect& src, const DstRect& dst,
		int rotateMode, const ColorF& color) = 0;

	// Solid quad.
	virtual void fillQuad(const DstRect& dst, const ColorF& color) = 0;

	// Legacy RENDER_* value (0..13, render/api/RenderModes.h). Implementations
	// flush pending work on change.
	virtual void setRenderMode(int renderMode) = 0;

	// Clip in canvas coordinates; w<=0 or h<=0 clips everything away (empty
	// rect, NOT "no clip"). One level only: nesting is Ui::pushClip's job.
	virtual void setClipCanvas(int x, int y, int w, int h) = 0;
	virtual void clearClip() = 0;

	// Rasterizes everything pending. Called by the backend around viewport
	// changes, and by Graphics2D never.
	virtual void flush() = 0;
};

} // namespace newcore

#endif // NEW_RENDER_API_DRAW2D_H
