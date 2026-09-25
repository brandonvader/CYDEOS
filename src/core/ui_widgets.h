#pragma once

#include <cstdint>

#include "boards/board_config.h"

// ---------------------------------------------------------------------
// Colors (black bg, aqua = active/idle accent, red = recording/danger)
// ---------------------------------------------------------------------
#define COLOR_BG 0x0000
#define COLOR_AQUA 0x07FF
#define COLOR_AQUA_DIM 0x0410
#define COLOR_RED 0xF800
#define COLOR_GREY_DIM 0x2104
#define COLOR_TEXT 0xC618
#define COLOR_WHITE 0xFFFF
#define COLOR_GREEN 0x07E0
#define COLOR_YELLOW 0xFFE0

// ---------------------------------------------------------------------
// OS-chrome layout - shared by the shell and every core settings screen.
// Derived from board_config.h's SCREEN_W/SCREEN_H via UI_SCALE(), so this
// whole block applies to any board without further changes.
// ---------------------------------------------------------------------
#define NOTIF_BAR_H UI_SCALE(32)

// Generic "back to the screen that opened this one" button, used by every
// settings sub-screen.
#define BACK_BTN_X UI_SCALE(8)
#define BACK_BTN_Y (NOTIF_BAR_H + UI_SCALE(6))
#define BACK_BTN_W UI_SCALE(70)
#define BACK_BTN_H UI_SCALE(26)

// Launcher - simple vertical list of app tiles. Also reused by Settings'
// own home screen (WiFi/Time tiles).
#define LAUNCHER_TILE_W UI_SCALE(260)
#define LAUNCHER_TILE_H UI_SCALE(90)
#define LAUNCHER_TILE_GAP UI_SCALE(24)
#define LAUNCHER_TILE_X ((SCREEN_W - LAUNCHER_TILE_W) / 2)
#define LAUNCHER_TILE1_Y (NOTIF_BAR_H + UI_SCALE(70))
#define LAUNCHER_TILE2_Y (LAUNCHER_TILE1_Y + LAUNCHER_TILE_H + LAUNCHER_TILE_GAP)
#define LAUNCHER_TILE3_Y (LAUNCHER_TILE2_Y + LAUNCHER_TILE_H + LAUNCHER_TILE_GAP)

// Generic row-list layout, reused by the timezone picker, saved-networks
// list, and (once ported) the Recordings list.
#define ROW_LIST_Y (NOTIF_BAR_H + UI_SCALE(40))
#define ROW_H UI_SCALE(40)

// Settings shade - slides down from below the notification bar. Sized to
// cover the button row (whose glow can extend a bit past its own radius)
// without reaching further down, so closing it only ever needs to redraw
// what's actually underneath it.
#define SHADE_Y NOTIF_BAR_H
#define SHADE_H UI_SCALE(170)
#define SLIDER_X UI_SCALE(40)
#define SLIDER_W (SCREEN_W - 2 * SLIDER_X)
#define SLIDER_H UI_SCALE(8)
#define SLIDER_Y (SHADE_Y + UI_SCALE(70))
#define SLIDER_HANDLE_R UI_SCALE(14)
#define SLIDER_HIT_MARGIN UI_SCALE(20)

// Bottom-edge swipe-up-to-launcher gesture zone.
#define BOTTOM_EDGE_ZONE_Y (SCREEN_H - UI_SCALE(20))
#define SWIPE_UP_THRESHOLD UI_SCALE(50)

// Generic text-entry field (WiFi password, Scriberr fields, etc.)
#define TEXT_FIELD_Y (NOTIF_BAR_H + UI_SCALE(56))
#define TEXT_FIELD_H UI_SCALE(40)
#define PW_CHECKBOX_Y (TEXT_FIELD_Y + TEXT_FIELD_H + UI_SCALE(20))
#define PW_CHECKBOX_SIZE UI_SCALE(24)
#define PW_CONNECT_BTN_Y (PW_CHECKBOX_Y + UI_SCALE(40))
#define PW_CONNECT_BTN_H UI_SCALE(40)

// On-screen keyboard
#define KB_Y (PW_CONNECT_BTN_Y + PW_CONNECT_BTN_H + UI_SCALE(16))
#define KB_ROW_H UI_SCALE(38)
#define KB_KEY_GAP UI_SCALE(2)
#define KB_ROWS 4

// Scan results floating panel (WiFi)
#define SCAN_PANEL_X UI_SCALE(12)
#define SCAN_PANEL_Y (NOTIF_BAR_H + UI_SCALE(10))
#define SCAN_PANEL_W (SCREEN_W - 2 * SCAN_PANEL_X)
#define SCAN_PANEL_H UI_SCALE(400)
#define SCAN_ROW_H UI_SCALE(42)
#define MAX_SCAN_RESULTS 9

// ---------------------------------------------------------------------
// Shared drawing helpers
// ---------------------------------------------------------------------
uint16_t lerp565(uint16_t c1, uint16_t c2, float t);

void drawCenteredLine(const char *text, int y, uint16_t color);
void drawTextIn(const char *text, int x, int w, int y, uint16_t color);

void drawBackButton();
bool handleBackButtonTouch(int x, int y);

void drawSignalIcon(int x, int y, int rssi);
void drawLockIcon(int x, int y);
void drawCheckbox(int x, int y, bool checked);

// Generic vertical list row background/selection highlight, shared by the
// timezone picker, saved-networks list, and Recordings list.
void drawListRow(int index, const char *label, bool selected, bool showDot);
