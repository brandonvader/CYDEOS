# CYDEOS — Cheap Yellow Display Extendable Operating System

**Status: M0 + M1 done.** The OS core services (shell/notification
bar/settings shade, WiFi, BLE Companion transport, battery, clock) and the
Recorder app (recording, spectrum visualizer, Recordings browser, Scriberr
upload) are both ported and build clean on both boards — see
`CYDEOS Spec.md` for the full architecture and milestone plan. This repo
is the next step after
[CYD Voice Recorder](https://github.com/brandonvader/CYD-Voice-Recorder),
which stays as its own clean, working reference (and the source of every
hardware finding this project inherits).

## What this is

A general maker/dev platform for CYD-family ESP32 boards: a small OS core
(shell, WiFi, BLE Companion, battery, clock) plus a real app model, where
the voice recorder becomes the first built-in app rather than the whole
firmware — and, eventually, a user can drop a compiled app onto the SD
card and CYDEOS will discover and run it.

Read `CYDEOS Spec.md` before touching anything here — it captures the
vision, the architecture decisions already made, and (importantly) the
open risks that aren't resolved yet, especially around native SD app
loading.

## Boards

Same two boards as CYD-Voice-Recorder to start:
- `cyd` — Hosyond 3.5" 320x480 CYD (ESP32-32E/E32R35T)
- `cyd_es3c28p` — lcdwiki ES3C28P 2.8" 240x320 ESP32-S3 CYD ("CBD")

## Layout

```
src/
  boards/   per-board pins/geometry (board_config.h) — HAL surface
  core/     OS core services (M0, done):
              identity      - CYDEOS-XXXX device name
              display       - tft instance, backlight, touch
              ui_widgets    - shared colors/layout/drawing helpers
              keyboard      - generic on-screen keyboard widget
              clock         - TZ/NTP + Settings > Time screen
              battery       - sampling/trend/status/icon
              wifi          - Settings > WiFi (scan/connect/saved networks)
              ble_companion - pairing/GATT/framed protocol/transfer session,
                              with hooks an app registers into
              shell         - notif bar, settings shade, gestures, launcher,
                              Settings app's WiFi/Time top-level navigation,
                              the CydeosApp registration slot for Recorder
              app           - the CydeosApp lifecycle interface
  apps/
    recorder/ (M1, done) - recording, spectrum visualizer, Recordings
              browser, Scriberr upload/settings - see its own README.md
```

## Building

Same toolchain as CYD-Voice-Recorder (PlatformIO Core in a dedicated venv
— see that repo's CLAUDE.md "Toolchain" section if this is a fresh
machine):

```sh
pio run -e cyd            # or -e cyd_es3c28p
pio run -e cyd -t upload
```

## Docs in this repo

- `CYDEOS Spec.md` — architecture, decisions, open risks, milestones.
- `CLAUDE.md` — hardware findings inherited from CYD-Voice-Recorder,
  re-scoped for this repo's layout; re-verify each item as it's ported.
- `CYDEOS Companion Spec.md` — BLE Companion protocol, v1 (recorder-only,
  now implemented here too via the Recorder app's BLE hooks); v2
  (generalized app control) is drafted in `CYDEOS Spec.md` and not yet
  merged in here.

## License

GPLv3 — see `LICENSE`.
