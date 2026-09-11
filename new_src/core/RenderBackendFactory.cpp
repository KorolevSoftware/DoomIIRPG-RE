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
	}
	return "gl";
}

bool parseBackendKind(const char* text, BackendKind& out) {
	if (text == nullptr) return false;
	if (std::strcmp(text, "gl") == 0) {
		out = BackendKind::OpenGL;
		return true;
	}
	return false;
}

bool backendKindAvailable(BackendKind kind) {
	// The raw GL backend is the only one built; the sokol one arrives in G2.
	return kind == BackendKind::OpenGL;
}

BackendKind resolveBackendKind(BackendKind requested) {
	if (backendKindAvailable(requested)) return requested;
	std::fprintf(stderr,
		"renderer: backend '%s' is not built in this binary; falling back to 'gl'\n",
		backendKindName(requested));
	return BackendKind::OpenGL;
}

std::unique_ptr<RenderBackend> createRenderBackend(BackendKind kind) {
	switch (kind) {
	case BackendKind::OpenGL:
		return std::make_unique<GlRenderBackend>();
	}
	return nullptr;
}

} // namespace newcore
