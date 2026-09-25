#include "core/clock.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "core/display.h"
#include "core/ui_widgets.h"

#ifndef BUILD_LOCAL_EPOCH
#define BUILD_LOCAL_EPOCH 1700000000UL // fallback if not injected at build time
#endif

#define TIME_DATE_Y (NOTIF_BAR_H + UI_SCALE(70))
#define TIME_CLOCK_Y (TIME_DATE_Y + UI_SCALE(34))
#define TIME_FORMAT_TOGGLE_Y (TIME_CLOCK_Y + UI_SCALE(50))
#define TIME_FORMAT_TOGGLE_H UI_SCALE(44)
#define TIME_TZ_LABEL_Y (TIME_FORMAT_TOGGLE_Y + TIME_FORMAT_TOGGLE_H + UI_SCALE(30))
#define TIME_TZ_BTN_Y (TIME_TZ_LABEL_Y + UI_SCALE(40))
#define TIME_TZ_BTN_H UI_SCALE(50)

static Preferences clockPrefs;
static ExitToSettingsHomeFn onExitToHome = nullptr;

enum ClockScreen { CLOCK_SCREEN_TIME,
                    CLOCK_SCREEN_TZ_PICKER };
static ClockScreen clockScreen = CLOCK_SCREEN_TIME;

