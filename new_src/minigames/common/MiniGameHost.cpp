#include "minigames/common/MiniGameHost.h"

#include <cstdio>
#include <string>

#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "io/Localization.h"
#include "io/MenuData.h"
#include "ui/Hud.h"

namespace newcore {

void MiniGameHost::init(const Env& env) {
	env_ = env;
	help_.init(env_.loc);
	vending_.init(this);
	hacking_.init(this);
	sentry_.init(this);
}

bool MiniGameHost::loadAssets(TextureStore& store, const UiAssets::ResourceReader& read) {
	return assets_.load(store, read) & vending_.loadAssets(store, read)
		& hacking_.loadAssets(store, read) & sentry_.loadAssets(store, read);
}

bool MiniGameHost::start(int type, int b1, int b2, int b3, ScriptThread* t) {
	MiniGame* g = nullptr;
	switch (type) {
	case kVending: g = &vending_; break;
	case kHacking: g = &hacking_; break;
	case kSentryBot: g = &sentry_; break;
	default: return false;   // unknown type
	}
	callingThread_ = t;
	fromMainMenu_ = false;
	active_ = g;
	touchButton_ = -1;
	env_.host->requestState(StateId::MiniGame);
	g->begin(b1, b2, b3);
	return true;
}

bool MiniGameHost::playFromMainMenu(int type) {
	MiniGame* g = nullptr;
	switch (type) {
	case kVending: g = &vending_; break;
	case kHacking: g = &hacking_; break;
	case kSentryBot: g = &sentry_; break;
	default: return false;   // unknown type
	}
	callingThread_ = nullptr;
	active_ = g;
	touchButton_ = -1;
	fromMainMenu_ = true;
	env_.host->requestState(StateId::MiniGame);
	if (g == &vending_) vending_.playFromMainMenu();
	else if (g == &hacking_) hacking_.playFromMainMenu();
	else sentry_.playFromMainMenu();
	return true;
}

void MiniGameHost::handleAction(Action a) {
	if (active_ == nullptr) return;
	if (a == Action::MiniGameTouch) active_->handleTouch(touchButton_);
	else active_->handleAction(a);
}

UiResult MiniGameHost::draw(Ui& ui) {
	return active_ != nullptr ? active_->draw(ui) : UiResult{};
}

void MiniGameHost::finish(int result) {
	// (src/HackingGame.cpp:631-634, src/SentryBotGame.cpp:820-823, src/VendingMachine.cpp:859-862)
	if (fromMainMenu_) {          // no scriptStateVars[7], no thread, no ST_PLAYING
		fromMainMenu_ = false;
		active_ = nullptr;
		callingThread_ = nullptr;
		std::fprintf(stderr, "[sound] 1071\n");        // Music_Title, looped
		env_.openMenu(kMenuMainMinigame);              // setMenu(18) with an empty stack
		return;
	}
	// Cleared before resuming: the resumed script may start another minigame.
	ScriptThread* t = callingThread_;
	callingThread_ = nullptr;
	active_ = nullptr;
	env_.vm->vars[kResultVar] = (short)result;
	env_.host->requestState(StateId::Playing);
	if (t != nullptr) env_.vm->resumeThread(t);
}

void MiniGameHost::evaluateResults(int n) {
	if (n != 1) return;
	const int gained = env_.player->modifyStat(Enums::STAT_IQ, 2);
	std::string s = env_.loc->get(kTextMain, gained > 0 ? 236 : 237);
	if (gained > 0) {
		std::string a = std::to_string(gained);
		composeArgs(s, &a, 1);
	}
	if (env_.hud != nullptr) env_.hud->addMessage(s, 0xAA000000, 3);   // addMessage(id, 3)
}

} // namespace newcore
