#ifndef NEW_CORE_APPCONTEXT_H
#define NEW_CORE_APPCONTEXT_H

#include <memory>
#include <string>
#include <vector>

#include "core/RenderBackendFactory.h"
#include "render/Graphics2D.h"

namespace newcore {

class Window;
class ZipArchive;
class RenderBackend;
class InputSystem;
class GameContext;

// Composition root. Owns every subsystem and wires them together.
class AppContext {
public:
	static AppContext& instance();

	// Path prefix for game resources inside the data archive.
	static constexpr const char* kResourcePrefix = "Payload/Doom2rpg.app/Packages/";

	bool initialize(const char* dataArchive, BackendKind backend);
	void shutdown();

	// The backend actually built (may differ from the request, see
	// resolveBackendKind).
	BackendKind backendKind() const { return backendKind_; }

	bool run();

	Window& window();
	ZipArchive& archive();
	RenderBackend& renderer();
	InputSystem& input();

	// The 2D drawing façade. Backend-neutral game code (it needs text/Font),
	// so the composition root owns it, not the backend (ADR 0020).
	Graphics2D& g2d() { return g2d_; }

	// Non-owning pointer to the game-state machine constructed in main()
	// (consumed by GameLoop::run).
	void setGameContext(GameContext* ctx) { gameContext_ = ctx; }
	GameContext& gameContext() const { return *gameContext_; }

	// Reads a resource from the archive using the standard prefix.
	bool readResource(const std::string& fileName, std::vector<uint8_t>& out) const;

private:
	AppContext() = default;
	bool startup();

	BackendKind backendKind_ = BackendKind::OpenGL;
	std::unique_ptr<Window> window_;
	std::unique_ptr<ZipArchive> archive_;
	std::unique_ptr<RenderBackend> renderer_;
	Graphics2D g2d_;
	std::unique_ptr<InputSystem> input_;
	GameContext* gameContext_ = nullptr;
};

} // namespace newcore

#endif // NEW_CORE_APPCONTEXT_H