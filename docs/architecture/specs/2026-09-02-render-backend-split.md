# 2026-09-02 — Render backend split: `dr_render_core` + GL and SDL_Render implementations

Status: design accepted, unimplemented.
ADRs: [0020](../adr/0020-render-backend-two-interfaces.md) (two need-shaped interfaces),
[0021](../adr/0021-sdl-render-second-backend-with-3d.md) (SDL_Render incl. 3D),
[0022](../adr/0022-fog-is-backend-dependent.md) (fog per backend).
Builds on: ADR 0009 (world viewport), ADR 0012 (UI layer), ADR 0019 + spec
`2026-09-01-blend-modes.md` (blend table).

Goal: the graphics backend becomes a library with two interchangeable implementations
selectable at startup (`--backend=gl|sdl`), where every faithful port detail stays in
backend-neutral game code and each implementation only answers "how do I put these
quads/triangles on the screen".

---

## 0. Starting inventory (measured 2026-09-02)

Direct `gl*` call sites: `render/World3D.cpp` 72, `render/gl/SpriteBatch.cpp` 65,
`render/gl/Texture.cpp` 35, `render/gl/Shader.cpp` 32, `platform/Window.cpp` 19
(context creation, SDL side), `render/RenderBackend.cpp` 10. `ui/MenuView.cpp` and
`ui/DialogView.cpp` mention `glColor4f` in comments only — no code.

GL leaks through headers into non-render modules today
(`grep -rn "render/gl/" new_src`): `ui/UiAssets.h:8`, `ui/Hud.h:8`, `ui/Ui.cpp:7`,
`ui/DialogView.cpp:6`, `text/Font.h:7`, `text/Font.cpp:3`, `core/GameLoop.cpp:11`
(unused include), plus `render/RenderBackend.h:6` and `render/Graphics2D.h:6`.

Caller inventory that shapes the interfaces:

* 2D operations actually used outside `render/`: `fillRect` (11), `drawImage`
  (region + anchored, 11), `drawString` (11), `drawRegion` (10), `drawRect` (5),
  `setBlendMode` (2, `ui/ViewWeapon.cpp:118,121`), `setClip`/`clearClip` (3,
  `ui/Ui.cpp:108,116,118`), `drawLine`, `drawImageScaled`, `drawBuffIcon`.
  All of them are already implemented **inside `Graphics2D`** in terms of two
  primitives: a textured quad and a solid quad (`Graphics2D.cpp:36-101`).
* 3D operations `World3D` needs from a device: "set texture", "set legacy render
  mode", "push N triangles of `{x,y,z,u,v}`", "draw the sky band", "set fog", plus
  scene begin/end with an MVP + view matrix. Nothing else: sky is the only
  screen-space primitive (`World3D.cpp:606-654`), all other geometry is world-space
  triangles produced by `drawPoly` / `drawBillboardPart` / `drawCharacter`.
* Frame-level operations used by `core/` and `render/SceneRenderer`:
  `beginFrame`, `endFrame`, `setCanvasViewport` (`SceneRenderer.cpp:112`),
  `restoreCanvasViewport` (`:157`), `drawableToCanvas`
  (`core/UiInputCollector.cpp:23`), `letterboxRect`, `flushFrame`.

---

## 1. Target file layout

```
new_src/render/api/          <- NEW, target dr_render_core (no GL, no SDL, no game deps)
    TextureId.h              opaque handle + TextureFlags
    TextureStore.h           texture creation interface
    Texture.h/.cpp           move-only RAII handle every caller keeps
    Draw2D.h                 2D device interface
    Scene3D.h                3D device interface
    RenderBackend.h/.cpp     abstract frame/device owner (virtual dtor in the .cpp)
    RenderModes.h/.cpp       the 14-row legacy RENDER_* table in neutral enums
    PixelConvert.h/.cpp      RGB565 -> RGBA8, palette expansion, indexed expansion
    QuadUV.h/.cpp            legacy rotateMode 0..8 -> quad UV permutation
    CanvasViewport.h/.cpp     letterbox rect + canvas<->drawable mapping
    BmpWriter.h/.cpp         24-bit BMP dump for frame capture

new_src/render/gl/           <- target dr_render_gl (existing dir, reshaped)
    GlCommon.h               (unchanged)
    Shader.h/.cpp            (unchanged, internal)
    GlTexture.h/.cpp         (was Texture.h/.cpp; internal to the store)
    GlTextureStore.h/.cpp    NEW: TextureStore impl, owns GlTexture by id
    GlDraw2D.h/.cpp          (was SpriteBatch.h/.cpp) Draw2D impl
    GlScene3D.h/.cpp         NEW: Scene3D impl (world shader, VAO/VBO, sky)
    GlRenderBackend.h/.cpp   NEW: RenderBackend impl (was RenderBackend.cpp)

new_src/render/sdl/          <- NEW, target dr_render_sdl
    SdlTextureStore.h/.cpp
    SdlDraw2D.h/.cpp
    SdlScene3D.h/.cpp
    SdlRenderBackend.h/.cpp
    SdlBlendModes.h/.cpp     RENDER_* row -> SDL_BlendMode table built at init

new_src/render/              <- stays in the executable target (needs text/, domain/)
    Graphics2D.h/.cpp        now holds a Draw2D*, no GL include
    World3D.h/.cpp           now holds a Scene3D*/TextureStore*, no GL include
    Camera3D.h/.cpp          unchanged
    SceneRenderer.h/.cpp     Env gains Graphics2D*
    RenderBackend.h          DELETED (replaced by render/api/RenderBackend.h)

new_src/core/
    RenderBackendFactory.h/.cpp  NEW: the only file including both impl headers
```

Namespace stays `newcore` everywhere. Include paths keep the `render/...` form
(`new_src` remains the single include root for all targets).

---

## 2. The interfaces (exact signatures)

### 2.1 `render/api/TextureId.h`

```cpp
namespace newcore {

// Opaque texture handle. 0 == invalid. Values are backend-private.
struct TextureId {
	uint32_t value = 0;
	bool valid() const { return value != 0; }
	bool operator==(TextureId o) const { return value == o.value; }
	bool operator!=(TextureId o) const { return value != o.value; }
};

// Creation-time intents, NOT API state.
enum class TextureFlags : uint32_t {
	None = 0,
	// Palette entries equal to the game's transparent color 0xF81F become fully
	// transparent (docs/original-code/image-formats.md §0, §1.2).
	TransparentKey = 1u << 0,
	// The caller's UVs may leave [0,1]: world walls/floors/ceilings and the sky.
	// GL uses GL_REPEAT; SDL splits geometry per tile (ADR 0021).
	Tiled = 1u << 1,
};
constexpr TextureFlags operator|(TextureFlags a, TextureFlags b);
constexpr bool hasFlag(TextureFlags set, TextureFlags f);

} // namespace newcore
```

### 2.2 `render/api/TextureStore.h`

