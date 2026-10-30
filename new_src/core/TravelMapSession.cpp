#include "core/TravelMapSession.h"

#include <algorithm>
#include <vector>

#include "io/Tables.h"
#include "render/api/Texture.h"
#include "ui/UiAssets.h"

namespace newcore {

namespace {

// src/Canvas.h:95-106.
constexpr int kCrossHair[] = { 64, 33, 60, 45, 60, 56, 110, 52, 38, 85, 83, 125, 61, 45, 72, 44, 67, 49 };
constexpr int kLocatorBox[] = { 86, -15, 106, 16, 106, 48, 136, 22, 136, 65, 136, 109, 90, 8, 100, 33, 92, 57 };
constexpr int kUacOnEarth[] = { 82, 48 };
constexpr int kMoon[] = { 137, 216 };
constexpr int kEarth[] = { 123, 150, 113, 140 };   // [0..1] crosshair origin, [2..3] TM_Levels4 (MD5 candidate)
constexpr int kHell[] = { 150, 11 };
constexpr int kMoonName[] = { 124, 255, 165, 64 };
constexpr int kMoonPath[] = { 200, 202 };
constexpr int kEarthName[] = { 126, 160, 40, 226 };
constexpr int kEarthPath[] = { 167, 206 };
constexpr int kHellName[] = { 184, 23 };
constexpr int kHellPath[] = { 137, 52 };

constexpr int kDisplayW = 480;              // displayRect[2]; xDiff = yDiff = 0 (§2.2)
constexpr int kDisplayH = 320;
constexpr int kMsPerPx = 22;                // path reveal speed (:283)
constexpr int kAnchorLeft = 4;              // path labels (:322, :359, :390)

// Strings file 3 (kTextIngame2).
constexpr int kStrEarth = 164;
constexpr int kStrEarthsMoon = 165;
constexpr int kStrHell = 166;

bool onMoon(int n) { return n >= 1 && n <= 3; }   // :446-456
bool onEarth(int n) { return n >= 4 && n <= 6; }
bool inHell(int n) { return n >= 7; }

} // namespace

// Self-check of the :333-337 formula; a sign or precedence slip fails the build.
static_assert(TravelMapSession::yCoordOfSpaceShip(0) == 54, "k 0: 40*15/11");
static_assert(TravelMapSession::yCoordOfSpaceShip(62) == 50, "k 45: -15 + 12 + 40 = 37 -> 555/11");
static_assert(TravelMapSession::yCoordOfSpaceShip(124) == 2, "k 90: -62 + 24 + 40 = 2 -> 30/11");

void TravelMapSession::init(const Env& env) {
	env_ = env;
}

void TravelMapSession::begin(int loadId, int lastId) {
	load_ = loadId;
	last_ = lastId;
	twoStepEarth_ = onEarth(load_) && (onMoon(last_) || inHell(last_));   // :50-59
	const int* origin = onMoon(load_) ? kMoon : onEarth(load_) ? kEarth : kHell;   // :86-99
	if (!twoStepEarth_) {                                                  // :101-109
		crossTX_ = kCrossHair[2 * (load_ - 1)] + origin[0];
		crossTY_ = kCrossHair[2 * (load_ - 1) + 1] + origin[1];
	} else {
		crossTX_ = kUacOnEarth[0] + origin[0];
		crossTY_ = kUacOnEarth[1] + origin[1];
	}
	for (int& v : sv_) v = 0;                                              // setState (src/Canvas.cpp:1024-1026)
	sv_[0] = static_cast<int>(*env_.upTimeMs);
	totalPast_ = 0;
	if (twoStepEarth_) sv_[8] = 1;
}

bool TravelMapSession::samePlanet() const {
	return last_ != load_ && ((onMoon(last_) && onMoon(load_)) ||
	                          (onEarth(last_) && onEarth(load_)) ||
	                          (inHell(last_) && inHell(load_)));
}

int TravelMapSession::levelName(int level) const {
	const std::vector<int16_t>& names = env_.tables->levelNames;
	return level >= 1 && level <= static_cast<int>(names.size()) ? names[level - 1] : -1;
}

void TravelMapSession::closeup(TravelMapModel& m, int level, bool twoStep) const {
	if (onMoon(level)) {
		m.closeup = TmImage::Moon1;
		m.closeupX = kMoon[0];
		m.closeupY = kMoon[1];
	} else if (onEarth(level)) {
		m.closeup = twoStep ? TmImage::Earth2 : TmImage::Earth4;
		m.closeupX = twoStep ? kEarth[0] : kEarth[2];
		m.closeupY = twoStep ? kEarth[1] : kEarth[3];
	} else {
		m.closeup = TmImage::Hell3;
		m.closeupX = kHell[0];
		m.closeupY = kHell[1];
	}
}

bool TravelMapSession::dottedLine(TravelMapModel& m, int time) const {
	const int px = time / kMsPerPx;
	if (last_ == 0 && onMoon(load_)) return marsToMoon(m, px);
	if (onMoon(last_) && onEarth(load_)) return moonToEarth(m, px, false);
	if (onEarth(last_) && onMoon(load_)) return moonToEarth(m, px, true);
	if (onEarth(last_) && inHell(load_)) return earthToHell(m, px, false);
	return !inHell(last_) || !onEarth(load_) || earthToHell(m, px, true);
}

bool TravelMapSession::marsToMoon(TravelMapModel& m, int n) const {
	const Texture& img = env_.art->tmToMoon;
	const Texture& ship = env_.art->tmSpaceShip;
	const int w = img.width();
	const bool done = n > w;
	const int n2 = std::min(n, w);
	if (n > w / 3) {
		m.pathLabelId = kStrEarthsMoon;
		m.pathLabelX = kMoonName[0];
		m.pathLabelY = kMoonName[1];
		m.pathLabelAnchor = kAnchorLeft;
	}
	m.path = TmImage::ToMoon;
	m.pathSrcX = w - n2;
	m.pathSrcY = 0;
	m.pathSrcW = n2;
	m.pathSrcH = img.height();
	m.pathX = kMoonPath[0] + w - n2;
	m.pathY = kMoonPath[1];
	const int u = w - n2;                                      // unrevealed width
	m.ship = true;
	m.shipX = kMoonPath[0] + u - ship.width();
	m.shipY = yCoordOfSpaceShip(u) + kMoonPath[1] - (ship.height() >> 1);
	return done;
}

bool TravelMapSession::moonToEarth(TravelMapModel& m, int n, bool toMoon) const {
	const Texture& img = env_.art->tmToEarth;
	const int h = img.height();
	const bool done = n > h;
	const int n2 = std::min(n, h);
	const int n3 = toMoon ? 0 : h - n2;
	if (n > h / 2) {
		m.pathLabelId = toMoon ? kStrEarthsMoon : kStrEarth;
		m.pathLabelX = toMoon ? kMoonName[2] : kEarthName[0];
		m.pathLabelY = toMoon ? kMoonName[3] : kEarthName[1];
		m.pathLabelAnchor = kAnchorLeft;
	}
	m.path = TmImage::ToEarth;
	m.pathSrcX = 0;
	m.pathSrcY = n3;
	m.pathSrcW = img.width();
	m.pathSrcH = n2;
	m.pathX = kEarthPath[0];
	m.pathY = kEarthPath[1] + n3;
	return done;
}

bool TravelMapSession::earthToHell(TravelMapModel& m, int n, bool toEarth) const {
	const Texture& img = env_.art->tmToHell;
	const int h = img.height();
	const bool done = n > h;
	const int n2 = std::min(n, h);
	const int n3 = toEarth ? 0 : h - n2;
	if (n > 2 * h / 3) {
		m.pathLabelId = toEarth ? kStrEarth : kStrHell;
		m.pathLabelX = toEarth ? kEarthName[2] : kHellName[0];
		m.pathLabelY = toEarth ? kEarthName[3] : kHellName[1];
		m.pathLabelAnchor = kAnchorLeft;
	}
	m.path = TmImage::ToHell;
	m.pathSrcX = 0;
	m.pathSrcY = n3;
	m.pathSrcW = img.width();
	m.pathSrcH = n2;
	m.pathX = kHellPath[0];
	m.pathY = kHellPath[1] + n3;
	return done;
}

void TravelMapSession::locatorBoxAndName(TravelMapModel& m, bool magnifier, int level) const {
	const int* origin = onMoon(level) ? kMoon : onEarth(level) ? kEarth : kHell;
	const int i = 2 * (level - 1);
	const Texture& hl = env_.art->tmHighlight;
	m.box = true;
	m.magnifier = magnifier;
	m.magX = kCrossHair[i] + origin[0];
	m.magY = kCrossHair[i + 1] + origin[1];
	m.highlightX = kLocatorBox[i] + origin[0];
	m.highlightY = kLocatorBox[i + 1] + origin[1] + (hl.height() >> 1);
	m.nameId = levelName(level);
	m.nameX = m.highlightX + (hl.width() >> 1);
	m.nameY = m.highlightY;
}

bool TravelMapSession::locatorLines(TravelMapModel& m, int n, bool same, bool twoStep) const {
	int sx, sy, x, y;
	if (n >= 0) {
		const int step = n / (same ? 15 : 7);
		if (same) {                                          // start at the last level (:532-556)
			const int* origin = onMoon(last_) ? kMoon : onEarth(last_) ? kEarth + (twoStep ? 2 : 0) : kHell;
			sx = kCrossHair[2 * (last_ - 1)] + origin[0];
			sy = kCrossHair[2 * (last_ - 1) + 1] + origin[1];
		} else {                                             // bottom-right corner (:557-560)
			sx = kDisplayW;
			sy = kDisplayH;
		}
		const int dx = crossTX_ > sx ? 1 : (crossTX_ == sx ? 0 : -1);
		const int dy = crossTY_ > sy ? 1 : (crossTY_ == sy ? 0 : -1);
		x = sx + dx * step;
		y = sy + dy * step;
	} else {
		x = sx = crossTX_;
		y = sy = crossTY_;
	}
	bool doneX = false, doneY = false;
	if ((crossTX_ - sx) * (crossTX_ - x) < 0 || crossTX_ == x) {   // :572-579
		x = crossTX_;
		doneX = true;
	}
	if ((crossTY_ - sy) * (crossTY_ - y) < 0 || crossTY_ == y) {
		y = crossTY_;
		doneY = true;
	}
	m.crosshair = true;
	m.crossX = x;
	m.crossY = y;
	return doneX && doneY;
}

void TravelMapSession::buildModel(TravelMapModel& m) {
	m = TravelMapModel{};
	const int now = static_cast<int>(*env_.upTimeMs);
	int time = now - sv_[0];                                   // :167
	m.gridT = time + totalPast_;                               // :168
	const bool same = samePlanet();
	auto endPhase = [&] {                                      // fold the phase time, restart the clock
		totalPast_ += time;
		sv_[0] += time;
	};

	if (sv_[1] != 1) {                                         // phase A (:171-191)
		if (time > (same ? 1500 : 700)) {
			endPhase();
			sv_[1] = 1;
			time = 0;
			if (same) {
				sv_[2] = 1;
				sv_[3] = 1;
			}
		} else if (same) {
			closeup(m, last_, false);
			locatorBoxAndName(m, (time & 0x200) == 0, last_);
		}
	}
	if (sv_[1] != 1) return;

	if (sv_[2] != 1) {                                         // phase B (:194-200)
		if (dottedLine(m, time)) {
			endPhase();
			sv_[2] = 1;
		}
	} else if (sv_[3] != 1) {                                  // phase C (:202-209)
		// Full path, n = 22 * max(screenRect[2], screenRect[3]) (:303-307): the
		// label threshold is passed too, so the label stays up through C.
		dottedLine(m, kMsPerPx * std::max(kDisplayW, kDisplayH));
		if (time > 500 || same) {
			endPhase();
			sv_[3] = 1;
		}
	} else {
		closeup(m, load_, sv_[8] == 1);
		if (sv_[6] == 1) {                                     // phase F (:212-224)
			locatorBoxAndName(m, (time & 0x200) == 0, load_);
			m.prompt = true;
		} else if (sv_[4] != 1) {                              // phase D (:225-231)
			if (locatorLines(m, time, same, sv_[8] == 1)) {
				endPhase();
				sv_[4] = 1;
			}
		} else {                                               // phase E (:232-249)
			locatorLines(m, -1, same, sv_[8] == 1);
			if (time > 800) {
				endPhase();
				if (sv_[8] == 0) {
					sv_[6] = 1;
				} else {                                       // two-step Earth: retarget on TM_Levels4
					sv_[4] = 0;
					sv_[8] = 0;
					crossTX_ = kCrossHair[2 * (load_ - 1)] + kEarth[0];
					crossTY_ = kCrossHair[2 * (load_ - 1) + 1] + kEarth[1];
				}
			}
		}
	}
}

bool TravelMapSession::handleAction(Action a) {
	if (a == Action::Menu) return true;                        // :463-466
	if (a == Action::Use) return sv_[6] == 1;                  // :469-470
	// The FIRE fast-forward branches (:471-507) need hasSavedState(), which is
	// false until a save system exists, and ACTION_AUTOMAP finishes only the
	// dead star-field page (:509-511).
	return false;
}

} // namespace newcore
