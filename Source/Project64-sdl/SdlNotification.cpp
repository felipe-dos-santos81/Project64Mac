#include "SdlNotification.h"
#include <Common/StdString.h>
#include <Common/Trace.h>
#include <Project64-core/Multilanguage.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Settings.h>
#include <Project64-core/TraceModulesProject64.h>
#include <stdio.h>

CSdlNotification::CSdlNotification()
{
}

CSdlNotification::~CSdlNotification()
{
}

void CSdlNotification::DisplayError(const char * Message) const
{
    WriteTrace(TraceUserInterface, TraceError, "%s", Message);
    fprintf(stderr, "[Project64] error: %s\n", Message);
}

void CSdlNotification::DisplayError(LanguageStringID StringID) const
{
    if (g_Lang)
    {
        DisplayError(g_Lang->GetString(StringID).c_str());
    }
}

void CSdlNotification::FatalError(const char * Message) const
{
    DisplayError(Message);
    if (g_BaseSystem)
    {
        g_BaseSystem->CloseCpu();
    }
}

void CSdlNotification::FatalError(LanguageStringID StringID) const
{
    if (g_Lang)
    {
        FatalError(g_Lang->GetString(StringID).c_str());
    }
}

void CSdlNotification::DisplayWarning(const char * Message) const
{
    WriteTrace(TraceUserInterface, TraceWarning, "%s", Message);
    fprintf(stderr, "[Project64] warning: %s\n", Message);
}

void CSdlNotification::DisplayWarning(LanguageStringID StringID) const
{
    if (g_Lang)
    {
        DisplayWarning(g_Lang->GetString(StringID).c_str());
    }
}

void CSdlNotification::DisplayMessage(int /*DisplayTime*/, const char * Message) const
{
    WriteTrace(TraceUserInterface, TraceInfo, "%s", Message);
    fprintf(stderr, "[Project64] %s\n", Message);
}

void CSdlNotification::DisplayMessage(int DisplayTime, LanguageStringID StringID) const
{
    if (g_Lang)
    {
        DisplayMessage(DisplayTime, g_Lang->GetString(StringID).c_str());
    }
}

void CSdlNotification::DisplayMessage2(const char * Message) const
{
    DisplayMessage(0, Message);
}

bool CSdlNotification::AskYesNoQuestion(const char * Question) const
{
    WriteTrace(TraceUserInterface, TraceWarning, "Unanswered question, defaulting to no: %s", Question);
    return false;
}

void CSdlNotification::BreakPoint(const char * FileName, int32_t LineNumber)
{
    TraceFlushLog();
    FatalError(stdstr_f("Break point found at\n%s\nLine: %d", FileName, LineNumber).c_str());
}

void CSdlNotification::AppInitDone(void)
{
}

// The emulation thread calls this; never pump SDL events here (main thread only).
bool CSdlNotification::ProcessGuiMessages(void) const
{
    return false;
}

void CSdlNotification::ChangeFullScreen(void) const
{
}
