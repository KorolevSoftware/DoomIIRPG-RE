#ifndef __LOOTING_SYSTEM_H__
#define __LOOTING_SYSTEM_H__

class Text;
class Graphics;

class LootingSystem
{
public:
	int lootingTime;
	bool crouchingForLoot;
	bool field_0xac5_;
	int lootingCachedPitch;
	short lootPoolIndices[18];
	int lootPool[9];
	int lootPoolCredits;
	int numPoolItems;
	int numLootItems;
	Text* lootText;
	int lootLineNum;
	int lootSource;
	int specialLootIcon;
	bool showingLoot;

	LootingSystem();
	~LootingSystem();

	void onEnterLooting(int destPitch);
	void lootingState();
	void handleLootingEvents(int action);
	void drawLootingMenu(Graphics* graphics);
	void poolLoot(int* array);
	void giveLootPool();
};

#endif
