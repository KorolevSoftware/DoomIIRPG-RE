#ifndef NEW_DOMAIN_GAME_PLAYER_H
#define NEW_DOMAIN_GAME_PLAYER_H

#include <cstdint>

#include "domain/game/CombatEntity.h"
#include "domain/game/Enums.h"
#include "io/EntityDefs.h"

namespace newcore {

class Combat;
class Entity;
class Hud;
class Localization;

// useItem outcome. Used = the legacy `b == true` tail: the caller must call
// Game::advanceTurn() (src/Player.cpp:956-958). UsedNoTurn = success without a
// turn (slot 23). Refused = legacy `return false` (nothing changed).
enum class UseResult { Refused, Used, UsedNoTurn };

// The player character: stats, inventory, keys, position/heading. Modern
// minimal port of the legacy Player. Inventory slots match legacy indices
// (18 journal, 19 red key, 20 blue key, ...).
class Player {
public:
	CombatEntity baseCe; // base (permanent) stats
	CombatEntity ce;     // current stats (with buffs/gear)

	// Inventory: slot counts (legacy layout).
	int16_t inventory[26] = { 0 };
	// Ammo per weapon ammo type (0 none,1 bullets,2 shells,3 holy water,
	// 4 cells, 5 rockets, 6 soul cube, 7 sentry bot, 8 item).
	int16_t ammo[9] = { 0 };
	// Bitmask of owned weapons.
	int weapons = 0;
	// Active weapon index.
	int weapon = -1;
	const EntityDef* activeWeaponDef = nullptr;
	// op 60 EV_DISABLED_WEAPONS mask (src/ScriptThread.cpp:1445); consumed
	// by fireWeapon's guard.
	int disabledWeapons = 0;
	// Facing-probe result (src/MovementController.cpp:32-87); fed by the
	// Group-2 facing probe, consumed by the HUD health-bar feed.
	Entity* facingEntity = nullptr;

	int level = 1;
	int currentXP = 0;
	int nextLevelXP = 0;
	int xpGained = 0;
	int foundSecretsLevels = 0;     // bit mapID-1: all secrets found (src/Game.cpp:3486-3488)
	// Vending machines (src/Player.cpp:506-518): bit id = hacked; tries are
	// 3-bit fields at bit 3*(k-1), ids 1..9 in ...1 and 10..18 in ...2.
	int hackedVendingMachines = 0;
	int vendingMachineHackTriesLeft1 = 0;
	int vendingMachineHackTriesLeft2 = 0;
	bool god = false;
	int helpBitmask = 0;   // src/Player.cpp:30, :1185-1188; session-only (config persistence not ported)

	// ---- Discrete movement (legacy Canvas) ----
	// Positions/angles in canvas units (tile = 64). view* is the interpolated
	// (current) position, dest* is where the player is heading.
	int viewX = 0, viewY = 0, viewZ = 0;
	int destX = 0, destY = 0, destZ = 0;
	// Angles are RAW ACCUMULATED ints (legacy never masks them after spawn;
	// src/MovementController.cpp:455-466 marches linearly and every consumer
	// masks with & 0x3FF at use). 1024 units = 360 degrees.
	int viewAngle = 0, destAngle = 0;
	// View pitch, legacy units (1024 = full turn, positive = up; written into
	// the camera view matrix). Only the loot-crouch drives it so far — the
	// terrain-slope target machinery is not ported yet
	// (src/MovementController.cpp:230-281).
	int viewPitch = 0;
	// View roll; only the death sequence writes it (src/GameStateRunner.cpp:265, :274).
	int viewRoll = 0;
	int animFrames = 10;
	int animPos = 0;   // step per frame in X/Y
	int animAngle = 0; // step per frame in angle
	int zStep = 0;     // step per frame in Z
	int viewSin = 0, viewCos = 0;
	int viewStepX = 0, viewStepY = 0;
	int viewRightStepX = 0, viewRightStepY = 0;

	// 8 direction vectors (dx,dy) at 45 deg (legacy Canvas::viewStepValues).
	static const int kViewStepValues[16];

	// Recomputes direction vectors / animation params for destAngle.
	void startRotation();
	// Advances view* toward dest* by one frame. Returns true when arrived.
	bool updateView();
	// Snap view* to dest* (used on arrival).
	void finishRotation();

