#include "core/shell.h"

#include <Arduino.h>
#include <cstring>

#include "boards/board_config.h"
#include "core/battery.h"
#include "core/ble_companion.h"
#include "core/clock.h"
#include "core/display.h"
#include "core/ui_widgets.h"
#include "core/wifi.h"

// ---------------------------------------------------------------------
// App shell state
// ---------------------------------------------------------------------
enum ActiveApp { APP_RECORDER,
                  APP_LAUNCHER,
                  APP_SETTINGS };
static ActiveApp activeApp = APP_RECORDER;

// The Recorder app registers itself here - see shellRegisterRecorderApp().
// Deliberately typed as the generic core/app.h interface, not a
// recorder-specific type: the shell doesn't otherwise know or care that
// this happens to be the recorder. recorderBackgroundTick is the one
// genuinely recorder-specific exception (see its declaration below) - the
// "keeps recording regardless of which app is on screen" behavior the
// spec calls out as an OS-level special case, not a general app
// capability every app gets.
static const CydeosApp *recorderApp = nullptr;
static void (*recorderBackgroundTick)() = nullptr;

enum SettingsTopScreen { SETTINGS_HOME,
                          SETTINGS_WIFI,
                          SETTINGS_TIME };
static SettingsTopScreen settingsTopScreen = SETTINGS_HOME;

static bool shadeOpen = false;

// A touch-down can only ever be the start of one gesture at a time -
// dragging the brightness slider, or a swipe-up-from-bottom-edge to open
// the launcher.
enum GestureMode { GESTURE_NONE,
                    GESTURE_SLIDER,
                    GESTURE_BOTTOM_SWIPE };
static GestureMode gestureMode = GESTURE_NONE;
static uint16_t gestureStartY = 0;

static uint32_t lastBatteryCheckMs = 0;

// ---------------------------------------------------------------------
// Notification bar
// ---------------------------------------------------------------------
static char lastTimeStr[10] = "";
static BatteryStatus lastDrawnBatteryStatus = (BatteryStatus)-1; // sentinel forces the first draw

static void drawNotifBar(bool force = false) {
  char dateStr[9], timeStr[10];
  getClockStrings(dateStr, sizeof(dateStr), timeStr, sizeof(timeStr));
  bool batteryChanged = (batteryStatus != lastDrawnBatteryStatus);
  if (!force && !batteryChanged && strcmp(timeStr, lastTimeStr) == 0) return;
  strcpy(lastTimeStr, timeStr);

  tft.fillRect(0, 0, SCREEN_W, NOTIF_BAR_H, COLOR_BG);
  tft.setTextColor(COLOR_AQUA, COLOR_BG);
  tft.setTextSize(UI_TEXT_SIZE_NORMAL);
  tft.setCursor(8, 8);
  tft.print(dateStr);
  int w = tft.textWidth(timeStr);
  int timeX = SCREEN_W - 8 - w;
  int iconX = timeX - 6 - (BATTERY_ICON_W + BATTERY_NUB_W);
  int iconY = (NOTIF_BAR_H - BATTERY_ICON_H) / 2;
  drawBatteryIcon(iconX, iconY, batteryStatus);
  lastDrawnBatteryStatus = batteryStatus;
  tft.setCursor(timeX, 8);
  tft.print(timeStr);
}

// ---------------------------------------------------------------------
// Settings shade - brightness slider only for now.
// ---------------------------------------------------------------------
static void drawShade() {
  tft.fillRect(0, SHADE_Y, SCREEN_W, SHADE_H, COLOR_BG);
  tft.drawFastHLine(0, SHADE_Y + SHADE_H - 1, SCREEN_W, COLOR_AQUA_DIM);

  drawCenteredLine("Brightness", SHADE_Y + 20, COLOR_TEXT);

  tft.fillRoundRect(SLIDER_X, SLIDER_Y, SLIDER_W, SLIDER_H, SLIDER_H / 2, COLOR_GREY_DIM);
  int handleX = SLIDER_X + (SLIDER_W * brightnessPercent) / 100;
  tft.fillCircle(handleX, SLIDER_Y + SLIDER_H / 2, SLIDER_HANDLE_R, COLOR_AQUA);

  char pctStr[6];
  snprintf(pctStr, sizeof(pctStr), "%d%%", brightnessPercent);
  drawCenteredLine(pctStr, SLIDER_Y + SLIDER_HANDLE_R + 20, COLOR_TEXT);
}

static bool touchIsOnSlider(int x, int y) {
  return y >= SLIDER_Y - SLIDER_HIT_MARGIN && y <= SLIDER_Y + SLIDER_H + SLIDER_HIT_MARGIN &&
         x >= SLIDER_X - SLIDER_HIT_MARGIN && x <= SLIDER_X + SLIDER_W + SLIDER_HIT_MARGIN;
}

static void updateBrightnessFromTouch(int x) {
  int pct = ((x - SLIDER_X) * 100) / SLIDER_W;
  applyBrightness(pct); // clamps internally
  drawShade();
}

