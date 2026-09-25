#include "core/ui_widgets.h"

#include "core/display.h"

uint16_t lerp565(uint16_t c1, uint16_t c2, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  uint8_t r = r1 + (uint8_t)((r2 - r1) * t);
  uint8_t g = g1 + (uint8_t)((g2 - g1) * t);
  uint8_t b = b1 + (uint8_t)((b2 - b1) * t);
  return (r << 11) | (g << 5) | b;
}

void drawCenteredLine(const char *text, int y, uint16_t color) {
  tft.setTextColor(color, COLOR_BG);
  tft.setTextSize(UI_TEXT_SIZE_NORMAL);
  int w = tft.textWidth(text);
  tft.setCursor((SCREEN_W - w) / 2, y);
  tft.print(text);
}

void drawTextIn(const char *text, int x, int w, int y, uint16_t color) {
  tft.setTextColor(color, COLOR_BG);
  tft.setTextSize(UI_TEXT_SIZE_NORMAL);
  int tw = tft.textWidth(text);
  tft.setCursor(x + (w - tw) / 2, y);
  tft.print(text);
}

void drawBackButton() {
  drawTextIn("< Back", BACK_BTN_X, BACK_BTN_W, BACK_BTN_Y, COLOR_AQUA);
}

bool handleBackButtonTouch(int x, int y) {
  return x >= BACK_BTN_X && x <= BACK_BTN_X + BACK_BTN_W &&
         y >= BACK_BTN_Y - 10 && y <= BACK_BTN_Y + BACK_BTN_H;
}

void drawSignalIcon(int x, int y, int rssi) {
  int filled;
  if (rssi > -55) filled = 4;
  else if (rssi > -65) filled = 3;
  else if (rssi > -75) filled = 2;
  else filled = 1;

  int barW = 3, gap = 2;
  for (int i = 0; i < 4; i++) {
    int barH = 4 + i * 3;
    int bx = x + i * (barW + gap);
    int by = y + (13 - barH);
    tft.fillRect(bx, by, barW, barH, i < filled ? COLOR_AQUA : COLOR_GREY_DIM);
  }
}

void drawLockIcon(int x, int y) {
  tft.drawRoundRect(x + 2, y, 8, 7, 3, COLOR_TEXT);
  tft.fillRoundRect(x, y + 5, 12, 9, 2, COLOR_TEXT);
}

void drawCheckbox(int x, int y, bool checked) {
  tft.drawRoundRect(x, y, PW_CHECKBOX_SIZE, PW_CHECKBOX_SIZE, 4, COLOR_AQUA);
  if (checked) {
    tft.fillRoundRect(x + 4, y + 4, PW_CHECKBOX_SIZE - 8, PW_CHECKBOX_SIZE - 8, 2, COLOR_AQUA);
  } else {
    tft.fillRect(x + 1, y + 1, PW_CHECKBOX_SIZE - 2, PW_CHECKBOX_SIZE - 2, COLOR_BG);
  }
}

void drawMenuTile(int y, const char *label, uint16_t color) {
  tft.fillRoundRect(LAUNCHER_TILE_X, y, LAUNCHER_TILE_W, LAUNCHER_TILE_H, 12, lerp565(COLOR_BG, color, 0.15f));
  tft.drawRoundRect(LAUNCHER_TILE_X, y, LAUNCHER_TILE_W, LAUNCHER_TILE_H, 12, color);
  drawCenteredLine(label, y + LAUNCHER_TILE_H / 2 - 8, COLOR_WHITE);
}

void drawListRow(int index, const char *label, bool selected, bool showDot) {
  int y = ROW_LIST_Y + index * ROW_H;
  uint16_t rowBg = selected ? lerp565(COLOR_BG, COLOR_AQUA, 0.15f) : COLOR_BG;
  tft.fillRect(0, y, SCREEN_W, ROW_H, rowBg);
  tft.setTextColor(selected ? COLOR_AQUA : COLOR_TEXT, rowBg);
  tft.setTextSize(UI_TEXT_SIZE_NORMAL);
  tft.setCursor(24, y + 10);
  tft.print(label);
  if (showDot) tft.fillCircle(SCREEN_W - 30, y + ROW_H / 2, 6, COLOR_AQUA);
  if (index > 0) tft.drawFastHLine(12, y, SCREEN_W - 24, COLOR_GREY_DIM);
}
