#include "core/battery.h"

#include <Arduino.h>

#include "boards/board_config.h"
#include "core/display.h"
#include "core/ui_widgets.h"

#define BATTERY_SMOOTH_SAMPLES 6   // ~30s rolling average - see the flicker note below
#define BATTERY_TREND_INTERVAL_MS 30000
#define BATTERY_TREND_RISE_MV 8.0f // min *smoothed* rise per window to count as "rising"
#define BATTERY_TREND_STREAK 2     // consecutive same-direction windows required to flip CHARGING on/off
#define BATTERY_NUB_H 5

BatteryStatus batteryStatus = BATTERY_NOT_CONNECTED;
static float batteryVoltage = 0.0f;

static float batterySampleBuf[BATTERY_SMOOTH_SAMPLES] = {0};
static int batterySampleCount = 0;
static int batterySampleIdx = 0;

static float batteryTrendBaselineV = -1.0f;
static uint32_t lastBatteryTrendMs = 0;
static int batteryRisingStreak = 0;
static int batteryFallingStreak = 0;
static bool batteryRisingTrend = false;

static float batterySmoothedVoltage() {
  int n = batterySampleCount < BATTERY_SMOOTH_SAMPLES ? batterySampleCount : BATTERY_SMOOTH_SAMPLES;
  if (n == 0) return 0.0f;
  float sum = 0;
  for (int i = 0; i < n; i++) sum += batterySampleBuf[i];
  return sum / n;
}

// No dedicated charge-status pin is broken out on this board (only the ADC
// sense pin) - "charging" is inferred from a rising voltage trend. See
// CLAUDE.md's "Battery status" section for the full flicker/plateau story
// behind the smoothing + streak-requirement below; don't simplify this back
// to a raw single-sample comparison.
void updateBatteryStatus() {
  int mv = analogReadMilliVolts(BATTERY_ADC_PIN);
  batteryVoltage = (mv / 1000.0f) * BATTERY_DIVIDER_RATIO;
  Serial.printf("Battery: raw=%dmV computed=%.2fV\n", mv, batteryVoltage);

  batterySampleBuf[batterySampleIdx] = batteryVoltage;
  batterySampleIdx = (batterySampleIdx + 1) % BATTERY_SMOOTH_SAMPLES;
  if (batterySampleCount < BATTERY_SMOOTH_SAMPLES) batterySampleCount++;
  float smoothedV = batterySmoothedVoltage();

  if (lastBatteryTrendMs == 0) {
    batteryTrendBaselineV = smoothedV;
    lastBatteryTrendMs = millis();
  } else if (millis() - lastBatteryTrendMs > BATTERY_TREND_INTERVAL_MS) {
    float deltaMv = (smoothedV - batteryTrendBaselineV) * 1000.0f;
    if (deltaMv > BATTERY_TREND_RISE_MV) {
      batteryRisingStreak++;
      batteryFallingStreak = 0;
      if (batteryRisingStreak >= BATTERY_TREND_STREAK) batteryRisingTrend = true;
    } else {
      batteryFallingStreak++;
      batteryRisingStreak = 0;
      if (batteryFallingStreak >= BATTERY_TREND_STREAK) batteryRisingTrend = false;
    }
    Serial.printf("Battery trend: deltaMv=%.1f risingStreak=%d fallingStreak=%d risingTrend=%d\n",
                  deltaMv, batteryRisingStreak, batteryFallingStreak, batteryRisingTrend);
    batteryTrendBaselineV = smoothedV;
    lastBatteryTrendMs = millis();
  }

  if (batteryVoltage < 1.0f) {
    batteryStatus = BATTERY_NOT_CONNECTED; // floating/disconnected ADC pin
  } else if (batteryRisingTrend && batteryVoltage < 4.15f) {
    batteryStatus = BATTERY_CHARGING;
  } else if (batteryVoltage < 3.4f) {
    batteryStatus = BATTERY_LOW;
  } else if (batteryVoltage < 3.9f) {
    batteryStatus = BATTERY_HALF;
  } else {
    batteryStatus = BATTERY_FULL;
  }
}

void drawBatteryIcon(int x, int y, BatteryStatus status) {
  tft.fillRect(x, y - 1, BATTERY_ICON_W + BATTERY_NUB_W + 2, BATTERY_ICON_H + 2, COLOR_BG);

  uint16_t outlineColor = (status == BATTERY_NOT_CONNECTED) ? COLOR_GREY_DIM : COLOR_TEXT;
  tft.drawRect(x, y, BATTERY_ICON_W, BATTERY_ICON_H, outlineColor);
  tft.fillRect(x + BATTERY_ICON_W, y + (BATTERY_ICON_H - BATTERY_NUB_H) / 2, BATTERY_NUB_W, BATTERY_NUB_H, outlineColor);

  if (status == BATTERY_NOT_CONNECTED) return; // empty outline only - no battery detected

  int fillMaxW = BATTERY_ICON_W - 4;
  int fillH = BATTERY_ICON_H - 4;
  uint16_t fillColor;
  int fillW;
  switch (status) {
    case BATTERY_LOW:      fillColor = COLOR_RED;    fillW = (int)(fillMaxW * 0.2f); break;
    case BATTERY_HALF:     fillColor = COLOR_YELLOW; fillW = (int)(fillMaxW * 0.5f); break;
    case BATTERY_CHARGING: fillColor = COLOR_AQUA;   fillW = fillMaxW;               break;
    case BATTERY_FULL:
    default:               fillColor = COLOR_GREEN;  fillW = fillMaxW;               break;
  }
  if (fillW > 0) tft.fillRect(x + 2, y + 2, fillW, fillH, fillColor);
}
