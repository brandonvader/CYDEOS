#pragma once

#include "core/app.h"

// Call BEFORE tft.init() - brings up I2S/mic (ES3C28P: the ES8311 codec's
// I2C-only bring-up first) and calibrates the channel offset. See
// CLAUDE.md: this ordering (I2S bring-up before display init, and channel
// calibration before the SD card is ever mounted - see recorderFinishSetup()
// below) is load-bearing, not arbitrary.
void recorderAudioBringup();

// Call after tft/backlight/touch are initialized, before shellInit():
// mounts the SD card, seeds the free-space cache, loads transcription
// settings, and registers this app's BLE Companion hooks.
void recorderFinishSetup();

// Analyzes the most recent recording's levels, if the SD card mounted and
// one exists. Call after shellInit() - deliberately after the UI is
// already up and interactive, so a slow analysis pass doesn't block the
// whole device from being usable at boot.
void recorderAnalyzeMostRecentIfPresent();

const CydeosApp *recorderAppInterface();
void recorderBackgroundTick(); // pass directly to shellRegisterRecorderApp() - see core/shell.h
