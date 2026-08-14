#ifndef __PLAYING_INPUT_HANDLER_H__
#define __PLAYING_INPUT_HANDLER_H__

class PlayingInputHandler {
public:
    PlayingInputHandler();

    bool handlePlayingEvents(int key, int action);
    bool handleCinematicInput(int action);
    bool shouldFakeCombat(int n, int n2, int n3);
    bool endOfHandlePlayingEvent(int action, bool b);
};

#endif
