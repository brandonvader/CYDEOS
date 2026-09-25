#pragma once

// The OS shell: notification bar + settings shade (both global, available
// from any app), the gesture state machine, the launcher, and the
// Settings app's own top-level navigation (WiFi / Time tiles - Scriberr
// intentionally does not live here, see CYDEOS Spec.md). This is the
// direct descendant of CYD-Voice-Recorder's switchToApp()/loop() touch
// handling, formalized into its own module.
//
// M0 status: only APP_LAUNCHER and APP_SETTINGS exist. The launcher has a
// single "Settings" tile for now - Recorder (and a real App interface for
// switching between more than these two hardcoded screens) lands in M1.
void shellInit();
void shellTick(); // call every loop() iteration - owns touch polling, gestures, notif bar/battery refresh, and ticking WiFi/clock/BLE
