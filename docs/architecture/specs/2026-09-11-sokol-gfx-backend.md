# 2026-09-11 — sokol_gfx becomes the graphics backend; the SDL_Render backend is retired

Status: specification, ready for implementation
ADRs: [0024](../adr/0024-sokol-gfx-single-backend.md),
[0025](../adr/0025-frame-command-list.md),
[0026](../adr/0026-frame-capture-is-gl-only.md)
Supersedes for the future: ADR 0021, ADR 0022, ADR 0023 (become historical)
Builds on: spec `2026-09-02-render-backend-split.md` (the `render/api/` interfaces stay
exactly as they are), ADR 0019 + spec `2026-09-01-blend-modes.md` (the 14-row table),
ADR 0020 (two interfaces), ADR 0009 (world viewport).

---

## 0. Verified facts this design rests on

Everything in this section was checked against the actual upstream sources on
2026-09-11 (sokol master `sokol_gfx.h`, `sokol-tools-bin/sokol_shaders.cmake`, and the
`sokol-shdc` osx_arm64 binary run locally). Line numbers refer to that
`sokol_gfx.h` revision; the revision we vendor must be re-checked against §1.1.

### 0.1 `SG_PIXELFORMAT_R8` — THE blocking question, answered: fully supported

`SG_PIXELFORMAT_R8` exists (`sokol_gfx.h:2264`) and is the first entry of the
non-`NONE` pixel-format range. Per backend:

| Backend | Capability decision | Result |
|---|---|---|
| GL 4.1 core / GLES3 | `_sg_gl_init_pixelformats()` calls `_sg_pixelformat_all(&_sg.formats[SG_PIXELFORMAT_R8])` **unconditionally**, outside every `#if` (`:10291`); internal format `GL_R8` (`:10202`) | sample + filter + render + blend + msaa |
| Metal | `_sg_mtl_init_caps()` calls `_sg_pixelformat_all(...R8)` **unconditionally** (`:16016`); `MTLPixelFormatR8Unorm` (`:15570`) | same |
| WebGPU | unconditional `_sg_pixelformat_all` (`:18209`); `WGPUTextureFormat_R8Unorm` (`:18010`) | same |
| Vulkan | `VK_FORMAT_R8_UNORM` (`:21539`) | same |
| D3D11 | queried at runtime from `DXGI_FORMAT_R8_UNORM` (`:13508`) via `D3D11_FORMAT_SUPPORT_TEXTURE2D` / `_SHADER_SAMPLE` (`:13851-13861`). `R8_UNORM` is a mandatory-support format for texture2d + shader sample at feature level 10.0 and above | sample + filter guaranteed |

`_sg_pixelformat_all` sets `sample=filter=blend=render=msaa=true` (`:9213-9219`), and
`sg_pixelformat_info.sample` is documented as "can be sampled in shaders at least with
nearest filtering" (`:2348`). We only ever need `sample` with nearest.

**Conclusion: the index-texture + 256×1-palette scheme carries over 1:1 on every
backend we target. The 15.1 MB figure survives; there is no fallback to design and no
motivation to re-litigate.** A defensive `sg_query_pixelformat(SG_PIXELFORMAT_R8).sample`
check at init with a fatal log is still specified (§5.3) because it costs one line.

### 0.2 sokol_gfx owns no window, device or swapchain

Confirmed by the header itself: the caller must create the 3D-API device/context before
`sg_setup()` and pass it in `sg_desc.environment`, and must hand a fresh `sg_swapchain`
to **every** `sg_begin_pass()` (`sokol_gfx.h:3026-3095`). The structs:

```c
typedef struct sg_environment {                  // -> sg_desc.environment
    sg_environment_defaults defaults;            // { color_format, depth_format, sample_count }
    sg_metal_environment metal;                  // { const void* device }        // MTLDevice
    sg_d3d11_environment d3d11;                  // { device, device_context }
    sg_wgpu_environment wgpu;
    sg_vulkan_environment vulkan;
} sg_environment;

typedef struct sg_swapchain {                    // -> sg_pass.swapchain, EVERY frame
    bool invalid;                                // true => all other members must be zero
    int width, height, sample_count;
    sg_pixel_format color_format, depth_format;
    sg_metal_swapchain metal;                    // { current_drawable, depth_stencil_texture, msaa_color_texture }
    sg_d3d11_swapchain d3d11;                    // { render_view, resolve_view, depth_stencil_view }
    sg_wgpu_swapchain wgpu;
    sg_vulkan_swapchain vulkan;
    sg_gl_swapchain gl;                          // { uint32_t framebuffer }  // 0 = default FB
} sg_swapchain;
```

`sg_swapchain.invalid = true` is the sanctioned way to say "the surface could not be
acquired this frame"; sokol then silently skips all rendering in that pass. Metal
pointers must be tunneled with `(__bridge const void*)`.

**On Metal, sokol presents the drawable itself**: `_sg_mtl_commit()` calls
`[cmd_buffer presentDrawable:cur_drawable]` (`sokol_gfx.h:17207-17209`). So on Metal
`Window::present()` must do **nothing**. On GL we still call `SDL_GL_SwapWindow`.

### 0.3 No depth buffer is needed — at all

The renderer has always been painter's-order with depth test off
(`new_src/render/gl/GlScene3D.cpp:106-107`, `GlRenderBackend.cpp:72`, matching
`src/GLES.cpp:89`). Therefore:

* `sg_environment.defaults.depth_format = SG_PIXELFORMAT_NONE`,
* `sg_swapchain.depth_format = SG_PIXELFORMAT_NONE`, no `depth_stencil_*` surface,
* every pipeline: `depth = { .pixel_format = SG_PIXELFORMAT_NONE, .write_enabled = false,
  .compare = SG_COMPAREFUNC_ALWAYS }`.

This is the single biggest simplification of the Metal and D3D11 glue: **we never have
to allocate or resize a depth texture ourselves.** Say so out loud in code comments so
nobody "helpfully" adds one back.

### 0.4 Clip-space depth convention differs, and it matters here

sokol_gfx does not normalize clip space. GL clips `-w ≤ z ≤ w`; Metal and D3D11 clip
`0 ≤ z ≤ w`. `Camera3D` produces a GL-style projection
(`new_src/render/Camera3D.cpp`), so on Metal/D3D11 **the whole near half of the world
would be clipped away** — a spectacular, easily-misdiagnosed bug.

Fix (specified, not optional): the world vertex shader ends with

```glsl
vec4 p = mvp * vec4(pos, 1.0);
p.z = p.z * depth_fix.x + p.w * depth_fix.y;
gl_Position = p;
```

with `depth_fix = (1.0, 0.0)` on `SOKOL_GLCORE`/`SOKOL_GLES3` and `(0.5, 0.5)` on
Metal/D3D11/WGPU. On GL this is the exact identity, so the GL A/B diff stays
byte-clean; on Metal/D3D11 it maps `[-w, w] → [0, w]`.

The 2D and sky shaders need no fix: they emit `z = 0.0, w = 1.0`, which is inside both
clip volumes.

**Viewport and scissor y-flip is NOT our problem**: `sg_apply_viewport` /
`sg_apply_scissor_rect` take an `origin_top_left` flag and sokol does the flip per
backend (`sokol_gfx.h:12296-12303`, `:14945-14963`). We pass `origin_top_left = true`
everywhere and feed canvas-derived top-left rects.

Texture rows need no flip either: on every backend, sampling `v = 0` reads the first row
in memory, which is what `BmpImageLoader`/`MediaLoader` already produce.

### 0.5 Mid-frame buffer updates are restricted — hence the command list

* `sg_update_buffer`: "Only one update per frame is allowed" (`sokol_gfx.h:340-342`).
* `sg_write_buffer_transient`: multiple calls per frame are allowed "**but only until
  the resource is bound**. After a resource has been used in a frame it cannot be
  written to until the next frame" (`:323-326`).
* `sg_append_buffer` still exists (`:5538`) and does allow interleaved
  append/draw, but the header openly announces the update functions are being replaced
  and marks `sg_buffer_usage.dynamic_update` as **deprecated** (`:3358`).

