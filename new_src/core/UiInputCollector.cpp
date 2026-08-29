#include "core/UiInputCollector.h"

#include "platform/Window.h"
#include "render/RenderBackend.h"

namespace newcore {

void UiInputCollector::beginFrame() {
	in_.pressed = false;
	in_.released = false;
	in_.nav = Nav::None;
	in_.wheel = 0;
	// cursorX/cursorY/cursorValid and `down` are levels: they keep the value
	// the last event left them with.
}

void UiInputCollector::updateCursor(int windowX, int windowY,
                                    const Window& window,
                                    const RenderBackend& backend) {
	int px = 0;
	int py = 0;
	window.windowToDrawable(windowX, windowY, px, py);
	in_.cursorValid = backend.drawableToCanvas(px, py, in_.cursorX, in_.cursorY);
}

void UiInputCollector::onEvent(const SDL_Event& ev, const Window& window,
                               const RenderBackend& backend) {
	switch (ev.type) {
	case SDL_MOUSEMOTION:
		updateCursor(ev.motion.x, ev.motion.y, window, backend);
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (ev.button.button == SDL_BUTTON_LEFT) {
			updateCursor(ev.button.x, ev.button.y, window, backend);
			in_.pressed = true;
			in_.down = true;
		}
		break;
	case SDL_MOUSEBUTTONUP:
		if (ev.button.button == SDL_BUTTON_LEFT) {
			updateCursor(ev.button.x, ev.button.y, window, backend);
			in_.released = true;
			in_.down = false;
		}
		break;
	case SDL_MOUSEWHEEL:
		in_.wheel += ev.wheel.y;
		break;
	case SDL_KEYDOWN:
		// No auto-repeat: the original drops repeats too (src/Input.cpp:740,794).
		if (ev.key.repeat != 0) break;
		switch (ev.key.keysym.scancode) {
		case SDL_SCANCODE_UP: case SDL_SCANCODE_W: in_.nav = Nav::Up; break;
		case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: in_.nav = Nav::Down; break;
		case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: in_.nav = Nav::Left; break;
		case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: in_.nav = Nav::Right; break;
		case SDL_SCANCODE_E:
		case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
			in_.nav = Nav::Activate;
			break;
		case SDL_SCANCODE_BACKSPACE: in_.nav = Nav::Cancel; break;
		default: break;
		}
		break;
	default:
		break;
	}
}

Action UiInputCollector::keyAction(const SDL_Event& ev) const {
	if (ev.type != SDL_KEYDOWN) return Action::None;
	// No auto-repeat, in every state: the original drops repeated KEYDOWNs
	// before they ever reach the pressed-key set (src/Input.cpp:740), and the
	// action queue is fed one entry per physical press — isKeyboardKeyPressed()
	// removes the key from the set as it queues (src/Input.cpp:1082-1088).
	// The menu depends on this (spec 2026-08-28-menu §2.5).
	if (ev.key.repeat != 0) return Action::None;
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
	default: break;
	}
	return a;
}

} // namespace newcore
