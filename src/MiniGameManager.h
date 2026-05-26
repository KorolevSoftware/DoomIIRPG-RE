#ifndef __MINI_GAME_MANAGER_H__
#define __MINI_GAME_MANAGER_H__

class Image;
class Graphics;
class fmButtonContainer;

class MiniGameManager
{
public:
	int treadmillNumSteps;
	int treadmillLastStep;
	int treadmillReturnCode;
	int treadmillLastStepTime;
	int miniGameHelpScrollPosition;
	int helpTextNumberOfLines;
	Image* imgBootL;
	Image* imgBootR;
	fmButtonContainer* m_treadmillButtons;

	MiniGameManager();
	~MiniGameManager();

	bool startup();
	void onEnterTreadmill();

	bool handleTreadmillEvents(int action);
	void treadmillState();
	bool treadmillFall();
	void drawTreadmillReadout(Graphics* graphics);
	void drawTargetPracticeScore(Graphics* graphics);
	void evaluateMiniGameResults(int n);

	void initMiniGameHelpScreen();
	void drawMiniGameHelpScreen(Graphics* graphics, int i, int i2, Image* image);
	void drawMiniGameHelpText(Graphics* graphics, int i, int i2);
	void handleMiniGameHelpScreenScroll(int i);
};

#endif
