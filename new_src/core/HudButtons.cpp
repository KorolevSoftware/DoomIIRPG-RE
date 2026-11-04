#include "core/HudButtons.h"

#include "ui/HudModel.h"
#include "ui/Ui.h"

namespace newcore {

namespace {
constexpr int64_t kLongPressMs = 300;   // src/Hud.cpp:1329, :1370
} // namespace

void HudButtons::trackPointer(const UiInput& in) {
	if (in.pressed) {
		pressX_ = in.cursorX;
		pressY_ = in.cursorY;
		dragged_ = false;
	} else if (in.down || in.released) {
		// pointInRectangle(x, y, x0-3, y0-3, 6, 6) (src/TouchController.cpp:131-134,
		// src/Utils.cpp:52-54).
		const UiRect box{ pressX_ - 3, pressY_ - 3, 6, 6 };
		if (!box.contains(in.cursorX, in.cursorY)) dragged_ = true;
	}
}

UiResult HudButtons::resolve(UiResult r, const Ui& ui, const HudModel& m, const Frame& f) {
	if (!m.interactive || m.familiar) { open_ = false; pressTime_ = 0; }   // HB-D1
	if (f.blockInput) return {};                                          // src/Hud.cpp:1238-1239
	if (ui.in().pressed && ui.isActive(UiId::HudWeapon)) pressTime_ = f.upTimeMs;   // :1291-1295
	// Long-press poll of Hud::update (src/Hud.cpp:1365-1378); hover stands in for
	// legacy `highlighted`, re-evaluated under the finger (:1214-1235). HB-D2.
	if (pressTime_ != 0 && ui.isActive(UiId::HudWeapon) && ui.hover(m.weaponHit) &&
	    f.upTimeMs - pressTime_ >= kLongPressMs) {
		if (!f.combat && !f.zoomed) open_ = true;
		pressTime_ = 0;
	}
	if (r.action == UiAction::None) return r;   // release on empty space keeps the overlay
	// The familiar branch has no `touched` test (src/Hud.cpp:1241-1266 vs :1276).
	if (dragged_ && !m.familiar) return {};
	// No combat gate: src/Hud.cpp:1281-1288 precedes :1303.
	if (r.action == UiAction::WeaponSelect) { open_ = false; return r; }
	if (r.action != UiAction::WeaponTap) open_ = false;                   // :1299-1301
	if (f.combat) return {};                                              // :1303
	if (r.action == UiAction::WeaponTap) {
		if (f.zoomed) return r;                      // :1325-1327 -> unzoom in PlayerActions
		const bool quick = pressTime_ != 0 && f.upTimeMs - pressTime_ < kLongPressMs;   // :1329
		pressTime_ = 0;
		if (!quick) return {};                       // long press already handled / expired
		if (open_) { open_ = false; return {}; }     // :1333-1335 tap closes the overlay
		return r;                                    // :1337
	}
	return r;
}

} // namespace newcore
