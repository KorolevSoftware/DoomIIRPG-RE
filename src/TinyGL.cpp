#include <stdexcept>

#include "SDLGL.h"
#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "Graphics.h"
#include "GLES.h"
#include "Render.h"
#include "TinyGL.h"
#include "Span.h"


TinyGL::TinyGL() {
	std::memset(this, 0, sizeof(TinyGL));
}

TinyGL::~TinyGL() {
}

bool TinyGL::startup(int screenWidth, int screenHeight) {
	Applet* app = CAppContainer::getInstance()->app;
	Canvas* canvas = app->canvas;

	printf("TinyGL::startup, w [%d], h [%d]\n", screenWidth, screenHeight);

	this->scratchPalette = new uint16_t[256];
	this->screenWidth  = screenWidth;
	this->screenHeight = screenHeight;
	this->columnScale  = new int[screenWidth];

	this->setViewport(canvas->viewRect[0], canvas->viewRect[1], canvas->viewRect[2], canvas->viewRect[3]);
	this->setView(0, 0, 0, 0, 0, 0, 290, 290);
	this->fogMin   = 32752;
	this->fogRange = 1;
	this->fogColor = 0;
	this->countBackFace = 0;
	this->countDrawn    = 0;
	this->spanPixels    = 0;
	this->spanCalls     = 0;
	this->zeroDT        = 0;
	this->zeroDS        = 0;
	this->colorBuffer   = 0; // [GEC]
	return true;
}

uint16_t* TinyGL::getFogPalette(int invZ) {
	int level = (((0x7FFFFF / (invZ >> 16)) - this->fogMin) << 4) / this->fogRange;
	level = (level & ~level >> 31) - 15;
	level = (level & level >> 31) + 15;
	return this->paletteBase[level];
}

void TinyGL::clearColorBuffer(int color) {
	Applet* app = CAppContainer::getInstance()->app;

	if (!app->render->_gles->ClearBuffer(color)) {
		this->colorBuffer = color; // [GEC]
	}
}

// Builds a view matrix from a camera position and Euler angles (yaw/pitch/roll).
// The sinTable uses a 1024-entry circle; +256 = +90 degrees = cosine.
// All values are in 2.14 fixed-point (MATRIX_ONE = 16384 = 1.0).
void TinyGL::buildViewMatrix(int x, int y, int z, int yaw, int pitch, int roll, int* matrix) {
	Applet* app = CAppContainer::getInstance()->app;
	int* sinTable = app->render->sinTable;

	// Trig components. sinTable[a+512] = sin(a+180°) = -sin(a), but the >>2
	// converts from the table's scale to 2.14 fixed-point.
	int sy = sinTable[yaw   + 512 & 0x3FF] >> 2;  // sin(yaw)   (negated via +512)
	int cy = sinTable[yaw   + 256 & 0x3FF] >> 2;  // cos(yaw)
	int sp = sinTable[pitch + 512 & 0x3FF] >> 2;  // sin(pitch)
	int cp = sinTable[pitch + 256 & 0x3FF] >> 2;  // cos(pitch)
	int sr = sinTable[roll         & 0x3FF] >> 2;  // sin(roll)
	int cr = sinTable[roll  + 256  & 0x3FF] >> 2;  // cos(roll)

	// Pre-compute the two mixed terms used in multiple matrix cells.
	int srsp = sr * sp >> 14;  // sin(roll)*sin(pitch)
	int crsp = cr * sp >> 14;  // cos(roll)*sin(pitch)

	// 3×3 rotation sub-matrix (column-major layout).
	// Row 0: right vector
	matrix[0] = srsp * cy + cr * -sy >> 14;
	matrix[4] = srsp * sy + cr *  cy >> 14;
	matrix[8] = sr * cp >> 14;
	// Row 1: up vector (negated in camera convention)
	matrix[1] = -(crsp * cy + -sr * -sy) >> 14;
	matrix[5] = -(crsp * sy + -sr *  cy) >> 14;
	matrix[9] = -(cr * cp) >> 14;
	// Row 2: forward vector
	matrix[2]  = -(cp * cy) >> 14;
	matrix[6]  = -(cp * sy) >> 14;
	matrix[10] = sp;  // simplified from -(-sp)

	// Translation: dot each row with the world-space camera position.
	for (int i = 0; i < 3; ++i) {
		matrix[12 + i] = -(x * matrix[i] + y * matrix[4 + i] + z * matrix[8 + i]) >> 14;
	}

	matrix[3]  = 0;
	matrix[7]  = 0;
	matrix[11] = 0;
	matrix[15] = 16384;  // w = 1.0 in 2.14 fixed-point
}

