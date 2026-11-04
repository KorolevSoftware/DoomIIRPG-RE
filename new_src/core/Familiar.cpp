#include "core/Familiar.h"

#include <string>

#include "core/CinematicCamera.h"
#include "core/PlayerActions.h"
#include "core/Sound.h"
#include "domain/game/DialogSystem.h"
#include "domain/game/EntityMonster.h"
#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapData.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "text/Text.h"
#include "ui/Hud.h"

namespace newcore {

void Familiar::message(int id, int flags) {
	if (env_.hud == nullptr || env_.loc == nullptr) return;
	env_.hud->addMessage(env_.loc->get(kTextMain, id), 0xAA000000, flags);
}

// enableHelp is not ported, treated as on (as core/TargetPractice.cpp:45).
bool Familiar::showHelp(int n, bool force) {
	Player& p = *env_.player;
	if (env_.cinematic->active()) return false;
	if (!force && (p.helpBitmask & (1 << n)) != 0) return false;
	p.helpBitmask |= 1 << n;
	env_.dialogs->enqueueHelpDialog(kTextMain, n, -1);
	const StateId s = env_.host->state();
	if (n != 5 && (s == StateId::Playing || s == StateId::Automap)) env_.dialogs->dequeueHelpDialog();
	return true;
}

void Familiar::attemptToDeploy() {
	Player& p = *env_.player;
	Game& g = *env_.game;
	// Active-monster ring walk (:2390-2400): a monster within one tile that
	// could attack refuses the deploy.
	Entity* head = g.monsters.activeMonsters;
	if (head != nullptr) {
		Entity* m = head;
		do {
			Entity* next = m->monster->nextOnList;
			if (g.trace.distFrom(m, p.destX, p.destY) <= g.combat.tileDistSq(1) && g.ai.isAttackValid(m)) {
				message(219, 3);
				return;
			}
			m = next;
		} while (m != g.monsters.activeMonsters && g.monsters.activeMonsters != nullptr);
	}
	const int fx = (p.viewX + p.viewStepX) >> 6, fy = (p.viewY + p.viewStepY) >> 6;   // :2401-2404
	if ((env_.map->mapFlags[fy * 32 + fx] & 0x10) != 0) {
		message(217, 3);
		return;
	}
	int type;                                                          // :2405-2423
	switch (p.ce.weapon) {
	case 4: type = 2; break;
	case 5: type = 3; break;
	case 6: type = 4; break;
	default: type = 1; break;
	}
	setFamiliar(type);
}

// savePlayerFog is not ported (ADR 0041 §6); drawPlayingSoftKeys is per frame.
void Familiar::setFamiliar(int type) {
	if (env_.host->state() == StateId::Automap) return;
	Player& p = *env_.player;
	ScriptVM& vm = *env_.vm;
	vm.vars[16] = 1;
	p.saveX = p.viewX;
	p.saveY = p.viewY;
	p.saveZ = p.viewZ;
	p.saveAngle = p.viewAngle;
	p.savePitch = p.viewPitch;
	p.isFamiliar = true;
	p.familiarType = type;
	vm.updateScriptVars();
	if (!env_.actions->attemptMove(p.viewX + p.viewStepX, p.viewY + p.viewStepY)) {
		vm.vars[16] = 0;
		p.isFamiliar = false;
		p.familiarType = 0;
		vm.updateScriptVars();
		message(190, 3);
		return;
	}
	env_.hud->startFade(*env_.upTimeMs, 400, Hud::kFadeIn);
	Game& g = *env_.game;
	const Entity* body = g.spawnPlayerEntityCopy(p.saveX, p.saveY, p.characterChoice);
	// No body when the drop ring is disabled (EntityDb::getFreeDropEnt); B still returns.
	p.playerEntityCopyIndex = body != nullptr ? static_cast<int>(body - g.db.entities().data()) : -1;
	p.weaponsCopy = 0;
	for (int16_t& v : p.ammoCopy) v = 0;
	for (int16_t& v : p.inventoryCopy) v = 0;
	p.clearOutFamiliarsStatusEffects();
	p.swapStatusEffects();
	showHelp(type == 1 || type == 3 ? 12 : 13, false);
	Sound::instance().playSound(1108, 0, 3);
}

// stopScreenSmack and loadPlayerFog are not ported (ADR 0041 §6).
int Familiar::unsetFamiliar(bool intoBody) {
	Player& p = *env_.player;
	Game& g = *env_.game;
	env_.hud->stopBrightenScreen();
	int angle = p.saveAngle;
	if (intoBody) {                                                    // face the body (:2134-2147)
		if (p.saveX > p.viewX) angle = 512;          // ANGLE_WEST
		else if (p.saveX < p.viewX) angle = 0;       // ANGLE_EAST
		else if (p.saveY > p.viewY) angle = 256;     // ANGLE_NORTH
		else if (p.saveY < p.viewY) angle = 768;     // ANGLE_SOUTH
	}
	p.viewAngle = p.destAngle = angle;
	p.viewPitch = p.savePitch;
	if (!intoBody) {
		p.destX = p.viewX = p.saveX;
		p.destY = p.viewY = p.saveY;
		p.destZ = p.viewZ = p.saveZ;
	}
	p.swapStatusEffects();
	env_.hud->startFade(*env_.upTimeMs, 400, Hud::kFadeIn);
	if (p.playerEntityCopyIndex >= 0) g.db.removeEntity(&g.db.entities()[p.playerEntityCopyIndex]);
	p.playerEntityCopyIndex = -1;
	Entity* pe = g.db.playerEntity();        // :2165-2167 unlink, finishRotation(true), relink
	if ((pe->info & Entity::kInfoLinked) != 0) g.db.unlinkEntity(pe);
	env_.actions->finishRotationFired();
	g.relinkPlayer(p);
	p.isFamiliar = false;
	p.unsetFamiliarOnceOutOfCinematic = false;   // ADR 0041 §4: no stale monster latch
	const int old = p.familiarType;
	p.familiarType = 0;
	env_.vm->updateScriptVars();
	return old;
}

void Familiar::returnsToPlayer(bool intoBody) {
	Sound::instance().playSound(1112, 0, 3);
	unsetFamiliar(intoBody);
	env_.player->stealFamiliarsInventory();
}

void Familiar::consumeMonsterLatch(StateId s) {
	Player& p = *env_.player;
	if (!p.unsetFamiliarOnceOutOfCinematic || s == StateId::Camera || s == StateId::BotDying) return;
	p.unsetFamiliarOnceOutOfCinematic = false;
	if (!p.isFamiliar) return;
	returnsToPlayer(false);
	p.botReturnedDueToMonster = true;
}

void Familiar::onSelfDestructDialogClosed(bool yes) {
	Player& p = *env_.player;
	if (!p.attemptingToSelfDestructFamiliar) return;
	p.attemptingToSelfDestructFamiliar = false;
	// DEVIATION: legacy also self-destructs a bot that already returned while
	// the dialog was open (duplicated remains, status effects swapped back).
	if (yes && p.isFamiliar) dying(true);
}

void Familiar::dying(bool selfDestruct) {
	if (env_.host->state() == StateId::BotDying) return;
	if (selfDestruct) {
		env_.game->combat.curAttacker = nullptr;
	} else {
		Sound::instance().playSound(1109, 0, 3);
		env_.hud->startShake(*env_.upTimeMs, 350, 10);   // startShake(350, 5, 500)
	}
	selfDestructed_ = selfDestruct;
	env_.host->requestState(StateId::BotDying);
}

// clearSoftKeys and destPitch have no rewrite counterpart; smackScreen is
// not ported (ADR 0041 §6).
void Familiar::onEnterBotDying() {
	deathTime_ = *env_.upTimeMs;
	shakeStarted_ = false;
	env_.hud->brightenScreen(*env_.upTimeMs, 100, 0);
}

void Familiar::tickDying() {
	Player& p = *env_.player;
	const int64_t now = *env_.upTimeMs;
	const int t = static_cast<int>(now - deathTime_);
	if (selfDestructed_) {
		if (t >= 1500) {
			Sound::instance().playSound(1032, 0, 3);
			env_.host->requestState(StateId::Playing);
			died();
		} else if (!shakeStarted_ && t >= 750) {
			shakeStarted_ = true;
			env_.hud->startShake(now, 1500 - t, 10);     // startShake(.., 5, 500)
		}
	} else if (t < 750) {                              // the bot sinks and tilts
		const int n = ((750 - t) << 16) / 750;
		p.viewZ = env_.map->heightAt(p.destX, p.destY) + 18 + ((20 * n) >> 16);
		p.viewPitch = 96 + ((-96 * n) >> 16);
		p.viewRoll = 16 + ((-16 * n) >> 16);
		// Same viewZ keep as GameContext::tickDying: legacy updateView steps
		// viewZ by a zero zStep, Player::updateView would snap it to destZ.
		const int z = p.viewZ;
		p.updateView();
		if (p.zStep == 0) p.viewZ = z;
	} else if (t < 1500) {
		if (!env_.hud->isFading()) env_.hud->startFade(now, 750, Hud::kFadeOut);
		p.viewRoll = 16;
	} else {
		p.viewRoll = 0;   // legacy passes the roll to renderScene only (deviation D5)
		env_.host->requestState(StateId::Playing);
		died();
	}
}

void Familiar::died() {
	Player& p = *env_.player;
	Game& g = *env_.game;
	Combat& c = g.combat;
	if (c.curAttacker != nullptr) {                    // :2211-2228
		Entity* a = c.curAttacker;
		const int s = a->getSprite();
		if (a->def != nullptr && a->def->eType == Enums::ET_MONSTER && s >= 0)
			env_.map->mapSpriteInfo[s] &= 0xFFFF00FF;
		if (c.accumRoundDamage > 0 && a->def != nullptr) {
			std::string args[2] = { Localization::titleOf(env_.loc->get(kTextIngame, a->def->name & 0x3FF)),
			                        std::to_string(c.accumRoundDamage) };
			c.centerMessage(112, args, 2);
		}
		env_.hud->stopShake();
		env_.hud->setDamageDir(0);
		env_.hud->setDamageTime(0);
		if (a->monster != nullptr) a->monster->flags |= Enums::MFLAG_ATTACKING;
		g.sprites.clear(GameSprites::kFlagAnim);
	}
	const int bx = p.viewX, by = p.viewY;
	const int type = unsetFamiliar(false);
	message(192, 2);
	if (type == 2 || type == 4) explode(bx >> 6, by >> 6, type);
	const int w = type == 2 ? 4 : type == 3 ? 5 : type == 4 ? 6 : 3;
	if (p.noFamiliarRemains) p.noFamiliarRemains = false;
	else handleBotRemains(bx, by, w);
	p.give(Enums::ITEM_CLASS_WEAPON, w, -1, true);
}

void Familiar::explode(int tx, int ty, int type) {
	Combat& c = env_.game->combat;
	c.attackerWeaponId = type == 2 ? 4 : 6;
	const WeaponDef& row = c.weaponDef(c.attackerWeaponId);
	int dmg = row.strMin & 0xFF;
	const int max = row.strMax & 0xFF;
	if (dmg != max) dmg += static_cast<int>(c.nextByte()) % (max - dmg);
	c.radiusHurtEntities(tx, ty, 0, dmg, env_.game->db.playerEntity(), nullptr);
}

// The copies are the bot's bag: kept items go straight to the player, some
// are dropped (thrown) at the bot spot, medkits and water go into the corpse.
void Familiar::handleBotRemains(int x, int y, int w) {
	Player& p = *env_.player;
	Game& g = *env_.game;
	const EntityDefs& defs = *env_.defs;
	const int bag = p.weaponsCopy;
	int medkits = 0, water = 0;
	for (int i = 0; i < 26; ++i) {
		const int n = p.inventoryCopy[i];
		if (n == 0) continue;
		if (i == 24 || (i >= 0 && i < 11)) {
			p.give(Enums::ITEM_CLASS_INVENTORY, i, n, true, true);
			const EntityDef* d = defs.find(Enums::ET_ITEM, Enums::ITEM_CLASS_INVENTORY, i);
			if (d != nullptr) {
				std::string text = env_.loc->get(kTextMain, 86);
				std::string args[2] = { std::to_string(n), Localization::titleOf(env_.loc->get(kTextIngame, d->longName)) };
				composeArgs(text, args, 2);
				env_.hud->addMessage(text, 0xAA000000, 0);
			}
		} else if (i == 13) {
			water += n;
		} else if (i == 16) {
			medkits += n;
		} else if (i == 11 || i == 12 || i == 17 || i == 19 || i == 20) {
			if (const EntityDef* d = defs.find(Enums::ET_ITEM, Enums::ITEM_CLASS_INVENTORY, i))
				g.spawnDropItem(x, y, d->tileIndex, d, n, true);
		}
	}
	for (int j = 1; j < 9; ++j) {
		if (p.ammoCopy[j] == 0 || !(j == 1 || j == 2 || j == 4 || j == 5)) continue;
		if (const EntityDef* d = defs.find(Enums::ET_ITEM, Enums::ITEM_CLASS_AMMO, j))
			g.spawnDropItem(x, y, d->tileIndex, d, p.ammoCopy[j], true);
	}
	for (int k = 0; k < 15; ++k) {
		if (((1 << k) & bag) == 0) continue;
		const int tile = Combat::getWeaponTileNum(k);
		if (const EntityDef* d = defs.lookup(tile)) g.spawnDropItem(x, y, tile, d, 1, true);
	}
	g.spawnSentryBotCorpse(x, y, w, medkits, water);
}

void Familiar::startSelfDestructDialog() {
	if (env_.host->state() == StateId::Automap) return;
	env_.player->attemptingToSelfDestructFamiliar = true;
	Text t;
	t.append(env_.loc->get(kTextMain, 194));
	env_.dialogs->startDialogText(nullptr, t, 12, 1, false);
}

} // namespace newcore
