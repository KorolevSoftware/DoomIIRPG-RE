#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "IntroSequenceManager.h"
#include "CAppContainer.h"
#include "App.h"
#include "Canvas.h"
#include "Graphics.h"
#include "Text.h"
#include "Image.h"
#include "Enums.h"
#include "Game.h"
#include "Player.h"
#include "Hud.h"
#include "Combat.h"
#include "Sound.h"
#include "Button.h"
#include "MenuSystem.h"
#include "Menus.h"
#include "HackingGame.h"
#include "MayaCamera.h"
#include "Render.h"

IntroSequenceManager::IntroSequenceManager() {
    std::memset(this, 0, sizeof(IntroSequenceManager));
}

IntroSequenceManager::~IntroSequenceManager() {}

void IntroSequenceManager::startup() {
    Applet* app = CAppContainer::getInstance()->app;
    fmButton* button;

    // Setup Character Buttons
    this->m_characterButtons = new fmButtonContainer();
    for (int i = 0; i < 5; i++) {
        button = new fmButton(i, 0, 0, 0, 0, 1027);
        this->m_characterButtons->AddButton(button);
    }

    // Setup Story Buttons
    this->m_storyButtons = new fmButtonContainer();
    button = new fmButton(0, 0, 280, 60, 40, 1027);
    this->m_storyButtons->AddButton(button);
    button = new fmButton(1, 380, 280, 100, 40, 1027);
    this->m_storyButtons->AddButton(button);
    button = new fmButton(2, 420, 0, 60, 40, 1027);
    this->m_storyButtons->AddButton(button);
}

void IntroSequenceManager::loadPrologueText() {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    Text* text;

    this->storyPage = 0;
    this->storyTotalPages = 0;
    app->localization->resetTextArgs();

    short n = 0;
    switch (app->player->characterChoice) {
    case 1: {
        n = 218;
        break;
    }
    case 2: {
        n = 219;
        break;
    }
    default: {
        n = 220;
        break;
    }
    }

    app->localization->addTextArg(3, n);
    text = app->localization->getLargeBuffer();
    app->localization->loadText(2);
    app->localization->composeText(2, 12, text);
    app->localization->unloadText(2);

    int n2 = (canvas->displayRect[2] - 30) / 10;
    int n3 = (canvas->displayRect[3] - 40) / 21;
    text->wrapText(n2);

    this->storyIndexes[this->storyTotalPages++] = 0;

    int n4 = 0;
    int first = 0;
    while ((first = text->findFirstOf('|', first)) != -1) {
        ++first;
        if (++n4 % n3 == 0) {
            this->storyIndexes[this->storyTotalPages++] = first;
        }
    }

    canvas->dialogBuffer = text;
    this->storyIndexes[this->storyTotalPages] = text->length();
    this->storyX = canvas->displayRect[0] + 15;
    this->storyY = canvas->displayRect[1] + 20;
}

void IntroSequenceManager::loadEpilogueText() {
    Applet* app = CAppContainer::getInstance()->app;
    this->imgProlog = app->loadImage("prolog.bmp", true);
    this->initScrollingText((short)0, (short)134, false, 32, 1, 1000);
}

void IntroSequenceManager::setupCharacterSelection() {
    Applet* app = CAppContainer::getInstance()->app;

    app->beginImageLoading();
    this->imgCharacter_select_stat_bar = app->loadImage("Character_select_stat_bar.bmp", true);
    this->imgCharacter_select_stat_header = app->loadImage("Character_select_stat_header.bmp", true);
    this->imgTopBarFill = app->loadImage("Character_select_top_bar.bmp", true);
    this->imgCharacter_upperbar = app->loadImage("character_upperbar.bmp", true);
    this->imgCharacterSelectionAssets = app->loadImage("charSelect.bmp", true);
    this->imgCharSelectionBG = app->loadImage("charSelectionBG.bmp", true);
    this->imgMajorMugs = app->loadImage("Hud_Player.bmp", true);
    this->imgSargeMugs = app->loadImage("Hud_PlayerDoom.bmp", true);
    this->imgScientistMugs = app->loadImage("Hud_PlayerScientist.bmp", true);
    this->imgMajor_legs = app->loadImage("Major_legs.bmp", true);
    this->imgMajor_torso = app->loadImage("Major_torso.bmp", true);
    this->imgRiley_legs = app->loadImage("Riley_legs.bmp", true);
    this->imgRiley_torso = app->loadImage("Riley_torso.bmp", true);
    this->imgSarge_legs = app->loadImage("Sarge_legs.bmp", true);
    this->imgSarge_torso = app->loadImage("Sarge_torso.bmp", true);
    app->endImageLoading();
}

