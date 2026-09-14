#pragma once
#include "Log.h"

enum TraceSeverity
{
    TraceError = 0x00000001,
    TraceWarning = 0x00000002,
    TraceNotice = 0x00000003,
    TraceInfo = 0x00000004,
    TraceDebug = 0x00000005,
    TraceVerbose = 0x00000006,
};

#ifndef _WIN32
#define __interface struct
#endif

__interface CTraceModule
{
    virtual void Write(uint32_t module, uint8_t severity, const char * file, int line, const char * function, const char * Message) = 0;
    virtual void FlushTrace() = 0;
};

class CTraceFileLog : public CTraceModule
{
public:
    CTraceFileLog(const char * FileName, bool FlushFile, CLog::LOG_OPEN_MODE eMode, size_t dwMaxFileSize = 5);
    virtual ~CTraceFileLog();

    void SetFlushFile(bool bFlushFile);
    void Write(uint32_t module, uint8_t severity, const char * file, int line, const char * function, const char * Message);
    void FlushTrace(void);

private:
    CLog m_hLogFile;
    bool m_FlushFile;
};

#ifdef _WIN32
#define WriteTrace(m, s, format, ...)                                                        \
    if (g_ModuleLogLevel[(m)] >= (s))                                                        \
    {                                                                                        \
        WriteTraceFull((m), (s), __FILE__, __LINE__, __FUNCTION__, (format), ##__VA_ARGS__); \
    }
#else
#define WriteTrace(m, s, format, ...)                                                               \
    if (g_ModuleLogLevel[(m)] >= (s))                                                               \
    {                                                                                               \
        WriteTraceFull((m), (s), __FILE__, __LINE__, __PRETTY_FUNCTION__, (format), ##__VA_ARGS__); \
    }
#endif

CTraceModule * TraceAddModule(CTraceModule * TraceModule);
CTraceModule * TraceRemoveModule(CTraceModule * TraceModule);
const char * TraceSeverity(uint8_t severity);
const char * TraceModule(uint32_t module);
void TraceSetModuleName(uint8_t module, const char * Name);
void CloseTrace(void);

void WriteTraceFull(uint32_t module, uint8_t severity, const char * file, int line, const char * function, const char * format, ...);
void TraceFlushLog(void);
void TraceSetMaxModule(uint32_t MaxModule, uint8_t DefaultSeverity);

// Raises trace levels and echoes trace output to stderr when the PJ64_TRACE environment
// variable is set; does nothing when it is not. PJ64_TRACE is a comma separated list, and
// each entry is either a severity that applies to every module ("debug") or a single
// module ("Audio Driver=verbose"). Severities are error, warning, notice, info, debug and
// verbose; module names match the names given to TraceSetModuleName, ignoring case and
// spaces. The core and each plugin keep their own trace state, so each one calls this for
// itself, and Tag identifies which of them a line came from.
void TraceEnvSetup(uint32_t MaxModule, const char * Tag);

extern uint32_t * g_ModuleLogLevel;
