#pragma once

// Shared by every Settings sub-module (WiFi, Time, ...): called when that
// module's own top-level screen's back button is tapped, so the shell can
// return to the Settings home tile list. A module's own drill-down screens
// (e.g. WiFi's scan results, or the timezone picker) handle their own
// "back to my top-level screen" navigation internally instead.
typedef void (*ExitToSettingsHomeFn)();
