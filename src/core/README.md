# OS core services

M0 done. The shell/notification-bar, settings shade, WiFi management, BLE
Companion transport, battery monitoring, and clock (NTP/TZ) are ported out
of CYD-Voice-Recorder's `src/cyd/main.cpp` and formalized into standalone
modules — see each file's own header comment for what it owns.

`ble_companion.h`'s `BleCompanionHooks` are all `nullptr` right now (no
app has registered into them yet), so every BLE command replies
`BLE_ERR_NOT_IMPLEMENTED`. The Recorder app (M1) is expected to be the
first to register real hooks.

The native SD app system's "app discovery" piece (listed as an OS-level
service in the spec) isn't built yet - that's a later milestone (M3+),
gated on the native-app-loading feasibility spike.

See "OS core services vs. built-in apps" and the milestone list in
`CYDEOS Spec.md` at the repo root.
