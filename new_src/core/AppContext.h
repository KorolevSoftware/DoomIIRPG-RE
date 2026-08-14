#ifndef NEW_CORE_APPCONTEXT_H
#define NEW_CORE_APPCONTEXT_H

#include <memory>
#include <string>
#include <vector>

namespace newcore {

class Window;
class ZipArchive;
class RenderBackend;
class Graphics2D;
class InputSystem;

// Composition root. Owns every subsystem and wires them together.
class AppContext {
public:
	static AppContext& instance();

	// Path prefix for game resources inside the data archive.
	static constexpr const char* kResourcePrefix = "Payload/Doom2rpg.app/Packages/";

	bool initialize(const char* dataArchive);
	void shutdown();

	bool run();

	Window& window();
	ZipArchive& archive();
	RenderBackend& renderer();
	InputSystem& input();

	// Reads a resource from the archive using the standard prefix.
	bool readResource(const std::string& fileName, std::vector<uint8_t>& out) const;

private:
	AppContext() = default;
	bool startup();

	std::unique_ptr<Window> window_;
	std::unique_ptr<ZipArchive> archive_;
	std::unique_ptr<RenderBackend> renderer_;
	std::unique_ptr<InputSystem> input_;
};

} // namespace newcore

#endif // NEW_CORE_APPCONTEXT_H