// ---------------------------------------------------------------------
// Launcher + Settings app (WiFi/Time tiles)
// ---------------------------------------------------------------------
static void switchToApp(ActiveApp app);

static void drawLauncher() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawCenteredLine("Apps", NOTIF_BAR_H + 24, COLOR_AQUA);
  drawMenuTile(LAUNCHER_TILE1_Y, "Recorder", COLOR_RED);
  drawMenuTile(LAUNCHER_TILE2_Y, "Settings", COLOR_AQUA_DIM);
}

static void handleLauncherTouch(int x, int y) {
  if (x < LAUNCHER_TILE_X || x > LAUNCHER_TILE_X + LAUNCHER_TILE_W) return;
  if (y >= LAUNCHER_TILE1_Y && y <= LAUNCHER_TILE1_Y + LAUNCHER_TILE_H) {
    switchToApp(APP_RECORDER);
  } else if (y >= LAUNCHER_TILE2_Y && y <= LAUNCHER_TILE2_Y + LAUNCHER_TILE_H) {
    switchToApp(APP_SETTINGS);
  }
}

static void drawSettingsHome() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawCenteredLine("Settings", NOTIF_BAR_H + 24, COLOR_AQUA);
  drawMenuTile(LAUNCHER_TILE1_Y, "WiFi", COLOR_AQUA_DIM);
  drawMenuTile(LAUNCHER_TILE2_Y, "Time", COLOR_AQUA_DIM);
}

static void goToSettingsHome() {
  settingsTopScreen = SETTINGS_HOME;
  drawSettingsHome();
}

static void handleSettingsHomeTouch(int x, int y) {
  if (x < LAUNCHER_TILE_X || x > LAUNCHER_TILE_X + LAUNCHER_TILE_W) return;
  if (y >= LAUNCHER_TILE1_Y && y <= LAUNCHER_TILE1_Y + LAUNCHER_TILE_H) {
    settingsTopScreen = SETTINGS_WIFI;
    wifiEnterSettings();
  } else if (y >= LAUNCHER_TILE2_Y && y <= LAUNCHER_TILE2_Y + LAUNCHER_TILE_H) {
    settingsTopScreen = SETTINGS_TIME;
    clockEnterSettings();
  }
}

static void enterSettingsApp() {
  goToSettingsHome();
}

static void switchToApp(ActiveApp app) {
  if (activeApp == APP_RECORDER && app != APP_RECORDER && recorderApp && recorderApp->onStop) {
    recorderApp->onStop();
  }
  activeApp = app;
  if (app == APP_RECORDER) {
    if (recorderApp && recorderApp->onStart) {
      recorderApp->onStart();
    } else {
      drawLauncher(); // defensive - no recorder registered, don't show a blank screen
      activeApp = APP_LAUNCHER;
    }
  } else if (app == APP_LAUNCHER) {
    drawLauncher();
  } else if (app == APP_SETTINGS) {
    enterSettingsApp();
  }
}

static void handleSettingsTouch(int x, int y) {
  switch (settingsTopScreen) {
    case SETTINGS_HOME: handleSettingsHomeTouch(x, y); break;
    case SETTINGS_WIFI: wifiHandleTouch(x, y); break;
    case SETTINGS_TIME: clockHandleTouch(x, y); break;
  }
}

// ---------------------------------------------------------------------
// Shade open/close + top-level touch dispatch
// ---------------------------------------------------------------------

// The shade draws its label/slider across the whole SHADE_H rect;
// closing it has to redraw whatever app is actually active, not assume
// a fixed one - re-entering the current app does a full, correct redraw
// of its content area, cleanly overwriting whatever the shade left
// behind.
static void closeShade() {
  shadeOpen = false;
  tft.fillRect(0, SHADE_Y, SCREEN_W, SHADE_H, COLOR_BG);
  switchToApp(activeApp);
}

// Tapping the notification bar, or a swipe starting from it, opens/closes
// the shade. While open, a tap on the slider starts a drag; any other tap
// outside the shade's own controls just closes it rather than also acting
// on whatever's underneath.
static void handleTouchDown(int x, int y) {
  if (y < NOTIF_BAR_H) {
    if (shadeOpen) {
      closeShade();
    } else {
      shadeOpen = true;
      drawShade();
    }
    return;
  }

  if (shadeOpen) {
    if (touchIsOnSlider(x, y)) {
      gestureMode = GESTURE_SLIDER;
      updateBrightnessFromTouch(x);
    } else {
      closeShade();
    }
    return;
  }

  if (activeApp == APP_RECORDER) {
    if (recorderApp && recorderApp->onTouch) recorderApp->onTouch(x, y);
  } else if (activeApp == APP_LAUNCHER) {
    handleLauncherTouch(x, y);
  } else if (activeApp == APP_SETTINGS) {
    handleSettingsTouch(x, y);
  }
}

// ---------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------
void shellRegisterRecorderApp(const CydeosApp *app, void (*backgroundTick)()) {
  recorderApp = app;
  recorderBackgroundTick = backgroundTick;
}

