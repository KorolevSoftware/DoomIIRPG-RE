#include "render/api/RenderBackend.h"

namespace newcore {

// Out of line so the vtable has exactly one home.
RenderBackend::~RenderBackend() = default;

} // namespace newcore