```cpp
class TextureStore {
public:
	virtual ~TextureStore() = default;

	// indices: w*h bytes, top-left origin (the format BmpImageLoader and
	// MediaLoader already produce). palette: RGB565, paletteCount <= 256
	// entries; missing entries are padded with 0xF81F by the implementation
	// (matches io/BmpImageLoader.cpp:51-91). Returns an invalid id on failure.
	virtual TextureId createIndexed(const uint8_t* indices, int w, int h,
		const uint16_t* palette, int paletteCount, TextureFlags flags) = 0;

	// rgba: w*h*4 bytes, top-left origin, non-premultiplied.
	virtual TextureId createRgba(const uint8_t* rgba, int w, int h,
		TextureFlags flags) = 0;

	virtual void destroy(TextureId id) = 0;
	virtual bool query(TextureId id, int& w, int& h) const = 0;

	// Bytes currently held for texture pixel data (for the memory log; the GL
	// store counts index+LUT bytes, the SDL store counts expanded RGBA bytes).
	virtual size_t textureBytes() const = 0;
};
```

Threading: every `TextureStore`, `Draw2D` and `Scene3D` call happens on the single
render/game thread, as today. No implementation may assume otherwise.

### 2.3 `render/api/Texture.h` — the handle every caller keeps

```cpp
// Move-only owner of one TextureId. Replaces render/gl/Texture.h for callers:
// same member names, plus a TextureStore& on the upload calls.
class Texture {
public:
	Texture() = default;
	~Texture();
	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;
	Texture(Texture&&) noexcept;
	Texture& operator=(Texture&&) noexcept;

	bool uploadIndexed(TextureStore& store, const std::vector<uint8_t>& indices,
		int w, int h, const std::vector<uint16_t>& palette,
		bool transparent = false, bool tiled = false);
	bool uploadRgba(TextureStore& store, const std::vector<uint8_t>& rgba,
		int w, int h);

	void destroy();
	bool valid() const { return id_.valid(); }
	int width() const { return width_; }
	int height() const { return height_; }
	TextureId id() const { return id_; }

private:
	TextureStore* store_ = nullptr;
	TextureId id_;
	int width_ = 0, height_ = 0;
};
```

`width()/height()` are cached in the handle because `Graphics2D`'s anchored blits need
them every frame (`Graphics2D.cpp:79-101`) and must not pay for a virtual query.

### 2.4 `render/api/Draw2D.h`

```cpp
struct SrcRect { int x, y, w, h; };  // texels
struct DstRect { int x, y, w, h; };  // canvas pixels (480x320, origin top-left)
struct ColorF  { float r = 1.f, g = 1.f, b = 1.f, a = 1.f; };

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
	// flush pending work on change, like SpriteBatch::setBlendMode does today.
	virtual void setRenderMode(int renderMode) = 0;

	// Clip in canvas coordinates; w<=0 or h<=0 clips everything away (empty
	// rect, NOT "no clip"). One level only: nesting is Ui::pushClip's job.
	virtual void setClipCanvas(int x, int y, int w, int h) = 0;
	virtual void clearClip() = 0;

	// Rasterizes everything pending. Called by the backend around viewport
	// changes and by Graphics2D never.
	virtual void flush() = 0;
};
```

### 2.5 `render/api/Scene3D.h`

```cpp
// World-space vertex, already scaled by the legacy constants:
// position = raw >> VERT_COORDS_TO_FLOAT (/16384), uv = raw /1024
// (World3D.cpp:565-575, TEXT_COORDS_TO_FLOAT).
struct WorldVertex { float x, y, z, u, v; };

struct SceneView {
	const float* mvp;   // float[16], column-major (Camera3D::mvp())
	const float* view;  // float[16], column-major (Camera3D::viewFloat())
};

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

	// Linear eye-space fog request. start/end in the same units as the view
	// matrix output; rgba is the fog color, alpha unused for blending
	// (World3D::setFog converts fogMin/fogRange, World3D.cpp setFog).
	// Per-mode fog on/off comes from the RenderModes table, not from here.
	virtual void setFog(bool enabled, float start, float end, const float rgba[4]) = 0;

	// The sky band: a viewport-filling quad with u in [-0.5+uOffset, 0.5+uOffset]
	// and v in [0,1] (legacy gles::DrawSkyMap; World3D.cpp:629-646). uOffset is
	// viewYaw/256. Independent of beginScene/endScene.
	virtual void drawSky(TextureId sky, float uOffset) = 0;

	virtual void flush() = 0;
};
```

Rationale for `submitTriangles` rather than "drawQuad3D": `World3D` already builds
triangle fans for polygons (`World3D.cpp:558-577`), billboards
(`World3D.cpp:1053-1055`) and stacked characters, so triangles are what the caller
has. A quad-only interface would force the caller to un-triangulate.

### 2.6 `render/api/RenderBackend.h`

```cpp
class Window;

// Abstract frame/device owner. Method names are unchanged from the current
// concrete RenderBackend so existing call sites keep compiling.
class RenderBackend {
public:
	virtual ~RenderBackend();

	virtual bool initialize(Window& window) = 0;

	virtual void beginFrame(Window& window) = 0;
	virtual void endFrame(Window& window) = 0;

	// Restricts drawing to a sub-rect of the letterboxed canvas, in canvas
	// coordinates (ADR 0009 world band 1,7,478,248). Flushes 2D work first.
	virtual void setCanvasViewport(int x, int y, int w, int h) = 0;
	virtual void restoreCanvasViewport(Window& window) = 0;

	virtual void letterboxRect(int& x, int& y, int& w, int& h) const = 0;
	virtual bool drawableToCanvas(int px, int py, int& cx, int& cy) const = 0;

	virtual void flushFrame() = 0;

	// Dumps the letterboxed canvas of the NEXT presented frame to a 24-bit BMP
	// at `path` (one shot; the implementation clears the request afterwards).
	virtual void requestCapture(const char* path) = 0;

	virtual Draw2D& draw2d() = 0;
	virtual Scene3D& scene3d() = 0;
	virtual TextureStore& textures() = 0;

	// "gl" / "sdl": window title suffix + log lines + capture file names.
	virtual const char* name() const = 0;
};
```

`Graphics2D` is deliberately **not** on this interface: it is backend-neutral game code
that depends on `text/Font` and therefore cannot live in `dr_render_core`. The
composition root owns it (see §4.3).

### 2.7 `render/api/RenderModes.h` — the one blend table

The 14-row table moves out of `World3D.cpp:145-183` (unchanged values and citations,
ADR 0019) into neutral enums:

```cpp
enum class BlendFactor { Zero, One, SrcAlpha, OneMinusSrcAlpha, OneMinusSrcColor };

struct RenderModeRow {
	BlendFactor src, dst;
	float mod[4];   // legacy glColor4f primary color under GL_MODULATE
	bool fog;       // legacy fogMode == 2
};

constexpr int kRenderModeCount = 14;              // Render::RENDER_MAX, src/Render.h:31
extern const RenderModeRow kRenderModes[kRenderModeCount];

// Rows 8/11/13 have no legacy GL case (default: assert(0), src/GLES.cpp:702-705).
bool renderModeNeedsWarning(int mode);
// Clamps out-of-range to 0 (RENDER_NORMAL) and logs each distinct offender once.
int clampRenderMode(int mode);

// Named values used by callers (currently ui/ViewWeapon.cpp).
constexpr int kRenderNormal = 0;
constexpr int kRenderBlend25 = 1, kRenderBlend50 = 2, kRenderBlend75 = 12;
constexpr int kRenderAdd = 3, kRenderAdd75 = 4, kRenderAdd50 = 5, kRenderAdd25 = 6;
constexpr int kRenderSub = 7, kRenderNone = 10;
```

