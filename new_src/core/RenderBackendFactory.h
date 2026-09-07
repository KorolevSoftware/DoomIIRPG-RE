#ifndef NEW_CORE_RENDERBACKENDFACTORY_H
#define NEW_CORE_RENDERBACKENDFACTORY_H

#include <memory>

namespace newcore {

class RenderBackend;

// Which implementation of the render backend to build (spec
// 2026-09-02-render-backend-split §4.3).
enum class BackendKind { OpenGL, SdlRender };

// "gl" / "sdl": the --backend= spelling, the window title suffix and the
// capture file name all use this.
const char* backendKindName(BackendKind kind);

// Parses "gl" | "sdl". Leaves `out` untouched and returns false otherwise.
bool parseBackendKind(const char* text, BackendKind& out);

// True when this build actually contains the implementation.
bool backendKindAvailable(BackendKind kind);

// Maps a requested kind to one that exists in this build, logging the
// substitution once. Callers must resolve BEFORE creating the window, because
// the window's GraphicsApi has to match the backend.
BackendKind resolveBackendKind(BackendKind requested);

// Builds the backend. Returns nullptr for a kind that is not available.
std::unique_ptr<RenderBackend> createRenderBackend(BackendKind kind);

} // namespace newcore

#endif // NEW_CORE_RENDERBACKENDFACTORY_H
