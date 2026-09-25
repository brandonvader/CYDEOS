#pragma once

#define FFT_SAMPLES 256
#define NUM_BARS 16

// I2S/mic capture - installed once at boot and left running continuously
// (see CLAUDE.md: "I2S is installed exactly once at boot and never
// uninstalled"). ES3C28P additionally needs the ES8311 codec's I2C-only
// bring-up (initES8311Codec()) before installI2S() - it never touches the
// I2S peripheral itself, capture still goes through the same
// processAudioChunk() either way.
void installI2S();
#if defined(BOARD_ES3C28P)
void initES8311Codec();
#endif

// Calibrates the mic channel offset exactly once, at boot, BEFORE the SD
// card is ever mounted - see CLAUDE.md and storage.h: calling i2s_read()
// immediately before an SD fopen() reliably broke that specific SD access.
void calibrateChannelOffset();

// Live spectrum bars, updated by processAudioChunk() while recording.
extern int barHeights[NUM_BARS];

// Drains everything currently buffered in I2S's DMA queue (not a single
// fixed-size read per call - see CLAUDE.md on why: a slow UI redraw could
// otherwise overflow the DMA buffer and silently drop audio). Writes PCM
// to the currently-open recording file if one is open, and recomputes
// barHeights[] whenever new samples arrive during an active recording.
// Returns true if barHeights[] was updated this call, so the caller can
// decide whether/when to actually redraw them (not this module's
// concern - see recorder_app.cpp).
bool processAudioChunk();
