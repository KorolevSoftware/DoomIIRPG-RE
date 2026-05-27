#ifndef __INPUT_EVENT_CONTROLLER_H__
#define __INPUT_EVENT_CONTROLLER_H__

class InputEventController
{
public:
	int getKeyAction(int key);
	void clearEvents(int ignoreFrameInput);
	bool handleEvent(int key);
	void runInputEvents();
	void addEvents(int event);
};

#endif
