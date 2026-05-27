#ifndef __TOUCH_CONTROLLER_H__
#define __TOUCH_CONTROLLER_H__

class Graphics;

class TouchController
{
public:
	void touchStart(int pressX, int pressY);
	void touchMove(int pressX, int pressY);
	void touchEnd(int pressX, int pressY);
	void touchEndUnhighlight();
	int touchToKey_Play(int pressX, int pressY);
	void drawTouchSoftkeyBar(Graphics* graphics, bool highlighted_Left, bool highlighted_Right);
	void touchSwipe(int swDir);
	void flipControls();
	void setControlLayout();
};

#endif
