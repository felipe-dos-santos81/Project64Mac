// Project64 - A Nintendo 64 emulator
// The one unit-test program. Runs every area in order, or only the one named as the first
// argument, and prints "ok: <area>" for each area that passed. See UnitTest.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "UnitTest.h"

namespace
{
struct Area
{
    const char * Name;
    void (*Run)();
};

const Area kAreas[] = {
    { "pointer-layout", RunPointerLayoutTests },
    { "pointer-menu", RunPointerMenuTests },
    { "face-gestures", RunFaceGesturesTests },
    { "game-config", RunGameConfigTests },
    { "input-config", RunInputConfigTests },
    { "wizard-draft", RunWizardDraftTests },
};
}

int main(int argc, char ** argv)
{
    const char * Only = (argc > 1 && argv[1][0] != '\0') ? argv[1] : nullptr;
    bool Ran = false;
    for (const Area & A : kAreas)
    {
        if (Only != nullptr && strcmp(Only, A.Name) != 0) continue;
        Ran = true;
        const int Before = TestFailures();
        A.Run();
        if (TestFailures() == Before)
        {
            printf("ok: %s\n", A.Name);
        }
        else
        {
            fprintf(stderr, "%s: %d failure(s)\n", A.Name, TestFailures() - Before);
        }
    }
    if (!Ran)
    {
        fprintf(stderr, "unknown area \"%s\"; the areas are:", Only);
        for (const Area & A : kAreas) fprintf(stderr, " %s", A.Name);
        fprintf(stderr, "\n");
        return 2;
    }
    return TestFailures() == 0 ? 0 : 1;
}
