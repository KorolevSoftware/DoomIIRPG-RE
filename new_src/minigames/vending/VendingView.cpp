#include "minigames/vending/VendingView.h"

#include <cstdint>

#include "minigames/common/MiniGameHelpView.h"
#include "minigames/vending/VendingAssets.h"
#include "render/Graphics2D.h"
#include "render/api/Texture.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

constexpr int kTitleX = 241;            // SCR_CX + 1 (src/VendingMachine.cpp:474-480)
constexpr int kTitleY = 14;
constexpr int kCreditsY = 30;           // 14 + 16 (:481)
constexpr int kPromptX = 240;
constexpr int kPromptY = 60;            // (:490)
constexpr int kHelpExitX = 445;         // (:628)
constexpr int kHelpExitY = 302;
constexpr char kCursorGlyph = '\x8A';   // Graphics::drawCursor (src/Graphics.cpp:670-686)
// Small buttons: renderMode 1 = RENDER_BLEND25 = glColor4f(1,1,1,0.25)
// (src/Image.cpp:172-176) unless pressed, then RENDER_NORMAL
// (src/VendingMachine.cpp:521-558).
constexpr uint8_t kButtonAlphaIdle = 64;
constexpr uint8_t kButtonAlphaHeld = 255;
// fmButton normalRenderMode 12 = glColor4f(1,1,1,0.75) (src/Image.cpp:195-198).
constexpr uint8_t kButtonAlphaRender12 = 192;
constexpr int kGameTop = 0;             // screenRect[1]
constexpr int kScrCx = 240;             // Canvas::SCR_CX
constexpr int kScrCy = 160;             // Canvas::SCR_CY

void drawTerminal(Ui& ui, const VendingModel& m, const VendingAssets& art) {
	ui.image(ui.art().vendingBackground, 0, 0, 0);   // drawVendingMachineBackground (:605)
	if (m.title != nullptr) ui.label(*m.title, kTitleX, kTitleY, 1);
	if (m.credits != nullptr) ui.label(*m.credits, kTitleX, kCreditsY, 17);
	if (m.prompt != nullptr) ui.label(*m.prompt, kPromptX, kPromptY, 17);
	const int halfW = art.buttonSmall.width() / 2;
	for (int i = 0; i < 2; ++i) {
		ui.imageAlpha(art.buttonSmall, m.buttonX[i], m.buttonTop,
			m.buttonHeld[i] ? kButtonAlphaHeld : kButtonAlphaIdle);
		if (m.buttonLabel[i] != nullptr) {
			ui.label(*m.buttonLabel[i], m.buttonX[i] + halfW, m.buttonLabelY, 3);
		}
	}
	ui.glyph(kCursorGlyph, m.cursorX, m.cursorY, 24);
	ui.image(m.softkeyPressed ? ui.art().vendingSoftkeyPressed : ui.art().vendingSoftkeyNormal,
		m.softkeyX, m.softkeyY, 0);
	if (m.exitLabel != nullptr) ui.label(*m.exitLabel, m.softkeyLabelX, m.softkeyLabelY, 3);
}

