#include "core/IntroMovieSession.h"

#include <algorithm>
#include <cstdio>

#include "core/Sound.h"
#include "io/Localization.h"
#include "io/Tables.h"

namespace newcore {

namespace {

constexpr int kFadeMs = 1500;           // canvas->fadeDuration (:791)
constexpr int kStrCrawl = 133;          // 0:133 "Mysterious occurrences ..."
constexpr int kCrawlWrap = (480 - 8) / 11;   // CHAR_SPACING[0] = 11, a character count (:180)
// initScrollingText(0, 133, false, 32, 1, 800) (:173-219): 1600 ms per 32 px,
// end = 1600 * (lines + 320/32 - 2) with lines = 9 + 1 for the shipped text.
constexpr int kCrawlSpacing = 32;
constexpr int kCrawlMsPerSpacing = 800 * ((kCrawlSpacing << 16) >> 4) >> 16;   // 1600
constexpr int kCrawlExtraLines = 1;

} // namespace

// Game::loadTableCamera(14, 15) (src/Game.cpp:511-574): [numKeys, sampleRate,
// posShiftRaw, angleRaw], 7*n keys channel-major, 6*n tween indices, then the
// per-channel tween counts (5 read by the loader).
void IntroMovieSession::init(const Env& env) {
	env_ = env;
	const std::vector<int16_t>& t = env.tables->tableCamera;
	const int n = t.size() >= 4 ? t[0] : 0;
	const size_t need = 4 + 13 * static_cast<size_t>(std::max(n, 0)) + 5;
	if (n <= 0 || t.size() < need) {
		std::fprintf(stderr, "[intro] table camera 14 malformed (%zu shorts) - pan skipped\n", t.size());
		return;
	}
	MapData::MayaCamera c;
	c.numKeys = n;
	c.sampleRate = t[1];
	c.keys.assign(t.begin() + 4, t.begin() + 4 + 7 * n);
	c.tweenIndices.assign(t.begin() + 4 + 7 * n, t.begin() + 4 + 13 * n);
	int total = 0;
	for (int ch = 0; ch < 5; ++ch) {
		c.tweenCounts[ch] = t[4 + 13 * n + ch];
		total += c.tweenCounts[ch];
	}
	c.tweens = env.tables->tableCameraTweens;
	if (static_cast<int>(c.tweens.size()) != total) {
		std::fprintf(stderr, "[intro] table camera 15 has %zu bytes, counts sum %d - pan skipped\n",
			c.tweens.size(), total);
		return;
	}
	posShift_ = 4 - t[2];
	camMap_.totalMayaCameraKeys = n;
	camMap_.mayaCameras.assign(1, c);
	camOk_ = cam_.setup(camMap_, 0, MayaPose{});
}

void IntroMovieSession::begin() {
	phase_ = 0;
	key_ = -1;
	fadeStart_ = -1;
	crawl_.setLength(0);
	crawl_.append(env_.loc->get(kTextMain, kStrCrawl));
	crawl_.wrapText(kCrawlWrap);                     // soft hyphens break and dehyphenate (src/Text.cpp:756-827)
	// Music_General, looped (:780). The original re-requests it every frame;
	// priority 6 makes repeats no-ops, so one call at begin is equivalent.
	Sound::instance().playSound(1068, 1, 6);
}

int IntroMovieSession::keyMs(int k) const {
	const MapData::MayaCamera& c = camMap_.mayaCameras[0];
	return c.keys[c.numKeys * 6 + k] & 0xFFFF;       // MS channel
}

void IntroMovieSession::tick() {
	const int64_t now = *env_.upTimeMs;
	switch (phase_) {
	case 0:                                          // (:779-793)
		fadeStart_ = now;
		phase_ = 1;
		break;
	case 1: {                                        // (:795-837)
		if (!camOk_) { phase_ = 2; break; }
		const int last = camMap_.mayaCameras[0].numKeys - 1;
		if (key_ < 0) {
			// Update(-1) -> Snap(-1): key 0 static, then NextKey (src/MayaCamera.cpp:72-75, :371-373).
			cam_.snap(0);
			key_ = 0;
			keyStart_ = now;
		} else if (key_ == last) {
			phase_ = 2;                              // complete: this frame keeps the last pose (:836-837)
		} else if (now - keyStart_ >= keyMs(key_)) {
			cam_.snap(key_ + 1);                     // Snap(k) + NextKey; the overshoot is dropped
			++key_;
			keyStart_ = now;
		} else {
			cam_.update(key_, static_cast<int>(now - keyStart_));
		}
		break;
	}
	case 2:                                          // (:837-841)
		crawlStart_ = now;
		phase_ = 3;
		break;
	default: {                                       // (:842-847)
		const int lines = crawl_.getNumLines() + kCrawlExtraLines;
		const int end = kCrawlMsPerSpacing * (lines + (320 / kCrawlSpacing - 2));
		if (now - crawlStart_ > end) {
			crawlStart_ = now - end;                 // hold the end position
			if (env_.exit) env_.exit();
		}
		break;
	}
	}
}

void IntroMovieSession::buildModel(IntroMovieModel& m) const {
	m = IntroMovieModel{};
	const int64_t now = *env_.upTimeMs;
	m.started = phase_ >= 1;
	if (phase_ >= 3) {
		const int t = static_cast<int>(now - crawlStart_);
		m.crawl = true;
		m.text = &crawl_;
		m.textOffset = (((t << 8) / kCrawlMsPerSpacing) * (kCrawlSpacing << 8)) >> 16;
	} else {
		m.camX = cam_.pose().x >> posShift_;
		m.camY = cam_.pose().y >> posShift_;
	}
	// Canvas fade-in (src/Canvas.cpp:499-509): fill alpha 1 - a/255, a = (t << 8) / 1500.
	if (fadeStart_ >= 0 && now - fadeStart_ < kFadeMs) {
		m.blackAlpha = 255 - std::min(255, static_cast<int>(((now - fadeStart_) << 8) / kFadeMs));
	}
}

} // namespace newcore