Our renderers flush many times per frame (texture change, render-mode change, clip
change, batch overflow). None of the above allows write→draw→write→draw on one buffer
without leaning on the deprecated path.

**Decision (ADR 0025): a per-frame command list.** Both devices append vertex bytes to
one CPU staging vector and record draw/viewport/scissor commands. At `endFrame` we do
one `sg_write_buffer_transient`, then `sg_begin_pass` → replay → `sg_end_pass` →
`sg_commit`. This is also why nothing outside `SgRenderBackend::endFrame` ever touches
the GPU.

### 0.6 Resource model: images are bound through *views*

The vendored sokol revision uses the `sg_view` resource type. A sampled texture is
**two** objects:

```c
sg_image img = sg_make_image(&(sg_image_desc){ ... });
sg_view  v   = sg_make_view(&(sg_view_desc){ .texture = { .image = img } });
```

and `sg_bindings.views[VIEW_tex] = v; sg_bindings.samplers[SMP_smp] = smp;`
(`sokol_gfx.h:3130`, bindings struct `:2xxx`, `sg_texture_view_desc` = `{ image,
mip_levels, slices }`). One indexed texture therefore costs **2 images + 2 views**.

Default pool sizes are `image 128`, `view 256` (`:6602-6607`) — far too small for this
game. Pool sizes are specified in §5.1.

### 0.7 sokol-shdc: verified working, and it dictates GL 4.1

* The CMake integration file is `sokol_shaders.cmake` at the root of
  `sokol-tools-bin`, and `_sokol_shader_get_binary()` resolves the compiler as
  `${CMAKE_CURRENT_FUNCTION_LIST_DIR}/bin/<host>/sokol-shdc`. **The `.cmake` file must
  therefore sit next to the vendored `bin/` directory** — vendor the repository whole,
  do not copy the single file out. Unsupported hosts (`Windows ARM64`, 32-bit Windows)
  produce a `FATAL_ERROR` at configure time, by design.
* Output goes to `${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/<name>.glsl.h` — the
  build tree, never the source tree. The generated header is an
  `add_custom_command(OUTPUT ...)` artifact, so the consuming target must list it in
  its sources.
* I compiled the two real shaders of this project (§4) with the osx_arm64 binary for
  `glsl410:glsl300es:metal_macos:metal_ios:metal_sim:hlsl5:wgsl:spirv_vk` — exit code 0,
  no diagnostics. The dialect and the generated names in §4 are copied from that run,
  not guessed.
* **The lowest desktop-GL target sokol-shdc offers is `glsl410` (`#version 410`).**
  Our window currently asks for GL 3.3 (`new_src/platform/Window.cpp:45-47`). It must
  ask for **4.1 core** (§6.1). macOS caps at exactly 4.1 core, so this is the one
  version that works everywhere. The raw-GL reference backend keeps its `#version 330`
  shaders — they compile fine in a 4.1 core context.

---

## 1. Vendoring

### 1.1 New directories

```
third_party_libs/sokol/               # sokol_gfx.h only (single header, copied from upstream)
third_party_libs/sokol-tools-bin/     # the repository, whole: sokol_shaders.cmake + bin/**
```

Existing convention followed: `third_party_libs/hash-library` is already vendored and
added with `add_subdirectory` from `CMakeLists.txt:32`.

Add `third_party_libs/sokol/VERSION.txt` recording the upstream commit hash of
`sokol_gfx.h`, and `third_party_libs/sokol-tools-bin/VERSION.txt` with the
`sokol-tools-bin` commit hash.

> **Version pairing is a real hazard.** The generated header carries
> `#version:5#` and static bind-slot defines that must match the vendored
> `sokol_gfx.h`'s resource model (this revision has `sg_view`, `sg_write_buffer_*`).
> Pick the two commits from the same day and never bump one alone. If a future bump
> breaks the build, bump both and re-run the §0 checks.

`third_party_libs/sokol` needs **no** `CMakeLists.txt`; it is an include directory.

### 1.2 Backend selection at build time

One CMake cache variable, `DOOM2RPG_SOKOL_BACKEND`, with values `metal | d3d11 | glcore`
and a per-platform default:

| Host | Default (final) | Default until group G6 / G7 lands |
|---|---|---|
| APPLE | `metal` | `glcore` |
| WIN32 | `d3d11` | `glcore` |
| other | `glcore` | `glcore` |

It maps to exactly one compile definition on `dr_render_sokol`: `SOKOL_METAL`,
`SOKOL_D3D11` or `SOKOL_GLCORE`. Being able to force `glcore` on macOS is not a
curiosity — it is how the F12 pixel diff against `dr_render_gl` is run (§8).

---

## 2. Module layout

New target `dr_render_sokol`, new directory `new_src/render/sokol/`. It links
`dr_render_core` (PUBLIC) and `SDL2` (PRIVATE, for `platform/Window.h` and the Metal
view helpers). It must **not** link `OpenGL::GL` explicitly on Apple — sokol includes
`<OpenGL/gl3.h>` itself and the framework comes from the executable link line; on Linux
it needs `OpenGL::GL`; on Windows sokol carries its own GL loader.

| File | Responsibility |
|---|---|
| `SgCommon.h` | The only place that includes `sokol_gfx.h` (declarations). Asserts exactly one `SOKOL_*` backend define is set. No `SOKOL_IMPL`. |
| `SokolGfxImpl.cpp` | Non-Apple: `#define SOKOL_IMPL` + `#include <sokol_gfx.h>`. Nothing else. |
| `SokolGfxImpl.mm` | Apple: identical content, Objective-C++ because the Metal and macOS-GL implementations require it (`sokol_gfx.h:83`). Only one of the two is compiled. |
| `SgEnvironment.h` | The platform glue interface (§3) + `createSgEnvironment()` factory. |
| `SgEnvironmentGl.cpp` | `SOKOL_GLCORE` implementation. |
| `SgEnvironmentMetal.mm` | `SOKOL_METAL` implementation. |
| `SgEnvironmentD3D11.cpp` | `SOKOL_D3D11` implementation. |
| `SgShaders.h` | Includes `SgCommon.h`, then the two generated headers `quad2d.glsl.h` and `world.glsl.h`. Single point of contact with generated code. |
| `SgPipelines.h/.cpp` | The 4 deduplicated blend variants of the 14-row table, the 4 shader programs, and the 16-entry pipeline cache (§5.4). |
| `SgTexture.h/.cpp` | One texture: index `sg_image`+`sg_view`, palette `sg_image`+`sg_view`, sampler choice, w/h, format. Move-only. Mirrors `render/gl/GlTexture`. |
| `SgTextureStore.h/.cpp` | `TextureStore` implementation, slot vector + free list, `lookup()` returning `const SgTexture*`. Mirrors `render/gl/GlTextureStore`. |
| `SgFrame.h/.cpp` | The per-frame command list and vertex staging buffer (§5.5). |
| `SgDraw2D.h/.cpp` | `Draw2D` implementation; records into `SgFrame`. Port of `GlDraw2D`. |
| `SgScene3D.h/.cpp` | `Scene3D` implementation; records into `SgFrame`. Port of `GlScene3D`. |
| `SgRenderBackend.h/.cpp` | `RenderBackend` implementation: `sg_setup`/`sg_shutdown`, letterbox, frame replay, capture. |
| `shaders/quad2d.glsl` | Hand-written annotated GLSL, 3 programs (§4.1). |
| `shaders/world.glsl` | Hand-written annotated GLSL, 1 program (§4.2). |

Nothing in `new_src/render/sokol/` may include `render/gl/` or `render/sdl/`.

---

## 3. The SDL → sokol_gfx platform glue

### 3.1 Interface (`new_src/render/sokol/SgEnvironment.h`)

