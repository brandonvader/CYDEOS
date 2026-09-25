#pragma once

#include <TFT_eSPI.h>
#include <cstdint>

// Shared display instance - every drawing module includes this to reach it.
extern TFT_eSPI tft;

// Backlight brightness - PWM, persisted in NVS. Call setupBacklight() once
// after tft.init() (it leaves TFT_BL as a plain digital-high output that
// would otherwise win over our PWM attachment), then applyBrightness() to
// change it live and saveBrightness() to persist a value once a drag ends.
extern int brightnessPercent;
void setupBacklight();
void applyBrightness(int percent);
void saveBrightness(int percent);

// Touch. Hosyond's resistive XPT2046 (via TFT_eSPI) needs a one-time
// 5-point calibration persisted in NVS. ES3C28P's capacitive FT6336G needs
// no calibration - just an I2C reset - and is read via a few register reads
// rather than TFT_eSPI's built-in touch support. Both boards' differences
// are isolated behind these two functions.
void setupTouch();
bool pollTouch(uint16_t &tx, uint16_t &ty);
