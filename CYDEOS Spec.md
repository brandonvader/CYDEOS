# CYDEOS — Cheap Yellow Display Extendable Operating System

**Status:** Draft v1, produced by interview (2026-09-24). Not yet implemented.
No code has been written against this spec. It captures direction and
architecture decisions only; treat every "TBD" and "Open Risk" below as
blocking further design until resolved by prototyping.

## Vision

CYDEOS is a general maker/dev platform for CYD-family ESP32 boards, not a
single-purpose device. It's the evolution of the
[CYD Voice Recorder](https://github.com/brandonvader/CYD-Voice-Recorder)
project: same hard-won hardware knowledge (see that repo's `CLAUDE.md`),
reorganized around a small OS core plus a real app model, so voice
recording becomes the *first* app rather than the entire firmware.

The defining feature: **a user can drop a compiled app file onto the SD
card and CYDEOS discovers and runs it** — a real, if minimal, app
ecosystem, not just a fixed set of screens compiled into one firmware
image.

## Relationship to CYD-Voice-Recorder

- New repo, fresh start. CYD-Voice-Recorder stays as-is — a clean, working
  reference and fallback, and the source of every hard-won hardware finding
  CYDEOS inherits.
- CYDEOS ports over: the notification bar/settings-shade shell, WiFi
  management + saved networks, BLE Companion, battery status logic, NTP/TZ
  clock handling, and the recorder itself (with its Recordings browser and
  Scriberr upload folded into one app rather than three).
- Every hardware-specific finding in CYD-Voice-Recorder's `CLAUDE.md`
  (SPI3_HOST for SD, I2S calibration-before-SD-mount ordering, NimBLE 2.x
  quirks, per-board battery divider ratios, etc.) still applies and should
  be re-verified against the new codebase as each piece is ported, not
  assumed to survive the refactor unchanged.

## Hardware scope

Same two boards as CYD-Voice-Recorder to start (Hosyond 3.5" `cyd` /
E32R35T, and lcdwiki ES3C28P "CBD" `cyd_es3c28p`), but CYDEOS is designed
with a real hardware abstraction layer (HAL) from day one so a third or
fourth CYD-family board is a config/board-profile addition, not a
structural rewrite. The HAL should cover, at minimum: display driver +
geometry, touch input, SD/storage bus, mic/audio input, battery ADC +
divider ratio, and available RAM (PSRAM presence/size) — the last of
these matters specifically for the native app loader (see below).

UI stays hand-rolled TFT_eSPI (immediate-mode drawing), not a migration to
LVGL — lowest risk, keeps all the SPI/touch hard-won findings valid.
Formalize existing ad-hoc drawing code into reusable widget helper
functions as part of the port, rather than a framework swap.

## OS core services vs. built-in apps

**OS-level services** (always present, not removable, available to every
app):
- Notification bar (clock, battery icon) + settings shade (brightness)
- WiFi management (scan, connect, saved networks, auto-connect)
- BLE Companion connectivity (see protocol v2 below)
- Battery status monitoring
- SD/storage mount + core filesystem access
- Clock (NTP sync + TZ handling)
- App launcher / app discovery (see native app system below)

**Built-in apps** (ported from CYD-Voice-Recorder, could in principle be
removed/replaced without touching OS core):
- **Recorder** — recording UI, free-space bar, spectrum visualizer, *plus*
  the Recordings browser and transcription upload folded in as part of the
  same app (not separate apps, per your direction) — accessible as tabs/
  screens within one Recorder app rather than three launcher tiles. This
  includes the app's own transcription-service configuration (host/port/
  API key, currently Scriberr) as an in-app settings screen, not an
  OS-level one — see below.
- **Settings** — WiFi, brightness, etc. (as today). No longer owns
  Scriberr config (moved into the Recorder app).
- **Launcher** — app gallery, now listing both built-in apps and any
  discovered SD apps (see below).

Transcription-service configuration lives inside the Recorder app rather
than OS-level Settings deliberately: it's specific to what the Recorder
app does with a recording, not a device-wide setting, and keeping it
app-scoped means adding a second transcription backend (or letting a
future SD-loaded recorder-like app bring its own) is a change contained
entirely to that app, not a Settings-screen change plus an app change.

## Native SD app system

### Discovery & layout

Proposed SD layout (final paths TBD during implementation, but shaped to
stay conceptually close to prior art — see "Relationship to GhostESP"
below):

```
/cydeos/
  apps/<app_id>/
    manifest.json
    <entry-binary>
  appdata/<app_id>/         per-app persistent storage
```

The launcher rescans `/cydeos/apps/` at boot and on demand (e.g. a
"Refresh" action), reading each `manifest.json` to populate the app
gallery — **without executing any app code just to list it** (this is why
the manifest is a sidecar file, not something read out of the binary).

### Manifest format (draft — needs refinement once the loader is prototyped)

```json
{
  "id": "example_app",
  "name": "Example App",
  "version": "1.0.0",
  "author": "someone",
  "description": "One-line description shown in the launcher",
  "entry": "example_app.bin",
  "target": "cyd_es3c28p",
  "api_version": 1,
  "icon": "icon.rgb565",
  "permissions": ["display", "touch", "storage", "wifi", "ble", "mic"],
  "requires_psram": true
}
```

- `target` matters more here than in most ESP32 app-loading systems: the
  two supported boards are different CPU architectures (Xtensa LX6 on
  classic ESP32 vs. Xtensa LX7 on the S3), so app binaries are
  **not interchangeable** across boards and must declare which one they're
  built for. The loader should refuse to load a mismatched `target`.
- `permissions` in v1 is **declarative/informational only** — since you
  chose direct hardware access over a curated/sandboxed API, CYDEOS does
  not enforce these at runtime. They exist so the launcher can show the
  user what an app *claims* to touch, and to keep the manifest shape
  compatible with a future stricter model if you ever want one. Don't
  over-invest in the enforcement side for v1.
- `requires_psram` / a rough `memory_limit` should still be declared even
  without enforcement, mainly to let the launcher warn ("this app may not
  run on the Hosyond board") rather than just silently fail.

### Entry point / ABI

Single exported entry function per app, e.g.:

```c
const cydeos_app_t *cydeos_app_init(const cydeos_api_t *api);
```

returning a small struct of callbacks (`on_start`, `on_stop`, `on_tick`,
`on_touch`, `on_draw`) — mirrors the existing `switchToApp()` pattern
already used for built-in apps, so built-in and SD-loaded apps end up
sharing one internal app-lifecycle interface even though only SD apps go
through the dynamic loader.

Even with direct hardware access allowed, the OS should still expose a
`cydeos_api_t` table of convenience functions (drawing primitives, touch
state, SD path helpers) purely so app authors *don't have to* reimplement
things like text rendering — this is a convenience layer, not a security
boundary, and apps remain free to bypass it and touch TFT_eSPI/I2S/SD
directly.

### Loading mechanism

Native position-independent binaries, loaded at runtime and executed in
place — the same general approach as Espressif's own `elf_loader`
component (confirmed real prior art: this is exactly what GhostESP's
native SD app system uses today, see below). **This is the biggest open
risk in the whole spec** and needs a feasibility spike before anything
else here is finalized:

- Classic ESP32 (Hosyond board) has no PSRAM, and code execution is
  generally restricted to internal IRAM (a budget on the order of
  ~100–200KB depending on what else is resident) — not a place to expect
  to run arbitrary-sized user apps.
- ESP32-S3 (ES3C28P/CBD) has 8MB PSRAM and more general headroom, making
  it the more realistic first target.
- It's unconfirmed whether Espressif's `elf_loader` component (or an
  equivalent) is usable from an **Arduino-framework** PlatformIO project
  (it's normally demonstrated in pure ESP-IDF projects) without pulling in
  enough of ESP-IDF's build system to threaten the existing Arduino-based
  toolchain and libraries (TFT_eSPI, NimBLE-Arduino) this whole codebase
  depends on.

**Per board scope, per your direction:** prototype and validate on
ES3C28P/CBD first; treat Hosyond support as a stretch goal once (if)
feasibility is proven there, possibly with a much tighter app size/
complexity ceiling.

### Execution model

- **One foreground app at a time** — matches the existing `switchToApp()`
  shell. No general background/service capability for loaded apps; the
  recorder's background-recording behavior stays an OS-level special case
  (`recState` gating independent of `activeApp`, as it works today), not a
  capability exposed to arbitrary apps.
- **OS keeps the notification bar** while an app runs; the app owns
  everything below it — same visual contract as built-in apps today, so
  loaded apps look native rather than like a different program took over
  the whole screen.
- App install path for v1 is **manual SD card copy only** — no OTA-style
  push via BLE/WiFi transfer. Revisit only if this becomes a real friction
  point.

### App state / failure handling

Borrow the shape of GhostESP's approach (see below) even though the
mechanism differs: track a small per-app state file (e.g.
`appdata/<app_id>/.state.json`) recording launch failure count and last
error, so a crashing app can be flagged/quarantined in the launcher rather
than silently retried forever. Exact fields TBD during implementation.

## BLE Companion protocol v2 (generalized)

Extend `CYDEOS Companion Spec.md`'s existing message-framed protocol
(unchanged transport: `[type][len LE16][payload]` over the Command/
Response characteristics) with new message types so the Android app can
manage the OS itself, not just the recorder:

| Code | Name | Direction | Payload |
|---|---|---|---|
| `0x40` | APP_LIST_REQUEST | phone→device | (empty) |
| `0x41` | APP_LIST_ENTRY | device→phone | 1 byte id length + id + 1 byte name length + name + 1 byte state (running/stopped/quarantined) |
| `0x42` | APP_LIST_END | device→phone | (empty) |
| `0x43` | APP_LAUNCH | phone→device | 1 byte app id length + app id |
| `0x44` | APP_STOP | phone→device | (empty) — stops whatever app is currently foreground, returns to launcher |
| `0x45` | DEVICE_STATUS_RESPONSE | device→phone | which app is foreground + battery + WiFi state, as an OS-level status distinct from the recorder-specific `STATUS_RESPONSE` |

Existing recorder-specific message types (`STATUS_REQUEST`,
`CMD_START/STOP/PAUSE_RECORDING`, `FILE_LIST_*`, `TRANSFER_SESSION_*`)
stay as-is, but should be understood going forward as *the Recorder app's*
command set, not the device's only command set — the pattern for a future
app to add its own message-type range, not something every app gets by
default. Real exact byte layouts TBD when this section is implemented;
treat the table above as a starting shape, same caveat the original spec
gave FILE_LIST_*/TRANSFER_SESSION_* before they were built and tested.

## Seed app ideas (design-validation targets, not v1 build list)

To sanity-check the app API surface without committing to building all of
these in v1:
- **Home Assistant dashboard/control** — needs WiFi/HTTP client access and
  a widget-style layout distinct from a fully custom-drawn app.
- **Notes / to-do list** — needs SD-based structured storage and reuse of
  the existing on-screen keyboard.
- **Media/clock/widget dashboard** — clock faces, timers, weather —
  mostly display + network, validates a lighter "widget" app shape.
- WiFi/BLE security tooling was considered and **deliberately deferred**
  pending the GhostESP investigation below — don't build this in CYDEOS
  without revisiting that decision first.

## Relationship to GhostESP

[GhostESP](https://github.com/GhostESP-Revival/GhostESP) (GPLv3, 1,000+
stars, actively maintained) is real, shipping prior art for almost the
exact "drop an app on the SD card" ecosystem described above — worth
recording in detail so this decision isn't re-litigated blind later:

- It's ESP-IDF-native (not Arduino) — a different toolchain than
  everything CYD-Voice-Recorder/CYDEOS is built on.
- Its core mission is wireless security research (deauth, evil portal,
  BadUSB, NFC/SubGHz/etc.) — not voice recording. It has no mic-capture/
  voice-recording feature today; its manifest schema even lists `audio`/
  `mic`/`microphone` as **reserved, not-yet-implemented** permissions —
  i.e., voice recording is a genuine gap CYDEOS fills, not a redundant
  effort.
- It **already supports CYD-family boards** (e.g. `CYD 2432S028R`, several
  `CYD2` variants) but not the specific Hosyond E32R35T or ES3C28P/CBD
  boards CYDEOS targets.
- Its native SD app system is real and battle-tested: apps are
  ESP-IDF shared objects (`.so`) loaded via Espressif's own `elf_loader`
  + `dlopen()`, described by a `manifest.json` with fields this spec's
  manifest draft above deliberately echoes (`id`/`name`/`version`/`entry`/
  `target`/`api_version`/`permissions`/`icon`/`requires_psram`) — this is
  **confirmation that native dynamic loading is achievable on ESP32
  hardware**, which de-risks (but doesn't eliminate) this spec's biggest
  open risk. Their permission list is enforced at runtime (unlike this
  spec's declarative-only choice), and apps are packaged into a custom
  `.gapp` streaming-archive format for one-file install.
- It also ships a sandboxed Lua runtime ("GhostScript"), a Cloud Store for
  installing apps/scripts on-device, and a generalized BLE+WebUI+Android
  companion app command surface across its whole feature set — all
  explicitly **out of scope for CYDEOS v1** (declared below).

**Decision: hybrid.** Keep building CYDEOS independently on the existing
Arduino/TFT_eSPI stack, but keep the manifest shape, SD directory layout,
and app-state/quarantine concept intentionally parallel to GhostESP's,
so that:
1. Lessons from their real-world experience (permission taxonomy, `target`
   field for cross-arch app binaries, state/quarantine tracking, tick-rate
   throttling for apps that redraw frequently) can be borrowed as CYDEOS's
   own loader is designed, without copying their enforcement model.
2. A CYDEOS app *could* plausibly be ported to GhostESP later (or vice
   versa) without a from-scratch rewrite, if a future checkpoint decides
   that's worthwhile.
3. Once CYDEOS has a working recorder app and native app loader, revisit
   whether contributing mic/audio-recording support to GhostESP (filling
   their reserved `audio`/`mic` permissions) is a better long-term use of
   effort than maintaining a fully parallel OS — this is an explicit
   future decision point, not a commitment either way.

**Not doing now:** merging codebases, adopting ESP-IDF or LVGL, building a
Cloud Store, or building a Lua/scripting runtime.

## Open risks / research spikes (must resolve before implementation locks in)

1. **Native app execution feasibility** — can position-independent code be
   loaded from SD and executed at runtime on the ES3C28P/CBD (and
   separately, classic ESP32) under the **Arduino** framework, without a
   full ESP-IDF migration? This is the spec's central unknown; everything
   in "Native SD app system" above is provisional until a minimal
   proof-of-concept exists.
2. **ABI/symbol stability** — if the loader resolves app calls against a
   fixed OS-exported function table (rather than GhostESP's full
   `dlopen()`-style linking), that table becomes a versioned contract
   between OS and app builds; need a plan for what happens when the OS
   changes and an old app's compiled binary expects an old table shape.
3. **Toolchain for building third-party apps** — same PlatformIO project
   structure as the OS itself (a sibling env producing a binary instead of
   a full firmware image), or a separate minimal SDK/template published
   later. Not decided.

## Explicitly out of scope for v1

- Sandboxing/permission enforcement for SD apps (direct hardware access
  was the deliberate choice)
- Background/multitasking apps beyond the recorder's existing OS-level
  background-recording special case
- WiFi/BLE-based app installation (manual SD copy only)
- Cloud store, Lua/scripting runtime, LVGL migration
- WiFi/BLE security/pentesting tooling (deferred pending the GhostESP
  contribution question above)
- Any new hardware/board beyond the existing two (HAL is designed for
  future boards, but none are targeted yet)

## Suggested milestones

1. **M0** — New repo scaffold; port OS core services (notification bar,
   settings shade, WiFi, BLE Companion transport, battery, clock) out of
   CYD-Voice-Recorder into a formalized shell, re-verifying each hard-won
   finding as it moves.
2. **M1** — Recorder ported as the first built-in app under the new
   app-lifecycle interface (`on_start`/`on_stop`/`on_tick`/`on_touch`/
   `on_draw`), with Recordings browser + Scriberr upload folded in as
   screens/tabs of that one app.
3. **M2** — BLE Companion protocol v2: generalize STATUS/app-list/launch
   message types alongside the existing recorder-specific ones.
4. **M3** — Native app loader feasibility spike on ES3C28P/CBD only:
   smallest possible "hello world" dynamically-loaded app, proving (or
   disproving) the core mechanism before investing further.
5. **M4** — Manifest format + launcher SD-app discovery, once M3 proves
   feasible; built-in apps optionally adopt the same manifest shape for
   consistency even though they don't need dynamic loading.
6. **M5 (stretch)** — Hosyond native app loading (if M3's findings permit
   at reduced scope); begin one seed app (most likely Notes or a
   clock/widget dashboard, as the lowest-complexity validation of the app
   API surface).
