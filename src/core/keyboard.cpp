#include "core/keyboard.h"

#include <cstring>

#include "core/display.h"
#include "core/ui_widgets.h"

static char *kbTarget = nullptr;
static size_t kbTargetSize = 0;
static int *kbRevealIndex = nullptr;
static KeyboardRedrawFn kbOnRedraw = nullptr;
static KeyboardDoneFn kbOnDone = nullptr;
static bool kbShift = false;
static bool kbSymbols = false;

void openKeyboardFor(char *target, size_t targetSize, int *revealIndex,
                      KeyboardRedrawFn onRedraw, KeyboardDoneFn onDone) {
  kbTarget = target;
  kbTargetSize = targetSize;
  kbRevealIndex = revealIndex;
  kbOnRedraw = onRedraw;
  kbOnDone = onDone;
  kbShift = false;
  kbSymbols = false;
}

// Appends into whichever buffer kbTarget currently points at.
static void appendChar(char c) {
  if (!kbTarget) return;
  int len = strlen(kbTarget);
  if (len < (int)kbTargetSize - 1) {
    kbTarget[len] = c;
    kbTarget[len + 1] = 0;
    if (kbRevealIndex) *kbRevealIndex = len; // show this char in plain text until the next keystroke
  }
}

static void redraw() {
  if (kbOnRedraw) kbOnRedraw();
}

static const char *kbRow1() { return kbSymbols ? "1234567890" : (kbShift ? "QWERTYUIOP" : "qwertyuiop"); }
static const char *kbRow2() { return kbSymbols ? "-_=+!@#$%" : (kbShift ? "ASDFGHJKL" : "asdfghjkl"); }
static const char *kbRow3Mid() { return kbSymbols ? "^&*()./?;" : (kbShift ? "ZXCVBNM" : "zxcvbnm"); }

static void drawKeyboardRow(int y, const char *keys) {
  int len = strlen(keys);
  int margin = 4;
  int keyW = (SCREEN_W - 2 * margin - (len - 1) * KB_KEY_GAP) / len;
  int rowH = KB_ROW_H - KB_KEY_GAP;
  for (int i = 0; i < len; i++) {
    int x = margin + i * (keyW + KB_KEY_GAP);
    tft.fillRoundRect(x, y, keyW, rowH, 4, lerp565(COLOR_BG, COLOR_AQUA_DIM, 0.5f));
    char label[2] = {keys[i], 0};
    drawTextIn(label, x, keyW, y + rowH / 2 - 8, COLOR_WHITE);
  }
}

static int keyboardRowHitTest(int touchX, int touchY, int rowY, const char *keys) {
  int rowH = KB_ROW_H - KB_KEY_GAP;
  if (touchY < rowY || touchY > rowY + rowH) return -1;
  int len = strlen(keys);
  int margin = 4;
  int keyW = (SCREEN_W - 2 * margin - (len - 1) * KB_KEY_GAP) / len;
  int i = (touchX - margin) / (keyW + KB_KEY_GAP);
  if (i < 0 || i >= len) return -1;
  return i;
}

