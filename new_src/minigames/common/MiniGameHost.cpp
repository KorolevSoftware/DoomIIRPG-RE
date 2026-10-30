#include "minigames/common/MiniGameHost.h"

#include <string>

#include "domain/game/Enums.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "io/Localization.h"
#include "ui/Hud.h"

namespace newcore {

void MiniGameHost::init(const Env& env) {
	env_ = env;
	help_.init(env_.loc);
	vending_.init(this);
	hacking_.init(this);
}

bool MiniGameHost::loadAssets(TextureStore& store, const UiAssets::ResourceReader& read) {
	return assets_.load(store, read) & vending_.loadAssets(store, read)
		& hacking_.loadAssets(store, read);
}

bool MiniGameHost::start(int type, int b1, int b2, int b3, ScriptThread* t) {
	MiniGame* g = nullptr;
	switch (type) {
	case kVending: g = &vending_; break;
	case kHacking: g = &hacking_; break;
	default: return false;   // sentry bot: later
	}
	callingThread_ = t;
	active_ = g;
	touchButton_ = -1;
	env_.host->requestState(StateId::MiniGame);
	g->begin(b1, b2, b3);
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