// Builds a perspective projection matrix from field-of-view and aspect ratio.
// fov and aspect are in the same angular unit as the sin table (0..1023 per circle).
void TinyGL::buildProjectionMatrix(int fov, int aspect, int* matrix) {
	Applet* app = CAppContainer::getInstance()->app;
	int* sinTable = app->render->sinTable;

	int halfAspect = aspect >> 1;
	int fovScale   = (fov << 14) / aspect;          // horizontal scale factor
	int sinH = sinTable[halfAspect        & 0x3FF] >> 2;  // sin(aspect/2)
	int cosH = sinTable[halfAspect + 256  & 0x3FF] >> 2;  // cos(aspect/2)

	matrix[0]  = (cosH << 14) / (fovScale * sinH >> 14);  // X scale (accounts for FOV + aspect)
	matrix[8]  = (matrix[4] = 0);
	matrix[1]  = (matrix[12] = 0);
	matrix[5]  = (cosH << 14) / sinH;                     // Y scale
	matrix[13] = (matrix[9]  = 0);
	matrix[6]  = (matrix[2]  = 0);
	matrix[10] = -TinyGL::MATRIX_ONE;
	matrix[14] = -(2 * TinyGL::NEAR_CLIP);
	matrix[7]  = (matrix[3]  = 0);
	matrix[11] = -TinyGL::MATRIX_ONE;
	matrix[15] = 0;
}

void TinyGL::multMatrix(int* matrix1, int* matrix2, int* destMtx) {
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			destMtx[i * 4 + j] = (  matrix1[i * 4 + 0] * matrix2[0  + j]
			                       + matrix1[i * 4 + 1] * matrix2[4  + j]
			                       + matrix1[i * 4 + 2] * matrix2[8  + j]
			                       + matrix1[i * 4 + 3] * matrix2[12 + j]) >> 14;
		}
	}
}

void TinyGL::_setViewport(int x, int y, int w, int h) {
	this->viewportX      = x;
	this->viewportY      = y;
	this->viewportWidth  = w;
	this->viewportHeight = h;
	this->viewportX2     = x + w;
	this->viewportY2     = y + h;

	// Clamp boundaries in sub-pixel units (pixel * SCREEN_ONE).
	this->viewportClampX1 = x << TinyGL::SCREEN_SHIFT;
	this->viewportClampY1 = y << TinyGL::SCREEN_SHIFT;
	this->viewportClampX2 = (this->viewportX2 << TinyGL::SCREEN_SHIFT) + TinyGL::SCREEN_ONE - 1;
	this->viewportClampY2 = (this->viewportY2 << TinyGL::SCREEN_SHIFT) + TinyGL::SCREEN_ONE - 1;

	// Viewport transform parameters for perspective divide.
	this->viewportXScale = w << 2;
	this->viewportYScale = h << 2;
	this->viewportXBias  = ((x + w / 2) << 3) - 4;
	this->viewportYBias  = ((y + h / 2) << 3) - 4;
	this->viewportZScale = (TinyGL::UNIT_SCALE / 2);
	this->viewportZBias  = (TinyGL::UNIT_SCALE / 2);
}

void TinyGL::setViewport(int x, int y, int w, int h) {
	Applet* app = CAppContainer::getInstance()->app;

	int posX = x - app->canvas->viewRect[0];
	int posY = y - app->canvas->viewRect[1];

	if (!app->render->_gles->isInit) { // [GEC]
		posY = 3;
	}

	if (this->viewportX == posX && this->viewportY == posY &&
	    this->viewportWidth == w && this->viewportHeight == h) {
		return;
	}

	uint16_t* backBuff = (uint16_t*)app->backBuffer->pBmp;
	this->pixels = backBuff + (this->screenWidth * 3) + app->canvas->viewRect[0];

	this->_setViewport(posX + 1, posY + 1, w - 2, h - 2);

	if (app->render->_gles->isInit) { // [GEC]
		app->canvas->repaintFlags &= ~Canvas::REPAINT_VIEW3D;
	}
}

void TinyGL::resetViewPort() {
	Applet* app = CAppContainer::getInstance()->app;
	this->setViewport(app->canvas->viewRect[0], app->canvas->viewRect[1],
	                  app->canvas->viewRect[2], app->canvas->viewRect[3]);
}

