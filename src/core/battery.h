#pragma once

// Battery status (board-agnostic - both cyd and cyd_es3c28p have real
// battery hardware). See "Battery status" in CLAUDE.md for the
// calibration/charging-detection caveats before trusting these numbers.
enum BatteryStatus { BATTERY_NOT_CONNECTED,
                      BATTERY_LOW,
                      BATTERY_HALF,
                      BATTERY_FULL,
                      BATTERY_CHARGING };

extern BatteryStatus batteryStatus;

// Samples the ADC, updates batteryStatus. Call periodically (every
// BATTERY_SAMPLE_INTERVAL_MS) from the shell's loop() - not on every tick,
// since analogReadMilliVolts() plus the trend bookkeeping isn't free and
// doesn't need to run any faster than the icon can visibly change.
#define BATTERY_SAMPLE_INTERVAL_MS 5000
void updateBatteryStatus();

void drawBatteryIcon(int x, int y, BatteryStatus status);

#define BATTERY_ICON_W 16
#define BATTERY_ICON_H 9
#define BATTERY_NUB_W 2
