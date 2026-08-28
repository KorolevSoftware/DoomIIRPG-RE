#ifndef NEW_UI_UISTATE_H
#define NEW_UI_UISTATE_H

#include <vector>

#include "text/Text.h"
#include "ui/UiTypes.h"

namespace newcore {

// The ONLY retained UI state (ADR 0012 point 2). Exactly three things live
// here: the press latch, the per-region scroll offsets and the text wrap
// cache. Screen state — current menu section, selected index, loot dwell
// timing — belongs to that screen's session struct, following the LootSession
// precedent (new_src/core/LootSession.h:39-64). Nothing else may be added.
class UiState {
public:
	// Which widget received the press. Without it "released inside" and
	// "released outside" are indistinguishable and the *_Active sheet
	// highlight (docs/original-code/ui.md §1) has nothing to key off.
	UiId activeId = UiId::None;

	// --- per-region scroll offsets, indexed by UiId ---
	int scroll(UiId id) const;
	void setScroll(UiId id, int topLine);

	// --- text wrap cache ---
	// Value uses the shape the loot path already proved: one Text plus
	// <start,len> pairs (CorpseLoot::Pool, new_src/domain/game/CorpseLoot.h:
	// 52-54; filled at new_src/domain/game/CorpseLoot.cpp:186-204, consumed by
	// Graphics2D::drawString's strBeg/strEnd). The wrapped Text is stored with
	// the table because Text::wrapText mutates its buffer (inserts break chars
	// and dehyphenates, new_src/text/Text.cpp:158-212), so the offsets are
	// only valid against the wrapped copy — never against the source.
	// `id` is the requesting region (Ui::textBlock's cacheId): it keeps that
	// parameter load-bearing instead of dead, and lets a region force its own
	// cache slot. The rest of the key is the spec's (&src, len, widthPx, lineH).
	struct WrapKey {
		UiId id = UiId::None;
		const void* src = nullptr;
		int len = 0, widthPx = 0, lineH = 0;
	};
	struct Wrapped {
		Text text;
		std::vector<short> lineIndex;   // <start,len> per line
		int lineCount = 0;
	};

	const Wrapped* lookupWrap(const WrapKey& k) const;
	const Wrapped& putWrap(const WrapKey& k, Wrapped&& w);
	void clearWrapCache();

private:
	// Small FIFO cache; overflow evicts the oldest insertion. A fixed array
	// (not a vector) so references handed out by putWrap can never be
	// invalidated by a later insert.
	static constexpr int kMaxWrapEntries = 8;
	struct WrapEntry {
		WrapKey key;
		Wrapped value;
		bool used = false;
	};

	int scroll_[(int)UiId::Count] = { 0 };
	WrapEntry wrap_[kMaxWrapEntries];
	int wrapNext_ = 0;
};

} // namespace newcore

#endif // NEW_UI_UISTATE_H
