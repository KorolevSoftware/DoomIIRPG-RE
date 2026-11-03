#ifndef NEW_CORE_GAMESTATES_H
#define NEW_CORE_GAMESTATES_H

namespace newcore {

// Legacy Canvas state ids (src/Canvas.h:68-93 numbering kept). Menu/Intro/
// Combat/... are documented out-of-scope stubs (spec
// 2026-08-23-phase5-skeleton §3): not enumerated, unreachable; script
// requests for them are logged by the VM.
enum class StateId : int {
	Menu        = 1,  // legacy ST_MENU (src/Canvas.h:68; spec 2026-08-28-menu §2.1)
	Playing     = 3,  // legacy ST_PLAYING
	InterCamera = 4,  // legacy ST_INTER_CAMERA (script lerp show; world renders from player view)
	Automap     = 6,  // legacy ST_AUTOMAP (src/Canvas.h:73)
	Dialog      = 8,  // legacy ST_DIALOG (real dialogs, spec GROUP 1)
	Loading     = 7,  // legacy ST_LOADING
	MiniGame    = 12, // legacy ST_MINI_GAME (src/Canvas.h:79; spec 2026-10-29-minigames-layout §4)
	Dying       = 13, // legacy ST_DYING (stub)
	Saving      = 16, // legacy ST_SAVING (src/Canvas.h:68-93)
	Camera      = 18, // legacy ST_CAMERA (scripted cinematic, cutscenes-camera.md §1)
	TravelMap   = 21, // legacy ST_TRAVELMAP (src/Canvas.h:88)
	Looting     = 23, // legacy ST_LOOTING (src/Canvas.h:90; loot-crouch camera,
	                  // docs/research/2026-08-25-camera-pitch-loot.md)
	BotDying    = 25, // legacy ST_BOT_DYING (src/Canvas.h:92; familiar death, ADR 0041 §3)
};

// Discrete input actions (legacy getKeyAction subset,
// src/InputEventController.cpp:24-141). The first six are the playing set;
// during ST_DIALOG every queued action routes to the dialog handler, which
// reads Forward/Back/TurnLeft/TurnRight as ACTION_UP/DOWN/LEFT/RIGHT.
enum class Action : int {
	None, Forward, Back, TurnLeft, TurnRight, Use,
	Passturn, Automap, Menu, BackKey,
	Items,   // ACTION_ITEMS (AVK_ITEMS_INFO, src/Input.cpp:92)
	ItemsDrinks,   // ACTION_ITEMS_DRINKS (AVK_DRINKS, src/Input.cpp:93)
	NextWeapon, PrevWeapon,   // ACTION_NEXTWEAPON / ACTION_PREVWEAPON (src/Enums.h:1002-1003)
	BotDiscard,   // ACTION_BOT_DISCARD (src/Enums.h:1010, AVK_BOTDISCARD src/Input.h:149)
	// No key produces this one: the in-game menu's right soft key is touch-only
	// in the original (src/MenuSystem.cpp:4787-4789 calls returnToGame directly),
	// so it exists purely so the UI intent travels through the one action queue
	// (spec 2026-08-28-menu §9).
	MenuResume,
	// The in-game menu's info buttons (GROUP 8). MenuInfo is the port's
	// ACTION_MENU_ITEM_INFO (src/Enums.h:1009), which the original's touch
	// handler raises from an info button (src/MenuSystem.cpp:4802-4805); no key
	// is bound to it in the rewrite. MenuInfoClose has no legacy action id: it
	// is the touch release that dismisses the torn page (:4809-4813).
	MenuInfo, MenuInfoClose,
	// No key produces this one either: it is a minigame touch release, whose
	// button id was latched through MiniGameHost::latchTouch (the ListRow idiom).
	MiniGameTouch,
	// DEBUG (removable, minigames): V starts the vending machine, slot 1.
	DebugVending,
	// DEBUG (removable, minigames): H starts the map00 hacking puzzle.
	DebugHacking,
	// DEBUG (removable, minigames): J / Shift+J start the sentry bot game, botType 0 / 1.
	DebugSentryBot, DebugSentryBotRed,
	// DEBUG (removable, map change): N / Shift+N
	DebugNextMap, DebugPrevMap,
	// DEBUG (removable, familiar): F5 gives the next bot weapon 3, 4, 5, 6.
	DebugGiveBot,
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
