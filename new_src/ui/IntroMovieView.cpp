#include "ui/IntroMovieView.h"

#include <algorithm>
#include <cstdint>

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

constexpr int kScrCx = 240;                       // Canvas::SCR_CX
constexpr int kScrCy = 160;                       // Canvas::SCR_CY
constexpr UiRect kScreen{ 0, 0, 480, 320 };
constexpr UiRect kBand{ 0, 50, 480, 220 };        // (displayH - 220) / 2 (:796-798)
constexpr int kCrawlSrcY = 65;                    // drawRegion(prolog, 0, 65, 480, 307) at (0,0) (:229-235)
constexpr uint32_t kCrawlDarken = 0x3F000000;     // fade(rect, 192, 0): black at alpha 63 (src/Graphics.cpp:755-767)
constexpr int kCrawlTopY = kBand.y + 250;         // cinRect[3] = 250 in band space (:243)
constexpr int kCrawlLineH = 32;
constexpr int kTopLeft = Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft;

} // namespace

UiResult drawIntroMovie(Ui& ui, const IntroMovieModel& m) {
	UiResult res;
	const UiAssets& art = ui.art();
	ui.panel(kScreen, 0xFF000000);
	if (m.started && !m.crawl) {
		// drawRegion(prolog, texX, texY, ...) at (0, dstY) == the whole image at
		// (-texX, SCR_CY - camY): dest x never goes positive (:806-835).
		ui.pushClip(kBand);
		ui.image(art.prolog, -std::max(0, m.camX - kScrCx), kScrCy - m.camY, kTopLeft);
		ui.popClip();
	} else if (m.crawl) {
		ui.pushClip(kBand);
		ui.image(art.prolog, 0, -kCrawlSrcY, kTopLeft);
		ui.popClip();
		ui.panel(kScreen, kCrawlDarken);
		if (m.text != nullptr) {
			ui.pushClip(kBand);
			ui.label(*m.text, kScrCx, kCrawlTopY - m.textOffset, Graphics2D::kAnchorHCenter, kCrawlLineH);
			ui.popClip();
		}
	}
	if (m.blackAlpha > 0) ui.panel(kScreen, (uint32_t)m.blackAlpha << 24);
	if (ui.in().released) res.action = UiAction::ScreenTouch;
	return res;
}

UiResult drawCredits(Ui& ui, const CreditsModel& m) {
	UiResult res;
	ui.panel(kScreen, 0xFF000000);                   // eraseRgn(displayRect); no prolog, fade or clip (:229-238)
	if (m.text != nullptr) ui.label(*m.text, kScrCx, m.textY, Graphics2D::kAnchorHCenter, 16);   // (:243)
	// (SCR_CX - 24, screenRect[3] - 32), anchor 2 = VCENTER (:195-203, src/Graphics.h:13-19).
	if (m.continueLabel != nullptr) ui.label(*m.continueLabel, kScrCx - 24, 320 - 32, Graphics2D::kAnchorVCenter);
	if (ui.in().released) res.action = UiAction::Activate;
	return res;
}

} // namespace newcore
