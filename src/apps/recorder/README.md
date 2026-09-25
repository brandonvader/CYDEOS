# Recorder app (built-in)

M1 done. Ported from CYD-Voice-Recorder's `src/cyd/main.cpp` under the
app-lifecycle interface (`core/app.h`) - see each file's own header
comment for what it owns:

- `recorder_app.*` - the `CydeosApp` implementation, main-screen touch
  handling (record/pause buttons), setup orchestration, and BLE Companion
  hook registration.
- `audio.*` - I2S/mic bring-up (ES3C28P: ES8311 codec first) and the
  drain-DMA/write/spectrum-FFT capture loop.
- `recording.*` - the current recording session's state (the single
  source of truth every other module reads), WAV file I/O, and recording
  metadata (used by both the Recordings browser and the BLE file-list/HTTP
  transfer-session handlers).
- `storage.*` - SD card mount + free-space query.
- `recorder_ui.*` - drawing for the main screen (buttons, free-space bar,
  spectrum, status messages).
- `recordings_screen.*` - the Recordings browser (list/detail + upload
  trigger), folded into this app per CYDEOS Spec.md rather than a
  separate launcher-level app.
- `transcription.*` - Scriberr config + upload + its own settings screen,
  intentionally scoped inside this app (not OS-level Settings) - see the
  rationale note in `CYDEOS Spec.md`'s "Built-in apps" section.

Recording keeps running regardless of which app is on screen - see
`recorderBackgroundTick()` in `recorder_app.cpp` and CYDEOS Spec.md's
"Execution model" for why that's a deliberate one-off exception to the
app-lifecycle interface, not a general capability.

BLE Companion hooks are registered in `recorderFinishSetup()` - this is
the first app to actually fill in `core/ble_companion.h`'s
`BleCompanionHooks`.
