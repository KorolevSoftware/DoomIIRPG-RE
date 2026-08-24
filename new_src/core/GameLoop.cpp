#include "core/GameLoop.h"

#include <cstdio>

#include "core/AppContext.h"
#include "core/GameContext.h"
#include "platform/InputSystem.h"
#include "platform/Window.h"
#include "render/RenderBackend.h"
#include "render/gl/GlCommon.h"

#include <SDL.h>

namespace newcore {

namespace {
constexpr uint32_t kMaxFrameMs = 125; // legacy clamp (src/Main.cpp:133-135)
}

// Dumb fixed-step driver (spec §4): accumulate clamped dt, consume 15 ms
// quanta via ctx.tick(), render once per frame. All simulation policy lives
// in GameContext.
bool GameLoop::run(AppContext& context) {
	GameContext& ctx = context.gameContext();
	bool running = true;

	context.input().setEventCallback([&](const SDL_Event& ev) {
		if (ev.type == SDL_QUIT) {
			running = false;
			return;
		}
		if (ev.type != SDL_KEYDOWN) return;
		if (ev.key.keysym.sym == SDLK_ESCAPE) { // clean exit through the machine
			running = false;
			return;
		}
		Action a = Action::None;
		switch (ev.key.keysym.scancode) {
		case SDL_SCANCODE_E: a = Action::Use; break;
		case SDL_SCANCODE_UP: case SDL_SCANCODE_W: a = Action::Forward; break;
		case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: a = Action::Back; break;
		case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: a = Action::TurnLeft; break;
		case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: a = Action::TurnRight; break;
		case SDL_SCANCODE_K: ctx.debugGiveKeycards(); break; // PHASE5 DEBUG (removable)
		default: break;
		}
		if (a != Action::None) {
			ctx.queueAction(a);
		}
	});

	uint32_t last = SDL_GetTicks();
	uint32_t acc = 0;
	int fpsFrames = 0;
	uint32_t fpsTimer = last;

	while (running) {
		uint32_t now = SDL_GetTicks();
		context.input().poll(context.window());

		acc += now - last;
		if (acc > kMaxFrameMs) acc = kMaxFrameMs;
		last = now;
		while (acc >= GameContext::kTickMs) {
			acc -= GameContext::kTickMs;
			ctx.tick();
		}

		Window& window = context.window();
		window.applyVideoSettings();
		ctx.render(context);
		++fpsFrames;

		if (now - fpsTimer >= 1000) {
			std::fprintf(stdout, "FPS: %d\n", fpsFrames);
			std::fflush(stdout);
			fpsFrames = 0;
			fpsTimer += 1000;
		}

		SDL_Delay(1);
	}

	return true;
}

} // namespace newcore
