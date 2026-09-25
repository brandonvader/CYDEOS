#include "apps/recorder/recorder_ui.h"

#include <Arduino.h>
#include <cstring>

#include "apps/recorder/audio.h"
#include "apps/recorder/recorder_layout.h"
#include "apps/recorder/recording.h"
#include "apps/recorder/storage.h"
#include "core/display.h"
#include "core/ui_widgets.h"

// ---------------------------------------------------------------------
// Bottom corner icon buttons: "fenced" into the screen's own bottom
// corners rather than drawn as a free-floating box - only the top edge,
// the chamfer (the corner nearest the *other* button, cut at 45deg so the
// pair reads as a matched set), and the inner edge (facing back toward
// the rest of the screen) are drawn. The outer side edge and the bottom
// edge are deliberately never drawn: they sit exactly on the display's
// own physical edges (RECORDER_FOLDER_BTN_X/RECORDER_SETTINGS_BTN_X and
// RECORDER_ICON_BTN_Y+SIZE=SCREEN_H - see recorder_layout.h), so a border
// there would just be a redundant line right at the bezel.
// ---------------------------------------------------------------------
static void drawIconButtonFence(int x, int y, int size, bool chamferOnLeft) {
  int chamfer = size / 4;
  int bottom = y + size;
  if (chamferOnLeft) {
    // Inner edge on the left, chamfer cuts the top-left corner.
    tft.drawFastHLine(x + chamfer, y, size - chamfer, COLOR_AQUA);       // top
    tft.drawLine(x, y + chamfer, x + chamfer, y, COLOR_AQUA);            // chamfer
    tft.drawFastVLine(x, y + chamfer, bottom - (y + chamfer), COLOR_AQUA); // inner
  } else {
    // Inner edge on the right, chamfer cuts the top-right corner.
    tft.drawFastHLine(x, y, size - chamfer, COLOR_AQUA);                          // top
    tft.drawLine(x + size - chamfer, y, x + size, y + chamfer, COLOR_AQUA);       // chamfer
    tft.drawFastVLine(x + size, y + chamfer, bottom - (y + chamfer), COLOR_AQUA); // inner
  }
}

static void drawFolderIcon(int cx, int cy, int size, uint16_t color) {
  int w = size, h = (size * 3) / 4;
  int x = cx - w / 2;
  int y = cy - h / 2 + size / 10; // nudge down slightly to leave room for the tab above
  int tabW = (w * 2) / 5;
  int tabH = h / 4;
  tft.fillRoundRect(x, y - tabH + 2, tabW, tabH, 1, color);
  tft.drawRoundRect(x, y, w, h, 2, color);
}

static void drawHamburgerIcon(int cx, int cy, int size, uint16_t color) {
  int w = size;
  int x = cx - w / 2;
  int gap = size / 3;
  tft.fillRect(x, cy - gap, w, 2, color);
  tft.fillRect(x, cy, w, 2, color);
  tft.fillRect(x, cy + gap, w, 2, color);
}

// The left (folder -> Recordings) and right (hamburger -> Recorder
// Settings) buttons - the folder's inner corner is top-right, the
// hamburger's is top-left, so they chamfer toward each other.
static void drawRecorderIconButtons() {
  int cy = RECORDER_ICON_BTN_Y + RECORDER_ICON_BTN_SIZE / 2;
  int iconSize = (RECORDER_ICON_BTN_SIZE * 5) / 10;

  drawIconButtonFence(RECORDER_FOLDER_BTN_X, RECORDER_ICON_BTN_Y, RECORDER_ICON_BTN_SIZE, false);
  drawFolderIcon(RECORDER_FOLDER_BTN_X + RECORDER_ICON_BTN_SIZE / 2, cy, iconSize, COLOR_AQUA);

  drawIconButtonFence(RECORDER_SETTINGS_BTN_X, RECORDER_ICON_BTN_Y, RECORDER_ICON_BTN_SIZE, true);
  drawHamburgerIcon(RECORDER_SETTINGS_BTN_X + RECORDER_ICON_BTN_SIZE / 2, cy, iconSize, COLOR_AQUA);
}

