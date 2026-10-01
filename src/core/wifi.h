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
void wifiTick(); // call only while the WiFi settings screen is active; each internal check no-ops unless its own sub-screen is showing

// Call every loop() iteration, UNCONDITIONALLY (regardless of which app/
// screen is active) - detects a background auto-connect's transition to
// WL_CONNECTED and fires the NTP clock sync, and handles the auto-connect
// timeout/forget-stale-network path. See CLAUDE.md: this used to be
// folded into wifiTick() and only ran while the WiFi settings screen was
// open, which silently broke clock sync for any device that boots
// straight into another app (the Recorder app, by default) and never
// visits Settings > WiFi.
void wifiBackgroundTick();
