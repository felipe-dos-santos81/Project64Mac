#pragma once
#include <Project64-core/Notification.h>

class CSdlNotification : public CNotification
{
public:
    CSdlNotification();
    ~CSdlNotification();

    void DisplayError(const char * Message) const;
    void DisplayError(LanguageStringID StringID) const;
    void FatalError(const char * Message) const;
    void FatalError(LanguageStringID StringID) const;
    void DisplayWarning(const char * Message) const;
    void DisplayWarning(LanguageStringID StringID) const;
    void DisplayMessage(int DisplayTime, const char * Message) const;
    void DisplayMessage(int DisplayTime, LanguageStringID StringID) const;
    void DisplayMessage2(const char * Message) const;
    bool AskYesNoQuestion(const char * Question) const;
    void BreakPoint(const char * FileName, int32_t LineNumber);
    void AppInitDone(void);
    bool ProcessGuiMessages(void) const;
    void ChangeFullScreen(void) const;

private:
    CSdlNotification(const CSdlNotification &);
    CSdlNotification & operator=(const CSdlNotification &);
};