void TinyGL::setView(int viewX, int viewY, int viewZ, int viewYaw, int viewPitch,
                     int viewRoll, int viewFov, int viewAspect) {
	Applet* app = CAppContainer::getInstance()->app;

	this->viewX     = viewX;
	this->viewY     = viewY;
	this->viewZ     = viewZ;
	this->viewPitch = viewPitch & 0x3FF;
	this->viewYaw   = viewYaw   & 0x3FF;

	this->buildViewMatrix(viewX, viewY, viewZ, this->viewYaw, this->viewPitch, viewRoll, this->view);
	this->buildViewMatrix(viewX, viewY, 0,     this->viewYaw, 0,              0,        this->view2D);
	this->buildProjectionMatrix(viewFov, viewAspect, this->projection);
	this->multMatrix(this->view, this->projection, this->mvp);

	app->render->_gles->BeginFrame(this->viewportX, this->viewportY,
	                               this->viewportWidth, this->viewportHeight,
	                               this->view, this->projection);

	if (this->viewPitch > 512) {
		this->viewPitch -= 1024;
	}

	// The 2D matrix uses a slightly wider fov to cover screen tilt from pitch.
	this->buildProjectionMatrix(viewFov + std::abs(this->viewPitch), viewAspect, this->projection);
	this->multMatrix(this->view2D, this->projection, this->mvp2D);
}

// Moves a pre-transformed vertex along view-space axes.
// fwd: depth (negated before use, so positive = forward away from camera)
// right: strafe along view X
// up: vertical (negated, so positive = up in world)
void TinyGL::viewMtxMove(TGLVert* vert, int fwd, int right, int up) {
	if (fwd != 0) {
		fwd = -fwd;
		vert->x += this->view[2]  * fwd >> 14;
		vert->y += this->view[6]  * fwd >> 14;
		vert->z += this->view[10] * fwd >> 14;
	}
	if (right != 0) {
		vert->x += this->view[0] * right >> 14;
		vert->y += this->view[4] * right >> 14;
		vert->z += this->view[8] * right >> 14;
	}
	if (up != 0) {
		up = -up;
		vert->x += this->view[1] * up >> 14;
		vert->y += this->view[5] * up >> 14;
		vert->z += this->view[9] * up >> 14;
	}
}

void TinyGL::drawModelVerts(TGLVert* verts, int count) {
	Applet* app = CAppContainer::getInstance()->app;

	if ((app->render->renderMode & 0x1) == 0x0) {
		return;
	}

	if (!app->render->_gles->DrawModelVerts(verts, count)) {
		if (this->faceCull != TinyGL::CULL_NONE) {
			// Back-face cull: compute dot(viewDir, faceNormal).
			// Two edge vectors from vertex 0.
			TGLVert* v0 = &verts[0];
			TGLVert* v1 = &verts[1];
			TGLVert* v2 = &verts[2];
			int e1x = v1->x - v0->x;
			int e1y = v1->y - v0->y;
			int e1z = v1->z - v0->z;
			int e2x = v2->x - v0->x;
			int e2y = v2->y - v0->y;
			int e2z = v2->z - v0->z;
			// facing = dot(camera - v0, cross(e1, e2))
			int facing = (this->viewX - v0->x) * (e1y * e2z - e1z * e2y >> 14)
			           + (this->viewY - v0->y) * (e1z * e2x - e1x * e2z >> 14)
			           + (this->viewZ - v0->z) * (e1x * e2y - e1y * e2x >> 14);
			if ((this->faceCull == TinyGL::CULL_CCW && facing < 0) ||
			    (this->faceCull == TinyGL::CULL_CW  && facing > 0)) {
				++this->c_backFacedPolys;
				return;
			}
		}
		++this->c_frontFacedPolys;
		TGLVert* clipped = this->transform3DVerts(verts, count);
		if ((app->render->renderMode & 0x2) == 0x0) {
			return;
		}
		if (count == 4) {
			this->ClipQuad(&clipped[0], &clipped[1], &clipped[2], &clipped[3]);
		}
		else {
			this->ClipPolygon(0, count);
		}
	}
}

TGLVert* TinyGL::transform3DVerts(TGLVert* verts, int count) {
	int* mvp = this->mvp;
	for (int i = 0; i < count; ++i) {
		TGLVert* src = &verts[i];
		TGLVert* dst = &this->cv[i];
		int x = src->x, y = src->y, z = src->z;
		dst->x = (x * mvp[0] + y * mvp[4] + z * mvp[8]  >> 14) + mvp[12];
		dst->y = (x * mvp[1] + y * mvp[5] + z * mvp[9]  >> 14) + mvp[13];
		dst->z = (x * mvp[2] + y * mvp[6] + z * mvp[10] >> 14) + mvp[14];
		dst->w = (x * mvp[3] + y * mvp[7] + z * mvp[11] >> 14) + mvp[15];
		dst->s = src->s;
		dst->t = src->t;
	}
	return this->cv;
}

TGLVert* TinyGL::transform2DVerts(TGLVert* verts, int count) {
	int* mvp2D = this->mvp2D;
	for (int i = 0; i < count; ++i) {
		TGLVert* src = &verts[i];
		TGLVert* dst = &this->cv[i];
		int x = src->x, y = src->y;
		dst->x = (x * mvp2D[0] + y * mvp2D[4] >> 14) + mvp2D[12];
		dst->z = (x * mvp2D[2] + y * mvp2D[6] >> 14) + mvp2D[14];
		dst->w = (x * mvp2D[3] + y * mvp2D[7] >> 14) + mvp2D[15];
		dst->s = src->s;
		dst->t = src->t;
	}
	return this->cv;
}