static void drawRecordIcon(int cx, int cy, uint16_t color) {
  tft.fillCircle(cx, cy, 20, color);
}

static void drawPauseIcon(int cx, int cy, uint16_t color) {
  int barW = 14, barH = 46, gap = 16;
  tft.fillRoundRect(cx - gap / 2 - barW, cy - barH / 2, barW, barH, 4, color);
  tft.fillRoundRect(cx + gap / 2, cy - barH / 2, barW, barH, 4, color);
}

static void drawStopIcon(int cx, int cy, uint16_t color) {
  int half = 16;
  tft.fillRoundRect(cx - half, cy - half, half * 2, half * 2, 4, color);
}

static void drawPlayIcon(int cx, int cy, uint16_t color) {
  int h = UI_SCALE(18), w = UI_SCALE(28);
  tft.fillTriangle(cx - w / 2, cy - h, cx - w / 2, cy + h, cx + w / 2, cy, color);
}

static void drawGlowButton(int cx, int cy, uint16_t glowColor, bool glowing) {
  int clearR = BTN_RADIUS + UI_SCALE(24);
  tft.fillCircle(cx, cy, clearR, COLOR_BG);

  if (glowing) {
    for (int i = 6; i >= 0; i--) {
      float t = i / 6.0f;
      int r = BTN_RADIUS + (int)(t * UI_SCALE(18));
      uint16_t c = lerp565(COLOR_BG, glowColor, 1.0f - t * 0.8f);
      tft.fillCircle(cx, cy, r, c);
    }
  } else {
    tft.fillCircle(cx, cy, BTN_RADIUS, lerp565(COLOR_BG, glowColor, 0.12f));
    tft.drawCircle(cx, cy, BTN_RADIUS, lerp565(COLOR_BG, glowColor, 0.35f));
  }
}

void renderButtons() {
  bool recording = (recState == REC_RECORDING);
  bool paused = (recState == REC_PAUSED);
  bool sessionActive = recording || paused;

  // Both buttons glow throughout the whole active session (recording or
  // paused), not just while actively recording - a session is still
  // "live" while paused, just not currently writing. The icon (not the
  // glow) is what communicates the specific current action.
  drawGlowButton(BTN_RECORD_CX, BTN_ROW_CENTER_Y, COLOR_RED, sessionActive);
  if (sessionActive) {
    drawStopIcon(BTN_RECORD_CX, BTN_ROW_CENTER_Y, COLOR_WHITE);
  } else {
    drawRecordIcon(BTN_RECORD_CX, BTN_ROW_CENTER_Y, lerp565(COLOR_BG, COLOR_RED, 0.5f));
  }

  drawGlowButton(BTN_PAUSE_CX, BTN_ROW_CENTER_Y, COLOR_AQUA, sessionActive);
  if (paused) {
    drawPlayIcon(BTN_PAUSE_CX, BTN_ROW_CENTER_Y, COLOR_WHITE);
  } else {
    drawPauseIcon(BTN_PAUSE_CX, BTN_ROW_CENTER_Y, sessionActive ? COLOR_WHITE : COLOR_GREY_DIM);
  }
}

void drawFreeSpaceBar() {
  uint64_t freeBytes = getSDFreeBytes();
  cachedFreeBytes = freeBytes; // BLE status responses read this instead of touching the SD card themselves
  float freeGB = freeBytes / (1024.0f * 1024.0f * 1024.0f);
  float bytesPerSec = SAMPLE_RATE * 2.0f; // 16-bit mono PCM
  float freeHours = freeBytes / bytesPerSec / 3600.0f;

  tft.fillRect(0, FREESPACE_BAR_Y - 4, SCREEN_W, FREESPACE_BAR_H + 8, COLOR_BG);
  tft.drawRoundRect(20, FREESPACE_BAR_Y, SCREEN_W - 40, FREESPACE_BAR_H, 8, COLOR_AQUA_DIM);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(UI_TEXT_SIZE_NORMAL);
  char buf[48];
  snprintf(buf, sizeof(buf), "%.1fGB free ~%.1fhrs", freeGB, freeHours);
  tft.setCursor(32, FREESPACE_BAR_Y + 10);
  tft.print(buf);
}