```cpp
#ifndef NEW_RENDER_SOKOL_SGENVIRONMENT_H
#define NEW_RENDER_SOKOL_SGENVIRONMENT_H

#include <cstdint>
#include <memory>

#include "render/sokol/SgCommon.h"

namespace newcore {

class Window;

// Everything sokol_gfx refuses to do: create the 3D device, acquire a drawable,
// describe the swapchain, put the finished frame on screen, and read it back.
// Exactly one implementation is compiled, chosen by the SOKOL_* backend define.
class SgEnvironment {
public:
	virtual ~SgEnvironment();

	// Creates the device/context/surface. The GL implementation finds the context
	// already made by Window; the Metal one creates the MTLDevice and the
	// CAMetalLayer; the D3D11 one creates device + swap chain.
	virtual bool create(Window& window) = 0;
	virtual void destroy() = 0;

	// Handed to sg_setup() via sg_desc.environment. Valid after create().
	virtual sg_environment environment() const = 0;

	// Called once per frame, immediately before sg_begin_pass(). Acquires the
	// drawable/render-target-view for this frame. On failure it must return a
	// struct with .invalid = true and everything else zeroed (sokol then skips
	// the pass, sokol_gfx.h:3044-3049).
	virtual sg_swapchain acquireSwapchain(Window& window) = 0;

	// Called after sg_commit(). GL swaps here; Metal must do NOTHING because
	// sokol already called presentDrawable (sokol_gfx.h:17207-17209); D3D11
	// calls IDXGISwapChain::Present and releases nothing.
	virtual void present(Window& window) = 0;

	// Called on a window size change, after Window refreshed its drawable size.
	virtual void resize(Window& window) = 0;

	// Letterbox readback for the F12 capture (ADR 0026). Writes w*h*4 RGBA bytes,
	// BOTTOM row first (BMP order). Returns false when the API cannot do it, which
	// is the honest answer on Metal and D3D11 for now.
	virtual bool readPixels(int x, int y, int w, int h, uint8_t* rgbaBottomUp) = 0;

	// "sokol-gl" / "sokol-metal" / "sokol-d3d11": logs + window title + capture names.
	virtual const char* apiName() const = 0;
};

// Built for whichever SOKOL_* define this target was compiled with.
std::unique_ptr<SgEnvironment> createSgEnvironment();

} // namespace newcore

#endif
```

### 3.2 `SgEnvironmentGl` — implement NOW (group G2)

* `create()`: nothing to do; `Window` already made the GL 4.1 core context. Log
  `glGetString(GL_VERSION)`. Return true.
* `environment()`: `{ .defaults = { .color_format = SG_PIXELFORMAT_RGBA8,
  .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1 } }`.
* `acquireSwapchain()`: `{ .width = window.drawableWidth(), .height =
  window.drawableHeight(), .sample_count = 1, .color_format = SG_PIXELFORMAT_RGBA8,
  .depth_format = SG_PIXELFORMAT_NONE, .gl.framebuffer = 0 }`. If either dimension is
  ≤ 0 return `{ .invalid = true }`.
* `present()`: `SDL_GL_SwapWindow(window.nativeHandle())`.
* `resize()`: nothing.
* `readPixels()`: `glPixelStorei(GL_PACK_ALIGNMENT, 4);` then `glReadPixels(x, y, w, h,
  GL_RGBA, GL_UNSIGNED_BYTE, dst)`. GL returns the bottom row first, which is already
  BMP order — identical to `GlRenderBackend::writeCapture`
  (`new_src/render/gl/GlRenderBackend.cpp:105-116`). The GL entry points come from the
  headers `sokol_gfx.h` already included via `SgCommon.h`; do **not** include
  `render/gl/GlCommon.h`.
* `apiName()`: `"sokol-gl"`.

### 3.3 `SgEnvironmentMetal` — implement in group G6

`SgEnvironmentMetal.mm`, Objective-C++. Members: `SDL_MetalView view_`,
`CAMetalLayer* layer_`, `id<MTLDevice> device_`, `id<CAMetalDrawable> drawable_`.

* `create()`:
  1. `view_ = SDL_Metal_CreateView(window.nativeHandle());` — fails if the window was
     not created with `SDL_WINDOW_METAL` (§6.1 adds that flag).
  2. `layer_ = (CAMetalLayer*)SDL_Metal_GetLayer(view_);`
  3. `device_ = MTLCreateSystemDefaultDevice();`
  4. `layer_.device = device_; layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
     layer_.framebufferOnly = YES;`
  5. `resize(window)` to set `layer_.drawableSize`.
* `environment()`: `{ .defaults = { .color_format = SG_PIXELFORMAT_BGRA8,
  .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1 },
  .metal.device = (__bridge const void*)device_ }`.
  BGRA8 is the documented Metal swapchain format (`sokol_gfx.h:5274`,
  `MTLPixelFormatBGRA8Unorm` at `:15596`).
* `acquireSwapchain()`: `drawable_ = [layer_ nextDrawable];` If nil → log once and
  return `{ .invalid = true }`. Otherwise `{ w, h from layer_.drawableSize,
  sample_count = 1, color_format = SG_PIXELFORMAT_BGRA8, depth_format =
  SG_PIXELFORMAT_NONE, .metal.current_drawable = (__bridge const void*)drawable_ }`.
  The strong member keeps the drawable alive across `sg_commit()`.
* `present()`: `drawable_ = nil;` **and nothing else.** sokol already presented.
* `resize()`: `layer_.drawableSize = CGSizeMake(window.drawableWidth(),
  window.drawableHeight());`
* `readPixels()`: `return false;` with a one-shot log line (ADR 0026).
* `apiName()`: `"sokol-metal"`.
* `destroy()`: `drawable_ = nil; layer_ = nil; device_ = nil;
  SDL_Metal_DestroyView(view_);`

Link: `Metal`, `QuartzCore`, `Foundation` frameworks.

### 3.4 `SgEnvironmentD3D11` — implement in group G7, explicitly untestable here

`D3D11CreateDeviceAndSwapChain` on the `HWND` from
`SDL_GetWindowWMInfo(...).info.win.window`, `DXGI_FORMAT_B8G8R8A8_UNORM`, one buffer,
`DXGI_SWAP_EFFECT_DISCARD`, feature level 11_0 with 10_1/10_0 fallback. A single
`ID3D11RenderTargetView` on buffer 0, recreated in `resize()` after
`ResizeBuffers`. No depth-stencil view (§0.3). `present()` → `Present(vsync ? 1 : 0,
0)`. `readPixels()` → `false`. `environment()` → `{ .defaults = { BGRA8, NONE, 1 },
.d3d11 = { device, device_context } }`. `acquireSwapchain()` → `{ w, h, 1, BGRA8, NONE,
.d3d11.render_view = rtv }`.

Until G7 lands, `createSgEnvironment()` compiled with `SOKOL_D3D11` must return
`nullptr` and log
`"sokol: the D3D11 environment is not implemented yet; configure with -DDOOM2RPG_SOKOL_BACKEND=glcore"`.
The same shape applies in reverse for any backend define we have not implemented — a
clear message, never a crash and never a silent black screen.

---

## 4. Shaders

Two hand-written annotated-GLSL files under `new_src/render/sokol/shaders/`. Both were
compiled successfully with the real `sokol-shdc` for all eight target languages; the
text below is the verified dialect.

### 4.1 `shaders/quad2d.glsl` — the 2D layer, 3 programs on one vertex shader

Port of the three inline programs in `new_src/render/gl/GlDraw2D.cpp:15-76`. The
vertex layout is unchanged: `vec2 pos; vec2 uv0; vec4 color0;` — 32 bytes, matching
`GlDraw2D::Vertex`.

