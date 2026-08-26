#include "domain/game/ScriptVM.h"

#include <cstdio>
#include <string>

#include "core/GameContext.h"
#include "domain/game/DialogSystem.h"
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
	// characterChoice: marine. Legacy forces {1,2,3} from the intro select
	// (src/IntroSequenceManager.cpp:599/628/676, marine=1) and re-stamps
	// scriptStateVars[14] each run (src/Game.cpp:3468); no select UI yet —
	// hardcode marine so EVT 617 takes the v14==1 branch (hides doorway
	// walkers 10+9 @IPs 3297/3299, docs/research/2026-08-26-choice-branch.md).
	vars[14] = 1;
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
			// act 1 unlocks then CLOSES (src/ScriptThread.cpp:757-762 passes
			// n44=1 into performDoorEvent, which closes: slide -32, dstScale
			// 64, sound 1028, src/Game.cpp:1104-1110).
			static const char* kActNames[4] = { "open", "unlock+close", "lock", "unlock" };
			std::fprintf(stderr, "[script] DOOROP sprite=%d %s%s\n",
				args & 0x3FF, kActNames[act], interactive ? " (blocking)" : "");
			Entity* ent = env_.game->findEntityBySprite(args & 0x3FF);
			if (ent == nullptr) break;             // silent, src/ScriptThread.cpp:753-779
			if (act == 0 || act == 1) {
				if (act == 1 && ent->isDoor()) env_.game->setLineLocked(ent, false);
				bool doorOk = env_.game->performDoorEvent(act, ent, snapMode, interactive ? t : nullptr);
				// Door family flips to media frame 1 while open/animating
				// (src/Game.cpp:1150-1152); updateDoors restores frame 0 on
				// close completion.
				if (doorOk && act == 0) {
					int info = env_.map->mapSpriteInfo[args & 0x3FF];
					int tileNum = info & 0xFF;
					if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
					if (tileNum >= 271 && tileNum < 281) {
						env_.map->mapSpriteInfo[args & 0x3FF] =
							(info & 0xFFFF00FF) | (1 << 8);
					}
				}
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
				// bit15 SET = disabled (src/ScriptThread.cpp:808-816).
				std::fprintf(stderr, "[script] EVENTOP %s event[%d]\n",
					((v >> 15) & 1) != 0 ? "disable" : "enable", idx);
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
						// Monster branch (src/ScriptThread.cpp:836-840):
						// corpsifyMonster receives TILE indices where pixel coords
						// are expected (legacy quirk, kept verbatim), then
						// removeEntity re-hides the sprite and unlinks — net
						// effect: the monster VANISHES (no visible corpse).
						env_.game->corpsifyMonster(ent, ent->linkIndex % 32, ent->linkIndex / 32);
						env_.game->removeEntity(ent);
						ent->info |= Entity::kInfoActivated;           // :839 (0x400000)
						std::fprintf(stderr, "[script] HIDE sprite=%d corpsify+remove\n", sprite);
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

		// ---- LERP* sprite family (docs/original-code/lerp-opcodes.md) ----

		case Enums::EV_LERPSPRITE: {               // src/ScriptThread.cpp:344-398
			int packed = readUByte(t) | readUByte(t) << 8 | readUByte(t) << 16;
			int sprite = (packed >> 14) & 0xFF;
			int dstTileX = (packed >> 9) & 0x1F;
			int dstTileY = (packed >> 4) & 0x1F;
			int lsFlags = packed & 0xF;
			int dstZrel = (lsFlags & Enums::SCRIPT_LS_DEFAULT_Z)
				? 32 : (readUByte(t) - 48);
			int time = (lsFlags & Enums::SCRIPT_LS_NO_TIME)
				? 0 : readUByte(t) * 100;
			if (time != 0 && (lsFlags & Enums::SCRIPT_LS_FLAG_BLOCK) != 0) {
				evWait(t, time);                   // up-front (:353-355)
			}
			Game::SpriteLerp* ls = env_.game->allocLerpSprite(
				t, sprite, (lsFlags & Enums::SCRIPT_LS_FLAG_BLOCK) != 0);
			if (ls == nullptr) break;
			ls->dstX = 32 + (dstTileX << 6);       // tile centers (:361-362)
			ls->dstY = 32 + (dstTileY << 6);
			Entity* ent = env_.game->findEntityBySprite(sprite);   // info |= 0x400000 (:364-371)
			if (ent != nullptr) ent->info |= 0x400000;
			ls->dstZ = env_.ctx->getHeight(ls->dstX, ls->dstY) + dstZrel;   // (:370)
			const MapData& m = *env_.map;
			ls->srcX = m.mapSprites[sprite + 0 * m.numSprites];
			ls->srcY = m.mapSprites[sprite + 1 * m.numSprites];
			// Stored Z is raw-relative; rebake to legacy space (src/Render.cpp:2464).
			ls->srcZ = m.mapSprites[sprite + 2 * m.numSprites]
				+ env_.game->spriteZBias(sprite, ls->srcX, ls->srcY);
			ls->srcScale = ls->dstScale =
				m.mapSprites[sprite + 8 * m.numSprites];   // scale held constant
			ls->startTime = env_.game->clockMs();
			ls->travelTime = time;
			ls->flags = lsFlags & Enums::SCRIPT_LS_FLAG_ASYNC_BLOCK;   // scriptBits&3 (:377)
			ls->calcDist();                        // walk-phase distance (src/ScriptThread.cpp:378)
			// TEMP [dbg] lerp audit (remove after bugs #1/#2 verified)
			std::fprintf(stderr, "[dbg] LERPSPRITE spr=%d src=%d,%d,%d dst=%d,%d,%d t=%dms flags=%d\n",
				sprite, ls->srcX, ls->srcY, ls->srcZ, ls->dstX, ls->dstY, ls->dstZ, time, lsFlags);
			if (time == 0) {
				env_.game->updateLerpSprite(ls);   // single tick completes (:379-386)
			} else if ((lsFlags & Enums::SCRIPT_LS_FLAG_ASYNC) == 0) {
				env_.game->skipAdvanceTurn = true; // thread parks until completion (:388-394)
				env_.game->queueAdvanceTurn = false;
				t->unpauseTime = -1;
				n = 2;
			}
			break;
		}

		case Enums::EV_LERPSPRITEOFFSET: {         // src/ScriptThread.cpp:1388-1441
			int sprite = readUByte(t);
			int time = readUByte(t) * 100;
			int packed = readInt(t);
			int dstY = packed & 0x7FF;             // ABSOLUTE fixed coords (:1393-1394)
			int dstX = (packed >> 11) & 0x7FF;     // X in bits 11-21, Y in bits 0-10
			int lsFlags = (packed >> 22) & 0x3;
			int dstZrel = ((packed >> 24) & 0xFF) - 48;
			Game::SpriteLerp* ls = env_.game->allocLerpSprite(
				t, sprite, (lsFlags & 0x2) != 0);
			if (ls == nullptr) break;
			ls->dstX = dstX;
			ls->dstY = dstY;
			Entity* ent = env_.game->findEntityBySprite(sprite);   // info |= 0x400000 (+MFLAG_LERP_SHADOW n/a)
			if (ent != nullptr) ent->info |= 0x400000;
			ls->dstZ = env_.ctx->getHeight(dstX, dstY) + dstZrel;  // (:1410)
			const MapData& m = *env_.map;
			ls->srcX = m.mapSprites[sprite + 0 * m.numSprites];
			ls->srcY = m.mapSprites[sprite + 1 * m.numSprites];
			// Stored Z is raw-relative; rebake to legacy space (src/Render.cpp:2464).
			ls->srcZ = m.mapSprites[sprite + 2 * m.numSprites]
				+ env_.game->spriteZBias(sprite, ls->srcX, ls->srcY);
			ls->srcScale = ls->dstScale =
				m.mapSprites[sprite + 8 * m.numSprites];
			ls->startTime = env_.game->clockMs();
			ls->travelTime = time;
			ls->flags = lsFlags & 0x3;
			ls->calcDist();                        // walk-phase distance (src/ScriptThread.cpp:1417)
			// TEMP [dbg] lerp audit (remove after bugs #1/#2 verified)
			std::fprintf(stderr, "[dbg] LERPOFFSET spr=%d src=%d,%d,%d dst=%d,%d,%d t=%dms flags=%d\n",
				sprite, ls->srcX, ls->srcY, ls->srcZ, ls->dstX, ls->dstY, ls->dstZ, time, lsFlags);
			if (time == 0) {
				env_.game->updateLerpSprite(ls);
			} else {
				if ((lsFlags & 0x2) != 0) evWait(t, time);   // BLOCK after alloc (:1428-1429)
				if ((ls->flags & Game::SpriteLerp::kFlagAsync) == 0) {
					env_.game->skipAdvanceTurn = true;
					env_.game->queueAdvanceTurn = false;
					t->unpauseTime = -1;
					n = 2;
				}
			}
			break;
		}

		// ---- TIER-B: parsed, logged, no-op (args consumed exactly) ----

		case Enums::EV_STARTCINEMATIC: {
			// Bind the map camera and enter ST_CAMERA
			// (src/ScriptThread.cpp:400-411).
			int cam = readUByte(t);
			std::fprintf(stderr, "[script] STARTCINEMATIC camera=%d\n", cam);
			env_.ctx->startCinematic(cam);
			env_.game->skipAdvanceTurn = true;                 // (:409-410)
			env_.game->queueAdvanceTurn = false;
			break;
		}

		case Enums::EV_ADV_CAMERAKEY: {
			// Park until `count` more camera keys completed; the context's
			// Snap analog resumes the thread (src/ScriptThread.cpp:690-702).
			int count = readUByte(t);
			if (!env_.ctx->cameraActive()) break;   // non-camera states consume the arg (:700-701)
			std::fprintf(stderr, "[script] ADV_CAMERAKEY resumes=%d\n", count);
			env_.ctx->advanceCameraKey(t, count);  // parks via unpauseTime=-1
			n = 2;
			break;
		}

		case Enums::EV_CAMERA_STR: {
			// Cinematic subtitle/title (src/ScriptThread.cpp:519-543):
			// packed u16 = str id bits0-13 | bit14 showCinPlayer (portrait
			// has no rewrite counterpart) | bit15 title; second u16 = ms.
			int v = readUShort(t);
			int timeMs = readUShort(t);
			int strIdx = v & 0x3FFF;
			std::string text = env_.loc->get(kTextMap, strIdx);
			if ((v & 0x8000) != 0) {
				env_.hud->setCinTitle(text, timeMs);
				std::fprintf(stderr, "[script] CAMERA_STR TITLE str=%d \"%s\" time=%dms\n",
					strIdx, text.c_str(), timeMs);
			} else {
				env_.hud->setSubtitle(text, timeMs);
				std::fprintf(stderr, "[script] CAMERA_STR SUBTITLE str=%d \"%s\" time=%dms\n",
					strIdx, text.c_str(), timeMs);
			}
			break;
		}

		case Enums::EV_DIALOG: {
			// Full dialogs (spec GROUP 1): legacy decodes style = lo nibble,
			// flags = hi nibble, then startDialog/enqueueHelp and parks the
			// thread at unpauseTime == -1 (src/ScriptThread.cpp:545-584).
			int strId = readUByte(t);
			int packed = readUByte(t);
			int style = packed & 0xF;                          // n32 (:550)
			int flags = packed >> 4;                           // n31 (:549)
			// ST_AUTOMAP force-exit (:551-554): no automap state in the subset.
			if (env_.game->skipDialog) {                       // (:555-557)
				std::fprintf(stderr, "[script] DIALOG str=%d style=%d skipped (skipDialog)\n", strId, style);
				break;
			}
			// Styles 6/7/1 clear player->inCombat (:558-560): combat not ported.
			if (style == 2) {
				// Style 2 opens NO box — it enqueues a help popup bound to this
				// thread's pool index (:561-573). player->prevWeapon save is
				// not ported (no weapon-swap restore path).
				if (!env_.dialogs->enqueueHelpDialog(kTextMap, strId, indexOf(t))) {
					break;                         // queue full -> no park
				}
			} else {
				env_.dialogs->startDialog(t, kTextMap, strId, style, flags, true);  // (:574-579)
			}
			env_.game->skipAdvanceTurn = true;                 // (:580-581)
			env_.game->queueAdvanceTurn = false;
			t->unpauseTime = -1;                               // external resume on close (:582)
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

		case Enums::EV_SPAWN_PARTICLES: {
			// u8 packed(count<<3|color, bit8=absolute-tile mode), u16 pos,
			// i8 z(+48) (src/ScriptThread.cpp:921-936); no particle system yet.
			int packed = readUByte(t);
			readUShort(t);                // sprite id or packed tile pos
			readUByte(t);                 // dz (+48)
			std::fprintf(stderr, "[script] SPAWN_PARTICLES count=%d color=%d skipped\n",
				(packed >> 3) & 0xF, packed & 0x7);
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

		case Enums::EV_DAMAGEMONSTER: {          // src/ScriptThread.cpp:704-724
			int sprite = readUByte(t);
			int dmg = readByte(t);
			std::fprintf(stderr, "[script] DAMAGEMONSTER sprite=%d dmg=%d\n", sprite, dmg);
			Entity* ent = env_.game->findEntityBySprite(sprite);
			if (ent == nullptr || !ent->isMonster()) {
				std::fprintf(stderr, "[script] DAMAGEMONSTER sprite=%d skipped (%s)\n",
					sprite, ent == nullptr ? "no entity" : "not a monster");
				break;
			}
			// Legacy pains then dies when lethal; died(false,nullptr) leaves the
			// VISIBLE corpse in place (death frame 0x7000, src/Entity.cpp:1627).
			// No health model yet, so every scripted hit is treated as lethal —
			// map00's only site (IP 2831, imp 15, dmg 127) is lethal anyway.
			const MapData& m = *env_.map;
			env_.game->corpsifyMonster(ent, m.mapSprites[sprite + 0 * m.numSprites],
				m.mapSprites[sprite + 1 * m.numSprites]);
			break;
		}

		case Enums::EV_DISABLED_WEAPONS: {
			// Legacy stores the s16 bitmask into player->disabledWeapons and may
			// switch away from a masked weapon (src/ScriptThread.cpp:1443-1451);
			// combat/weapon UI has no rewrite counterpart yet, so consume the
			// operand and continue. Unblocks the elevator-cinematic tail
			// (map00 IP 2835 — the thread killer of research
			// 2026-08-25-unhandled-script-events.md §1.1).
			int weaponMask = readShort(t);
			std::fprintf(stderr, "[script] DISABLED_WEAPONS mask=%d consumed\n", weaponMask);
			break;
		}

		case Enums::EV_MAKE_CORPSE: {    // src/ScriptThread.cpp:1614-1625
			int sprite = readUShort(t) & 0xFFF;
			int dstX = readUByte(t);                 // dst tile x
			int dstY = readUByte(t);                 // dst tile y
			Entity* ent = env_.game->findEntityBySprite(sprite);
			// Legacy corpsifies only entities with a Monster struct
			// (:1618-1620), else silent no-op; EntityMonster is not ported,
			// the monster-family def check stands in.
			if (ent == nullptr || !ent->isMonster()) {
				std::fprintf(stderr, "[script] MAKE_CORPSE sprite=%d skipped (no monster entity)\n", sprite);
				break;
			}
			env_.game->corpsifyMonster(ent, (dstX << 6) + 32, (dstY << 6) + 32); // pixel tile-centers
			std::fprintf(stderr, "[script] MAKE_CORPSE sprite=%d -> corpse @ tile %d,%d\n",
				sprite, dstX, dstY);
			break;
		}

		// No-operand / one-byte HUD+fog toggles on the map00 init path
		// (src/ScriptThread.cpp:1710-1726); fog effects have no rewrite
		// target yet.
		case Enums::EV_ENTITY_BREATHES: {  // src/ScriptThread.cpp:1907-1920
			// Sits between the squad walk-in and the imp parabola of camera 5;
			// consuming it keeps that thread alive (info bit 0x20000000 has no
			// rewrite consumer yet).
			int sprite = readUByte(t);
			int mode = readUByte(t);
			Entity* ent = env_.game->findEntityBySprite(sprite);
			if (ent != nullptr) {
				if (mode == 1) ent->info &= ~0x20000000;
				else if (mode == 0) ent->info |= 0x20000000;
			}
			break;
		}

		case Enums::EV_TOGGLE_OVERLAY:
			env_.hud->setCockpitOverlay(!env_.hud->cockpitOverlay());   // (:1710-1714)
			std::fprintf(stderr, "[script] TOGGLE_OVERLAY -> %d\n",
				env_.hud->cockpitOverlay() ? 1 : 0);
			break;

		case Enums::EV_FOG_AFFECTS_SKYMAP:
			readUByte(t);
			std::fprintf(stderr, "[script] FOG_AFFECTS_SKYMAP skipped\n");
			break;

		case Enums::EV_ENABLE_HELP:
			readUByte(t);
			std::fprintf(stderr, "[script] ENABLE_HELP skipped (no help system)\n");
			break;

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

		case Enums::EV_SCREEN_SHAKE: {            // src/ScriptThread.cpp:1228-1245
			int v = readUShort(t);
			int durUnits = (v >> 14) & 0x3;       // n90 (:1231-1234), always ++'d to 1..4
			if (durUnits >= 0) ++durUnits;
			int dx = (v >> 7) & 0x7F;             // n91: carries the DURATION ms
			if (dx > 0) dx = (dx + 1) << 4;       // (:1235-1238)
			int dy = v & 0x7F;                    // n92: vibration strength, unused here
			if (dy > 0) dy = (dy + 1) << 4;       // (:1239-1242)
			// Canvas::startShake(dx, durUnits, dy) — the dx field is the
			// duration, durUnits*2 the amplitude (src/Canvas.cpp:1008-1011);
			// vibration (dy) has no desktop counterpart.
			if (env_.hud != nullptr) {
				env_.hud->startShake(env_.ctx ? env_.ctx->upTimeMs : 0, dx,
					2 * durUnits);
				std::fprintf(stderr, "[script] SCREEN_SHAKE dur=%dms amp=%d\n",
					dx, 2 * durUnits);
			}
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
			// Shortest-arc turn to facing dir<<7, 1024 = full circle
			// (src/ScriptThread.cpp:1547-1574). bits0-2 target facing
			// (=angle/128), bit3 animate (strict ==1 like legacy n34).
			int b = readUByte(t);
			int viewAngle = env_.player->viewAngle & 0x3FF;
			int destAngle = (b & 7) << 7;
			if (destAngle - viewAngle > 512) destAngle -= 1024;
			else if (destAngle - viewAngle < -512) destAngle += 1024;
			if (viewAngle == destAngle) break;                 // already facing (:1560-1562)
			if ((b >> 3) == 1) {                               // animated (:1563-1571)
				env_.player->viewAngle = viewAngle;
				env_.player->destAngle = destAngle;
				env_.player->startRotation();
				env_.ctx->gotoThread_ = t;         // resumed by the rotation arrival
				t->unpauseTime = -1;
				n = 2;
			} else {                                           // instant snap (:1572)
				env_.player->viewAngle = env_.player->destAngle = destAngle;
			}
			break;
		}

		case Enums::EV_GOTO: {
			// Scripted player move (src/ScriptThread.cpp:593-661;
			// cutscenes-camera.md §4). u16 packed: bits0-4 dstY tile,
			// bits5-9 dstX tile, bits10-13 face dir (15 = keep angle),
			// bit14 animated, bit15 advance-turn-after.
			int v = readUShort(t);
			Player& p = *env_.player;
			bool animate = (v & 0x4000) != 0;
			p.destX = (((v >> 5) & 0x1F) << 6) + 32;           // (:604)
			p.destY = ((v & 0x1F) << 6) + 32;                  // (:605)
			p.destZ = env_.ctx->getHeight(p.destX, p.destY) + 36; // (:606)
			int face = (v >> 10) & 0xF;
			// TEMP [dbg] GOTO decode audit (remove after bug #1 verified)
			std::fprintf(stderr, "[dbg] GOTO raw=0x%04X tile=%d,%d dest=%d,%d destZ=%d face=%d anim=%d adv=%d\n",
				v, (v >> 5) & 0x1F, v & 0x1F, p.destX, p.destY, p.destZ, face, animate ? 1 : 0,
				(v & 0x8000) ? 1 : 0);
			// viewPitch/viewRoll zeroing (:608-610) n/a — no pitch/roll in the
			// rewrite yet; knockbackDist=0 likewise.
			if (animate) {
				if (face != 15) {                              // shortest arc (:612-623)
					int viewAngle = p.viewAngle & 0x3FF;
					int destAngle = face << 7;
					if (destAngle - viewAngle > 512) destAngle -= 1024;
					else if (destAngle - viewAngle < -512) destAngle += 1024;
					p.viewAngle = viewAngle;
					p.destAngle = destAngle;
				}
				p.startRotation();                             // startRotation(false) (:624)
				p.setZStep(p.destZ - p.viewZ);                 // (:625)
				if (p.destX != p.viewX || p.destY != p.viewY || p.viewAngle != p.destAngle) {
					env_.ctx->gotoThread_ = t;     // arrival resumes via tickPlaying (:627)
					t->unpauseTime = -1;
					n = 2;
				}
			} else {
				p.viewX = p.destX;                             // snap (:636-638)
				p.viewY = p.destY;
				p.viewZ = p.destZ;
				if (face != 15) {                              // ABSOLUTE angle (:639-642)
					p.viewAngle = p.destAngle = face << 7;
					env_.ctx->finishRotationFired();           // finishRotation(true) FACE events
				}
				if ((v & 0x8000) != 0) env_.game->advanceTurn(); // (:643-645)
				if (env_.ctx->state != StateId::Camera) p.startRotation(); // (:646-649)
				env_.ctx->gotoTriggered_ = true;   // destination events next tick (:653)
			}
			// relink()/clearEvents(1)/updateFacingEntity/invalidateRect
			// (:656-659) n/a — player entity is unlinked and input events are
			// consumed per tick already.
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

		case Enums::EV_LERPSCALE: {                // src/ScriptThread.cpp:1453-1498
			int packed = readUShort(t);
			int sprite = packed >> 4;
			int lsFlags = packed & 0xF;
			int timeMs = readUShort(t);            // ms directly, no *100
			int scaleByte = readUByte(t);
			Game::SpriteLerp* ls = env_.game->allocLerpSprite(
				t, sprite, (lsFlags & 0x2) != 0);
			if (ls == nullptr) break;
			const MapData& m = *env_.map;
			ls->srcX = ls->dstX = m.mapSprites[sprite + 0 * m.numSprites];   // position/Z held
			ls->srcY = ls->dstY = m.mapSprites[sprite + 1 * m.numSprites];
			// Stored Z is raw-relative; rebake to legacy space (src/Render.cpp:2464).
			ls->srcZ = ls->dstZ = m.mapSprites[sprite + 2 * m.numSprites]
				+ env_.game->spriteZBias(sprite, ls->srcX, ls->srcY);
			ls->srcScale = m.mapSprites[sprite + 8 * m.numSprites];
			ls->dstScale = scaleByte << 1;         // 64 = 1.0 (:1468)
			Entity* ent = env_.game->findEntityBySprite(sprite);
			if (ent != nullptr) ent->info |= 0x400000;   // (:1469-1472)
			ls->startTime = env_.game->clockMs();
			ls->travelTime = timeMs;
			ls->flags = lsFlags & 0x3;
			ls->calcDist();                        // position held => dist 0 (src/ScriptThread.cpp:1683)
			std::fprintf(stderr, "[script] LERPSCALE sprite=%d dstScale=%d t=%dms flags=%d\n",
				sprite, ls->dstScale, timeMs, lsFlags);
			if (timeMs == 0) {
				env_.game->updateLerpSprite(ls);
			} else {
				if ((lsFlags & 0x2) != 0) evWait(t, timeMs);
				if ((ls->flags & Game::SpriteLerp::kFlagAsync) == 0) {
					env_.game->skipAdvanceTurn = true;
					env_.game->queueAdvanceTurn = false;
					t->unpauseTime = -1;
					n = 2;
				}
			}
			break;
		}

		case Enums::EV_LERPSPRITEPARABOLA:         // src/ScriptThread.cpp:1653-1708
		case Enums::EV_LERPSPRITEPARABOLA_SCALE: { // :1962-2019
			int op = bc[t->IP];                    // captured before operand reads advance IP
			int packed = readInt(t);
			int timeMs = readUShort(t);
			int sprite = (packed >> 22) & 0x3FF;
			int dstTileX = (packed >> 17) & 0x1F;
			int dstTileY = (packed >> 12) & 0x1F;
			int arcHeight = ((packed >> 4) & 0xFF) - 48;
			int lsFlags = packed & 0xF;
			int scaleByte = -1;
			if (op == Enums::EV_LERPSPRITEPARABOLA_SCALE) {
				scaleByte = readUByte(t);
			}
			Game::SpriteLerp* ls = env_.game->allocLerpSprite(
				t, sprite, (lsFlags & 0x2) != 0);
			if (ls == nullptr) break;
			const MapData& m = *env_.map;
			ls->dstX = 32 + (dstTileX << 6);       // (:1662-1663)
			ls->dstY = 32 + (dstTileY << 6);
			Entity* ent = env_.game->findEntityBySprite(sprite);   // info |= 0x400000; monster shadow bit CLEARED (:1672,1982)
			if (ent != nullptr) ent->info |= 0x400000;
			ls->srcX = m.mapSprites[sprite + 0 * m.numSprites];
			ls->srcY = m.mapSprites[sprite + 1 * m.numSprites];
			// Stored Z is raw-relative; rebake to legacy space (src/Render.cpp:2464).
			ls->srcZ = m.mapSprites[sprite + 2 * m.numSprites]
				+ env_.game->spriteZBias(sprite, ls->srcX, ls->srcY);
			// Relative landing height preserved (:1678): dstZ = h(dst)+srcZ-h(src).
			ls->dstZ = env_.ctx->getHeight(ls->dstX, ls->dstY)
				+ (ls->srcZ - env_.ctx->getHeight(ls->srcX, ls->srcY));
			ls->srcScale =
				m.mapSprites[sprite + 8 * m.numSprites];
			ls->dstScale = (scaleByte >= 0) ? scaleByte << 1 : ls->srcScale;
			ls->height = arcHeight;
			ls->startTime = env_.game->clockMs();
			ls->travelTime = timeMs;
			ls->flags = (lsFlags & 0x3) | Game::SpriteLerp::kFlagParabola;   // (:1684-1685)
			ls->calcDist();                        // walk-phase distance (src/ScriptThread.cpp:1994)
			std::fprintf(stderr, "[script] %s sprite=%d tile=%d,%d h=%d t=%dms flags=%d\n",
				op == Enums::EV_LERPSPRITEPARABOLA ? "LERPSPRITEPARABOLA"
				                                   : "LERPSPRITEPARABOLA_SCALE",
				sprite, dstTileX, dstTileY, arcHeight, timeMs, lsFlags);
			if (timeMs == 0) {
				env_.game->updateLerpSprite(ls);
			} else {
				if ((lsFlags & 0x2) != 0) evWait(t, timeMs);
				if ((ls->flags & Game::SpriteLerp::kFlagAsync) == 0) {
					env_.game->skipAdvanceTurn = true;
					env_.game->queueAdvanceTurn = false;
					t->unpauseTime = -1;
					n = 2;
				}
			}
			break;
		}

		case Enums::EV_ASSIGN_LOOTSET: { // src/ScriptThread.cpp:1782-1804
			int sprite = readUShort(t) & 0xFFF;
			int count = readUByte(t);
			// The entity resolves through mapSprites[S_ENT] (error 117 if
			// none); operands are consumed regardless, entries only stored
			// when the entity owns a lootSet (initspawn keeps it alive for
			// monsters/corpses only), remaining slots zero-filled.
			Entity* ent = env_.game->findEntityBySprite(sprite);
			bool store = ent != nullptr && ent->hasLootSet;
			int entries[Entity::kMaxCorpseLoot] = { 0, 0, 0 };
			for (int i = 0; i < count; ++i) {
				uint16_t entry = readUShort(t);
				if (store && i < Entity::kMaxCorpseLoot) entries[i] = entry;
			}
			if (ent == nullptr) {
				std::fprintf(stderr, "[script] ASSIGN_LOOTSET sprite=%d entries=%d Err117 (no entity)\n",
					sprite, count);
			} else if (store) {
				for (int j = 0; j < Entity::kMaxCorpseLoot; ++j) ent->lootSet[j] = entries[j];
				std::fprintf(stderr, "[script] ASSIGN_LOOTSET sprite=%d entries=%d [%X %X %X]\n",
					sprite, count, ent->lootSet[0], ent->lootSet[1], ent->lootSet[2]);
			} else {
				std::fprintf(stderr, "[script] ASSIGN_LOOTSET sprite=%d entries=%d dropped (no lootSet)\n",
					sprite, count);
			}
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
