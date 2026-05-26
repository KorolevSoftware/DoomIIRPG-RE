#ifndef __DIALOG_SYSTEM_H__
#define __DIALOG_SYSTEM_H__

#include <cstdint>

class Text;
class Graphics;
class ScriptThread;
class EntityDef;

class DialogSystem
{
public:
    short dialogIndexes[1024];
    EntityDef* dialogItem;
    int dialogViewLines;
    int dialogLineStartTime;
    int dialogStartTime;
    int dialogTypeLineIdx;
    int dialogStyle;
    int dialogType;
    int dialogFlags;
    bool dialogResumeScriptAfterClosed;
    bool dialogResumeMenu;
    bool dialogClosing;
    ScriptThread* dialogThread;
    int numDialogLines;
    int currentDialogLine;
    int helpMessageTypes[16];
    int helpMessageInts[16];
    void* helpMessageObjs[16];
    char helpMessageThreads[16];
    int numHelpMessages;

    DialogSystem();
    ~DialogSystem();

    void handleDialogEvents(int key);
    void dialogState(Graphics* graphics);
    void closeDialog(bool skipDialog);
    void prepareDialog(Text* text, int dialogStyle, int dialogFlags);
    void startDialog(ScriptThread* scriptThread, short n, int n2, int n3);
    void startDialog(ScriptThread* scriptThread, short n, short n2, int n3, int n4, bool b);
    void startDialog(ScriptThread* scriptThread, Text* text, int n, int n2);
    void startDialog(ScriptThread* dialogThread, Text* text, int n, int n2, bool dialogResumeScriptAfterClosed);
    void dequeueHelpDialog();
    void dequeueHelpDialog(bool b);
    void enqueueHelpDialog(short n);
    bool enqueueHelpDialog(short n, short n2, uint8_t b);
    bool enqueueHelpDialog(Text* text);
    bool enqueueHelpDialog(Text* text, int n);
    void enqueueHelpDialog(EntityDef* entityDef);
};

#endif