// Fast path for quads: test each clip plane without building an output polygon.
// If all four vertices pass every plane, rasterize directly (no allocation needed).
// If any vertex is outside, fall through to the general ClipPolygon.
void TinyGL::ClipQuad(TGLVert* v0, TGLVert* v1, TGLVert* v2, TGLVert* v3) {
	++this->c_totalQuad;
	int plane = 0;
	while (plane < 5) {
		int d0 = 0, d1 = 0, d2 = 0, d3 = 0;
		// Clip-space plane distances: w+x>=0 (left), w-x>=0 (right),
		// w+y>=0 (bottom), w-y>=0 (top), w+z>=0 (near).
		switch (plane) {
		default:
		case 0: d0 = v0->w + v0->x; d1 = v1->w + v1->x; d2 = v2->w + v2->x; d3 = v3->w + v3->x; break;
		case 1: d0 = v0->w - v0->x; d1 = v1->w - v1->x; d2 = v2->w - v2->x; d3 = v3->w - v3->x; break;
		case 2: d0 = v0->w + v0->y; d1 = v1->w + v1->y; d2 = v2->w + v2->y; d3 = v3->w + v3->y; break;
		case 3: d0 = v0->w - v0->y; d1 = v1->w - v1->y; d2 = v2->w - v2->y; d3 = v3->w - v3->y; break;
		case 4: d0 = v0->w + v0->z; d1 = v1->w + v1->z; d2 = v2->w + v2->z; d3 = v3->w + v3->z; break;
		}
		int outcode = ((d0 < 0) ? 1 : 0) | ((d1 < 0) ? 2 : 0)
		            | ((d2 < 0) ? 4 : 0) | ((d3 < 0) ? 8 : 0);
		if (outcode == 0) {
			++plane;
		} else {
			if (outcode == 15) {
				++this->c_rejectedQuad;
				return;
			}
			++this->c_clippedQuad;
			this->ClipPolygon(plane, 4);
			return;
		}
	}
	++this->c_unclippedQuad;
	this->RasterizeConvexPolygon(4);
}

// Sutherland-Hodgman polygon clipping against 5 half-spaces (left/right/bottom/top/near).
// Works in-place on cv[]. For each plane:
//   1. Compute clip distance for every vertex.
//   2. Insert intersection vertices on edges that straddle the plane.
//   3. Remove vertices with negative clip distance (outside the plane).
void TinyGL::ClipPolygon(int plane, int count) {
	while (plane < 5) {
		// Step 1: compute signed distance to the current clip plane for each vertex.
		switch (plane) {
		default:
		case 0: for (int vi = 0; vi < count; ++vi) this->cv[vi].clipDist = this->cv[vi].w + this->cv[vi].x; break;
		case 1: for (int vi = 0; vi < count; ++vi) this->cv[vi].clipDist = this->cv[vi].w - this->cv[vi].x; break;
		case 2: for (int vi = 0; vi < count; ++vi) this->cv[vi].clipDist = this->cv[vi].w + this->cv[vi].y; break;
		case 3: for (int vi = 0; vi < count; ++vi) this->cv[vi].clipDist = this->cv[vi].w - this->cv[vi].y; break;
		case 4: for (int vi = 0; vi < count; ++vi) this->cv[vi].clipDist = this->cv[vi].w + this->cv[vi].z; break;
		}

		// Step 2: insert intersection vertex on each edge that crosses the plane.
		for (int vi = 0; vi < count; ++vi) {
			int ni = vi + 1;
			if (ni == count) ni = 0;

			if ((this->cv[vi].clipDist < 0) != (this->cv[ni].clipDist < 0)) {
				// Make room at cv[vi+1] by shifting the tail one slot to the right.
				int shiftCount = count - 1 - vi;
				if (shiftCount > 0) {
					std::memmove(&this->cv[vi + 2], &this->cv[vi + 1], shiftCount * sizeof(TGLVert));
					++ni;
				}

				// Interpolate the new vertex at the plane boundary.
				TGLVert* newVert = &this->cv[vi + 1];
				newVert->clipDist = 0;

				TGLVert* inside;
				TGLVert* outside;
				if (this->cv[vi].clipDist < 0) {
					inside  = &this->cv[ni];
					outside = &this->cv[vi];
				} else {
					inside  = &this->cv[vi];
					outside = &this->cv[ni];
				}

				// t = inside.d / (inside.d - outside.d), in 16-bit fixed-point.
				int t = (inside->clipDist << 16) / (inside->clipDist - outside->clipDist);
				newVert->x = inside->x + ((outside->x - inside->x) * t >> 16);
				newVert->y = inside->y + ((outside->y - inside->y) * t >> 16);
				newVert->z = inside->z + ((outside->z - inside->z) * t >> 16);
				newVert->w = inside->w + ((outside->w - inside->w) * t >> 16);
				newVert->s = inside->s + ((outside->s - inside->s) * t >> 16);
				newVert->t = inside->t + ((outside->t - inside->t) * t >> 16);
				++count;
				++vi;  // skip the newly inserted vertex in the next iteration
			}
		}

		// Step 3: remove vertices that are outside the plane (clipDist < 0).
		for (int vi = 0; vi < count; ++vi) {
			if (this->cv[vi].clipDist < 0) {
				std::memcpy(&this->cv[vi], &this->cv[vi + 1], (count - 1 - vi) * sizeof(TGLVert));
				--vi;
				--count;
			}
		}

		if (count < 3) {
			return;
		}
		++plane;
	}
	this->RasterizeConvexPolygon(count);
}