Row values are copied verbatim from `World3D.cpp:159-176` including the comment block
explaining why the additive rows keep `SrcAlpha` as the source factor
(`src/GLES.cpp:648-650`) and why row 7 keeps the GL meaning `dst = dst*(1-Csrc)`.
`warnRenderModeOnce` moves here too (`World3D.cpp:185-201`).

### 2.8 Shared neutral helpers

* `PixelConvert.h`: `kTransparentKey565 = 0xF81F`;
  `void rgb565ToRgba8(uint16_t c, bool keyTransparent, uint8_t out[4])` — the exact
  bit-replication of `gl/Texture.cpp:75-88`;
  `void expandPalette565(const uint16_t* pal, int count, bool keyTransparent, uint8_t out[1024])`
  (pads missing entries with the key color);
  `void expandIndexed(const uint8_t* idx, int w, int h, const uint8_t pal8888[1024], uint8_t* outRgba)`.
* `QuadUV.h`: `void quadUv(int rotateMode, float u0, float v0, float u1, float v1, float uv[4][2])`
  — the rotate-mode permutation extracted **verbatim** from `SpriteBatch::draw`, so the
  two backends cannot drift. Output order is the destination quad's
  top-left, top-right, bottom-right, bottom-left.
* `CanvasViewport.h`:
  ```cpp
  struct CanvasViewport {
      static constexpr int kCanvasW = 480, kCanvasH = 320;
      int x = 0, y = 0, w = 0, h = 0;          // letterbox rect in drawable px
      void setFromDrawable(int drawableW, int drawableH);  // = Window::computeViewport
      float scale() const;                     // min(w/480, h/320)
      bool drawableToCanvas(int px, int py, int& cx, int& cy) const;  // verbatim from RenderBackend.cpp:49-64
      // canvas sub-rect -> drawable rect; glBottomUp flips y like RenderBackend.cpp:66-80.
      void canvasSubRect(int cx, int cy, int cw, int ch, bool glBottomUp,
                         int& dx, int& dy, int& dw, int& dh) const;
  };
  ```
