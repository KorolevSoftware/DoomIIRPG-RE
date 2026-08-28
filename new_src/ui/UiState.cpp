#include "ui/UiState.h"

namespace newcore {

int UiState::scroll(UiId id) const {
	const int i = (int)id;
	if (i < 0 || i >= (int)UiId::Count) return 0;
	return scroll_[i];
}

void UiState::setScroll(UiId id, int topLine) {
	const int i = (int)id;
	if (i < 0 || i >= (int)UiId::Count) return;
	scroll_[i] = topLine;
}

const UiState::Wrapped* UiState::lookupWrap(const WrapKey& k) const {
	for (const WrapEntry& e : wrap_) {
		if (!e.used) continue;
		if (e.key.id == k.id && e.key.src == k.src && e.key.len == k.len &&
			e.key.widthPx == k.widthPx && e.key.lineH == k.lineH)
			return &e.value;
	}
	return nullptr;
}

const UiState::Wrapped& UiState::putWrap(const WrapKey& k, Wrapped&& w) {
	WrapEntry& e = wrap_[wrapNext_];
	wrapNext_ = (wrapNext_ + 1) % kMaxWrapEntries;
	e.key = k;
	e.value = std::move(w);
	e.used = true;
	return e.value;
}

void UiState::clearWrapCache() {
	for (WrapEntry& e : wrap_) {
		e = WrapEntry{};
	}
	wrapNext_ = 0;
}

} // namespace newcore