// Clips a line segment (array[0]..array[1]) against 3 planes: left, right, near.
// Modifies the endpoints in-place. Returns false if the segment is fully outside.
bool TinyGL::clipLine(TGLVert* array) {
	TGLVert* a = &array[0];
	TGLVert* b = &array[1];
	for (int i = 0; i < 3; ++i) {
		int d0 = 0, d1 = 0;
		switch (i) {
		default:
		case 0: d0 = a->w + a->x; d1 = b->w + b->x; break;
		case 1: d0 = a->w - a->x; d1 = b->w - b->x; break;
		case 2: d0 = a->w + a->z; d1 = b->w + b->z; break;
		}
		int outcode = ((d0 < 0) ? 1 : 0) | ((d1 < 0) ? 2 : 0);
		if (outcode != 0) {
			if (outcode == 3) {
				return false;  // both endpoints outside
			}
			if (outcode == 1) {
				// endpoint a is outside — clip it toward b
				int t = (d1 << 14) / (d1 - d0);
				a->x = b->x + ((a->x - b->x) * t >> 14);
				a->y = b->y + ((a->y - b->y) * t >> 14);
				a->z = b->z + ((a->z - b->z) * t >> 14);
				a->w = b->w + ((a->w - b->w) * t >> 14);
				a->s = b->s + ((a->s - b->s) * t >> 14);
				a->t = b->t + ((a->t - b->t) * t >> 14);
			} else {
				// endpoint b is outside — clip it toward a
				int t = (d0 << 14) / (d0 - d1);
				b->x = a->x + ((b->x - a->x) * t >> 14);
				b->y = a->y + ((b->y - a->y) * t >> 14);
				b->z = a->z + ((b->z - a->z) * t >> 14);
				b->w = a->w + ((b->w - a->w) * t >> 14);
				b->s = a->s + ((b->s - a->s) * t >> 14);
				b->t = a->t + ((b->t - a->t) * t >> 14);
			}
		}
	}
	return true;
}

void TinyGL::projectVerts(TGLVert* array, int count) {
	for (int i = 0; i < count; i++) {
		TGLVert* v = &array[i];
		v->x = this->viewportXBias + ((v->x * this->viewportXScale) / v->w);
		v->y = this->viewportYBias + ((v->y * this->viewportYScale) / v->w);
		v->z = 0x7FFFFF / v->w;
		v->s *= v->z;
		v->t *= v->z;
	}
}

