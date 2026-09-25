#pragma once

#include <cstdint>
#include <cstdio>

// Fixed WAV header size (16-bit PCM mono, matching this project's only
// recording format) - exposed as a plain constant rather than the whole
// WavHeader struct definition (private to recording.cpp), since callers
// outside that file only ever need the size, e.g. to convert between a
// file's total size and its PCM data size.
#define WAV_HEADER_SIZE 44

// Current recording session state - the single source of truth every
// other recorder module (audio capture, the UI, BLE Companion hooks,
// transcription upload's "is it safe to touch SD/WiFi" checks) reads.
enum RecState { REC_IDLE,
                REC_RECORDING,
                REC_PAUSED };
extern RecState recState;

extern FILE *recFile;
extern uint32_t samplesWritten;
extern char lastRecordingPath[48];

// Session-lifecycle timers the background tick (recorder_app.cpp) reads
// to decide when to refresh the free-space bar / revert the post-
// recording stats display back to "-Ready-". Owned here since
// startRecording()/stopRecording() are what reset them.
extern uint32_t lastFreeSpaceMs;
extern bool showingStats;
extern uint32_t statsShownAtMs;
extern uint32_t lastRecordingDataSize;

// Last value getSDFreeBytes() computed - the BLE status hook and HTTP
// transfer-session /files handler read this instead of touching the SD
// card themselves on every request.
extern uint64_t cachedFreeBytes;

// Returns true on success. Opens a new timestamped WAV file and writes a
// placeholder header (finalized by stopRecording()).
bool startRecording();
void stopRecording();

// Reports peak/RMS level of the just-finished recording to Serial - see
// CLAUDE.md, this is intentionally capped (ANALYZE_MAX_SAMPLES) and reads
// back from the file rather than tracking during recording, so it
// reflects exactly what's on disk.
void analyzeLastRecording();

// Scans /sdcard for the most recent rec_*.wav file (filenames are
// YYYYMMDD_HHMMSS-stamped, so lexical order matches chronological order)
// and sets lastRecordingPath to it. Returns true if one was found. Used
// once at boot to analyze the last recording's levels.
bool findMostRecentRecording();

// ---------------------------------------------------------------------
// Recording metadata - shared by the Recordings browser screen and the
// BLE file-list/HTTP transfer-session handlers (same metadata, multiple
// transports).
// ---------------------------------------------------------------------
struct RecordingInfo {
  char name[40];
  uint32_t size;
  uint32_t duration;
  uint32_t timestamp;
};

// Returns false for anything that isn't one of our own recordings or
// whose size can't be read.
bool getRecordingInfo(const char *filename, RecordingInfo *out);

#define MAX_RECORDINGS_SHOWN 10 // bounded top-N by recency, not a full sort - see loadRecordingsList()
extern RecordingInfo recordingsList[MAX_RECORDINGS_SHOWN];
extern int recordingsListCount;

// Populates recordingsList[]/recordingsListCount with up to
// MAX_RECORDINGS_SHOWN entries, newest first.
void loadRecordingsList();

void formatFileSize(uint32_t bytes, char *buf, size_t len);
void formatDuration(uint32_t totalSeconds, char *buf, size_t len);