void IntroSequenceManager::disposeIntro() {
    Canvas* canvas = CAppContainer::getInstance()->app->canvas;
    canvas->dialogBuffer->dispose();
    canvas->dialogBuffer = NULL;
    canvas->loadMap(canvas->startupMap, false, true);
}

void IntroSequenceManager::disposeEpilogue() {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    canvas->dialogBuffer->dispose();
    canvas->dialogBuffer = nullptr;
    app->sound->soundStop();
    app->menuSystem->setMenu(Menus::MENU_LEVEL_STATS);
    app->sound->playSound(1069, '\x01', 3, false);
}

void IntroSequenceManager::drawScroll(Graphics* graphics, int n, int n2, int n3, int n4) { // J2ME
    Canvas* canvas = CAppContainer::getInstance()->app->canvas;
    int width = canvas->imgDialogScroll->width;
    int height = canvas->imgDialogScroll->height;
    graphics->drawRegion(canvas->imgDialogScroll, 0, 0, width, height, n, n2 + n4 - height, 0, 0, 0);
    graphics->drawRegion(canvas->imgDialogScroll, 0, 0, width, height, n + n3 - width, n2 + n4 - height, 0, 2, 0);
    graphics->drawRegion(canvas->imgDialogScroll, 0, 0, width, height, n, n2, 0, 1, 0);
    graphics->drawRegion(canvas->imgDialogScroll, 0, 0, width, height, n + n3 - width, n2, 0, 3, 0);
    graphics->fillRegion(canvas->imgDialogScroll, width - 14, 0, 14, height, n + width, n2, n3 - 2 * width, height, 3);
    graphics->fillRegion(canvas->imgDialogScroll, width - 14, 0, 14, height, n + width, n2 + (n4 - height), n3 - 2 * width, height, 0);
    graphics->fillRegion(canvas->imgDialogScroll, 0, 0, width, 6, n, n2 + height, width, n4 - 2 * height, 0);
    graphics->fillRegion(canvas->imgDialogScroll, 0, 0, width, 6, n + n3 - width, n2 + height, width, n4 - 2 * height, 3);
    graphics->fillRegion(canvas->imgDialogScroll, width - 6, 0, 6, 6, n + width, n2 + height, n3 - 2 * width, n4 - 2 * height, 0);
}

void IntroSequenceManager::initScrollingText(short i, short i2, bool dehyphenate, int spacingHeight, int numLines, int textMSLine) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;

    if (canvas->dialogBuffer == nullptr) {
        canvas->dialogBuffer = app->localization->getLargeBuffer();
    }
    else {
        canvas->dialogBuffer->setLength(0);
    }

    app->localization->composeText(i, i2, canvas->dialogBuffer);

    if (dehyphenate) {
        canvas->dialogBuffer->dehyphenate();
    }

    canvas->dialogBuffer->wrapText((canvas->displayRect[2] - 8) / Applet::CHAR_SPACING[app->fontType]);
    this->scrollingTextSpacing = spacingHeight;
    this->scrollingTextStart = -1;
    this->scrollingTextLines = (canvas->dialogBuffer->getNumLines() + numLines);
    this->scrollingTextMSLine = textMSLine;
    this->scrollingTextDone = false;
    this->scrollingTextFontHeight = Applet::FONT_HEIGHT[app->fontType] + 2;
    this->scrollingTextSpacingHeight = spacingHeight * ((316 - (Applet::FONT_HEIGHT[app->fontType] * 2)) / spacingHeight);
}

void IntroSequenceManager::drawCredits(Graphics* graphics) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    Text* textBuff;

    this->drawScrollingText(graphics);
    if (this->scrollingTextDone != false) {
        textBuff = app->localization->getSmallBuffer();
        textBuff->setLength(0);
        app->localization->composeText(0, 43, textBuff);
        textBuff->dehyphenate();
        graphics->drawString(textBuff, canvas->SCR_CX - 24, canvas->screenRect[3] - 32, 2);
        textBuff->dispose();
    }
}