void drawKeyboard() {
  int rowH = KB_ROW_H - KB_KEY_GAP;
  drawKeyboardRow(KB_Y, kbRow1());
  drawKeyboardRow(KB_Y + KB_ROW_H, kbRow2());

  int sideW = 46;
  int row3Y = KB_Y + KB_ROW_H * 2;
  uint16_t shiftColor = (kbShift && !kbSymbols) ? COLOR_AQUA : lerp565(COLOR_BG, COLOR_AQUA_DIM, 0.5f);
  tft.fillRoundRect(4, row3Y, sideW, rowH, 4, shiftColor);
  drawTextIn("^", 4, sideW, row3Y + rowH / 2 - 8, COLOR_WHITE);
  tft.fillRoundRect(SCREEN_W - 4 - sideW, row3Y, sideW, rowH, 4, lerp565(COLOR_BG, COLOR_RED, 0.3f));
  drawTextIn("<-", SCREEN_W - 4 - sideW, sideW, row3Y + rowH / 2 - 8, COLOR_WHITE);

  const char *mid = kbRow3Mid();
  int midLen = strlen(mid);
  int midX = 4 + sideW + KB_KEY_GAP;
  int midW = SCREEN_W - 2 * (4 + sideW + KB_KEY_GAP);
  int keyW = (midW - (midLen - 1) * KB_KEY_GAP) / midLen;
  for (int i = 0; i < midLen; i++) {
    int x = midX + i * (keyW + KB_KEY_GAP);
    tft.fillRoundRect(x, row3Y, keyW, rowH, 4, lerp565(COLOR_BG, COLOR_AQUA_DIM, 0.5f));
    char label[2] = {mid[i], 0};
    drawTextIn(label, x, keyW, row3Y + rowH / 2 - 8, COLOR_WHITE);
  }

  int row4Y = KB_Y + KB_ROW_H * 3;
  int toggleW = 60, doneW = 80;
  tft.fillRoundRect(4, row4Y, toggleW, rowH, 4, lerp565(COLOR_BG, COLOR_AQUA_DIM, 0.5f));
  drawTextIn(kbSymbols ? "ABC" : "123", 4, toggleW, row4Y + rowH / 2 - 8, COLOR_WHITE);

  int spaceX = 4 + toggleW + KB_KEY_GAP;
  int spaceW = SCREEN_W - 2 * 4 - toggleW - doneW - 2 * KB_KEY_GAP;
  tft.fillRoundRect(spaceX, row4Y, spaceW, rowH, 4, lerp565(COLOR_BG, COLOR_AQUA_DIM, 0.3f));
  drawTextIn("space", spaceX, spaceW, row4Y + rowH / 2 - 8, COLOR_TEXT);

  int doneX = SCREEN_W - 4 - doneW;
  tft.fillRoundRect(doneX, row4Y, doneW, rowH, 4, COLOR_AQUA);
  drawTextIn("Done", doneX, doneW, row4Y + rowH / 2 - 8, COLOR_WHITE);
}

void handleKeyboardTouch(int x, int y) {
  int rowH = KB_ROW_H - KB_KEY_GAP;

  int idx = keyboardRowHitTest(x, y, KB_Y, kbRow1());
  if (idx >= 0) {
    appendChar(kbRow1()[idx]);
    redraw();
    return;
  }
  idx = keyboardRowHitTest(x, y, KB_Y + KB_ROW_H, kbRow2());
  if (idx >= 0) {
    appendChar(kbRow2()[idx]);
    redraw();
    return;
  }

  int row3Y = KB_Y + KB_ROW_H * 2;
  if (y >= row3Y && y <= row3Y + rowH) {
    int sideW = 46;
    if (x <= 4 + sideW) {
      if (!kbSymbols) {
        kbShift = !kbShift;
        drawKeyboard();
      }
      return;
    }
    if (x >= SCREEN_W - 4 - sideW) {
      if (kbTarget) {
        int len = strlen(kbTarget);
        if (len > 0) kbTarget[len - 1] = 0;
      }
      if (kbRevealIndex) *kbRevealIndex = -1;
      redraw();
      return;
    }
    const char *mid = kbRow3Mid();
    int midLen = strlen(mid);
    int midX = 4 + sideW + KB_KEY_GAP;
    int midW = SCREEN_W - 2 * (4 + sideW + KB_KEY_GAP);
    int keyW = (midW - (midLen - 1) * KB_KEY_GAP) / midLen;
    int i = (x - midX) / (keyW + KB_KEY_GAP);
    if (i >= 0 && i < midLen) {
      appendChar(mid[i]);
      redraw();
    }
    return;
  }

  int row4Y = KB_Y + KB_ROW_H * 3;
  if (y >= row4Y && y <= row4Y + rowH) {
    int toggleW = 60, doneW = 80;
    if (x <= 4 + toggleW) {
      kbSymbols = !kbSymbols;
      kbShift = false;
      drawKeyboard();
      return;
    }
    if (x >= SCREEN_W - 4 - doneW) {
      if (kbOnDone) kbOnDone();
      return;
    }
    appendChar(' ');
    redraw();
  }
}
