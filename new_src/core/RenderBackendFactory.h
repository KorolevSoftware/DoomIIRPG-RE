#ifndef NEW_CORE_RENDERBACKENDFACTORY_H
#define NEW_CORE_RENDERBACKENDFACTORY_H

#include <memory>

namespace newcore {

class RenderBackend;

// Builds the render backend (spec 2026-09-02-render-backend-split §4.3). Only
// sokol is left since group G8 of spec 2026-09-11-sokol-gfx-backend retired
// render/gl/; its GPU API is a build-time choice (DOOM2RPG_SOKOL_BACKEND).
std::unique_ptr<RenderBackend> createRenderBackend();

} // namespace newcore

#endif // NEW_CORE_RENDERBACKENDFACTORY_H
