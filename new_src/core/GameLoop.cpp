#include "core/GameLoop.h"

#include "core/AppContext.h"
#include "platform/InputSystem.h"
#include "platform/Window.h"
#include "render/RenderBackend.h"
#include "render/gl/GlCommon.h"

#include <SDL.h>

namespace newcore {

namespace {
constexpr uint32_t kTickMs = 15;
}

bool GameLoop::run(AppContext& context) {
	bool running = true;

	context.input().setEventCallback([&](const SDL_Event& ev) {
		if (ev.type == SDL_QUIT) running = false;
	});

	uint32_t nextTick = SDL_GetTicks();
	int fpsFrames = 0;
	uint32_t fpsTimer = nextTick;

	while (running) {
		uint32_t now = SDL_GetTicks();
		context.input().poll(context.window());

		if (now >= nextTick) {
			nextTick = now + kTickMs;

			Window& window = context.window();
			window.applyVideoSettings();

			context.renderer().beginFrame(window);

			// Game logic + scene rendering will be hooked up in later phases.
			context.renderer().endFrame(window);
			++fpsFrames;
		}

		if (now - fpsTimer >= 1000) {
			std::fprintf(stdout, "FPS: %d\n", fpsFrames);
			fpsFrames = 0;
			fpsTimer += 1000;
		}

		SDL_Delay(1);
	}

	return true;
}

} // namespace newcore