#ifndef NEW_RENDER_SOKOL_SGENVIRONMENT_H
#define NEW_RENDER_SOKOL_SGENVIRONMENT_H

#include <cstdint>
#include <memory>

#include "platform/Window.h"
#include "render/sokol/SgCommon.h"

namespace newcore {

// Everything sokol_gfx refuses to do: create the 3D device, acquire a drawable,
// describe the swapchain, put the finished frame on screen, and read it back
// (spec 2026-09-11-sokol-gfx-backend §3). Exactly one implementation is
// compiled, chosen by the SOKOL_* backend define.
class SgEnvironment {
public:
	virtual ~SgEnvironment();

	// Creates the device/context/surface. The GL implementation finds the
	// context already made by Window; the Metal one (G6) creates the MTLDevice
	// and the CAMetalLayer; the D3D11 one (G7) creates device + swap chain.
	virtual bool create(Window& window) = 0;
	virtual void destroy() = 0;

	// Handed to sg_setup() via sg_desc.environment. Valid after create().
	virtual sg_environment environment() const = 0;

	// Called once per frame, immediately before sg_begin_pass(). On failure it
	// must return a struct with .invalid = true and everything else zeroed;
	// sokol then skips the pass (sokol_gfx.h:3044-3049).
	virtual sg_swapchain acquireSwapchain(Window& window) = 0;

	// Called after sg_commit(). GL swaps here; Metal must do NOTHING because
	// sokol already called presentDrawable (sokol_gfx.h:17207-17209).
	virtual void present(Window& window) = 0;

	// Called on a window size change, after Window refreshed its drawable size.
	virtual void resize(Window& window) = 0;

	// Letterbox readback for the F12 capture (ADR 0026). Writes w*h*4 RGBA
	// bytes, BOTTOM row first (BMP order). Returns false when the API cannot do
	// it, which is the honest answer on Metal and D3D11.
	virtual bool readPixels(int x, int y, int w, int h, uint8_t* rgbaBottomUp) = 0;

	// "sokol-gl" / "sokol-metal" / "sokol-d3d11": logs + window title + captures.
	virtual const char* apiName() const = 0;
};

// Built for whichever SOKOL_* define this target was compiled with. Returns
// nullptr (after logging) for a backend whose environment is not implemented
// yet, so the caller falls back instead of crashing.
std::unique_ptr<SgEnvironment> createSgEnvironment();

// Which window the compiled sokol backend needs. Keeps the #if out of
// AppContext (spec §6.2).
GraphicsApi sokolGraphicsApi();

// False while the compiled environment is still a stub (D3D11 before G7).
bool sokolEnvironmentImplemented();

} // namespace newcore

#endif // NEW_RENDER_SOKOL_SGENVIRONMENT_H
