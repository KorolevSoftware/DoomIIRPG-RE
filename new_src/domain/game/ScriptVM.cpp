#include "domain/game/ScriptVM.h"

#include <cstdio>
#include <string>

#include "core/GameContext.h"
#include "domain/game/Entity.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "ui/Hud.h"

namespace newcore {

// ---- setup / pool ----

void ScriptVM::init(const Env& env) {
	env_ = env;
	resetPool();
}

void ScriptVM::resetPool() {
	for (ScriptThread& t : threads_) {
		t.inuse = false;
		t.IP = t.FP = t.stackPtr = 0;
		t.unpauseTime = 0;
		t.type = 0;
		t.flags = 0;
		t.state = 2;
	}
	numThreads_ = 0;
	lastTileEvent_ = -1;
}

void ScriptVM::clearStateVars(int keepSlot) {
	short kept = (keepSlot >= 0 && keepSlot < kNumStateVars) ? vars[keepSlot] : 0;
	for (short& v : vars) v = 0;
	if (keepSlot >= 0 && keepSlot < kNumStateVars) vars[keepSlot] = kept;
}

ScriptThread* ScriptVM::allocThread() {
	for (ScriptThread& t : threads_) {
		if (!t.inuse) {
			t.IP = 0; t.FP = 0; t.stackPtr = 0;
			t.unpauseTime = 0;
			t.state = 2;                       // ScriptThread::init (src/ScriptThread.cpp:2048-2056)
			t.inuse = true;
			++numThreads_;
			return &t;
		}
	}
	// Legacy fatals with ERR_MAX_SCRIPTTHREADS (src/Game.cpp:3282); bring-up
	// must survive malformed data (spec §7.1), so log and report "nothing ran".
	std::fprintf(stderr, "[script] ERR_MAX_SCRIPTTHREADS (40): pool exhausted\n");
	return nullptr;
}

void ScriptVM::freeThread(ScriptThread* t) {
	t->inuse = false;                          // reset()
	t->IP = 0; t->FP = 0; t->stackPtr = 0;
	t->unpauseTime = 0;
	t->state = 2;
	--numThreads_;
	// FIX A: every termination path funnels through here (executeTile /
	// executeStaticFunc non-paused results, runScriptThreads sweep for
	// EV_RETURN/EV_END completions, opcode kills). Release the input latch
	// the thread may hold so a finished script cannot block forever.
	releaseBlockIfUnheld("thread-end");
}

// ---- event lookup + trigger filter ----

int ScriptVM::findEventIndex(int tile) {
	const std::vector<int32_t>& events = env_.map->tileEvents;
	for (int i = 0; i < env_.map->numTileEvents; ++i) {
		int n2 = i * 2;
		if ((events[n2] & 0x3FF) == tile) {
			lastTileEvent_ = i | (tile << 16); // Render::findEventIndex (src/Render.cpp:200-209)
			return n2;
		}
	}
	lastTileEvent_ = -1;
	return -1;
}

int ScriptVM::getNextEventIndex() {
	if (lastTileEvent_ == -1) return -1;
	int n = (lastTileEvent_ & 0xFFFF0000) >> 16;
	int n2 = (lastTileEvent_ & 0xFFFF) + 1;
	if (n2 < env_.map->numTileEvents) {
		int n3 = n2 * 2;
		if ((env_.map->tileEvents[n3] & 0x3FF) == n) {
			lastTileEvent_ = n2 | (n << 16);   // Render::getNextEventIndex (src/Render.cpp:182-198)
			return n3;
		}
	}
	lastTileEvent_ = -1;
	return -1;
}

bool ScriptVM::eventMatches(int w1, int flags) const {
	int n6 = w1 & flags;
	return (w1 & Enums::EVFL_FLAG_DISABLE) == 0
		&& (n6 & Enums::EVFL_EXEC_MASK) != 0
		&& (n6 & Enums::EVFL_MOD_DIR_MASK) != 0
		&& (((w1 & Enums::EVFL_MOD_ATTACK_MASK) == 0 && (flags & Enums::EVFL_MOD_ATTACK_MASK) == 0)
			|| (n6 & Enums::EVFL_MOD_ATTACK_MASK) != 0);   // src/ScriptThread.cpp:75
}

// ---- thread starts ----

void ScriptVM::allocFromEvent(ScriptThread* t, int eventIdx, int type, bool blockInput) {
	t->IP = (int)((env_.map->tileEvents[eventIdx] & 0xFFFF0000) >> 16);
	t->FP = 0;
	t->stackPtr = 0;
	push(t, -1);
	push(t, 0);
	t->type = type;
	t->flags = blockInput ? 1 : 0;             // src/ScriptThread.cpp:155-167
}

void ScriptVM::allocRaw(ScriptThread* t, int ip) {
	t->IP = ip;
	t->FP = 0;
	t->stackPtr = 0;
	push(t, -1);
	push(t, 0);
	t->type = 0;
	t->flags = 1;                              // src/ScriptThread.cpp:169-177
}

int ScriptVM::executeTile(int x, int y, int mask, bool blockInput) {
	ScriptThread* t = allocThread();
	int result = 0;
	if (t == nullptr) return 0;
	if (x < 0 || x >= 32 || y < 0 || y >= 32) {
		t->state = 0;                                        // src/ScriptThread.cpp:60-63
	} else {
		env_.game->skipAdvanceTurn = false;                  // :64
		int tileIdx = y * 32 + x;
		if ((env_.map->mapFlags[tileIdx] & 0x40) != 0) {
			std::fprintf(stderr, "[script] executeTile(tile=%d,%d mask=0x%X)\n", x, y, mask);
			for (int i = findEventIndex(tileIdx); i != -1; i = getNextEventIndex()) {
				int w1 = env_.map->tileEvents[i + 1];
				if (!eventMatches(w1, mask)) continue;
				if ((w1 & Enums::EVFL_FLAG_SKIP_TURN) != 0) { // :76-79
					env_.game->skipAdvanceTurn = true;
					env_.game->queueAdvanceTurn = false;
				}
				allocFromEvent(t, i, mask, blockInput);
				result = run(t);
			}
		} else {
			t->state = 0;                                    // :87-88
		}
	}
	if (result != 2) freeThread(t);              // Game::executeTile frees non-paused threads (src/Game.cpp:3336-3338)
	return result;
}

int ScriptVM::executeStaticFunc(int idx) {
	if (idx < 0 || idx >= Enums::MAX_BUILT_IN_CMD ||
		env_.map->staticFuncs[idx] == Enums::SCR_NOT_DEFINED) return 0;   // src/Game.cpp:3345-3347
	ScriptThread* t = allocThread();
	if (t == nullptr) return 0;
	allocRaw(t, (int)env_.map->staticFuncs[idx]);
	int result = run(t);
	// Legacy leaves finished static-func threads pooled until the next
	// runScriptThreads sweep; freeing immediately is equivalent (their
	// trigger type is 0 and can never block input).
	if (result != 2) freeThread(t);
	return result;
}

// ---- resumption ----

void ScriptVM::runScriptThreads(int64_t gameTime) {
	if (numThreads_ == 0) return;
	for (int i = 0; i < kMaxThreads; ++i) {
		ScriptThread& t = threads_[i];
		if (!t.inuse) continue;
		if (t.state == 2) attemptResume(&t, gameTime);      // resumes only while Playing (caller gates)
		if (t.state != 2 || t.stackPtr == 0) freeThread(&t); // src/Game.cpp:3263-3265
	}
}

int ScriptVM::attemptResume(ScriptThread* t, int64_t gameTime) {
	if (t->stackPtr == 0) return 1;
	if (t->unpauseTime == -1 || gameTime < (int64_t)t->unpauseTime) return 2; // -1 = external resume will call run()
	t->unpauseTime = 0;
	return (int)run(t);                                     // src/ScriptThread.cpp:2063-2072
}

void ScriptVM::releaseBlockIfUnheld(const char* why) {
	(void)why;                                 // caller tag kept for call-site readability
	if (env_.ctx == nullptr || env_.ctx->blockInputTime == 0) return;
	for (const ScriptThread& t : threads_) {
		if (t.inuse && ((t.flags & 0x1) != 0 ||
			(t.type & Enums::EVFL_FLAG_BLOCKINPUT) != 0)) {
			return;
		}
	}
	env_.ctx->blockInputTime = 0;
}

int ScriptVM::resumeThread(ScriptThread* t) {
	if (!t->inuse) return 0;                  // callThreads inuse check (src/Game.cpp:3009-3013)
	return (int)run(t);
}

// ---- stack / args ----

void ScriptVM::push(ScriptThread* t, int v) {
	if (t->stackPtr >= kStackSize) {
		std::fprintf(stderr, "[script] stack overflow at IP=%d\n", t->IP);
		return;
	}
	t->scriptStack[t->stackPtr++] = v;
}

int ScriptVM::pop(ScriptThread* t) {
	if (t->stackPtr <= 0) {
		std::fprintf(stderr, "[script] stack underflow at IP=%d\n", t->IP);
		return 0;
	}
	return t->scriptStack[--t->stackPtr];
}

// Big-endian, pre-incrementing readers (src/ScriptThread.cpp:2095-2119).
// Legacy getByteArg() returns uint8_t, so single-byte operands read unsigned
// here; opcodes needing a signed value mask explicitly (e.g. GIVEITEM qty).
// Every inline operand is consumed BEFORE the dispatch loop's trailing
// ++t->IP, so jump offsets and CALL_FUNC's `IP = target - 1` are relative to
// "next instruction address minus one" exactly like the original.
uint8_t ScriptVM::readUByte(ScriptThread* t) {
	return (uint8_t)(env_.map->mapByteCode[(size_t)++t->IP] & 0xFF);
}

int8_t ScriptVM::readByte(ScriptThread* t) {
	return (int8_t)env_.map->mapByteCode[(size_t)++t->IP];
}

uint16_t ScriptVM::readUShort(ScriptThread* t) {
	uint16_t n = (uint16_t)(((env_.map->mapByteCode[t->IP + 1] & 0xFF) << 8) |
	                        (env_.map->mapByteCode[t->IP + 2] & 0xFF));
	t->IP += 2;
	return n;
}

int16_t ScriptVM::readShort(ScriptThread* t) {
	int16_t n = (int16_t)((env_.map->mapByteCode[t->IP + 1] << 8) |
	                      (env_.map->mapByteCode[t->IP + 2] & 0xFF));
	t->IP += 2;
	return n;
}

int32_t ScriptVM::readInt(ScriptThread* t) {
	int32_t n = (int32_t)(((uint32_t)env_.map->mapByteCode[t->IP + 1] << 24) |
	                      ((uint32_t)(env_.map->mapByteCode[t->IP + 2] & 0xFF) << 16) |
	                      ((uint32_t)(env_.map->mapByteCode[t->IP + 3] & 0xFF) << 8) |
	                      (uint32_t)(env_.map->mapByteCode[t->IP + 4] & 0xFF));
	t->IP += 4;
	return n;
}

// ---- pause / frame pop ----

int ScriptVM::evWait(ScriptThread* t, int ms) {
	t->unpauseTime = (int)(*env_.gameTime + ms);
	if ((t->flags & 0x1) != 0) {
		env_.ctx->blockInputTime = t->unpauseTime; // ST_CAMERA/AUTOMAP branches n/a in the subset (src/ScriptThread.cpp:120-137)
	}
	return 2;
}

bool ScriptVM::evReturn(ScriptThread* t) {
	while (t->FP < t->stackPtr - 2) pop(t);
	t->FP = pop(t);
	int ip = pop(t);
	if (ip != -1) {
		t->IP = ip;
		return false;
	}
	if (t->stackPtr != 0) {
		std::fprintf(stderr, "[script] frame pointer should be zero at script end (%d)\n", t->stackPtr);
	}
	return true;                                            // src/ScriptThread.cpp:139-153
}

void ScriptVM::updateScriptVars() {
	vars[0] = 0;                                   // status effects n/a
	vars[1] = (short)env_.player->getHealth();
	vars[2] = (short)(env_.player->viewX >> 6);
	vars[3] = (short)(env_.player->viewY >> 6);
	vars[8] = env_.player->inventory[24];
	vars[14] = 0;                                  // character choice
	vars[16] = 0;                                  // familiar
	// vars[12] (difficulty) is stamped at load only (src/Game.cpp:3461-3471).
}

bool ScriptVM::isInputBlockedByScript() const {
	if (numThreads_ > 0) {
		for (const ScriptThread& t : threads_) {
			if (t.inuse && (t.type & Enums::EVFL_FLAG_BLOCKINPUT) != 0) return true;
		}
	}
	return false;
}

// ---- dispatch loop ----

uint32_t ScriptVM::run(ScriptThread* t) {
	updateScriptVars();                            // refreshed shared slots each entry
	if (t->stackPtr == 0) return 1;
	int n = 1;
	const uint8_t* bc = env_.map->mapByteCode.data();

	while (t->IP < env_.map->mapByteCodeSize && n != 2) {
		bool writeVar7 = true;                     // legacy `b`: whether this opcode reports its fail flag into vars[7]
		short n2 = 0;

		switch (bc[t->IP]) {
		case Enums::EV_EVAL: {
			writeVar7 = false;
			int termCount = readUByte(t);      // count operand: legacy getByteArg() is uint8_t (src/ScriptThread.cpp:247,2099-2101)
			while (--termCount >= 0) {
				int term = readUByte(t);
				if ((term & Enums::EVAL_VARFLAG) != 0) {
					push(t, vars[term & Enums::EVAL_VARMASK]);
				} else if ((term & Enums::EVAL_CONSTFLAG) != 0) {
					int next = readUByte(t);
					push(t, ((((term & Enums::EVAL_CONSTMASK) << 8) | next) << 18) >> 18); // 14-bit sign-extended constant
				} else {
					switch (term) {
					case Enums::EVAL_AND: push(t, (pop(t) == 1 && pop(t) == 1) ? 1 : 0); break;
					case Enums::EVAL_OR:  push(t, (pop(t) == 1 || pop(t) == 1) ? 1 : 0); break;
					case Enums::EVAL_LTE: { int a = pop(t); int b = pop(t); push(t, (b <= a) ? 1 : 0); break; }
					case Enums::EVAL_LT:  { int a = pop(t); int b = pop(t); push(t, (b < a) ? 1 : 0); break; }
					case Enums::EVAL_EQ:  { int a = pop(t); int b = pop(t); push(t, (a == b) ? 1 : 0); break; }
					case Enums::EVAL_NEQ: { int a = pop(t); int b = pop(t); push(t, (a != b) ? 1 : 0); break; }
					case Enums::EVAL_NOT: push(t, (pop(t) == 0) ? 1 : 0); break;
					default: break;
					}
				}
			}
			int offset = readUByte(t);
			if (pop(t) == 0) t->IP += offset;      // forward-only false jump (src/ScriptThread.cpp:306-310)
			break;
		}

		case Enums::EV_JUMP: {
			writeVar7 = false;
			t->IP += readUShort(t);                // forward-only (src/ScriptThread.cpp:314-318)
			break;
		}

		case Enums::EV_RETURN:
			if (evReturn(t)) {                     // script complete
				return 1;
			}
			break;

		case Enums::EV_MESSAGE: {
			int v = readUShort(t);
			int strIdx = v & 0x7FFF;
			std::string text = env_.loc->get(kTextMap, strIdx); // loadMapStringID = kTextMap + (mapID-1); map00 -> kTextMap
			if ((v & 0x8000) != 0) {
				std::fprintf(stderr, "[script] MESSAGE str=%d important \"%s\"\n", strIdx, text.c_str());
				env_.hud->showImportantMessage(text);
			} else {
				std::fprintf(stderr, "[script] MESSAGE str=%d \"%s\"\n", strIdx, text.c_str());
				env_.hud->showCenterMessage(text, 0xAA000000, 3500);
			}
			break;
		}

		case Enums::EV_SETSTATE: {
			int idx = readUByte(t);                // uint8_t id (src/ScriptThread.cpp:416); guard drops OOB like legacy UB
			short value = readShort(t);
			if (idx >= 0 && idx < kNumStateVars) vars[idx] = value; // src/ScriptThread.cpp:414-420
			break;
		}

		case Enums::EV_CALL_FUNC: {
			int ip = readUShort(t);
			int fp = t->FP;
			t->FP = t->stackPtr;
			push(t, t->IP);
			push(t, fp);
			t->IP = ip - 1;                        // compensates the trailing ++IP (src/ScriptThread.cpp:422-431)
			break;
		}

		case Enums::EV_ITEM_COUNT: {
			int packed = readUShort(t);
			int cls = packed & 0x1F;
			int idx = (packed >> 5) & 0x1F;
			int dst = readUByte(t);                // uint8_t destination (src/ScriptThread.cpp:439); guard drops OOB like legacy UB
			int value = 0;
			if (cls == 0) value = env_.player->inventory[idx];
			else if (cls == 1) value = ((env_.player->weapons >> idx) & 1) != 0 ? 1 : 0;
			else if (cls == 2) value = env_.player->ammo[idx];
			if (dst >= 0 && dst < kNumStateVars) vars[dst] = (short)value;
			std::fprintf(stderr, "[script] ITEM_COUNT %s[%d]=%d -> var%d\n",
				cls == 0 ? "inv" : (cls == 1 ? "wpn" : "ammo"), idx, value, dst);
			break;
		}

		case Enums::EV_TILE_EMPTY: {
			int packed = readUShort(t);
			int tx = packed & 0x1F;
			int ty = (packed >> 5) & 0x1F;
			int dst = readUByte(t);                // uint8_t destination (src/ScriptThread.cpp:461); guard drops OOB like legacy UB
			short empty = 1;
			for (Entity* e = env_.game->findMapEntity(tx, ty); e != nullptr; e = e->nextOnTile) {
				// Empty iff every linked entity has eType==12 or 1<<eType & 0x6240
				// (src/ScriptThread.cpp:456-475).
				if (e->def && e->def->eType != 12 && (1 << e->def->eType & 0x6240) == 0) {
					empty = 0;
					break;
				}
			}
			if (dst >= 0 && dst < kNumStateVars) vars[dst] = empty;
			break;
		}

		case Enums::EV_DOOROP: {
			int args = readUShort(t);
			int op = args >> 10;
			// Legacy n43 (src/ScriptThread.cpp:751): quiet-bit clear AND not
			// in the automap -> interactive; the automap half has no
			// counterpart here (no automap state in the subset).
			bool interactive = (op & 4) == 0;
			int act = op & 3;
			// n2 forwarded to performDoorEvent (src/ScriptThread.cpp:760):
			// 1 animates, 0 snaps the animation instantly.
			int snapMode = interactive ? 1 : 0;
			static const char* kActNames[4] = { "open", "unlock+open", "lock", "unlock" };
			std::fprintf(stderr, "[script] DOOROP sprite=%d %s%s\n",
				args & 0x3FF, kActNames[act], interactive ? " (blocking)" : "");
			Entity* ent = env_.game->findEntityBySprite(args & 0x3FF);
			if (ent == nullptr) break;             // silent, src/ScriptThread.cpp:753-779
			if (act == 0 || act == 1) {
				if (act == 1 && ent->isDoor()) env_.game->setLineLocked(ent, false);
				bool doorOk = env_.game->performDoorEvent(act, ent, snapMode, interactive ? t : nullptr);
				if (doorOk && interactive) {
					t->unpauseTime = -1;           // resumed by the door-lerp completion
					n = 2;
				}
			} else if (act == 2) {
				env_.game->setLineLocked(ent, true);
				if (interactive) std::fprintf(stderr, "[script] sound 1065\n");
			} else {
				env_.game->setLineLocked(ent, false);
				if (interactive) std::fprintf(stderr, "[script] sound 1065\n");
			}
			break;
		}

		case Enums::EV_EVENTOP: {
			int v = readUShort(t);
			int idx = v & 0x7FFF;
			if (idx < env_.map->numTileEvents) {
				int& w1 = env_.map->tileEvents[idx * 2 + 1];
				w1 = (w1 & ~Enums::EVFL_FLAG_DISABLE) | (((v >> 15) & 1) << Enums::EVFL_DISABLE_SHIFT);
				std::fprintf(stderr, "[script] EVENTOP %s event[%d]\n",
					((v >> 15) & 1) != 0 ? "enable" : "disable", idx);
			}
			break;
		}

		case Enums::EV_HIDE: {
			int sprite = readUByte(t);
			if (sprite < env_.map->numSprites) {
				env_.map->mapSpriteInfo[sprite] |= 0x10000;
				Entity* ent = env_.game->findEntityBySprite(sprite);
				if (ent != nullptr) {
					ent->info |= Entity::kInfoActivated;
					env_.game->unlinkEntity(ent);
					const EntityDef* def = ent->def;
					// foundLoot/destroyedObject/corpseify side effects logged only (spec §7.4).
					if (def && def->eType == 10 && def->eSubType != 3) {
						std::fprintf(stderr, "[script] HIDE sprite=%d destroyedObject\n", sprite);
					} else if (def && def->eType == 6 &&
						(def->eSubType == 1 || def->eSubType == 2 || (def->eSubType == 0 && def->parm == 21))) {
						std::fprintf(stderr, "[script] HIDE sprite=%d foundLoot\n", sprite);
					} else if (def && def->eType == 2) {
						std::fprintf(stderr, "[script] HIDE sprite=%d corpseify (no monsters yet)\n", sprite);
					}
				}
			}
			break;
		}

		case Enums::EV_PREVSTATE: {
			int idx = readUByte(t);                // uint8_t id (src/ScriptThread.cpp:864); guard drops OOB like legacy UB
			if (idx >= 0 && idx < kNumStateVars) --vars[idx];
			break;
		}

		case Enums::EV_NEXTSTATE: {
			int idx = readUByte(t);                // uint8_t id (src/ScriptThread.cpp:871); guard drops OOB like legacy UB
			if (idx >= 0 && idx < kNumStateVars) ++vars[idx];
			break;
		}

		case Enums::EV_GIVEITEM: {
			// NOTE (spec C4): Player::give kinds 0/1/2 coincide with legacy
			// IT_INVENTORY/IT_WEAPON/IT_AMMO — do not renumber either side.
			int defId = readUByte(t);
			int qtyByte = readUByte(t);
			int8_t mode = readByte(t);
			if (mode == 0) {
				std::fprintf(stderr, "[script] GIVEITEM sprite-touch give unsupported\n");
				break;
			}
			const EntityDef* def = env_.defs->lookup(defId);   // BY tileIndex (src/EntityDef.cpp:76-83)
			if (def == nullptr) {
				std::fprintf(stderr, "[script] GIVEITEM def=%d NOT FOUND (Err109)\n", defId);
				n2 = 1;
				break;
			}
			int qty = (int8_t)qtyByte;
			bool ok = env_.player->give(def->eSubType, def->parm, qty); // direct give (legacy spawns drop+touched; identical net inventory effect for keycards)
			std::fprintf(stderr, "[script] GIVEITEM def=%d qty=%d -> %s\n", defId, qty, ok ? "ok" : "FAIL");
			if (!ok) n2 = 1;
			break;
		}

		case Enums::EV_WAIT: {
			int ms = readUByte(t) * 100;
			std::fprintf(stderr, "[script] WAIT %dms\n", ms);
			n = evWait(t, ms);
			break;
		}

		case Enums::EV_ABORT_MOVE:
			env_.game->abortMove = true;           // src/ScriptThread.cpp:663-667
			break;

		case Enums::EV_ENTITY_FRAME: {            // src/ScriptThread.cpp:669-688
			int sprite = readUByte(t);
			int frame = readUByte(t);
			int timeMs = readUByte(t) * 100;
			if (sprite < env_.map->numSprites) {
				env_.map->mapSpriteInfo[sprite] =
					(env_.map->mapSpriteInfo[sprite] & 0xFFFF00FF) | (frame << 8);
				std::fprintf(stderr, "[script] ENTITY_FRAME sprite=%d frame=%d\n", sprite, frame);
				Entity* ent = env_.game->findEntityBySprite(sprite); // S_ENT lookup analog (src/Game.h:98)
				if (ent != nullptr) {
					ent->info |= Entity::kInfoActivated;
					// monster->frameTime = 0x7FFFFFFF anim-freeze (:679-681) omitted —
					// EntityMonster does not exist yet.
				}
			} else {
				std::fprintf(stderr, "[script] ENTITY_FRAME sprite=%d OUT OF RANGE\n", sprite);
			}
			// canvas->staleView n/a: World3D re-reads mapSpriteInfo every frame.
			if (timeMs > 0) n = evWait(t, timeMs); // legacy honors the optional wait
			break;
		}

		case Enums::EV_ADVANCETURN:
			// snap-monsters/endMonstersTurn parts are placeholders (no monsters).
			env_.game->advanceTurn();
			break;

		case Enums::EV_DEBUGPRINT: {
			// Consecutive 66 ops chain into one message in legacy; printing
			// per-op keeps the same information on stderr (cosmetic deviation).
			int mode = readUByte(t);
			std::string out;
			if (mode == 0) {
				for (char c = (char)readUByte(t); c != '\0'; c = (char)readUByte(t)) out += c;
			} else if (mode == 1) {
				int idx = readUByte(t);
				if (idx >= 0 && idx < kNumStateVars) out = std::to_string(vars[idx]);
			}
			std::fprintf(stderr, "[script] DEBUGPRINT %s\n", out.c_str());
			break;
		}

		// ---- TIER-B: parsed, logged, no-op (args consumed exactly) ----

		case Enums::EV_LERPSPRITE: {
			int packed = readUByte(t) | readUByte(t) << 8 | readUByte(t) << 16;
			int lsFlags = packed & 0xF;
			if (!(lsFlags & Enums::SCRIPT_LS_DEFAULT_Z)) readUByte(t); // dstZ
			if (!(lsFlags & Enums::SCRIPT_LS_NO_TIME)) readUByte(t);   // travel time
			std::fprintf(stderr, "[script] LERPSPRITE sprite=%d skipped (no lerp system)\n", (packed >> 14) & 0xFF);
			break;
		}

		case Enums::EV_LERPSPRITEOFFSET: {
			readUByte(t);                 // sprite
			readUByte(t);                 // travel time
			readInt(t);                   // packed offsets
			std::fprintf(stderr, "[script] LERPSPRITEOFFSET skipped (no lerp system)\n");
			break;
		}

		case Enums::EV_STARTCINEMATIC: {
			int cam = readByte(t);
			std::fprintf(stderr, "[script] STARTCINEMATIC camera=%d skipped (no camera system)\n", cam);
			break;
		}

		case Enums::EV_ADV_CAMERAKEY: {
			readUByte(t);
			std::fprintf(stderr, "[script] ADV_CAMERAKEY skipped (no camera system)\n");
			break;
		}

		case Enums::EV_CAMERA_STR: {
			int v = readUShort(t);
			int timeMs = readUShort(t);
			std::fprintf(stderr, "[script] CAMERA_STR str=%d time=%d skipped (no camera system)\n", v & 0x3FFF, timeMs);
			break;
		}

		case Enums::EV_DIALOG: {
			// Dialogs-lite bring-up: legacy startDialog switches to ST_DIALOG,
			// claims skipAdvanceTurn and parks the thread at unpauseTime=-1
			// (src/ScriptThread.cpp:545-585); DialogSystem::closeDialog resumes
			// it via dialogThread->run() (src/DialogSystem.cpp:559-562). The
			// port shows the text through Hud and lets GameContext route the
			// dismiss key (Action::Use == ACTION_FIRE) to resumeThread.
			int strId = readUByte(t);
			int styleByte = readUByte(t);
			std::string text = env_.loc->get(kTextMap, strId); // loadMapStringID analog; map00 -> kTextMap
			std::fprintf(stderr, "[script] DIALOG str=%d style=%d type=%d \"%s\" (dialog-lite)\n",
				strId, styleByte & 0xF, styleByte >> 4, text.c_str());
			t->unpauseTime = -1;                               // external resume on dismiss (:582)
			if (!env_.ctx->enterScriptDialog(t)) {             // ST_DIALOG modal analog
				// Refused (already modal): this thread just stays frozen at -1,
				// exactly like legacy background threads under ST_DIALOG
				// (src/Game.cpp:3259). No text swap, no turn claim, no re-park.
				n = 2;
				break;
			}
			env_.hud->showDialogMessage(text);
			env_.game->skipAdvanceTurn = true;                 // src/ScriptThread.cpp:580-581
			env_.game->queueAdvanceTurn = false;
			n = 2;
			break;
		}

		case Enums::EV_PLAYSOUND: {
			int id = readUByte(t);
			readUByte(t);                 // vol/priority nibbles
			std::fprintf(stderr, "[script] PLAYSOUND id=%d\n", id + 1000);
			break;
		}

		case Enums::EV_STOPSOUND: {
			int id = readUByte(t);
			std::fprintf(stderr, "[script] STOPSOUND id=%d\n", id + 1000);
			break;
		}

		case Enums::EV_FADEOP: {
			int v = readUShort(t);
			std::fprintf(stderr, "[script] FADEOP %s dur=%d skipped (no fades)\n",
				(v & 0x8000) != 0 ? "out" : "in", v & 0x7FFF);
			break;
		}

		case Enums::EV_CHANGE_MAP: {
			// Parse-validate-only: multi-map change is out of scope; mutate nothing.
			int b = readUByte(t);
			int s = readUShort(t);
			int nextMap = b & 0xF;
			int dir = (b >> 4) & 7;
			int tile = s & 0x3FF;
			int fade = (b & 0x80) != 0 ? 1 : 0;
			std::fprintf(stderr, "[EV_CHANGE_MAP] map=%d dir=%d tile=%d fade=%d (parse-only)\n",
				nextMap, dir, tile, fade);
			break;
		}

		case Enums::EV_NAMEENTITY: {
			int sprite = readUByte(t);
			int nameId = readUByte(t);
			std::fprintf(stderr, "[script] NAMEENTITY sprite=%d name=%d skipped (entity names not ported)\n", sprite, nameId);
			break;
		}

		case Enums::EV_SETDEATHFUNC: {
			int sprite = readUByte(t);
			readShort(t);                 // death-func ip (-1 unbinds)
			std::fprintf(stderr, "[script] SETDEATHFUNC sprite=%d skipped (no monsters)\n", sprite);
			break;
		}

		case Enums::EV_MONSTERFLAGOP: {
			int sprite = readUByte(t);
			readUByte(t);                 // op/mask
			std::fprintf(stderr, "[script] MONSTERFLAGOP sprite=%d skipped (no monsters)\n", sprite);
			break;
		}

		case Enums::EV_WAKEMONSTER: {
			int sprite = readUByte(t);
			std::fprintf(stderr, "[script] WAKEMONSTER sprite=%d skipped (no monsters)\n", sprite);
			break;
		}

		case Enums::EV_DAMAGEMONSTER: {
			int sprite = readUByte(t);
			readByte(t);                  // damage
			std::fprintf(stderr, "[script] DAMAGEMONSTER sprite=%d skipped (no monsters)\n", sprite);
			break;
		}

		case Enums::EV_DAMAGEPLAYER: {
			int dmg = readByte(t);
			int arm = readByte(t);
			readByte(t);                  // direction
			std::fprintf(stderr, "[script] DAMAGEPLAYER dmg=%d armor=%d skipped (no combat)\n", dmg, arm);
			break;
		}

		case Enums::EV_MARKTILE:
		case Enums::EV_UNMARKTILE: {
			int packed = readUShort(t);
			std::fprintf(stderr, "[script] %s tile=%d,%d skipped (no automap)\n",
				bc[t->IP] == Enums::EV_MARKTILE ? "MARKTILE" : "UNMARKTILE",
				(packed >> 5) & 0x1F, packed & 0x1F);
			break;
		}

		case Enums::EV_UPDATEJOURNAL: {
			int quest = readUByte(t);
			int state = readUByte(t);
			std::fprintf(stderr, "[script] UPDATEJOURNAL quest=%d state=%d skipped (no journal)\n", quest, state);
			break;
		}

		case Enums::EV_JOURNAL_TILE: {
			int quest = readUByte(t);
			readUByte(t);                 // tile x
			readUByte(t);                 // tile y
			std::fprintf(stderr, "[script] JOURNAL_TILE quest=%d skipped (no journal)\n", quest);
			break;
		}

		case Enums::EV_SCREEN_SHAKE: {
			int v = readUShort(t);
			std::fprintf(stderr, "[script] SCREEN_SHAKE dur=%d dx=%d dy=%d skipped\n",
				((v >> 14) & 3) + 1, ((v >> 7) & 0x7F) + 1, (v & 0x7F) + 1);
			break;
		}

		case Enums::EV_SPEECHBUBBLE: {
			readShort(t);                 // texture id
			readUByte(t);                 // color
			std::fprintf(stderr, "[script] SPEECHBUBBLE skipped\n");
			break;
		}

		case Enums::EV_NPCCHAT: {
			int v = readUShort(t);
			std::fprintf(stderr, "[script] NPCCHAT sprite=%d param=%d skipped\n", v & 0x3FFF, (v >> 14) & 3);
			break;
		}

		case Enums::EV_GIVELOOT: {
			int count = readUByte(t);
			for (int i = 0; i < count; ++i) readUShort(t);
			std::fprintf(stderr, "[script] GIVELOOT entries=%d skipped (no loot UI)\n", count);
			break;
		}

		case Enums::EV_TURN_PLAYER: {
			int b = readUByte(t);
			std::fprintf(stderr, "[script] TURN_PLAYER dir=%d animate=%d skipped (teleport contract absent)\n",
				b & 7, (b >> 3) & 1);
			break;
		}

		case Enums::EV_GOTO: {
			int v = readUShort(t);
			std::fprintf(stderr, "[script] GOTO tile=%d,%d face=%d skipped (teleport contract absent)\n",
				(v >> 5) & 0x1F, v & 0x1F, (v >> 10) & 0xF);
			break;
		}

		case Enums::EV_GIVE_AUTOMAP:
			std::fprintf(stderr, "[script] GIVE_AUTOMAP skipped (no automap)\n");
			break;

		case Enums::EV_MINIGAME: {
			int type = readByte(t);
			readByte(t);
			readByte(t);
			readByte(t);
			std::fprintf(stderr, "[script] MINIGAME type=%d skipped (no minigames)\n", type);
			break;
		}

		case Enums::EV_DROPITEM: {
			int packed = readUShort(t);
			readUByte(t);                 // def id
			std::fprintf(stderr, "[script] DROPITEM tile=%d,%d skipped (no drop items)\n",
				packed & 0x1F, (packed >> 5) & 0x1F);
			break;
		}

		// Spec tier tables omit these four but map00's INIT_MAP chain runs
		// through them BEFORE its door lock; consuming their args exactly
		// keeps the thread alive (same rationale as TIER-B).
		case Enums::EV_SET_FOG_COLOR: {
			int argb = readInt(t);
			std::fprintf(stderr, "[script] SET_FOG_COLOR %08X skipped (fog values fixed at boot)\n", argb);
			break;
		}

		case Enums::EV_LERP_FOG: {
			readInt(t);
			std::fprintf(stderr, "[script] LERP_FOG skipped (fog values fixed at boot)\n");
			break;
		}

		case Enums::EV_LERPSCALE: {
			readUShort(t);                // sprite/flags
			readUShort(t);                // ms
			readUByte(t);                 // dst scale
			std::fprintf(stderr, "[script] LERPSCALE skipped (no lerp system)\n");
			break;
		}

		case Enums::EV_ASSIGN_LOOTSET: {
			int sprite = readUShort(t) & 0xFFF;
			int count = readUByte(t);
			for (int i = 0; i < count; ++i) readUShort(t);
			std::fprintf(stderr, "[script] ASSIGN_LOOTSET sprite=%d entries=%d skipped (no loot UI)\n", sprite, count);
			break;
		}

		case Enums::EV_END: {                    // byte value 255
			if (t->stackPtr != 0) {
				std::fprintf(stderr, "[script] EV_END with stackPtr=%d (legacy Error 102)\n", t->stackPtr);
			}
			return 1;
		}

		default:
			// Deliberate replacement of the legacy fatal Error("Cannot handle
			// event: %d") (src/ScriptThread.cpp:2034-2037): a stray opcode
			// must not kill the process during bring-up.
			std::fprintf(stderr, "[script] UNIMPLEMENTED opcode %d at IP=%d\n", bc[t->IP], t->IP);
			t->state = 0;
			return 1;
		}

		if (writeVar7) vars[7] = n2;               // src/ScriptThread.cpp:2040-2042
		++t->IP;                                   // after EVERY opcode (src/ScriptThread.cpp:2043)
	}
	return n;
}

} // namespace newcore
