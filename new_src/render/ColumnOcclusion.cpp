#include "render/ColumnOcclusion.h"

#include <utility>

namespace newcore {

void ColumnOcclusion::begin(const int* mvp2D, int viewX, int viewY) {
	for (int i = 0; i < 16; ++i) mvp2D_[i] = mvp2D[i];
	viewX_ = viewX;
	viewY_ = viewY;
	for (int i = 0; i < kColumns; ++i) columnScale_[i] = kColumnInit;
}

bool ColumnOcclusion::project(int ax, int ay, int bx, int by, V& a, V& b) const {
	// transform2DVerts (src/TinyGL.cpp:279-292).
	const int* m = mvp2D_;
	a.x = ((ax * m[0] + ay * m[4]) >> 14) + m[12];
	a.z = ((ax * m[2] + ay * m[6]) >> 14) + m[14];
	a.w = ((ax * m[3] + ay * m[7]) >> 14) + m[15];
	b.x = ((bx * m[0] + by * m[4]) >> 14) + m[12];
	b.z = ((bx * m[2] + by * m[6]) >> 14) + m[14];
	b.w = ((bx * m[3] + by * m[7]) >> 14) + m[15];

	// clipLine (src/TinyGL.cpp:459-514), planes w+x, w-x, w+z.
	for (int i = 0; i < 3; ++i) {
		int n, n2;
		if (i == 0) { n = a.w + a.x; n2 = b.w + b.x; }
		else if (i == 1) { n = a.w - a.x; n2 = b.w - b.x; }
		else { n = a.w + a.z; n2 = b.w + b.z; }
		const int n3 = ((n < 0) ? 1 : 0) | ((n2 < 0) ? 2 : 0);
		if (n3 == 3) return false;
		if (n3 == 1) {
			const int t = (n2 << 14) / (n2 - n);
			a.x = b.x + (((a.x - b.x) * t) >> 14);
			a.z = b.z + (((a.z - b.z) * t) >> 14);
			a.w = b.w + (((a.w - b.w) * t) >> 14);
		} else if (n3 == 2) {
			const int t = (n << 14) / (n - n2);
			b.x = a.x + (((b.x - a.x) * t) >> 14);
			b.z = a.z + (((b.z - a.z) * t) >> 14);
			b.w = a.w + (((b.w - a.w) * t) >> 14);
		}
	}

	// projectVerts x/z (src/TinyGL.cpp:516-525).
	a.x = kXBias + ((a.x * kXScale) / a.w);
	a.z = 0x7fffff / a.w;
	b.x = kXBias + ((b.x * kXScale) / b.w);
	b.z = 0x7fffff / b.w;
	return true;
}

void ColumnOcclusion::occludeLine(int x0, int y0, int x1, int y1, bool sortX) {
	V a, b;
	if (!project(x0, y0, x1, y1, a, b)) return;
	if (sortX && a.x > b.x) std::swap(a, b);
	// occludeClippedLine (src/TinyGL.cpp:719-743).
	int begX = (a.x + 7) >> 3;
	int endX = (b.x + 7) >> 3;
	if (begX < 0) begX = 0;
	if (endX > kColumns) endX = kColumns;
	if (endX <= begX) return;
	const int n = (b.z - a.z) / (endX - begX);
	int n2 = a.z + (n * ((begX << 3) - a.x) >> 3);
	for (; begX < endX; ++begX, n2 += n) {
		const int n3 = 0x7fffff / n2;
		if (n3 < columnScale_[begX]) columnScale_[begX] = n3;
	}
}

bool ColumnOcclusion::cullBox(int n, int n2, int n3, int n4) const {
	if (viewX_ >= n - kCullExtra && viewX_ <= n3 + kCullExtra &&
	    viewY_ >= n2 - kCullExtra && viewY_ <= n4 + kCullExtra) {
		return false;
	}
	// Silhouette edge, src/Render.cpp:759-815.
	int p0x, p0y, p1x, p1y;
	if (viewX_ < n) {
		if (viewY_ < n2) { p0x = n3; p0y = n2; p1x = n; p1y = n4; }
		else if (viewY_ < n4) { p0x = n; p0y = n2; p1x = n; p1y = n4; }
		else { p0x = n; p0y = n2; p1x = n3; p1y = n4; }
	} else if (viewX_ < n3) {
		if (viewY_ < n2) { p0x = n3; p0y = n2; p1x = n; p1y = n2; }
		else {
			if (viewY_ < n4) return false;
			p0x = n; p0y = n4; p1x = n3; p1y = n4;
		}
	} else if (viewY_ < n2) { p0x = n3; p0y = n4; p1x = n; p1y = n2; }
	else if (viewY_ < n4) { p0x = n3; p0y = n4; p1x = n3; p1y = n2; }
	else { p0x = n; p0y = n4; p1x = n3; p1y = n2; }

	V a, b;
	if (!project(p0x, p0y, p1x, p1y, a, b)) return true;
	// clippedLineVisCheck with b == true (src/TinyGL.cpp:689-710), no swap.
	int begX = (a.x + 7) >> 3;
	int endX = (b.x + 7) >> 3;
	if (begX < 0) begX = 0;
	if (endX > kColumns) endX = kColumns;
	if (endX <= begX) return true;
	const int k = (b.z - a.z) / (endX - begX);
	for (int z = a.z + (k * ((begX << 3) - a.x) >> 3); begX < endX; ++begX, z += k) {
		if (0x7fffff / z < columnScale_[begX]) return false;
	}
	return true;
}

} // namespace newcore