void IntroSequenceManager::drawScrollingText(Graphics* graphics) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;

    int n = this->scrollingTextMSLine * ((this->scrollingTextSpacing << 16) >> 4) >> 16;
    if (this->scrollingTextStart == -1) {
        this->scrollingTextStart = app->gameTime;
        int n2 = canvas->screenRect[3] / this->scrollingTextSpacing;
        if (canvas->state == Canvas::ST_CREDITS) {
            this->scrollingTextEnd = n * this->scrollingTextLines;
        }
        else {
            this->scrollingTextEnd = n * (this->scrollingTextLines + (n2 - 2));
        }
    }

    int gameTime = app->gameTime;
    int scrollingTextStart = this->scrollingTextStart;
    if (gameTime - scrollingTextStart > this->scrollingTextEnd) {
        scrollingTextStart = gameTime - this->scrollingTextEnd;
        this->scrollingTextDone = true;
    }

    graphics->eraseRgn(canvas->displayRect);

    if (canvas->state == Canvas::ST_EPILOGUE || canvas->state == Canvas::ST_INTRO_MOVIE) {
        int rect[4];
        rect[0] = canvas->displayRect[0];
        rect[1] = canvas->displayRect[1];
        rect[2] = canvas->displayRect[2] - rect[0];
        rect[3] = canvas->displayRect[3]; // missing line code?
        graphics->clipRect(0, (canvas->displayRect[3] - 220) / 2, canvas->displayRect[2], 220);
        graphics->drawRegion(this->imgProlog, 0, 65, 480, 307, 0, 0, 0, 0, 0);
        graphics->fade(rect, 192, 0);
        graphics->setScreenSpace(0, (canvas->displayRect[3] - 220) / 2, canvas->displayRect[2], 220);
    }

    graphics->drawString(canvas->dialogBuffer, canvas->SCR_CX, canvas->cinRect[3] - ((((gameTime - scrollingTextStart) << 8) / n) * (this->scrollingTextSpacing << 8) >> 16), this->scrollingTextSpacing, 1, 0, -1);
    graphics->resetScreenSpace();
}

void IntroSequenceManager::changeStoryPage(int i) {
    Canvas* canvas = CAppContainer::getInstance()->app->canvas;
    if (i < 0 && this->storyPage == 0) {
        if (canvas->state == Canvas::ST_INTRO) {
            canvas->setState(Canvas::ST_CHARACTER_SELECTION);
            canvas->dialogBuffer->dispose();
            canvas->dialogBuffer = nullptr;
        }
    }
    else {
        this->storyPage += i;
    }
}

void IntroSequenceManager::drawStory(Graphics* graphics)
{
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;

    Text* this_00;
    Text* this_01;
    short i2;
    Text* text;

    if (this->storyPage < this->storyTotalPages) {
        graphics->drawImage(app->hackingGame->imgHelpScreenAssets, 0, 0, 0, 0, 0);

        this_00 = app->localization->getLargeBuffer();
        if ((canvas->state != Canvas::ST_EPILOGUE) || (0 < this->storyPage)) {
            this_00->setLength(0);
            app->localization->composeText(3, 80, this_00); // "Back"
            this_00->dehyphenate();
            app->setFontRenderMode(2);
            if (this->m_storyButtons->GetButton(0)->highlighted != false) {
                app->setFontRenderMode(0);
            }
            graphics->drawString(this_00, 17, 310, 36); // Old -> 2, 319, 36
            app->setFontRenderMode(0);
        }

        this_00->setLength(0);
        this_01 = app->localization->getSmallBuffer();
        if (this->storyPage < this->storyTotalPages + -1) {
            app->localization->composeText(0, 56, this_00); // more
            i2 = 40; // Skip
            text = this_01;
            this->m_storyButtons->GetButton(1)->touchAreaDrawing.x = 420; // [GEC], ajusta la posicion X de la caja de toque
            this->m_storyButtons->GetButton(1)->touchAreaDrawing.w = 60; // [GEC], ajusta el ancho de la caja de toque
        }
        else {
            i2 = 43; // Continue
            text = this_00;
            this->m_storyButtons->GetButton(1)->touchAreaDrawing.x = 380; // [GEC], ajusta la posicion X de la caja de toque
            this->m_storyButtons->GetButton(1)->touchAreaDrawing.w = 100; // [GEC], ajusta el ancho de la caja de toque
        }

        app->localization->composeText(0, i2, text);
        this_00->dehyphenate();
        this_01->dehyphenate();
        app->setFontRenderMode(2);
        if (this->m_storyButtons->GetButton(1)->highlighted != false) {
            app->setFontRenderMode(0);
        }
        graphics->drawString(this_00, 463, 310, 40); // Old -> 478, 319, 40);
        app->setFontRenderMode(0);

        app->setFontRenderMode(2);
        if (this->m_storyButtons->GetButton(2)->highlighted != false) {
            app->setFontRenderMode(0);
        }
        graphics->drawString(this_01, 463, 10, 8); // Old -> 478, 1, 8);

        app->setFontRenderMode(0);
        this_00->dispose();
        this_01->dispose();

        graphics->drawString(canvas->dialogBuffer, this->storyX, this->storyY, 21, 0, this->storyIndexes[0],
            this->storyIndexes[1] - this->storyIndexes[0]);
    }
}

