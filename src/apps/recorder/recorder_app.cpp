#include "apps/recorder/recorder_app.h"

#include <Arduino.h>
#include <dirent.h>

#include "apps/recorder/audio.h"
#include "apps/recorder/recorder_layout.h"
#include "apps/recorder/recorder_ui.h"
#include "apps/recorder/recording.h"
#include "apps/recorder/recordings_screen.h"
#include "apps/recorder/storage.h"
#include "apps/recorder/transcription.h"
#include "boards/board_config.h"
#include "core/ble_companion.h"
#include "core/display.h"
#include "core/ui_widgets.h"

enum RecorderScreen { RECORDER_MAIN,
                       RECORDER_RECORDINGS };
static RecorderScreen screen = RECORDER_MAIN;

// True only while this app is the foreground app - gates the spectrum's
// live redraw and the free-space-bar/stats-revert redraws so they don't
// paint over whatever app is actually on screen. Recording itself is NOT
// gated on this - see recorderBackgroundTick().
static bool foreground = false;

// ---------------------------------------------------------------------
// Main screen: record/pause buttons + a small link into the Recordings
// browser.
// ---------------------------------------------------------------------
static void drawRecordingsLink() {
  drawTextIn("Recordings", RECORDER_RECORDINGS_LINK_X, RECORDER_RECORDINGS_LINK_W,
             RECORDER_RECORDINGS_LINK_Y, COLOR_AQUA_DIM);
}

static void enterMain() {
  screen = RECORDER_MAIN;
  drawRecorderMainScreen();
  drawRecordingsLink();
}

static void onRecordTapped() {
  if (recState == REC_IDLE) {
    if (startRecording()) {
      recState = REC_RECORDING;
      showingStats = false;
      clearSpectrumArea(); // remove any lingering "-Writing-"/stats display from last time
    }
  } else {
    stopRecording();
    recState = REC_IDLE;
  }
  renderButtons();
  // Deliberately not refreshing the free space bar right here - it hits
  // the SD card immediately after start/stopRecording() just did their
  // own SD access, with no settling gap. The periodic idle-only refresh
  // in recorderBackgroundTick() handles it instead. See CLAUDE.md.
  lastFreeSpaceMs = millis();
  bleCompanionSendStatus(); // unsolicited push - no-op if nothing's connected
}

static void onPauseTapped() {
  if (recState == REC_RECORDING) {
    recState = REC_PAUSED;
    showPausedOverlay(); // freezes the last-drawn bars in place, labels them
  } else if (recState == REC_PAUSED) {
    recState = REC_RECORDING;
    // No explicit clear needed - the next live update redraws the whole
    // area anyway, erasing the overlay.
  } else {
    return; // no-op while idle
  }
  bleCompanionSendStatus();
  renderButtons();
}

static bool touchOnRecordingsLink(int x, int y) {
  return x >= RECORDER_RECORDINGS_LINK_X && x <= RECORDER_RECORDINGS_LINK_X + RECORDER_RECORDINGS_LINK_W &&
         y >= RECORDER_RECORDINGS_LINK_Y - 10 && y <= RECORDER_RECORDINGS_LINK_Y + RECORDER_RECORDINGS_LINK_H;
}

static void handleMainTouch(int x, int y) {
  if (touchOnRecordingsLink(x, y)) {
    screen = RECORDER_RECORDINGS;
    enterRecordingsScreen();
    return;
  }

  int dx = x - BTN_RECORD_CX, dy = y - BTN_ROW_CENTER_Y;
  int touchR = BTN_RADIUS + UI_SCALE(15);
  if (dx * dx + dy * dy <= touchR * touchR) {
    onRecordTapped();
    return;
  }
  int dx2 = x - BTN_PAUSE_CX, dy2 = y - BTN_ROW_CENTER_Y;
  if (dx2 * dx2 + dy2 * dy2 <= touchR * touchR) {
    onPauseTapped();
    return;
  }
}

static void goToRecorderMain() {
  screen = RECORDER_MAIN;
  enterMain();
}

// ---------------------------------------------------------------------
// CydeosApp interface
// ---------------------------------------------------------------------
static void onStart() {
  foreground = true;
  enterMain();
}

static void onStop() {
  foreground = false;
}

static void onTouch(int x, int y) {
  if (screen == RECORDER_MAIN) {
    handleMainTouch(x, y);
  } else {
    recordingsHandleTouch(x, y);
  }
}

static const CydeosApp kRecorderApp = {
    "Recorder",
    onStart,
    onStop,
    nullptr, // onTick - nothing needed beyond the always-on background tick below
    onTouch,
};

const CydeosApp *recorderAppInterface() {
  return &kRecorderApp;
}

