#ifndef NEW_CORE_FAMILIAR_H
#define NEW_CORE_FAMILIAR_H

#include <cstdint>

#include "core/GameStates.h"

namespace newcore {

class CinematicCamera;
class DialogSystem;
class EntityDefs;
class Game;
class Hud;
class Localization;
class MapData;
class Player;
class PlayerActions;
class ScriptVM;

// Familiar (sentry-bot control) behaviour, ADR 0041: deploy, set/unset,
// return to the body, self-destruct, ST_BOT_DYING, death and remains. The state lives on Player
// (isFamiliar, familiarType, save*, the copy bags); this peer only acts on it.
class Familiar {
public:
	struct Env {
		Game* game = nullptr;
		Player* player = nullptr;
		ScriptVM* vm = nullptr;
		MapData* map = nullptr;
		Hud* hud = nullptr;
		const Localization* loc = nullptr;
		const EntityDefs* defs = nullptr;              // bot remains: item defs
		DialogSystem* dialogs = nullptr;
		PlayerActions* actions = nullptr;              // the deploy step and the return rotation
		StateHost* host = nullptr;
		const CinematicCamera* cinematic = nullptr;    // isCameraActive gate of showHelp
		const int64_t* upTimeMs = nullptr;
	};

	void init(const Env& env) { env_ = env; }
	void attemptToDeploy();                 // Player::attemptToDeploySentryBot, src/Player.cpp:2387-2425
	void setFamiliar(int type);             // :2080-2126
	int unsetFamiliar(bool intoBody);       // :2128-2174, returns the old type
	void returnsToPlayer(bool intoBody);    // familiarReturnsToPlayer, :2278-2284
	void startSelfDestructDialog();         // :2439-2449
	// ADR 0041 §4: the monster-at-body latch set by SpriteLerps is acted on
	// here, at the end of every tick outside a cinematic
	// (forceFamiliarReturnDueToMonster :2382-2385, src/MayaCamera.cpp:386-388).
	void consumeMonsterLatch(StateId s);
	// DialogSystem::closeDialog style 12 tail (src/DialogSystem.cpp:564-569).
	void onSelfDestructDialogClosed(bool yes);
	void dying(bool selfDestruct);          // familiarDying, src/Player.cpp:735-752
	void onEnterBotDying();                 // setState(ST_BOT_DYING), src/Canvas.cpp:1130-1141
	void tickDying();                       // familiarDyingState, src/GameStateRunner.cpp:283-322

private:
	bool showHelp(int n, bool force);       // Player::showHelp, src/Player.cpp:1175-1194
	void message(int id, int flags);        // hud->addMessage((short)0, id, flags)
	void died();                            // familiarDied, src/Player.cpp:2208-2262
	void explode(int tx, int ty, int type); // explodeFamiliar, :2264-2276
	void handleBotRemains(int x, int y, int w);   // :2313-2380

	Env env_;
	int64_t deathTime_ = 0;                 // canvas->familiarDeathTime
	bool selfDestructed_ = false;           // canvas->familiarSelfDestructed
	bool shakeStarted_ = false;             // canvas->selfDestructScreenShakeStarted
};

} // namespace newcore

#endif // NEW_CORE_FAMILIAR_H
