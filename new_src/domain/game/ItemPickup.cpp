#include "domain/game/ItemPickup.h"

#include <cstdlib>
#include <string>

#include "domain/game/Entity.h"
#include "domain/game/EntityDb.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/game/WeaponTable.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "ui/Hud.h"

namespace newcore {

// Applet::nextInt (src/App.cpp:506-508): the raw generator masked to a
// non-negative int, so `% n` keeps the legacy distribution shape.
int ItemPickup::nextInt() {
	return std::rand() & 0x7FFFFFFF;
}

std::string ItemPickup::ingameTitle(int stringId) const {
	if (env_.loc == nullptr) return {};
	return Localization::titleOf(env_.loc->get(kTextIngame, stringId));
}

// getMessageBuffer(flags) / composeText / finishMessageBuffer collapsed into
// one call: the rewrite's Hud takes the composed line plus the same flag word
// (new_src/ui/Hud.h:82-85 vs src/Hud.cpp:163-199).
void ItemPickup::message(int stringIndex, const std::string* args, int numArgs, int flags) {
	if (env_.hud == nullptr || env_.loc == nullptr) return;
	std::string text = env_.loc->get(kTextMain, stringIndex);
	if (args != nullptr && numArgs > 0) composeArgs(text, args, numArgs);
	env_.hud->addMessage(text, 0xAA000000, flags);
}

// src/Entity.cpp:118-149. Only the item branch is ported: ET_ENV_DAMAGE
// (:134-146) needs painEvent/status effects and stays unported (spec §5).
bool ItemPickup::touched(Entity* e) {
	if (e == nullptr || e->def == nullptr || env_.player == nullptr) return false;
	const int eType = e->def->eType;
	if (eType != Enums::ET_ITEM && eType != Enums::ET_MONSTERBLOCK_ITEM) return false; // :123
	if (!touchedItem(e)) return false;
	if (env_.vm != nullptr) {                                  // :125-126
		env_.vm->vars[11] = e->def->tileIndex;
		env_.vm->executeStaticFunc(Enums::SCR_ITEM_PICKUP);
	}
	// Dropped-entity tail (sprite backlink -1, def = nullptr, :127-130) not
	// ported: spawnDropItem has no counterpart in new_src yet (spec §5).
	return true;
}

// Verbatim port of Entity::touchedItem (src/Entity.cpp:152-278). Every
// isDroppedEntity() branch collapses to the world-item side (no drops yet).
bool ItemPickup::touchedItem(Entity* e) {
	const EntityDef* def = e->def;
	Player& player = *env_.player;
	const int sprite = e->getSprite();

	if (def->eSubType == Enums::ITEM_CLASS_INVENTORY) {        // :154
		int qty = 1;                                           // :155
		if (def->parm == Enums::INV_ONE_UAC_CREDIT) qty = 2 + nextInt() % 3;   // :158-161
		if (!player.give(Enums::ITEM_CLASS_INVENTORY, def->parm, qty)) {       // :162
			std::string args[1] = { ingameTitle(def->name) };
			message(83, args, 1, 2);                           // :163-169
			return false;
		}
		if (def->parm == Enums::INV_RED_KEY || def->parm == Enums::INV_BLUE_KEY) {
			message(84, nullptr, 0, 3);                        // :171-175
			// hud->repaintFlags |= 0x4 (:174) has no counterpart: the HUD
			// model is rebuilt every frame (new_src/ui/HudView.cpp).
		} else if (def->parm == Enums::INV_ONE_UAC_CREDIT) {
			std::string args[2] = { std::to_string(qty), ingameTitle(def->longName) };
			message(86, args, 2, 1);                           // :176-184
		} else {
			std::string args[1] = { ingameTitle(def->longName) };
			message(85, args, 1, 1);                           // :186-190
		}
		if (def->parm != Enums::INV_JOURNAL && env_.game != nullptr) {
			env_.game->foundLoot(sprite, 1);                   // :192-194
		}
	} else if (def->eSubType == Enums::ITEM_CLASS_FOOD) {      // :196
		const int n = (def->parm == 0 || def->parm == 2) ? 40 : 20;            // :198-203
		if (!player.addHealth(n)) {                            // :204-207
			message(46, nullptr, 0, 2);
			return false;
		}
		// No foundLoot for food — legacy has none.
	} else if (def->eSubType == Enums::ITEM_CLASS_AMMO) {      // :209
		int qty = 2 + nextInt() % 4;                           // :214-215
		if (def->parm == 2) qty &= ~1;                         // :216-218 shells: even only
		if (!player.give(Enums::ITEM_CLASS_AMMO, def->parm, qty)) {            // :220
			// Legacy asymmetry (:220-222 vs the inventory/food branches):
			// a failed ammo give prints str 87 and then FALLS THROUGH to the
			// common tail (:276-278), so a full ammo slot still consumes the
			// item. No `return false` here — reproduced deliberately.
			message(87, nullptr, 0, 0);
		} else {
			// Ammo names come from def->name (Entity::name = def->name | 0x400
			// -> text type 1, src/Entity.cpp:53 + composeTextField :231), NOT
			// longName as the inventory/weapon branches do.
			std::string args[2] = { std::to_string(qty), ingameTitle(def->name) };
			message(86, args, 2, 1);                           // :224-232
			if (env_.game != nullptr) env_.game->foundLoot(sprite, 1);         // :233-235
		}
	} else if (def->eSubType == Enums::ITEM_CLASS_WEAPON) {    // :238
		if ((1 << def->parm) & Enums::WP_SENTRY_BOT_MASK) {    // :239 weaponIsASentryBot
			if (player.hasASentryBot()) return false;          // :240-242 (isFamiliar unported)
			player.give(Enums::ITEM_CLASS_WEAPON, def->parm, 1);               // :249
			// Direct write, as legacy (:250): give() already refilled the pool
			// to 100 in its sentry pre-step (src/Player.cpp:981), and a dropped
			// bot would overwrite it with its own param. Same value here.
			player.ammo[Enums::AMMO_SENTRY_BOT] = 100;         // :244-250 (world qty = 100)
			message(223, nullptr, 0, 3);                       // :251
		} else {
			player.give(Enums::ITEM_CLASS_WEAPON, def->parm, 1);               // :254
			if (env_.tables != nullptr) {
				const WeaponDef& row = env_.tables->weaponDef(def->parm);       // :255-256
				if (row.ammoUsage != 0) {                      // :257-263
					player.give(Enums::ITEM_CLASS_AMMO, row.ammoType,
					            ((1 << def->parm) & 0x200) ? 8 : 10, true);   // quiet, as legacy (:258-262)
				}
			}
			std::string args[1] = { ingameTitle(def->longName) };
			message(85, args, 1, 1);                           // :265-269
			// showWeaponHelp (:270) unported: no help popup for gameplay grants.
		}
		if (env_.game != nullptr) env_.game->foundLoot(sprite, 1);             // :272-274
	}

	if (env_.db != nullptr) env_.db->removeEntity(e);          // :276
	// TODO sound 1054 (src/Entity.cpp:277) — no audio subsystem in new_src.
	return true;                                               // :278
}

} // namespace newcore