```glsl
@vs vs2d
layout(binding=0) uniform vs2d_params {
    vec4 canvas_size;   // .xy = canvas pixels (480,320); .zw unused
};
in vec2 pos;
in vec2 uv0;
in vec4 color0;
out vec2 uv;
out vec4 color;
void main() {
    uv = uv0;
    color = color0;
    // Canvas origin is top-left, clip space +Y is up: flip Y.
    float nx = pos.x / canvas_size.x * 2.0 - 1.0;
    float ny = 1.0 - pos.y / canvas_size.y * 2.0;
    gl_Position = vec4(nx, ny, 0.0, 1.0);
}
@end

@fs fs2d_indexed
layout(binding=1) uniform fs2d_params { vec4 color_mod; };
layout(binding=0) uniform texture2D tex;   // R8 index texture
layout(binding=1) uniform texture2D pal;   // RGBA8 palette LUT, 256x1
layout(binding=0) uniform sampler smp;
in vec2 uv;
in vec4 color;
out vec4 frag_color;
void main() {
    float index = texture(sampler2D(tex, smp), uv).r;
    frag_color = texture(sampler2D(pal, smp), vec2(index, 0.5)) * color * color_mod;
}
@end

@fs fs2d_rgba   // same, minus the palette indirection
@fs fs2d_color  // frag_color = color * color_mod;

@program quad_indexed vs2d fs2d_indexed
@program quad_rgba    vs2d fs2d_rgba
@program quad_color   vs2d fs2d_color
```

Generated names (verbatim from the test run) — `quad2d.glsl.h`:

```
ATTR_quad_indexed_pos    = 0   ATTR_quad_indexed_uv0 = 1   ATTR_quad_indexed_color0 = 2
(identical numbering for quad_rgba / quad_color)
UB_vs2d_params = 0    UB_fs2d_params = 1
VIEW_tex = 0          VIEW_pal = 1          SMP_smp = 0
vs2d_params_t { float canvas_size[4]; }      fs2d_params_t { float color_mod[4]; }
sg_shader_desc* quad_indexed_shader_desc(sg_backend);   // and _rgba / _color
```

Note the two structural changes forced by sokol-shdc, both cosmetic:

1. `uCanvasSize` was a `vec2`; it becomes `vec4 canvas_size` (`.xy` used) so the
   uniform block is a clean 16-byte multiple on every backend.
2. `uColorMod` moves from a loose uniform into the `fs2d_params` block — sokol has no
   loose uniforms.

The 1×1 white texture used for `fillQuad` (`GlDraw2D.cpp:118-128`) disappears:
`fillQuad` records a `quad_color` draw, which samples nothing.

### 4.2 `shaders/world.glsl` — the world program

Port of `new_src/render/gl/GlScene3D.cpp:15-56`. Vertex layout unchanged:
`vec3 pos; vec2 uv0;` — 20 bytes, matching `newcore::WorldVertex`.

```glsl
@vs vs_world
layout(binding=0) uniform vs_world_params {
    mat4 mvp;
    mat4 view;        // eye-space depth for the fog
    vec4 depth_fix;   // .x/.y remap clip z; (1,0) on GL, (0.5,0.5) on Metal/D3D11 (§0.4)
};
in vec3 pos;
in vec2 uv0;
out vec2 uv;
out float fog_depth;
void main() {
    uv = uv0;
    vec4 eye = view * vec4(pos, 1.0);
    fog_depth = -eye.z;
    vec4 p = mvp * vec4(pos, 1.0);
    p.z = p.z * depth_fix.x + p.w * depth_fix.y;
    gl_Position = p;
}
@end

@fs fs_world
layout(binding=1) uniform fs_world_params {
    vec4 color_mod;
    vec4 fog_color;
    vec4 fog_params;   // .x = start, .y = end, .z = enabled (0/1), .w unused
};
layout(binding=0) uniform texture2D tex;
layout(binding=1) uniform texture2D pal;
layout(binding=0) uniform sampler smp;
in vec2 uv;
in float fog_depth;
out vec4 frag_color;
void main() {
    float index = texture(sampler2D(tex, smp), uv).r;
    vec4 col = texture(sampler2D(pal, smp), vec2(index, 0.5));
    // Fixed-pipeline GL_MODULATE (src/GLES.cpp:620), BEFORE fog, as ADR 0019 requires.
    col *= color_mod;
    if (fog_params.z != 0.0) {
        // Fog touches RGB only, so transparent billboard texels stay transparent.
        float f = clamp((fog_params.y - fog_depth) /
                        max(fog_params.y - fog_params.x, 1e-6), 0.0, 1.0);
        col.rgb = mix(fog_color.rgb, col.rgb, f);
    }
    frag_color = col;
}
@end

@program world vs_world fs_world
```

Generated names: `ATTR_world_pos = 0`, `ATTR_world_uv0 = 1`, `UB_vs_world_params = 0`,
`UB_fs_world_params = 1`, `VIEW_tex = 0`, `VIEW_pal = 1`, `SMP_smp = 0`,
`vs_world_params_t { float mvp[16]; float view[16]; float depth_fix[4]; }`,
`fs_world_params_t { float color_mod[4]; float fog_color[4]; float fog_params[4]; }`,
`world_shader_desc(sg_backend)`.

**Fog is per-pixel again, on every backend. ADR 0022 ("fog is backend-dependent") has
no subject left and becomes historical.**

The sky reuses the `world` program with an identity `mvp`, exactly as
`GlScene3D::drawSky` does (`GlScene3D.cpp:221-258`), including the
`color_mod = (1,1,1,1)` reset that ADR 0019 point 4 demands.

### 4.3 CMake wiring for the shaders

In `new_src/CMakeLists.txt`, before `dr_render_sokol` is defined:

```cmake
include("${PROJECT_SOURCE_DIR}/../third_party_libs/sokol-tools-bin/sokol_shaders.cmake")
set(SG_SLANG "glsl410:glsl300es:metal_macos:metal_ios:metal_sim:hlsl5")
sokol_shader("render/sokol/shaders/quad2d.glsl" "${SG_SLANG}")
sokol_shader("render/sokol/shaders/world.glsl"  "${SG_SLANG}")
set(SG_GENERATED
    "${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/render/sokol/shaders/quad2d.glsl.h"
    "${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/render/sokol/shaders/world.glsl.h")
```

and `${SG_GENERATED}` goes into `add_library(dr_render_sokol ...)`'s source list, with
`target_include_directories(dr_render_sokol PRIVATE
"${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/render/sokol/shaders")` so `SgShaders.h`
can `#include "quad2d.glsl.h"`.

`wgsl` and `spirv_vk` are deliberately **not** in `SG_SLANG`: we never build
`SOKOL_WGPU`/`SOKOL_VULKAN`, and each extra language roughly doubles the generated
header. Both compile fine if someone wants them later (verified).

**If the host has no shdc binary** (Windows ARM64, 32-bit Windows, an exotic host),
`sokol_shaders.cmake` raises `FATAL_ERROR` during configure. That is the accepted
behavior: shaders are build artifacts, not repository content, so there is no
"pre-generated header" fallback and no generated code under `new_src/`. Document the
message in `docs/status.md` when it first bites someone.

---

## 5. The sokol backend

### 5.1 `SgRenderBackend::initialize(Window&)`

```cpp
env_ = createSgEnvironment();
if (!env_ || !env_->create(window)) return false;   // the env logged why

sg_desc desc = {};
desc.environment       = env_->environment();
desc.buffer_pool_size  = 8;
desc.image_pool_size   = 8192;    // 2 images per indexed texture (§0.6)
desc.view_pool_size    = 8192;    // 2 views  per indexed texture
desc.sampler_pool_size = 8;
desc.shader_pool_size  = 8;
desc.pipeline_pool_size= 32;
desc.uniform_buffer_size = 8 * 1024 * 1024;
desc.logger.func       = /* a small forwarder to fprintf(stderr, ...) */;
sg_setup(&desc);
if (!sg_isvalid()) return false;
```

`desc.logger.func` is mandatory in spirit: without it sokol validation failures are
silent aborts. Wire it to `stderr` on day one.

Then the R8 sanity check (§0.1):

```cpp
if (!sg_query_pixelformat(SG_PIXELFORMAT_R8).sample) {
    fprintf(stderr, "sokol: SG_PIXELFORMAT_R8 is not sampleable on this device\n");
    return false;
}
```

