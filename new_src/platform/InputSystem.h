#ifndef NEW_PLATFORM_INPUTSYSTEM_H
#define NEW_PLATFORM_INPUTSYSTEM_H

#include <SDL.h>

#include <cstdint>
#include <functional>

namespace newcore {

class Window;

// Collects SDL input events for the current frame and dispatches them to
// registered callbacks. Expands into the full action-mapping pipeline later.
class InputSystem {
public:
	using EventCallback = std::function<void(const SDL_Event&)>;

	InputSystem() = default;

	void setEventCallback(EventCallback cb) { callback_ = std::move(cb); }

	// Polls SDL events since the last call, dispatching each to the callback.
	void poll(Window& window);

private:
	EventCallback callback_;
};

} // namespace newcore

#endif // NEW_PLATFORM_INPUTSYSTEM_H