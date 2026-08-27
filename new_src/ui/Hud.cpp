#include "ui/Hud.h"

#include <algorithm>
#include <cstdio>

#include "core/AppContext.h"
#include "io/BmpImageLoader.h"
#include "render/Graphics2D.h"
#include "text/Font.h"
#include "text/Text.h"

namespace newcore {

namespace {

constexpr int kPanelTopW = 480;
constexpr int kPanelTopH = 20;
constexpr int kPanelBottomY = 256;
constexpr int kWeaponH = 44;
constexpr int kNumH = 20;
constexpr int kKeyH = 44;
constexpr int kFaceH = 32;

// Legacy drawWeapon texY for each weapon id (see Hud::drawWeapon).
int weaponTexY(int weapon) {
	switch (weapon) {
	case 0: return 0;
	case 1: return 1;
	case 2: return 2;
	case 3: case 4: return 10;
	case 5: case 6: return 11;
	case 7: return 3;
	case 8: return 4;
	case 9: return 5;
	case 10: return 6;
	case 11: return 7;
	case 12: return 8;
	case 13: return 9;
	case 14: return 12;
	default: return 13;
	}
}

bool weaponShowsAmmo(int weapon) {
	switch (weapon) {
	case 1: return false;
	default: return true;
	}
}

bool loadTexture(const AppContext& app, const char* name, Texture& out) {
	std::vector<uint8_t> bmp;
	if (!app.readResource(name, bmp)) {
		std::fprintf(stderr, "Hud: missing resource %s\n", name);
		return false;
	}
	BmpImageLoader loader;
	ImagePtr img = loader.load(bmp, true);
	if (!img) {
		std::fprintf(stderr, "Hud: failed to decode %s\n", name);
		return false;
	}
	out.uploadIndexed(img->indices(), img->width(), img->height(), img->palette(), true);
	if (!out.valid()) {
		std::fprintf(stderr, "Hud: texture upload failed for %s\n", name);
		return false;
	}
	return true;
}

} // namespace

bool Hud::startup() {
	const AppContext& app = AppContext::instance();
	bool ok = true;
	ok &= loadTexture(app, "HUD_Panel_top.bmp", imgPanelTop_);
	ok &= loadTexture(app, "gameMenu_Panel_bottom.bmp", imgPanelBottom_);
	ok &= loadTexture(app, "Hud_Weapon_Normal.bmp", imgWeaponNormal_);
	ok &= loadTexture(app, "HUD_Weapon_Active.bmp", imgWeaponActive_);
	ok &= loadTexture(app, "arrow-up.bmp", imgArrowUp_);
	ok &= loadTexture(app, "arrow-down.bmp", imgArrowDown_);
	ok &= loadTexture(app, "arrow-left.bmp", imgArrowLeft_);
	ok &= loadTexture(app, "arrow-right.bmp", imgArrowRight_);
	ok &= loadTexture(app, "arrow-up_pressed.bmp", imgArrowUpPressed_);
	ok &= loadTexture(app, "arrow-down_pressed.bmp", imgArrowDownPressed_);
	ok &= loadTexture(app, "arrow-left_pressed.bmp", imgArrowLeftPressed_);
	ok &= loadTexture(app, "arrow-right_pressed.bmp", imgArrowRightPressed_);
	ok &= loadTexture(app, "ui_images.bmp", imgUIImages_);
	ok &= loadTexture(app, "Hud_Portrait_Small.bmp", imgPortraitsSmall_);
	ok &= loadTexture(app, "pageUP_Icon.bmp", imgPageUp_);
	ok &= loadTexture(app, "pageDOWN_Icon.bmp", imgPageDown_);
	ok &= loadTexture(app, "pageOK_Icon.bmp", imgPageOk_);
	ok &= loadTexture(app, "damage.bmp", imgDamageVignette_);
	ok &= loadTexture(app, "Hud_Attack_Arrows.bmp", imgAttArrow_);
	ok &= loadTexture(app, "Hud_Test.bmp", imgHudTest_);
	std::fprintf(stdout, "Hud: weaponNormal=%dx%d valid=%d, weaponActive valid=%d\n",
		imgWeaponNormal_.width(), imgWeaponNormal_.height(), (int)imgWeaponNormal_.valid(),
		(int)imgWeaponActive_.valid());
	ok &= loadTexture(app, "HUD_Shield_Normal.bmp", imgShieldNormal_);
	ok &= loadTexture(app, "Hud_Shield_Button_Active.bmp", imgShieldButtonActive_);
	ok &= loadTexture(app, "Hud_Key_Normal.bmp", imgKeyNormal_);
	ok &= loadTexture(app, "Hud_Key_Active.bmp", imgKeyActive_);
	ok &= loadTexture(app, "HUD_Health_Normal.bmp", imgHealthNormal_);
	ok &= loadTexture(app, "Hud_Health_Button_Active.bmp", imgHealthButtonActive_);
	ok &= loadTexture(app, "Hud_Player.bmp", imgPlayerFaces_);
	ok &= loadTexture(app, "HUD_Player_Active.bmp", imgPlayerActive_);
	ok &= loadTexture(app, "HUD_Player_frame_Normal.bmp", imgPlayerFrameNormal_);
	ok &= loadTexture(app, "HUD_Player_frame_Active.bmp", imgPlayerFrameActive_);
	ok &= loadTexture(app, "Hud_Numbers.bmp", imgNumbers_);
	ok &= loadTexture(app, "cockpit.bmp", imgCockpitOverlay_);
	return ok;
}

void Hud::draw(Graphics2D& g, const Font& font, int canvasWidth, int canvasHeight) {
	// Demo: attack-direction arrow while the damage effect is active.
	if (damageCount_ > 0 && damageTime_ > 0 && imgAttArrow_.valid() && (1 << damageDir_ & 0xC1)) {
		int n4 = canvasHeight - 20 - 25;
		int n5 = 0;
		int n6;
		if (damageDir_ == 0) { n6 = canvasWidth - 20; n5 = 24; }
		else if (damageDir_ == 6) { n6 = 20; n5 = 12; }
		else { n6 = canvasWidth >> 1; }
		g.drawRegion(imgAttArrow_, 0, n5, 12, 12, n6, n4, Graphics2D::kAnchorHCenter);
	}
	drawDamageVignette(g, 0, 42, canvasWidth, canvasHeight - 42);
	drawHudOverdraw(g, 0, 0, canvasWidth, canvasHeight);
	drawTopBar(g, font, canvasWidth); // also paints the bottom panel background
	if (showArrows_) drawArrowControls(g);
	drawBottomBar(g, font);
	drawBubbleText(g, font, 240, 42, 480);
}

void Hud::drawOverlay(Graphics2D& g, int cinX, int cinY, int cinW) {
	if (!imgCockpitOverlay_.valid()) return;
	// Legacy: left copy at (cinRect[0], cinRect[1]); right copy mirrored with
	// flags=24 (TOP|RIGHT) so its right edge anchors at cinRect[2] (=x=cinW-240).
	g.drawImage(imgCockpitOverlay_, 0, 0, 240, 234, cinX, cinY, 240, 234, 0);
	g.drawImage(imgCockpitOverlay_, cinX + cinW, cinY,
		Graphics2D::kAnchorTop | Graphics2D::kAnchorRight, 4);
}

// Legacy feeds app->nextByte() (an LCG over the save buffer) into the shake
// offsets (src/MovementController.cpp:381-383); a local LCG stands in here.
static uint8_t shakeNextByte(uint32_t& s) {
	s = s * 1664525u + 1013904223u;
	return static_cast<uint8_t>(s >> 24);
}

void Hud::startShake(int64_t nowMs, int durationMs, int intensity) {
	if (intensity == 0) return;                 // src/Canvas.cpp:1008
	shakeStartMs_ = nowMs;
	shakeEndMs_ = nowMs + durationMs;           // shakeTime = time + i (:1009)
	shakeIntensity_ = intensity;                // 2 * packed dur field (:1010)
	tickShake(nowMs);
}

void Hud::tickShake(int64_t nowMs) {
	if (shakeIntensity_ == 0) return;
	if (nowMs >= shakeEndMs_) {                 // expiry snaps to zero (:386-388)
		shakeIntensity_ = 0;
		shakeX_ = 0;
		shakeY_ = 0;
		return;
	}
	// Amplitude decays linearly to zero across the duration (task spec;
	// legacy holds full amplitude until the deadline instead).
	int amp = static_cast<int>(static_cast<int64_t>(shakeIntensity_) *
		(shakeEndMs_ - nowMs) / (shakeEndMs_ - shakeStartMs_));
	if (amp <= 0) {
		shakeX_ = 0;
		shakeY_ = 0;
		return;
	}
	shakeX_ = static_cast<int>(shakeNextByte(shakeRng_) % (amp * 2)) - amp;
	shakeY_ = static_cast<int>(shakeNextByte(shakeRng_) % (amp * 2)) - amp;
}

void Hud::drawDamageVignette(Graphics2D& g, int viewX, int viewY, int viewW, int viewH) {
	if (damageCount_ <= 0 || !imgDamageVignette_.valid()) return;
	if (damageTime_ <= 0) { damageCount_ = 0; return; }

	int n = 0;
	switch (damageDir_) {
		case 1: n = 2; break;
		case 2: n = 3; break;
		case 3: n = 15; break;
		case 4: n = 5; break;
		case 5: n = 4; break;
		case 0: case 6: case 7: n = 8; break;
		default: damageCount_ = 0; return;
	}
	int width = imgDamageVignette_.width(); // 16
	auto tile = [&](int x, int y, int w, int h, int rot) {
		// Legacy fillRegion: repeat the 16x16 texture (no scaling).
		int yBeg = y, yEnd = y + h;
		while (yBeg < yEnd) {
			int texh = std::min(yEnd - yBeg, width);
			int xBeg = x, xEnd = x + w;
			while (xBeg < xEnd) {
				int texw = std::min(xEnd - xBeg, width);
				g.drawRegion(imgDamageVignette_, 0, 0, texw, texh, xBeg, yBeg, 0, rot);
				xBeg += texw;
			}
			yBeg += texh;
		}
	};
	if (n & 1) tile(viewX, viewY, viewW, width, 1);                        // top
	if (n & 2) tile(viewX + (viewW - width), viewY, width, viewH, 4);      // right
	if (n & 4) tile(viewX, viewY, width, viewH, 0);                        // left
	if (n & 8) tile(viewX, viewY + (viewH - width), viewW, width, 3);      // bottom
}

void Hud::drawHudOverdraw(Graphics2D& g, int hudX, int hudY, int hudW, int hudH) {
	if (!imgHudTest_.valid()) return;
	// Legacy: thin strip (13px) of Hud_Test across the bottom panel's top edge.
	// drawRegion(... 0,0,hudW,13, hudX, hudY+hudH-49, flags=20, ...)
	// flags=20 -> TOP|RIGHT? No: 20=16|4 = TOP|LEFT anchored. Using anchored blit:
	g.drawRegion(imgHudTest_, 0, 0, hudW, 13, hudX, hudY + hudH - 49,
		Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft);
}

void Hud::drawMonsterHealth(Graphics2D& g, int scrCx, int viewTop) {
	if (monsterId_ < 0 || monsterMaxHp_ <= 0) return;

	// Drain animation tail (src/Hud.cpp:851-859): clamp the animated source
	// to max, then ease toward the fed hp across the 250 ms window
	// (monsterChangeTime_ advances in update(); >250 snaps to monsterHp_).
	int stat = monsterMaxHp_;
	int stat2;
	if (displayHp_ > stat) displayHp_ = stat;
	if (monsterChangeTime_ > 250) {
		stat2 = monsterHp_;                                                          // :854-856
	} else {
		stat2 = displayHp_ - (displayHp_ - monsterHp_) * monsterChangeTime_ / 250;   // :858
	}

	// Legacy: n=25, n4 = 2*(screenRect[2]<<8)/128>>8 = 2*screenW/128 (=7.5).
	int n = 25;
	if (stat2 > stat) stat2 = stat;
	int n2 = ((n << 8) * ((stat2 << 16) / (stat << 8)) >> 8) + 256 - 1 >> 8;
	if (n2 == 0 && stat2 > 0) n2 = 1;
	int n4 = 2 * (480 << 8) / 128 >> 8;
	if (monsterBoss_) ++n4;                 // :874 boss segments are 1px wider
	else if ((n4 & 0x1) != 0) ++n4;         // odd -> even (both give 8 at 480)
	int n5 = 2 + n4 * n;
	int n6 = scrCx - (n5 >> 1);
	// :866-871: pinky with parm 0 pushes the bar down; the zoomed-in +20
	// variant has no counterpart here (no zoom system yet).
	int n3 = monsterLowBar_ ? 50 : 6;
	int y = viewTop + n3;

	g.fillRect(n6, y, n5, n4 * 2 + 1, 0, 0, 0);
	g.drawRect(n6, y, n5, n4 * 2 + 1, 0xAA, 0xAA, 0xAA);

	uint8_t r, g8, b8;
	if (n2 <= n / 4) { r = 0xFF; g8 = 0; b8 = 0; }
	else if (n - n2 <= n / 4) { r = 0; g8 = 0xFF; b8 = 0; }
	else { r = 0xFF; g8 = 0x88; b8 = 0; }
	n6 += 2;
	for (int i = 0; i < n2; ++i) {
		g.fillRect(n6, y + 2, n4 - 1, n4 * 2 - 2, r, g8, b8);
		n6 += n4;
	}
}

void Hud::feedMonsterHealth(int id, int hp, int maxHp, bool lowBar, bool boss) {
	// State half of legacy drawMonsterHealth (src/Hud.cpp:838-850): a new
	// target snaps the animated value; changed hp on the same target starts
	// a fresh 250 ms drain from the previous destination.
	if (id != monsterId_) {
		monsterId_ = id;
		displayHp_ = hp;
		monsterHp_ = hp;
		monsterChangeTime_ = 0;
	} else if (hp != monsterHp_) {
		displayHp_ = monsterHp_;
		monsterHp_ = hp;
		monsterChangeTime_ = 0;
	}
	monsterMaxHp_ = maxHp;
	monsterLowBar_ = lowBar;
	monsterBoss_ = boss;
}

void Hud::feedPlayerStatus(int health, int maxHealth, int shield, int weapon, int ammo, int keysRow) {
	// Pure value transfer: the legacy widgets read these straight off the
	// player each draw pass (src/Hud.cpp:683-709), so there is no smoothing
	// and no dirty flag here. maxHealth is only ever used as a divisor for
	// the portrait row, which the original leaves unguarded (ui.md 4).
	health_ = health;
	maxHealth_ = maxHealth;
	shield_ = shield;
	weapon_ = weapon;
	ammo_ = ammo;
	keys_ = keysRow;
}

void Hud::drawWeaponSelection(Graphics2D& g, const Font& font) {
	if (!weaponSelect_ || !imgWeaponNormal_.valid()) return;

	// Legacy FMGL_fillRect: semi-transparent black over the whole screen.
	g.fillRect(0, imgPanelTop_.valid() ? imgPanelTop_.height() : 20, 480, 320, 0, 0, 0, 128);

	// Demo weapons: draw a handful in a 4-column grid like the legacy select.
	int owned = 0b0000000000101111; // weapons 0..4 owned
	int x0 = (480 - 16 - 4 * imgWeaponNormal_.width()) / 2;
	int y0 = 320 - 108;
	int posX = 0, posY = -4, v8 = 0;
	for (int i = 0; i < 15; ++i) {
		if (((owned >> i) & 1) == 0) continue;
		if (v8 > 0 && (v8 & 3) == 0) {
			posY -= 48;
			posX = 0;
		}
		++v8;
		// Legacy: highlighted only while the finger is on the button.
		drawWeapon(g, x0 + posX, posY + y0, i, i == touchedWeapon_);
		posX += imgWeaponNormal_.width() + 4;
	}
}

void Hud::drawArrowControls(Graphics2D& g) {
	if (!imgArrowUp_.valid()) return;

	// Legacy controlMode==0 button positions (fmButton touch areas):
	// up(42,31) down(42,181) left(5,106) right(80,106), 70x70 buttons,
	// the arrow images themselves are 80x76 drawn at those anchor points.
	Texture* texUp = (arrowPressed_ == 1) ? &imgArrowUpPressed_ : &imgArrowUp_;
	Texture* texDown = (arrowPressed_ == 2) ? &imgArrowDownPressed_ : &imgArrowDown_;
	Texture* texLeft = (arrowPressed_ == 3) ? &imgArrowLeftPressed_ : &imgArrowLeft_;
	Texture* texRight = (arrowPressed_ == 4) ? &imgArrowRightPressed_ : &imgArrowRight_;
	g.drawImage(*texUp, 42, 31, 0);
	g.drawImage(*texDown, 42, 181, 0);
	g.drawImage(*texLeft, 5, 106, 0);
	g.drawImage(*texRight, 80, 106, 0);
}

void Hud::drawTopBar(Graphics2D& g, const Font& font, int canvasWidth) {
	if (!imgPanelTop_.valid()) return;
	g.drawImage(imgPanelTop_, canvasWidth / 2, 0, Graphics2D::kAnchorHCenter);

	// Segmented health bar under the panel (spec combat-stage1 §0.F):
	// viewRect[1]=20 here + the legacy n3=6 inset = absolute y=26
	// (src/Hud.cpp:882). Messages are drawn solely by drawMessages.
	drawMonsterHealth(g, 240, 20);

	// Legacy paints the bottom panel in the same HUD pass as the top strip
	// (src/TouchController.cpp:544-545). Called here rather than from
	// GameContext::render so the gameplay states {Playing, Looting, Dialog}
	// that already own the drawTopBar call get it with no extra call site.
	drawBottomPanel(g);
}

void Hud::drawBottomPanel(Graphics2D& g) {
	if (!imgPanelBottom_.valid()) return;
	g.drawImage(imgPanelBottom_, 0, kPanelBottomY, 0);
}

void Hud::drawImportantMessage(Graphics2D& g, const Font& font, const Text& text, uint32_t color) {
	int x = 0, y = 0, w = 480, h = 18;
	uint8_t r = (uint8_t)(color >> 16);
	uint8_t g8 = (uint8_t)(color >> 8);
	uint8_t b8 = (uint8_t)color;
	g.fillRect(x, y, w, h, r, g8, b8);
	g.drawRect(x, y, w, h, 255, 255, 255);
	Text t(text);
	t.dehyphenate();
	g.drawString(font, t, x + 2, y + 2, Graphics2D::kAnchorLeft);
}

void Hud::drawCenterMessage(Graphics2D& g, const Font& font, const Text& text, uint32_t color) {
	int w = text.getStringWidth() + 8;
	if (w > 480) w = 480;
	int y = 40;
	int x = 240;
	int numLines = text.getNumLines();
	uint8_t r = (uint8_t)(color >> 16);
	uint8_t g8 = (uint8_t)(color >> 8);
	uint8_t b8 = (uint8_t)color;
	g.fillRect(x - (w / 2), y, w - 1, (16 * numLines) + 3, r, g8, b8);
	g.drawRect(x - (w / 2), y, w - 1, (16 * numLines) + 3, 0xAA, 0xAA, 0xAA);

	y += 3;
	int i = 0;
	while (true) {
		int first = text.findFirstOf('|', i);
		int end = (first >= 0) ? first : text.length();
		Text line;
		text.substring(line, i, end - i);
		g.drawString(font, line, x, y, Graphics2D::kAnchorHCenter);
		y += 16;
		if (first < 0) break;
		i = first + 1;
	}
}

void Hud::showCenterMessage(const std::string& text, uint32_t color, int durationMs) {
	centerText_ = text;
	centerColor_ = color;
	centerDuration_ = durationMs;
	centerTime_ = 0;
	hasCenterMessage_ = true;
}

void Hud::setSubtitle(const std::string& text, int durationMs) {
	subText_ = text;
	subTitleDuration_ = durationMs;
	subTitleTime_ = 0;
	hasSubtitle_ = true;
}

void Hud::setCinTitle(const std::string& text, int durationMs) {
	cinTitleText_ = text;
	cinTitleDuration_ = durationMs;
	cinTitleTime_ = 0;
	hasCinTitle_ = true;
}

void Hud::clearCinematicText() {
	// ST_CAMERA entry (src/Canvas.cpp:1207-1210).
	hasSubtitle_ = false;
	subText_.clear();
	hasCinTitle_ = false;
	cinTitleText_.clear();
}

void Hud::setBubbleText(const std::string& text, uint32_t color, int durationMs) {
	bubbleText_ = text;
	bubbleColor_ = color;
	bubbleTextTime_ = 0;
	bubbleTextDuration_ = durationMs;
}

void Hud::drawBubbleText(Graphics2D& g, const Font& font, int scrCx, int viewTop, int viewRight) {
	if (bubbleText_.empty()) return;
	if (bubbleTextTime_ >= bubbleTextDuration_) {
		bubbleText_.clear();
		return;
	}

	int n = viewTop + 1;
	int n2 = scrCx + 5;
	int n3 = 0, n4 = 6;
	switch (bubbleColor_) {
		case 0xFF800000: n3 = 0; break;
		case 0xFF002864: n3 = 10; break;
		case 0xFF2E0854: n3 = 20; break;
		case 0xFFFF9600: n3 = 45; n4 = 12; break;
	}
	n += 10;
	int n5 = (int)bubbleText_.size() * 9 + 6;
	int n6 = 20;
	int n7 = n2 - std::max(0, n5 + 2 - (viewRight - n2));
	if (n7 + 15 < scrCx) n4 = 12;

	g.fillRect(n7, n, n5, n6, (bubbleColor_ >> 16) & 0xFF, (bubbleColor_ >> 8) & 0xFF, bubbleColor_ & 0xFF);
	g.drawRect(n7, n, n5, n6, 255, 255, 255);
	Text t;
	t.append(bubbleText_);
	// Legacy: drawString(bubbleText, n7+2, n+3, 4) -> flags=4 is LEFT anchor.
	g.drawString(font, t, n7 + 2, n + 3, Graphics2D::kAnchorLeft);
	if (imgUIImages_.valid()) {
		g.drawRegion(imgUIImages_, n3, n4, 10, 6, n7 + 5, n + n6, 0);
	}
}

// Dialog boxes are drawn by DialogSystem::draw (spec GROUP 1); Hud only
// carries the messages below.
void Hud::drawMessages(Graphics2D& g, const Font& font) {
	if (hasImportant_) {
		Text t;
		t.append(importantText_);
		drawImportantMessage(g, font, t, 0xFF7F0000);
	} else if (hasCenterMessage_ && centerTime_ >= 0) {
		Text t;
		t.append(centerText_);
		drawCenterMessage(g, font, t, centerColor_);
	}
	// Cinematic text (drawCinematicText analog, src/Hud.cpp:461-487):
	// title top-center at y=1 (drawString flags=1), subtitle bottom-center
	// at y=280 — n4 = (cinRect[1]+cinRect[3] + ((320 - n3 - 32) >> 1)) - 10
	// with cinRect={0,42,480,250} — both HCENTER|TOP. The legacy wrapped a
	// second line at +16 px; the fixed-width rewrite font renders one line.
	if (hasCinTitle_) {
		Text t;
		t.append(cinTitleText_);
		g.drawString(font, t, 240, 1, Graphics2D::kAnchorHCenter | Graphics2D::kAnchorTop);
	}
	if (hasSubtitle_) {
		Text t;
		t.append(subText_);
		g.drawString(font, t, 240, 280, Graphics2D::kAnchorHCenter | Graphics2D::kAnchorTop);
	}
}

void Hud::update(int timeMs) {
	if (hasCenterMessage_) {
		centerTime_ += timeMs;
		if (centerTime_ >= centerDuration_ + 100) {
			hasCenterMessage_ = false;
		}
	}
	if (hasImportant_) {
		importantTime_ += timeMs;
		if (importantTime_ >= kImportantDurationMs) hasImportant_ = false;
	}
	if (!bubbleText_.empty()) bubbleTextTime_ += timeMs;
	// Health-bar drain clock (src/Hud.cpp:854 threshold): past 250 ms the
	// bar snaps to the fed hp instead of easing.
	if (monsterId_ >= 0) monsterChangeTime_ += timeMs;
	if (damageCount_ > 0) {
		damageTime_ -= timeMs;
		if (damageTime_ <= 0) damageCount_ = 0;
	}
	// Cinematic-text expiry: legacy clears when subTitleTime/cinTitleTime
	// pass gameTime during the HUD pass (src/Hud.cpp:787-798).
	if (hasCinTitle_) {
		cinTitleTime_ += timeMs;
		if (cinTitleTime_ >= cinTitleDuration_) {
			hasCinTitle_ = false;
			cinTitleText_.clear();
		}
	}
	if (hasSubtitle_) {
		subTitleTime_ += timeMs;
		if (subTitleTime_ >= subTitleDuration_) {
			hasSubtitle_ = false;
			subText_.clear();
		}
	}
}

void Hud::drawBottomBar(Graphics2D& g, const Font& font) {
	if (weaponSelect_) {
		drawWeaponSelection(g, font);
		return;
	}

	// Weapon.
	drawWeapon(g, 268, 258, weapon_, false);

	// Shield.
	Texture& shield = imgShieldNormal_;
	g.drawImage(shield, 49, 258, 0);
	drawNumbers(g, 89, 268, 1, shield_, -1);

	// Health.
	Texture& health = imgHealthNormal_;
	g.drawImage(health, 133, 258, 0);
	drawNumbers(g, 173, 268, 1, health_, -1);

	// Player portrait.
	playerRow_ = 4 - 5 * health_ / (maxHealth_ ? maxHealth_ : 1);
	if (playerRow_ < 0) playerRow_ = 0;
	if (imgPlayerFaces_.valid()) {
		g.drawRegion(imgPlayerFaces_, 0, kFaceH * playerRow_, imgPlayerFaces_.width(), kFaceH,
			224, 263, Graphics2D::kAnchorTop);
	}
	if (imgPlayerFrameNormal_.valid()) {
		g.drawImage(imgPlayerFrameNormal_, 216, 255, 0);
	}

	// Keys.
	drawCurrentKeys(g, 375, 258);
}

void Hud::drawWeapon(Graphics2D& g, int x, int y, int weapon, bool highlighted) {
	Texture& tex = highlighted ? imgWeaponActive_ : imgWeaponNormal_;
	if (!tex.valid()) return;
	int texY = weaponTexY(weapon);
	g.drawRegion(tex, 0, texY * kWeaponH, tex.width(), kWeaponH, x, y, Graphics2D::kAnchorTop);
	if (weaponShowsAmmo(weapon)) {
		drawNumbers(g, x + (tex.width() >> 1) + 6, y + 8, 1, ammo_, weapon);
	}
}

void Hud::drawNumbers(Graphics2D& g, int x, int y, int space, int num, int weapon) {
	if (!imgNumbers_.valid() || num >= 1000) return;
	int h1 = num / 100;
	int h2 = num % 100 / 10;
	int h3 = num % 100 % 10;
	if (weapon == 13) {
		h1 = num % 100 % 10;
		h3 = 5;
	}
	if (weapon == 13) {
		h2 = -1;
	}
	// All three glyphs are drawn unconditionally and at fixed offsets
	// (src/Hud.cpp:1143-1150). h2 == -1 is meaningful: row 20*(9-(-1)) = 200
	// is row 10 of the 11-row sheet, the '/' glyph, so weapon 13 reads "N/5".
	// No leading-zero suppression either: 0 shows "000".
	g.drawRegion(imgNumbers_, 0, kNumH * (9 - h1), 10, kNumH, x, y, Graphics2D::kAnchorTop);
	g.drawRegion(imgNumbers_, 0, kNumH * (9 - h2), 10, kNumH, x + space + 10, y, Graphics2D::kAnchorTop);
	g.drawRegion(imgNumbers_, 0, kNumH * (9 - h3), 10, kNumH, x + 2 * space + 20, y, Graphics2D::kAnchorTop);
}

void Hud::drawCurrentKeys(Graphics2D& g, int x, int y) {
	Texture& tex = imgKeyNormal_;
	if (!tex.valid()) return;
	g.drawRegion(tex, 0, kKeyH * keys_, tex.width(), kKeyH, x, y, Graphics2D::kAnchorTop);
}

} // namespace newcore