Then `textures_.initialize()`, `pipelines_.initialize()`, `draw2d_.initialize(...)`,
`scene3d_.initialize(...)`. Log one line mirroring the GL backend's
(`GlRenderBackend.cpp:12-16`): `sokol_gfx backend: <apiName> | swapchain <w>x<h>`.

`shutdown`: `sg_shutdown()` then `env_->destroy()`. Add an explicit `shutdown()` path
(the GL backend relies on process exit); `RenderBackend`'s destructor is virtual, so
put it in `~SgRenderBackend`.

### 5.2 Samplers — exactly two, created once

| Sampler | Config | Used by |
|---|---|---|
| `smpClamp_` | `min_filter = mag_filter = SG_FILTER_NEAREST`, `mipmap_filter = SG_FILTER_NONE`, `wrap_u = wrap_v = SG_WRAP_CLAMP_TO_EDGE` | all sprites, all 2D, every palette LUT |
| `smpRepeat_` | same filters, `wrap_u = wrap_v = SG_WRAP_REPEAT` | textures created with `TextureFlags::Tiled` (world walls/floors/ceilings, the sky) |

This reproduces `GlTexture::uploadIndices`'s `GL_NEAREST` + conditional `GL_REPEAT`
(`new_src/render/gl/GlTexture.cpp`). Palettes are **always** clamp+nearest.

### 5.3 `SgTexture` / `SgTextureStore`

`SgTextureStore` is a straight transliteration of `GlTextureStore`
(`new_src/render/gl/GlTextureStore.{h,cpp}`) — same slot vector of
`std::unique_ptr<SgTexture>`, same free list, same "slot 0 is never handed out", same
`lookup()` returning a raw `const SgTexture*` that stays valid across later creations
(sprite textures are created mid-frame by `World3D::ensureSpriteTexture`).

`SgTexture` members: `sg_image indexImg_, palImg_; sg_view indexView_, palView_;
sg_sampler smp_; int width_, height_; Format format_ { Indexed, Rgba };`

`uploadIndexed(indices, w, h, palette565, count, transparent, tiled)`:

1. Expand the palette to 256 RGBA8 entries with the existing shared helper in
   `render/api/PixelConvert` — the same call `GlTexture::uploadPalette` makes, so the
   0xF81F transparent key and the 5/6/5 bit replication cannot drift between backends.
2. `indexImg_ = sg_make_image(&{ .width = w, .height = h, .pixel_format =
   SG_PIXELFORMAT_R8, .data.mip_levels[0] = { indices, w*h } });`
   (immutable usage, the default — every texture in this game is uploaded once).
3. `palImg_ = sg_make_image(&{ .width = 256, .height = 1, .pixel_format =
   SG_PIXELFORMAT_RGBA8, .data.mip_levels[0] = { lut, 1024 } });`
4. Two `sg_make_view(&{ .texture.image = ... })`.
5. `smp_ = tiled ? smpRepeat_ : smpClamp_;`

`uploadRgba(rgba, w, h)`: one `SG_PIXELFORMAT_RGBA8` image + view, `format_ = Rgba`,
`palView_` invalid.

`textureBytes()` must count **exactly** what `GlTextureStore::bytesOf` counts
(`GlTextureStore.cpp:72-80`): `w*h` index bytes + 1024 palette bytes for indexed,
`w*h*4` for RGBA. The log line in `Main.cpp:293-296` is a cross-backend comparison; if
the two numbers differ, one of the stores is wrong.

`destroy(id)`: `sg_destroy_view` both views, `sg_destroy_image` both images. Never
destroy the shared samplers.

### 5.4 `SgPipelines` — 4 blend variants × 4 programs

`new_src/render/api/RenderModes.cpp` has 14 rows but only **four** distinct
`(src, dst)` pairs:

| Variant | `src` / `dst` | Legacy rows |
|---|---|---|
| `AlphaBlend` | `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA` | 0, 1, 2, 8, 9, 11, 12, 13 |
| `Additive` | `SRC_ALPHA` / `ONE` | 3, 4, 5, 6 |
| `Subtractive` | `ZERO` / `ONE_MINUS_SRC_COLOR` | 7 |
| `WriteNothing` | `ZERO` / `ONE` | 10 |

`int blendVariant(int renderMode)` lives in `SgPipelines.cpp` and is derived from
`kRenderModes[mode].src/.dst` at init (a 14-entry lookup built once), so it can never
drift from the shared table. The `mod[4]` factor and the `fog` flag keep coming from
`kRenderModes` — they are uniforms, not pipeline state.

`sg_blend_state` per variant:

```cpp
{ .enabled = true,
  .src_factor_rgb = <src>, .dst_factor_rgb = <dst>, .op_rgb = SG_BLENDOP_ADD,
  .src_factor_alpha = <src_a>, .dst_factor_alpha = <dst_a>, .op_alpha = SG_BLENDOP_ADD }
```

For three of the four variants the alpha factors equal the RGB factors, which is what
`glBlendFunc` means. **`Subtractive` is the one deviation:** its RGB factors stay
`(ZERO, ONE_MINUS_SRC_COLOR)`, but the alpha factors become
`(ZERO, ONE_MINUS_SRC_ALPHA)`. D3D11 does not accept a colour-typed blend factor in the
alpha slot, and the swapchain's destination alpha is never read by anything, so the
visible RGB result is bit-identical while the pipeline stays portable. Comment it at the
table.

Shared pipeline fields for all 16:

```cpp
.primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
.index_type     = SG_INDEXTYPE_NONE,
.cull_mode      = SG_CULLMODE_NONE,          // src/GLES.cpp:89, no culling ever
.sample_count   = 1,
.depth          = { .pixel_format = SG_PIXELFORMAT_NONE,
                    .compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false },
.color_count    = 1,
.colors[0]      = { .pixel_format = <swapchain color format>,
                    .write_mask = SG_COLORMASK_RGBA, .blend = <variant> },
```

`colors[0].pixel_format` must match the swapchain (`RGBA8` on GL, `BGRA8` on
Metal/D3D11); take it from `env_->environment().defaults.color_format`. Pipelines are
created lazily on first use and cached in a `[program][variant]` array — 16 slots,
created once, never destroyed before `sg_shutdown`.

Vertex layouts:

```cpp
// 2D programs: stride 32
.layout.buffers[0].stride = 32,
.layout.attrs[0] = { .offset =  0, .format = SG_VERTEXFORMAT_FLOAT2 },  // pos
.layout.attrs[1] = { .offset =  8, .format = SG_VERTEXFORMAT_FLOAT2 },  // uv0
.layout.attrs[2] = { .offset = 16, .format = SG_VERTEXFORMAT_FLOAT4 },  // color0
// world program: stride 20
.layout.buffers[0].stride = 20,
.layout.attrs[0] = { .offset =  0, .format = SG_VERTEXFORMAT_FLOAT3 },  // pos
.layout.attrs[1] = { .offset = 12, .format = SG_VERTEXFORMAT_FLOAT2 },  // uv0
```

### 5.5 `SgFrame` — the per-frame command list (ADR 0025)

```cpp
class SgFrame {
public:
	static constexpr size_t kVertexBytes = 4u * 1024u * 1024u;   // GPU + CPU staging

	struct Uniforms {                 // one copy per draw command, ~176 bytes
		float mvp[16];                // world only; identity for 2D (unused there)
		float view[16];
		float depthFix[4];
		float colorMod[4];
		float fogColor[4];
		float fogParams[4];
		float canvasSize[4];
	};

	enum class Kind : uint8_t { Viewport, Scissor, Draw };

	struct Cmd {
		Kind kind;
		// Viewport / Scissor (drawable pixels, TOP-LEFT origin):
		int x, y, w, h;
		// Draw:
		sg_pipeline pip;
		sg_view texView, palView;     // invalid handles allowed (quad_color)
		sg_sampler smp;
		int vertexOffset;             // BYTE offset into the frame vertex buffer
		int vertexCount;
		int uniformIndex;             // index into uniforms_
		bool world;                   // which uniform-block pair to apply
	};

	bool initialize();                // creates the write-transient sg_buffer
	void beginFrame();                // clears staging + commands, resets overflow flag
	// Copies `bytes` into the staging vector; returns the byte offset, or -1 on overflow.
	int appendVertices(const void* data, size_t bytes);
	int addUniforms(const Uniforms& u); // returns uniformIndex
	void record(const Cmd& c);
	// One sg_write_buffer_transient + replay of every command. Called from
	// SgRenderBackend::endFrame INSIDE the pass.
	void replay();
	sg_buffer buffer() const;
};
```

