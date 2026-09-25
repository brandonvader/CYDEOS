#pragma once

#include "core/settings_common.h"

// Settings > WiFi: on/off toggle, scan, connect (with saved-credential
// shortcut and on-screen-keyboard password entry), and a saved-networks
// list (disconnect/forget/auto-connect toggle per network). WiFi
// connectivity itself is a core OS service usable by any app directly via
// the standard Arduino WiFi.h API (WiFi.status(), etc.) - this module only
// owns the Settings UI for configuring it.
void wifiInit(ExitToSettingsHomeFn onExitToHome);
void wifiEnterSettings(); // draws the WiFi main screen
void wifiHandleTouch(int x, int y);
void wifiTick(); // call every loop() iteration; each internal check no-ops unless its own screen is showing