int IntroSequenceManager::getCharacterConstantByOrder(int i) {
    switch (i) {
    case 0: {
        return 1;
    }
    case 1: {
        return 3;
    }
    case 2: {
        return 2;
    }
    }
    return 0;
}

void IntroSequenceManager::drawCharacterSelection(Graphics* graphics) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    fmButton* button;
    Text* textBuff;
    Image* img;
    int textID;


    graphics->clipRect(0, 0, canvas->screenRect[2], canvas->screenRect[3]);
    graphics->drawImage(this->imgCharSelectionBG, 0, 0, 0, 0, 0);
    graphics->drawImage(this->imgTopBarFill, canvas->SCR_CX - this->imgTopBarFill->width / 2, 0, 0, 0, 0);

    textBuff = app->localization->getSmallBuffer();
    textBuff->setLength(0);

    switch (canvas->stateVars[0]) {
        case 1: {
            app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::CHARACTER_SELECT_MAJOR_NAME, textBuff);
            break;
        }
        case 3: {
            app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::CHARACTER_SELECT_SCIENTIST_NAME, textBuff);
            break;
        }
        case 2: {
            app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::CHARACTER_SELECT_SERGEANT_NAME, textBuff);
            break;
        }
    }

    textBuff->dehyphenate();
    graphics->drawString(textBuff, canvas->SCR_CX, 3, 1);

    int j = 152;
    for (int i = 0; i < ((canvas->stateVars[1] == 0) ? 3 : 1); i++) {
        bool b = this->getCharacterConstantByOrder(i) == canvas->stateVars[0] || canvas->stateVars[1] != 0;

        //iVar2 = j + -0xf;
        graphics->drawRegion(this->imgCharacterSelectionAssets, 0, 0, 31, 83, j - 15, 55, 0, 0, 0);
        graphics->drawRegion(this->imgCharacterSelectionAssets, 0, 0, 31, 83, j + 16, 55, 0, 4, 0);

        button = this->m_characterButtons->GetButton(i);
        if (button->highlighted)
        {
            graphics->fillRect(j - 15, 55, 62, 83, 0x2896ff);
            graphics->drawRegion(this->imgCharacterSelectionAssets, 31, 0, 27, 60, j - 11, 64, 0, 0, 0);
            graphics->drawRegion(this->imgCharacterSelectionAssets, 31, 0, 27, 60, j + 16, 64, 0, 4, 0);
        }

        button->SetTouchArea(j - 15, 55, 62, 83);

        if (i < 2 && canvas->stateVars[1] == 0) {
            for (int x = (j + 46); x < (j + 64); x += 6) {
                graphics->drawRegion(this->imgCharacterSelectionAssets, 51, 60, 6, 19, x, 81, 0, 0, 0);
            }
        }

        if (b != 0) {
            graphics->drawRegion(this->imgCharacterSelectionAssets, 31, 60, 20, 32, j - 3, 161, 0, 0, 0);
            graphics->drawRegion(this->imgCharacterSelectionAssets, 31, 60, 19, 32, j + 17, 161, 0, 4, 0);
        }

        switch ((canvas->stateVars[1] == 0) ? this->getCharacterConstantByOrder(i) : canvas->stateVars[0]) {
            case 1: {
                img = this->imgMajorMugs;
                textID = MenuStrings::CHARACTER_SELECT_MAJOR;
                canvas->graphics.currentCharColor = 5;
                break;
            }
            case 3: {
                img = this->imgScientistMugs;
                textID = MenuStrings::CHARACTER_SELECT_SCIENTIST;
                canvas->graphics.currentCharColor = 5;
                break;
            }
            case 2: {
                img = this->imgSargeMugs;
                textID = MenuStrings::CHARACTER_SELECT_SERGEANT;
                canvas->graphics.currentCharColor = 5;
                break;
            }
        }

        graphics->drawRegion(img, 0, 0, 0x20, 0x20, j, 0x4b, 0, 0, 0);
        textBuff->setLength(0);
        app->localization->composeText(Strings::FILE_MENUSTRINGS, textID, textBuff);
        textBuff->dehyphenate();
        graphics->drawString(textBuff, j + (img->width / 2), 142, 1);

        j += 75;
    }

    this->drawCharacterSelectionAvatar(canvas->stateVars[0], -10, 0x8c, graphics);
    this->drawCharacterSelectionStats(canvas->stateVars[0], textBuff, 0x16a, 0x73, graphics);

    textBuff->setLength(0);
    app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::CHARACTER_SELECT_CONFIRM, textBuff);
    textBuff->wrapText(0x18, '\n');
    graphics->drawString(textBuff, canvas->SCR_CX, canvas->screenRect[3] - 85, 1);

    textBuff->setLength(0);
    app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::BACK_ITEM, textBuff);
    button = this->m_characterButtons->GetButton(4);
    graphics->fillRect(canvas->SCR_CX - 85, canvas->screenRect[3] - 60, 70, 30, button->highlighted ? 0x2896ff : 0x646464);
    graphics->drawRect(canvas->SCR_CX - 85, canvas->screenRect[3] - 60, 70, 30);
    button->SetTouchArea(canvas->SCR_CX - 85, canvas->screenRect[3] - 60, 70, 30);
    graphics->drawString(textBuff, canvas->SCR_CX - 50, canvas->screenRect[3] - 50, 1);

    int8_t b = canvas->OSC_CYCLE[app->time / 100 % 4];

    // [GEC]
    if (canvas->stateVars[2] == 0 && canvas->stateVars[8] == 1) { // J2ME/BREW
        graphics->drawCursor((canvas->SCR_CX - 50 - 6) - (textBuff->getStringWidth() / 2) + b, canvas->screenRect[3] - 50, 0x18, true);
    }

    textBuff->setLength(0);
    app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::YES_LABEL, textBuff);
    button = this->m_characterButtons->GetButton(3);
    graphics->fillRect(canvas->SCR_CX + 15, canvas->screenRect[3] - 60, 70, 30, button->highlighted ? 0x2896ff : 0x646464);
    graphics->drawRect(canvas->SCR_CX + 15, canvas->screenRect[3] - 60, 70, 30);
    button->SetTouchArea(canvas->SCR_CX + 15, canvas->screenRect[3] - 60, 70, 30);
    graphics->drawString(textBuff, canvas->SCR_CX + 50, canvas->screenRect[3] - 50, 1);

    // [GEC]
    if (canvas->stateVars[2] == 1 && canvas->stateVars[8] == 1) { // J2ME/BREW
        graphics->drawCursor((canvas->SCR_CX + 50 - 6) - (textBuff->getStringWidth() / 2) + b, canvas->screenRect[3] - 50, 0x18, true);
    }

    textBuff->dispose();
}

