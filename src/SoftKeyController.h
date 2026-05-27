#ifndef __SOFT_KEY_CONTROLLER_H__
#define __SOFT_KEY_CONTROLLER_H__

class Graphics;

class SoftKeyController
{
public:
	void clearSoftKeys();
	void clearLeftSoftKey();
	void clearRightSoftKey();
	void setLeftSoftKey(short i, short i2);
	void setRightSoftKey(short i, short i2);
	void setSoftKeys(short n, short n2, short n3, short n4);
	void checkHudEvents();
	void drawSoftKeys(Graphics* graphics);
	void drawPlayingSoftKeys();
};

#endif
