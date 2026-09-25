// CYDEOS entry point.
//
// M0 status: the OS core services (shell/notification bar/settings shade,
// WiFi, BLE Companion transport, battery, clock) are ported and live under
// src/core/. There is no Recorder app yet (that's M1 - see CYDEOS
// Spec.md), so setup() below skips I2S/SD/mic bring-up entirely and the
// launcher only offers Settings. Re-verify each hardware finding in
// CLAUDE.md as SD/I2S/mic code actually gets ported back in for M1.

#include <Arduino.h>
#include <WiFi.h>

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

  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(COLOR_BG);

  // Must come after tft.init() - it leaves TFT_BL as a plain digital-high
  // output, which would win over our PWM attachment if done first.
  setupBacklight();
  setupTouch();

  shellInit();

  Serial.println("CYDEOS core up (no apps yet - see CYDEOS Spec.md milestone M1)");
}

void loop() {
  shellTick();
}