void IntroSequenceManager::drawCharacterSelectionAvatar(int i, int x, int y, Graphics* graphics)
{
    Applet* app = CAppContainer::getInstance()->app;
    Image* troso, * legs;

    switch (i) {
    case 1: {
        legs = this->imgMajor_legs;
        troso = this->imgMajor_torso;
        break;
    }
    case 3: {
        legs = this->imgRiley_legs;
        troso = this->imgRiley_torso;
        break;
    }
    case 2: {
        legs = this->imgSarge_legs;
        troso = this->imgSarge_torso;
        break;
    }
    }

    graphics->drawImage(legs, x, y, 0, 0, 0);
    graphics->drawImage(troso, x, y - (app->time / 1000 & 1U), 0, 0, 0);
}

void IntroSequenceManager::drawCharacterSelectionStats(int i, Text* text, int x, int y, Graphics* graphics) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    int defense;
    int strength;
    int accuracy;
    int agility;
    int iq;

    switch (i) {
    case 1: {
        defense = 8;
        strength = 9;
        accuracy = 97;
        agility = 12;
        iq = 110;
        break;
    }
    case 3: {
        defense = 8;
        strength = 8;
        accuracy = 87;
        agility = 6;
        iq = 150;
        break;
    }
    case 2: {
        defense = 12;
        strength = 14;
        accuracy = 92;
        agility = 6;
        iq = 100;
        break;
    }
    }

    if (app->game->difficulty == 2) {
        defense = 0;
    }

    graphics->drawImage(this->imgCharacter_select_stat_header, x - 2, y - 4, 0, 0, 0);
    graphics->drawImage(this->imgCharacter_select_stat_bar, x - 2, y + 14, 0, 0, 0);

    int yFix = -2; // [GEC] ajusta el texto

    text->setLength(0);
    app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::DEFENSE_LABEL, text);
    text->dehyphenate();
    text->append(defense);
    canvas->graphics.currentCharColor = 5;
    graphics->drawString(text, x, y + 18 + yFix, 20);
    graphics->drawImage(this->imgCharacter_select_stat_bar, x - 2, y + 34, 0, 0, 0);

    text->setLength(0);
    app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::STRENGTH_LABEL, text);
    text->dehyphenate();
    text->append(strength);
    canvas->graphics.currentCharColor = 5;
    graphics->drawString(text, x, y + 38 + yFix, 20);
    graphics->drawImage(this->imgCharacter_select_stat_bar, x - 2, y + 54, 0, 0, 0);

    text->setLength(0);
    app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::ACCURACY_LABEL, text);
    text->dehyphenate();
    text->append(accuracy);
    canvas->graphics.currentCharColor = 5;
    graphics->drawString(text, x, y + 58 + yFix, 20);
    graphics->drawImage(this->imgCharacter_select_stat_bar, x - 2, y + 74, 0, 0, 0);

    text->setLength(0);
    app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::AGILITY_LABEL, text);
    text->dehyphenate();
    text->append(agility);
    canvas->graphics.currentCharColor = 5;
    graphics->drawString(text, x, y + 78 + yFix, 20);
    graphics->drawImage(this->imgCharacter_select_stat_bar, x - 2, y + 94, 0, 0, 0);

    text->setLength(0);
    app->localization->composeText(Strings::FILE_MENUSTRINGS, MenuStrings::IQ_LABEL, text);
    text->dehyphenate();
    text->append(iq);
    canvas->graphics.currentCharColor = 5;
    graphics->drawString(text, x, y + 98 + yFix, 20);
}