	// Attempts to move one tile to (tx,ty) in canvas units. Returns true if
	// the move is accepted (caller performs collision check).
	bool attemptMove(int tx, int ty);

	// Sets the target height from a terrain height (legacy: getHeight+36).
	void setDestHeight(int terrainHeight) { destZ = terrainHeight + 36; }
	// Sets the Z step per frame based on height difference.
	void setZStep(int diff) { zStep = (diff < 0 ? -diff : diff); if (zStep) zStep = (zStep + animFrames - 1) / animFrames; }

	// No character select: always marine (ScriptVM.cpp:315); 1..3 like legacy.
	int characterChoice = 1;
	void reset();
	// src/Player.cpp:454-486: base DEF/STR/ACC/AGI/IQ + starting credits.
	void setStatsAccordingToCharacterChoice();

	// src/Player.cpp:222-245: adds n2 to stat n (cap 200 for IQ, 99 else; n 0
	// = health) and returns the change actually applied.
	int modifyStat(int n, int n2);

	// src/Player.cpp:2451-2483.
	bool vendingMachineIsHacked(int id) const;
	void setVendingMachineHack(int id);
	int getVendingMachineTriesLeft(int id) const;
	void removeOneVendingMachineTry(int id);

	// The defs table give() needs for the auto-equip lookup the legacy does
	// itself (find(6, 1, wp), src/Player.cpp:160). Wired in core/Main.cpp.
	void setDefs(const EntityDefs* defs) { defs_ = defs; }

	// give(kind, slot, amount, quiet): kind 0 = inventory, 1 = weapons,
	// 2 = ammo, 3 = health. Mirrors legacy Player::give (src/Player.cpp:970).
	// `quiet` is the legacy `b` flag: it gates only the help popups
	// (showWeaponHelp/showInvHelp/showAmmoHelp, :1001,:1029,:1049), none of
	// which is ported — it changes no state here.
	bool give(int kind, int slot, int amount, bool quiet = false);

	// src/Player.cpp:2489-2493 — any of the four sentry-bot weapon bits.
	bool hasASentryBot() const { return (weapons & Enums::WP_SENTRY_BOT_MASK) != 0; }

	// Equips weapon `i`; `def` is the caller's find(ET_ITEM, 1, i) result, which
	// the legacy looks up itself (src/Player.cpp:160). Port of
	// Player::selectWeapon (src/Player.cpp:132-163) minus what the rewrite has
	// no counterpart for: isFamiliar, zoomOut, drawPlayingSoftKeys,
	// readyWeapon/prevWeapon and the HUD repaint flag. The legacy
	// selectNextWeapon() branch (:145-148) is a no-op here too: :153 assigns
	// `i` unconditionally afterwards and only prevWeapon survives it.
	void selectWeapon(int i, const EntityDef* def);

	// requireItem(kind, slot, min, max): inventory count check or weapon bit.
	bool requireItem(int kind, int slot, int min, int max) const;

	// Messaging sinks for addHealth's msg 111 (ADR 0032). Wired in core/Main.cpp.
	void setMessaging(Hud* hud, const Localization* loc) { hud_ = hud; loc_ = loc; }

	// EV_INVENTORY_OP (src/Player.cpp:2664-2759). restoreInventory's angryVIOS
	// clear (:2741) is done by the caller, which owns Game.
	int weaponsCopy = 0;
	int16_t ammoCopy[9] = { 0 };
	int currentWeaponCopy = -1;
	void stripInventoryForViosBattle();
	void stripInventoryForTargetPractice();
	void restoreInventory();

	// EV_USED_CHAINSAW (src/Player.cpp:2636-2652), script form (b = false).
	int chainsawStrengthBonusCount = 0;
	void usedChainsaw();

	bool gameCompleted = false;     // EV_END_GAME (src/ScriptThread.cpp:1647)

	// src/Player.cpp:416-449 (legacy `b` = showMessage gates msg 111).
	bool addHealth(int amount, bool showMessage = true);

	// src/Player.cpp:801-959; n = inventory slot, 22 = holy water.
	UseResult useItem(int n);