// drawGameScreen (:634-686) with drawGameTopBar (:688-731), drawGameMiddleBar
// (:733-803) and drawGameResults (:421-467). The canvas soft keys it sets
// (:647-658) are not drawn: the labels at (35/445, 302) are the visible ones.
void drawGame(Ui& ui, const VendingModel& m, const VendingAssets& art) {
	Graphics2D& g = ui.g();
	Text t;
	auto drawText = [&](const char* s, int x, int y) {
		t.setLength(0);
		t.append(s);
		ui.label(t, x, y, 3);
	};
	auto drawInt = [&](int v, int x, int y) {
		t.setLength(0);
		t.append(v);
		ui.label(t, x, y, 3);
	};

	ui.image(art.gameBackground, 0, 0, 0);

	// Top bar: five 33x29 boxes, the four guesses + "=" correctSum.
	drawText("Current Sum", 240, 9);
	const int gap = m.boxStep - 33;
	for (int i = 0; i < 5; ++i) {
		const int x = i < 4 ? m.columns[i].x : m.sumBoxX;
		g.drawRegion(art.vendingGame, 38, 37, 33, 29, x, kGameTop + 17, 0);
		if (i < 4) {
			if (m.topValues[i] != -1) drawInt(m.topValues[i], x + 14, kGameTop + 31);
		} else {
			drawInt(m.correctSum, x + 14, kGameTop + 31);
		}
		if (i > 0) drawText(i == 4 ? "=" : "+", x - gap / 2 - 1, kGameTop + 31);
	}
	drawInt(m.correctSum, 300, 280);
	drawText("Hack Goal Sum =", 200, 280);

	// Middle bar: per column the drag outline, the "too low" chevrons from the
	// bottom up (flipped), the "too high" ones from the top down, the slider.
	for (int i = 0; i < 4; ++i) {
		const VendingModel::Column& c = m.columns[i];
		if (c.dragged) ui.frame(UiRect{ c.x + 5, kGameTop + 104, 22, 110 }, 0xFFFFFF00);
		const int posX = c.x + 5;
		int posY = kGameTop + 203;
		for (int j = 0; j < c.minimum; ++j) {
			g.drawRegion(art.vendingGame, 22, 97 - c.chevronOffset, 22, 11, posX, posY,
				Graphics2D::kAnchorLeft | Graphics2D::kAnchorTop, 6);
			posY -= 11;
		}
		posY = kGameTop + 104;
		for (int k = 9; k > c.maximum; --k) {
			g.drawRegion(art.vendingGame, 22, 97 - c.chevronOffset, 22, 11, posX, posY,
				Graphics2D::kAnchorLeft | Graphics2D::kAnchorTop, 0);
			posY += 11;
		}
		posY = kGameTop + 109 + (9 - c.slider) * 99 / 9;
		if (c.selected) {
			ui.frame(UiRect{ posX, posY - 5, 21, 10 }, m.pulseColor);
			ui.frame(UiRect{ posX + 1, posY - 5 + 1, 21 - 2, 10 - 2 }, m.pulseColor);
		} else {
			ui.frame(UiRect{ posX, posY - 5, 21, 10 }, 0xFFFFFFFF);
		}
		drawInt(c.slider, c.x + 15, posY + 1);
	}

	if (m.result != nullptr) {
		const int sw = m.result->getStringWidth();
		const int n = m.result->getNumLines() * 16;
		ui.panel(UiRect{ kScrCx - sw / 2 - 6, kScrCy - n / 2 - 4, sw + 12, n + 8 },
			0xFF000000, 0xFFFFFFFF);
		ui.label(*m.result, kScrCx, kScrCy, 3);
	}
	if (m.prompt != nullptr) ui.label(*m.prompt, 240, 320 - 12, 3);
	if (m.exitLabel != nullptr) ui.label(*m.exitLabel, 445, 302, 3);
	if (m.backLabel != nullptr) ui.label(*m.backLabel, 35, 302, 3);

	// m_vendingButtons->Render (src/Button.cpp:161-235): normal art at
	// renderMode 12, the pressed art plain while highlighted; 9/10 have none.
	const int held = highlightedButtonId(m.buttons, m.buttonCount, ui.in());
	for (int i = 0; i < m.buttonCount; ++i) {
		const MiniGameButton& b = m.buttons[i];
		const Texture* normal = nullptr;
		const Texture* pressed = nullptr;
		if (b.id == 0) {
			normal = &art.submit;
			pressed = &art.submitPressed;
		} else if (b.id >= 1 && b.id <= 8) {
			normal = (b.id % 2 == 1) ? &art.arrowUp : &art.arrowDown;
			pressed = (b.id % 2 == 1) ? &art.arrowUpPressed : &art.arrowDownPressed;
		} else {
			continue;
		}
		if (b.id == held) ui.image(*pressed, b.touch.x, b.touch.y, 0);
		else ui.imageAlpha(*normal, b.touch.x, b.touch.y, kButtonAlphaRender12);
	}
	drawText("OK", art.submit.width() / 2 + 398, 160);
}

// drawHelpScreen (:620-632). The fontRenderMode(2) dimming is a no-op in the
// original (Canvas::fontRenderMode is never read), so the label is plain.
void drawHelp(Ui& ui, const VendingModel& m) {
	drawMiniGameHelp(ui, m.help);
	if (m.exitLabel != nullptr) ui.label(*m.exitLabel, kHelpExitX, kHelpExitY, 3);
}

} // namespace

UiResult drawVending(Ui& ui, const VendingModel& m, const VendingAssets& art) {
	switch (m.screen) {
	case VendingModel::Screen::Terminal: drawTerminal(ui, m, art); break;
	case VendingModel::Screen::Help:     drawHelp(ui, m); break;
	case VendingModel::Screen::Game:     drawGame(ui, m, art); break;
	}
	return touchRelease(m.buttons, m.buttonCount, ui.in());
}

} // namespace newcore