void IntroSequenceManager::handleCharacterSelectionInput(int key, int action) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;
    //printf("handleCharacterSelectionInput key %d, action %d\n", key, action);
    if (canvas->stateVars[1]) {
        if (canvas->stateVars[1] == 1) {

            if ((action == Enums::ACTION_LEFT) || (action == Enums::ACTION_RIGHT)) {
                canvas->stateVars[2] = canvas->stateVars[2] != 1;
            }
            else if (action == Enums::ACTION_FIRE) {
                if (canvas->stateVars[2] == 1) {
                    app->player->setCharacterChoice(canvas->stateVars[0]);
                    app->player->reset();
                    canvas->setState(Canvas::ST_INTRO);
                    this->disposeCharacterSelection();
                }
                else {
                    canvas->stateVars[1] = 0;
                    canvas->stateVars[2] = 1;
                }
            }

        }
    }
    else {
        for (int i = 0; i < 3; i++) { // Characters
            if (this->m_characterButtons->GetButton(i)->highlighted) {
                if (i == 0) {
                    canvas->stateVars[0] = 1;
                }
                else if (i == 1) {
                    canvas->stateVars[0] = 3;
                }
                else {
                    canvas->stateVars[0] = 2;
                }
            }
        }

        if (this->m_characterButtons->GetButton(3)->highlighted) { // Yes
            app->player->setCharacterChoice(canvas->stateVars[0]);
            app->player->reset();
            canvas->setState(Canvas::ST_INTRO);
            this->disposeCharacterSelection();
        }

        if (this->m_characterButtons->GetButton(4)->highlighted) { // Back
            canvas->backToMain(false);
        }

        this->m_characterButtons->HighlightButton(0, 0, false);

        if (!canvas->touched) {
            if (canvas->stateVars[8] == 0) { // [GEC]
                if (action == Enums::ACTION_RIGHT) {
                    for (int i = 0; i < 3; ++i) {
                        if (canvas->stateVars[0] == this->getCharacterConstantByOrder(i)) {
                            canvas->stateVars[0] = this->getCharacterConstantByOrder((i + 1) % 3);
                            app->menuSystem->soundClick();
                            break;
                        }
                    }
                }
                else if (action == Enums::ACTION_LEFT) {
                    for (int j = 0; j < 3; ++j) {
                        if (canvas->stateVars[0] == this->getCharacterConstantByOrder(j)) {
                            canvas->stateVars[0] = this->getCharacterConstantByOrder((j + 2) % 3);
                            app->menuSystem->soundClick();
                            break;
                        }
                    }
                }
                else if (action == Enums::ACTION_FIRE) {
                    canvas->stateVars[8] = 1; // [GEC]
                    app->sound->playSound(1086, 0, 5, false);
                }
                else if (action == Enums::ACTION_MENU) {
                    this->disposeCharacterSelection();
                    canvas->backToMain(false);
                }
            }
            else if (canvas->stateVars[8] == 1) { // [GEC]
                if ((action == Enums::ACTION_LEFT) || (action == Enums::ACTION_RIGHT)) {
                    canvas->stateVars[2] = canvas->stateVars[2] != 1;
                    app->menuSystem->soundClick();
                }
                else if (action == Enums::ACTION_FIRE) {
                    if (canvas->stateVars[2] == 1) {
                        app->player->setCharacterChoice(canvas->stateVars[0]);
                        app->player->reset();
                        canvas->setState(Canvas::ST_INTRO);
                        this->disposeCharacterSelection();
                    }
                    else {
                        this->disposeCharacterSelection();
                        canvas->backToMain(false);
                    }
                    app->sound->playSound(1086, 0, 5, false);
                }
                else if (action == Enums::ACTION_MENU) {
                    canvas->stateVars[1] = 0;
                    canvas->stateVars[2] = 1;
                    canvas->stateVars[8] = 0; // [GEC]
                    app->sound->playSound(1122, 0, 5, false);
                }
            }
        }
    }
}

