#include "domain/game/DialogSystem.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "core/GameContext.h"
#include "domain/game/Game.h"
#include "domain/game/ScriptVM.h"
#include "io/Localization.h"
#include "io/Tables.h"
#include "text/Font.h"
#include "ui/Hud.h"
#include "ui/Ui.h"

namespace newcore {

namespace {

constexpr int kTypewriterMsPerChar = 25;    // (src/DialogSystem.cpp:339)

// The box geometry, the style fills and the gradient/scale helpers moved with
// the drawing into new_src/ui/DialogView.cpp.

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

// ---- view model (state half of src/DialogSystem.cpp:114-518) ----

// The box, the title, the portrait inset, the rows and the page icons are
// drawn by drawDialog (new_src/ui/DialogView.cpp). What stays here is the
// state this frame resolves: the header clamp, the typewriter reveal counts,
// the paging-derived scrollbar values and which page icon is up.
bool DialogSystem::buildViewModel(DialogViewModel& m) {
	if (env_.font == nullptr || !env_.font->valid() ||
		env_.hud == nullptr || buffer_.length() == 0) return false;

	m.style = style_;
	m.flags = flags_;
	m.viewLines = viewLines_;
	m.text = &buffer_;
	m.scratch = &scratch_;

	// Header-strip styles reserve line 0 as the speaker/title (230-253); the
	// title itself is drawn by the view from dialogIndexes[0..1].
	int headerLines = 0;
	if (style_ == 2 || style_ == 16 || style_ == 9) {
		headerLines = 1;
		m.titleStart = dialogIndexes_[0];
		m.titleLen = dialogIndexes_[1];
	}
	if (currentDialogLine_ < headerLines) currentDialogLine_ = headerLines;  // (330-332)

	// Text lines loop with typewriter reveal (333-354). Only the reveal count
	// per row is computed; the row's screen slot is the view's business.
	// The kMaxViewLines term is a pure bounds guard on m.rows: prepareDialog
	// sets viewLines_ to 3 or 4 only (:128-131).
	m.rowCount = 0;
	for (int i = 0; i < viewLines_ && i < DialogViewModel::kMaxViewLines &&
		currentDialogLine_ + i < numDialogLines_; ++i) {
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
		m.rows[i].start = start;
		m.rows[i].visible = visible;
		m.rowCount = i + 1;
	}

	// Scrollbar + page icons (454-489).
	if (numDialogLines_ <= viewLines_) {
		m.showPageOk = !(flags_ & 0x7);
	} else {
		m.showScrollBar = true;
		int pageEnd = std::min(currentDialogLine_ + viewLines_, numDialogLines_);
		m.scrollTop = currentDialogLine_ - headerLines;
		m.scrollPageEnd = pageEnd - headerLines;
		m.scrollNumLines = numDialogLines_ - headerLines;
		if (numDialogLines_ - headerLines > viewLines_) {
			m.showPageUp = currentDialogLine_ > 1;
			if (currentDialogLine_ < numDialogLines_ - viewLines_) {
				m.showPageDown = true;
			} else {
				m.showPageOk = true;
			}
		} else {
			m.showPageOk = true;
		}
	}

	// Touch-only rule (src/TouchController.cpp:379-392): on the last page of a
	// choice dialog the ok icon and the box body swallow the tap.
	m.activateFires = !(currentDialogLine_ >= numDialogLines_ - viewLines_ &&
		(flags_ & 0x7) != 0);
	return true;
}

// The Canvas::drawScrollBar port itself lives in the UI layer
// (drawScrollBarCanvas, new_src/ui/Ui.cpp). GROUP 7 repointed the dialog's own
// call at Ui::scrollBar, so the only caller left is the loot overlay
// (new_src/core/LootSession.cpp:163) — GROUP 5 moves that one and deletes this
// forwarder together with the Hud pointer it needs.
void DialogSystem::drawScrollBar(Graphics2D& g, int x, int y, int h,
	int topLine, int pageEnd, int numLines, int viewLines) const {
	drawScrollBarCanvas(g, env_.hud->imgUIImages(), x, y, h, topLine, pageEnd,
		numLines, viewLines);
}

} // namespace newcore
