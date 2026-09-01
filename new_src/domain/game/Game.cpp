#include "domain/game/Game.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "domain/game/Enums.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapBits.h"
#include "io/Localization.h"

namespace newcore {

// Monster subtypes of the spawn-scale table (src/Enums.h:71-79; the
// constants are not ported into new_src Enums yet). The default-lootset
// subtypes moved with populateDefaultLootSet (spec §P2-GE).
enum {
	kMonPinky = 5,
	kBossMastermind = 14,
};

// ---- Level load ----

void Game::loadEntities(MapData& map, const EntityDefs& defs) {
	map_ = &map;
	defs_ = &defs;
	db.init({ &map });                             // peer subsystem wiring (spec §P2-GF)
	trace.init({ &db, &map });                     // peer subsystem wiring (spec §P2-GA)
	doors.init({ &db, &map, vm_, &trace });        // peer subsystem wiring (spec §P2-GB)
	monsters.init({ &db, &map, &defs, vm_, &combat, &trace, &monstersTurn,
	                &facingDirty, &lerps });                            // spec §P2-GC
	lerps.init({ &db, &map, vm_ });                     // peer subsystem wiring (spec §P2-GD)
	loot.init({ &db, &defs });                          // peer subsystem wiring (spec §P2-GE)
	db.resetEntities();
	monstersTurn = 0;
	lootFound = 0;                                 // src/Game.cpp:681 (unloadMap)
	lootSource = -1;
	showingLoot = false;
	queueAdvanceTurn = false;

	// Load-time AUTO_ANIMATE injection (src/Game.cpp:374-397): raw maps carry
	// no animation bits — the five tiles below get 0x80000 plus the frame count
	// in bits 8-15. Counts are the mediaMappings ranges, verified against
	// tmp_newMappings.bin: 130 -> 726..730 = 4, 136 -> 736..737 = 1; ANIM_FIRE
	// 234 is forced to 4 (:380-382), EYE_PORTAL 156 to 2 (:383-387) and
	// AIR_VENT 236 to 3 (:388-392). The 0x200 / 0x300 in the legacy constants
	// 0x80200 / 0x80300 are just the same n7 << 8 ORed twice, so the count is
	// written once here. Legacy also sets S_RENDERMODE = 3 for 236 (:392) —
	// the postProcessSprites port below writes the same value for that tile.
	// Invariant (src/Render.cpp:1545): 0x80000 with a zero count divides by
	// zero, so every branch here must produce frameCount >= 1.
	for (int i = 0; i < map.numSprites; ++i) {
		const int info = map.mapSpriteInfo[i];
		int tileNum = info & SpriteInfo::kTileNumMask;
		if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
		int frameCount;
		if (tileNum == Enums::TILENUM_OBJ_FIRE ||
		    tileNum == Enums::TILENUM_ANIM_FIRE)      frameCount = 4;
		else if (tileNum == Enums::TILENUM_AIR_VENT)  frameCount = 3;
		else if (tileNum == Enums::TILENUM_EYE_PORTAL) frameCount = 2;
		else if (tileNum == Enums::TILENUM_OBJ_TORCHIERE) frameCount = 1;
		else continue;
		map.mapSpriteInfo[i] = (info & 0xFFFF00FF) | (frameCount << 8) |
		                       Enums::SPRITE_FLAG_AUTO_ANIMATE;
	}

	// Per-sprite render mode (src/Render.cpp:2459-2495 postProcessSprites):
	// effective tileNum (+257 for wall-tile sprites) selects the blend mode,
	// written to S_RENDERMODE; everything else stays RENDER_NORMAL.
	for (int i = 0; i < map.numSprites; ++i) {
		const int info = map.mapSpriteInfo[i];
		int tileNum = info & SpriteInfo::kTileNumMask;
		if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
		int mode = 0;
		if (tileNum == 208 || tileNum == 234 || tileNum == 130 ||
		    tileNum == 242 || tileNum == 178 || tileNum == 236) mode = 3; // RENDER_ADD
		else if (tileNum == 212) mode = 7;                                // RENDER_SUB
		else if (tileNum == 161) mode = 2;                                // BLEND50
		else if (tileNum == 244) mode = 4;                                // ADD75
		map.mapSprites[i + 3 * map.numSprites] = (int16_t)mode;
	}

	// Full legacy spawn rule (ADR 0015, src/Game.cpp:373-486): every sprite
	// whose lookup(tileNum) returns a def gets an entity, plus a fallback for
	// def-less sprites flagged SOLIDSIDE. Hidden sprites do get an entity —
	// they are only left unlinked (src/Game.cpp:451-454).
	const EntityDef* const spriteWallDef = defs.find(Enums::ET_SPRITEWALL, 0);          // :371
	const EntityDef* const spriteWallNoclipDef = defs.find(Enums::ET_NONOBSTRUCTING_SPRITEWALL, 0);
	numDestroyableObj = 0;                        // src/Game.cpp:448-450
	destroyedObj = 0;                             // src/Game.cpp:679-680 (unloadMap)
	int census[Enums::ET_MAX] = { 0 };
	int numLinked = 0;
	int numDeflessWalls = 0;
	int nextSlot = 2; // entities[0]=world, entities[1]=player (reserved)
	for (int i = 0; i < map.numSprites; ++i) {
		int info = map.mapSpriteInfo[i];
		// No-entity sprites: legacy clears the bit and skips (src/Game.cpp:397-400).
		if (info & Enums::SPRITE_FLAG_NOENTITY) {
			map.mapSpriteInfo[i] &= ~Enums::SPRITE_FLAG_NOENTITY;
			continue;
		}
		int tileNum = info & SpriteInfo::kTileNumMask;
		if (info & Enums::SPRITE_FLAG_TILE) tileNum += 257;
		// Link coordinates are read before the nudge (src/Game.cpp:401-403).
		int x = map.mapSprites[i + 0 * map.numSprites];
		int y = map.mapSprites[i + 1 * map.numSprites];
		// World-weapon pickup pose (src/Game.cpp:404-406): frame 2 in bits 8-15,
		// for every sprite in the tile range regardless of the def family.
		if (tileNum >= Enums::TILENUM_WEAPON_FRAME_FORCE_MIN &&
		    tileNum <= Enums::TILENUM_WEAPON_FRAME_FORCE_MAX) {
			map.mapSpriteInfo[i] |= 0x200;
			info = map.mapSpriteInfo[i];
		}
		// Link-tile nudge for oriented sprites sitting on a tile border
		// (src/Game.cpp:407-418). x/y stay local — never written back.
		if ((info & SpriteInfo::ORIENTED) && (((x & 0x3F) == 0) || ((y & 0x3F) == 0))) {
			if      (info & Enums::SPRITE_FLAG_EAST)  ++x;
			else if (info & Enums::SPRITE_FLAG_SOUTH) ++y;
			else if (info & Enums::SPRITE_FLAG_NORTH) --y;
			else if (info & Enums::SPRITE_FLAG_WEST)  --x;
		}
		const EntityDef* def = (tileNum >= 0 && tileNum < 512) ? defs.lookup(tileNum) : nullptr;
		if (def == nullptr) {
			// Def-less solid sprite: borrow a sprite-wall def (src/Game.cpp:457-481).
			if ((info & Enums::SPRITE_FLAG_SOLIDSIDE) == 0) continue;
			const EntityDef* wallDef =
				(tileNum == Enums::TILENUM_SPRITEWALL_NOCLIP_A ||
				 tileNum == Enums::TILENUM_SPRITEWALL_NOCLIP_B)
					? spriteWallNoclipDef : spriteWallDef;
			if (wallDef == nullptr) {
				std::fprintf(stderr, "[spawn] sprite=%d tile=%d: no sprite-wall def\n", i, tileNum);
				continue;
			}
			if (nextSlot >= EntityDb::kEntities) {
				std::fprintf(stderr, "[spawn] ERR_MAX_ENTITIES(35): %d slots exhausted at sprite %d\n",
					EntityDb::kEntities, i);
				break;
			}
			Entity& w = db.entities()[nextSlot++];
			w.def = wallDef;                      // no initspawn, no loot set, no active bit
			w.setSprite(i);
			++numDeflessWalls;
			if (wallDef->eType < Enums::ET_MAX) ++census[wallDef->eType];
			if ((map.mapSpriteInfo[i] & Enums::SPRITE_FLAG_HIDDEN) == 0) {
				db.linkEntity(&w, x >> 6, y >> 6);
				++numLinked;
			}
			continue;
		}
		if (def->eType == Enums::ET_DOOR) {
			// Rewrite-only range guard, harmless on shipped data: the only
			// eType==5 defs in entities.bin are tileIndex 271-278.
			if (tileNum < Enums::TILENUM_FIRST_DOOR || tileNum > Enums::TILENUM_LAST_DOOR) continue;
		}
		if (nextSlot >= EntityDb::kEntities) {
			std::fprintf(stderr, "[spawn] ERR_MAX_ENTITIES(35): %d slots exhausted at sprite %d\n",
				EntityDb::kEntities, i);
			break;
		}

		Entity& e = db.entities()[nextSlot++];
		e.def = def;
		e.setSprite(i);
		if (def->eType == Enums::ET_DOOR) {
			e.info |= Entity::kInfoActive;
		} else if (def->eType == Enums::ET_ITEM) {
			// Nothing per-entity: legacy initspawn() has no ET_ITEM case, so no
			// active/dirty marker, no loot set and param stays 0
			// (src/Entity.cpp:49-112). Only the shared tail (linkEntity) applies.
		} else if (def->eType == Enums::ET_MONSTER) {
			// Monster half (src/Game.cpp:430-447 + src/Entity.cpp:59-81):
			// payload from the fixed pool, random art flip, shared-stat clone
			// with the difficulty hp bump, z/scale snap, active marker.
			e.monster = monsters.allocMonster();             // :435 (pool + Error 37)
			if (e.monster == nullptr) {
				--nextSlot;   // give the slot back and skip this sprite
				continue;
			}
			e.monster->reset();                              // :436
			if ((std::rand() & 1) == 0 && !MonsterSystem::isBossDef(def)) { // :438-441 (nextByte analog)
				map.mapSpriteInfo[i] |= Enums::SPRITE_FLAG_FLIP_HORIZONTAL;
			}
			const int tmplIdx = def->eSubType * 3 + (int8_t)def->parm;  // src/Entity.cpp:60
			if (tmplIdx >= 0 && tmplIdx < (int)combat.monsterTemplates.size()) {
				// NOTE direction: rewrite clone(&other) copies FROM the arg
				// into this entity (legacy template.clone(dest) is reversed).
				e.monster->ce.clone(combat.monsterTemplates[tmplIdx]);
			} else {
				std::fprintf(stderr, "[monster] template %d missing (sub=%d parm=%d)\n",
					tmplIdx, def->eSubType, def->parm);
			}
			const int diff = difficulty();                   // :62-67 (+25% hp)
			if (diff == 4 || (diff == 2 && !MonsterSystem::isBossDef(def))) {
				const int stat = e.monster->ce.getStat(1);
				const int n2 = stat + (stat >> 2);
				e.monster->ce.setStat(1, n2);
				e.monster->ce.setStat(0, n2);
			}
			// z snap: stored S_Z is raw-relative in this rewrite (renderer
			// re-adds terrain), so write the bare +32 offset — legacy bakes
			// getHeight+32 (src/Entity.cpp:70, corpsify note Game.cpp:637-640).
			map.mapSprites[i + 2 * map.numSprites] = 32;
			int scale = 64;                                  // :68-75
			if ((def->eSubType == kBossMastermind || def->eSubType == kMonPinky) &&
			    def->parm == 0) scale = 42;
			map.mapSprites[i + 8 * map.numSprites] = (int16_t)scale;
			e.info |= Entity::kInfoActive;                   // :77 (0x20000)
			CorpseLoot::populateDefaultLootSet(e);           // :109-111
		} else if (def->eType == Enums::ET_DECOR && def->eSubType != Enums::DECOR_STATUE) {
			// src/Entity.cpp:81-86: decor flagged hidden in the map data is
			// unhidden (and therefore linked below); the wall switch shrinks.
			map.mapSpriteInfo[i] &= ~Enums::SPRITE_FLAG_HIDDEN;
			if ((map.mapSpriteInfo[i] & SpriteInfo::kTileNumMask) == Enums::TILENUM_SWITCH) {
				map.mapSprites[i + 8 * map.numSprites] = 32;   // S_SCALEFACTOR
			}
		} else if (def->eType == Enums::ET_ATTACK_INTERACTIVE) {
			e.info |= Entity::kInfoActive;                     // src/Entity.cpp:87-89 (0x20000)
		} else if (def->eType == Enums::ET_CORPSE) {
			// Placed corpse props spawn with info |= 0x420000
			// (src/Entity.cpp:96-98); generalized to every corpse.
			e.info |= Entity::kInfoActive | Entity::kInfoDirty;
			CorpseLoot::populateDefaultLootSet(e);             // src/Entity.cpp:103-111
		} else if (def->eType == Enums::ET_NPC) {
			// ET_NPC construction sets param = 1 -> chat-icon overhead in
			// legacy (src/Entity.cpp:99-101); the icon itself is not rendered
			// this cycle (spec 2026-08-25-character-animation §1 elision).
			// NPCs carry no loot set (src/Entity.cpp:103-108).
			e.param = 1;
		}
		// Everything else (ET_ITEM, ET_ENV_DAMAGE, ET_SPRITEWALL,
		// ET_NONOBSTRUCTING_SPRITEWALL, ET_DECOR_NOCLIP, statues) has no
		// initspawn branch at all (src/Entity.cpp:50-111).

		// Destroyable-object stat (src/Game.cpp:448-450): eType 10 minus the
		// crate (2) and subtype 3.
		if (def->eType == Enums::ET_ATTACK_INTERACTIVE &&
		    def->eSubType != Enums::INTERACT_CRATE && def->eSubType != 3) {
			++numDestroyableObj;
		}
		if (def->eType < Enums::ET_MAX) ++census[def->eType];
		// Link gate (src/Game.cpp:451-454): re-read the flags — the decor
		// branch above may have just cleared the hidden bit.
		const bool linked = (map.mapSpriteInfo[i] & Enums::SPRITE_FLAG_HIDDEN) == 0;
		if (linked) {
			db.linkEntity(&e, x >> 6, y >> 6);
			++numLinked;
		}
		// Post-spawn self-hide (src/Game.cpp:455-457).
		if (tileNum >= Enums::TILENUM_HIDE_AFTER_SPAWN_MIN &&
		    tileNum <= Enums::TILENUM_HIDE_AFTER_SPAWN_MAX) {
			map.mapSpriteInfo[i] |= Enums::SPRITE_FLAG_HIDDEN;
		}
		if (def->eType == Enums::ET_MONSTER) {
			// :441 - legacy sets 0x40000 so spawn deactivate() links the monster onto the inactive ring (src/Game.cpp:441-443)
			e.info |= Entity::kInfoOnActiveList;
			monsters.deactivate(&e); // every monster starts on the inactive ring
			std::fprintf(stderr,
				"[monster] spawn sprite=%d sub=%d parm=%d hp=%d/%d (%d,%d)%s\n",
				i, def->eSubType, def->parm,
				e.monster ? e.monster->ce.getStat(0) : -1,
				e.monster ? e.monster->ce.getStat(1) : -1,
				x >> 6, y >> 6, linked ? "" : " unlinked");
		}
	}
	std::fprintf(stderr,
		"[spawn] %d entities (%d/%d slots), %d linked, %d destroyable; "
		"monster=%d npc=%d door=%d item=%d decor=%d envdmg=%d corpse=%d "
		"interact=%d spritewall=%d noclipwall=%d decornoclip=%d (def-less walls %d)\n",
		nextSlot - 2, nextSlot, EntityDb::kEntities, numLinked, numDestroyableObj,
		census[Enums::ET_MONSTER], census[Enums::ET_NPC], census[Enums::ET_DOOR],
		census[Enums::ET_ITEM], census[Enums::ET_DECOR], census[Enums::ET_ENV_DAMAGE],
		census[Enums::ET_CORPSE], census[Enums::ET_ATTACK_INTERACTIVE],
		census[Enums::ET_SPRITEWALL], census[Enums::ET_NONOBSTRUCTING_SPRITEWALL],
		census[Enums::ET_DECOR_NOCLIP], numDeflessWalls);
}

// ---- Phase 5: script-facing services ----

// Faithful port (src/Game.cpp:2477-2498): flip bit0 of the sprite-info
// tileNum byte (271<->272, 273<->274, ...) and re-look-up the def by
// tileNum+257. The renderer resolves textures from the same low byte, so
// the locked/unlocked texture swap is automatic.
void Game::setLineLocked(Entity* e, bool locked) {
	if (!e || !map_ || !defs_) return;
	int sprite = e->getSprite();
	if (sprite < 0 || sprite >= map_->numSprites) return;
	int info = map_->mapSpriteInfo[sprite];
	int tn = info & SpriteInfo::kTileNumMask;
	tn = locked ? (tn & 0xFFFFFFFE) : (tn | 0x1);
	map_->mapSpriteInfo[sprite] = (info & 0xFFFFFF00) | tn;
	e->def = defs_->lookup(tn + 257);
	std::fprintf(stderr, "[script] setLineLocked sprite=%d -> %s\n", sprite, locked ? "locked" : "unlocked");
}

// Turn advance (src/Game.cpp:1238-1281, subset). player->advanceTurn stats /
// updateBombs / updateMonsterFX / startRotation pitch refresh are
// placeholders until those systems exist.
void Game::advanceTurn() {
	queueAdvanceTurn = false;                  // (:1240)
	if (monsters.interpolatingMonsters) {      // (:1241-1243) Error 95 guard:
		// nothing sets interpolatingMonsters in Stage 1 (no lerps), so this
		// is defensive only.
		std::fprintf(stderr, "[turn] ERR_NONSNAPPEDMONSTERS (95)\n");
		monsters.snapMonsters(true);
	}
	// haste-parity block (:1244-1262): statusEffects[2] absent ->
	// monstersTurn = 1 always. Player-side ticks (poison/infection/combat
	// decay) deferred with citation src/Player.cpp:51-92.
	monstersTurn = 1;                          // arm the monster phase (:1257-1264); Playing tick step disarms
	facingDirty = true;                        // updateFacingEntity latch (src/Game.cpp:1269; no haste -> b always true)
	doors.advanceTurnDoors();                  // auto-close sweep (:1271-1278)
	if (vm_) vm_->executeStaticFunc(Enums::SCR_PER_TURN); // PER_TURN hook (:1279)
}

// Crate opening (src/PlayingInputHandler.cpp:387-393). The unlink is
// immediate — the crate stops blocking the player and stops being a
// trace/facing target while the 4-frame animation is still running.
// Clock choice (ADR 0016): the SpriteLerps clock is the rewrite's simulation
// clock for sprite animation, so arming and advancing share it.
void Game::openCrate(Entity* e) {
	if (e == nullptr || e->def == nullptr) return;
	e->param = lerps.clockMs() + 200;          // legacy entity->param = upTimeMs + 200 (:389)
	db.unlinkEntity(e);                        // (:390)
	facingDirty = true;                        // deviation: drop the crate name from the HUD this turn
	std::fprintf(stderr, "[use] crate opened sprite=%d\n", e->getSprite());
}

// ---- Damage / death dispatch (ADR 0018) ----

// Entity::pain (src/Entity.cpp:280-393) reduced to its dispatch: the
// (info & 0x20000) pre-guard (:285-287) plus the eType switch. The legacy
// return value is the boss staticFunc result, false everywhere else.
bool Game::entityPain(Entity* e, int damage) {
	if (e == nullptr || e->def == nullptr) return false;
	if ((e->info & Entity::kInfoActive) == 0) return false;   // :285-287
	switch (e->def->eType) {
	case Enums::ET_MONSTER:                                   // :288-369
		return monsters.painMonster(e, damage, combat.attackerWeaponId);
	case Enums::ET_ATTACK_INTERACTIVE:                        // :371-391
		return painProp(e);
	default:
		return false;                                         // legacy has no other arm
	}
}

// Entity::died (src/Entity.cpp:424-521) reduced to its dispatch: the pre-guard
// of :431-433 plus the eType switch.
//
// Legacy clears `info &= ~0x20000` once, before the switch (:434), for every
// eType. Here the clear is repeated in EVERY arm instead of being hoisted,
// because MonsterSystem::diedMonster re-checks that bit and early-returns
// (MonsterSystem.cpp:196) — clearing it up front would silently kill the
// monster death path. So the arms that have no body (default, and the ET_CORPSE
// stub of group G4) must still do the clear, or their entity would stay
// damageable forever (calcHitEntity's active gate, Combat.cpp:367-369).
//
// The monster->flags & 0x4 half of :431 is not ported (diedMonster never had
// it — adding it would change monster deaths).
void Game::entityDied(Entity* e, bool giveXP) {
	if (e == nullptr || e->def == nullptr) return;
	if ((e->info & Entity::kInfoActive) == 0) return;         // :431-433
	switch (e->def->eType) {
	case Enums::ET_MONSTER:                                   // :459-521
		monsters.diedMonster(e, giveXP);                      // clears kInfoActive itself (:434)
		break;
	case Enums::ET_ATTACK_INTERACTIVE:                        // :437-447
		diedProp(e);                                          // clears kInfoActive itself (:434)
		break;
	case Enums::ET_CORPSE:                                    // :448-458 (group G4)
		e->info &= ~Entity::kInfoActive;                      // :434
		std::fprintf(stderr, "[combat] corpse died arm deferred sprite=%d\n", e->getSprite());
		break;
	default:
		e->info &= ~Entity::kInfoActive;                      // :434, legacy has no other arm
		break;
	}
}

// ET_ATTACK_INTERACTIVE arm of Entity::pain (src/Entity.cpp:371-391). Props
// have no health: the hit destroys them here, and Combat's stage-1 tail then
// calls entityDied for the message/XP. Particles (:375,:381,:384) and sound
// 1038 (:376) have no subsystem yet.
bool Game::painProp(Entity* e) {
	const int sprite = e->getSprite();
	if (map_ == nullptr || sprite < 0 || sprite >= map_->numSprites) return false;
	const int sub = e->def->eSubType;
	if (sub == Enums::INTERACT_BARRICADE) {                   // :373-378
		// Broken frame 1 in the frame byte; the entity survives but stops
		// blocking, so the player can walk through the wreck.
		map_->mapSpriteInfo[sprite] = (map_->mapSpriteInfo[sprite] & 0xFFFF00FF) | 0x100;
		db.unlinkEntity(e);                                   // :377
		std::fprintf(stderr, "[combat] barricade broken sprite=%d\n", sprite);
	} else {
		if (sub == Enums::INTERACT_PICKUP) {                  // :383-386
			// Particle burst type 1 / color -1 (white upward jet, :384) has no
			// subsystem yet.
			turnEntityIntoWaterSpout(e);                      // :385
			// Legacy takes updateFacingEntity from died (:527); the rewrite's
			// entityDied default arm (which this entity now reaches, eType 14)
			// has no such latch, so the HUD name swap is armed here.
			facingDirty = true;
			return false;                                     // :386 early return: no removeEntity, no hide bit
		}
		db.removeEntity(e);                                   // :388; also sets 0x10000 (:390)
		e->info |= Entity::kInfoDirty;                        // :389
		std::fprintf(stderr, "[combat] prop destroyed sprite=%d sub=%d\n", sprite, sub);
	}
	facingDirty = true;                // rewrite latch: the target is gone from the HUD
	return false;
}

// ET_ATTACK_INTERACTIVE arm of Entity::died (src/Entity.cpp:437-447): name the
// prop in message 89, 5 XP, and drop it from the map-completion counter.
void Game::diedProp(Entity* e) {
	e->info &= ~Entity::kInfoActive;                          // :434
	const Localization* loc = combat.env().loc;
	if (loc != nullptr) {
		// Legacy composes Entity::name = def->name | 0x400, i.e. the ingame
		// text of def->name (PlayerActions.cpp:168-174 uses the same rule).
		std::string args[1] = { Localization::titleOf(loc->get(kTextIngame, e->def->name)) };
		combat.centerMessage(89, args, 1);                    // :438-442
	}
	Player* player = combat.env().player;
	if (player != nullptr) player->addXP(5);                  // :444
	if (e->def->eSubType != Enums::INTERACT_PICKUP &&
	    e->def->eSubType != Enums::INTERACT_CRATE) {
		++destroyedObj;                                       // destroyedObject(sprite) (:445-446)
	}
	std::fprintf(stderr, "[combat] prop died sprite=%d destroyed=%d/%d\n",
		e->getSprite(), destroyedObj, numDestroyableObj);
}

// src/ArmorRepairSystem.cpp:56-63 (nothing armor-related happens there; the
// class is just where the original parked it). Exactly the legacy four
// statements, minus the entity `name` copy: the rewrite's Entity has no name
// field, every reader goes through def->name, and the new def already carries
// the spout's name 133 (docs/original-code/combat.md §11.1).
//
// What is deliberately absent: no unlink/relink (the entity keeps its tile
// link and sprite slot), no hide bit 0x10000, no Z/scale change. Blocking ends
// on its own because the new eType 14 ET_DECOR_NOCLIP is in none of the solid
// masks. Note the sprite-info mask is 0xFFFFFF00, not the frame mask
// 0xFFFF00FF used above: the tile number is the low byte and frame bits 8-15
// survive.
void Game::turnEntityIntoWaterSpout(Entity* e) {
	if (e == nullptr || map_ == nullptr || defs_ == nullptr) return;
	const int sprite = e->getSprite();
	if (sprite < 0 || sprite >= map_->numSprites) return;
	const EntityDef* def = defs_->lookup(Enums::TILENUM_WATER_SPOUT);
	if (def == nullptr) return;
	e->def = def;                                             // :59
	map_->mapSpriteInfo[sprite] =
		(map_->mapSpriteInfo[sprite] & 0xFFFFFF00) | Enums::TILENUM_WATER_SPOUT; // :61
	e->info |= Entity::kInfoDirty;                             // :62 (0x400000)
	std::fprintf(stderr, "[combat] water spout sprite=%d\n", sprite);
}

// ---- Monsters / combat (spec 2026-08-26-combat-stage1 §0.B, §3.2) ----

int Game::difficulty() const {
	return vm_ != nullptr ? vm_->vars[12] : 2;
}

// %NN arg substitution, decode rules of Localization composeText
// (src/Text.cpp:281-326; DialogSystem.cpp:85-115 in the rewrite). Declared
// in Game.h — Combat.cpp reuses it for the combat message feed.
void composeArgs(std::string& text, const std::string* args, int numArgs) {
	std::string out;
	for (size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c == '%' && i + 2 < text.size() &&
		    text[i + 1] >= '0' && text[i + 1] <= '9' &&
		    text[i + 2] >= '0' && text[i + 2] <= '9') {
			int a = (text[i + 1] - '0') * 10 + (text[i + 2] - '0') - 1; // first arg is %01
			i += 2;
			if (a >= 0 && a < numArgs) out += args[a];
		} else {
			out += c;
		}
	}
	text.swap(out);
}

