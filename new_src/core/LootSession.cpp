#include "core/LootSession.h"

#include <algorithm>
#include <cstdio>

#include "domain/game/Player.h"
#include "domain/world/MapData.h"
#include "io/Localization.h"
#include "io/Tables.h"

namespace newcore {

void LootSession::init(const Env& env) {
	env_ = env;
}

void LootSession::begin() {
	// Legacy setState hook -> LootingSystem::onEnterLooting
	// (src/Canvas.cpp:1142-1143, src/LootingSystem.cpp:26-33): cache the
	// pose + facing and restart the 500 ms clock, crouch phase first.
	// The rewrite caches viewPitch (no destPitch slope machinery yet;
	// identical value here). poolLoot runs immediately after setState
	// (src/PlayingInputHandler.cpp:374-378): every eType-9 entity on the
	// faced tile is marked looted and the list is built NOW, not at close.
	lootDestX_ = env_.player->viewX;
	lootDestY_ = env_.player->viewY;
	lootDestZ_ = env_.player->viewZ;
	lootDestPitch_ = env_.player->viewPitch;
	lootStepX_ = env_.player->viewStepX >> 6;
	lootStepY_ = env_.player->viewStepY >> 6;
	lootTime_ = *env_.upTimeMs;
	lootCrouch_ = true;
	lootSettleSfx_ = false;                            // field_0xac5_ (:32)
	{
		int tx = (lootDestX_ + lootStepX_ * 64) >> 6;
		int ty = (lootDestY_ + lootStepY_ * 64) >> 6;
		env_.game->loot.poolLootCorpse(tx, ty, *env_.loc, lootPool_);
	}
}

// ---- loot-crouch camera (docs/research/2026-08-25-camera-pitch-loot.md) ----

void LootSession::tick() {
	// LootingSystem::lootingState (src/LootingSystem.cpp:35-83): crouch lerp
	// 500 ms -> dwell (settled crouch pose held every tick, sound 1055 once,
	// loot menu drawn + input live) -> stand-up lerp 500 ms on close ->
	// snap + advanceTurn. The pose is recomputed from scratch every tick,
	// formulas verbatim (16.16 fraction n: remaining, n2: elapsed). Lerps and
	// doors do NOT tick — legacy ST_LOOTING calls lootingState() only
	// (src/Canvas.cpp:940-942).
	Player& p = *env_.player;
	int t = (int)(*env_.upTimeMs - lootTime_);
	if (t < kLootPhaseMs) {
		int n = ((kLootPhaseMs - t) << 16) / kLootPhaseMs; // (:43)
		int n2 = 65536 - n;
		int h0 = env_.map->heightAt(lootDestX_, lootDestY_);
		int h1 = env_.map->heightAt(lootDestX_ + lootStepX_ * 64, lootDestY_ + lootStepY_ * 64);
		if (lootCrouch_) {                              // crouch down (:46-50)
			int hb = (h0 > h1) ? h0 : ((h0 * n + h1 * n2) >> 16);
			p.viewX = lootDestX_ + (48 + ((-48 * n) >> 16)) * lootStepX_;
			p.viewY = lootDestY_ + (48 + ((-48 * n) >> 16)) * lootStepY_;
			p.viewZ = hb + 26 + ((10 * n) >> 16);
			p.viewPitch = std::max(-(64 - ((64 * n) >> 16)) + lootDestPitch_, -64);
		} else {                                        // stand up (:53-57)
			int hb = (h0 > h1) ? h0 : ((h0 * n2 + h1 * n) >> 16);
			p.viewX = lootDestX_ + ((48 * n) >> 16) * lootStepX_;
			p.viewY = lootDestY_ + ((48 * n) >> 16) * lootStepY_;
			p.viewZ = hb + 36 + ((-10 * n) >> 16);
			p.viewPitch = std::max(-((64 * n) >> 16) + lootDestPitch_, -64);
		}
		return;
	}
	if (lootCrouch_) {
		// Crouch settled -> DWELL (:66-71): hold the end pose every tick and
		// play sound 1055 once per session (field_0xac5_ latch :62-65). The
		// clock is NOT restarted; close() (input) starts stand-up.
		int h0 = env_.map->heightAt(lootDestX_, lootDestY_);
		int h1 = env_.map->heightAt(lootDestX_ + lootStepX_ * 64, lootDestY_ + lootStepY_ * 64);
		p.viewX = lootDestX_ + 48 * lootStepX_;
		p.viewY = lootDestY_ + 48 * lootStepY_;
		p.viewZ = std::max(h0, h1) + 26;
		p.viewPitch = std::max(lootDestPitch_ - 64, -64);
		if (!lootSettleSfx_) {
			lootSettleSfx_ = true;
			std::fprintf(stderr, "[loot] sound 1055\n");
		}
	} else {
		// Stand-up expiry: snap home and close the session (:74-80); the turn
		// is consumed only now.
		p.viewX = lootDestX_;
		p.viewY = lootDestY_;
		p.viewZ = env_.map->heightAt(lootDestX_, lootDestY_) + 36;
		p.viewPitch = lootDestPitch_;
		env_.host->requestState(StateId::Playing);
		env_.game->advanceTurn();
	}
}

// ---- loot dwell session (src/LoothingSystem.cpp:85-150) ----

void LootSession::handleAction(Action a) {
	if (!lootCrouch_ || *env_.upTimeMs <= lootTime_ + kLootPhaseMs) return;  // (:87)
	int maxLine = std::max(CorpseLoot::Pool::lineCount(lootPool_) - 3, 0);
	switch (a) {
	case Action::Use:                                   // ACTION_FIRE
		if (lootPool_.topLine >= maxLine) close();
		else lootPool_.topLine = std::min(lootPool_.topLine + 3, maxLine);
		break;
	case Action::Passturn:
	case Action::BackKey:                close(); break;
	case Action::Forward:  lootPool_.topLine = std::max(lootPool_.topLine - 1, 0); break;
	case Action::Back:     lootPool_.topLine = std::min(lootPool_.topLine + 1, maxLine); break;
	case Action::TurnLeft:  lootPool_.topLine = 0; break;
	case Action::TurnRight: lootPool_.topLine = maxLine; break;
	default: break;                                     // other ids ignored
	}
}

void LootSession::close() {
	env_.game->loot.giveLootPool(lootPool_, *env_.player, env_.tables);
	lootCrouch_ = false;
	lootTime_ = *env_.upTimeMs;            // stand-up starts now, zero extra delay
}

// State half of the legacy drawLootingMenu (src/LootingSystem.cpp:118-150): the
// dwell-window gate plus the title compose. The geometry, the colours and the
// scrollbar live in ui/LootView.cpp now (spec 2026-08-27-ui-layer §5).
bool LootSession::buildViewModel(LootListModel& m) {
	if (!(lootCrouch_ && *env_.upTimeMs > lootTime_ + kLootPhaseMs)) return false; // (:121-122)
	if (lootPool_.text.length() == 0 || env_.font == nullptr) return false;
	// Recomposed every frame, exactly like the legacy getSmallBuffer +
	// composeText + dehyphenate + dispose triple (:135-139).
	titleText_.setLength(0);
	titleText_.append(env_.loc->get(kTextMain, 227));
	titleText_.dehyphenate();
	m.title = &titleText_;
	m.text = &lootPool_.text;
	m.lineIndex = lootPool_.lineIndex;
	m.indexCapacity = CorpseLoot::Pool::kMaxLines;
	m.lineCount = CorpseLoot::Pool::lineCount(lootPool_);
	m.topLine = lootPool_.topLine;
	m.visibleRows = 3;                                           // (:140)
	return true;
}

} // namespace newcore
