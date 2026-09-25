// CYDEOS entry point.
//
// M1 status: the OS core services (src/core/) plus the Recorder app
// (src/apps/recorder/, with the Recordings browser and Scriberr upload
// folded in - see CYDEOS Spec.md) are both ported. setup()'s ordering
// below is load-bearing, not arbitrary - see CLAUDE.md before rearranging
// it: I2S bring-up + channel calibration must happen before tft.init(),
// and the SD card must not be mounted until after that calibration read
// completes.

#include <Arduino.h>
#include <WiFi.h>

#include "apps/recorder/recorder_app.h"
#include "boards/board_config.h"
#include "core/clock.h"
#include "core/display.h"
#include "core/identity.h"
#include "core/shell.h"
#include "core/ui_widgets.h"

void setup() {
  Serial.begin(115200);
  delay(300);

  randomSeed(esp_random()); // seeds BLE pairing-passkey generation
  computeCydeosDeviceName(); // before anything that uses it (WiFi hostname, BLE name)

  setupClock();
  WiFi.mode(WIFI_OFF); // stays off until the user enables it in Settings

  recorderAudioBringup(); // I2S/codec bring-up + channel calibration - before tft.init(), see CLAUDE.md

  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(COLOR_BG);

  // Must come after tft.init() - it leaves TFT_BL as a plain digital-high
  // output, which would win over our PWM attachment if done first.
  setupBacklight();
  setupTouch();

  recorderFinishSetup(); // SD mount, BLE hooks, transcription settings - after touch, before shellInit()
  shellRegisterRecorderApp(recorderAppInterface(), recorderBackgroundTick);
  shellInit(); // draws the initial screen (boots into the Recorder app), starts BLE advertising

  Serial.println("CYDEOS core up");

  // Deliberately after shellInit() so the UI is already up and
  // interactive - a slow analysis pass shouldn't block the whole device
  // from being usable at boot. See CLAUDE.md.
  recorderAnalyzeMostRecentIfPresent();
}

void loop() {
  shellTick();
}
