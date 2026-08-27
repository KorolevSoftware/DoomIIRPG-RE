#include "domain/game/CorpseLoot.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "domain/game/Enums.h"
#include "domain/game/Player.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "io/Tables.h"

namespace newcore {

// Defined in Game.cpp (declared in Game.h): %NN argument substitution over
// Localization strings. Redeclared here so the loot lines reuse the single
// copy of the decoder without pulling the Game container into this module.
void composeArgs(std::string& text, const std::string* args, int numArgs);

// Monster subtypes of the default-lootset table (src/Enums.h:71-79; the
// constants are not ported into new_src Enums yet).
enum {
	kMonZombie = 0,
	kMonCacodemon = 6,
	kMonMancubus = 8,
	kMonRevenant = 9,
	kMonSentryBot = 11,
};

void CorpseLoot::init(const Env& env) {
	env_ = env;
}

// ---- entityDb tile-list access (copy of Game's; see CorpseLoot.h) ----

Entity* CorpseLoot::findMapEntity(int x, int y) const {
	if (x < 0 || y < 0 || x >= 32 || y >= 32) return nullptr;
	return env_.entityDb[y * 32 + x];
}

// Port of Entity::populateDefaultLootSet (src/Entity.cpp:1997-2048). The
// default (imp etc.) branch fills a per-map joke string via
// findRandomJokeItem (:2050+); that flavor table is not ported, so those
// corpses stay empty (class-6 lines would never grant anyway).
void CorpseLoot::populateDefaultLootSet(Entity& e) {
	e.hasLootSet = true;
	e.lootSet[0] = e.lootSet[1] = e.lootSet[2] = 0;
	if (e.def->eType == Enums::ET_CORPSE) {
		if (e.def->eSubType != kMonSentryBot) e.lootSet[0] = 1089; // Health Pack x1 (INV_HEALTH_PACK=17)
		return;
	}
	int s = e.getSprite();
	switch (e.def->eSubType) {
	case kMonZombie:    e.lootSet[0] = 0x600 | (s % 3 + 1); break;
	case kMonCacodemon: e.lootSet[0] = 0x2100 | (s % 5 + 3); break;
	case kMonMancubus:  e.lootSet[0] = 0x2140 | (s % 3 + 1); break;
	case kMonRevenant:  e.lootSet[0] = 0x2140 | (s % 3 + 3); break;
	case kMonSentryBot: e.lootSet[0] = 0x2040 | (s % 6 + 6); break;
	default: break;
	}
}

// See CorpseLoot.h. Adjacent-tile stand-in for the legacy one-tile trace
// distance (tileDistances[0] = 4096 = distFrom squared across one tile).
Entity* CorpseLoot::findLootableCorpseFacing(int px, int py, int stepX, int stepY) {
	int tx = (px + stepX) >> 6;
	int ty = (py + stepY) >> 6;
	for (Entity* e = findMapEntity(tx, ty); e != nullptr; e = e->nextOnTile) {
		if (!e->isCorpse()) continue;
		if (!(e->info & Entity::kInfoLinked)) continue;   // unlinked = not traceable
		// Looted gate: prop corpses count prior loots in param
		// (src/PlayingInputHandler.cpp:324-330); the monster flag 0x800 is
		// unified into param here (EntityMonster not ported).
		if (e->param != 0) continue;
		if (!e->hasLootSet) continue;                     // (:325/:331)
		return e;
	}
	return nullptr;
}

static std::string itemLongName(const EntityDefs& defs, const Localization& loc,
                                int cls, int idx) {
	const EntityDef* d = defs.find(Enums::ET_ITEM, cls, idx); // src/LootingSystem.cpp:240
	if (d == nullptr) return {};
	return Localization::titleOf(loc.get(kTextIngame, d->longName));
}

// Pooling half of legacy LootingSystem::poolLoot (src/LoothingSystem.cpp:
// 154-278); see CorpseLoot.h. Marks every eType-9 entity on the tile looted
// BEFORE reading its loot set, merges entries into `out`, then composes one
// '|'-separated display buffer + the <start,len> line table.
void CorpseLoot::poolLootCorpse(int tx, int ty, const Localization& loc, Pool& out) {
	// Reset first (:158-162): a stale buffer would corrupt the line table.
	out.numEntries = 0;
	out.numItems = 0;
	out.credits = 0;
	out.topLine = 0;
	out.text.setLength(0);

	for (Entity* e = findMapEntity(tx, ty); e != nullptr; e = e->nextOnTile) {
		if (!e->isCorpse()) continue;                    // eType == 9 only (:164)
		if (e->param != 0) continue;                     // prop already looted (:166-169)
		// Monster corpses carry a separate flag 0x800 in legacy (:172-177);
		// EntityMonster is not ported, so both markers unify into ++param
		// (spec Deviations #1).
		++e->param;
		e->info |= Entity::kInfoActivated;               // (:179)

		if (!e->hasLootSet) continue;                    // lootSet == nullptr analog
		for (int i = 0; i < Entity::kMaxCorpseLoot; ++i) {
			int entry = e->lootSet[i];
			if (entry == 0) break;                       // stop at first zero slot (:181)
			bool push = true;
			int cls = entry >> 12 & 0xF;
			if (cls == 6) {
				int n2 = entry & 0xFFF;
				for (int j = 0; j < out.numEntries; ++j) {
					// Verbatim legacy quirk: the class bit is tested on the
					// SOURCE entity's lootSet[j] where lootPool[j] was meant
					// (src/LoothingSystem.cpp:186-194; loot-inventory.md §2.4).
					// j >= kMaxCorpseLoot would read past lootSet[] (legacy
					// read adjacent memory) — treated as no match.
					if (j < Entity::kMaxCorpseLoot &&
					    ((e->lootSet[j] >> 12) & 0xF) == 6 &&
					    n2 == (out.entries[j] & 0xFFF)) {
						push = false;
						break;
					}
				}
			} else {
				int cnt = entry & 0x3F;
				++out.numItems;                          // stat counts pre-merge (:197)
				int idx = (entry & 0xFC0) >> 6;
				if (cls == 0 && idx == 24) { out.credits += cnt; continue; }      // (:200-207)
				if (cls == 0 && idx == 25) { out.credits += cnt * 100; continue; }
				int key = entry >> 6;
				for (int k = 0; k < out.numEntries; ++k) { // dupes merge, saturated (:209-216)
					if (key == (out.entries[k] >> 6)) {
						push = false;
						out.entries[k] = (out.entries[k] & 0xFFFFFFC0) |
						                 ((cnt + (out.entries[k] & 0x3F)) & 0x3F);
						break;
					}
				}
			}
			// Legacy had no bound here (its lootPool[9] is larger); entries
			// beyond kMaxCorpseLoot are dropped.
			if (push && out.numEntries < Entity::kMaxCorpseLoot) {
				out.entries[out.numEntries++] = entry;
			}
		}
	}

	// Compose lines into one buffer (:226-261). Format strings end in '|'.
	for (int l = 0; l < out.numEntries; ++l) {
		int entry = out.entries[l];
		int cls = entry >> 12 & 0xF;
		if (cls == 6) {                                  // flavor: raw map string (:229-234)
			out.text.append('\x88');
			out.text.append(loc.get(kTextMap, entry & 0xFFF));
			out.text.append("|");
			continue;
		}
		int idx = (entry & 0xFC0) >> 6;
		int cnt = entry & 0x3F;
		std::string name = itemLongName(env_.defs ? *env_.defs : EntityDefs(), loc, cls, idx);
		std::string line = loc.get(kTextMain, cls == 1 ? 91 : 90);
		if (cls == 1) {                                  // "%01%02|" (:241-243)
			std::string args[2] = { "\x88", name };
			composeArgs(line, args, 2);
		} else {                                         // "%01%02x %03|" (:245-249)
			std::string args[3] = { "\x88", std::to_string(cnt), name };
			composeArgs(line, args, 3);
		}
		out.text.append(line);
	}
	if (out.credits != 0) {                              // "<icon> N x UAC Credits" (:252-258)
		std::string line = loc.get(kTextMain, 90);
		std::string args[3] = { "\x88", std::to_string(out.credits),
		                        Localization::titleOf(loc.get(kTextIngame, 157)) };
		composeArgs(line, args, 3);
		out.text.append(line);
	}
	if (out.numEntries == 0 && out.credits == 0) {
		out.text.append(loc.get(kTextMain, 228));        // "None found!" (:259-261)
	}

	// Dehyphenate BEFORE recording offsets (legacy order :262-278), then
	// split at '|' into <start,len> pairs; the last pair covers the tail.
	out.text.dehyphenate();
	for (short& v : out.lineIndex) v = 0;
	int length = out.text.length();
	int start = 0;
	int slot = 0;
	for (int i = 0; i < length; ++i) {
		if (out.text.charAt(i) == '|') {
			if (slot < Pool::kMaxLines) {
				out.lineIndex[slot * 2] = (short)start;
				out.lineIndex[slot * 2 + 1] = (short)(i - start);
			}
			++slot;
			start = i + 1;
		}
	}
	if (slot < Pool::kMaxLines) {
		out.lineIndex[slot * 2] = (short)start;
		out.lineIndex[slot * 2 + 1] = (short)(length - start);
	}

	std::fprintf(stderr, "[loot] pooled tile=%d,%d entries=%d items=%d credits=%d\n",
		tx, ty, out.numEntries, out.numItems, out.credits);
}

// Grant half of legacy LootingSystem::giveLootPool (src/LoothingSystem.cpp:
// 281-307); see CorpseLoot.h.
void CorpseLoot::giveLootPool(Pool& pool, Player& player, const Tables* tables) {
	for (int i = 0; i < pool.numEntries; ++i) {
		int entry = pool.entries[i];
		int cls = entry >> 12 & 0xF;
		if (cls == 6) continue;                          // display-only flavor (:287)
		int idx = (entry & 0xFC0) >> 6;
		int cnt = entry & 0x3F;
		player.give(cls, idx, cnt);                      // (:289)
		std::fprintf(stderr, "[loot] give class=%d idx=%d cnt=%d\n", cls, idx, cnt);
		if (cls == 1 && tables != nullptr &&
		    (size_t)(idx * 9 + 5) < tables->weaponData.size()) {
			int ammoType = tables->weaponData[idx * 9 + 4];   // AMMOTYPE (src/Combat.h:26-36)
			int usage = tables->weaponData[idx * 9 + 5];      // AMMOUSAGE
			if (usage > 0) player.give(2, ammoType, std::max(usage, 10)); // (:290-296)
		}
	}
	if (pool.credits != 0) {
		player.give(0, 24, pool.credits);                // (:299-302)
		std::fprintf(stderr, "[loot] credits=%d\n", pool.credits);
	}
	std::fprintf(stderr, "[loot] foundLoot items=%d\n", pool.numItems); // run-stat stub (:303)
	pool.numEntries = 0;                                 // counters reset + dispose analog
	pool.numItems = 0;
	pool.credits = 0;
	pool.text.setLength(0);                              // (:304-306)
}

} // namespace newcore
