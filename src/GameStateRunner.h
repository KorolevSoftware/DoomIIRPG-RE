#ifndef __GAME_STATE_RUNNER_H__
#define __GAME_STATE_RUNNER_H__

class GameStateRunner {
public:
    GameStateRunner();

    void combatState();
    void renderOnlyState();
    void playingState();
    void menuState();
    void dyingState();
    void familiarDyingState();
    void logoState();
};

#endif
