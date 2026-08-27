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
	// Shared UI sheet (tail arrows, scrollbar caps) and the dialog extras
	// loaded for DialogSystem (hero portrait rows, page icons).
	const Texture& imgUIImages() const { return imgUIImages_; }
	const Texture& imgPortraitsSmall() const { return imgPortraitsSmall_; }
	const Texture& imgPageUp() const { return imgPageUp_; }
	const Texture& imgPageDown() const { return imgPageDown_; }
	const Texture& imgPageOk() const { return imgPageOk_; }

	void draw(Graphics2D& g, const Font& font, int canvasWidth, int canvasHeight);
	void drawOverlay(Graphics2D& g, int cinX, int cinY, int cinW);

	// Screen shake (legacy Canvas::startShake + shakeTime/shakeIntensity/
	// shakeX/shakeY, src/Canvas.cpp:1000-1018, src/Canvas.h:228-231).
	// EV_SCREEN_SHAKE arms it; GameContext ticks it each quantum (the
	// MovementController.cpp:381-388 randomize block) and applies the offsets
	// to the rendered view (src/Render.cpp:2265-2270). Amplitude decays
	// linearly to zero across the duration.
	void startShake(int64_t nowMs, int durationMs, int intensity);
	void tickShake(int64_t nowMs);
	int shakeX() const { return shakeX_; }
	int shakeY() const { return shakeY_; }

	// Cockpit overlay raw toggle (legacy hud->cockpitOverlayRaw,
	// src/Hud.h:57): written only by EV_TOGGLE_OVERLAY
	// (src/ScriptThread.cpp:1710-1714), drawn during cinematics.
	void setCockpitOverlay(bool on) { cockpitOverlay_ = on; }
	bool cockpitOverlay() const { return cockpitOverlay_; }
	void drawMonsterHealth(Graphics2D& g, int scrCx, int viewTop);
	void drawWeaponSelection(Graphics2D& g, const Font& font);
	void drawBubbleText(Graphics2D& g, const Font& font, int scrCx, int viewTop, int viewRight);
	// BUBBLE_TEXT_TIME = 1500 ms (src/Hud.h:38, ui.md 18).
	static constexpr int kBubbleDurationMs = 1500;
	void setBubbleText(const std::string& text, uint32_t color, int durationMs = kBubbleDurationMs);
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

	// Message queue (legacy Hud::addMessage, src/Hud.cpp:163-200). Flag bits
	// as in the original: 1 = FORCE (flush the queue, double the duration of
	// the resulting head), 2 = CENTER, 4 = IMPORTANT (src/Hud.cpp:171-199,
	// ui.md 18).
	static constexpr int kMsgFlagForce = 1;
	static constexpr int kMsgFlagCenter = 2;
	static constexpr int kMsgFlagImportant = 4;
	void addMessage(const std::string& text, uint32_t color, int flags);
	// durationMs is ignored: the queue derives every duration from the text
	// length like the original calcMsgTime. Kept as an argument only because
	// the existing gameplay call sites still pass a literal.
	void showCenterMessage(const std::string& text, uint32_t color, int durationMs = 0);
	void showImportantMessage(const std::string& text) {
		addMessage(text, 0xFF7F0000, kMsgFlagImportant);
	}

	// Cinematic text (EV_CAMERA_STR target, legacy hud->subTitleID/
	// subTitleTime + cinTitleID/cinTitleTime: set by src/ScriptThread.cpp:
	// 519-543, expired by src/Hud.cpp:787-798, cleared on ST_CAMERA entry by
	// src/Canvas.cpp:1207-1210, drawn by drawCinematicText src/Hud.cpp:
	// 447-491). Subtitle sits bottom-center at y=280, title top-center at
	// y=1 (480x320 canvas); both shown for the operand duration.
	void setSubtitle(const std::string& text, int durationMs);
	void setCinTitle(const std::string& text, int durationMs);
	void clearCinematicText();

	void update(int timeMs);
	// msgCount = 0 (menu open / ST_CAMERA entry, src/Canvas.cpp:1208).
	void clearMessages() { messages_.clear(); }

	// Draws only the center-message + important-banner; works while the
	// cockpit/HUD stay hidden (spec 2026-08-23-phase5-skeleton §2).
	void drawMessages(Graphics2D& g, const Font& font);

	// Top panel strip + segmented monster health bar. Public for the
	// GameContext::render caller (spec combat-stage1 §0.F); messages are NOT
	// drawn here — drawMessages owns them.
	void drawTopBar(Graphics2D& g, const Font& font, int canvasWidth);

	// Bottom panel background: gameMenu_Panel_bottom.bmp (480x64) at y=256,
	// the legacy band below the 3D viewport (src/TouchController.cpp:544-545,
	// rendering.md 6.1). Background only, no widgets.
	void drawBottomPanel(Graphics2D& g);

	// Health-bar feed (replaces the demo setter; legacy state half of
	// src/Hud.cpp:822-899): id = faced entity sprite index, -1 clears.
	// A target change snaps display HP; an hp change on the same target
	// restarts the 250 ms drain window that update() advances.
	// lowBar = PINKY (eSubType 5) with parm 0 -> bar drops to n3 = 50
	// (src/Hud.cpp:866-868); boss = Entity::isBoss() -> ++n4 (:874).
	void feedMonsterHealth(int id, int hp, int maxHp, bool lowBar = false, bool boss = false);

	// Bottom-bar feed. All values are the RAW stats the legacy widgets read
	// live every frame (src/Hud.cpp:683-709; the invalidate-flag clears in
	// Hud::draw are commented out, ui.md 8): health/maxHealth = stats 0/1,
	// shield = stat 2, weapon = ce->weapon, ammo = ammo[weapons[9w+4]],
	// keysRow = (inventory[19]>0 ? 1 : 0) | (inventory[20]>0 ? 2 : 0)
	// (19 = red, 20 = blue; src/Hud.cpp:1154-1179).
	void feedPlayerStatus(int health, int maxHealth, int shield, int weapon, int ammo, int keysRow);

	// Bottom bar: the widget row on top of drawBottomPanel's background.
	void drawBottomBar(Graphics2D& g, const Font& font);

	// Bottom-bar sub-elements.
	void drawWeapon(Graphics2D& g, int x, int y, int weapon, bool highlighted);
	void drawNumbers(Graphics2D& g, int x, int y, int space, int num, int weapon);
	void drawCurrentKeys(Graphics2D& g, int x, int y);

