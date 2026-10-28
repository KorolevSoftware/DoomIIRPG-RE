#ifndef NEW_RENDER_COLUMNOCCLUSION_H
#define NEW_RENDER_COLUMNOCCLUSION_H

#include <climits>

namespace newcore {

// CPU port of the 2D column-occlusion cull of legacy TinyGL/Render (ADR 0031).
// Bookkeeping only: never feeds the GPU draw path.
class ColumnOcclusion {
public:
	static constexpr int kColumns = 480;      // TinyGL::screenWidth (src/App.cpp:111, src/Render.cpp:156)
	static constexpr int kXScale = 478 << 2;  // viewportXScale (src/TinyGL.cpp:141, :166)
	static constexpr int kXBias = ((1 + 478 / 2) << 3) - 4; // viewportXBias (src/TinyGL.cpp:143)
	static constexpr int kCullExtra = 256 + 16;             // TinyGL::CULL_EXTRA (src/TinyGL.h:37)
	static constexpr int kColumnInit = INT_MAX;             // COLUMN_SCALE_INIT (src/TinyGL.h:39)

	// New frame: latch the camera, reset every column (src/Render.cpp:2315-2317).
	void begin(const int* mvp2D, int viewX, int viewY);
	// occludeClippedLine for a world-space segment (world units = canvas << 4).
	// sortX: swap the projected ends so x0 <= x1 (occludeSpriteLine, src/Render.cpp:708-713);
	// false for map lines (drawNodeLines, src/Render.cpp:735).
	void occludeLine(int x0, int y0, int x1, int y1, bool sortX);
	// Render::cullBoundingBox(n, n2, n3, n4, true) (src/Render.cpp:750-823): true = culled.
	bool cullBox(int x1, int y1, int x2, int y2) const;

private:
	struct V { int x, z, w; };
	// transform2DVerts + clipLine + projectVerts for two points; false = clipped away.
	bool project(int ax, int ay, int bx, int by, V& a, V& b) const;

	int mvp2D_[16] = {};
	int viewX_ = 0, viewY_ = 0;
	int columnScale_[kColumns];
};

} // namespace newcore

#endif // NEW_RENDER_COLUMNOCCLUSION_H
