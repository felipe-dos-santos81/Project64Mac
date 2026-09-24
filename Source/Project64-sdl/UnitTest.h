// Project64 - A Nintendo 64 emulator
// The shared harness of the one unit-test program: CHECK, the failure count, and the
// helpers more than one area needs. Each area is a Run<Area>Tests() in its own *Test.cpp,
// registered in UnitTestMain.cpp; an area leaves process-wide state (the working directory,
// the environment, the InputConfig instance) as it found it. No window and no SDL init.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>

// Failures so far, across every area; main reads it before and after each one.
inline int & TestFailures()
{
    static int Count = 0;
    return Count;
}

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); TestFailures()++; } } while (0)

// Writes Text to a new file under /tmp and returns its path, valid until the next call.
inline const char * TestWriteTemp(const char * Text)
{
    static char Path[64];
    snprintf(Path, sizeof(Path), "/tmp/pj64-test-XXXXXX");
    const int Fd = mkstemp(Path);
    if (Fd < 0) { perror("mkstemp"); exit(2); }
    write(Fd, Text, strlen(Text));
    close(Fd);
    return Path;
}

// True when Text contains Needle.
inline bool TestHas(const std::string & Text, const char * Needle)
{
    return Text.find(Needle) != std::string::npos;
}

void RunPointerLayoutTests();
void RunPointerMenuTests();
void RunFaceGesturesTests();
void RunGameConfigTests();
void RunInputConfigTests();
void RunWizardDraftTests();
