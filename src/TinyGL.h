#ifndef __TINYGL_H__
#define __TINYGL_H__

#include <climits>

#include "Span.h"
#include "TGLVert.h"
#include "TGLEdge.h"

class TGLVert;
class TGLEdge;

class TinyGL
{
private:

public:

    // Fixed-point scale factors
    static constexpr int SHIFT_STRETCH             = 12;   // UV shift for sprite stretch (1.0 = 1<<12)
    static constexpr int UNIT_SCALE                = 65536;
    static constexpr int MATRIX_ONE_SHIFT          = 14;   // matrix arithmetic uses 2.14 fixed-point
    static constexpr int MATRIX_ONE                = 16384;
    static constexpr int SCREEN_SHIFT              = 3;    // screen coords stored as pixels * 8
    static constexpr int SCREEN_ONE                = 8;
    static constexpr int INTERPOLATE_SHIFT         = 16;   // edge interpolants use 16 fractional bits
    static constexpr int INTERPOLATE_TO_PIXELS_SHIFT = 19; // = INTERPOLATE_SHIFT + SCREEN_SHIFT
    static constexpr int SCREEN_PRESTEP            = 7;
    static constexpr int INTERPOLATE_PRESTEP       = 524287;

    static constexpr bool CLAMP_TO_VIEWPORT = false;
    static constexpr int  PIXEL_GUARD_SIZE  = 1;
    static constexpr int  MAX_PRIMITIVE_VERTS = 20;
    static constexpr int  OE_SHIFT = 4;

    // Face-culling modes
    static constexpr int CULL_NONE = 0;
    static constexpr int CULL_CW   = 1;
    static constexpr int CULL_CCW  = 2;

    static constexpr int NEAR_CLIP  = 256;
    static constexpr int CULL_EXTRA = NEAR_CLIP + 16;

    static constexpr int NUM_FOG_LEVELS         = 16;
    static constexpr int COLUMN_SCALE_INIT      = INT_MAX;
    static constexpr int COLUMN_SCALE_OCCLUDED  = (INT_MAX - 1);

    // --- Render state ---
    int faceCull;
    SpanType* span;

    // Texture state
    uint8_t*  textureBase;
    int       imageBounds[4];
    uint16_t* spanPalette;
    uint16_t** paletteBase;
    uint16_t* scratchPalette;
    int sWidth,  sShift, sMask;
    int tHeight, tShift, tMask;
    bool swapXY;

    // Framebuffer
    int       screenWidth;
    int       screenHeight;
    uint16_t* pixels;
    int*      columnScale;  // 1/z per column for occlusion (1D z-buffer)

    // Matrices (column-major, 2.14 fixed-point)
    int view[16];
    int view2D[16];
    int projection[16];
    int mvp[16];
    int mvp2D[16];

    // Vertex scratch buffers
    TGLVert cv[32];   // clip-space vertices (Sutherland-Hodgman output)
    TGLVert mv[20];   // model-space input vertices
    TGLEdge edges[2]; // left and right polygon edge walkers

    // Fog
    int fogMin;
    int fogRange;
    int fogColor;

    // Counters
    int countBackFace;
    int countDrawn;
    int spanPixels;
    int spanCalls;
    int zeroDT;
    int zeroDS;

    // Viewport (in sub-pixel units, SCREEN_SHIFT=3)
    int viewportX, viewportY;
    int viewportWidth, viewportHeight;
    int viewportClampX1, viewportClampY1;
    int viewportClampX2, viewportClampY2;
    int viewportX2, viewportY2;
    int viewportXScale, viewportXBias;
    int viewportYScale, viewportYBias;
    int viewportZScale, viewportZBias;

    // Camera position and orientation
    int viewX, viewY, viewZ;
    int viewYaw, viewPitch;

    // Debug counters
    int c_backFacedPolys;
    int c_frontFacedPolys;
    int c_totalQuad;
    int c_clippedQuad;
    int c_unclippedQuad;
    int c_rejectedQuad;
    int unk03;
    int unk04;

    // [GEC] extensions
    uint32_t textureBaseSize;
    uint32_t paletteBaseSize;
    int16_t  paletteTransparentMask;
    uint32_t mediaID;
    int      colorBuffer;

    TinyGL();
    ~TinyGL();

    bool startup(int screenWidth, int screenHeight);
    uint16_t* getFogPalette(int invZ);
    void clearColorBuffer(int color);
    void buildViewMatrix(int x, int y, int z, int yaw, int pitch, int roll, int* matrix);
    void buildProjectionMatrix(int fov, int aspect, int* matrix);
    void multMatrix(int* matrix1, int* matrix2, int* destMtx);
    void _setViewport(int x, int y, int w, int h);
    void setViewport(int x, int y, int w, int h);
    void resetViewPort();
    void setView(int viewX, int viewY, int viewZ, int viewYaw, int viewPitch, int viewRoll, int viewFov, int viewAspect);
    void viewMtxMove(TGLVert* vert, int fwd, int right, int up);
    void drawModelVerts(TGLVert* verts, int count);
    TGLVert* transform3DVerts(TGLVert* verts, int count);
    TGLVert* transform2DVerts(TGLVert* verts, int count);
    void ClipQuad(TGLVert* v0, TGLVert* v1, TGLVert* v2, TGLVert* v3);
    void ClipPolygon(int plane, int count);
    bool clipLine(TGLVert* array);
    void projectVerts(TGLVert* array, int count);
    void RasterizeConvexPolygon(int count);
    bool clippedLineVisCheck(TGLVert* a, TGLVert* b, bool checkZ);
    bool occludeClippedLine(TGLVert* a, TGLVert* b);
    void drawClippedSpriteLine(TGLVert* bottom, TGLVert* top, TGLVert* topAlt, int flags, bool unused);
    void resetCounters();
    void applyClearColorBuffer(); // [GEC]
};

#endif
