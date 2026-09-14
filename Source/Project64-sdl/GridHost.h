#pragma once

// Runs `Project64 --grid rom1 .. rom16`: lays the ROMs out in a near-square grid of
// windows with a focusable control strip, and cleans every tile up on quit.
int GridHostRun(int argc, char ** argv);