void shellInit() {
  wifiInit(goToSettingsHome);
  clockInit(goToSettingsHome);

  // Not set here anymore - the Recorder app (if registered) sets its own
  // real hooks via bleCompanionInit() before calling
  // shellRegisterRecorderApp(). If nothing registered, BLE commands still
  // correctly reply BLE_ERR_NOT_IMPLEMENTED (ble_companion.cpp's default).

  updateBatteryStatus(); // seed a real reading before the first draw below
  drawNotifBar(true);
  switchToApp(APP_RECORDER); // boots straight into the recorder, per CYDEOS Spec.md
  bleCompanionSetup();
}

void shellTick() {
  uint16_t tx, ty;
  static bool wasTouched = false;
  static uint32_t lastTouchMs = 0;
#if !defined(BOARD_ES3C28P)
  static uint32_t releaseCandidateMs = 0;
#endif
  static uint16_t lastGoodX = 0, lastGoodY = 0;
  bool rawTouched = pollTouch(tx, ty);

#if defined(BOARD_ES3C28P)
  // Capacitive touch doesn't suffer the resistive-style dropouts handled
  // below, so use the raw reading directly.
  bool touched = rawTouched;
  if (touched) {
    lastGoodX = tx;
    lastGoodY = ty;
  }
#else
  // Resistive touch reports brief signal dropouts even mid-drag - see
  // CLAUDE.md. Bridge brief dropouts (<80ms) by treating them as still
  // touched at the last known-good position, rather than a real release.
  bool touched;
  if (rawTouched) {
    touched = true;
    lastGoodX = tx;
    lastGoodY = ty;
    releaseCandidateMs = 0;
  } else if (wasTouched) {
    if (releaseCandidateMs == 0) releaseCandidateMs = millis();
    if (millis() - releaseCandidateMs < 80) {
      touched = true;
      tx = lastGoodX;
      ty = lastGoodY;
    } else {
      touched = false;
    }
  } else {
    touched = false;
  }
#endif

  if (touched && !wasTouched && millis() - lastTouchMs > SHADE_TOUCH_DEBOUNCE_MS) {
    lastTouchMs = millis();
    Serial.printf("touch trigger at x=%u y=%u\n", tx, ty);
    // The bottom corners are excluded (see BOTTOM_EDGE_SWIPE_MARGIN_X) so
    // an app's own corner buttons there still receive a normal tap.
    bool inSwipeZone = ty >= BOTTOM_EDGE_ZONE_Y &&
                        tx >= BOTTOM_EDGE_SWIPE_MARGIN_X && tx <= SCREEN_W - BOTTOM_EDGE_SWIPE_MARGIN_X;
    if (inSwipeZone) {
      gestureMode = GESTURE_BOTTOM_SWIPE;
      gestureStartY = ty;
    } else {
      handleTouchDown(tx, ty); // may set gestureMode = GESTURE_SLIDER
    }
  } else if (touched && wasTouched && gestureMode == GESTURE_SLIDER) {
    updateBrightnessFromTouch(tx);
  } else if (touched && wasTouched && gestureMode == GESTURE_BOTTOM_SWIPE) {
    if ((int)gestureStartY - (int)ty > SWIPE_UP_THRESHOLD) {
      gestureMode = GESTURE_NONE;
      switchToApp(APP_LAUNCHER);
    }
  } else if (!touched && wasTouched) {
    if (gestureMode == GESTURE_SLIDER) {
      saveBrightness(brightnessPercent);
    }
    gestureMode = GESTURE_NONE;
  }
  wasTouched = touched;

  if (millis() - lastBatteryCheckMs > BATTERY_SAMPLE_INTERVAL_MS) {
    lastBatteryCheckMs = millis();
    updateBatteryStatus();
  }

  // The pairing overlay only covers the content area below the notif bar
  // (see ble_companion.cpp), so the clock keeps ticking live even while a
  // pairing dialog is up - matches the original's unconditional redraw
  // here.
  drawNotifBar();

  if (activeApp == APP_SETTINGS) {
    if (settingsTopScreen == SETTINGS_WIFI) wifiTick();
    if (settingsTopScreen == SETTINGS_TIME) clockTick();
  } else if (activeApp == APP_RECORDER && recorderApp && recorderApp->onTick) {
    recorderApp->onTick();
  }

  // Keeps recording regardless of which app is on screen - see
  // shellRegisterRecorderApp()'s doc comment.
  if (recorderBackgroundTick) recorderBackgroundTick();

  // BLE control works from any app (not gated on Settings being active).
  bleCompanionTick();

  // The pairing overlay takes over the content area outside the normal
  // app-switching flow (it doesn't change activeApp) - once it clears,
  // whatever app was active needs a fresh redraw to erase it.
  static bool wasPairingUIActive = false;
  if (wasPairingUIActive && !blePairingUIActive) {
    switchToApp(activeApp);
  }
  wasPairingUIActive = blePairingUIActive;
}
