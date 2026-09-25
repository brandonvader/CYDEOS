#pragma once

#include <cstdint>

// Drawing for the Recorder app's main screen: record/pause buttons, free
// space bar, live spectrum, and the status messages shown in the
// spectrum area (-Ready-/-Writing-/-Paused-/post-recording stats). Pure
// drawing - no state ownership; reads recState/barHeights/etc. from
// recording.h/audio.h.
void renderButtons();
void drawFreeSpaceBar();
void drawSpectrum(bool active);
void clearSpectrumArea();
void showPausedOverlay();
void showWritingMessage();
void showReadyMessage();
void showRecordingStats(uint32_t dataSize);

// Full redraw of the main screen's content area (called on entering the
// Recorder app, or returning to its main screen from a sub-screen).
void drawRecorderMainScreen();

// Redraws whichever status message currently applies to the spectrum
// area, based on the actual current recState/showingStats - used both
// after a state change and when re-entering the main screen, so it always
// reflects reality rather than a stale snapshot.
void refreshRecorderSpectrumArea();
