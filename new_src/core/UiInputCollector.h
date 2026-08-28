#ifndef NEW_CORE_UIINPUTCOLLECTOR_H
#define NEW_CORE_UIINPUTCOLLECTOR_H

#include <SDL.h>

#include "core/GameStates.h"
#include "ui/UiTypes.h"

namespace newcore {

class RenderBackend;
class Window;

// The single place that turns SDL events into a UiInput (spec §3). Lives in
// core/ because ui/ may not know about platform/ or SDL.
//
// Edges (pressed/released/nav/wheel) are cleared by beginFrame() and set by
// onEvent(), so they are true for exactly the one frame whose poll produced
// them; `down` is a level that survives across frames.
class UiInputCollector {
public:
	void beginFrame();
	void onEvent(const SDL_Event& ev, const Window& window,
	             const RenderBackend& backend);

	const UiInput& input() const { return in_; }

	// The keyboard -> Action mapping, moved verbatim out of GameLoop. Pure:
	// it reads the event and returns what to queue, so the gameplay path stays
	// exactly what it was and the Nav read below cannot consume a key.
	Action keyAction(const SDL_Event& ev) const;

private:
	void updateCursor(int windowX, int windowY, const Window& window,
	                  const RenderBackend& backend);

	UiInput in_;
};

} // namespace newcore

#endif // NEW_CORE_UIINPUTCOLLECTOR_H
