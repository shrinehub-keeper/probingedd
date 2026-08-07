#pragma once

// smsemuRun() return codes.
#define EMU_RUN_RESET (1)  // user asked for a soft reset (system/PS/Home button)
#define EMU_RUN_ERROR (2)  // ROM could not be opened/read

// Starts the emulator + render tasks. Never blocks; the emulator keeps
// running (auto-retrying if no ROM is found yet) until the board resets.
void smsemuStart(void);
