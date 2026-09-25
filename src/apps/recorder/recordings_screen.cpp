#include "apps/recorder/recordings_screen.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <ctime>

#include "apps/recorder/recorder_layout.h"
#include "apps/recorder/recording.h"
#include "apps/recorder/transcription.h"
#include "core/display.h"
#include "core/ui_widgets.h"

static ExitRecordingsFn onExitToRecorderMain = nullptr;

enum RecordingsScreen { RECORDINGS_LIST,
                         RECORDINGS_DETAIL };
static RecordingsScreen screen = RECORDINGS_LIST;

static char selectedRecordingName[40] = "";

static void drawList();
static void drawDetail();

static void goToList() {
  screen = RECORDINGS_LIST;
  drawList();
}

// ---- List screen ----
static void drawList() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawBackButton();
  drawCenteredLine("Recordings", NOTIF_BAR_H + 8, COLOR_AQUA);

  if (recordingsListCount == 0) {
    drawCenteredLine("No recordings found", ROW_LIST_Y + 20, COLOR_TEXT);
    return;
  }
  for (int i = 0; i < recordingsListCount; i++) {
    char label[19];
    strncpy(label, recordingsList[i].name, sizeof(label) - 1);
    label[sizeof(label) - 1] = 0;
    drawListRow(i, label, false, false);
  }
}

static void handleListTouch(int x, int y) {
  if (handleBackButtonTouch(x, y)) {
    if (onExitToRecorderMain) onExitToRecorderMain();
    return;
  }
  if (recordingsListCount == 0) return;
  int idx = (y - ROW_LIST_Y) / ROW_H;
  if (idx < 0 || idx >= recordingsListCount) return;

  strncpy(selectedRecordingName, recordingsList[idx].name, sizeof(selectedRecordingName) - 1);
  selectedRecordingName[sizeof(selectedRecordingName) - 1] = 0;
  lastUploadStatus = UPLOAD_NONE;
  screen = RECORDINGS_DETAIL;
  drawDetail();
}

// ---- Detail screen ----
static void drawDetail() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawBackButton();

  // Re-find this recording's info in the list still held from when the
  // list screen was drawn (cheap - no fresh SD access needed just to
  // redraw this screen, e.g. after toggling upload status).
  RecordingInfo *info = nullptr;
  for (int i = 0; i < recordingsListCount; i++) {
    if (strcmp(recordingsList[i].name, selectedRecordingName) == 0) {
      info = &recordingsList[i];
      break;
    }
  }
  if (!info) {
    goToList();
    return;
  }

  drawCenteredLine(info->name, REC_DETAIL_NAME_Y, COLOR_AQUA);

  char buf[48];
  snprintf(buf, sizeof(buf), "Size: %.1f MB", info->size / (1024.0f * 1024.0f));
  drawCenteredLine(buf, REC_DETAIL_SIZE_Y, COLOR_TEXT);

  snprintf(buf, sizeof(buf), "Duration: %u:%02u", info->duration / 60, info->duration % 60);
  drawCenteredLine(buf, REC_DETAIL_DURATION_Y, COLOR_TEXT);

  time_t ts = (time_t)info->timestamp;
  struct tm tmStruct;
  localtime_r(&ts, &tmStruct);
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d", tmStruct.tm_mon + 1, tmStruct.tm_mday,
           tmStruct.tm_year + 1900, tmStruct.tm_hour, tmStruct.tm_min);
  drawCenteredLine(buf, REC_DETAIL_DATE_Y, COLOR_TEXT);

  bool configured = transcriptionIsConfigured();
  // WiFi.status() == WL_CONNECTED is required, not just "enabled" -
  // calling WiFiClient::connect() while WiFi was never turned on crashed
  // with an lwIP assert on real hardware - see CLAUDE.md.
  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  bool canUpload = configured && wifiConnected && recState == REC_IDLE && lastUploadStatus != UPLOAD_IN_PROGRESS;
  uint16_t btnColor = canUpload ? COLOR_AQUA : COLOR_GREY_DIM;
  tft.fillRoundRect(30, REC_DETAIL_UPLOAD_BTN_Y, SCREEN_W - 60, REC_DETAIL_UPLOAD_BTN_H, 10, lerp565(COLOR_BG, btnColor, 0.2f));
  tft.drawRoundRect(30, REC_DETAIL_UPLOAD_BTN_Y, SCREEN_W - 60, REC_DETAIL_UPLOAD_BTN_H, 10, btnColor);
  drawTextIn("Upload to Scriberr", 30, SCREEN_W - 60, REC_DETAIL_UPLOAD_BTN_Y + REC_DETAIL_UPLOAD_BTN_H / 2 - 8,
             canUpload ? COLOR_WHITE : COLOR_GREY_DIM);

  if (lastUploadStatus == UPLOAD_IN_PROGRESS) {
    drawCenteredLine("Uploading...", REC_DETAIL_STATUS_Y, COLOR_AQUA);
  } else if (lastUploadStatus == UPLOAD_SUCCESS) {
    drawCenteredLine(lastUploadMessage, REC_DETAIL_STATUS_Y, COLOR_AQUA);
  } else if (lastUploadStatus == UPLOAD_FAILED) {
    drawCenteredLine(lastUploadMessage, REC_DETAIL_STATUS_Y, COLOR_RED);
  } else if (!configured) {
    drawCenteredLine("Set up Scriberr in Settings first", REC_DETAIL_STATUS_Y, COLOR_GREY_DIM);
  } else if (!wifiConnected) {
    drawCenteredLine("Connect to WiFi first (Settings)", REC_DETAIL_STATUS_Y, COLOR_GREY_DIM);
  }
}

static void handleDetailTouch(int x, int y) {
  if (handleBackButtonTouch(x, y)) {
    goToList();
    return;
  }
  if (y >= REC_DETAIL_UPLOAD_BTN_Y && y <= REC_DETAIL_UPLOAD_BTN_Y + REC_DETAIL_UPLOAD_BTN_H) {
    bool canUpload = transcriptionIsConfigured() && WiFi.status() == WL_CONNECTED &&
                      recState == REC_IDLE && lastUploadStatus != UPLOAD_IN_PROGRESS;
    if (canUpload) {
      uploadRecording(selectedRecordingName);
    }
  }
}

void redrawRecordingsDetailScreenIfShowing() {
  if (screen == RECORDINGS_DETAIL) drawDetail();
}

void recordingsScreenInit(ExitRecordingsFn onExit) {
  onExitToRecorderMain = onExit;
}

void enterRecordingsScreen() {
  screen = RECORDINGS_LIST;
  loadRecordingsList();
  drawList();
}

void recordingsHandleTouch(int x, int y) {
  switch (screen) {
    case RECORDINGS_LIST: handleListTouch(x, y); break;
    case RECORDINGS_DETAIL: handleDetailTouch(x, y); break;
  }
}