void drawSpectrum(bool active) {
  tft.fillRect(0, SPECTRUM_Y, SCREEN_W, SPECTRUM_H, COLOR_BG);
  if (!active) return;
  int barW = (SCREEN_W - 40) / NUM_BARS - 4;
  int baseY = SPECTRUM_Y + SPECTRUM_H;
  int maxH = SPECTRUM_H - 10; // clamp - barHeights[] isn't screen-size-aware (see audio.h)
  for (int b = 0; b < NUM_BARS; b++) {
    int x = 20 + b * (barW + 4);
    int h = barHeights[b];
    if (h > maxH) h = maxH;
    if (h < 2) h = 2;
    tft.fillRoundRect(x, baseY - h, barW, h, 2, COLOR_AQUA);
  }
}

void clearSpectrumArea() {
  tft.fillRect(0, SPECTRUM_Y, SCREEN_W, SPECTRUM_H, COLOR_BG);
}

void showPausedOverlay() {
  tft.fillRect(0, SPECTRUM_Y, SCREEN_W, 24, COLOR_BG);
  drawCenteredLine("-Paused-", SPECTRUM_Y + 4, COLOR_AQUA);
}

void showWritingMessage() {
  clearSpectrumArea();
  drawCenteredLine("-Writing-", SPECTRUM_Y + SPECTRUM_H / 2 - 8, COLOR_RED);
}

void showReadyMessage() {
  clearSpectrumArea();
  drawCenteredLine("-Ready-", SPECTRUM_Y + SPECTRUM_H / 2 - 8, COLOR_AQUA_DIM);
}

void showRecordingStats(uint32_t dataSize) {
  clearSpectrumArea();

  const char *base = strrchr(lastRecordingPath, '/');
  base = base ? base + 1 : lastRecordingPath;

  uint32_t byteRate = SAMPLE_RATE * sizeof(int16_t); // 16-bit mono
  uint32_t totalFileSize = dataSize + WAV_HEADER_SIZE;
  uint32_t totalSeconds = byteRate > 0 ? dataSize / byteRate : 0;

  char sizeStr[16], durStr[16], bitrateStr[24];
  formatFileSize(totalFileSize, sizeStr, sizeof(sizeStr));
  formatDuration(totalSeconds, durStr, sizeof(durStr));
  snprintf(bitrateStr, sizeof(bitrateStr), "%lu kbps", (unsigned long)(byteRate * 8 / 1000));

  int lineY = SPECTRUM_Y + 16;
  const int lineSpacing = 34;
  drawCenteredLine(base, lineY, COLOR_TEXT);
  lineY += lineSpacing;
  drawCenteredLine(sizeStr, lineY, COLOR_TEXT);
  lineY += lineSpacing;
  drawCenteredLine(durStr, lineY, COLOR_TEXT);
  lineY += lineSpacing;
  drawCenteredLine(bitrateStr, lineY, COLOR_TEXT);
}

void refreshRecorderSpectrumArea() {
  if (recState == REC_RECORDING) {
    drawSpectrum(true);
  } else if (recState == REC_PAUSED) {
    // The last-drawn bars aren't preserved across an app switch (the
    // whole content area gets cleared), so this just shows the label on
    // an empty area rather than genuinely frozen bars in that case.
    drawSpectrum(false);
    showPausedOverlay();
  } else if (showingStats) {
    showRecordingStats(lastRecordingDataSize);
  } else {
    showReadyMessage();
  }
}

void drawRecorderMainScreen() {
  // Full clear first - the individual draw calls below only patch their
  // own specific regions, which was fine when this only ever redrew over
  // its own prior state. Coming back from another app left stale
  // graphics stranded in the gaps between them otherwise.
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  renderButtons();
  drawFreeSpaceBar();
  refreshRecorderSpectrumArea();
  drawRecorderIconButtons();
}