// Rasterizes a convex polygon stored in cv[0..count-1].
// Walks two edges simultaneously (left CCW, right CW) from the topmost vertex,
// emitting horizontal spans for each scanline between the two edges.
void TinyGL::RasterizeConvexPolygon(int count) {
	Applet* app = CAppContainer::getInstance()->app;

	if ((app->render->renderMode & 0x4) == 0x0) {
		return;
	}

	if (!app->render->_gles->RasterizeConvexPolygon(count, this->cv)) {
		// Perspective-divide and compute per-vertex 1/z and perspective-correct UV.
		for (int i = 0; i < count; ++i) {
			TGLVert* v = &this->cv[i];
			v->x = this->viewportXBias + ((v->x * this->viewportXScale) / v->w);
			v->y = this->viewportYBias + ((v->y * this->viewportYScale) / v->w);
			if (app->render->isSkyMap) {
				this->swapXY = false;
				// [GEC] adjusted for iOS consistency
				v->s = (132 << 4) + (app->render->skyMapX * 2) + v->x << (this->sShift - 8U & 0xFF);
				v->t = app->render->skyMapY + v->y << (this->tShift + 1U & 0xFF);
				v->z = 4096;
			} else {
				v->z  = 0x7FFFFF / v->w;   // 1/z scaled to fit in an int
				v->s *= v->z;               // s and t are now perspective-correct (s/w)
				v->t *= v->z;
			}
		}

		if ((app->render->renderMode & 0x8) == 0x0) {
			return;
		}

		// Optional XY swap for rendering rotated geometry.
		if (this->swapXY) {
			for (int j = 0; j < count; ++j) {
				int x = this->cv[j].x;
				this->cv[j].x = this->cv[j].y;
				this->cv[j].y = x;
			}
		}

		// Find the vertex with the smallest Y (topmost on screen).
		int topY    = this->cv[0].y;
		int leftIdx = 0;
		for (int k = 1; k < count; ++k) {
			if (this->cv[k].y < topY) {
				topY    = this->cv[k].y;
				leftIdx = k;
			}
		}

		int rightIdx = leftIdx;
		TGLVert* leftVert  = &this->cv[leftIdx];
		TGLVert* rightVert = &this->cv[rightIdx];

		// First scanline (in sub-pixel units, rounded up to pixel grid).
		int scanY = topY + 7 >> 3;

		TGLEdge* leftEdge  = &this->edges[0];  // walks CCW (index decreases)
		TGLEdge* rightEdge = &this->edges[1];  // walks CW  (index increases)
		leftEdge->stopY  = scanY;
		rightEdge->stopY = scanY;

		// When swapXY, spans step by screenWidth (column rendering);
		// otherwise step by 1 (normal row rendering).
		const int pixelStride = this->swapXY ? this->screenWidth : 1;

		const int startIdx = leftIdx;  // remember starting vertex to detect full traversal

		while (true) {
			// Advance the left edge walker (CCW, index decreases).
			if (scanY == leftEdge->stopY) {
				if (leftIdx == rightIdx && leftIdx != startIdx) {
					break;  // both walkers met at the bottom vertex
				}
				int prevLeftIdx = leftIdx;
				leftIdx = (leftIdx == 0) ? count - 1 : leftIdx - 1;
				TGLVert* prevLeft = leftVert;
				leftVert = &this->cv[leftIdx];
				leftEdge->setFromVerts(prevLeft, leftVert);
				(void)prevLeftIdx;
			}

			// Advance the right edge walker (CW, index increases).
			if (scanY == rightEdge->stopY) {
				if (leftIdx == rightIdx) {
					break;  // walkers converged before the left edge advanced
				}
				if (++rightIdx == count) rightIdx = 0;
				TGLVert* prevRight = rightVert;
				rightVert = &this->cv[rightIdx];
				rightEdge->setFromVerts(prevRight, rightVert);
			}

			int nextStop = (leftEdge->stopY < rightEdge->stopY) ? leftEdge->stopY : rightEdge->stopY;
			if (scanY > nextStop) {
				return;
			}

			while (scanY != nextStop) {
				// Sort edges by current X so spanLeft is always the screen-left edge.
				TGLEdge* spanLeft;
				TGLEdge* spanRight;
				if (rightEdge->fracX < leftEdge->fracX) {
					spanLeft  = rightEdge;
					spanRight = leftEdge;
				} else {
					spanLeft  = leftEdge;
					spanRight = rightEdge;
				}

				// Convert sub-pixel edge positions to pixel columns (round up).
				int startX = spanLeft->fracX  + 0x7FFFF >> 19;
				int endX   = spanRight->fracX + 0x7FFFF >> 19;

				if (endX > startX) {
					int pixelIdx = this->swapXY
					             ? startX * this->screenWidth + scanY
					             : scanY  * this->screenWidth + startX;

					// spanWidthFP: span width in INTERPOLATE_SHIFT fixed-point units.
					int spanWidthFP = spanRight->fracX - spanLeft->fracX >> 16;

					// Convert interpolated 1/z and perspective-correct UV at both edges.
					int leftInvZ = spanLeft->fracZ  >> 16;
					int leftS    = (spanLeft->fracS  / leftInvZ)  << 16;
					int leftT    = (spanLeft->fracT  / leftInvZ)  << 16;
					int rightInvZ = spanRight->fracZ >> 16;
					int rightS    = (spanRight->fracS / rightInvZ) << 16;
					int rightT    = (spanRight->fracT / rightInvZ) << 16;

					if (spanWidthFP == 0) {
						// Degenerate 1-pixel wide span: use right-edge UV directly.
						this->spanPalette = this->getFogPalette(
						    app->render->isSkyMap ? 0x10000000 : spanRight->fracZ);
						this->span->Span->DT(&this->pixels[pixelIdx],
						    rightS, rightT, pixelStride, 0, 0, endX - startX, this);
					} else {
						// prestep: sub-pixel offset so UV starts at the pixel centre.
						int prestep = (startX << 3) - (spanLeft->fracX >> 16);
						int dInvZ   = (spanRight->fracZ - spanLeft->fracZ) / spanWidthFP;
						int dS      = (rightS - leftS) / spanWidthFP;
						int dT      = (rightT - leftT) / spanWidthFP;

						this->spanPalette = this->getFogPalette(
						    app->render->isSkyMap ? 0x10000000 : spanLeft->fracZ);

						if (dS == 0) {
							// S is constant across span — use DT variant.
							this->span->Span->DT(&this->pixels[pixelIdx],
							    leftS, leftT + prestep * dT, pixelStride,
							    0, dT << 3, endX - startX, this);
						} else if (dT == 0) {
							// T is constant across span — use DS variant.
							this->span->Span->DS(&this->pixels[pixelIdx],
							    leftS + dS * prestep, leftT, pixelStride,
							    dS << 3, 0, endX - startX, this);
						} else {
							this->span->Span->Normal(&this->pixels[pixelIdx],
							    leftS + dS * prestep, leftT + prestep * dT, pixelStride,
							    dS << 3, dT << 3, endX - startX, this);
						}
					}
				}

				rightEdge->fracX += rightEdge->stepX;
				rightEdge->fracZ += rightEdge->stepZ;
				rightEdge->fracS += rightEdge->stepS;
				rightEdge->fracT += rightEdge->stepT;
				leftEdge->fracX  += leftEdge->stepX;
				leftEdge->fracZ  += leftEdge->stepZ;
				leftEdge->fracS  += leftEdge->stepS;
				leftEdge->fracT  += leftEdge->stepT;
				++scanY;
			}
		}
	}
}

