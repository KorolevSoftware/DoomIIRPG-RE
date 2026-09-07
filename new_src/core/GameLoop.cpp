#include "core/GameLoop.h"

#include <cstdio>

#include "core/AppContext.h"
#include "core/GameContext.h"
#include "core/UiInputCollector.h"
#include "platform/InputSystem.h"
#include "platform/Window.h"
#include "render/api/RenderBackend.h"

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
	// Spec §3: one normalizer turns SDL into the frame's UiInput. It only reads
	// the events GameLoop already dispatches, so the Action path below is
	// unchanged (no event is consumed by either reader).
	UiInputCollector collector;
	// F12 frame captures are numbered per run (spec §8.2).
	int captureIndex = 0;

	context.input().setEventCallback([&](const SDL_Event& ev) {
		if (ev.type == SDL_QUIT) {
			running = false;
			return;
		}
		collector.onEvent(ev, context.window(), context.renderer());
		if (ev.type != SDL_KEYDOWN) return;
		// No quit key: the reference default table has none (src/Input.cpp:80-95)
		// and ESCAPE is the menu/back key, so SDL_QUIT (window close) is the
		// only way out.
		if (ev.key.keysym.scancode == SDL_SCANCODE_K) {
			ctx.debugGiveKeycards(); // PHASE5 DEBUG (removable)
		}
		// DEBUG (rewrite-only, removable): B flips the player coordinate
		// readout. B is one of the reserved-unmapped keys (AVK_BOTDISCARD,
		// src/Input.cpp:95 — the rewrite has no bots), so nothing is taken
		// away from the reference keymap (UiInputCollector.cpp:103-106).
		if (ev.key.keysym.scancode == SDL_SCANCODE_B && ev.key.repeat == 0) {
			ctx.debugToggleCoords();
		}
		// DEBUG (rewrite-only): F12 dumps the letterboxed canvas of the frame
		// being presented, so the two backends can be diffed (spec §8.2).
		if (ev.key.keysym.scancode == SDL_SCANCODE_F12 && ev.key.repeat == 0) {
			char path[64];
			std::snprintf(path, sizeof(path), "capture-%s-%03d.bmp",
				context.renderer().name(), captureIndex++);
			context.renderer().requestCapture(path);
		}
		const Action a = collector.keyAction(ev);
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
		collector.beginFrame(); // clear last frame's edges before this poll
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
		// Hand this frame's normalized input to the UI pass inside render()
		// (spec §3): input() is only valid until the next beginFrame().
		ctx.setUiInput(collector.input());
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
