#ifndef NEW_CORE_GAMESTATES_H
#define NEW_CORE_GAMESTATES_H

namespace newcore {

// Legacy Canvas state ids (src/Canvas.h:68-93 numbering kept). Menu/Intro/
// Combat/Automap/TravelMap/... are documented out-of-scope stubs (spec
// 2026-08-23-phase5-skeleton §3): not enumerated, unreachable; script
// requests for them are logged by the VM.
enum class StateId : int {
	Playing     = 3,  // legacy ST_PLAYING
	InterCamera = 4,  // legacy ST_INTER_CAMERA (script lerp show; world renders from player view)
	Dialog      = 8,  // legacy ST_DIALOG (real dialogs, spec GROUP 1)
	Loading     = 7,  // legacy ST_LOADING
	Dying       = 13, // legacy ST_DYING (stub)
	Camera      = 18, // legacy ST_CAMERA (scripted cinematic, cutscenes-camera.md §1)
	Looting     = 23, // legacy ST_LOOTING (src/Canvas.h:90; loot-crouch camera,
	                  // docs/research/2026-08-25-camera-pitch-loot.md)
};

// Discrete input actions (legacy getKeyAction subset,
// src/InputEventController.cpp:24-141). The first six are the playing set;
// during ST_DIALOG every queued action routes to the dialog handler, which
// reads Forward/Back/TurnLeft/TurnRight as ACTION_UP/DOWN/LEFT/RIGHT.
enum class Action : int {
	None, Forward, Back, TurnLeft, TurnRight, Use,
	Passturn, Automap, Menu, BackKey,
};

// The only thing a module may know about the state machine (spec
// 2026-08-26-decomposition §1.1): read the current state and request a change
// synchronously. GameContext is the sole implementation.
class StateHost {
public:
	virtual ~StateHost() = default;
	virtual StateId state() const = 0;
	virtual void requestState(StateId s) = 0;   // == GameContext::setState
};

} // namespace newcore

#endif // NEW_CORE_GAMESTATES_H
