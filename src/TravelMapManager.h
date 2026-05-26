#ifndef __TRAVEL_MAP_MANAGER_H__
#define __TRAVEL_MAP_MANAGER_H__

class Image;
class Graphics;
class Text;

class TravelMapManager
{
public:
	short TM_LastLevelId;
	short TM_LoadLevelId;
	bool TM_NewGame;
	int totalTMTimeInPastAnimations;
	int targetX;
	int targetY;
	int xDiff;
	int yDiff;
	int mapWidth;
	int mapHeight;
	int _field_0xf20;
	int _field_0xf24;
	int _field_0xf28;
	int _field_0xf2c;
	Image* imgTravelBG;
	Image* imgTravelPath;
	Image* imgNameHighlight;
	Image* imgSpaceShip;
	Image* imgTierCloseUp;
	Image* imgEarthCloseUp;
	Image* imgStarField;
	Image* imgMapHorzGridLines;
	Image* imgMapVertGridLines;
	Image* imgMagGlass;

	TravelMapManager();
	~TravelMapManager();

	void init();
	void dispose();

	void drawTravelMap(Graphics* graphics);
	bool newLevelSamePlanet();
	void drawAppropriateCloseup(Graphics* graphics, int n, bool b);
	bool drawDottedLine(Graphics* graphics, int n);
	bool drawDottedLine(Graphics* graphics);
	bool drawMarsToMoonLinePlusSpaceShip(Graphics* graphics, int n);
	int yCoordOfSpaceShip(int n);
	bool drawMoonToEarthLine(Graphics* graphics, int n, bool b);
	bool drawEarthToHellLine(Graphics* graphics, int n, bool b);
	void drawLocatorBoxAndName(Graphics* graphics, bool b, int n, Text* text);
	void drawGridLines(Graphics* graphics, int i);
	bool onMoon(int n);
	bool onEarth(int n);
	bool inHell(int n);
	void handleInput(int key, int action);
	void finishAndLoadLevel();
	bool drawLocatorLines(Graphics* graphics, int n, bool b, bool b2);
	void drawStarFieldPage(Graphics* graphics);
	void drawStarField(Graphics* graphics, int x, int y);
	void runStarFieldFrame();
};

#endif
