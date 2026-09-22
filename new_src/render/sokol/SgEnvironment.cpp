#include "render/sokol/SgEnvironment.h"

#include <cstdio>

namespace newcore {

SgEnvironment::~SgEnvironment() = default;

GraphicsApi sokolGraphicsApi() {
#if defined(SOKOL_METAL)
	return GraphicsApi::Metal;
#else
	// glcore wants the GL window Window already creates. D3D11 joins in G7;
	// its environment returns nullptr until then, so the window it asks for
	// never gets used.
	return GraphicsApi::OpenGL;
#endif
}

bool sokolEnvironmentImplemented() {
#if defined(SOKOL_GLCORE) || defined(SOKOL_METAL)
	return true;
#else
	return false;
#endif
}

#if !defined(SOKOL_GLCORE) && !defined(SOKOL_METAL)
// D3D11 (G7) replaces this with a real factory in SgEnvironmentD3D11.cpp.
// A clear message, never a crash and never a silent black screen (spec §3.4).
std::unique_ptr<SgEnvironment> createSgEnvironment() {
	std::fprintf(stderr,
		"sokol: this environment is not implemented yet; configure with "
		"-DDOOM2RPG_SOKOL_BACKEND=glcore\n");
	return nullptr;
}
#endif

} // namespace newcore
