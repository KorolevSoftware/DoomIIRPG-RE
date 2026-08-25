#ifndef NEW_DOMAIN_GAME_SCRIPTVM_H
#define NEW_DOMAIN_GAME_SCRIPTVM_H

#include <cstdint>

namespace newcore {

class EntityDef;
class Entity;
class DialogSystem;
class Game;
class GameContext;
class Hud;
class Localization;
class MapData;
class Player;

// One interpreter strand of the tileEvents VM. Layout follows the legacy
// ScriptThread (src/ScriptThread.cpp; PORT CHECKLIST 2 of
// docs/original-code/tile-events-vm.md).
struct ScriptThread {
	int IP = 0;
	int FP = 0;
	int stackPtr = 0;
	int unpauseTime = 0; // ms in gameTime space; -1 = waiting on external resume
	int type = 0;        // trigger mask of the spawning event
	int flags = 0;       // bit0 = block-input capable (executeTile b=true), bit1 = queued
	int state = 2;       // 2 alive, 0 dead (out-of-range coords / aborted)
	bool inuse = false;
	int scriptStack[16]; // src/ScriptThread.cpp pop/push :2083-2093
};

// Faithful tileEvents bytecode interpreter: fixed 20-thread pool,
// executeTile/executeStaticFunc/runScriptThreads, scriptStateVars[128],
// event-index iterator and trigger filter (spec 2026-08-23-phase5-skeleton §7).
class ScriptVM {
public:
	static constexpr int kMaxThreads = 20;
	static constexpr int kNumStateVars = 128;
	static constexpr int kStackSize = 16;

	// Non-owning pointers to the systems the VM calls into.
	struct Env {
		MapData* map = nullptr;
		const class EntityDefs* defs = nullptr;
		Game* game = nullptr;
		Player* player = nullptr;
		Localization* loc = nullptr;
		Hud* hud = nullptr;
		GameContext* ctx = nullptr;      // blockInputTime latch (abortMove/message routing live on game/hud)
		DialogSystem* dialogs = nullptr; // EV_DIALOG target (startDialog / help enqueue)
		const int64_t* gameTime = nullptr;
	};

	short vars[kNumStateVars] = { 0 };   // scriptStateVars (src/Game.h:129)

	void init(const Env& env);

	// loadMapEntities-time clear of the whole pool (src/Game.cpp:332-360).
	void resetPool();

	// Zeroes every state var except keepSlot (< 0 keeps none)
	// (src/LoadingManager.cpp:631-635).
	void clearStateVars(int keepSlot);

	// Runs all matching events of the tile synchronously on a fresh thread.
	// Returns the last run code: 1 finished, 2 paused, 0 nothing ran.
	int executeTile(int x, int y, int mask, bool blockInput);

	// Runs staticFuncs[idx] synchronously; sentinel 65535 / idx >= 12 -> 0.
	int executeStaticFunc(int idx);

	// Per-frame resumption pass (src/Game.cpp:3246-3268); only called while
	// ST_PLAYING in the subset.
	void runScriptThreads(int64_t gameTime);

	// Any inuse thread whose trigger mask carries EVFL_FLAG_BLOCKINPUT
	// (src/Game.cpp:3447-3459).
	bool isInputBlockedByScript() const;

	// External-resume protocol: direct run() on a thread parked with
	// unpauseTime == -1 (blocking door open, dialog-lite dismissal). Returns
	// the run code (1 finished, 2 re-parked). No-op when the thread was freed
	// meanwhile (legacy callThreads inuse check, src/Game.cpp:3009-3013).
	int resumeThread(ScriptThread* t);

	// FIX A (bring-up): clears ctx->blockInputTime once no live thread still
	// demands it. Legacy releases purely by timestamp compare
	// (src/InputEventController.cpp:472-476); the port adds owner accounting:
	// a holder is a live thread with flags&1 (evWait latch owner,
	// src/ScriptThread.cpp:164-166) or the dormant EVFL_FLAG_BLOCKINPUT
	// trigger bit (src/Game.cpp:3447-3459). No-op while blockInputTime == 0.
	void releaseBlockIfUnheld(const char* why);

	// Pool index of a pooled thread (legacy scans scriptThreads comparing
	// pointers, src/ScriptThread.cpp:564-569) and back-mapping for the help
	// FIFO's resume binding.
	int indexOf(const ScriptThread* t) const { return (int)(t - threads_); }
	ScriptThread* threadAt(int i) { return &threads_[i]; }

private:
	ScriptThread* allocThread();                       // first free slot (src/Game.cpp:3270-3284)
	void freeThread(ScriptThread* t);                  // reset() (src/Game.cpp:3286-3295)
	void allocFromEvent(ScriptThread* t, int eventIdx, int type, bool blockInput); // src/ScriptThread.cpp:155-167
	void allocRaw(ScriptThread* t, int ip);            // src/ScriptThread.cpp:169-177

	uint32_t run(ScriptThread* t);                     // dispatch loop, src/ScriptThread.cpp:230-2046
	int attemptResume(ScriptThread* t, int64_t gameTime); // src/ScriptThread.cpp:2063-2072
	int evWait(ScriptThread* t, int ms);               // src/ScriptThread.cpp:120-137
	bool evReturn(ScriptThread* t);                    // src/ScriptThread.cpp:139-153
	void updateScriptVars();                           // src/Game.cpp:3461-3471 (refreshed slots only)

	void push(ScriptThread* t, int v);
	int pop(ScriptThread* t);

	// Big-endian pre-increment arg readers (src/ScriptThread.cpp:2095-2119).
	uint8_t readUByte(ScriptThread* t);
	int8_t readByte(ScriptThread* t);
	uint16_t readUShort(ScriptThread* t);
	int16_t readShort(ScriptThread* t);
	int32_t readInt(ScriptThread* t);

	// Event lookup with the packed lastTileEvent cache (src/Render.cpp:182-209).
	int findEventIndex(int tile);
	int getNextEventIndex();
	bool eventMatches(int w1, int flags) const;        // src/ScriptThread.cpp:75

	Env env_;
	ScriptThread threads_[kMaxThreads];
	int numThreads_ = 0;
	int lastTileEvent_ = -1;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_SCRIPTVM_H