* `BmpWriter.h`: `bool writeBmp24(const char* path, const uint8_t* rgba, int w, int h, bool bottomUp);`
  The ad-hoc `saveIndexedBmp` debug helper in `World3D.cpp:16-55` (unreferenced, known
  incomplete spot #4) is deleted in favour of this.

### 2.9 What is NOT in any interface (enforced by review)

* Shaders, GLSL source, shader handles, uniform names, `Shader` class.
* `GLuint`, `GLenum`, `GLint`, any `GL_*` constant, `render/gl/GlCommon.h`.
* `SDL_Renderer`, `SDL_Texture`, `SDL_Vertex`, `SDL_BlendMode`.
* Palettes as GPU objects: a palette is only an argument to `createIndexed`. There is
  no "bind palette", no palette id, no palette update.
* VAO/VBO/batch capacities, vertex layouts, `Vertex` structs other than
  `WorldVertex`.
* GL blend enums: only the neutral `BlendFactor` in `RenderModes.h`.
* Depth buffers, render targets, framebuffer objects, mipmaps, filtering modes
  (nearest is the only mode the game ever wants; both implementations hardcode it).
* Anything named `SpriteBatch` — the batching strategy is a private implementation
  detail of each backend.

---

## 3. Backend-neutral consumers after the split

### 3.1 `Graphics2D` (unchanged public API, new dependency)

* `#include "render/gl/Texture.h"` becomes `#include "render/api/Texture.h"`.
* `setBatch(SpriteBatch*)` becomes `setDevice(Draw2D*)`; the member is `Draw2D* dev_`.
* Bodies change mechanically: `batch_->draw(tex, ...)` becomes
  `dev_->drawQuad(tex.id(), SrcRect{...}, DstRect{...}, rotateMode, ColorF{...})`;
  `batch_->fillRect(...)` becomes `dev_->fillQuad(...)`;
  `batch_->setScissorCanvas` / `clearScissor` become `setClipCanvas` / `clearClip`.
* `setBlendMode(int mode)` becomes a pass-through to `setRenderMode` taking the
  **legacy RENDER_\* value**. The two existing call sites change:
  `ui/ViewWeapon.cpp:118` `setBlendMode(1)` -> `setBlendMode(kRenderAdd50)` (5),
  `:121` `setBlendMode(0)` -> `setBlendMode(kRenderNormal)` (0). This is the fix of a
  latent bug: today "1" means ADD50 in `SpriteBatch` but RENDER_BLEND25 in the world
  table; after the split there is one numbering.
* All layout logic (anchors, `drawString`, Bresenham `drawLine`, `drawRect`,
  `drawImageScaled`, `drawBuffIcon`) stays exactly where it is.

### 3.2 `text/Font`

`Font::drawChar(SpriteBatch&, ...)` becomes `Font::drawChar(Draw2D&, ...)`;
`Font::upload(indices, w, h, palette)` gains a leading `TextureStore&`. `Font.h`
includes `render/api/Texture.h` and forward-declares `Draw2D`.

### 3.3 `ui/` and `io/`

* `ui/UiAssets.h`, `ui/Hud.h`, `ui/Ui.cpp`, `ui/DialogView.cpp`: include
  `render/api/Texture.h` instead of `render/gl/Texture.h`.
* `UiAssets::load(const ResourceReader&)` becomes
  `UiAssets::load(TextureStore&, const ResourceReader&)`; its private `loadTexture`
  helper forwards the store (`ui/UiAssets.cpp:11-29`).
* `core/GameLoop.cpp:11` drops its unused `render/gl/GlCommon.h` include.

### 3.4 `World3D` — geometry only

Header: drops `render/gl/GlCommon.h`, `Shader.h`, `Texture.h`; includes
`render/api/Texture.h` and forward-declares `Scene3D`, `TextureStore`.

* `bool initialize()` becomes `bool initialize(Scene3D& scene, TextureStore& store)`;
  it stores the two pointers and no longer compiles anything. The shader sources
  (`World3D.cpp:59-96`) move to `GlScene3D.cpp`.
* Members removed: `Shader shader_`, `vao_`, `vbo_`, `vertexCount_`, every `loc*`
  uniform location, `currentTex_`/`currentPal_` as `GLuint`.
* Members kept, retyped to the API `Texture`: `white_`, `textureByTile_`,
  `spriteTexByMedia_`, `sky_`, and `spriteIsRle_`.
* `begin(camera)` becomes `scene_->beginScene(SceneView{camera.mvp(), camera.viewFloat()})`
  plus resetting the local trackers; `end()` becomes `scene_->endScene()`.
* `flush()` disappears (the device batches); the "flush before texture change" logic
  becomes `scene_->setTexture(tex.id())` — the device flushes internally, and the
  early-out on an unchanged id lives in the device too. `World3D` keeps its own
  `currentTexId_` only if it needs it for logic (it does not).
* `applyBatchState(renderMode)` becomes `scene_->setRenderMode(clampRenderMode(mode))`.
  The kBlendModes table, `warnRenderModeOnce` and `renderModeNeedsWarning` move to
  `RenderModes.*`; the *decision* which mode a sprite draws in stays in `World3D`.
* `setFog(int argb, int fogMin, int fogRange)` keeps its signature and body
  (the fogStart/fogEnd conversion is port knowledge) and forwards the result to
  `scene_->setFog(...)`.
* `drawSky(camera)` keeps its signature; body becomes
  `scene_->drawSky(sky_.id(), camera.viewYaw() / 256.f)`. The NDC quad and its UV
  recipe move into the backends (they are device-space constructs).
* `uploadMapTextures` / `ensureSpriteTexture` / `uploadSky` call
  `Texture::uploadIndexed(store, ...)`. World wall/floor textures pass
  `tiled = true` (today's `repeat`), sprites pass `transparent = true, tiled = true`
  exactly as today (`World3D.cpp:434`), sky passes both true
  (`World3D.cpp:590`).
* Every triangle emission (`drawPoly`, `drawBillboardPart`, `drawTorchiereGlow`,
  `drawCharacter`, the wall/flat/slip-door branches) fills a local
  `std::vector<WorldVertex>` reused across calls and ends with
  `scene_->submitTriangles(v.data(), (int)v.size())`. The vertex math, all legacy
  constants and all comments stay byte-for-byte.

### 3.5 `SceneRenderer`

`Env` gains `Graphics2D* g2d = nullptr;` and the fallback fill at
`SceneRenderer.cpp:159` uses it instead of `renderer.g2d()`. Everything else
(viewport band, camera, shake, classification) is untouched.

---

## 4. Implementations

### 4.1 `dr_render_gl`

Pure relocation of existing code, no behavior change:

* `GlTextureStore` owns `std::vector<GlTexture>` (slot 0 unused so id 0 is invalid) +
  a free list; `createIndexed` = today's `Texture::uploadIndexed` (R8 index texture +
  RGBA8 256x1 LUT, `GL_NEAREST`, wrap `GL_REPEAT` when `Tiled`), using
  `PixelConvert::expandPalette565` for the LUT. `textureBytes()` sums `w*h + 1024`.
* `GlDraw2D` is today's `SpriteBatch` with the three programs, the canvas-space
  scissor and the letterbox latch, plus: `setRenderMode` looks the row up in
  `kRenderModes` and maps `BlendFactor` -> GL enum; `drawQuad` resolves `TextureId`
  through the store (a raw `const GlTexture*` lookup, no virtual call).
* `GlScene3D` is the GL half of today's `World3D.cpp`: the world shader, VAO/VBO,
  `beginScene` uniform setup (`uMVP`, `uView`, `uTexture`, `uPalette`, fog uniforms,
  `uColorMod`), `setTexture` (bind index + LUT to units 0/1, flush first),
  `setRenderMode` (glBlendFunc + `uColorMod` + fog toggle, the exact
  flush-compare-assign cache of `applyBatchState`), `submitTriangles` (append to the
  vertex vector, flush at `kMaxVerts`), `drawSky` (identity MVP NDC quad,
  `uColorMod` reset — ADR 0019 point 4), `endScene` (flush + restore standard blend).
* `GlRenderBackend` is today's `RenderBackend.cpp` with `CanvasViewport` doing the
  math, plus `requestCapture` (glReadPixels of the letterbox rect after
  `flushFrame`, before `swap`; rows come out bottom-up -> pass `bottomUp = true` to
  `writeBmp24`).

### 4.2 `dr_render_sdl`

#### 4.2.0 Measured texture-format facts — SDL2 vs SDL3 (probes run 2026-09-02)

Both probes were compiled and run on this machine; the output is reproducible and the
sources live in the scratchpad (`sdl_fmt_probe.c` for SDL2, `sdl3_fmt_probe.c` for
SDL3). The two generations differ **in kind**, so the facts are listed separately. The
project currently builds against SDL2 (`CMakeLists.txt:18` `find_package(SDL2 REQUIRED)`,
`new_src/CMakeLists.txt:35` `SDL2::SDL2`); moving to SDL3 is a separate decision that
has not been taken.

**SDL 2.32.10 (current dependency):**

1. `SDL_CreateTexture(SDL_PIXELFORMAT_INDEX8, ...)` is **rejected on every driver** with
   the literal error *"Palettized textures are not supported"*. No indexed-texture path
   exists; expansion is mandatory.
2. Trap: `ARGB1555` / `RGB565` textures are *created* "successfully", but the `metal`
   driver advertises only 32-bit formats in `SDL_RendererInfo.texture_formats`
   (ARGB8888/ABGR8888 + YUV). Requesting 16 bits there makes SDL keep an intermediate
   surface and convert on every update — strictly more memory. Never hardcode a 16-bit
   format.
3. None of the three working drivers (`metal`, `opengl`, `software`) advertises a 16-bit
   format **with alpha** (no `ARGB1555`, no `ARGB4444`). Every sprite needs the
   transparent key, so sprites are 32-bit with no alternative; opaque wall/floor
   textures could be `RGB565` only under the software driver — not worth a second code
   path.
4. Working path: build an `INDEX8` `SDL_Surface`
   (`SDL_CreateRGBSurfaceWithFormatFrom` + `SDL_SetPaletteColors`), then
   `SDL_CreateTextureFromSurface`, which converts **once**. Observed results:
   `ARGB8888` on metal, `RGB888` (24-bit) on opengl/software.
5. Measured volume: **9.7 MB** of indexed texels (39 `newTexels*.bin`) + **328 KB** of
   palettes; expanded to 32 bits that is **~40 MB** for the complete world texture set.
6. Consequence for fog: expansion depends on the *palette*, and world textures would
   need 16 fog levels per palette; baking them turns ~40 MB into hundreds. Fog on the
   SDL2 path is therefore per-vertex colour (ADR 0022), which `SDL_RenderGeometry`
   carries natively.

**SDL 3.4.0 (installed, not adopted):**

7. Indexed textures are supported **natively**. Every working driver (`metal`, `opengl`,
   `gpu`, `software`) advertises `SDL_PIXELFORMAT_INDEX8`; `SDL_CreateTexture(INDEX8)`
   returns a real 8-bit texture (*actual format: SDL_PIXELFORMAT_INDEX8, 8 bpp*), and
   `SDL_CreateTextureFromSurface` on an INDEX8 surface **keeps** INDEX8.
8. Texture palettes are first-class and mutable: `SDL_SetTexturePalette` /
   `SDL_GetTexturePalette` (`/opt/homebrew/include/SDL3/SDL_render.h:1018,1033`) and the
   creation property `SDL_PROP_TEXTURE_CREATE_PALETTE_POINTER` (`:696,813`).
9. 16-bit formats **with alpha** work: `ARGB1555` and `ARGB4444` are created as real
   16-bit textures on all drivers.
10. Consequence: on SDL3 the indexed pipeline survives end to end (9.7 MB stays 9.7 MB,
    UI stays 0.77 MB) and palette-swap effects — the original's 16 baked fog palettes,
    and the `Render::setupPalette` greyshift/pulsate/multiply-shift effects — become
    *expressible*, per texture / per draw rather than per span.

Affine mapping (D1) is unaffected by all of this: `SDL_RenderGeometry` has no `w`
component in either generation.

#### 4.2.1 The fork this creates (user's decision, NOT taken here)

| | **Track A — stay on SDL2** (baseline of this spec) | **Track B — migrate to SDL3** |
|---|---|---|
| Texture storage | expand once to a runtime-negotiated 32-bit format | keep `INDEX8` + palette, as the data already is |
| World texture memory | ~40 MB | 9.7 MB (+328 KB palettes) |
| Fog on the SDL path | per-vertex colour (ADR 0022): correct at vertices, faceted across large quads, sprites fade to black | *option* of palette-swap fog, i.e. the original's own approach, quantized to 16 levels per draw |
| Palette effects (`setupPalette`) | not expressible without CPU re-expansion | expressible by swapping the palette |
| Interface impact | none — `TextureStore::createIndexed` already takes indices + palette | none for creation; would *add* one optional call (`TextureStore::setPalette(TextureId, const uint16_t*, int)`) |
| Cost | none (already the dependency) | SDL2 -> SDL3 API migration across `platform/Window`, `platform/InputSystem`, `core/GameLoop` event handling, `render/sdl/*`, CMake; SDL3 renames/removes a large part of the SDL2 surface |
| Unknown | none | whether INDEX8 is expanded on the GPU or on the CPU (see §4.2.3) |

**Recommendation (decision stays with the user):** implement Track A now, because it is
the current dependency and because the whole point of the SDL backend is portability
that works today. The design is deliberately arranged so Track B costs nothing extra
later: `TextureStore::createIndexed` already speaks indices + palette (never RGBA), so
migrating means *deleting* the expansion inside `SdlTextureStore`, not reshaping the
interface. Do **not** promise palette fog before the §4.2.3 measurement.

#### 4.2.2 `SdlTextureStore` (Track A, SDL2)

* At init, query `SDL_GetRendererInfo` and pick the pixel format **once**: the first
  advertised format that is 32-bit with alpha (preferring `ARGB8888`, then `ABGR8888`);
  if the driver advertises none, fall back to `SDL_PIXELFORMAT_ARGB8888` and log the
  driver name plus the advertised list. Log the choice at startup. **Never** hardcode a
  16-bit format (facts 2-3).
* `createIndexed` builds an `INDEX8` surface over the caller's indices
  (`SDL_CreateRGBSurfaceWithFormatFrom(indices, w, h, 8, w, SDL_PIXELFORMAT_INDEX8)`),
  fills its palette from `expandPalette565` (256 entries, key colour -> `a = 0`), sets
  `SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE)` so alpha survives the
  conversion verbatim, then `SDL_CreateTextureFromSurface` (fact 4). No manual
  per-pixel loop is needed on this path; `PixelConvert::expandIndexed` stays for
  `createRgba` callers.
* `createRgba` uses `SDL_CreateTexture(chosenFormat, SDL_TEXTUREACCESS_STATIC, w, h)` +
  `SDL_UpdateTexture`.
* Both set `SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest)` and
  `SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND)` as the default.
* Keeps `w`, `h`, the `SDL_Texture*` and the `TextureFlags` per slot (the flags drive
  `SdlScene3D`'s tiling split and the fog rule of ADR 0022).
* `textureBytes()` sums `w*h*SDL_BYTESPERPIXEL(chosenFormat)`; logged after the map
  media upload so the ~40 MB budget of fact 5 is observable.

#### 4.2.3 Required measurement before anyone banks on palette fog (SDL3 only)

**Open question:** does an SDL3 driver expand `INDEX8` on the **GPU** (palette as a LUT
in its own shader) or on the **CPU** when the texture is created/updated? If the latter,
`SDL_SetTexturePalette` implies re-uploading all texels and palette fog is dead on
performance grounds.

Concrete measurement (a scratchpad probe, not project code — no new_src changes):

1. Create one `INDEX8` texture of 256x256 and 16 distinct 256-entry palettes.
2. Loop 10,000 times: `SDL_SetTexturePalette(tex, pal[i % 16])` +
   `SDL_RenderTexture` of the whole texture into a 256x256 render target; measure wall
   time per iteration for each driver (`metal`, `opengl`, `gpu`, `software`).
3. Compare against a control loop that renders the same texture 10,000 times with the
   palette set **once**.
4. Interpretation: a per-iteration cost on the order of a plain draw (< ~50 us) means
   GPU expansion and palette fog is viable; a cost that scales with `w*h`
   (~65 K pixel copies, hundreds of us to ms) means CPU expansion and palette fog is
   rejected on performance.
5. Second question in the same probe: does a palette swap affect textures *already
   submitted* in the current frame (i.e. is the palette latched at draw time or at
   present time)? If it is latched at present time, per-draw palette fog is impossible
   regardless of speed, since all draws would share the last palette.

Until this probe has been run and reported, the spec's fog behaviour is exactly ADR 0022
(per-vertex on the SDL path) and no group depends on palettes.

#### 4.2.4 The remaining SDL components (identical on both tracks)

* **`SdlDraw2D`**: one code path for everything — a batch of `SDL_Vertex` +
  indices flushed with `SDL_RenderGeometry`. `drawQuad` builds 4 vertices / 6 indices
  using `quadUv` for the rotate mode and `ColorF*255` for the vertex color;
  `fillQuad` appends to the same batch with a 1x1 white texture created at init (keeps
  ordering trivially correct and avoids `SDL_RenderFillRect` interleaving problems).
  `setRenderMode` flushes and latches the row; the texture's `SDL_BlendMode` is set
  from `SdlBlendModes` right before each `SDL_RenderGeometry`. `setClipCanvas` flushes
  then `SDL_RenderSetClipRect` (canvas coords work directly, see §4.4);
  `w<=0||h<=0` sets an empty rect.
* **`SdlScene3D`**: the CPU pipeline of ADR 0021.
  1. `beginScene` latches `mvp`/`view` (copies 32 floats) and the owner's current
     canvas viewport rect.
  2. `submitTriangles`: for each triangle —
     a. if the current texture has `Tiled` and any `u`/`v` leaves `[0,1]`: split in
        object space against integer `u`/`v` boundaries (Sutherland-Hodgman on the
        plane `u = k`, then `v = k`; interpolate `x,y,z,u,v`), cap the number of
        emitted cells at 64 and fall back to clamped UVs beyond that with a one-shot
        log; each output piece gets `u -= floor(u_cell)`, `v -= floor(v_cell)`.
        Non-tiled textures skip this step entirely (sprites, characters, HUD art).
     b. transform by `mvp`; clip against `w > 1.0f/1024.0f` (near plane) with the same
        interpolation; skip degenerate output.
     c. divide by `w`, map NDC to canvas pixels of the latched viewport:
        `sx = vx + (ndc.x*0.5f + 0.5f) * vw`, `sy = vy + (0.5f - ndc.y*0.5f) * vh`.
     d. fog per vertex: depth from the `view` matrix (`-(view*pos).z`, the same
        expression the GL vertex shader uses, `World3D.cpp:67-68`), factor
        `f = clamp((end - depth)/(end - start), 0, 1)` when the mode's row says
        `fog` and fog is enabled.
        Colors: `vertexColor = row.mod * 255`, multiplied by `f` when fog applies and
        (the texture is keyed OR the fog color is near-black); otherwise `f` is stored
        per vertex for the haze pass.
     e. append into the batch keyed by (texture, render mode).
  3. flush: `SDL_SetTextureBlendMode(tex, sdlModeFor(row))` +
     `SDL_RenderGeometry`; then, if a haze pass is due (opaque texture, fog on,
     non-black fog color), a second `SDL_RenderGeometry` with `texture = nullptr`,
     color = fog RGB, per-vertex alpha `= (1-f)*255`, blend
     `SDL_BLENDMODE_BLEND` (ADR 0022).
  4. `drawSky(tex, uOffset)`: a viewport-filling quad with `u` in
     `[-0.5 + uOffset, 0.5 + uOffset]`, `v` in `[0,1]`, run through the same tiling
     split (the sky texture is `Tiled`), no fog, blend NORMAL.
  5. No depth buffer, no sorting: submission order is preserved by construction, and
     batches are flushed whenever texture or mode changes, so painter order is
     exactly the GL path's.
* **`SdlBlendModes`**: builds a `SDL_BlendMode[14]` at init from `kRenderModes`:
  * `{SrcAlpha, OneMinusSrcAlpha}` -> `SDL_BLENDMODE_BLEND`;
  * `{SrcAlpha, One}` -> `SDL_BLENDMODE_ADD` (SDL's ADD is exactly
    `dstRGB = srcRGB*srcA + dstRGB`, matching `src/GLES.cpp:648-650`);
  * `{Zero, OneMinusSrcColor}` (row 7, SUB) -> `SDL_ComposeCustomBlendMode(
    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_BLENDOPERATION_ADD,
    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD)`
    — an exact match of the GL row;
  * `{Zero, One}` (row 10, RENDER_NONE) -> the batch is **skipped entirely** (the
    result is `dst = dst`);
  * validation at init: each composed mode is probed once with
    `SDL_SetTextureBlendMode` on the 1x1 white texture; on failure the row falls back
    to `SDL_BLENDMODE_MOD` with a one-shot log naming the SDL driver
    (`SDL_GetRendererInfo`).
* **`SdlRenderBackend`**: owns the store, `SdlDraw2D`, `SdlScene3D` and a
  `CanvasViewport`. `beginFrame`: clear the whole target black
  (`SDL_RenderSetViewport(nullptr)` + `SDL_RenderSetScale(1,1)` +
  `SDL_SetRenderDrawColor(0,0,0,255)` + `SDL_RenderClear`), then apply the letterbox
  (see §4.4). `endFrame`: flush both devices, optional capture, `SDL_RenderPresent`.
  `requestCapture`: `SDL_RenderReadPixels` of the letterbox rect into RGBA and
  `writeBmp24(..., bottomUp = false)`.

### 4.3 Composition root

* `core/RenderBackendFactory.h`:
  ```cpp
  enum class BackendKind { OpenGL, SdlRender };
  const char* backendKindName(BackendKind);
  bool parseBackendKind(const char* text, BackendKind& out); // "gl" | "sdl"
  std::unique_ptr<RenderBackend> createRenderBackend(BackendKind);
  ```
  This is the only translation unit that includes `render/gl/GlRenderBackend.h` and
  `render/sdl/SdlRenderBackend.h`.
* `AppContext::initialize(const char* dataArchive, BackendKind backend)` picks the
  matching `Window::GraphicsApi`, creates the window, then the backend.
* `AppContext` gains `Graphics2D g2d_` and `Graphics2D& g2d()`; after
  `renderer_->initialize(window)` it calls `g2d_.setDevice(&renderer_->draw2d())`.
  Call-site changes: `core/GameContext.cpp:692` (`renderer.g2d()` -> `app.g2d()`),
  `core/Main.cpp:245` (`&app.renderer().g2d()` -> `&app.g2d()`), and
  `SceneRenderer::Env.g2d` wired in `Main.cpp`.
* `Main.cpp`: parse `--backend=gl|sdl` (and env `DOOM2RPG_BACKEND` as a fallback,
  default `gl`) before `AppContext::initialize`; pass the store into
  `Font::upload`, `UiAssets::load`, `World3D::initialize(scene3d, textures)`; log
  one line `renderer: <name>, textures: <bytes>` after the map media upload.
* Window title becomes `Doom II RPG [gl]` / `[sdl]` so the user always knows which
  backend is on screen.

### 4.4 `platform/Window`

```cpp
enum class GraphicsApi { OpenGL, SdlRender };

bool initialize(const char* title, GraphicsApi api);
GraphicsApi api() const;
struct SDL_Renderer* sdlRenderer() const;   // nullptr under OpenGL
void present();                              // was swapBuffers()
```

* `OpenGL`: unchanged path (`SDL_WINDOW_OPENGL` + `SDL_GL_CreateContext`,
  `Window.cpp:42-70`), `present()` = `SDL_GL_SwapWindow`.
* `SdlRender`: window without `SDL_WINDOW_OPENGL`;
  `SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | (vSync_ ?
  SDL_RENDERER_PRESENTVSYNC : 0))`; `present()` = `SDL_RenderPresent`.
* `applyVSync()` branches: `SDL_GL_SetSwapInterval` vs `SDL_RenderSetVSync`.
* `computeViewport` stays but delegates to `CanvasViewport::setFromDrawable` so the
  letterbox math exists once.
* Letterbox application differs per backend and lives in the backend, not in Window:
  GL sets `glViewport`; SDL sets `SDL_RenderSetViewport(letterbox rect in drawable px)`
  + `SDL_RenderSetScale(scale, scale)`, after which all `SdlDraw2D`/`SdlScene3D`
  coordinates are plain canvas coordinates — identical to the GL path's canvas-space
  vertices. `setCanvasViewport(x,y,w,h)` for the world band becomes
  `SDL_RenderSetViewport(canvas rect offset by the letterbox)` in the SDL backend and
  `glViewport` (with the y flip of `RenderBackend.cpp:66-80`) in the GL one; both
  flush first. `SDL_RenderSetLogicalSize` is deliberately **not** used, so both
  backends share `CanvasViewport` and the input mapping stays bit-identical.

### 4.5 CMake

```
new_src/CMakeLists.txt
    add_library(dr_render_core STATIC   <glob render/api/*.cpp>)
        target_include_directories(dr_render_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
        # links nothing (stdlib only)
    add_library(dr_render_gl STATIC     <glob render/gl/*.cpp>)
        target_link_libraries(dr_render_gl PUBLIC dr_render_core OpenGL::GL
                                                 $<$<PLATFORM_ID:Darwin>:${OPENGL_FRAMEWORK}>)
        target_link_libraries(dr_render_gl PRIVATE SDL2::SDL2)   # Window.h only
    add_library(dr_render_sdl STATIC    <glob render/sdl/*.cpp>)
        target_link_libraries(dr_render_sdl PUBLIC dr_render_core SDL2::SDL2)
    add_executable(DoomIIRPG <glob everything, minus render/api|gl|sdl>)
        target_link_libraries(DoomIIRPG dr_render_gl dr_render_sdl SDL2::SDL2 ZLIB::ZLIB OpenGL::GL)
```

The executable glob keeps `GLOB_RECURSE` and then
`list(FILTER SOURCE_FILES EXCLUDE REGEX "/render/(api|gl|sdl)/")` (same for headers).
Compile options (`-fsigned-char`, `-Wno-write-strings`, Darwin
`-Wno-deprecated-declarations`) and `CXX_STANDARD 17` are applied to all four targets.
**Every group that adds or removes a file requires a reconfigure**
(`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`).

---

## 5. Blend modes and color modulation across backends

| Legacy mode | GL row (unchanged) | SDL realization | Notes |
|---|---|---|---|
| 0 NORMAL | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`, mod 1,1,1,1, fog | `SDL_BLENDMODE_BLEND`, vertex color 255 | exact |
| 1 BLEND25 | same func, mod a=0.25 | BLEND, vertex alpha 64 | exact |
| 2 BLEND50 | mod a=0.50 | BLEND, vertex alpha 128 | exact |
| 12 BLEND75 | mod a=0.75 | BLEND, vertex alpha 191 | exact |
| 3 ADD | `SRC_ALPHA, ONE` | `SDL_BLENDMODE_ADD` | exact |
| 4/5/6 ADD75/50/25 | `SRC_ALPHA, ONE`, mod rgb=k | ADD, vertex rgb = k*255 | exact |
| 7 SUB | `ZERO, ONE_MINUS_SRC_COLOR` | `SDL_ComposeCustomBlendMode(ZERO, ONE_MINUS_SRC_COLOR, ADD, ZERO, ONE_MINUS_SRC_ALPHA, ADD)` | exact where custom modes are supported |
| 9 PERF | BLEND, mod a=0.5 | BLEND, alpha 128 | exact |
| 10 NONE | `ZERO, ONE` | batch skipped | exact (`dst = dst`) |
| 8/11/13 | fallback rows + one-shot log | same rows, same log | unreachable from a 3D sprite (`src/GLES.cpp:697-705`) |

Color modulation: GL keeps the `uColorMod` uniform for the world (ADR 0019) and vertex
colors for 2D; SDL uses **vertex colors everywhere** (`SDL_RenderGeometry` multiplies
texel by vertex color, which is exactly `GL_MODULATE`). `SDL_SetTextureColorMod` /
`SDL_SetTextureAlphaMod` are deliberately **not** used: they are per-texture state
(fighting batching) and are ignored by some `SDL_RenderGeometry` paths.

SUB fallback (`SDL_BLENDMODE_MOD`) on screen: the only user is the tile-212 wall decal
override; instead of darkening the wall by the *inverse* of the decal color it darkens
it by the decal color itself, i.e. the stain appears with inverted contrast (bright
parts of the decal darken the wall the most). One log line names the driver so we know
when it happens; the fix is to pick a different SDL driver, not to change the game.

---

## 6. Implementation groups (one delegation each)

Ordered least-risky first; **every group ends with a building, playable tree** and a
picture the user can check.

### G1 — `dr_render_core` skeleton (neutral code only, GL still wired as today)

Create `render/api/`: `TextureId.h`, `TextureStore.h`, `Draw2D.h`, `Scene3D.h`,
`RenderBackend.h/.cpp`, `RenderModes.h/.cpp`, `PixelConvert.h/.cpp`, `QuadUV.h/.cpp`,
`CanvasViewport.h/.cpp`, `BmpWriter.h/.cpp`, `Texture.h/.cpp`. Add the three CMake
library targets and the executable filter (the two impl libs may start out with a
single placeholder `.cpp` each so the targets exist).
Wire *only* the safe consumers: `render/gl/Texture.cpp` uses `PixelConvert` for the
LUT; `World3D.cpp` uses `RenderModes`/`clampRenderMode` instead of its local table
(mapping `BlendFactor` -> GL enum in a local helper); `SpriteBatch::draw` uses
`quadUv`; `RenderBackend.cpp` uses `CanvasViewport`; `GameLoop.cpp` drops the unused
GL include; `World3D.cpp`'s `saveIndexedBmp` deleted.
Acceptance: `cmake --build build_new -j 8` clean; the game looks **exactly** as before
(user check: world, sprites, HUD, menu, blend-mode effects such as the torchiere glow
and the muzzle flash); `grep -c "kBlendModes" new_src/render/World3D.cpp` == 0.

### G2 — GL implements `TextureStore` + `Draw2D`; the 2D layer loses GL

Add `render/gl/GlTextureStore.*`, rename `Texture.*` -> `GlTexture.*` (store-internal),
rename `SpriteBatch.*` -> `GlDraw2D.*` implementing `Draw2D`. `Graphics2D` holds a
`Draw2D*`; `Font::drawChar` takes `Draw2D&`; `Font::upload`/`UiAssets::load` take
`TextureStore&`; `ui/*` and `text/*` include `render/api/Texture.h`.
`RenderBackend.h` (old) deleted; `render/gl/GlRenderBackend.*` implements the abstract
`RenderBackend` (with `requestCapture` returning a "not implemented" log for now);
`AppContext` owns `unique_ptr<RenderBackend>` + `Graphics2D`; the 4 `g2d()` call sites
move; `ViewWeapon`'s two `setBlendMode` calls take RENDER_* values.
Acceptance: no file outside `render/gl/` mentions `render/gl/` (single allowed
exception: `core/RenderBackendFactory.cpp`, added in G4); picture identical, including
the view weapon's additive muzzle flash, the menu's 25 % plates, dialog boxes, the
damage vignette and text.

### G3 — GL implements `Scene3D`; `World3D` loses GL

Add `render/gl/GlScene3D.*` (world shader + VAO/VBO + fog/colorMod state + sky quad,
moved verbatim). `World3D` becomes geometry-only per §3.4.
Acceptance: `grep -c "gl[A-Z]" new_src/render/World3D.cpp` == 0 and no `gl*` call
exists outside `new_src/render/gl/` and `platform/Window.cpp`; the world is pixel-wise
unchanged — user check on: wall/floor textures and their tiling, lava UV scroll, sky
with yaw shift, billboards and stacked characters, torchiere glow, water spout, fog if
enabled via the `Main.cpp` test line, item pickup sprites, crates.

### G4 — Dual-API window, backend factory, frame capture

`Window` gains `GraphicsApi`/`sdlRenderer()`/`present()`; `core/RenderBackendFactory.*`
with only `BackendKind::OpenGL` producing a backend (`SdlRender` logs "not built yet"
and falls back to GL); `--backend=` parsing in `Main.cpp`; window title suffix;
`GlRenderBackend::requestCapture` implemented; F12 in the `GameLoop` event callback
calls `renderer.requestCapture("capture-gl-NNN.bmp")` (counter in the loop) — placed
next to the existing debug keys (`GameLoop.cpp:43-51`).
Acceptance: `./DoomIIRPG --backend=gl` runs as before; F12 writes a BMP next to the
executable that the user can open and confirms it matches what was on screen (correct
orientation, no letterbox bars, 480x320-proportioned).

### G5 — SDL backend: 2D complete, 3D stub

`render/sdl/`: `SdlTextureStore`, `SdlBlendModes`, `SdlDraw2D`, `SdlRenderBackend`,
plus an `SdlScene3D` whose `submitTriangles`/`drawSky` do nothing and whose
`beginScene` fills the world band with a flat color (so the world band is uniformly
colored, as `SceneRenderer.cpp:159` does when the world is missing). Factory registers
`BackendKind::SdlRender`. Startup logs the SDL driver, the negotiated texture
format and `textureBytes()` after the UI sheets load (§4.2.0 facts 1-5 must be visible
in the log, not assumed).
Acceptance: `./DoomIIRPG --backend=sdl` boots and is fully navigable: title/menu, HUD,
dialogs, loot list, comic/cutscene pages, travel map, text, the damage vignette and
the view weapon all render; F12 capture from both backends of the same screen (e.g.
the in-game menu) is visually identical — the orchestrator diffs the two BMPs with a
throwaway `tools/` script and reports the max per-channel delta (expect 0-1 from
rounding, and 0 differences in geometry/placement).

### G6 — SDL backend: the 3D world

`SdlScene3D` per §4.2: near clip, viewport mapping, object-space tile split, batching,
per-vertex fog + haze pass, sky quad.
Acceptance: `--backend=sdl` renders a recognizable world — user check on: corridor
walls with correctly tiled textures (no smearing, no visible seams at tile
boundaries), floors/ceilings, the sky band shifting with yaw, sprites/monsters at the
right size and position, the muzzle flash additive, the torchiere glow additive, no
z-fighting/order inversions relative to the GL capture. Expected and accepted:
texture warping on large near polygons (ADR 0021) and per-vertex fog banding
(ADR 0022). Side-by-side F12 captures from the same position are compared by the user;
the orchestrator additionally checks that sprite bounding boxes land within a couple
of pixels of the GL capture.

### G7 — Parity polish (only after user feedback from G6)

Texture-memory log line, SUB fallback / driver logs, optional `--gfx-stats` frame
counters, `docs/architecture/README.md` module-map refresh, and whichever specific
artifacts the user names in G6 (candidate: quad subdivision against the affine
warping, deliberately not done earlier).

---

## 7. Deviations (with the on-screen description)

* **D1 — Affine texture mapping in the SDL 3D path** (ADR 0021). Texture patterns bend
  and the quad's triangle diagonal becomes visible as a crease on large polygons seen
  at a steep angle — worst on the floor directly under/in front of the camera, absent
  on frontal walls and on all billboards. Accepted by the user in exchange for
  portability.
* **D2 — Fog is backend-dependent** (ADR 0022). GL: per-pixel as today. SDL:
  per-vertex, exact at polygon corners, faintly faceted across large floor quads;
  keyed sprites fade toward black instead of toward a colored fog. The original's
  16-step palette quantization (`src/TinyGL.cpp:47-52`, calls at `:654,662`) is
  reproduced by neither backend.
* **D3 — No per-pixel palette effects on the SDL path.** Any future palette trick
  (palette-rotation damage flash, the original's baked fog palettes) has to be done by
  re-expanding textures on the CPU or by drawing overlay quads: SDL cannot hold indexed
  textures at all (§4.2.0 fact 1), so a palette change means re-uploading every
  affected texture. The original's 16 baked fog palettes are ruled out on SDL2 by
  measurement — one expanded copy per palette per fog level turns the measured ~40 MB
  into hundreds (ADR 0022). On SDL3 they would become expressible via
  `SDL_SetTexturePalette` (§4.2.0 fact 8), gated on the §4.2.3 measurement and on the
  SDL3 migration decision, which is the user's. Today nothing changes on screen, because
  the rewrite has no palette effects.
* **D4 — Index-0 sprite transparency stays unimplemented** in both backends (the
  second, independent transparency mechanism documented in
  `docs/original-code/image-formats.md` §0). Unchanged from today; parked.
* **D5 — On SDL2 textures are expanded; the format is negotiated at runtime; on SDL3
  this deviation would disappear.** Measured (§4.2.0): SDL 2.32.10 rejects
  `SDL_PIXELFORMAT_INDEX8` on every driver (*"Palettized textures are not supported"*)
  and advertises no 16-bit format with alpha, so the game's palette-indexed data lands
  in a 32-bit texture: world media 9.7 MB of indexed texels + 328 KB of palettes ->
  **~40 MB** expanded for the complete set; the 43 UI sheets the rewrite loads =
  783,269 px -> 3.0 MB (0.77 MB as indices); all 275 shipped BMPs would be 13.2 Mpx ->
  50 MB but are never all resident. SDL 3.4.0 supports INDEX8 natively on every driver
  and would keep 9.7 MB as 9.7 MB (fact 7), which is why the interface never speaks RGBA
  (§4.2.1 Track B). No atlas, no eviction; `textureBytes()` is logged so a regression is
  visible. On screen: nothing changes either way — the SDL2 conversion is exact
  (256-entry palette, `a = 0` on the key colour); the cost is only memory.
* **D6 — `Graphics2D::setBlendMode` renumbers** from its private {0,1} to the legacy
  `RENDER_*` values. Two call sites in `ui/ViewWeapon.cpp`; no visual change (1 -> 5
  keeps the additive flash).

---

## 8. Comparing the two backends (how the user and the orchestrator verify)

1. Selection: `./DoomIIRPG --backend=gl` / `--backend=sdl` (default `gl`,
   env override `DOOM2RPG_BACKEND`). The window title shows `[gl]` / `[sdl]`; the
   startup log prints `renderer: <name>` plus, for SDL, the chosen SDL driver from
   `SDL_GetRendererInfo`.
2. Same-frame capture: F12 asks the active backend to dump the letterboxed canvas of
   the frame being presented to `capture-<backend>-NNN.bmp` in the working directory.
   Both backends capture *after* all drawing and *before* present, at the same
   resolution (run both with the default resolution index so the BMPs are the same
   size and directly diffable).
3. Reproducible frame: use the debug coordinate overlay (`B`, `GameLoop.cpp:50`) to
   confirm identical player x/y/yaw before pressing F12 in each run; the simulation is
   deterministic given the same inputs, so identical coordinates mean identical
   geometry submission.
4. The orchestrator may diff the two BMPs with a throwaway `tools/` script (max/mean
   per-channel delta, and a mask of pixels differing by more than 8). Expectation:
   2D screens diff to ~0; world screens differ in texture interiors (D1/D2) but agree
   on silhouettes and sprite placement within a couple of pixels.
5. The user remains the eyes for everything the numbers cannot judge (is the warping
   tolerable, is the fog convincing, does anything blink or z-fight).

---

## 9. Explicitly NOT done in this work

* No third backend, no Vulkan/GLES/D3D path, no `SDL_Renderer` software-driver tuning.
* **No SDL2 -> SDL3 migration.** The backend is written against the current SDL2
  dependency (Track A of §4.2.1). The interface is shaped so the migration stays a
  local change inside `render/sdl/` plus the platform layer, but the decision and the
  work are out of scope here.
* No palette-swap fog and no palette effects (`Render::setupPalette`): they need SDL3
  *and* the §4.2.3 measurement first.
* No runtime hot-switch between backends: selection happens before window creation;
  switching means restarting the process.
* No render thread; the single-threaded GL/game loop stays (ADR 0010 wiring).
* No texture atlas, no streaming, no eviction, no compression.
* No z-buffer in either backend (the port relies on BSP painter order).
* No perspective-correct subdivision in the SDL path (candidate for G7 only if the
  user asks).
* No palette-effect framework, no shader files on disk, no shader hot-reload.
* No change to any faithful port logic: `World3D`'s geometry math, `Camera3D`,
  `SceneRenderer`'s classification, `Graphics2D`'s layout and the `RENDER_*` table
  values move but do not change. Any behavior change spotted during the move is a bug
  in the move.
* No unit-test harness for the render layer beyond frame capture + the user's eyes.