// Port of Game::touchTile (src/Game.cpp:687-699). x/y arrive in canvas units
// (src/MovementController.cpp:170); legacy findMapEntity shifts them itself
// (src/Game.cpp:729-736) while the rewrite's takes tile coords, so the shift
// happens here. The bool return (b2) has no reader in the rewrite.
void Game::touchTile(int x, int y, bool b) {
	EntityDb::TileWalk walk("Game::touchTile");
	Entity* next = nullptr;
	for (Entity* e = db.findMapEntity(x >> 6, y >> 6); e != nullptr && walk.ok(e); e = next) {
		next = e->nextOnTile;                  // :692 cached BEFORE touched() unlinks
		// Legacy gate is `b || eType == ET_ENV_DAMAGE` (:693); ET_ENV_DAMAGE
		// is unported (no pain/status-effect system, spec §5) and touched()
		// ignores that type, so the b == false pass is a no-op either way.
		if (!b) continue;
		items.touched(e);                      // :694
	}
}

// src/Game.cpp:974-981.
void Game::eventFlagsForMovement(int x0, int y0, int x1, int y1) {
	int dx = x1 - x0;
	int dy = y1 - y0;
	eventFlags_[0] = 2 | eventFlagForDirection(dx, dy);   // LEAVE mask for source tile
	eventFlags_[1] = 1 | eventFlagForDirection(-dx, -dy); // ENTER mask for destination tile
}

