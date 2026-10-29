#include "minigames/common/MiniGameHelpView.h"

#include "render/api/Texture.h"
#include "text/Text.h"
#include "ui/Ui.h"

namespace newcore {

namespace {

// screenRect = {0,0,480,320} (docs/original-code/cutscenes-camera.md:408).
constexpr int kCenterX = 240;          // (screenRect[2] - screenRect[0]) / 2
constexpr int kTitleY = 25;            // screenRect[1] + 0x19
constexpr int kBodyX = 18;             // screenRect[0] + 0x12
constexpr int kBodyY = 45;             // screenRect[1] + 0x2d
constexpr int kBarX = 462;             // screenRect[2] - 0x12
constexpr int kBarH = 256;             // 0x100
constexpr int kViewLines = 16;         // 0x10
constexpr int kFooterY = 300;          // screenRect[3] - 0x14
constexpr int kAnchorCenter = 3;
constexpr int kAnchorTopLeft = 20;     // 0x14

} // namespace

void drawMiniGameHelp(Ui& ui, const MiniGameHelpModel& m) {
	if (m.background != nullptr) ui.image(*m.background, 0, 0, 0);
	if (m.title != nullptr) ui.label(*m.title, kCenterX, kTitleY, kAnchorCenter);
	if (m.body != nullptr) ui.label(*m.body, kBodyX, kBodyY, kAnchorTopLeft);
	ui.scrollBar(kBarX, kBodyY, kBarH, m.firstLine, m.pageEnd, m.numLines, kViewLines);
	if (m.footer != nullptr) ui.label(*m.footer, kCenterX, kFooterY, kAnchorCenter);
}

} // namespace newcore
