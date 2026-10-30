#ifndef NEW_MINIGAMES_COMMON_MINIGAMEHOST_H
#define NEW_MINIGAMES_COMMON_MINIGAMEHOST_H

#include <cstdint>
#include <functional>

#include "core/GameStates.h"
#include "minigames/common/MiniGame.h"
#include "minigames/common/MiniGameAssets.h"
#include "minigames/common/MiniGameHelp.h"
#include "minigames/hacking/HackingGame.h"
#include "minigames/sentrybot/SentryBotGame.h"
#include "minigames/vending/VendingMachine.h"
#include "ui/UiAssets.h"
#include "ui/UiTypes.h"

namespace newcore {

class EntityDefs;
class Game;
class Hud;
class Localization;
class Player;
class ScriptVM;
class Tables;
class TextureStore;
class Ui;
struct ScriptThread;

// Dispatcher for ST_MINI_GAME (ADR 0033 point 3): owns the calling script
// thread and the active game, and the shared endGame tail.
class MiniGameHost {
public:
	// EV_MINIGAME byte 1 == legacy stateVars[0] selector
	// (src/ScriptThread.cpp:1296-1347, src/Canvas.cpp:451-465).
	static constexpr int kSentryBot = 0, kHacking = 2, kVending = 4;
	// Script result slot written by endGame (src/VendingMachine.cpp:853).
	static constexpr int kResultVar = 7;

	struct Env {
		ScriptVM* vm = nullptr;
		StateHost* host = nullptr;
		Game* game = nullptr;               // loadMapID (vending machine id)
		Player* player = nullptr;
		const Tables* tables = nullptr;     // energyDrinkData (table 10), oscCycle
		const Localization* loc = nullptr;
		const EntityDefs* defs = nullptr;
		const int64_t* upTimeMs = nullptr;  // legacy app->time
		Hud* hud = nullptr;                 // evaluateResults message
		std::function<void(int)> openMenu;  // GameContext: nextMenuId_ = id; setState(Menu)
	};

	void init(const Env& env);   // also inits help_
	bool loadAssets(TextureStore& store, const UiAssets::ResourceReader& read);

	// EV_MINIGAME. False = type not implemented: nothing changed, the VM must
	// not park the thread. True = state is MiniGame and the game has begun.
	bool start(int type, int b1, int b2, int b3, ScriptThread* t);

	void latchTouch(int buttonId) { touchButton_ = buttonId; }
	void handleAction(Action a);
	UiResult draw(Ui& ui);

	// The shared endGame tail (src/VendingMachine.cpp:850-864,
	// src/HackingGame.cpp:619-636, src/SentryBotGame.cpp:806-824), world
	// branch only. Legacy order: the resumed script sees ST_PLAYING.
	void finish(int result);
	// evaluateMiniGameResults (src/MiniGameManager.cpp:235-248): a win grants
	// +2 IQ with a HUD message. Vending does not call it (ADR 0033).
	void evaluateResults(int n);

	const Env& env() const { return env_; }
	MiniGameHelp& help() { return help_; }
	const MiniGameAssets& assets() const { return assets_; }
	VendingMachine& vending() { return vending_; }   // vending menu hooks (spec §7)

private:
	Env env_;
	// Both survive StateId::Menu (vending terminal -> menu 83 -> back);
	// only finish() clears them.
	ScriptThread* callingThread_ = nullptr;
	MiniGame* active_ = nullptr;
	int touchButton_ = -1;
	MiniGameAssets assets_;
	MiniGameHelp help_;
	VendingMachine vending_;
	HackingGame hacking_;
	SentryBotGame sentry_;
};

} // namespace newcore

#endif // NEW_MINIGAMES_COMMON_MINIGAMEHOST_H
