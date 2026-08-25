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
		case SDL_SCANCODE_E: a = Action::Use; break;          // ACTION_FIRE
		case SDL_SCANCODE_UP: case SDL_SCANCODE_W: a = Action::Forward; break;   // dialog: ACTION_UP
		case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: a = Action::Back; break;    // dialog: ACTION_DOWN
		case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: a = Action::TurnLeft; break; // dialog: ACTION_LEFT
		case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: a = Action::TurnRight; break; // dialog: ACTION_RIGHT
		case SDL_SCANCODE_TAB: a = Action::Passturn; break;   // ACTION_PASSTURN (skip-close)
		case SDL_SCANCODE_M: a = Action::Automap; break;      // ACTION_AUTOMAP (skip-close)
		case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: a = Action::Menu; break; // ACTION_MENU
		case SDL_SCANCODE_BACKSPACE: a = Action::BackKey; break; // KEY_CLR/BACK — swallowed in dialogs
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
		uint32_t frameStart = SDL_GetTicks();
		context.input().poll(context.window());

		acc += frameStart - last;
		if (acc > kMaxFrameMs) acc = kMaxFrameMs;
		last = frameStart;
		// Render-locked cadence (ADR-0004 addendum): exactly ONE quantum per
		// rendered frame, like the legacy DoLoop pass (src/Main.cpp:83-88).
		// Leftover accumulation is shed: chasing it produced periodic
		// double-step frames (30 ms sim jumps every ~8th frame) whenever the
		// paced period rounded above kTickMs, reading as sharp camera judder.
		if (acc >= GameContext::kTickMs) {
			acc = 0;
			ctx.tick();
		}

		Window& window = context.window();
		window.applyVideoSettings();
		ctx.render(context);
		++fpsFrames;

		if (frameStart - fpsTimer >= 1000) {
			std::fprintf(stdout, "FPS: %d\n", fpsFrames);
			std::fflush(stdout);
			fpsFrames = 0;
			fpsTimer += 1000;
		}

		// FPS lock (ADR-0004 addendum): pace to ~66 fps so simulation and
		// render advance together at the legacy cadence — one DoLoop pass per
		// ~15 ms measured from the real frame start (src/Main.cpp:83-88),
		// with the same 125 ms catch-up clamp above (src/Main.cpp:133-135).
		uint32_t spent = SDL_GetTicks() - frameStart;
		if (spent < GameContext::kTickMs) SDL_Delay(GameContext::kTickMs - spent);
	}

	return true;
}

} // namespace newcore