Rules, all of them load-bearing:

* `appendVertices` overflow (> `kVertexBytes`) drops the vertices, returns -1, and logs
  **once per frame**; the caller must skip recording the command. A dropped frame is
  visible, which is what we want — silence would not be.
* The staging vector is `reserve`d to `kVertexBytes` at `initialize()`, so no
  reallocation happens mid-frame.
* `replay()` is the only code in the whole program that calls `sg_apply_*` / `sg_draw`.
* `sg_begin_pass` resets viewport and scissor to the full framebuffer
  (`sokol_gfx.h:289-290`), so the backend records the letterbox `Viewport` command
  first, every frame.
* Draw commands with `vertexCount == 0` are never recorded.

The vertex buffer:

```cpp
sg_make_buffer(&(sg_buffer_desc){
    .size  = SgFrame::kVertexBytes,
    .usage = { .vertex_buffer = true, .write_transient = true },
    .label = "doom2rpg-frame-vertices" });
```

One buffer serves both vertex layouts: offsets of 32-byte and 20-byte strides are all
multiples of 4, which is what `sg_bindings.vertex_buffer_offsets` requires.

### 5.6 `SgDraw2D`

Transliteration of `GlDraw2D` (`new_src/render/gl/GlDraw2D.cpp`) with the GL calls
replaced by `SgFrame::record`:

* State kept between flushes is unchanged: `boundTex_`, `boundIndexed_`, `renderMode_`,
  `scissorRect_`/`scissorActive_`, `letterbox_[4]`, `vertices_` (host-side `Vertex`
  vector), `canvasWidth_/Height_`.
* `drawQuad` / `fillQuad` are unchanged, including the shared `quadCorners()` call
  (`render/api/QuadUV.h`) — that is exactly the code that made the 2D layer
  pixel-identical between backends; do not touch it.
* `flush()` becomes: pick the program (`quad_indexed` / `quad_rgba` / `quad_color`
  by `boundTex_`/`boundIndexed_`), pick the variant via `blendVariant(renderMode_)`,
  `appendVertices(vertices_)`, build `Uniforms{ canvasSize, colorMod = kRenderModes[renderMode_].mod }`,
  `record({ Kind::Draw, ... })`, clear `vertices_` and the bound texture.
* `setClipCanvas` / `clearClip` / `setLetterbox` keep the existing flush-then-change
  ordering, and record a `Kind::Scissor` command.

**Scissor arithmetic must be copied verbatim from `GlDraw2D::applyScissor`
(`GlDraw2D.cpp:206-232`), with the y flip removed** (sokol takes top-left rects):

```cpp
const float sx = (float)letterbox_[2] / (float)canvasWidth_;
const float sy = (float)letterbox_[3] / (float)canvasHeight_;
x = letterbox_[0] + (int)lroundf(cx * sx);
y = letterbox_[1] + (int)lroundf(cy * sy);      // top-left, no flip
w = max(0, (int)lroundf(cw * sx));
h = max(0, (int)lroundf(ch * sy));
// Degenerate rect clips everything away, it is NOT "no clip":
if (cw <= 0 || ch <= 0) { x = y = w = h = 0; }
```

Do **not** substitute `CanvasViewport::canvasSubRect` here: it truncates where
`applyScissor` rounds (`CanvasViewport.cpp:38-49`), and a one-pixel difference in the
menu scrollbar clip would show up as a false positive in the F12 diff.

`clearClip()` records a `Scissor` command covering the whole drawable
(`0, 0, drawableW, drawableH`).

### 5.7 `SgScene3D`

Transliteration of `GlScene3D` (`new_src/render/gl/GlScene3D.cpp`):

* `beginScene(view)` stores `mvp`/`view` for the frame's uniform blocks, clears
  `vertices_`, resets `currentTex_`/`currentRenderMode_ = 0`/`currentFogOn_ =
  fogEnabled_`. It records **no** GPU state — the pipeline carries it.
* `setTexture` / `setRenderMode` keep the identical flush-compare-assign caches, with
  the same early-outs, so the batch boundaries — and therefore the draw order — are
  bit-identical to the GL reference.
* `submitTriangles` appends to the host vector exactly as today, flushing at
  `kMaxVerts = 65536`.
* `flush()` appends the vertices and records one `Draw` with program `world`, variant
  `blendVariant(currentRenderMode_)`, and
  `Uniforms{ mvp, view, depthFix, colorMod = kRenderModes[m].mod, fogColor,
  fogParams = { fogStart_, fogEnd_, currentFogOn_ ? 1.f : 0.f, 0.f } }`.
* `depthFix` is `{1,0,0,0}` under `SOKOL_GLCORE`/`SOKOL_GLES3` and `{0.5f,0.5f,0,0}`
  otherwise, decided by `#if` in `SgScene3D.cpp` (§0.4).
* `drawSky(sky, uOffset)` keeps the NDC quad and the UV expression of
  `GlScene3D.cpp:236-252` verbatim, with identity `mvp`, `view` = identity,
  `colorMod = (1,1,1,1)` and fog off, recorded as a plain `AlphaBlend` `world` draw.
* `endScene()` flushes. There is no blend-state restoration to do any more — that whole
  class of "a leftover ADD50 darkened the next thing" bug disappears with pipelines.

### 5.8 `SgRenderBackend` frame flow

```
beginFrame(window):
    applyViewport(window)          // latches vp_ from window.computeViewport()
    frame_.beginFrame()
    frame_.record(Viewport{vp_})   // sg_begin_pass resets it, so record it first
    draw2d_.begin()

setCanvasViewport(x,y,w,h):
    draw2d_.flush()
    CanvasViewport::canvasSubRect(x, y, w, h, /*glBottomUp=*/false, ...)
    frame_.record(Viewport{...})
restoreCanvasViewport(window):
    draw2d_.flush(); applyViewport(window); frame_.record(Viewport{vp_})

endFrame(window):
    draw2d_.end()                              // final flush
    sg_swapchain sc = env_->acquireSwapchain(window)
    sg_pass pass = { .action = { .colors[0] = { .load_action = SG_LOADACTION_CLEAR,
                                                .clear_value = {0,0,0,1} } },
                     .swapchain = sc }
    sg_begin_pass(&pass)
    frame_.replay()
    sg_end_pass()
    sg_commit()
    if (capturePath_ non-empty) writeCapture()   // GL only, ADR 0026
    env_->present(window)
```

`flushFrame()` (used by the loading screen) = `draw2d_.end()` plus the same
pass/replay/commit/present sequence, then `frame_.beginFrame()` and a fresh `Viewport`
record so the next `beginFrame` is not required.

`letterboxRect` / `drawableToCanvas` delegate to `vp_` exactly as
`GlRenderBackend.cpp:32-48` does. `name()` returns `"sokol"` (the capture file name
prefix); `env_->apiName()` goes into the log and the window title.

`writeCapture()`: same shape as `GlRenderBackend::writeCapture`
(`GlRenderBackend.cpp:95-119`) but the readback goes through
`env_->readPixels(vp_.x, vp_.y, vp_.w, vp_.h, buf)`; when that returns false, log
`"capture: not supported on the <apiName> environment"` once and clear the request.

**Note the capture is on the GL bottom-left-origin rect.** `readPixels` is specified to
return bottom-row-first RGBA, so `writeBmp24(..., bottomUp = true)` stays as is and the
BMP is byte-comparable with the `dr_render_gl` captures.

---

## 6. Changes outside `render/sokol/`

### 6.1 `new_src/platform/Window.{h,cpp}`