private:
	void drawArrowControls(Graphics2D& g);
	// calcMsgTime / shiftMsgs (src/Hud.cpp:100-135).
	void calcMsgTime();
	void shiftMsgs();
	void drawImportantMessage(Graphics2D& g, const Font& font, const Text& text, uint32_t color);
	void drawCenterMessage(Graphics2D& g, const Font& font, const Text& text, uint32_t color);

	Texture imgPanelTop_;
	Texture imgPanelBottom_;
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
	Texture imgPortraitsSmall_;
	Texture imgPageUp_;
	Texture imgPageDown_;
	Texture imgPageOk_;
	Texture imgDamageVignette_;
	Texture imgAttArrow_;
	Texture imgHudTest_;

	// Bottom-bar widget values, refreshed by feedPlayerStatus every frame.
	int health_ = 0;
	int maxHealth_ = 1;
	int shield_ = 0;
	int weapon_ = -1;
	int ammo_ = 0;
	int keys_ = 0; // 0=none, 1=red, 2=blue, 3=both
	int playerRow_ = 0; // face row based on health

	// Message queue: only messages_[0] is drawn, and it expires
	// msgDuration_ + 100 ms after becoming the head (src/Hud.cpp:249-251).
	struct Message {
		std::string text;
		uint32_t color = 0;
		int flags = 0;
	};
	// canvas->menuHelpMaxChars, the short-message threshold in calcMsgTime
	// (src/Hud.cpp:128-130, ui.md 18).
	static constexpr int kMenuHelpMaxChars = 49;
	std::vector<Message> messages_;
	int msgTime_ = 0;      // ms since the head became current (legacy msgTime stamp)
	int msgDuration_ = 0;

	// Monster health-bar feed state (legacy lastTarget / monsterStartHealth /
	// monsterDestHealth / monsterHealthChangeTime, src/Hud.cpp:822-899):
	// monsterId_ < 0 hides the bar; displayHp_ eases toward monsterHp_ over
	// the 250 ms window counted by monsterChangeTime_ (update()).
	int monsterId_ = -1;
	int displayHp_ = 0;
	int monsterHp_ = 0;
	int monsterMaxHp_ = 0;
	int monsterChangeTime_ = 0;
	bool monsterLowBar_ = false;
	bool monsterBoss_ = false;

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
	int bubbleTextDuration_ = kBubbleDurationMs;

	// Demo damage effect state.
	int damageDir_ = -1;
	int damageTime_ = 0;
	int damageCount_ = 0;

	// Cinematic text state (subTitleID/subTitleTime + cinTitleID/
	// cinTitleTime analogs; duration-counted instead of gameTime-stamped).
	bool hasSubtitle_ = false;
	std::string subText_;
	int subTitleTime_ = 0;
	int subTitleDuration_ = 0;
	bool hasCinTitle_ = false;
	std::string cinTitleText_;
	int cinTitleTime_ = 0;
	int cinTitleDuration_ = 0;

	// Cockpit overlay toggle (initially false like the legacy memset,
	// src/Hud.cpp:24-26).
	bool cockpitOverlay_ = false;

	// Screen shake state (Canvas shakeTime/shakeIntensity/shakeX/shakeY
	// analogs, src/Canvas.h:228-231).
	int64_t shakeStartMs_ = 0;
	int64_t shakeEndMs_ = 0;
	int shakeIntensity_ = 0;
	int shakeX_ = 0;
	int shakeY_ = 0;
	uint32_t shakeRng_ = 0x2A5F2A5F; // LCG stand-in for legacy app->nextByte()
};

} // namespace newcore

#endif // NEW_UI_HUD_H
