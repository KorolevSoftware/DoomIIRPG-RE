#include "ui/TravelMapView.h"

#include <cstdint>

#include "render/Graphics2D.h"
#include "text/Text.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

constexpr int kCanvasW = 480;
constexpr int kCanvasH = 320;
constexpr int kScrCx = 240;                 // Canvas::SCR_CX
constexpr int kGridStep = 44;               // drawGridLines (src/TravelMapManager.cpp:432-444)
constexpr uint8_t kBlend50 = 128;           // renderMode 2 = RENDER_BLEND50
constexpr uint32_t kShadow = 0xFF000000;    // drawLocatorLines (:580-583)
constexpr uint32_t kCrossColor = 0xFFBDFD80;
constexpr int kTopLeft = Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft;
constexpr int kCentered = Graphics2D::kAnchorHCenter | Graphics2D::kAnchorVCenter;   // 3
constexpr int kVCenterLeft = Graphics2D::kAnchorVCenter | Graphics2D::kAnchorLeft;  // 6
constexpr int kHCenterTop = Graphics2D::kAnchorHCenter | Graphics2D::kAnchorTop;    // 17

const Texture* imageOf(const UiAssets& art, TmImage id) {
	switch (id) {
	case TmImage::Moon1:   return &art.tmLevels1;
	case TmImage::Earth2:  return &art.tmLevels2;
	case TmImage::Earth4:  return &art.tmLevels4;
	case TmImage::Hell3:   return &art.tmLevels3;
	case TmImage::ToMoon:  return &art.tmToMoon;
	case TmImage::ToEarth: return &art.tmToEarth;
	case TmImage::ToHell:  return &art.tmToHell;
	case TmImage::None:    break;
	}
	return nullptr;
}

} // namespace

UiResult drawTravelMap(Ui& ui, const TravelMapModel& m) {
	const UiAssets& art = ui.art();
	ui.image(art.tmBackground, kScrCx, 0, kHCenterTop);                   // :156

	// Grid (:432-444): vertical strips scroll right 1 px / 200 ms, period 44.
	for (int x = (m.gridT / 200) % kGridStep; x < kCanvasW; x += kGridStep)
		ui.imageAlpha(art.tmGridVert, x, 0, kBlend50);
	for (int y = 5; y < kCanvasH; y += kGridStep) {
		ui.imageAlpha(art.tmGridHorz, 0, y, kBlend50);
		ui.imageAlpha(art.tmGridHorz, 240, y, kBlend50);
	}

	if (const Texture* t = imageOf(art, m.closeup)) ui.image(*t, m.closeupX, m.closeupY, kTopLeft);
	if (m.pathLabelText != nullptr) ui.label(*m.pathLabelText, m.pathLabelX, m.pathLabelY, m.pathLabelAnchor);
	if (const Texture* t = imageOf(art, m.path)) {
		ui.imageRegion(*t, m.pathSrcX, m.pathSrcY, m.pathSrcW, m.pathSrcH, m.pathX, m.pathY, kTopLeft);
	}
	if (m.ship) ui.image(art.tmSpaceShip, m.shipX, m.shipY, kTopLeft);
	if (m.crosshair) {
		ui.panel(UiRect{ m.crossX + 1, 0, 1, kCanvasH + 1 }, kShadow);
		ui.panel(UiRect{ 0, m.crossY + 1, kCanvasW + 1, 1 }, kShadow);
		ui.panel(UiRect{ m.crossX, 0, 1, kCanvasH + 1 }, kCrossColor);
		ui.panel(UiRect{ 0, m.crossY, kCanvasW + 1, 1 }, kCrossColor);
	}
	if (m.box) {                                                           // :401-430
		if (m.magnifier) ui.image(art.tmMagGlass, m.magX, m.magY, kCentered);
		ui.image(art.tmHighlight, m.highlightX, m.highlightY, kVCenterLeft);
		if (m.nameText != nullptr) ui.label(*m.nameText, m.nameX, m.nameY, kCentered);
	}
	if (m.prompt && m.promptText != nullptr) ui.label(*m.promptText, kScrCx, kCanvasH - 21, kHCenterTop);

	UiResult res;
	if (ui.in().released) res.action = UiAction::Activate;
	return res;
}

} // namespace newcore
