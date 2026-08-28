#ifndef NEW_UI_UI_H
#define NEW_UI_UI_H

#include <cstdint>

#include "ui/UiState.h"
#include "ui/UiTypes.h"

namespace newcore {

class Font;
class Graphics2D;
class Text;
class Texture;
class UiAssets;

// Canvas::drawScrollBar port (src/Canvas.cpp:1284-1315), moved out of
// DialogSystem::drawScrollBar. Exposed as a free function as well as through
// Ui::scrollBar because DialogSystem still draws without a Ui (spec §5 moves
// its call site).
void drawScrollBarCanvas(Graphics2D& g, const Texture& uiImages,
	int x, int y, int h, int topLine, int pageEnd, int numLines, int viewLines);

// Immediate-mode UI façade (ADR 0012): the primitives both draw and report.
// Per-frame borrow of Graphics2D / Font / UiAssets / UiState plus the frame's
// UiInput; owns nothing and allocates nothing per frame (the wrap cache lives
// in UiState). Every legacy constant — line height, visible row counts,
// colours, sheet row heights — is passed in by the caller so the numbers stay
// greppable against the src/ citations.
class Ui {
public:
	struct Env {
		Graphics2D* g = nullptr;
		const Font* font = nullptr;
		const UiAssets* assets = nullptr;
		UiState* state = nullptr;
	};
	void init(const Env& env);

	void beginFrame(const UiInput& in);   // latch input; no drawing
	void endFrame();                      // drop a stale activeId

	const UiInput& in() const { return in_; }
	Graphics2D& g() const { return *env_.g; }
	const Font& font() const { return *env_.font; }
	const UiAssets& art() const { return *env_.assets; }
	UiState& state() const { return *env_.state; }

	// ---- clipping (nested; intersects with the enclosing clip) ----
	static constexpr int kMaxClipDepth = 4;
	void pushClip(const UiRect& r);
	void popClip();

	// ---- hit testing ----
	bool isActive(UiId id) const;         // pressed and still held
	bool hover(const UiRect& r) const;

	// ---- primitives ----
	// borderArgb == 0 means no border; the alpha byte of fillArgb is honoured.
	void panel(const UiRect& r, uint32_t fillColorArgb, uint32_t borderArgb = 0);
	// 1px outline with no fill. Needed because the legacy dialog box fills w
	// but outlines w-1 (src/DialogSystem.cpp:145-146), so the fill and the
	// border cannot share one rect.
	void frame(const UiRect& r, uint32_t borderArgb);
	void image(const Texture& tex, int x, int y, int anchorFlags);
	// Whole-texture blit at a top-left position with a constant alpha, for the
	// dialog page icons: their legacy normalRenderMode is RENDER_BLEND75 and
	// their highlightRenderMode is RENDER_NORMAL (src/Canvas.cpp:329-346).
	void imageAlpha(const Texture& tex, int x, int y, uint8_t alpha);
	void imageRegion(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
		int x, int y, int anchorFlags);
	// Three 10x20 glyphs from the Hud_Numbers sheet (src/Hud.cpp:1127-1152).
	void number3(const Texture& sheet, int x, int y, int space, int value,
		bool slashMode);
	void label(const Text& t, int x, int y, int anchorFlags, int lineH = 16);
	// One explicit <start,len> run of a buffer. The dialog's typewriter needs
	// it: the revealed length changes per frame, so there is no line table to
	// index (src/DialogSystem.cpp:333-354).
	void textRun(const Text& t, int start, int len, int x, int y,
		int anchorFlags, int lineH = 16);
	// Draws rows firstLine .. firstLine+rows-1 of a <start,len> line table.
	// Lines outside [0, lineCount) are skipped, not clamped, so a skipped row
	// leaves its slot blank (the loot loop's `continue`,
	// new_src/core/LootSession.cpp:155-161).
	void textRows(const Text& t, const short* lineIndex, int lineCount,
		int firstLine, int rows, int x, int y, int lineH, int anchorFlags);
	// Wrap-on-demand paragraph for NEW UI only (Deviation D1: migrated screens
	// keep their own verified wrap tables). Returns the total line count.
	int textBlock(UiId cacheId, const Text& src, int x, int y, int widthPx,
		int lineH, int firstLine, int rows, int anchorFlags);
	bool button(UiId id, const UiRect& r, const Texture& normal,
		const Texture& active, int anchorFlags);
	bool buttonRect(UiId id, const UiRect& r);   // invisible hit area
	bool softKey(UiId id, const Text& text, int x, int y, int anchorFlags,
		const UiRect& hit);
	// 0..visibleRows-1 on the release edge inside row i of r, else -1. Draws
	// nothing: the rows are the caller's textRows call.
	int listHit(UiId id, const UiRect& r, int rowH, int visibleRows);
	void scrollBar(int x, int y, int h, int topLine, int pageEnd,
		int numLines, int viewLines);
	// row = 4 - 5*health/maxHealth, clamped at 0 (src/Hud.cpp:695-699).
	void face(const Texture& sheet, int x, int y, int rowH,
		int health, int maxHealth);
	void weaponIcon(const Texture& sheet, int x, int y, int rowH, int weapon);
	void keys(const Texture& sheet, int x, int y, int rowH, int keysRow);

private:
	// The shared press/release rule for button/buttonRect/softKey/listHit.
	bool hitTest(UiId id, const UiRect& r);

	Env env_;
	UiInput in_;
	UiRect clipStack_[kMaxClipDepth];
	int clipDepth_ = 0;
};

} // namespace newcore

#endif // NEW_UI_UI_H
