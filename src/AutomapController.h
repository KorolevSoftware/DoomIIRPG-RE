#ifndef __AUTOMAP_CONTROLLER_H__
#define __AUTOMAP_CONTROLLER_H__

class Graphics;

class AutomapController
{
public:
	void automapState();
	void uncoverAutomap();
	void drawAutomap(Graphics* graphics, bool fullRefresh);
};

#endif