// Checks whether the line segment [a, b] is visible in the column z-buffer.
// If checkZ: returns true if any column's 1/z is less than the stored scale (closer).
// If !checkZ: returns true if any column has never been written (COLUMN_SCALE_INIT).
bool TinyGL::clippedLineVisCheck(TGLVert* a, TGLVert* b, bool checkZ) {
	int begX = a->x + 7 >> 3;
	int endX = b->x + 7 >> 3;
	if (begX < 0)                  begX = 0;
	if (endX > this->screenWidth)  endX = this->screenWidth;
	if (endX <= begX) return false;

	if (checkZ) {
		int dz   = (b->z - a->z) / (endX - begX);
		int curZ = a->z + (dz * ((begX << 3) - a->x) >> 3);
		for (; begX < endX; ++begX, curZ += dz) {
			if (0x7FFFFF / curZ < this->columnScale[begX]) {
				return true;
			}
		}
		return false;
	}
	while (begX < endX) {
		if (this->columnScale[begX] == TinyGL::COLUMN_SCALE_INIT) {
			return true;
		}
		++begX;
	}
	return false;
}

// Writes the minimum 1/z for each column covered by [a, b] into the column z-buffer.
// Returns true if any column was updated (i.e. this segment was closer).
bool TinyGL::occludeClippedLine(TGLVert* a, TGLVert* b) {
	int begX = a->x + 7 >> 3;
	int endX = b->x + 7 >> 3;
	if (begX < 0)                  begX = 0;
	if (endX > this->screenWidth)  endX = this->screenWidth;
	if (endX <= begX) return false;

	int dz   = (b->z - a->z) / (endX - begX);
	int curZ = a->z + (dz * ((begX << 3) - a->x) >> 3);
	bool updated = false;
	while (begX < endX) {
		int invZ = 0x7FFFFF / curZ;
		if (invZ < this->columnScale[begX]) {
			this->columnScale[begX] = invZ;
			updated = true;
		}
		++begX;
		curZ += dz;
	}
	return updated;
}

