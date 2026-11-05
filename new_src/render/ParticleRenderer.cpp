#include "render/ParticleRenderer.h"

#include <algorithm>

#include "domain/game/ParticleSystem.h"
#include "render/Camera3D.h"
#include "render/ColumnOcclusion.h"
#include "render/Graphics2D.h"
#include "render/api/Texture.h"

namespace newcore {

namespace {

// TinyGL viewport (1,1,478,248) from viewRect {0,20,480,250} inset 1
// (src/TinyGL.cpp:141-144, :149-167, src/Canvas.cpp:124-127).
constexpr int kXScale = 478 << 2;
constexpr int kXBias = ((1 + 239) << 3) - 4;
constexpr int kYScale = 248 << 2;
constexpr int kYBias = ((1 + 124) << 3) - 4;
// TinyGL raster -> canvas: the world band is canvas (1,7,478,248) (ADR 0009, ADR 0047 §3).
constexpr int kOffX = 0, kOffY = 6;
constexpr int kBandX = 1, kBandY = 7, kBandW = 478, kBandH = 248;
constexpr int kRotationSequence[4] = {0, 4, 2, 6};   // src/ParticleSystem.h:44

struct V { int x, y, z, w; };

// transform3DVerts (src/TinyGL.cpp:261-276).
V transform(const int* m, int x, int y, int z) {
	V v;
	v.x = (int)(((int64_t)x * m[0] + (int64_t)y * m[4] + (int64_t)z * m[8]) >> 14) + m[12];
	v.y = (int)(((int64_t)x * m[1] + (int64_t)y * m[5] + (int64_t)z * m[9]) >> 14) + m[13];
	v.z = (int)(((int64_t)x * m[2] + (int64_t)y * m[6] + (int64_t)z * m[10]) >> 14) + m[14];
	v.w = (int)(((int64_t)x * m[3] + (int64_t)y * m[7] + (int64_t)z * m[11]) >> 14) + m[15];
	return v;
}

// projectVerts (src/TinyGL.cpp:516-524).
void project(V& v) {
	v.x = kXBias + (int)((int64_t)v.x * kXScale / v.w);
	v.y = kYBias + (int)((int64_t)v.y * kYScale / v.w);
	v.z = 0x7FFFFF / v.w;
}

// Render::convertToGrayscale (src/Render.cpp:2692-2696).
uint32_t toGray(uint32_t c) {
	const int r = (int)((c >> 16) & 0xFF), gr = (int)((c >> 8) & 0xFF), b = (int)(c & 0xFF);
	const int n2 = b + ((r - b) >> 1);
	const int n3 = (n2 + ((gr - n2) >> 1)) >> 1;
	return 0xFF000000u | (uint32_t)n3 << 16 | (uint32_t)n3 << 8 | (uint32_t)n3;
}

} // namespace

void drawParticles(Graphics2D& g, ParticleSystem& ps, const Camera3D& cam,
                   const ColumnOcclusion& occl, int64_t gameTime, bool grayscale,
                   const Texture (&gibs)[3]) {
	const int* m = cam.mvpTglInt();
	const int32_t* sin = cam.sinTable();
	const int yaw = cam.viewYaw();
	bool clipped = false;   // an empty pool leaves the device clip untouched
	for (int i = 0; i < ParticleSystem::kNumEmitters; ++i) {
		int p = ps.next[i];
		if (p == ParticleSystem::kEnd) continue;
		const ParticleSystem::Emitter& e = ps.emitters[i];
		const int t = (int)(((gameTime - e.startTime) << 10) / 1000);
		const int64_t t2 = (int64_t)t * t >> 10;
		const int rx = 64 * sin[yaw & 0x3FF] >> 12;
		const int ry = 64 * sin[(yaw + 256) & 0x3FF] >> 12;
		const int x0 = e.pos[0] + rx, y0 = e.pos[1] + ry, z0 = e.pos[2] + 1024;
		V v0 = transform(m, x0, y0, z0);
		V v1 = transform(m, x0, y0, z0 - 2048);
		V v2 = transform(m, x0 - 2 * rx, y0 - 2 * ry, z0);
		if (v0.w + v0.z < 0 || v1.w + v1.z < 0 || v2.w + v2.z < 0) continue;   // near clip (:56-58)
		project(v0);
		project(v1);
		project(v2);
		// Whole-emitter column cull (:60-68).
		const int depth = 0x7FFFFF / v0.z;
		const int col = ((v0.x >> 3) + (v2.x >> 3)) >> 1;
		if (col < 0 || col >= ColumnOcclusion::kColumns || depth > occl.column(col)) continue;
		const int w8 = v1.x - v2.x, h8 = v1.y - v0.y;   // 1/8 px
		int left = v2.x >> 3, top = v0.y >> 3, bw = w8 >> 3, bh = h8 >> 3;   // :69-97
		if (left < 0) { bw += left; left = 0; }
		if (top < 0) { bh += top; top = 0; }
		if (left + bw > 480) bw = 480 - left;
		if (top + bh > 250) bh = 250 - top;
		if (bw < 0 || bh < 0) continue;
		const int cx0 = std::max(left + kOffX, kBandX), cy0 = std::max(top + kOffY, kBandY);
		const int cx1 = std::min(left + kOffX + bw, kBandX + kBandW);
		const int cy1 = std::min(top + kOffY + bh, kBandY + kBandH);
		g.setClip(cx0, cy0, cx1 - cx0, cy1 - cy0);
		clipped = true;
		const int cx = (v1.x + v2.x) >> 4, cy = (v0.y + v1.y) >> 4;   // :98-99
		// Familiar grayscale without write-back (ADR 0047 §4a); gibs keep their colors.
		const uint32_t c = grayscale ? toGray(e.color) : e.color;
		const uint8_t cr = (uint8_t)(c >> 16), cg = (uint8_t)(c >> 8), cb = (uint8_t)c;
		int prev = i;
		while (p != ParticleSystem::kEnd) {
			const int s = ps.size[p];
			const int64_t dx = ((int64_t)ps.startX[p] << 10) + (int64_t)ps.velX[p] * t;
			const int64_t dy = ((int64_t)ps.startY[p] << 10) - ((int64_t)ps.velY[p] * t + t2 * e.gravity);
			if (dx < -65536 || dx > 65536 || dy < -65536 || dy > 65536) {
				ps.freeAfter(prev);   // :110-112; prev stays, p is its successor
			} else {
				const int px = cx + (int)(dx * w8 >> 20) + kOffX;
				const int py = cy + (int)(dy * h8 >> 20) + kOffY;
				if (!(s & 8)) {
					g.fillCircle(px, py, (s & 7) * std::min(w8, 1024) >> 11, cr, cg, cb);   // :117-119
				} else {
					const int scale = std::min((w8 * 3) >> 11, 3);   // :121-130
					if (scale >= 1 && e.gibType < 3) {
						int rot = (((p << 6) + t) >> 8) & 3;
						if (ps.velX[p] < 0) rot = 3 - rot;
						g.drawRegion(gibs[e.gibType], (3 - scale) * 48, (s & 7) * 48, 48, 48, px, py,
							Graphics2D::kAnchorHCenter | Graphics2D::kAnchorVCenter, kRotationSequence[rot]);
					}
				}
				prev = p;
			}
			p = ps.next[prev];
		}
	}
	if (clipped) g.clearClip();
}

} // namespace newcore
