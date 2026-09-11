#include "render/sokol/SgEnvironment.h"

#include <cstdio>

namespace newcore {

SgEnvironment::~SgEnvironment() = default;

GraphicsApi sokolGraphicsApi() {
	// Metal and D3D11 join GraphicsApi in G6/G7; until then the compiled sokol
	// backend always wants the GL window Window already creates.
	return GraphicsApi::OpenGL;
}

bool sokolEnvironmentImplemented() {
#if defined(SOKOL_GLCORE)
	return true;
#else
	return false;
#endif
}

#if !defined(SOKOL_GLCORE)
// Metal (G6) and D3D11 (G7) replace this with a real factory in
// SgEnvironmentMetal.mm / SgEnvironmentD3D11.cpp. A clear message, never a
// crash and never a silent black screen (spec §3.4).
std::unique_ptr<SgEnvironment> createSgEnvironment() {
	std::fprintf(stderr,
		"sokol: this environment is not implemented yet; configure with "
		"-DDOOM2RPG_SOKOL_BACKEND=glcore\n");
	return nullptr;
}
#endif

} // namespace newcore
