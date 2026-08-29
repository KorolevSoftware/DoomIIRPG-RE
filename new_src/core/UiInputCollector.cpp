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
		case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
			in_.nav = Nav::Activate;                          // AVK_MENU_SELECT (src/Input.cpp:88)
			break;
		case SDL_SCANCODE_ESCAPE:                             // AVK_MENU_OPEN (src/Input.cpp:91)
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
	// The map follows the reference default table verbatim
	// (src/Input.cpp:80-95); deviations are marked below.
	Action a = Action::None;
	switch (ev.key.keysym.scancode) {
	case SDL_SCANCODE_UP: case SDL_SCANCODE_W: a = Action::Forward; break;   // :80, dialog: ACTION_UP
	case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: a = Action::Back; break;    // :81, dialog: ACTION_DOWN
	// DEVIATION: the original turns with the arrows only (:82-83) and gives
	// A/D strafe (AVK_MOVELEFT/AVK_MOVERIGHT, :84-85). The rewrite has no
	// strafe and none is wanted, so A/D duplicate the turn keys instead of
	// becoming dead keys.
	case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: a = Action::TurnLeft; break;   // :82, dialog: ACTION_LEFT
	case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: a = Action::TurnRight; break; // :83, dialog: ACTION_RIGHT
	// AVK_SELECT | AVK_MENU_SELECT (:88): attack/talk/use in play, select in
	// the menu, page advance in a dialog — one action, state decides.
	// DEVIATION: KP_ENTER is ours, the table lists RETURN alone.
	case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: a = Action::Use; break; // ACTION_FIRE
	case SDL_SCANCODE_C: a = Action::Passturn; break;     // :89, ACTION_PASSTURN (skip-close)
	case SDL_SCANCODE_TAB: a = Action::Automap; break;    // :90, ACTION_AUTOMAP (skip-close)
	// AVK_MENUOPEN | AVK_MENU_OPEN (:91): opens the menu from play and steps
	// back inside it — exactly what Action::Menu already means everywhere
	// (PlayerActions.cpp:279, MenuSession.cpp:581, DialogSystem.cpp:310).
	case SDL_SCANCODE_ESCAPE: a = Action::Menu; break;    // ACTION_MENU
	// DEVIATION: ours. BACKSPACE keeps feeding Action::BackKey (KEY_CLR/BACK)
	// so the mouse Back path and the loot list keep a keyboard equivalent.
	case SDL_SCANCODE_BACKSPACE: a = Action::BackKey; break; // swallowed in dialogs
	// Reserved, deliberately unmapped: the rewrite has no counterpart for
	// AVK_NEXTWEAPON Z (:86), AVK_PREVWEAPON X (:87), AVK_ITEMS_INFO I (:92),
	// AVK_DRINKS O (:93), AVK_PDA P (:94), AVK_BOTDISCARD B (:95).
	// SDL_SCANCODE_K is ours too — debug keycards, handled in GameLoop.cpp.
	default: break;
	}
	return a;
}

} // namespace newcore