1. **`GraphicsApi::SdlRender` is deleted** along with `sdlRenderer_`,
   `SDL_CreateRenderer`, `SDL_RenderPresent`, `SDL_RenderSetVSync`,
   `SDL_GetRendererOutputSize` and `sdlRenderer()`. The enum becomes
   `enum class GraphicsApi { OpenGL, Metal, D3D11 }`.
2. **GL request rises to 4.1 core, on every platform** (§0.7):
   `SDL_GL_CONTEXT_MAJOR_VERSION 4`, `MINOR 1`, `PROFILE_MASK
   SDL_GL_CONTEXT_PROFILE_CORE` — the `#else` compatibility-profile branch at
   `Window.cpp:48-51` goes away. Drop `SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24)`: we
   have no depth buffer (§0.3).
3. New window flags per API: `SDL_WINDOW_OPENGL` for `OpenGL`, `SDL_WINDOW_METAL` for
   `Metal`, none for `D3D11`.
4. `refreshDrawableSize()`: `SDL_GL_GetDrawableSize` for `OpenGL`,
   `SDL_Metal_GetDrawableSize` for `Metal`, `SDL_GetWindowSizeInPixels`-equivalent
   (`SDL_GetWindowSize` scaled, SDL2 has no better call for D3D11 — use
   `SDL_GetWindowSize`) for `D3D11`.
5. `present()`: `SDL_GL_SwapWindow` only for `OpenGL`; **for `Metal` and `D3D11` it does
   nothing** — the `SgEnvironment` owns presentation. Better: make `present()`
   private-by-convention and have `SgEnvironmentGl::present()` call
   `SDL_GL_SwapWindow(window.nativeHandle())` directly, so presentation lives in exactly
   one place per API. Keep `Window::present()` as a no-op-for-non-GL shim only if
   `GlRenderBackend` still needs it during the transition (it does, until G8).
6. `applyVSync()`: GL path unchanged; Metal/D3D11 leave it to the environment (Metal:
   `layer_.displaySyncEnabled`; D3D11: the `Present` interval). Acceptable to leave
   vsync as "always on" for Metal/D3D11 in G6/G7 and note it.
7. `SDL_WINDOW_ALWAYS_ON_TOP` (`Window.cpp:55-56`) — leave it; known issue #8, out of
   scope.

### 6.2 `new_src/core/RenderBackendFactory.{h,cpp}`

`enum class BackendKind { OpenGL, Sokol };` — `SdlRender` is deleted. `"gl"` →
`OpenGL`, `"sokol"` → `Sokol`. `AppContext` maps `BackendKind::Sokol` to the
`GraphicsApi` matching the compiled sokol backend; expose that mapping from
`dr_render_sokol` as

```cpp
// render/sokol/SgEnvironment.h
GraphicsApi sokolGraphicsApi();   // OpenGL | Metal | D3D11, per SOKOL_* define
```

so `AppContext` does not need any `#if`s. `backendKindAvailable(Sokol)` returns whether
the compiled environment is implemented (false for D3D11 before G7).

`DOOM2RPG_BACKEND` / `--backend=` keep working; the default is `gl` until the end of
group G5 and `sokol` afterwards.

### 6.3 `new_src/core/AppContext.cpp`

`const GraphicsApi api = (backendKind_ == BackendKind::OpenGL) ? GraphicsApi::OpenGL :
sokolGraphicsApi();` and the title suffix becomes
`"[" + backendKindName(kind) + "]"` as today (so `[gl]` vs `[sokol]`).

### 6.4 `new_src/CMakeLists.txt`

* `list(FILTER ... EXCLUDE REGEX "/render/(api|gl|sokol)/")` — `sdl` drops out, `sokol`
  joins. Note `*.mm` is not in the GLOB patterns at all, so the Metal files must be
  listed explicitly.
* Delete the `dr_render_sdl` target and its `target_link_libraries`/`foreach` entries.
* Add `dr_render_sokol` per §2 and §4.3, and its `SOKOL_*` compile definition per
  §1.2. On Apple: `SokolGfxImpl.mm` + `SgEnvironmentMetal.mm`, and
  `target_link_libraries(dr_render_sokol PUBLIC "-framework Metal" "-framework QuartzCore")`
  when the backend is `metal`.
* `target_include_directories(dr_render_sokol PRIVATE
  "${PROJECT_SOURCE_DIR}/../third_party_libs/sokol")`.
* Add `dr_render_sokol` to the `foreach(tgt ...)` block that sets C++17 and the dialect
  flags.

### 6.5 Explicitly NOT touched

Game logic; `domain/`; `io/`; `ui/`; `text/`; `render/World3D` (geometry only, and it
never learns about any API); `render/SceneRenderer`; `render/Camera3D`;
`render/Graphics2D`; the whole `render/api/` interface set (`Draw2D`, `Scene3D`,
`TextureStore`, `RenderModes`, `PixelConvert`, `QuadUV`, `CanvasViewport`, `BmpWriter`,
`Texture`, `TextureId`); the accepted deviations (torchiere halo over the lamp, fire
over the scorch mark — both in shared code); the coordinate debug overlay; the F12 key
binding.

---

## 7. What dies with the SDL_Render backend — the honest list

| Thing | Fate |
|---|---|
| `new_src/render/sdl/` (10 files, `SdlRenderBackend`, `SdlDraw2D`, `SdlScene3D`, `SdlTextureStore`, `SdlBlendModes`, `SdlCommon.h`) | deleted |
| CMake target `dr_render_sdl` | deleted |
| `BackendKind::SdlRender`, `--backend=sdl`, `GraphicsApi::SdlRender`, `Window::sdlRenderer()` | deleted |
| The G7.1 adaptive clip-space tessellation, its `DOOM2RPG_SDL_TESS` / `DOOM2RPG_GFX_STATS` knobs, and the off-screen-geometry rejection | deleted — sokol gives hardware perspective correction on every backend, so there is nothing left to tessellate against |
| The CPU vertex pipeline (near clip, NDC→canvas, object-space UV tile split for CLAMP_TO_EDGE textures), per-vertex fog and the haze pass | deleted |
| ADR 0021 (SDL as a second backend), ADR 0022 (fog is backend-dependent), ADR 0023 (adaptive tessellation) | status → **historical**, superseded by ADR 0024. Keep the files: they document *why* we know what we know. |
| Spec `2026-09-07-sdl-tessellation.md` | status → historical |
| The 59.1 MB RGBA texture expansion the user objected to | gone; only the 15.1 MB indexed path remains |

**G5/G6/G7.1 were not wasted.** They are the reason this migration is a
transliteration instead of a redesign: building a completely different backend against
`Draw2D`/`Scene3D` proved the abstraction does not leak, and it produced the
pixel-identical 2D layer, the shared `QuadUV`/`PixelConvert`/`CanvasViewport`/
`RenderModes` helpers and the F12 capture tool — all four of which this spec leans on
directly. The tessellation work also produced the measurement that killed the SDL path
on its own merits.

---

## 8. Implementation groups

Each group ends with a buildable, playable game. The raw-GL backend (`dr_render_gl`,
`--backend=gl`) stays alive and untouched through G1–G7 and is the pixel reference; it
is removed only in G8, after the user has signed off on both the sokol-GL and the
sokol-Metal pictures.

Build after every group: `cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug` (new files
⇒ reconfigure, the project uses GLOB) then `cmake --build build_new -j 8`.

### G1 — Retire the SDL_Render backend

Touch: delete `new_src/render/sdl/` (all 10 files); `new_src/CMakeLists.txt`;
`new_src/core/RenderBackendFactory.{h,cpp}`; `new_src/core/AppContext.cpp`;
`new_src/platform/Window.{h,cpp}` (items 1, 2, 3, 4, 5 of §6.1 — the `GraphicsApi` enum
keeps only `OpenGL` for now; `Metal`/`D3D11` arrive in G6/G7).

Acceptance:
* `--backend=sdl` now prints "backend 'sdl' is not built in this binary; falling back to
  'gl'" (or the unknown-value message) and the game runs on GL.
