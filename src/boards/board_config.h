#pragma once

// ---------------------------------------------------------------------
// Per-board pins/geometry. Exactly one of BOARD_HOSYOND_35 / BOARD_ES3C28P
// is defined via platformio.ini build_flags for the active env. Layout
// constants derived from SCREEN_W/H (buttons, spectrum, settings screens,
// etc.) live in main.cpp, not here - they already fall out of these values
// automatically. Absolute pixel sizes there (BTN_RADIUS, LAUNCHER_TILE_W,
// etc.) were tuned against the 320x480 Hosyond panel and will likely need a
// visual re-tune pass once ES3C28P (240x320) is running.
// ---------------------------------------------------------------------

#if defined(BOARD_ES3C28P)

// 2.8" ESP32-S3 CYD (lcdwiki ES3C28P) - see
// https://www.lcdwiki.com/2.8inch_ESP32-S3_Display and the vendor's
// "ES3C28P&ES3N28P Arduino Demo_Instructions" PDF for the pin table this
// was taken from.

#define SCREEN_W 240
#define SCREEN_H 320
#define DISPLAY_ROTATION 0

// UI layout constants in main.cpp were tuned against the 320x480 Hosyond
// panel. Rather than re-derive every absolute pixel size (button radii,
// row heights, font size, etc.) from scratch for this smaller 240x320
// panel, scale them all by a flat percentage. In a portrait UI, vertical
// stacking is almost always the binding constraint, not width - the raw
// axis ratios are 75% (width, 240/320) vs 66.7% (height, 320/480), but the
// real ceiling is set by the most vertically-dense screen in the app: the
// timezone picker's 10 fixed-height rows only fit up to ~67.8% before
// running off the bottom of this screen (it already has just 8px of slack
// at 100% scale on the original 480px-tall panel). Other screens tolerate
// more (WiFi scan panel up to ~72%, time/password/keyboard screens up to
// ~81-91%), and the recorder screen's spectrum area is elastic (fills
// whatever's left, so it can't overflow, just gets shorter). 67% is the
// largest flat value that keeps every screen on-screen.
#define UI_SCALE_PCT 67
#define UI_TEXT_SIZE_NORMAL 1 // TFT_eSPI only takes integer text-size multiples
#define UI_TEXT_SIZE_LARGE 2  // BLE pairing passkey - same 2x-normal ratio as Hosyond below

// Touch (FT6336G, capacitive, I2C) - shares its bus with the ES8311 audio
// codec (different address). No calibration data needed, unlike the
// resistive touch below.
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_RST 18
#define TOUCH_INT 17 // not used yet - polled like the resistive board, not IRQ-driven

// SD card - SDIO/SD_MMC 4-line, NOT SPI.
#define SD_CLK 38
#define SD_CMD 40
#define SD_D0 39
#define SD_D1 41
#define SD_D2 48
#define SD_D3 47
#define SD_MOUNT_RETRIES 5

// Mic - built-in, behind an ES8311 codec (I2C addr 0x18, on the same bus as
// touch) feeding I2S. DO (IO8) is the codec's playback data-out pin, unused
// for v1's capture-only path.
#define I2S_MCK 4
#define I2S_SCK 5
#define I2S_WS 7 // LRC
#define I2S_DI 6
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000
#define AUDIO_EN_PIN 1 // low = enable, per vendor pin table
#define AUDIO_CODEC_I2C_ADDR 0x18

// Battery voltage sense (2000mAh LiPo via the board's built-in JST
// connector + onboard charge circuit) - ADC1 channel, per the vendor pin
// table. Divider ratio confirmed via multimeter - see "Battery status" in
// CLAUDE.md before trusting these numbers.
#define BATTERY_ADC_PIN 9
#define BATTERY_DIVIDER_RATIO 2.011f

// Backlight - single pin, active-high, no ambiguity (unlike Hosyond below).
#define BACKLIGHT_PIN 45
#define BACKLIGHT_PWM_FREQ 5000
#define BACKLIGHT_PWM_RES 8
#define BACKLIGHT_MIN_DUTY 20
#define BACKLIGHT_DEFAULT_PERCENT 75
#define SHADE_TOUCH_DEBOUNCE_MS 300

#define BOOT_BUTTON_PIN 0

#else // BOARD_HOSYOND_35 (default)

// 3.5" Hosyond ESP32-32E CYD - see
// https://www.lcdwiki.com/3.5inch_ESP32-32E_Display and CLAUDE.md's
// "Hard-won CYD findings" for how these were determined.

#define SCREEN_W 320
#define SCREEN_H 480
#define DISPLAY_ROTATION 0

#define UI_SCALE_PCT 100 // no-op - this is the panel the layout was tuned against
#define UI_TEXT_SIZE_NORMAL 2
#define UI_TEXT_SIZE_LARGE 4 // BLE pairing passkey

// Mic (INMP441, bare I2S, no codec)
#define I2S_WS 32
#define I2S_SCK 25
#define I2S_SD 39
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000

#define BOOT_BUTTON_PIN 0 // kept as a secondary start/stop control

// SD card - SPI, forced onto SPI3_HOST/VSPI (see mountSD() in main.cpp).
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18
#define SD_CS 5
#define SD_MOUNT_RETRIES 5

// Battery voltage sense (E32R35T variant, built-in JST battery port +
// TP4054 charge IC) - confirmed via the vendor's own schematic PDF
// (lcdwiki's "3.5inch_ESP32-32E_E32R35T_Schematic.pdf"): IO34 is labeled
// "BAT_ADC", fed by a 100K/100K divider (R2/R3) wired directly across the
// raw BAT+ battery line - NOT a charge-managed system rail like the
// ES3C28P's sense pin (see CLAUDE.md's "Hard-won CYD findings"), so
// NOT_CONNECTED may actually be detectable on this board. No CHRG status
// pin from the TP4054 is broken out to any GPIO (only pulled up via a
// resistor, presumably just for internal logic level) - same charging-
// detection limitation as the ES3C28P. Divider ratio below is a
// placeholder pending live multimeter calibration.
#define BATTERY_ADC_PIN 34
// Calibrated against a real multimeter reading at the JST connector
// (3.642V measured vs. 1877.5mV average raw ADC reading = 1.940) -
// slightly under the nominal 2.0 for a 100K/100K pair, within normal
// resistor tolerance.
#define BATTERY_DIVIDER_RATIO 1.940f

// Backlight pin is ambiguous between GPIO21 (TFT_BL build flag) and
// GPIO27 - drive both via PWM so brightness control works regardless of
// which one is real, and the other is harmlessly ignored.
#define BACKLIGHT_PIN_A 21
#define BACKLIGHT_PIN_B 27
#define BACKLIGHT_PWM_FREQ 5000
#define BACKLIGHT_PWM_RES 8                  // bits -> duty 0-255
#define BACKLIGHT_MIN_DUTY 20                // floor so the backlight never fully turns off (~8%)
#define BACKLIGHT_DEFAULT_PERCENT 75
#define SHADE_TOUCH_DEBOUNCE_MS 300

#endif

// Integer-percentage scale, shared by both board branches above.
#define UI_SCALE(px) (((px) * UI_SCALE_PCT) / 100)