void IntroSequenceManager::handleStoryInput(int key, int action) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;

    if (action == Enums::ACTION_LEFT || action == Enums::ACTION_RIGHT) {
        if (canvas->stateVars[0] != 2) {
            canvas->stateVars[0] ^= 1;
        }
    }
    else if (action == Enums::ACTION_UP) {
        if (canvas->stateVars[0] != 2 && this->storyPage < this->storyTotalPages - 1) {
            canvas->stateVars[0] = 2;
        }
    }
    else if (action == Enums::ACTION_DOWN) {
        if (canvas->stateVars[0] == 2) {
            canvas->stateVars[0] = 1;
        }
    }
    else if (action == Enums::ACTION_FIRE) {
        switch (canvas->stateVars[0]) {
        case 0: {
            this->changeStoryPage(-1);
            if (canvas->state == Canvas::ST_CHARACTER_SELECTION) {
                canvas->stateVars[0] = app->player->characterChoice;
                break;
            }
            break;
        }
        case 1: {
            this->changeStoryPage(1);
            break;
        }
        case 2: {
            this->storyPage = this->storyTotalPages;
            break;
        }
        }
    }
    else if (action == Enums::ACTION_AUTOMAP) {
        this->changeStoryPage(1);
        canvas->stateVars[0] = 1;
    }
    else if (action == Enums::ACTION_BACK || action == Enums::ACTION_MENU) {
        this->changeStoryPage(-1);
        if (canvas->state == Canvas::ST_CHARACTER_SELECTION) {
            canvas->stateVars[0] = app->player->characterChoice;
        }
        else {
            canvas->stateVars[0] = 0;
        }
    }
}

