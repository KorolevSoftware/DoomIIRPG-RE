#ifndef NEW_UI_HUD_H
#define NEW_UI_HUD_H

#include <cstdint>
#include <string>
#include <vector>

#include "render/gl/Texture.h"

namespace newcore {

class Font;
class Text;
class Graphics2D;

// In-game heads-up display: top status bar, bottom control bar (weapon,
// shield, health, keys, player portrait) and the cockpit overlay.
class Hud {
public:
	static constexpr int kMaxMessages = 5;
	static constexpr int kMaxWeaponButtons = 15;

	Hud() = default;

	// Loads all HUD textures. Returns false on failure.
	bool startup();

	const Texture& imgCockpitOverlay() const { return imgCockpitOverlay_; }

	void draw(Graphics2D& g, const Font& font, int canvasWidth, int canvasHeight);
	void drawOverlay(Graphics2D& g, int cinX, int cinY, int cinW);
	void drawMonsterHealth(Graphics2D& g, int scrCx, int viewTop);
	void drawWeaponSelection(Graphics2D& g, const Font& font);
	void drawBubbleText(Graphics2D& g, const Font& font, int scrCx, int viewTop, int viewRight);
	void setBubbleText(const std::string& text, uint32_t color, int durationMs = 3000);
	void setBubbleText(int durationMs) { if (bubbleText_.empty()) bubbleTextTime_ = 0; else bubbleTextTime_ = durationMs; }

	// Demo damage effects (red vignette + attack direction arrow).
	void setDamageDemo(int dir, int durationMs = 1500) {
		damageDir_ = dir;
		damageTime_ = durationMs;
		damageCount_ = 1;
	}
	void drawDamageVignette(Graphics2D& g, int viewX, int viewY, int viewW, int viewH);
	void drawHudOverdraw(Graphics2D& g, int hudX, int hudY, int hudW, int hudH);
	void setWeaponSelect(bool on) { weaponSelect_ = on; }
	bool weaponSelect() const { return weaponSelect_; }
	void setTouchedWeapon(int w) { touchedWeapon_ = w; }
	void setArrowPressed(int a) { arrowPressed_ = a; }
	void setShowArrows(bool on) { showArrows_ = on; }

	// Demo message API (temporary stand-in for the real message queue).
	void showCenterMessage(const std::string& text, uint32_t color, int durationMs = 700);
	void showImportantMessage(const std::string& text) {
		importantText_ = text;
		hasImportant_ = true;
		importantTime_ = 0;
	}
	void clearImportantMessage() { hasImportant_ = false; importantText_.clear(); }
	// Dialog-lite passthrough (task FIX B): gray bottom panel like the legacy
	// script dialogs, NOT ticked by update() — persists until explicitly
	// dismissed (legacy dialogs close on ACTION_FIRE,
	// src/DialogSystem.cpp:34-48).
	void showDialogMessage(const std::string& text) { dialogText_ = text; hasDialog_ = true; }
	void clearDialogMessage() { hasDialog_ = false; dialogText_.clear(); }
	void update(int timeMs);
	void clearMessages() { hasCenterMessage_ = false; importantText_.clear(); }

	// Draws only the center-message + important-banner; works while the
	// cockpit/HUD stay hidden (spec 2026-08-23-phase5-skeleton §2).
	void drawMessages(Graphics2D& g, const Font& font);

	// Demo monster for the health bar.
	void setDemoMonster(int hp, int maxHp) {
		monsterValid_ = true;
		monsterHp_ = hp;
		monsterMaxHp_ = maxHp;
	}

	// Bottom-bar sub-elements.
	void drawWeapon(Graphics2D& g, int x, int y, int weapon, bool highlighted);
	void drawNumbers(Graphics2D& g, int x, int y, int space, int num, int weapon);
	void drawCurrentKeys(Graphics2D& g, int x, int y);

private:
	// Shared draw for the top panel strip.
	void drawTopBar(Graphics2D& g, const Font& font, int canvasWidth);
	void drawBottomBar(Graphics2D& g, const Font& font);
	void drawArrowControls(Graphics2D& g);
	void drawImportantMessage(Graphics2D& g, const Font& font, const Text& text, uint32_t color);
	void drawCenterMessage(Graphics2D& g, const Font& font, const Text& text, uint32_t color);
	void drawDialogMessage(Graphics2D& g, const Font& font);

	Texture imgPanelTop_;
	Texture imgWeaponNormal_;
	Texture imgWeaponActive_;
	Texture imgShieldNormal_;
	Texture imgShieldButtonActive_;
	Texture imgKeyNormal_;
	Texture imgKeyActive_;
	Texture imgHealthNormal_;
	Texture imgHealthButtonActive_;
	Texture imgPlayerFaces_;
	Texture imgPlayerActive_;
	Texture imgPlayerFrameNormal_;
	Texture imgPlayerFrameActive_;
	Texture imgNumbers_;
	Texture imgCockpitOverlay_;
	Texture imgArrowUp_;
	Texture imgArrowDown_;
	Texture imgArrowLeft_;
	Texture imgArrowRight_;
	Texture imgArrowUpPressed_;
	Texture imgArrowDownPressed_;
	Texture imgArrowLeftPressed_;
	Texture imgArrowRightPressed_;

	Texture imgUIImages_;
	Texture imgDamageVignette_;
	Texture imgAttArrow_;
	Texture imgHudTest_;

	// Demo state (later replaced by real player state).
	int health_ = 100;
	int maxHealth_ = 100;
	int shield_ = 75;
	int weapon_ = 3;
	int ammo_ = 30;
	int keys_ = 3; // 0=none, 1=red, 2=blue, 3=both
	int playerRow_ = 0; // face row based on health

	// Demo messages.
	bool hasCenterMessage_ = false;
	uint32_t centerColor_ = 0xAA000000;
	std::string centerText_;
	int centerTime_ = 0;
	int centerDuration_ = 700;
	std::string importantText_;
	bool hasImportant_ = false;
	int importantTime_ = 0;          // style-3 messages auto-expire like the legacy queue
	static constexpr int kImportantDurationMs = 3500;
	bool hasDialog_ = false;         // dialog-lite: no timer, dismiss-driven
	std::string dialogText_;

	// Demo monster for the health bar.
	bool monsterValid_ = false;
	int monsterHp_ = 100;
	int monsterMaxHp_ = 100;

	// Demo weapon select screen.
	bool weaponSelect_ = false;
	int touchedWeapon_ = -1;

	// Demo arrow controls: 0=none,1=up,2=down,3=left,4=right.
	int arrowPressed_ = 0;
	bool showArrows_ = true;

	// Demo bubble text (speech bubble above the player/facing entity).
	std::string bubbleText_;
	uint32_t bubbleColor_ = 0xFF002864;
	int bubbleTextTime_ = 0;
	int bubbleTextDuration_ = 3000;

	// Demo damage effect state.
	int damageDir_ = -1;
	int damageTime_ = 0;
	int damageCount_ = 0;
};

} // namespace newcore

#endif // NEW_UI_HUD_H
