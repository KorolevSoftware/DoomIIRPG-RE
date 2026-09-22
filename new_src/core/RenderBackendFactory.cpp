#include "core/RenderBackendFactory.h"

#include "render/api/RenderBackend.h"
// The only translation unit allowed to name concrete backends (spec §4.3).
#include "render/sokol/SgRenderBackend.h"

namespace newcore {

std::unique_ptr<RenderBackend> createRenderBackend() {
	return std::make_unique<SgRenderBackend>();
}

} // namespace newcore