// src/Game.cpp:983-1014 (8-way table; screen Y grows downward).
int Game::eventFlagForDirection(int dx, int dy) {
	if (dx > 0) {
		if (dy < 0) return Enums::EVFL_MOD_NORTHEAST;
		if (dy > 0) return Enums::EVFL_MOD_SOUTHEAST;
		return Enums::EVFL_MOD_EAST;
	}
	if (dx < 0) {
		if (dy < 0) return Enums::EVFL_MOD_NORTHWEST;
		if (dy > 0) return Enums::EVFL_MOD_SOUTHWEST;
		return Enums::EVFL_MOD_WEST;
	}
	return (dy > 0) ? Enums::EVFL_MOD_SOUTH : Enums::EVFL_MOD_NORTH;
}

void Game::update(int dtMs) {
	lerps.update(dtMs);
	doors.update(dtMs);

	// Pain/dodge pose auto-revert (legacy render-side src/Render.cpp:1600-1604,
	// moved into the simulation per spec deviation D-6): anim bytes 96/144
	// fall back to IDLE once the monster's frameTime hold expired. Same
	// guards as the walk writer: hidden sprites skipped, knockback-flagged
	// monsters keep their pose (:1600).
	if (map_ == nullptr) return;
	for (Entity& ent : db.entities()) {
		if (ent.monster == nullptr || ent.def == nullptr) continue;
		const int s = ent.getSprite();
		if (s < 0 || s >= map_->numSprites) continue;
		const int info = map_->mapSpriteInfo[s];
		if ((info & Enums::SPRITE_FLAG_HIDDEN) != 0) continue;
		const int anim = (info >> 8) & Enums::MANIM_MASK;
		if ((anim == Enums::MANIM_PAIN || anim == Enums::MANIM_DODGE) &&
		    (ent.monster->flags & Enums::MFLAG_KNOCKBACK) == 0 &&
		    lerps.clockMs() > ent.monster->frameTime) {
			map_->mapSpriteInfo[s] = info & 0xFFFF00FF;   // back to IDLE (:1601-1602)
			ent.monster->frameTime = 0;
		}
	}

	// Crate opening animation, ported out of the renderer
	// (src/Render.cpp:1607-1617) with the same deviation precedent as the
	// pain-pose revert above: legacy steps the frame only while the crate is
	// drawn, we step it in the simulation for every armed crate. Both reach
	// frame 3 in ~600 ms and latch there — the sprite is never hidden.
	const int now = lerps.clockMs();
	for (Entity& ent : db.entities()) {
		if (ent.def == nullptr || ent.param == 0) continue;
		if (ent.def->eType != Enums::ET_ATTACK_INTERACTIVE || ent.def->eSubType != 2) continue;
		const int s = ent.getSprite();
		if (s < 0 || s >= map_->numSprites) continue;
		const int info = map_->mapSpriteInfo[s];
		int frame = (info >> 8) & 0xFF;                   // src/Render.cpp:1508
		if (now > ent.param) {                            // (:1608-1611)
			++frame;
			ent.param = now + 200;
		}
		if (frame > 3) {                                  // (:1612-1615)
			ent.param = 0;
			frame = 3;
		}
		map_->mapSpriteInfo[s] = (info & 0xFFFF00FF) | (frame << 8);  // (:1616)
	}
}

} // namespace newcore