// ---------------------------------------------------------------------
// Background tick - keeps recording regardless of which app is on
// screen; only the drawing is gated on `foreground`. See CLAUDE.md's "UI
// architecture" section and CYDEOS Spec.md's "Execution model".
// ---------------------------------------------------------------------
void recorderBackgroundTick() {
  // Only drain I2S while recording or paused - each i2s_read() call can
  // block up to 20ms, no reason to pay that latency while fully idle.
  if (recState != REC_IDLE) {
    bool gotSpectrumData = processAudioChunk();
    if (gotSpectrumData && screen == RECORDER_MAIN && foreground && !blePairingUIActive) {
      static uint32_t lastSpectrumDrawMs = 0;
      if (millis() - lastSpectrumDrawMs > 100) {
        lastSpectrumDrawMs = millis();
        drawSpectrum(true);
      }
    }
  }

  // Refresh only while idle - avoids hitting the SD card right after
  // start/stopRecording() just did (see onRecordTapped()).
  if (recState == REC_IDLE && millis() - lastFreeSpaceMs > 10000) {
    lastFreeSpaceMs = millis();
    if (screen == RECORDER_MAIN && foreground && !blePairingUIActive) drawFreeSpaceBar();
  }

  // Revert the post-recording file stats display back to "-Ready-" after
  // 10s of remaining idle.
  if (recState == REC_IDLE && showingStats && millis() - statsShownAtMs > 10000) {
    showingStats = false;
    if (screen == RECORDER_MAIN && foreground && !blePairingUIActive) showReadyMessage();
  }
}

// ---------------------------------------------------------------------
// BLE Companion hooks
// ---------------------------------------------------------------------
#define BLE_MAX_FILE_LIST_ENTRIES 200 // bounds worst-case blocking time for one request

static uint8_t bleGetRecordingState() {
  return (recState == REC_IDLE) ? 0 : (recState == REC_RECORDING) ? 1 : 2;
}

static void bleGetFreeSpace(uint32_t *freeMB, float *freeHoursOut) {
  *freeMB = (uint32_t)(cachedFreeBytes / (1024ULL * 1024ULL));
  *freeHoursOut = cachedFreeBytes / (SAMPLE_RATE * 2.0f) / 3600.0f;
}

static void bleOnStartRecording() {
  if (recState == REC_IDLE) onRecordTapped(); // toggle fn - guarded so this only ever starts
  bleCompanionSendStatus();
}

static void bleOnStopRecording() {
  if (recState != REC_IDLE) onRecordTapped(); // toggle fn - guarded so this only ever stops
  bleCompanionSendStatus();
}

static void bleOnPauseResume() {
  onPauseTapped(); // already a natural toggle; no-op while idle
  bleCompanionSendStatus();
}

// Runs on the main loop() task (via bleCompanionTick()), same "only touch
// the SD card while idle" caution already used for the free space bar -
// a directory listing here means one stat() call per file plus a BLE
// notify per file, real work that would compete with I2S/DMA timing if
// attempted mid-recording.
static bool bleOnFileListRequest() {
  if (recState != REC_IDLE) {
    bleCompanionSendError(BLE_ERR_BUSY_RECORDING);
    return false;
  }
  DIR *dir = opendir("/sdcard");
  if (!dir) {
    bleCompanionSendError(BLE_ERR_SD_UNAVAILABLE);
    return false;
  }
  int sent = 0;
  struct dirent *entry;
  RecordingInfo info;
  while (sent < BLE_MAX_FILE_LIST_ENTRIES && (entry = readdir(dir)) != nullptr) {
    if (!getRecordingInfo(entry->d_name, &info)) continue;
    bleCompanionSendFileListEntry(info.name, info.size, info.duration, info.timestamp);
    sent++;
  }
  closedir(dir);
  return true;
}

static bool bleCanStartTransferSession() {
  return recState == REC_IDLE;
}

static String bleHttpFileListJson() {
  DIR *dir = opendir("/sdcard");
  String json = "[";
  bool first = true;
  if (dir) {
    struct dirent *entry;
    RecordingInfo info;
    while ((entry = readdir(dir)) != nullptr) {
      if (!getRecordingInfo(entry->d_name, &info)) continue;
      if (!first) json += ",";
      first = false;
      json += "{\"name\":\"" + String(info.name) + "\",\"size\":" + String(info.size) +
              ",\"duration\":" + String(info.duration) + ",\"timestamp\":" + String(info.timestamp) + "}";
    }
    closedir(dir);
  }
  json += "]";
  return json;
}

// ---------------------------------------------------------------------
// Setup orchestration - see recorder_app.h for why this is split across
// three calls at three different points in main.cpp's setup().
// ---------------------------------------------------------------------
void recorderAudioBringup() {
#if defined(BOARD_ES3C28P)
  initES8311Codec(); // I2C-only codec bring-up, before I2S capture starts below
#endif
  installI2S();
  calibrateChannelOffset();
}

void recorderFinishSetup() {
  recordingsScreenInit(goToRecorderMain);
  loadTranscriptionSettings();

  delay(1500); // let SD power stabilize after a fresh power-up
  bool sdOk = mountSD();
  if (!sdOk) {
    Serial.println("SD mount failed");
  } else {
    cachedFreeBytes = getSDFreeBytes(); // seeds the BLE status cache before the first idle refresh runs
  }

  BleCompanionHooks hooks;
  hooks.getRecordingState = bleGetRecordingState;
  hooks.getFreeSpace = bleGetFreeSpace;
  hooks.onStartRecording = bleOnStartRecording;
  hooks.onStopRecording = bleOnStopRecording;
  hooks.onPauseResume = bleOnPauseResume;
  hooks.onFileListRequest = bleOnFileListRequest;
  hooks.canStartTransferSession = bleCanStartTransferSession;
  hooks.httpFileListJson = bleHttpFileListJson;
  bleCompanionInit(hooks);
}

void recorderAnalyzeMostRecentIfPresent() {
  if (findMostRecentRecording()) {
    Serial.printf("Most recent recording: %s\n", lastRecordingPath);
    analyzeLastRecording();
  }
}
