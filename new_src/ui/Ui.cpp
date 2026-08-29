#include "ui/Ui.h"

#include <algorithm>
#include <cstdio>

#include "render/Graphics2D.h"
#include "render/gl/Texture.h"
#include "text/Font.h"
#include "text/Text.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

// Scrollbar colours, moved with drawScrollBar out of DialogSystem.cpp:33-34.
constexpr uint32_t kScrollTrack = 0xFFB3AA93;     // -5002605
constexpr uint32_t kScrollThumb = 0xFFE7CFAD;     // -1585235

constexpr int kNumberGlyphW = 10;                 // Hud_Numbers cell (src/Hud.cpp:1143)
constexpr int kNumberGlyphH = 20;

void fillArgb(Graphics2D& g, int x, int y, int w, int h, uint32_t argb) {
	g.fillRect(x, y, w, h, (uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb,
		(uint8_t)(argb >> 24));
}

void rectArgb(Graphics2D& g, int x, int y, int w, int h, uint32_t argb) {
	g.drawRect(x, y, w, h, (uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb,
		(uint8_t)(argb >> 24));
}

// Legacy Hud::drawWeapon texY table (docs/original-code/ui.md §3).
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

} // namespace

// Verbatim move of DialogSystem::drawScrollBar (was
// new_src/domain/game/DialogSystem.cpp:579-595), itself a port of
// src/Canvas.cpp:1284-1315 — including the numLines == pageEnd special case
// and the 7x7 ui_images caps at (60,0)/(60,7). The only change is that the
// sheet arrives as an argument instead of via env_.hud->imgUIImages().
void drawScrollBarCanvas(Graphics2D& g, const Texture& uiImages,
	int x, int y, int h, int topLine, int pageEnd, int numLines, int viewLines) {
	if (viewLines >= numLines) return;
	int scrollRange = std::max(numLines - viewLines, topLine);
	int thumbH = 3 * h / (4 * ((viewLines + numLines - 1) / viewLines));
	int thumbY = ((topLine << 16) / (scrollRange << 8) * ((h - thumbH - 14) << 8)) >> 16;
	if (numLines == pageEnd) thumbY = h - 3 * h / (4 * ((viewLines + numLines - 1) / viewLines)) - 14;
	if (!uiImages.valid()) return;
	g.drawRegion(uiImages, 60, 0, 7, 7, x, y, Graphics2D::kAnchorTop | Graphics2D::kAnchorRight);
	g.drawRegion(uiImages, 60, 7, 7, 7, x, y + h, Graphics2D::kAnchorBottom | Graphics2D::kAnchorRight);
	fillArgb(g, x - 7, y + 7, 7, h - 14, kScrollTrack);
	fillArgb(g, x - 7, thumbY + 7 + y, 7, thumbH, kScrollThumb);
	rectArgb(g, x - 7, thumbY + 7 + y, 6, thumbH - 1, 0xFF000000);
	rectArgb(g, x - 7, y, 6, h - 1, 0xFF000000);
}

void Ui::init(const Env& env) {
	env_ = env;
	in_ = UiInput{};
	clipDepth_ = 0;
	glyphBuf_.reserve(2);
}

void Ui::beginFrame(const UiInput& in) {
	in_ = in;
	clipDepth_ = 0;
}

void Ui::endFrame() {
	// A press that ended off-canvas (or on focus loss) must not leave a widget
	// latched and highlighted forever.
	if (!in_.down) state().activeId = UiId::None;
}

void Ui::pushClip(const UiRect& r) {
	if (clipDepth_ >= kMaxClipDepth) {
		static bool warned = false;
		if (!warned) {
			warned = true;
			std::fprintf(stderr, "Ui::pushClip: depth > %d, ignored\n", kMaxClipDepth);
		}
		return;
	}
	const UiRect eff = (clipDepth_ > 0) ? clipStack_[clipDepth_ - 1].intersect(r) : r;
	clipStack_[clipDepth_++] = eff;
	g().setClip(eff.x, eff.y, eff.w, eff.h);
}

void Ui::popClip() {
	if (clipDepth_ <= 0) return;
	--clipDepth_;
	if (clipDepth_ > 0) {
		const UiRect& p = clipStack_[clipDepth_ - 1];
		g().setClip(p.x, p.y, p.w, p.h);
	} else {
		g().clearClip();
	}
}

bool Ui::isActive(UiId id) const {
	return env_.state->activeId == id && in_.down;
}

void Ui::clearActive() {
	state().activeId = UiId::None;
}

bool Ui::hover(const UiRect& r) const {
	return in_.cursorValid && r.contains(in_.cursorX, in_.cursorY);
}

// Press latched on the down edge inside the rect, fired on the release edge
// only if the cursor is still inside (released outside = cancel).
bool Ui::hitTest(UiId id, const UiRect& r) {
	UiState& st = state();
	const bool inside = hover(r);
	if (in_.pressed && inside && st.activeId == UiId::None) st.activeId = id;
	bool fired = false;
	if (in_.released && st.activeId == id) {
		fired = inside;
		st.activeId = UiId::None;
	}
	return fired;
}

void Ui::panel(const UiRect& r, uint32_t fillColorArgb, uint32_t borderArgb) {
	fillArgb(g(), r.x, r.y, r.w, r.h, fillColorArgb);
	if (borderArgb != 0) rectArgb(g(), r.x, r.y, r.w, r.h, borderArgb);
}

void Ui::frame(const UiRect& r, uint32_t borderArgb) {
	rectArgb(g(), r.x, r.y, r.w, r.h, borderArgb);
}

void Ui::image(const Texture& tex, int x, int y, int anchorFlags) {
	g().drawImage(tex, x, y, anchorFlags);
}

void Ui::imageAlpha(const Texture& tex, int x, int y, uint8_t alpha) {
	g().drawImage(tex, 0, 0, tex.width(), tex.height(), x, y, tex.width(),
		tex.height(), 0, 255, 255, 255, alpha);
}

void Ui::imageRegion(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
	int x, int y, int anchorFlags) {
	g().drawRegion(tex, srcX, srcY, srcW, srcH, x, y, anchorFlags);
}

// Port of Hud::drawNumbers (src/Hud.cpp:1127-1152): always three glyphs, no
// leading-zero suppression. slashMode is the weapon-13 rule — d1 = -1 selects
// row 20*(9-(-1)) = 200, the '/' glyph of the 11-row sheet, so it reads "N/5".
void Ui::number3(const Texture& sheet, int x, int y, int space, int value,
	bool slashMode) {
	if (!sheet.valid() || value >= 1000) return;
	int d0 = value / 100;
	int d1 = value % 100 / 10;
	int d2 = value % 100 % 10;
	if (slashMode) {
		d0 = value % 100 % 10;
		d1 = -1;
		d2 = 5;
	}
	Graphics2D& gr = g();
	gr.drawRegion(sheet, 0, kNumberGlyphH * (9 - d0), kNumberGlyphW, kNumberGlyphH,
		x, y, Graphics2D::kAnchorTop);
	gr.drawRegion(sheet, 0, kNumberGlyphH * (9 - d1), kNumberGlyphW, kNumberGlyphH,
		x + space + 10, y, Graphics2D::kAnchorTop);
	gr.drawRegion(sheet, 0, kNumberGlyphH * (9 - d2), kNumberGlyphW, kNumberGlyphH,
		x + 2 * space + 20, y, Graphics2D::kAnchorTop);
}

void Ui::label(const Text& t, int x, int y, int anchorFlags, int lineH) {
	g().drawString(font(), t, x, y, anchorFlags, lineH);
}

void Ui::glyph(char c, int x, int y, int anchorFlags) {
	glyphBuf_.setLength(0);
	glyphBuf_.append(c);
	g().drawString(font(), glyphBuf_, x, y, anchorFlags);
}

void Ui::textRun(const Text& t, int start, int len, int x, int y,
	int anchorFlags, int lineH) {
	g().drawString(font(), t, x, y, anchorFlags, lineH, start, len);
}

void Ui::textRows(const Text& t, const short* lineIndex, int lineCount,
	int firstLine, int rows, int x, int y, int lineH, int anchorFlags) {
	if (lineIndex == nullptr) return;
	for (int i = 0; i < rows; ++i) {
		const int line = firstLine + i;
		if (line < 0 || line >= lineCount) continue;
		g().drawString(font(), t, x, y + i * lineH, anchorFlags, lineH,
			lineIndex[line * 2], lineIndex[line * 2 + 1]);
	}
}

int Ui::textBlock(UiId cacheId, const Text& src, int x, int y, int widthPx,
	int lineH, int firstLine, int rows, int anchorFlags) {
	const UiState::WrapKey key{ cacheId, &src, src.length(), widthPx, lineH };
	const UiState::Wrapped* w = state().lookupWrap(key);
	if (w == nullptr) {
		UiState::Wrapped built;
		built.text = src;
		// Char budget: the font advance is a fixed 9 px (Font::kAdvance). The
		// legacy per-style budgets ((W-2)/9, (W-9)/9,
		// new_src/domain/game/DialogSystem.h:36-38) stay in the caller's
		// widthPx.
		built.text.wrapText(widthPx / Font::kAdvance);
		// Split at '|' into <start,len> pairs, the tail forming the last line —
		// the same loop as new_src/domain/game/CorpseLoot.cpp:188-204, minus
		// the fixed 9-line cap.
		const int length = built.text.length();
		int start = 0;
		for (int i = 0; i < length; ++i) {
			if (built.text.charAt(i) != '|') continue;
			built.lineIndex.push_back((short)start);
			built.lineIndex.push_back((short)(i - start));
			start = i + 1;
		}
		built.lineIndex.push_back((short)start);
		built.lineIndex.push_back((short)(length - start));
		built.lineCount = (int)built.lineIndex.size() / 2;
		w = &state().putWrap(key, std::move(built));
	}
	textRows(w->text, w->lineIndex.data(), w->lineCount, firstLine, rows,
		x, y, lineH, anchorFlags);
	return w->lineCount;
}

bool Ui::button(UiId id, const UiRect& r, const Texture& normal,
	const Texture& active, int anchorFlags) {
	// Highlighting only swaps the sheet, never the position
	// (docs/original-code/ui.md §1).
	const Texture& tex = isActive(id) ? active : normal;
	imageRegion(tex, 0, 0, r.w, r.h, r.x, r.y, anchorFlags);
	return hitTest(id, r);
}

bool Ui::buttonRect(UiId id, const UiRect& r) {
	return hitTest(id, r);
}

bool Ui::softKey(UiId id, const Text& text, int x, int y, int anchorFlags,
	const UiRect& hit, bool interactive) {
	label(text, x, y, anchorFlags);
	if (!interactive) return false;
	return hitTest(id, hit);
}

int Ui::listHit(UiId id, const UiRect& r, int rowH, int visibleRows) {
	if (rowH <= 0) return -1;
	if (!hitTest(id, r)) return -1;
	const int row = (in_.cursorY - r.y) / rowH;
	if (row < 0 || row >= visibleRows) return -1;
	return row;
}

void Ui::scrollBar(int x, int y, int h, int topLine, int pageEnd,
	int numLines, int viewLines) {
	drawScrollBarCanvas(g(), art().uiImages, x, y, h, topLine, pageEnd,
		numLines, viewLines);
}

// fmScrollButton::Render, image branch (src/Button.cpp:574-588). The track and
// the sliders get two different x values because the source centres the track
// by its own width and the sliders by imgBarTop's (20 vs 24).
void Ui::scrollBarMenu(const UiRect& barRect, int thumbOffset, int thumbLen) {
	const Texture& bar = art().menuScrollBar;
	const Texture& top = art().menuSliderTop;
	const Texture& mid = art().menuSliderMid;
	const Texture& bottom = art().menuSliderBottom;
	if (!bar.valid() || !top.valid() || !mid.valid() || !bottom.valid()) return;
	if (mid.height() <= 0) return;

	const int flags = Graphics2D::kAnchorTop | Graphics2D::kAnchorLeft;
	const int xBar = barRect.x + ((barRect.w - bar.width()) >> 1);
	const int xSlider = barRect.x + ((barRect.w - top.width()) >> 1);
	const int yThumb = barRect.y + thumbOffset;
	image(bar, xBar, barRect.y, flags);
	image(top, xSlider, yThumb, flags);
	for (int y = yThumb + top.height(); y < yThumb + thumbLen - bottom.height();
		y += mid.height()) {
		image(mid, xSlider, y, flags);
	}
	image(bottom, xSlider, yThumb + thumbLen - bottom.height(), flags);
}

void Ui::face(const Texture& sheet, int x, int y, int rowH,
	int health, int maxHealth) {
	if (!sheet.valid()) return;
	int row = 4 - 5 * health / (maxHealth ? maxHealth : 1);
	if (row < 0) row = 0;
	imageRegion(sheet, 0, rowH * row, sheet.width(), rowH, x, y,
		Graphics2D::kAnchorTop);
}

void Ui::weaponIcon(const Texture& sheet, int x, int y, int rowH, int weapon) {
	if (!sheet.valid()) return;
	imageRegion(sheet, 0, weaponTexY(weapon) * rowH, sheet.width(), rowH, x, y,
		Graphics2D::kAnchorTop);
}

void Ui::keys(const Texture& sheet, int x, int y, int rowH, int keysRow) {
	if (!sheet.valid()) return;
	imageRegion(sheet, 0, rowH * keysRow, sheet.width(), rowH, x, y,
		Graphics2D::kAnchorTop);
}

} // namespace newcore