	// ---- Status effects / buffs (src/Player.h:107-113) ----
	int statusEffects[54] = { 0 };   // [0+i] turns, [18+i] amount, [36+i] stack count
	int numStatusEffects = 0;
	int16_t buffs[30] = { 0 };       // [0+k] turns, [15+k] amount; derived by translateStatusEffects
	int numbuffs = 0;
	bool addStatusEffect(int n, int amount, int turns);   // src/Player.cpp:1562-1595
	void removeStatusEffect(int n);                       // :1534-1560
	void updateStatusEffects();                           // :1458-1472
	void translateStatusEffects();                        // :1474-1531
	void updateStats();                                   // :1448-1456
	void advanceTurn();                                   // :50-92 subset
	// app->time of the last advanceTurn (:87); the HUD buff box shows turn
	// counts for 600 ms after it (:1614).
	int64_t turnTime = 0;
	void setClock(const int64_t* upTimeMs) { clock_ = upTimeMs; }

	// Health-fraction crossing reported by pain(); the caller shows the message.
	enum class PainBand { None, NearDeath, LowHealth, HalfHealth };
	// Player::pain state half (src/Player.cpp:652-709): god / n == 0 early
	// outs, the 1/256 crossing bands, then addHealth(-n). noDeathFlag is never
	// set in src/ and isFamiliar is absent. Death is NOT handled here (ADR 0028
	// decision 4): the caller checks getHealth().
	PainBand pain(int n);
	// src/Player.cpp:576-622: compass direction 0..7 (3 = ahead) of (x2,y2)
	// seen from (x1,y1) facing `angle`, for the HUD damage arrow/vignette.
	static int calcDamageDir(int x1, int y1, int angle, int x2, int y2);
	// src/Player.cpp:1933-1946: god blocks negatives; clamp [0,200] on STAT_ARMOR.
	bool addArmor(int n);

	// Player::died counters (src/Player.cpp:717-718), zeroed at startup (:34-35).
	int currentLevelDeaths = 0;
	int totalDeaths = 0;
	int completedLevels = 0;   // bit mapID-1 = map left via EV_CHANGE_MAP (src/ScriptThread.cpp:489)
	int highestMap = 1;        // src/Player.cpp:571; raised at load (src/LoadingManager.cpp:641-643)
	int moves = 0;             // turns on this map (src/Player.cpp:53, :96)
	// Level grades, 3 bits per mapID (src/Player.cpp:2058-2074); config-file fields.
	int currentGrades = 0;
	int bestGrades = 0;
	int getCurrentGrade(int mapId) const { return (currentGrades >> (3 * (mapId - 1))) & 0x7; }
	void setCurrentGrade(int mapId, int g) { currentGrades |= g << (3 * (mapId - 1)); }   // OR only, as legacy
	int getBestGrade(int mapId) const { return (bestGrades >> (3 * (mapId - 1))) & 0x7; }
	void setBestGrade(int mapId, int g) {
		bestGrades &= ~(7 << (3 * (mapId - 1)));
		bestGrades |= g << (3 * (mapId - 1));
	}
	// src/Player.cpp:1991-2020: grade 6..1 -> strings file 3 ids 38..43, else 44.
	static int gradeToString(int g) { return g >= 1 && g <= 6 ? 44 - g : 44; }
	// src/Player.cpp:94-105 subset: moves = 0, turnTime = now, health 0 -> 1.
	// numNotebookIndexes / questComplete / questFailed / inCombat are not ported.
	void levelInit();

	int getHealth() const { return ce.getStat(Enums::STAT_HEALTH); }
	int getMaxHealth() const { return ce.getStat(Enums::STAT_MAX_HEALTH); }

	// ---- XP / leveling (src/Player.cpp:264-362; spec deviation 14) ----

	// State half of legacy addXP: counters + level-up loop. The msg-103
	// composition lives in Game::awardKillXP.
	void addXP(int xp);
	// 500n + 100((n−1)^3 + (n−1)) (src/Player.cpp:360-362).
	int calcLevelXP(int n) const;
	// Subset: level++, nextLevelXP, baseCe MAX_HEALTH +10 clamp 999, health
	// refill (src/Player.cpp:283-307,:342). modifyStat DEF/STR/ACC/AGI bumps
	// are deferred and logged.
	void addLevel();

	// Fire-pipeline entry (src/Player.cpp:754-799): guard chain then
	// combat.performAttack. Returns false when the shot is refused.
	bool fireWeapon(Combat& combat, Entity* target, int x, int y);

private:
	const EntityDefs* defs_ = nullptr;
	Hud* hud_ = nullptr;
	const Localization* loc_ = nullptr;
	const int64_t* clock_ = nullptr;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_PLAYER_H