void IntroSequenceManager::playIntroMovie(Graphics* graphics) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;

    if (canvas->skipIntro != false) {
        canvas->backToMain(true);
        return;
    }

    if (app->game->hasSeenIntro && app->game->skipMovie) {
        this->exitIntroMovie(false);
        return;
    }

    if (canvas->stateVars[1] == 0) {  // load table camera
        canvas->stateVars[1] = app->gameTime;
        app->game->loadTableCamera(14, 15);
        canvas->numEvents = 0;
        canvas->keyDown = false;
        canvas->keyDownCausedMove = false;
        canvas->ignoreFrameInput = 1;
        this->imgProlog = app->loadImage("prolog.bmp", true);
    }

    if (app->game) {
        if (app->game->mayaCameras)
        {
            if (canvas->stateVars[3] == 0) { // init movie prolog
                app->sound->playSound(1068, 1, 6, false);
                if (canvas->stateVars[1] < app->gameTime) {
                    app->game->activeCameraKey = -1;
                    canvas->stateVars[3] = 0;
                    canvas->stateVars[1] = app->gameTime;
                    canvas->stateVars[2] = 0;
                    canvas->stateVars[0] = 0;
                    canvas->fadeRect = canvas->displayRect;
                    canvas->fadeFlags = Canvas::FADE_FLAG_FADEIN;
                    canvas->fadeColor = 0;
                    canvas->fadeTime = app->time;
                    canvas->fadeDuration = 1500;
                    canvas->stateVars[3] = 1;
                }
            }
            else if (canvas->stateVars[3] == 1) { // draw movie prolog
                if (canvas->displayRect[3] > 220) {
                    graphics->clipRect(0, (canvas->displayRect[3] - 220) / 2, canvas->displayRect[2], 220);
                }

                canvas->stateVars[0] = app->gameTime - app->game->activeCameraTime;
                canvas->stateVars[2] = app->gameTime - canvas->stateVars[1];

                app->game->mayaCameras->Update(app->game->activeCameraKey, canvas->stateVars[0]);

                int uVar3 = app->game->posShift;
                int texW = canvas->displayRect[2];
                int texH = canvas->displayRect[3];

                int posX = app->game->mayaCameras->x >> (uVar3 & 0xff);
                int posY = app->game->mayaCameras->y >> (uVar3 & 0xff);

                int texX = posX - canvas->SCR_CX;
                if (texX < 0) {
                    texX = 0;
                }

                int texY = posY - canvas->SCR_CY;
                if (texY < 0) {
                    texY = 0;
                }

                posY = canvas->SCR_CY - posY;
                if (posY < 0) {
                    posY = 0;
                }

                if (texX + texW > this->imgProlog->width) {
                    texW = this->imgProlog->width - texX;
                }

                if (texY + texH > this->imgProlog->height) {
                    texH = this->imgProlog->height - texY;
                }

                graphics->drawRegion(this->imgProlog, texX, texY, texW, texH, 0, posY, 0, 0, 0);
                if (app->game->mayaCameras->complete) {
                    canvas->stateVars[3] += 1;
                }
            }
            else if (canvas->stateVars[3] == 2) { // init Scrolling Text
                this->initScrollingText(0, 133, false, 32, 1, 800);
                this->drawScrollingText(graphics);
                canvas->stateVars[3] += 1;
            }
            else if (canvas->stateVars[3] == 3) { // draw Scrolling Text
                this->drawScrollingText(graphics);
                if (this->scrollingTextDone) {
                    this->exitIntroMovie(false);
                }
            }

            canvas->staleView = true;
            return;
        }
    }
}

void IntroSequenceManager::exitIntroMovie(bool b) {
    Applet* app = CAppContainer::getInstance()->app;
    Canvas* canvas = app->canvas;

    app->game->cleanUpCamMemory();

    delete this->imgProlog;
    this->imgProlog = nullptr;

    if (canvas->dialogBuffer) {
        canvas->dialogBuffer->dispose();
        canvas->dialogBuffer = nullptr;
    }

    app->sound->soundStop();
    if (b == false) {
        app->game->hasSeenIntro = true;
        app->game->saveConfig();
        canvas->backToMain(false);
    }
}

void IntroSequenceManager::disposeCharacterSelection() {
    delete this->imgCharacter_select_stat_bar;
    this->imgCharacter_select_stat_bar = nullptr;
    delete this->imgCharacter_select_stat_header;
    this->imgCharacter_select_stat_header = nullptr;
    delete this->imgTopBarFill;
    this->imgTopBarFill = nullptr;
    delete this->imgCharacter_upperbar;
    this->imgCharacter_upperbar = nullptr;
    delete this->imgCharacterSelectionAssets;
    this->imgCharacterSelectionAssets = nullptr;
    delete this->imgCharSelectionBG;
    this->imgCharSelectionBG = nullptr;
    delete this->imgMajorMugs;
    this->imgMajorMugs = nullptr;
    delete this->imgSargeMugs;
    this->imgSargeMugs = nullptr;
    delete this->imgScientistMugs;
    this->imgScientistMugs = nullptr;
    delete this->imgMajor_legs;
    this->imgMajor_legs = nullptr;
    delete this->imgMajor_torso;
    this->imgMajor_torso = nullptr;
    delete this->imgRiley_legs;
    this->imgRiley_legs = nullptr;
    delete this->imgRiley_torso;
    this->imgRiley_torso = nullptr;
    delete this->imgSarge_legs;
    this->imgSarge_legs = nullptr;
    delete this->imgSarge_torso;
    this->imgSarge_torso = nullptr;
}
