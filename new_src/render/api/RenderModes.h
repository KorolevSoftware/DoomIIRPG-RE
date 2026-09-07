#ifndef NEW_RENDER_API_RENDERMODES_H
#define NEW_RENDER_API_RENDERMODES_H

namespace newcore {

// Neutral blend factors: the only blend vocabulary any interface speaks.
// Each backend maps them to its own enums (GL: glBlendFunc, SDL: SDL_BlendMode).
enum class BlendFactor { Zero, One, SrcAlpha, OneMinusSrcAlpha, OneMinusSrcColor };

struct RenderModeRow {
	BlendFactor src, dst; // blend equation factors
	float mod[4];         // legacy glColor4f primary color under GL_MODULATE
	bool fog;             // legacy fogMode == 2
};

constexpr int kRenderModeCount = 14; // Render::RENDER_MAX, src/Render.h:31

// One row per legacy Render::RENDER_* value.
extern const RenderModeRow kRenderModes[kRenderModeCount];

// True for the modes that have no legacy GL case (default: assert(0),
// src/GLES.cpp:702-705) or that need an analog the rewrite does not have.
bool renderModeNeedsWarning(int mode);

// Clamps out-of-range values to 0 (RENDER_NORMAL) and logs each distinct
// offender once. Returns the row index to use.
int clampRenderMode(int mode);

// Named values used by callers.
constexpr int kRenderNormal = 0;
constexpr int kRenderBlend25 = 1, kRenderBlend50 = 2, kRenderBlend75 = 12;
constexpr int kRenderAdd = 3, kRenderAdd75 = 4, kRenderAdd50 = 5, kRenderAdd25 = 6;
constexpr int kRenderSub = 7, kRenderNone = 10;

} // namespace newcore

#endif // NEW_RENDER_API_RENDERMODES_H