* `glGetString(GL_VERSION)` in the startup log shows 4.1 (or higher) core.
* **On screen:** absolutely no change. Take an F12 capture of the main menu and of a
  gameplay frame *before* the change and diff them against captures taken after — they
  must be byte-identical.

### G2 — Vendor sokol, wire CMake and shdc, black-screen sokol backend

Touch: `third_party_libs/sokol/**`, `third_party_libs/sokol-tools-bin/**`,
`new_src/CMakeLists.txt`, `new_src/render/sokol/{SgCommon.h, SokolGfxImpl.cpp,
SokolGfxImpl.mm, SgEnvironment.h, SgEnvironmentGl.cpp, SgRenderBackend.{h,cpp},
shaders/quad2d.glsl, shaders/world.glsl}`,
`new_src/core/RenderBackendFactory.{h,cpp}`, `new_src/core/AppContext.cpp`.

`SgRenderBackend` in this group implements only `initialize` / `beginFrame` /
`endFrame` / `letterboxRect` / `drawableToCanvas` / `name` / `requestCapture`; the three
device accessors return stub `Draw2D`/`Scene3D`/`TextureStore` objects whose methods do
nothing (a file-local `SgNullDevices` is fine) so the game's call sites keep working.

Acceptance:
* Configure succeeds; the build log shows `Compile shader: render/sokol/shaders/world.glsl`
  and the two headers appear under `build_new/new_src/compile_shaders/`.
* `--backend=gl` — the game, unchanged.
* **On screen with `--backend=sokol`:** a black letterboxed window, title `[sokol]`,
  log line `sokol_gfx backend: sokol-gl | swapchain WxH`, and no sokol validation
  errors on stderr. Keys still quit the app.

### G3 — `SgTextureStore` + `SgTexture` + samplers

Touch: `new_src/render/sokol/{SgTexture.{h,cpp}, SgTextureStore.{h,cpp}}`,
`SgRenderBackend.cpp` (replace the stub store).

Acceptance:
* `--backend=sokol` startup log prints
  `renderer: sokol, textures: N bytes (X.Y MB)` with **N exactly equal** to the
  `--backend=gl` run's N (≈15.1 MB).
* No sokol pool-exhaustion warnings ("image pool exhausted", "view pool exhausted").
* On screen: still black.

### G4 — `SgFrame` + `SgPipelines` + `SgDraw2D`: the 2D layer

Touch: `new_src/render/sokol/{SgFrame.{h,cpp}, SgShaders.h, SgPipelines.{h,cpp},
SgDraw2D.{h,cpp}}`, `SgRenderBackend.{h,cpp}` (real `draw2d()`, viewport/scissor
recording, `writeCapture` via `env_->readPixels`).

Acceptance:
* **On screen with `--backend=sokol`:** the loading screen, the HUD, the in-game menu
  with its scrollbar and cursor, dialog boxes, the view weapon and the fade overlays all
  render, in the right colours, with the right clipping — everything except the 3D band,
  which stays black.
* F12 on the main menu with `--backend=gl` and with `--backend=sokol` (built with
  `DOOM2RPG_SOKOL_BACKEND=glcore`) must produce **byte-identical** BMPs
  (`capture-gl-000.bmp` vs `capture-sokol-000.bmp`). The menu cursor animation is what
  caught the last regression here, so capture on the same menu row.
* A clipped frame (menu with the scrollbar, or the view weapon) must also be
  byte-identical — that is the §5.6 `lroundf` clause earning its keep.

### G5 — `SgScene3D`: the world, the sky and the fog

Touch: `new_src/render/sokol/{SgScene3D.{h,cpp}}`, `SgRenderBackend.{h,cpp}`
(real `scene3d()`).
Last step of this group, after the user confirms: flip the default backend to `sokol`
in `Main.cpp`.

Acceptance:
* **On screen with `--backend=sokol`:** the full 3D view — walls, floors, ceilings, the
  sky band with its yaw scroll, sprites and stacked characters, the torchiere halo, the
  tile-212 subtractive override, and the additive/blend modes all matching the GL
  backend.
* F12 on a gameplay frame, `gl` vs `sokol(glcore)`: byte-identical, or the deltas are
  understood and reported. If they are not identical, suspect (a) the `depth_fix` value
  on the GL build (must be exactly `(1,0)`), (b) a batch-boundary difference in
  `setTexture`/`setRenderMode`, (c) the mode-7 alpha-factor deviation of §5.4.
* Turn fog on temporarily (`world.setFog(0xFF1A2A1A, 500, 900)`,
  `Main.cpp:300-302`) and confirm it is smooth per-pixel, then revert.

### G6 — The Metal environment

Touch: `new_src/render/sokol/{SgEnvironmentMetal.mm, SgEnvironment.h (factory)}`,
`new_src/platform/Window.{h,cpp}` (`GraphicsApi::Metal`, `SDL_WINDOW_METAL`,
`SDL_Metal_GetDrawableSize`, no-op present), `new_src/CMakeLists.txt` (default
`DOOM2RPG_SOKOL_BACKEND=metal` on Apple, framework links).

Acceptance:
* Configure with the default on macOS, build, run: **on screen** the same game as the
  `glcore` build — this one is judged by eye (ADR 0026), so ask the user to compare the
  main menu, a gameplay frame and the loading screen.
* Window resize keeps the letterbox correct (the layer's `drawableSize` follows).
* No sokol validation errors; F12 prints "capture: not supported on the sokol-metal
  environment".
* `DOOM2RPG_SOKOL_BACKEND=glcore` still configures and still gives byte-identical
  captures against `dr_render_gl`.

### G7 — The D3D11 environment

Touch: `new_src/render/sokol/SgEnvironmentD3D11.cpp`, `new_src/CMakeLists.txt`
(default on Windows, `d3d11.lib`/`dxgi.lib`).

Acceptance: compiles on Windows and runs; **flag it in the journal as untested on this
machine** and have the user verify. Until verified, the Windows default stays `glcore`.

### G8 — Retire the raw GL backend

Touch: delete `new_src/render/gl/` (9 files, including `Shader.{h,cpp}` and
`GlCommon.h`); `new_src/CMakeLists.txt` (drop `dr_render_gl`, drop `OpenGL::GL` from the
executable when the sokol backend is not `glcore`); `new_src/core/RenderBackendFactory.*`
collapses to a single backend (keep `--backend=` parsing so the flag does not become an
error, or remove the flag entirely and log a deprecation line).

Do this only after the user has signed off on G5 (byte-identical) **and** G6 (looks
right on Metal).

Acceptance: the game builds and plays with only `dr_render_core` +
`dr_render_sokol`; texture memory still logs ≈15.1 MB; the startup log names the sokol
environment.

---

## 9. Risk register

| Risk | Mitigation in this spec |
|---|---|
| Vendored `sokol_gfx.h` and `sokol-tools-bin` drift apart | `VERSION.txt` in both, §1.1 pairing rule, `--genver 5` mismatch fails loudly at compile time |
| GL 4.1 core request breaks on an old Linux driver | The raw-GL reference also moves to 4.1; if a machine cannot give 4.1, neither backend runs and the failure is a clear `SDL_GL_CreateContext` error |
| The `depth_fix` remap is forgotten on Metal → the near half of the world vanishes | §0.4 is a numbered fact, the uniform is in the shader from G2, and G6's acceptance criterion is "the same game as the glcore build" |
| Draw order changes because a flush boundary moved | §5.6/§5.7 mandate transliteration of the existing flush-compare-assign caches, and G4/G5 demand byte-identical F12 captures |
| `SgFrame::kVertexBytes` too small on a text-heavy frame | Overflow is logged once per frame and visibly drops geometry; raise the constant if it fires |
| sokol pool exhaustion under many sprite textures | Pools set to 8192 images / 8192 views (§5.1); G3 acceptance explicitly checks for the warning |
| Losing the F12 diff tool on Metal | ADR 0026: capture stays GL-only and `DOOM2RPG_SOKOL_BACKEND=glcore` remains configurable forever, so the diff never becomes unavailable |
