#pragma once

#include <cstddef>

#include "core/settings_common.h"

// Clock. The system clock always holds true UTC; local time is derived via
// localtime_r() using the selected TZ rule, user-changeable in Settings >
// Time. Two sources feed the UTC clock: BUILD_LOCAL_EPOCH baked in at flash
// time (boot fallback), then a real NTP sync once WiFi connects.
void setupClock();
void syncClockFromNTP();

// Shared by the notification bar (short, no seconds) and the Time screen's
// live clock (with seconds).
void formatTimeString(int hour24, int minute, int second, bool includeSeconds, char *buf, size_t len);
void getClockStrings(char *dateStr, size_t dateLen, char *timeStr, size_t timeLen);
void getTimestampForFilename(char *buf, size_t len);

// Settings > Time screen + timezone picker. onExitToSettingsHome is called
// when this screen's back button is tapped from its own top level (the
// timezone picker instead just returns to the Time screen internally).
void clockInit(ExitToSettingsHomeFn onExitToHome);
void clockEnterSettings(); // draws the Time screen
void clockHandleTouch(int x, int y);
void clockTick(); // call every loop() iteration; no-ops unless the Time screen is showing