struct TimezoneOption {
  const char *name;
  const char *rule;
};
static const TimezoneOption TIMEZONE_OPTIONS[] = {
    {"Pacific (US)", "PST8PDT,M3.2.0,M11.1.0"},
    {"Mountain (US)", "MST7MDT,M3.2.0,M11.1.0"},
    {"Central (US)", "CST6CDT,M3.2.0,M11.1.0"},
    {"Eastern (US)", "EST5EDT,M3.2.0,M11.1.0"},
    {"Alaska (US)", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Hawaii (US)", "HST10"},
    {"UTC", "UTC0"},
    {"London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Tokyo", "JST-9"},
};
#define NUM_TIMEZONES (sizeof(TIMEZONE_OPTIONS) / sizeof(TIMEZONE_OPTIONS[0]))
static int selectedTimezoneIndex = 0;
static bool use12HourFormat = false;

// Sized for the longest possible formatted string: 12-hour + seconds +
// AM/PM, e.g. "12:45:22 PM" (11 chars + null). Undersizing this caused a
// buffer overflow that corrupted memory and made the comparison against it
// never reliably match, redrawing every single loop iteration - showed up
// as rapid flicker specifically in 12-hour mode.
static char lastTimeScreenClock[14] = "";

static void loadTimezoneFromNVS() {
  clockPrefs.begin("settings", false);
  selectedTimezoneIndex = clockPrefs.getInt("timezone", 0);
  clockPrefs.end();
  if (selectedTimezoneIndex < 0 || selectedTimezoneIndex >= (int)NUM_TIMEZONES) selectedTimezoneIndex = 0;
}

static void saveTimezoneToNVS() {
  clockPrefs.begin("settings", false);
  clockPrefs.putInt("timezone", selectedTimezoneIndex);
  clockPrefs.end();
}

static void loadTimeFormatFromNVS() {
  clockPrefs.begin("settings", false);
  use12HourFormat = clockPrefs.getBool("use12h", false);
  clockPrefs.end();
}

static void saveTimeFormatToNVS() {
  clockPrefs.begin("settings", false);
  clockPrefs.putBool("use12h", use12HourFormat);
  clockPrefs.end();
}

// Just re-points TZ at the newly selected rule - no need to re-run NTP,
// since the underlying UTC clock hasn't changed, only how it's displayed.
static void applyTimezone(int index) {
  if (index < 0 || index >= (int)NUM_TIMEZONES) return;
  selectedTimezoneIndex = index;
  setenv("TZ", TIMEZONE_OPTIONS[index].rule, 1);
  tzset();
  saveTimezoneToNVS();
}

void formatTimeString(int hour24, int minute, int second, bool includeSeconds, char *buf, size_t len) {
  if (use12HourFormat) {
    int h = hour24 % 12;
    if (h == 0) h = 12;
    const char *ampm = (hour24 < 12) ? "AM" : "PM";
    if (includeSeconds) {
      snprintf(buf, len, "%d:%02d:%02d %s", h, minute, second, ampm);
    } else {
      snprintf(buf, len, "%d.%02d %s", h, minute, ampm);
    }
  } else {
    if (includeSeconds) {
      snprintf(buf, len, "%02d:%02d:%02d", hour24, minute, second);
    } else {
      snprintf(buf, len, "%02d.%02d", hour24, minute);
    }
  }
}

void setupClock() {
  loadTimezoneFromNVS();
  loadTimeFormatFromNVS();
  setenv("TZ", TIMEZONE_OPTIONS[selectedTimezoneIndex].rule, 1);
  tzset();

  struct timeval tv;
  tv.tv_sec = BUILD_LOCAL_EPOCH;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

void syncClockFromNTP() {
  Serial.println("Starting NTP sync (us.pool.ntp.org)...");
  // configTime(0,0,server) reconstructs its own TZ string from the two
  // numeric offsets (both zero here) and applies it, silently overwriting
  // whatever TZ was set before - the clock then displayed raw UTC instead
  // of local time. configTzTime() takes the POSIX TZ string directly
  // instead, so it doesn't clobber it.
  configTzTime(TIMEZONE_OPTIONS[selectedTimezoneIndex].rule, "us.pool.ntp.org");
}

void getClockStrings(char *dateStr, size_t dateLen, char *timeStr, size_t timeLen) {
  time_t now = time(nullptr);
  struct tm tmStruct;
  localtime_r(&now, &tmStruct);
  snprintf(dateStr, dateLen, "%02d/%02d/%02d", tmStruct.tm_mon + 1, tmStruct.tm_mday, (tmStruct.tm_year + 1900) % 100);
  formatTimeString(tmStruct.tm_hour, tmStruct.tm_min, tmStruct.tm_sec, false, timeStr, timeLen);
}

void getTimestampForFilename(char *buf, size_t len) {
  time_t now = time(nullptr);
  struct tm tmStruct;
  localtime_r(&now, &tmStruct);
  snprintf(buf, len, "%04d%02d%02d_%02d%02d%02d", tmStruct.tm_year + 1900, tmStruct.tm_mon + 1, tmStruct.tm_mday,
           tmStruct.tm_hour, tmStruct.tm_min, tmStruct.tm_sec);
}

// ---- Time screen ----
static void drawFormatToggle() {
  tft.fillRoundRect(40, TIME_FORMAT_TOGGLE_Y, SCREEN_W - 80, TIME_FORMAT_TOGGLE_H, 10, lerp565(COLOR_BG, COLOR_AQUA, 0.15f));
  tft.drawRoundRect(40, TIME_FORMAT_TOGGLE_Y, SCREEN_W - 80, TIME_FORMAT_TOGGLE_H, 10, COLOR_AQUA);
  drawTextIn(use12HourFormat ? "12-Hour Time" : "24-Hour Time", 40, SCREEN_W - 80,
             TIME_FORMAT_TOGGLE_Y + TIME_FORMAT_TOGGLE_H / 2 - 8, COLOR_WHITE);
}

static void drawTimeScreen() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawBackButton();
  drawCenteredLine("Time & Date", NOTIF_BAR_H + 8, COLOR_AQUA);

  time_t now = time(nullptr);
  struct tm tmStruct;
  localtime_r(&now, &tmStruct);
  char dateStr[16], timeStr[14];
  snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", tmStruct.tm_mon + 1, tmStruct.tm_mday, tmStruct.tm_year + 1900);
  formatTimeString(tmStruct.tm_hour, tmStruct.tm_min, tmStruct.tm_sec, true, timeStr, sizeof(timeStr));
  strcpy(lastTimeScreenClock, timeStr);
  drawCenteredLine(dateStr, TIME_DATE_Y, COLOR_TEXT);
  drawCenteredLine(timeStr, TIME_CLOCK_Y, COLOR_WHITE);

  drawFormatToggle();

  char tzLabel[32];
  snprintf(tzLabel, sizeof(tzLabel), "Timezone: %s", TIMEZONE_OPTIONS[selectedTimezoneIndex].name);
  drawCenteredLine(tzLabel, TIME_TZ_LABEL_Y, COLOR_AQUA_DIM);

  tft.fillRoundRect(40, TIME_TZ_BTN_Y, SCREEN_W - 80, TIME_TZ_BTN_H, 10, lerp565(COLOR_BG, COLOR_AQUA, 0.15f));
  tft.drawRoundRect(40, TIME_TZ_BTN_Y, SCREEN_W - 80, TIME_TZ_BTN_H, 10, COLOR_AQUA);
  drawTextIn("Change Timezone", 40, SCREEN_W - 80, TIME_TZ_BTN_Y + TIME_TZ_BTN_H / 2 - 8, COLOR_WHITE);
}

// Called every loop tick while this screen is up - only actually redraws
// the clock line when the displayed second changes, same pattern as the
// notification bar.
static void updateTimeScreenClock() {
  time_t now = time(nullptr);
  struct tm tmStruct;
  localtime_r(&now, &tmStruct);
  char timeStr[14];
  formatTimeString(tmStruct.tm_hour, tmStruct.tm_min, tmStruct.tm_sec, true, timeStr, sizeof(timeStr));
  if (strcmp(timeStr, lastTimeScreenClock) == 0) return;
  strcpy(lastTimeScreenClock, timeStr);
  tft.fillRect(0, TIME_CLOCK_Y - 4, SCREEN_W, 28, COLOR_BG);
  drawCenteredLine(timeStr, TIME_CLOCK_Y, COLOR_WHITE);
}

// ---- Timezone picker ----
static void drawTimezonePicker() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawBackButton();
  drawCenteredLine("Select Timezone", NOTIF_BAR_H + 8, COLOR_AQUA);

  for (int i = 0; i < (int)NUM_TIMEZONES; i++) {
    drawListRow(i, TIMEZONE_OPTIONS[i].name, i == selectedTimezoneIndex, i == selectedTimezoneIndex);
  }
}

