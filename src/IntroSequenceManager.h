#ifndef __INTRO_SEQUENCE_MANAGER_H__
#define __INTRO_SEQUENCE_MANAGER_H__

class Image;
class Graphics;
class Text;
class fmButtonContainer;

class IntroSequenceManager
{
public:
    int storyX;
    int storyY;
    int storyPage;
    int storyTotalPages;
    int storyIndexes[4];
    int scrollingTextStart;
    int scrollingTextEnd;
    int scrollingTextMSLine;
    int scrollingTextLines;
    int scrollingTextSpacing;
    bool scrollingTextDone;
    int scrollingTextFontHeight;
    int scrollingTextSpacingHeight;
    Image* imgProlog;
    Image* imgScientistMugs;
    Image* imgCharacter_upperbar;
    Image* imgMajorMugs;
    Image* imgSargeMugs;
    Image* imgCharacterSelectionAssets;
    Image* imgCharSelectionBG;
    Image* imgCharacter_select_stat_bar;
    Image* imgCharacter_select_stat_header;
    Image* imgTopBarFill;
    Image* imgMajor_legs;
    Image* imgMajor_torso;
    Image* imgRiley_legs;
    Image* imgRiley_torso;
    Image* imgSarge_legs;
    Image* imgSarge_torso;
    fmButtonContainer* m_characterButtons;
    fmButtonContainer* m_storyButtons;

    IntroSequenceManager();
    ~IntroSequenceManager();

    void startup();

    void loadPrologueText();
    void loadEpilogueText();
    void setupCharacterSelection();
    void disposeIntro();
    void disposeEpilogue();
    void disposeCharacterSelection();

    void drawScroll(Graphics* graphics, int n, int n2, int n3, int n4);
    void initScrollingText(short i, short i2, bool dehyphenate, int spacingHeight, int numLines, int textMSLine);
    void drawCredits(Graphics* graphics);
    void drawScrollingText(Graphics* graphics);

    void changeStoryPage(int i);
    void drawStory(Graphics* graphics);
    void handleStoryInput(int key, int action);

    void drawCharacterSelection(Graphics* graphics);
    void drawCharacterSelectionAvatar(int i, int x, int y, Graphics* graphics);
    void drawCharacterSelectionStats(int i, Text* text, int x, int y, Graphics* graphics);
    void handleCharacterSelectionInput(int key, int action);
    int  getCharacterConstantByOrder(int i);

    void playIntroMovie(Graphics* graphics);
    void exitIntroMovie(bool b);
};

#endif
