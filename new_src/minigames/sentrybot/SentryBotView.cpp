#include "minigames/sentrybot/SentryBotView.h"

#include <cstdint>

#include "minigames/common/MiniGameHelpView.h"
#include "minigames/sentrybot/SentryBotAssets.h"
#include "render/api/Texture.h"
#include "text/Text.h"
#include "ui/Ui.h"

namespace newcore {

namespace {

constexpr int kCenterX = 240;   // screenRect half width (n)
constexpr int kBoardY = 50;     // screenRect[1] + 50 (n2)
constexpr char kCursorGlyph = '\x8A';   // Graphics::drawCursor 3-arg (src/Graphics.cpp:670-686)

// Pressed-cell art per cell (src/SentryBotGame.cpp:518-633): image index
// (0 _pressed, 1 _2, 2 _3), rotate mode and offset from (n3, n4).
struct PressedCell { int img, rot, dx, dy; };
constexpr PressedCell kPressed[16] = {
	{ 1, 4, -84, 6 }, { 0, 0, -85, 6 }, { 0, 0, -84, 6 }, { 1, 0, -84, 6 },
	{ 2, 6, -84, 4 }, { 0, 0, -86, 4 }, { 0, 0, -84, 4 }, { 2, 2, -83, 4 },
	{ 2, 0, -84, 3 }, { 0, 0, -86, 3 }, { 0, 0, -84, 3 }, { 2, 4, -83, 3 },
	{ 1, 2, -84, 2 }, { 0, 0, -86, 2 }, { 0, 0, -84, 2 }, { 1, 8, -83, 2 },
};

void text(Ui& ui, const Text* t, int x, int y, int anchor) {
	if (t != nullptr) ui.label(*t, x, y, anchor);
}

// drawCursor (:749-754).
void drawCursor(Ui& ui, const SentryBotAssets& art, int x, int y, bool visible) {
	if (visible) ui.imageRegion(art.game, 95, 91, 22, 22, x + 1, y, 3);
}

// drawPlayersGuess (:729-747). The legacy drawLine is endpoint-inclusive: 13 px.
void drawGuess(Ui& ui, const SentryBotModel& m, const SentryBotAssets& art, int y) {
	ui.imageRegion(art.game, 0, 91, 95, 33, kCenterX - 95, y, 20, 0);
	ui.imageRegion(art.game, 0, 91, 95, 33, kCenterX, y, 20, 4);
	const int ty = y + 16;
	for (int i = 0; i < 4; ++i) {
		const int x = kCenterX - 67 + 45 * i;
		text(ui, m.guess[i], x, ty, 3);
		ui.panel(UiRect{ x - 6, ty + 8, 13, 1 }, 0xFF6C834A);
		if (m.answerCursor == i) drawCursor(ui, art, x, ty, true);
	}
}

// fmButton::Render draws the image top-left at the touch rect (src/Button.cpp:195-201).
void drawButton(Ui& ui, const SentryBotAssets& art, const MiniGameButton& b, uint8_t alpha) {
	ui.imageAlpha(art.button, b.touch.x, b.touch.y, alpha);
}

void centerLabel(Ui& ui, const Text* t, const MiniGameButton& b) {
	text(ui, t, b.touch.x + b.touch.w / 2, b.touch.y + b.touch.h / 2, 3);
}

// drawHelpScreen (:475-487); the fontRenderMode(2) dimming is a no-op.
void drawHelp(Ui& ui, const SentryBotModel& m) {
	drawMiniGameHelp(ui, m.help);
	text(ui, m.exitCode, 445, 302, 3);
}

// drawGameScreen (:489-727).
void drawBoard(Ui& ui, const SentryBotModel& m, const SentryBotAssets& art) {
	const int n = kCenterX, n2 = kBoardY;
	ui.image(art.background, 0, 0, 0);
	ui.imageRegion(art.game, 53, 0, 91, 91, n - 91, n2, 20, 0);
	ui.imageRegion(art.game, 53, 0, 91, 91, n, n2, 20, 4);
	ui.imageRegion(art.game, 53, 0, 91, 91, n - 91, n2 + 91, 20, 6);
	ui.imageRegion(art.game, 53, 0, 91, 91, n, n2 + 91, 20, 2);
	if (m.cursor == -1) ui.imageRegion(art.game, 144, 0, 41, 37, n - 120, n2 + 8, 20, 0);
	for (int i = 0; i < 16; ++i) {
		const int n3 = n + (i % 4) * 42;
		const int n4 = n2 + (i / 4) * 44;
		if (i == m.pressedCell) {
			const PressedCell& p = kPressed[i];
			const Texture& tex = art.pressed[p.img];
			ui.imageRegion(tex, 0, 0, tex.width(), tex.height(), n3 + p.dx, n4 + p.dy, 20, p.rot);
		}
		text(ui, m.digits[i], n3 - 63, n4 + 27, 3);
		if (i == m.cursor) drawCursor(ui, art, n3 - 63, n4 + 26, m.cursorVisible);
	}
	if (m.cursor == -1) drawCursor(ui, art, n - 63 - 21 - 18, n2 + 26, m.cursorVisible);
	drawGuess(ui, m, art, n2 + 199);
	text(ui, m.hint, n, 30, 3);
	text(ui, m.enterCode, n, 241, 3);
	text(ui, m.tapHint, n, 294, 3);
	text(ui, m.back, 35, 310, 3);
	text(ui, m.exit, 445, 310, 3);
	drawButton(ui, art, m.buttons[2], m.submitAlpha);
	drawButton(ui, art, m.buttons[3], m.deleteAlpha);
	centerLabel(ui, m.del, m.buttons[3]);
	centerLabel(ui, m.submit, m.buttons[2]);
}

// drawSuccessScreen, script branch (:373-473).
void drawSuccess(Ui& ui, const SentryBotModel& m, const SentryBotAssets& art) {
	ui.image(art.background, 0, 0, 0);
	drawGuess(ui, m, art, 26);
	text(ui, m.title, kCenterX, 15, 3);
	text(ui, m.select, kCenterX, 95, 3);
	drawButton(ui, art, m.buttons[2], m.submitAlpha);
	drawButton(ui, art, m.buttons[3], m.deleteAlpha);
	text(ui, m.shooting, 60, 140, 3);
	text(ui, m.exploding, 420, 140, 3);
	ui.glyph(kCursorGlyph, m.selCursorX, 140, 10);
	ui.panel(UiRect{ 12, 170, 456, 140 }, 0xFF464C43);
	text(ui, m.shootingHead, 22, 178, 20);
	text(ui, m.shootingBody, 22, 197, 20);
	text(ui, m.explodingHead, 22, 243, 20);
	text(ui, m.explodingBody, 22, 262, 20);
}

// drawFailureScreen (:323-370).
void drawFailure(Ui& ui, const SentryBotModel& m, const SentryBotAssets& art) {
	ui.image(art.background, 0, 0, 0);
	drawGuess(ui, m, art, 26);
	text(ui, m.title, kCenterX, 15, 3);
	text(ui, m.subtitle, kCenterX, 71, 3);
	if (m.warning) {
		ui.panel(UiRect{ 187, 113, 107, 52 }, 0xFF5C000B);
		ui.panel(UiRect{ 190, 116, 101, 46 }, 0xFF343A33);
		ui.panel(UiRect{ 200, 126, 80, 26 }, 0xFF5C000B);
		ui.panel(UiRect{ 207, 133, 66, 12 }, 0xFF343A33);
		text(ui, m.warningLabel, kCenterX, 111, 3);
	}
	text(ui, m.message, kCenterX, m.messageY, 3);
}

} // namespace

UiResult drawSentryBot(Ui& ui, const SentryBotModel& m, const SentryBotAssets& art) {
	switch (m.screen) {
	case SentryBotModel::Screen::Help:    drawHelp(ui, m); break;
	case SentryBotModel::Screen::Board:   drawBoard(ui, m, art); break;
	case SentryBotModel::Screen::Success: drawSuccess(ui, m, art); break;
	case SentryBotModel::Screen::Failure: drawFailure(ui, m, art); break;
	}
	return touchRelease(m.buttons, m.buttonCount, ui.in());
}

} // namespace newcore
