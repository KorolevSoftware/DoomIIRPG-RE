#include "domain/game/DialogSystem.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "core/GameContext.h"
#include "domain/game/Game.h"
#include "domain/game/ScriptVM.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "render/Graphics2D.h"
#include "text/Font.h"
#include "ui/Hud.h"

namespace newcore {

namespace {

constexpr int kCanvasW = 480;
constexpr int kCanvasH = 320;
constexpr int kScrCx = 240;                 // Canvas::SCR_CX
constexpr int kLineH = 16;                  // drawString line height (src/Graphics.cpp:554)
constexpr int kTypewriterMsPerChar = 25;    // (src/DialogSystem.cpp:339)
constexpr int kHudTopPinnedY = 20;          // hudRect[1]+20; hudRect[1]=screenRect[1]=0 (src/Canvas.cpp:139-140)

// Style fills (switch at src/DialogSystem.cpp:137-214).
constexpr uint32_t kColorWhite = 0xFFFFFFFF;
constexpr uint32_t kHeaderGray = 0xFF666666;      // default color2 (:139)
constexpr uint32_t kPlayerDlgColor = 0xFF005617;  // Canvas::PLAYER_DLG_COLOR (src/Canvas.h:112)

// Scrollbar colors (src/Canvas.cpp:1307-1311, negated legacy literals).
constexpr uint32_t kScrollTrack = 0xFFB3AA93;     // -5002605
constexpr uint32_t kScrollThumb = 0xFFE7CFAD;     // -1585235

void fillArgb(Graphics2D& g, int x, int y, int w, int h, uint32_t argb) {
	g.fillRect(x, y, w, h, (uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb);
}

void rectArgb(Graphics2D& g, int x, int y, int w, int h, uint32_t argb) {
	g.drawRect(x, y, w, h, (uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb);
}

// Per-channel brightness scaling of an ARGB color, brightness 0..256
// (gradient row math at src/DialogSystem.cpp:286-287; the legacy packed
// expression is reproduced here per-channel — same visual ramp).
uint32_t scaleColor(uint32_t argb, int brightness) {
	uint32_t r = (((argb >> 16) & 0xFFu) * (uint32_t)brightness) >> 8;
	uint32_t gr = (((argb >> 8) & 0xFFu) * (uint32_t)brightness) >> 8;
	uint32_t b = ((argb & 0xFFu) * (uint32_t)brightness) >> 8;
	return 0xFF000000u | (r << 16) | (gr << 8) | b;
}

} // namespace

// ---- setup ----

void DialogSystem::init(const Env& env) {
	env_ = env;
}

// ---- text args (%NN pool, src/Text.cpp:222-275) ----

void DialogSystem::resetTextArgs() {
	numTextArgs_ = 0;
	dynamicArgs_.clear();
}

void DialogSystem::addTextArg(const std::string& arg) {
	if (numTextArgs_ + 1 >= kMaxTextArgs) {
		// Legacy fatals with "Added too many String Args"; survive instead.
		std::fprintf(stderr, "[dialog] ERR too many string args\n");
		return;
	}
	dynamicArgs_ += arg;
	argIndex_[numTextArgs_++] = (int)dynamicArgs_.size();
}

void DialogSystem::addTextArg(int value) {
	addTextArg(std::to_string(value));
}

// composeText decode: '\\' escapes (\% -> %, \n -> newline), %NN -> arg NN-1
// (src/Text.cpp:281-326).
void DialogSystem::composeText(int type, int idx, Text& out) const {
	std::string raw = env_.loc->get(type, idx);
	for (size_t i = 0; i < raw.size(); ++i) {
		char c = raw[i];
		if (c == '\\') {
			if (i + 1 < raw.size()) {
				char d = raw[++i];
				if (d == '%') out.append('%');
				else if (d == 'n') out.append('\n');
				else { out.append('\\'); out.append(d); }
			} else {
				out.append('\\');
			}
		} else if (c == '%') {
			if (i + 2 < raw.size() &&
				raw[i + 1] >= '0' && raw[i + 1] <= '9' &&
				raw[i + 2] >= '0' && raw[i + 2] <= '9') {
				int n6 = ((raw[i + 1] - '0') * 10 + (raw[i + 2] - '0')) - 1; // first arg is %01
				i += 2;
				if (n6 >= 0 && n6 < numTextArgs_) {
					int beg = (n6 > 0) ? argIndex_[n6 - 1] : 0;
					for (int k = beg; k < argIndex_[n6]; ++k) out.append(dynamicArgs_[(size_t)k]);
				}
			} else {
				out.append('%');
			}
		} else {
			out.append(c);
		}
	}
}

// ---- lifecycle ----

void DialogSystem::startDialog(ScriptThread* thread, int textType, int strIdx,
	int style, int flags, bool resumeScript) {
	resumeScriptAfterClosed_ = resumeScript;
	thread_ = thread;
	Text large;
	composeText(textType, strIdx, large);        // src/DialogSystem.cpp:727-728
	prepareDialog(large, style, flags);
	env_.ctx->setState(StateId::Dialog);         // (:746)
}

void DialogSystem::prepareDialog(Text& text, int style, int flags) {
	// Lines per page (src/DialogSystem.cpp:607-618).
	if (style == 3) viewLines_ = 4;
	else if (style == 8) viewLines_ = 3;
	else if (style == 2) viewLines_ = 3;
	else viewLines_ = 4;

	// NPC facing look-at (619-630): player->facingEntity has no counterpart yet.
	buffer_.setLength(0);
	if ((flags & 0x4) != 0 || (flags & 0x1) != 0) {           // (637-647)
		env_.vm->vars[4] = (style == 12) ? 0 : 1;
		scratch_.setLength(0);
		composeText(kTextMain, 50, scratch_);                 // common "||" spacer
		text.append(scratch_);
	}

	// Wrap at dialogMaxChars, retry narrower when lines overflow the page
	// (648-691). Style 8 reserves 3 chars of portrait inset on every line
	// (n3 = len("   "), :651-653).
	int maxChars = kDialogMaxChars;
	int narrowChars = kWithBarMaxChars;
	if (style == 8) {
		maxChars -= 3;
		narrowChars -= 3;
	}
	buffer_.append(text);
	buffer_.wrapText(maxChars);
	int numLines = buffer_.getNumLines();
	if (style == 2 || style == 16 || style == 9) --numLines;  // title excluded (682-685)
	if (numLines > viewLines_) {
		buffer_.setLength(0);
		buffer_.append(text);
		buffer_.wrapText(narrowChars);
	}

	// Line table: split the buffer at '|' into <start,len> pairs (692-705).
	int length2 = buffer_.length();
	numDialogLines_ = 0;
	int n = 0;
	for (int i = 0; i < length2; ++i) {
		if (buffer_.charAt(i) == '|') {
			if (numDialogLines_ < kMaxDialogLines) {
				dialogIndexes_[numDialogLines_ * 2] = (int16_t)n;
				dialogIndexes_[numDialogLines_ * 2 + 1] = (int16_t)(i - n);
				++numDialogLines_;
			}
			n = i + 1;
		}
	}
	dialogIndexes_[numDialogLines_ * 2] = (int16_t)n;
	dialogIndexes_[numDialogLines_ * 2 + 1] = (int16_t)(length2 - n);
	++numDialogLines_;

	currentDialogLine_ = 0;
	lineStartTimeMs_ = env_.ctx->upTimeMs;
	typeLineIdx_ = 0;
	flags_ = flags;
	style_ = style;
	// dialogItem reset (:710): loot popups arrive with spec GROUP 3.

	if (style == 2) std::fprintf(stderr, "[dialog] sound 1027\n");  // (715-717)
}

void DialogSystem::closeDialog(bool skip) {
	closing_ = true;
	buffer_.setLength(0);
	// player->unpause is an empty stub in legacy too (:527, src/Player.cpp:1419-1423).

	// queueAdvanceTurn rule (src/DialogSystem.cpp:530-532).
	if (numHelpMessages_ == 0 &&
		(style_ == 3 || style_ == 4 ||
		 (style_ == 2 && type_ == 1) ||
		 (style_ == 12 && env_.vm->vars[4] == 1 && !skip))) {
		env_.game->queueAdvanceTurn = true;
	}
	if (style_ == 11 && (flags_ & 0x2) != 0) {
		// VIOS malloc/anger bookkeeping (:533-540): fields arrive with that system.
		std::fprintf(stderr, "[dialog] VIOS choice var4=%d (no consumer)\n", env_.vm->vars[4]);
	}

	// Prior-state restore (:541-554): INTER_CAMERA > CAMERA(active) > COMBAT >
	// ST_PLAYING priority. The camera/combat states land with spec GROUP 2;
	// ST_PLAYING is the only reachable prior state this phase.
	env_.ctx->setState(StateId::Playing);
	closing_ = false;

	if (resumeScriptAfterClosed_) {              // (559-563)
		env_.game->skipDialog = skip;
		env_.vm->resumeThread(thread_);
		env_.game->skipDialog = false;
	}
	// Styles 12/13 purchases (:564-597): familiar/armor systems out of scope.
}

// ---- help FIFO ----

bool DialogSystem::enqueueHelpDialog(int textType, int strIdx, int threadIdx) {
	// enableHelp gate (:847): EV_ENABLE_HELP has no rewrite counterpart yet —
	// treated as enabled. Dying-state gate n/a (no death dialogs in subset).
	if (numHelpMessages_ == kMaxHelpMessages) {
		std::fprintf(stderr, "[dialog] ERR_MAXHELP (41)\n");
		return false;
	}
	helpTypes_[numHelpMessages_] = 2;
	helpIds_[numHelpMessages_] = (textType << 10) | strIdx;
	helpThreads_[numHelpMessages_] = threadIdx;
	++numHelpMessages_;
	if (env_.ctx->state() == StateId::Playing) dequeueHelpDialog();  // (859-861)
	return true;
}

void DialogSystem::dequeueHelpDialog(bool force) {
	if (numHelpMessages_ == 0) return;
	if (env_.ctx->state() == StateId::Dialog || closing_) return;    // (760-762)
	// INTER_CAMERA also passes in legacy (:763-765); it arrives with GROUP 2.
	if (!force && env_.ctx->state() != StateId::Playing) return;
	// secretActive guard (:766-768): secrets not implemented.

	type_ = helpTypes_[0];
	int id = helpIds_[0];
	int threadIdx = helpThreads_[0];
	if (type_ != 2) {
		// Types 1 (item pickup composition; producers arrive with GROUP 3
		// loot) and >=2 (raw Text) have no producers in this phase — drop.
		for (int i = 0; i < kMaxHelpMessages - 1; ++i) {
			helpTypes_[i] = helpTypes_[i + 1];
			helpIds_[i] = helpIds_[i + 1];
			helpThreads_[i] = helpThreads_[i + 1];
		}
		--numHelpMessages_;
		return;
	}
	// FIFO shift (820-827).
	for (int i = 0; i < kMaxHelpMessages - 1; ++i) {
		helpTypes_[i] = helpTypes_[i + 1];
		helpIds_[i] = helpIds_[i + 1];
		helpThreads_[i] = helpThreads_[i + 1];
	}
	--numHelpMessages_;
	// enableHelp gate (:828): treated as enabled (see enqueueHelpDialog).
	startDialog(threadIdx >= 0 ? env_.vm->threadAt(threadIdx) : nullptr,
		id >> 10, id & 0x3FF, 2, 0, threadIdx != -1);            // (829-834)
}

// ---- input (src/DialogSystem.cpp:29-112) ----

void DialogSystem::handleInput(Action action) {
	int64_t now = env_.ctx->upTimeMs;
	switch (action) {
	case Action::Use: {                          // ACTION_FIRE (34-49)
		if (typeLineIdx_ < viewLines_ && typeLineIdx_ < numDialogLines_ - currentDialogLine_) {
			typeLineIdx_ = viewLines_;           // finish the typewriter reveal
		} else if (currentDialogLine_ < numDialogLines_ - viewLines_) {
			lineStartTimeMs_ = now;
			typeLineIdx_ = 0;
			currentDialogLine_ += viewLines_;    // next page
			if (((flags_ & 0x4) != 0 || (flags_ & 0x1) != 0) &&
				currentDialogLine_ + viewLines_ > numDialogLines_) {
				currentDialogLine_ = numDialogLines_ - viewLines_; // keep last page full
			}
		} else {
			closeDialog(false);
		}
		break;
	}
	case Action::Forward: {                      // ACTION_UP (50-67)
		short& var4 = env_.vm->vars[4];
		if (currentDialogLine_ >= numDialogLines_ - viewLines_ && (flags_ & 0x2) != 0) {
			if (var4 == 0) {
				if (--currentDialogLine_ < 0) currentDialogLine_ = 0;
			} else {
				--var4;
			}
		} else if (--currentDialogLine_ < 0) {
			currentDialogLine_ = 0;
		}
		break;
	}
	case Action::Back: {                         // ACTION_DOWN (69-89)
		short& var4 = env_.vm->vars[4];
		if (currentDialogLine_ >= numDialogLines_ - viewLines_ && (flags_ & 0x2) != 0) {
			if (var4 < 1) ++var4;
		} else {
			++currentDialogLine_;
			if (currentDialogLine_ > numDialogLines_ - viewLines_) {
				currentDialogLine_ = numDialogLines_ - viewLines_;
				if ((flags_ & 0x2) == 0 && currentDialogLine_ < 0) currentDialogLine_ = 0;
			} else {
				lineStartTimeMs_ = now;          // newly shown line types out (86-88)
				typeLineIdx_ = viewLines_ - 1;
			}
		}
		break;
	}
	case Action::TurnLeft:                       // ACTION_LEFT (+91-93, 97-102)
	case Action::TurnRight: {                    // ACTION_RIGHT (+91-93, 103-107)
		if ((flags_ & 0x5) != 0 && currentDialogLine_ >= numDialogLines_ - viewLines_) {
			env_.vm->vars[4] ^= 0x1;             // choice flip
			break;
		}
		if (action == Action::TurnLeft) {
			currentDialogLine_ -= viewLines_;
			if (currentDialogLine_ < 0) currentDialogLine_ = 0;
		} else {
			currentDialogLine_ += viewLines_;
			int last = numDialogLines_ - viewLines_;
			if (currentDialogLine_ > last) currentDialogLine_ = std::max(last, 0);
		}
		break;
	}
	case Action::Menu: {                         // ACTION_MENU page-back (97-102)
		currentDialogLine_ -= viewLines_;
		if (currentDialogLine_ < 0) currentDialogLine_ = 0;
		break;
	}
	case Action::Passturn:                       // skip-close (94-96)
	case Action::Automap:
		closeDialog(true);
		break;
	case Action::BackKey:
		break;                                   // swallowed (src/InputEventController.cpp:167-184)
	default:
		break;                                   // no movement actions exist here
	}
	// Tail: dequeue the next queued help popup once back in play (109-111).
	if (env_.ctx->state() == StateId::Playing && env_.game->monstersTurn == 0) {
		dequeueHelpDialog();
	}
}

// ---- rendering (src/DialogSystem.cpp:114-518) ----

void DialogSystem::draw(Graphics2D& g) {
	if (env_.font == nullptr || !env_.font->valid() ||
		env_.hud == nullptr || buffer_.length() == 0) return;

	const Texture& uiImages = env_.hud->imgUIImages();

	int rx = 0;                                  // -screenRect[0], screen origin 0
	int rw = kCanvasW;                           // hudRect[2]
	int rh = viewLines_ * kLineH + 8;            // (:133)
	int ry = kCanvasH - rh - 1;                  // (:134)
	int textX = rx + 1;                          // (:136)
	uint32_t fill = 0xFF000000;
	uint32_t border = kColorWhite;
	uint32_t headerCol = kHeaderGray;
	bool greenText = false;

	switch (style_) {
	case 3:                                      // scroll-log layout (141-148)
		ry -= 10;
		fillArgb(g, rx, ry - 10, rw, rh + 20, 0x003200);   // translucent fill 12800
		rectArgb(g, rx, ry - 10, rw - 1, rh + 19, border);
		break;
	case 16: headerCol = 0xFF000066; break;      // (150-153): blue HEADER strip, body stays black
	case 4:                                      // loot popup (154-161)
		fill = (flags_ & 0x1) != 0 ? 0xFFB18A01u : 0xFF005A00u;
		break;
	case 11:                                     // VIOS terminal (162-169)
		fill = 0xFF800000;
		if ((flags_ & 0x2) != 0) ry = kHudTopPinnedY;
		break;
	case 5:                                      // NPC bubble (170-177)
		fill = 0xFF800000;
		if ((flags_ & 0x2) != 0) ry = kHudTopPinnedY;
		break;
	case 8:                                      // hero speech (178-182)
		ry -= 64;
		fill = kPlayerDlgColor;
		break;
	case 14:                                     // (183-191) falls into the navy label
		ry -= 20;
		[[fallthrough]];
	case 1: case 6:                              // (187-190) comm-link navy
		fill = 0xFF002864;
		break;
	case 9:                                      // terminal/log (192-196)
		fill = 0xFF000000;
		headerCol = 0xFF000000;
		greenText = true;
		break;
	case 10:                                     // (197-201)
		fill = 0xFF2E0854;
		ry = kHudTopPinnedY;
		break;
	case 12: case 13:                            // choice boxes (202-209)
		fill = 0xFFB18A01;
		break;
	case 15:
		fill = 0xFFFF9600;
		break;
	default:
		break;
	}

	int headerLines = 0;
	if (style_ == 2 || style_ == 16 || style_ == 9) {
		// Title-bar layout (230-253): 18px header strip above the box with the
		// first '|'-line centered in it as the speaker/title.
		headerLines = 1;
		fillArgb(g, rx, ry, rw, rh, fill);
		fillArgb(g, rx, ry - 18, rw, 18, headerCol);
		rectArgb(g, rx, ry - 18, rw - 1, 18, border);
		rectArgb(g, rx, ry, rw - 1, rh, border);
		drawTitle(g, rx + kScrCx, ry - 16, greenText);
	} else if (style_ == 4) {
		// Item-pickup box (254-274). The dialogItem name bar (259-269) needs
		// the loot composer — spec GROUP 3.
		fillArgb(g, rx, ry, rw, rh, fill);
		rectArgb(g, rx, ry, rw - 1, rh, border);
	} else if (style_ != 3) {
		fillArgb(g, rx, ry, rw, rh, fill);
		rectArgb(g, rx, ry, rw - 1, rh, border);
		if (style_ == 8) {
			// Vertical gradient rows (281-289).
			int y0 = ry + 1;
			int y1 = y0 + (rh - 1);
			for (int y = y0 + 1; y < y1; ++y) {
				int b = 96 + ((((256 - (((y - y0) << 8) / (y1 - y0))) * 160)) >> 8);
				uint32_t c = scaleColor(fill, b);
				g.drawLine(rx + 1, y, rx + (rw - 2), y,
					(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
			}
			if (uiImages.valid()) {
				g.drawRegion(uiImages, 30, 0, 15, 9, kScrCx + 10, ry + rh, 0);   // corner icon (290)
			}
			// Hero portrait from Hud_Portrait_Small.bmp (imgPortraitsSM, 20x60,
			// height/3 rows; row = characterChoice-1, :291-308). The rewrite has
			// no character selection, so choice is fixed to the first marine =
			// row 0 (spec §9).
			const Texture& portraits = env_.hud->imgPortraitsSmall();
			if (portraits.valid() && portraits.height() / 3 > 0) {
				g.drawRegion(portraits, 0, 0, portraits.width(), portraits.height() / 3,
					rx + 2, ry + 3, 0);
				textX += portraits.width() + 2;  // text starts after portrait (309-310)
			} else {
				// Documented fallback: colored header strip in lieu of a portrait.
				fillArgb(g, rx, ry - 12, rw, 12, fill);
				rectArgb(g, rx, ry - 12, rw - 1, 12, border);
			}
		} else if (style_ == 5) {
			// Speech-bubble tails (312-319).
			if (uiImages.valid()) {
				if ((flags_ & 0x2) != 0) {
					g.drawRegion(uiImages, 0, 12, 10, 6, kScrCx - 64, ry + rh + 6,
						Graphics2D::kAnchorBottom | Graphics2D::kAnchorLeft);
				} else {
					g.drawRegion(uiImages, 0, 0, 10, 6, kScrCx - 64, ry + 1,
						Graphics2D::kAnchorBottom | Graphics2D::kAnchorLeft);
				}
			}
		} else if (style_ == 1) {
			// Tail arrow above the box (320-322).
			if (uiImages.valid()) {
				g.drawRegion(uiImages, 10, 0, 10, 6, kScrCx - 64, ry + 1,
					Graphics2D::kAnchorBottom | Graphics2D::kAnchorLeft);
			}
		} else if (style_ == 10) {
			if (uiImages.valid()) g.drawRegion(uiImages, 20, 6, 10, 6, kScrCx + 10, ry + rh, 0);
		} else if (style_ == 14) {
			if (uiImages.valid()) g.drawRegion(uiImages, 45, 0, 15, 9, kScrCx + 10, ry + rh, 0);
		}
	}
	if (currentDialogLine_ < headerLines) currentDialogLine_ = headerLines;  // (330-332)

	// Text lines loop with typewriter reveal (333-354).
	const Font& font = *env_.font;
	int ty = ry + 2;
	for (int i = 0; i < viewLines_ && currentDialogLine_ + i < numDialogLines_; ++i) {
		int line = currentDialogLine_ + i;
		int start = dialogIndexes_[line * 2];
		int len = dialogIndexes_[line * 2 + 1];
		int visible = 0;
		if (i == typeLineIdx_) {
			visible = (int)((env_.ctx->upTimeMs - lineStartTimeMs_) / kTypewriterMsPerChar);
			if (visible >= len) {
				visible = len;
				++typeLineIdx_;
				lineStartTimeMs_ = env_.ctx->upTimeMs;
			}
		} else if (i < typeLineIdx_) {
			visible = len;
		}
		if (visible > 0) {
			if (greenText) {
				// Style 9 draws green via the '^2' color code
				// (currentCharColor=2, src/DialogSystem.cpp:249-251,349-351).
				scratch_.setLength(0);
				scratch_.append('^');
				scratch_.append('2');
				scratch_.append(buffer_, start, visible);
				g.drawString(font, scratch_, textX, ty, Graphics2D::kAnchorLeft, kLineH, 0, visible + 2);
			} else {
				g.drawString(font, buffer_, textX, ty, Graphics2D::kAnchorLeft, kLineH, start, visible);
			}
		}
		ty += kLineH;
	}

	// var4 Yes/No widgets (flags&2 vertical pair, flags&4/&1 bottom pair,
	// src/DialogSystem.cpp:355-452): deferred per spec §8 — the map00 intro
	// chain uses only flagless styles 1/8. var4 cursor logic is live in
	// handleInput.

	// Scrollbar + page icons (454-489).
	if (numDialogLines_ <= viewLines_) {
		if (!(flags_ & 0x7) && env_.hud->imgPageOk().valid()) {
			g.drawRegion(env_.hud->imgPageOk(), 0, 0, 72, 72, 390 + 9, 110 + 9, 0);
		}
	} else {
		int pageEnd = std::min(currentDialogLine_ + viewLines_, numDialogLines_);
		drawScrollBar(g, rx + rw - 1, ry + 2, rh - 4,
			currentDialogLine_ - headerLines, pageEnd - headerLines,
			numDialogLines_ - headerLines, viewLines_);
		if (uiImages.valid()) {
			if (numDialogLines_ - headerLines > viewLines_) {
				if (currentDialogLine_ > 1 && env_.hud->imgPageUp().valid()) {
					g.drawRegion(env_.hud->imgPageUp(), 0, 0, 72, 72, 390 + 9, 20 + 9, 0);
				}
				if (currentDialogLine_ < numDialogLines_ - viewLines_) {
					if (env_.hud->imgPageDown().valid()) {
						g.drawRegion(env_.hud->imgPageDown(), 0, 0, 72, 72, 390 + 9, 110 + 9, 0);
					}
				} else if (env_.hud->imgPageOk().valid()) {
					g.drawRegion(env_.hud->imgPageOk(), 0, 0, 72, 72, 390 + 9, 110 + 9, 0);
				}
			} else if (env_.hud->imgPageOk().valid()) {
				g.drawRegion(env_.hud->imgPageOk(), 0, 0, 72, 72, 390 + 9, 110 + 9, 0);
			}
		}
	}
}

// Title line (dialogIndexes[0..1]) centered in the header strip
// (src/DialogSystem.cpp:252).
void DialogSystem::drawTitle(Graphics2D& g, int cx, int y, bool greenText) {
	int start = dialogIndexes_[0];
	int len = dialogIndexes_[1];
	if (len <= 0) return;
	if (greenText) {
		scratch_.setLength(0);
		scratch_.append('^');
		scratch_.append('2');
		scratch_.append(buffer_, start, len);
		g.drawString(*env_.font, scratch_, cx, y, Graphics2D::kAnchorHCenter, kLineH, 0, len + 2);
	} else {
		g.drawString(*env_.font, buffer_, cx, y, Graphics2D::kAnchorHCenter, kLineH, start, len);
	}
}

// Canvas::drawScrollBar analog (src/Canvas.cpp:1284-1315).
void DialogSystem::drawScrollBar(Graphics2D& g, int x, int y, int h,
	int topLine, int pageEnd, int numLines, int viewLines) const {
	const Texture& uiImages = env_.hud->imgUIImages();
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

} // namespace newcore
