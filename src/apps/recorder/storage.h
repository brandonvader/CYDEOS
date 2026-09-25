#pragma once

#include <cstdint>

// SD card mount + free-space query. See CLAUDE.md's "Hard-won CYD
// findings" (SPI3_HOST) and "Hard-won ES3C28P findings" (SD_MMC/SDIO)
// before changing either board's mountSD() - both are workarounds for
// confirmed, non-obvious hardware/driver conflicts, not arbitrary choices.
//
// Call mountSD() only after installI2S()/calibrateChannelOffset() (see
// audio.h) have already run - CLAUDE.md: an i2s_read() call immediately
// before an SD fopen() reliably broke that specific SD access, even after
// the SPI3_HOST fix. This ordering constraint is why storage.h and
// audio.h are separate modules but recorder_app.cpp's setup sequencing
// still has to call them in the right order - it's not something either
// module can enforce on its own.
bool mountSD();
uint64_t getSDFreeBytes();