// Renders one vertical column of a sprite using RLE-encoded texture data.
// The sprite is stored as a list of columns; each column has a nibble-packed
// run count followed by (runStart, runHeight) byte pairs in the run data block.
// bottom/top/topAlt define the screen extents; flags bits control flipping.
void TinyGL::drawClippedSpriteLine(TGLVert* bottom, TGLVert* top, TGLVert* topAlt,
                                   int flags, bool /*unused*/) {
	Applet* app = CAppContainer::getInstance()->app;

	int rleCol = 0;  // current position in the RLE column table

	// pixelScale: how many sub-pixel units one texel occupies vertically on screen.
	int pixelScale = (bottom->y - topAlt->y << 12) / 176 >> 3;
	int screenTopY = topAlt->y >> 3;
	// texelStep: UV step per screen pixel (1/pixelScale in 24-bit fixed-point).
	int texelStep  = 16777216 / pixelScale;
	if (flags & 0x40000) {
		texelStep = -texelStep;  // flip Y
	}

	uint8_t* texData = this->textureBase;

	// The texture format: a nibble table at rleBase (each nibble = run count for
	// that column), followed by the actual run data (2 bytes per run).
	int rleBase  = this->textureBaseSize
	             - ((texData[this->textureBaseSize - 1] & 0xFF) << 8
	                | (texData[this->textureBaseSize - 2] & 0xFF)) - 2;
	int colMin   = this->imageBounds[0];
	int colMax   = this->imageBounds[1];
	int rlePtr   = rleBase + (colMax - colMin + 1 >> 1);  // past the nibble table
	int yAccum   = 0;  // cumulative y in the run data (used to compute UV)

	int screenX = bottom->x + 7 >> 3;
	int endX    = top->x    + 7 >> 3;
	if (screenX < this->viewportX)  screenX = this->viewportX;
	if (endX    > this->viewportX2) endX    = this->viewportX2;
	if (endX - screenX <= 0) return;

	int dsDx = (top->s - bottom->s) / (endX - screenX);  // S step per screen pixel
	int s    = bottom->s;

	while (screenX < endX) {
		// Map perspective-correct S to texture column index.
		int texCol = (176 * (s / bottom->z) + (176 * (s / bottom->z) < 0 ? 63 : 0)) >> 10;
		if (flags & 0x20000) {
			texCol = 175 - texCol;  // flip X
		}

		if (texCol >= colMin && texCol < colMax) {
			int targetCol = texCol - colMin;  // column index relative to colMin

			// Seek forward through RLE columns to reach targetCol.
			for (; rleCol < targetCol; ++rleCol) {
				int runCount = texData[rleBase + (rleCol >> 1)] >> ((rleCol & 1) << 2) & 0xF;
				// [GEC]: Fix animated water sprite
				app->render->fixTexels(rleBase + (rleCol >> 1), (rleCol & 1), this->mediaID, &runCount);
				while (runCount-- > 0) {
					yAccum += texData[rlePtr + 1];
					rlePtr += 2;
				}
			}

			// Seek backward through RLE columns if we overshot.
			while (rleCol > targetCol) {
				--rleCol;
				int runCount = texData[rleBase + (rleCol >> 1)] >> ((rleCol & 1) << 2) & 0xF;
				// [GEC]: Fix animated water sprite
				app->render->fixTexels(rleBase + (rleCol >> 1), (rleCol & 1), this->mediaID, &runCount);
				while (runCount-- > 0) {
					yAccum -= texData[rlePtr - 1];
					rlePtr -= 2;
				}
			}

			// Render all runs in the current column.
			int numRuns = texData[rleBase + (rleCol >> 1)] >> ((rleCol & 1) << 2) & 0xF;
			// [GEC]: Fix animated water sprite
			app->render->fixTexels(rleBase + (rleCol >> 1), (rleCol & 1), this->mediaID, &numRuns);
			while (numRuns-- > 0) {
				int     runStart = texData[rlePtr++];
				uint8_t runLen   = texData[rlePtr++];
				int     uvStart  = yAccum << 12;
				yAccum += runLen;
				if (flags & 0x40000) {
					runStart = 176 - (runStart + runLen);
					uvStart += (runLen << 12) - 1;
				}
				int screenY = screenTopY + (runStart * pixelScale >> 12);
				int screenLen = pixelScale * runLen >> 12;
				if (screenY < this->viewportY) {
					int clipTop = this->viewportY - screenY;
					screenLen -= clipTop;
					uvStart   += clipTop * texelStep;
					screenY    = this->viewportY;
				}
				if (screenY + screenLen > this->viewportY2) {
					screenLen = this->viewportY2 - screenY;
				}
				if (screenLen > 0) {
					this->span->Span->Stretch(
					    &this->pixels[screenX + this->screenWidth * screenY],
					    uvStart, texelStep, this->screenWidth, screenLen, this);
				}
			}
			++rleCol;
		}
		s += dsDx;
		++screenX;
	}
}

void TinyGL::resetCounters() {
	this->countBackFace = 0;
	this->countDrawn    = 0;
	this->spanPixels    = 0;
	this->spanCalls     = 0;
	this->zeroDT        = 0;
	this->zeroDS        = 0;
}


// [GEC]
// Fills the viewport with the solid color stored in colorBuffer.
// First row is drawn pixel-by-pixel, then copied into the remaining rows with memcpy.
void TinyGL::applyClearColorBuffer() {
	Applet* app = CAppContainer::getInstance()->app;
	uint16_t* pixels = this->pixels;
	int color = this->colorBuffer;
	int firstRow = this->viewportX + this->viewportY * this->screenWidth;
	for (int i = 0; i < this->viewportWidth; ++i) {
		pixels[firstRow + i] = Render::upSamplePixel(color);
	}
	int curRow = firstRow;
	for (int j = 1; j < this->viewportHeight; ++j) {
		curRow += this->screenWidth;
		std::memcpy(&pixels[curRow], &pixels[firstRow], this->viewportWidth * sizeof(uint16_t));
	}
}