static void handleTimezonePickerTouch(int x, int y) {
  if (handleBackButtonTouch(x, y)) {
    clockScreen = CLOCK_SCREEN_TIME;
    drawTimeScreen();
    return;
  }
  int idx = (y - ROW_LIST_Y) / ROW_H;
  if (idx < 0 || idx >= (int)NUM_TIMEZONES) return;
  applyTimezone(idx);
  clockScreen = CLOCK_SCREEN_TIME;
  drawTimeScreen();
}

static void handleTimeScreenTouch(int x, int y) {
  if (handleBackButtonTouch(x, y)) {
    if (onExitToHome) onExitToHome();
    return;
  }
  if (y >= TIME_FORMAT_TOGGLE_Y && y <= TIME_FORMAT_TOGGLE_Y + TIME_FORMAT_TOGGLE_H) {
    use12HourFormat = !use12HourFormat;
    saveTimeFormatToNVS();
    drawTimeScreen(); // refresh this screen's clock immediately
    // The notification bar's own clock format follows the same setting;
    // the shell redraws it on its own next tick (drawNotifBar() only
    // skips a redraw when the formatted string is unchanged, and this
    // toggle just changed it).
    return;
  }
  if (y >= TIME_TZ_BTN_Y && y <= TIME_TZ_BTN_Y + TIME_TZ_BTN_H) {
    clockScreen = CLOCK_SCREEN_TZ_PICKER;
    drawTimezonePicker();
  }
}

void clockInit(ExitToSettingsHomeFn onExit) {
  onExitToHome = onExit;
}

void clockEnterSettings() {
  clockScreen = CLOCK_SCREEN_TIME;
  drawTimeScreen();
}

void clockHandleTouch(int x, int y) {
  if (clockScreen == CLOCK_SCREEN_TIME) {
    handleTimeScreenTouch(x, y);
  } else {
    handleTimezonePickerTouch(x, y);
  }
}

void clockTick() {
  if (clockScreen == CLOCK_SCREEN_TIME) {
    updateTimeScreenClock();
  }
}
