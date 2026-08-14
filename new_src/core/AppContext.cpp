#include "core/AppContext.h"

#include "core/GameLoop.h"
#include "io/ZipArchive.h"
#include "platform/FileSystem.h"
#include "platform/InputSystem.h"
#include "platform/Window.h"
#include "render/RenderBackend.h"

#include <cstdio>

namespace newcore {

AppContext& AppContext::instance() {
	static AppContext ctx;
	return ctx;
}

Window& AppContext::window() { return *window_; }
ZipArchive& AppContext::archive() { return *archive_; }
RenderBackend& AppContext::renderer() { return *renderer_; }
InputSystem& AppContext::input() { return *input_; }

bool AppContext::initialize(const char* dataArchive) {
	window_ = std::make_unique<Window>();
	if (!window_->initialize("Doom II RPG")) {
		std::fprintf(stderr, "Failed to initialize window.\n");
		return false;
	}

	renderer_ = std::make_unique<RenderBackend>();
	if (!renderer_->initialize(*window_)) {
		std::fprintf(stderr, "Failed to initialize renderer.\n");
		return false;
	}

	input_ = std::make_unique<InputSystem>();

	archive_ = std::make_unique<ZipArchive>();
	std::string path = FileSystem::instance().findDataArchive(dataArchive);
	if (path.empty()) {
		std::fprintf(stderr, "Data archive '%s' not found.\n", dataArchive);
		return false;
	}
	if (!archive_->open(path)) {
		std::fprintf(stderr, "Failed to open data archive '%s'.\n", path.c_str());
		return false;
	}

	return startup();
}

bool AppContext::startup() {
	return true;
}

bool AppContext::readResource(const std::string& fileName, std::vector<uint8_t>& out) const {
	return archive_->readEntry(std::string(kResourcePrefix) + fileName, out);
}

bool AppContext::run() {
	return GameLoop::run(*this);
}

void AppContext::shutdown() {
	archive_.reset();
	input_.reset();
	renderer_.reset();
	window_.reset();
}

} // namespace newcore