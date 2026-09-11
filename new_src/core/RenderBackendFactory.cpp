#include "core/RenderBackendFactory.h"

#include <cstdio>
#include <cstring>

#include "render/api/RenderBackend.h"
// The only translation unit allowed to name concrete backends (spec §4.3).
#include "render/gl/GlRenderBackend.h"
#include "render/sokol/SgEnvironment.h"
#include "render/sokol/SgRenderBackend.h"

namespace newcore {

const char* backendKindName(BackendKind kind) {
	switch (kind) {
	case BackendKind::OpenGL: return "gl";
	case BackendKind::Sokol: return "sokol";
	}
	return "gl";
}

bool parseBackendKind(const char* text, BackendKind& out) {
	if (text == nullptr) return false;
	if (std::strcmp(text, "gl") == 0) {
		out = BackendKind::OpenGL;
		return true;
	}
	if (std::strcmp(text, "sokol") == 0) {
		out = BackendKind::Sokol;
		return true;
	}
	return false;
}

bool backendKindAvailable(BackendKind kind) {
	switch (kind) {
	case BackendKind::OpenGL: return true;
	// False while the compiled sokol environment is still a stub (D3D11 before G7).
	case BackendKind::Sokol: return sokolEnvironmentImplemented();
	}
	return false;
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
	case BackendKind::Sokol:
		return std::make_unique<SgRenderBackend>();
	}
	return nullptr;
}

} // namespace newcore
