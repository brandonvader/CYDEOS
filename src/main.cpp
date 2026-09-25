// CYDEOS entry point — scaffold only, not yet implemented.
//
// See "CYDEOS Spec.md" at the repo root for the full architecture and
// milestone plan. This file is a placeholder until M0 (port OS core
// services out of CYD-Voice-Recorder: notification bar, settings shade,
// WiFi, BLE Companion transport, battery, clock) lands.
//
// Planned layout (see CYDEOS Spec.md's "OS core services vs. built-in
// apps"):
//   src/boards/   — per-board pins/geometry (board_config.h), HAL surface
//   src/core/     — OS core services: shell, WiFi, BLE Companion, battery,
//                   clock, app launcher/discovery
//   src/apps/     — built-in apps (recorder first, per M1)

#include <Arduino.h>

#include "boards/board_config.h"

void setup() {
  Serial.begin(115200);
  Serial.println("CYDEOS scaffold — no OS core implemented yet.");
}

void loop() {
  delay(1000);
}
