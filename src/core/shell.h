#pragma once

#include "core/app.h"

// The OS shell: notification bar + settings shade (both global, available
// from any app), the gesture state machine, the launcher, and the
// Settings app's own top-level navigation (WiFi / Time tiles - Scriberr
// lives in the Recorder app instead, see CYDEOS Spec.md). This is the
// direct descendant of CYD-Voice-Recorder's switchToApp()/loop() touch
// handling, formalized into its own module. APP_RECORDER/APP_LAUNCHER/
// APP_SETTINGS are the only apps that exist (the Recorder app is the only
// one going through the CydeosApp interface; Launcher/Settings stay
// shell-internal since they're OS chrome, never candidates for SD
// loading).
//
// Call shellRegisterRecorderApp() before shellInit() so the initial boot
// screen (Recorder, per CYDEOS Spec.md) has something to switch to.
// backgroundTick is called every loop() iteration regardless of which app
// is actually in the foreground - the one deliberate exception to "apps
// only run while active" (see CYDEOS Spec.md's "Execution model"): the
// recorder's mic capture/SD write keeps running in the background exactly
// like it did in CYD-Voice-Recorder. Pass nullptr if not needed.
void shellRegisterRecorderApp(const CydeosApp *app, void (*backgroundTick)());

void shellInit();
void shellTick(); // call every loop() iteration - owns touch polling, gestures, notif bar/battery refresh, and ticking WiFi/clock/BLE/the recorder's background audio tick
