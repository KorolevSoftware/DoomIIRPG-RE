#include "core/RenderBackendFactory.h"

#include <cstdio>
#include <cstring>

#include "render/api/RenderBackend.h"
// The only translation unit allowed to name concrete backends (spec §4.3).
#include "render/gl/GlRenderBackend.h"

namespace newcore {

const char* backendKindName(BackendKind kind) {
	switch (kind) {
	case BackendKind::OpenGL: return "gl";
	case BackendKind::SdlRender: return "sdl";
	}
	return "gl";
}

bool parseBackendKind(const char* text, BackendKind& out) {
	if (text == nullptr) return false;
	if (std::strcmp(text, "gl") == 0) {
		out = BackendKind::OpenGL;
		return true;
	}
	if (std::strcmp(text, "sdl") == 0) {
		out = BackendKind::SdlRender;
		return true;
	}
	return false;
}

bool backendKindAvailable(BackendKind kind) {
	// render/sdl/ arrives with spec group G5; until then only GL exists.
	return kind == BackendKind::OpenGL;
}

BackendKind resolveBackendKind(BackendKind requested) {
	if (backendKindAvailable(requested)) return requested;
	std::fprintf(stderr,
		"renderer: backend '%s' is not built yet (spec group G5); falling back to 'gl'\n",
		backendKindName(requested));
	return BackendKind::OpenGL;
}

std::unique_ptr<RenderBackend> createRenderBackend(BackendKind kind) {
	switch (kind) {
	case BackendKind::OpenGL:
		return std::make_unique<GlRenderBackend>();
	case BackendKind::SdlRender:
		return nullptr;
	}
	return nullptr;
}

} // namespace newcore
