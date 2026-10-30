#include "minigames/hacking/HackingView.h"

#include <cstdint>

#include "minigames/common/MiniGameHelpView.h"
#include "minigames/hacking/HackingAssets.h"
#include "render/api/Texture.h"
#include "text/Text.h"
#include "ui/Ui.h"

namespace newcore {

namespace {

constexpr int kColumnStep = 79;         // CORE_WIDTH 35 + 44
constexpr int kExitX = 445, kBackX = 35, kLabelY = 302;
constexpr char kCursorGlyph = '\x8A';   // Graphics::drawCursor (src/Graphics.cpp:674-686)
// drawGoalTextAndBars (src/HackingGame.cpp:512-551).
constexpr uint32_t kBar[6] = {
	0xFFA9E94E, 0xFFE77313, 0xFFFD1100, 0xFFC450FF, 0xFFFFF395, 0xFF101ADE
};

// Legacy drawRect(x, y, w, h) outlines (w+1)x(h+1) (src/Graphics.cpp:236-241),
// so every outline below is the legacy size + 1.

// drawHelpScreen (:359-373). The fontRenderMode(2) dimming is a no-op in the
// original (Canvas::fontRenderMode is never read), so the label is plain.
void drawHelp(Ui& ui, const HackingModel& m) {
	drawMiniGameHelp(ui, m.help);
	if (m.exitLabel != nullptr) ui.label(*m.exitLabel, kExitX, kLabelY, 3);
}

// Yes/No box (:460-503): legacy fillRect is opaque, then drawRect(.., -1).
void drawChoiceBox(Ui& ui, int x, bool held, const Text* label, int labelX) {
	ui.panel(UiRect{ x, 240, 45, 25 }, held ? 0xFF7878FF : 0xFF808080);
	ui.frame(UiRect{ x, 240, 46, 26 }, 0xFFFFFFFF);
	if (label != nullptr) ui.label(*label, labelX, 245, 4);
}

// drawGameScreen (:375-510) with drawGoalTextAndBars and drawGamePieces (:553-582).
void drawGame(Ui& ui, const HackingModel& m, const HackingAssets& art) {
	ui.image(art.background, 0, 0, 0);
	if (m.hoverColumn >= 0) {
		ui.frame(UiRect{ 25 + kColumnStep * m.hoverColumn, 90, 36, 101 }, 0xFFFFFFFF);
	}
	for (int j = 0; j < m.columns; ++j) {
		ui.imageRegion(art.colors, 12 * j, 18, 12, 12, 37 + kColumnStep * j, 195, 0);
	}
	if (m.turns != nullptr) ui.label(*m.turns, 148, 28, 1);

	if (m.goal != nullptr) ui.label(*m.goal, 335, 38, 1);
	const int barX = 335 - (5 + 9 * (m.columns - 1)) / 2;
	for (int i = 0; i < m.columns; ++i) {
		ui.panel(UiRect{ barX + 9 * i, 22, 5, 15 }, kBar[i]);
	}

	if (m.screen == HackingModel::Screen::Game) {
		if (m.prompt != nullptr) ui.label(*m.prompt, 12, 215, 4);
		if (m.backLabel != nullptr) ui.label(*m.backLabel, kBackX, kLabelY, 3);
		if (m.exitLabel != nullptr) ui.label(*m.exitLabel, kExitX, kLabelY, 3);
	} else {
		if (m.prompt != nullptr) ui.label(*m.prompt, 75, 215, 4);
		drawChoiceBox(ui, 142, m.yesHeld, m.yesLabel, 150);
		drawChoiceBox(ui, 258, m.noHeld, m.noLabel, 270);
		ui.glyph(kCursorGlyph, m.cursorX, 245, 8);
	}

	for (int i = 0; i < 5; ++i) {
		for (int j = 0; j < m.columns; ++j) {
			const int k = m.board[i][j];
			if (k != 0) {
				ui.imageRegion(art.colors, 18 * k - 18, 0, 18, 18,
					34 + kColumnStep * j, 94 + 19 * i, 0);
			}
		}
	}
	const int x = 34 + kColumnStep * m.selCol;
	const int y = 94 + 19 * m.selRow;
	ui.frame(UiRect{ x, y, 19, 19 }, m.selColor);
	if (m.carrying) ui.frame(UiRect{ x + 1, y + 1, 17, 17 }, m.selColor);
}

} // namespace

UiResult drawHacking(Ui& ui, const HackingModel& m, const HackingAssets& art) {
	if (m.screen == HackingModel::Screen::Help) drawHelp(ui, m);
	else drawGame(ui, m, art);
	return touchRelease(m.buttons, m.buttonCount, ui.in());
}

} // namespace newcore
