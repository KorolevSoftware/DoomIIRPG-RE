#ifndef __LOADING_SCREEN_CONTROLLER_H__
#define __LOADING_SCREEN_CONTROLLER_H__

class Graphics;

class LoadingScreenController
{
public:
	void setLoadingBarText(short loadingStringID, short loadingStringType);
	void updateLoadingBar(bool force);
	void drawLoadingBar(Graphics* graphics);
};

#endif
