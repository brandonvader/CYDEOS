#include "core/display.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include "boards/board_config.h"
#include "core/ui_widgets.h"

TFT_eSPI tft = TFT_eSPI();

static Preferences touchPrefs;
static Preferences backlightPrefs;
static uint16_t calData[5];

int brightnessPercent = BACKLIGHT_DEFAULT_PERCENT;

// ---------------------------------------------------------------------
// Backlight - see board_config.h for why Hosyond drives two candidate pins
// (backlight wiring is ambiguous across units) while ES3C28P has a single,
// unambiguous pin.
// ---------------------------------------------------------------------
#define BACKLIGHT_LEDC_CHANNEL_A 4
#define BACKLIGHT_LEDC_CHANNEL_B 5

void applyBrightness(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  brightnessPercent = percent;
  int duty = BACKLIGHT_MIN_DUTY + ((255 - BACKLIGHT_MIN_DUTY) * percent) / 100;
  ledcWrite(BACKLIGHT_LEDC_CHANNEL_A, duty);
#if !defined(BOARD_ES3C28P)
  ledcWrite(BACKLIGHT_LEDC_CHANNEL_B, duty);
#endif
}

void setupBacklight() {
#if defined(BOARD_ES3C28P)
  ledcSetup(BACKLIGHT_LEDC_CHANNEL_A, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_RES);
  ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_LEDC_CHANNEL_A);
#else
  ledcSetup(BACKLIGHT_LEDC_CHANNEL_A, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_RES);
  ledcAttachPin(BACKLIGHT_PIN_A, BACKLIGHT_LEDC_CHANNEL_A);
  ledcSetup(BACKLIGHT_LEDC_CHANNEL_B, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_RES);
  ledcAttachPin(BACKLIGHT_PIN_B, BACKLIGHT_LEDC_CHANNEL_B);
#endif

  backlightPrefs.begin("settings", false);
  brightnessPercent = backlightPrefs.getInt("brightness", BACKLIGHT_DEFAULT_PERCENT);
  backlightPrefs.end();

  applyBrightness(brightnessPercent);
}

void saveBrightness(int percent) {
  backlightPrefs.begin("settings", false);
  backlightPrefs.putInt("brightness", percent);
  backlightPrefs.end();
}

// ---------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------
#if defined(BOARD_ES3C28P)
#define FT6336_I2C_ADDR 0x38
#define FT6336_REG_TOUCH_COUNT 0x02

void setupTouch() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(50); // FT6336G needs a brief settle time after reset before it ACKs on I2C
}

bool pollTouch(uint16_t &tx, uint16_t &ty) {
  Wire.beginTransmission(FT6336_I2C_ADDR);
  Wire.write(FT6336_REG_TOUCH_COUNT);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)FT6336_I2C_ADDR, 5) != 5) return false;

  uint8_t touchCount = Wire.read();
  uint8_t xh = Wire.read(), xl = Wire.read();
  uint8_t yh = Wire.read(), yl = Wire.read();
  if (touchCount == 0 || touchCount > 2) return false;

  tx = ((xh & 0x0F) << 8) | xl;
  ty = ((yh & 0x0F) << 8) | yl;
  return true;
}
#else
void setupTouch() {
  touchPrefs.begin("touchcal", false);
  if (touchPrefs.isKey("calData")) {
    touchPrefs.getBytes("calData", calData, sizeof(calData));
    tft.setTouch(calData);
    Serial.println("Loaded touch calibration from NVS");
  } else {
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_WHITE, COLOR_BG);
    tft.setTextSize(UI_TEXT_SIZE_NORMAL);
    tft.setCursor(20, SCREEN_H / 2 - 20);
    tft.println("Tap each crosshair");
    tft.calibrateTouch(calData, COLOR_WHITE, COLOR_BG, 20);
    touchPrefs.putBytes("calData", calData, sizeof(calData));
    Serial.println("Touch calibration saved to NVS");
  }
}

bool pollTouch(uint16_t &tx, uint16_t &ty) {
  return tft.getTouch(&tx, &ty);
}
#endif
