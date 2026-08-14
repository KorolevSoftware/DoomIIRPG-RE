#ifndef NEW_CORE_GAMELOOP_H
#define NEW_CORE_GAMELOOP_H

namespace newcore {

class AppContext;

// Fixed-step main loop, throttled to ~15 ms ticks like the original game.
class GameLoop {
public:
	static bool run(AppContext& context);
};

} // namespace newcore

#endif // NEW_CORE_GAMELOOP_H