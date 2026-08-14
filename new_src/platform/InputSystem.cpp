#include "platform/InputSystem.h"

namespace newcore {

void InputSystem::poll(Window& window) {
	(void)window;
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT) {
			if (callback_) callback_(ev);
			continue;
		}
		if (callback_) callback_(ev);
	}
}

} // namespace newcore