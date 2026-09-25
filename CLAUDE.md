# CYDEOS

> **Inherited document.** This file is carried over from
> [CYD-Voice-Recorder](https://github.com/brandonvader/CYD-Voice-Recorder)'s
> `CLAUDE.md`, re-scoped for this repo's layout (no `esp32dev_oled`
> prototype env here; `src/cyd/main.cpp` is being split into `src/core/`
> + `src/apps/` + `src/boards/` per `CYDEOS Spec.md`). The hardware facts
> below (SPI/I2S/touch/BLE/battery findings) are still 100% applicable —
> the boards haven't changed — but **re-verify each one as it's ported**
> into the new code structure rather than assuming it survived the
> refactor unchanged. Sections describing old-repo specifics (single-file
> `switchToApp()`, Scriberr living in OS Settings, the v1 BLE Companion
> protocol) are noted inline where this repo's plan diverges. See
> `CYDEOS Spec.md` for the actual architecture and milestone plan this
> repo is building toward.

PlatformIO + Arduino framework project. Two hardware targets, sharing one
`src/` tree, kept apart by a `BOARD_*` macro selected per-env in
`platformio.ini`; board-specific pins/geometry live in
`src/boards/board_config.h`, selected by that same macro:

- **`cyd`** (`BOARD_HOSYOND_35`) — "Hosyond 3.5" 320x480" CYD, model
  ESP32-32E, resistive touch, onboard micro SD slot (no external SD reader
  needed on this board), and real vendor-designed battery circuitry
  (E32R35T variant - JST battery port + TP4054 charge IC). See the "Hard-won
  CYD findings" section below before changing anything I2S/touch/SD/
  display related — several things that look like reasonable code are
  actually workarounds for confirmed, non-obvious hardware/driver
  conflicts.
- **`cyd_es3c28p`** (`BOARD_ES3C28P`) — a second CYD model, internal
  nickname "CBD": 2.8" ESP32-S3 CYD (lcdwiki ES3C28P), 240x320 ILI9341
  display, **capacitive** FT6336G touch over I2C, SD over SDIO (not SPI),
  and a built-in mic behind an ES8311 audio codec instead of a bare
  INMP441. See "Hard-won ES3C28P findings" below.

### Hard-won CYD findings (don't re-debug these)

- **This specific physical unit occasionally needs a full power-off
  (unplug the USB cable, not just a RESET tap) every few flashes, or SD
  mount fails** with inconsistent error codes across the retry attempts
  (`ESP_ERR_NOT_SUPPORTED`, `ESP_ERR_INVALID_CRC`,
  `ESP_ERR_INVALID_RESPONSE` - a different one each attempt, not the same
  failure repeating) - a marginal/flaky electrical connection, not a
  firmware bug. Confirmed by diffing against a known-good commit: zero
  SD-mounting-related code had changed when this was hit. If SD mount
  starts failing after a normal reflash and the SD card itself seems fine,
  try a full power-off/power-on cycle before assuming a code regression.
- **Display**: ST7796 driver over SPI, not parallel. TFT_eSPI config (in
  `platformio.ini` build_flags): MOSI=13, SCLK=14, MISO=12, CS=15, DC=2,
  `USE_HSPI_PORT=1`. Backlight pin is genuinely ambiguous between GPIO21 and
  GPIO27 across sources/units — firmware drives GPIO27 high manually as a
  belt-and-suspenders fallback alongside TFT_eSPI's own GPIO21 handling.
- **Touch (XPT2046) shares the display's own SPI bus** (MOSI=13/SCLK=14/
  MISO=12), with its own CS=33 — confirmed against
  https://www.lcdwiki.com/3.5inch_ESP32-32E_Display. It does **not** share
  pins with the mic (32/25/39). An earlier bit-banged test on 32/25/39 that
  looked like touch data was actually electrical crosstalk from the
  display, not real touch traffic — a misdiagnosis that led to an
  unnecessary BOOT-button-only workaround (removed). Touch now uses
  TFT_eSPI's built-in `tft.getTouch()` and coexists fine with the mic,
  including mid-recording — pause is fully functional via touch. The BOOT
  button (GPIO0) was also found to read spurious "pressed" states for
  seconds at a time with zero physical interaction (a real hardware/noise
  issue) and is no longer wired up as a control at all; touch is the sole,
  verified-reliable input.
- **SD card must be forced onto SPI3_HOST (VSPI), not the default.**
  `SDSPI_HOST_DEFAULT()` defaults to SPI2_HOST (HSPI) — the same physical
  peripheral TFT_eSPI's `USE_HSPI_PORT` uses, despite different pins. This
  silently "worked" for light access (mount, occasional reads) but reliably
  broke (SD writes timing out with `sdmmc_read_sectors_dma` / 0x108 errors)
  once I2S's interrupt handling added timing pressure. Fix: explicitly set
  `host.slot = SPI3_HOST` in `mountSD()`. Do not remove this.
- **Mic channel offset is calibrated exactly once, at boot, before the SD
  card is ever mounted.** Calling `i2s_read()` immediately before an SD
  `fopen()` reliably broke that specific SD access even after the SPI3_HOST
  fix above (opening a *new* file involves more directory-table SD reads
  than a plain `fwrite()` to an already-open file, and seems more sensitive
  to timing). Recalibrating per-recording isn't needed anyway since it's a
  fixed property of the wiring — just don't move this calibration call to
  happen right before a file-open.
- **I2S is installed exactly once at boot and never uninstalled.**
  Switching between "I2S owns pins 32/25/39" and "touch bit-banging owns
  them" is done cheaply with `i2s_start`/`i2s_stop` plus re-pointing the
  GPIO matrix (`i2s_set_pin` vs `pinMode`), not full driver install/
  uninstall cycles.
- **Debounce the BOOT button properly** (require the new reading to be
  stable for the whole debounce window, not just react to the first
  differing sample) — a naive single-sample edge check let mechanical
  contact bounce fire several rapid start/stop toggles per physical press.
- **BLE uses NimBLE-Arduino (`h2zero/NimBLE-Arduino`), not the classic
  Bluedroid `BLEDevice.h`** — deliberately, since the architecture only
  ever needs BLE (see `CYDEOS Companion Spec.md`) and Bluedroid's combined
  classic+BLE stack is meaningfully larger. Flash usage jumped from ~70%
  to ~90% just adding NimBLE — there isn't much headroom left for the
  file-list/HTTP-transfer phases still to come; watch `pio run` size output.
  NimBLE-Arduino 2.x quirks that cost real debugging time:
  - The device name is **not** advertised by default, and scan response is
    **disabled** by default (both breaking changes from 1.x). Without
    `advertising->enableScanResponse(true)` + `advertising->setName(...)`
    the device is connectable but shows up nameless in scans, breaking the
    app's `CYDEOS-XXXX` name filter.
  - Call order matters: `enableScanResponse(true)` **before** `setName()`.
    The 128-bit service UUID already fills nearly all of the 31-byte
    primary advertisement payload, so `setName()` needs scan response
    already enabled to know to put the name in the separate scan-response
    packet instead of the full primary one (where it silently gets
    dropped).
  - `NimBLECharacteristicCallbacks::onWrite`/security callbacks
    (`onPassKeyDisplay`, `onAuthenticationComplete`, etc.) run on NimBLE's
    own FreeRTOS task, not the Arduino `loop()` task — they must not touch
    the display or SD card directly (same class of cross-task SPI hazard as
    the SPI3_HOST fix above). They only set a small pending flag; `loop()`
    does the actual work.
  - Writing to a `WRITE_ENC`-protected characteristic before pairing
    doesn't fail fast against BlueZ — it stalls waiting for a pairing
    agent (see `scripts/ble_test.py`, which times that out deliberately
    rather than hanging forever). A real phone's OS pairing dialog handles
    this normally; there's no equivalent quick host-side agent set up here.
- **Switched `board_build.partitions` to `huge_app.csv`** (one 3MB app
  partition instead of the default two 1.31MB OTA slots) once BLE pushed
  flash to ~90%. This project does no OTA updates and no SPIFFS (SD card is
  the only storage), so the default scheme's second OTA slot was pure
  waste. Same NVS partition offset/size as before, so saved WiFi/settings
  survive the switch - confirmed on a real reflash. If OTA is ever wanted
  later, this will need revisiting.
- **The BLE file-list/transfer-session handlers only run while
  `recState == REC_IDLE`.** Both do real work in a single `loop()` pass
  (directory scans, `stat()` calls, WiFi mode switches) that would compete
  with the same I2S/DMA timing sensitivity documented above if attempted
  mid-recording. Requests made while recording get a `BLE_ERR_BUSY_RECORDING`
  error reply instead of being attempted.
- **Recording timestamps for `FILE_LIST_ENTRY`/the transfer HTTP `/files`
  endpoint are parsed back out of the filename** (`rec_YYYYMMDD_HHMMSS.wav`,
  local time) via `mktime()`, not read from the FAT filesystem's own file
  timestamps - `esp_vfs_fat` only stamps real timestamps if a custom
  `get_fattime()` is wired up, which this project doesn't do, so `stat()`
  timestamps would otherwise be a meaningless fixed default.
- **This board has real, vendor-designed battery circuitry** (the
  E32R35T variant - confirmed via lcdwiki's own schematic PDF,
  `3.5inch_ESP32-32E_E32R35T_Schematic.pdf`), not an aftermarket bodge:
  a built-in JST battery port, a TP4054 single-cell linear charge IC, and
  a "Battery level detection circuit" block with a 100K/100K divider
  (R2/R3) wired directly across the raw `BAT+` battery line, tapped by
  `IO34` (labeled `BAT_ADC` in the vendor's own IO resource table). No
  CHRG status pin from the TP4054 is broken out to any GPIO - see
  "Battery status" below for what that means for charging/not-connected
  detection. `IO34` was unused by anything else on this board, matching
  the vendor's intent for it.

### Hard-won ES3C28P findings (don't re-debug these)

- **Different SoC family, not just different pins.** ESP32-S3 (Xtensa LX7,
  8MB OPI PSRAM, 16MB flash), not classic ESP32 — its own PlatformIO env
  (`board = esp32-s3-devkitc-1`), `board_build.arduino.memory_type =
  qio_opi`, `board_upload.flash_size = 16MB`.
- **`platform` is pinned, not left to float like the other env**
  (`platform = espressif32@7.1.3`), specifically to stay clear of
  [esp-idf#18621](https://github.com/espressif/esp-idf/issues/18621): ES8311
  mic capture via the legacy `driver/i2s.h` API (what this project uses)
  returns silent/constant samples on ESP-IDF 5.5.1+. Verified 7.1.3 bundles
  `framework-arduinoespressif32@4.20017` = IDF 4.4.7, well clear of the
  affected range. Re-verify this before ever bumping the pin.
- **`arduino-audio-driver` needs `-std=gnu++17`, not Arduino's default
  `gnu++11`.** Its codec classes (e.g. `TLV320AIC3104`, pulled in
  transitively by `AudioBoard.h` even though only `AudioDriverES8311` is
  used) declare `constexpr static` data members of non-integral type.
  Pre-C++17 these need an out-of-class definition if ODR-used, or the link
  fails with "undefined reference" — C++17 makes them implicitly `inline`,
  which the library assumes. Fixed via `build_unflags = -std=gnu++11` +
  `build_flags = -std=gnu++17` in this env only.
- **The vendor's documented TFT_eSPI ESP32-S3 DMA patch (a `dma_end_callback`
  fix) is already merged upstream** in `bodmer/TFT_eSPI@2.5.43` (the version
  this project already pins for both CYD envs) — verified by reading the
  installed library directly. No patch script needed; don't add one unless
  a future TFT_eSPI bump regresses this.
- **`USE_HSPI_PORT=1` is required in build_flags on ESP32-S3, not just
  classic ESP32 — without it, `tft.init()` crashes** with a
  `Guru Meditation Error: ... (StoreProhibited)` at `EXCVADDR: 0x00000010`,
  inside `writecommand()`/`begin_tft_write()`, on the very first SPI
  transaction. Root cause (confirmed against real hardware and
  [Bodmer/TFT_eSPI#3743](https://github.com/Bodmer/TFT_eSPI/issues/3743)):
  without this flag, TFT_eSPI's ESP32-S3 processor code resolves its default
  `SPI_PORT` (`FSPI`) to an invalid host index (`0`) on S3, and writes to a
  near-null register address. `USE_HSPI_PORT` resolves to a valid, distinct
  host index instead. This isn't about which physical SPI peripheral is
  "correct" for this board (unlike Hosyond's genuine SPI-bus-sharing
  concern) — it's working around a TFT_eSPI bug in its ESP32-S3 port.
- **This panel needs `TFT_INVERSION_ON=1`, or the whole screen renders
  solid white instead of black** — confirmed on real hardware. A common
  quirk of ILI9341V clone panels; TFT_eSPI's bundled generic
  `Setup70b_ESP32_S3_ILI9341.h` example doesn't need it, so don't assume
  it's universal to ILI9341 — it's specific to this panel batch/vendor.
- **ES8311 codec bring-up uses `pschatzmann/arduino-audio-driver`'s
  `AudioDriverES8311`, for I2C register init only** — it never touches the
  I2S peripheral itself, so capture still goes through this project's
  existing legacy `i2s_read()` loop, just retargeted to this board's I2S
  pins. `output_device = DAC_OUTPUT_NONE` for now (capture-only v1) — the
  seam for adding speaker playback later is changing just that config
  field, not the capture path. A `delay(20)` after driving `AUDIO_EN_PIN`
  low (before the first I2C transaction) fixed a consistent NACK/timeout on
  the codec's address during boot — its power rail needs a moment to settle.
  This mostly (not 100%) eliminates it; an occasional single NACK
  (`->p_wire->endTransmission: 2`) still shows up on some boots and is
  harmless — confirmed via multiple real recordings with healthy, correct
  levels (peak around -5 to -18 dBFS, not clipped or silent) immediately
  after a boot that logged the warning. Don't chase this further without a
  concrete symptom (e.g. an actually-bad recording) reproducing it.
- **UI is scaled to 67% via `UI_SCALE(px)`** (`board_config.h`'s
  `UI_SCALE_PCT`) — not the naive axis ratios (75% width, 66.7% height). In
  a portrait UI, vertical stacking is the binding constraint, and the
  *real* ceiling is set by the most vertically-dense screen, not a simple
  screen-dimension ratio — the timezone picker's 10 fixed-height rows only
  have ~8px of slack at 100% scale on the original 480px-tall Hosyond
  screen and need ≤~67.8% to fit in 320px. Other screens tolerate a higher
  percentage (WiFi scan panel ~72%, time/password/keyboard screens
  ~81-91%); the recorder screen's spectrum area is elastic (fills whatever
  is left) so it can't overflow at all. **This ratio needs re-checking
  once screens are re-laid-out under the new app-shell structure** — if
  new fixed-height stacked content is added to any screen, re-check
  whether it becomes the new tightest constraint before assuming 67% is
  still safe.
- **v1 core recorder path confirmed working end-to-end on real hardware**
  (in CYD-Voice-Recorder, pre-port): display, capacitive touch (coordinates
  land correctly against on-screen buttons), SD_MMC mount + file I/O, and
  mic capture all verified via a full record → stop → WAV analysis cycle
  (sample count × 2 bytes matched the file's PCM data size exactly, both
  times — no dropped/sped-up audio, the same class of bug documented for
  Hosyond above). **Re-verify this again once the recorder is ported into
  this repo's app-shell structure.**
- **Touch (FT6336G) is hand-rolled I2C register reads**, not a pulled-in
  library — addr `0x38`, reads 5 bytes starting at reg `0x02`: touch count
  + X/Y hi/lo. No calibration data needed, unlike Hosyond's resistive
  touch.
- **SD is SDIO/`SD_MMC`, not SPI** — `SD_MMC.setPins()` + `SD_MMC.begin()`
  mount at the same `/sdcard` VFS path the SPI-mounted Hosyond board uses.
- **Native USB CDC, not a CH340 bridge** — this board enumerates as
  `/dev/ttyACM0`, not `/dev/ttyUSB0`. Run `pio device list` to confirm
  before flashing/monitoring; don't assume the port from the other env.
- **Internal nickname: "CBD" ("cheap black display")**, spoken/written
  form only — matches "CYD" for the Hosyond board. Doesn't rename anything
  in code/config (still `cyd_es3c28p`/`BOARD_ES3C28P` everywhere).
- **Automatic reset-into-bootloader over this native USB connection was
  unreliable early on, but has since run 10/10 clean automatic
  `pio run -t upload` cycles with zero manual intervention** in the same
  dev session - so this is NOT a hard "always needs the manual dance" rule.
  esptool.py already auto-detects this exact chip's built-in
  USB-Serial-JTAG peripheral (PID `0x1001`) and always uses its
  `USBJTAGSerialReset` strategy regardless of any `--before` flag passed -
  there's no missing esptool flag to add. That strategy is still just a
  timed DTR/RTS toggle mapped through silicon to GPIO0/EN, so it depends on
  the same electrical reset-circuit assumptions as any board; intermittent
  failure (repeated pySerial "Write timeout" during esptool's "Connecting…"
  handshake, worse when the board was already crash-looping - a moving
  target for the handshake) is a commonly-documented class of issue on
  clone ESP32-S3 boards specifically traced to a missing/undersized
  capacitor on EN. **ModemManager was also found active on the original
  dev machine with no exclusion for Espressif's USB vendor ID (303a)** -
  a well-known separate source of intermittent native-USB-CDC reset
  interference (it briefly probes new serial devices, which can race with
  esptool's own reset sequence); add `SUBSYSTEM=="usb",
  ATTRS{idVendor}=="303a", ENV{ID_MM_DEVICE_IGNORE}="1"` to a
  `/etc/udev/rules.d/*.rules` file (then `udevadm control --reload-rules &&
  udevadm trigger`) as a standing precaution regardless of whether it was
  the actual cause. Verify on the *parent USB device*, not the tty node:
  `udevadm info -q property -n /dev/ttyACM0` will **not** show
  `ID_MM_DEVICE_IGNORE` even when the rule is correctly applied, since
  `ENV{}` assignments made while processing the parent `usb_device` don't
  appear on the child tty device's own udev-database entry - only on the
  parent's. Check the parent instead:
  `udevadm info -q property -p "$(udevadm info -q path -n /dev/ttyACM0 |
  sed 's/\/tty\/ttyACM0$//')"`  (or just trim the path down to the `usbN/N-N`
  segment shown by `udevadm info -a -n /dev/ttyACM0`) and look for
  `ID_MM_DEVICE_IGNORE=1` there. ModemManager's own code walks up to the
  physical USB device to check this flag even though `udevadm info` won't
  show it as "inherited" on the child - don't mistake the child-node query
  coming up empty for the rule having failed.
  If automatic upload does fail: hold BOOT, tap RESET while still holding
  BOOT, then release BOOT, *then* run the upload. After a successful
  upload, if the board comes back up showing `boot:0x2
  (DOWNLOAD(USB/UART0))` / `waiting for download` instead of actually
  running (check via `scripts/serial_read.py`), BOOT was likely still
  physically held through the upload's own closing reset — release BOOT and
  tap RESET once more to boot normally. A project-local `esptool.cfg` (with
  `serial_write_timeout`/`connect_attempts`/`open_port_attempts`/
  `reset_delay` under an `[esptool]` section) is also available as a
  lower-effort retry/timeout tuning lever if flakiness returns.
- **Opening the serial port (e.g. via `scripts/serial_read.py`) resets the
  board** on this env, unlike the CH340-based board — every connection
  shows a fresh ROM boot banner at the top of the capture. Useful to know
  when timing a capture around a manual on-device action (touch a button,
  etc.) — the board will have just rebooted, so budget a few extra seconds
  in the capture window before the action actually happens.

### WiFi settings findings (board-agnostic - this code is shared)

- **A password is only ever persisted to NVS once `WiFi.status() ==
  WL_CONNECTED` is actually observed** (using `WiFi.psk()` to recover the
  password rather than threading it through as state) — never at the
  moment a connection is merely *attempted*. Confirmed on real hardware
  that saving eagerly caused a real problem: a mistyped password got saved
  with `autoConnect=true` regardless of outcome, then retried on every
  future WiFi-enable — and `WiFi.scanNetworks()` while that retry is in
  flight can return 0 results, silently, with no error (an ESP32
  WiFi-driver limitation, not a bug in this app's scan-handling code) — so
  scanning looked permanently broken until the stale credential was
  cleared.
- **A saved network's credentials now auto-clear after ~15s of failing to
  connect** — this is what actually recovers a device stuck with a bad
  saved password (confirmed on real hardware). Background auto-connect
  calls `WiFi.begin()` directly rather than going through the normal
  connect-attempt path, so it needs its own timeout tracking, separate
  from the foreground connect-attempt path.
- **A "tap outside the panel to back out" condition must not also fire on
  any tap while a scan is still in progress** (a `scanResultCount == -1`
  sentinel state) — since `-1 <= 0` is an easy off-by-one to reintroduce,
  and it silently made scanning look like it always failed (repeated "WiFi
  scan started" with no matching "WiFi scan complete" until fixed). Only a
  confirmed-empty result (`scanResultCount == 0`) should back out on any
  tap; a tap during an in-progress scan should fall through to the normal
  row-index bounds check.
- **Password entry should reveal only the most-recently-typed character in
  plain text** (cleared on backspace/re-entering the screen), plus a
  tappable "Show"/"Hide" toggle to reveal the whole thing — added after a
  real mistyped-password incident traced back to the on-screen keyboard's
  key size.
- **Settings > WiFi > Saved Networks** should let the user disconnect,
  forget, or flip auto-connect for any saved network, with only one
  network allowed `autoConnect=true` at a time (turning it on for one
  clears it on every other saved network first) — the auto-connect loop
  stops at the first match anyway, so allowing the UI to show multiple
  networks as "auto" would be misleading about what actually happens.

### Recorder app: transcription upload (board-agnostic - shared code)

**Architecture note (this repo diverges from CYD-Voice-Recorder here):**
in CYD-Voice-Recorder, Scriberr config lived in OS-level Settings and
upload was a separate "Recordings" app. In CYDEOS, both live **inside the
Recorder app** (Recordings browser + transcription upload + transcription
settings, all as part of one app) — see `CYDEOS Spec.md`'s "Built-in
apps" section for why. The technical findings below are unaffected by
that reorganization.

Manual, per-recording upload to a self-hosted
[Scriberr](https://github.com/rishikanthc/Scriberr) instance for
transcription, streaming the WAV straight from SD over a raw
`WiFiClient` (multipart/form-data, field `audio`, `X-API-Key` header) to
Scriberr's `POST /api/v1/transcription/submit` — confirmed against a real
instance (auth header, endpoint, and the project's exact WAV format -
16-bit PCM/16kHz/mono - all verified via a live test upload that completed
a full transcription job end-to-end before any firmware was written).

- **Host and port are separate settings fields, not a combined
  "host:port" string** - the shared on-screen keyboard's symbol rows have
  no `:` character, discovered the first time this screen was actually
  used on real hardware. Port defaults to 80 if left blank. Don't go back
  to a combined field without also adding a colon to the keyboard. If a
  future second transcription backend is added, keep this per-field
  pattern rather than reintroducing a combined string.
- **Calling `WiFiClient::connect()` while WiFi has never been turned on
  crashes**, not a clean connection-failed return - confirmed on real
  hardware: `assert failed: tcpip_send_msg_wait_sem
  IDF/components/lwip/lwip/src/api/tcpip.c:455 (Invalid mbox)`, then a
  full reboot. WiFi defaults to `WIFI_OFF` at boot and only becomes
  `WIFI_STA` once the user manually toggles it on in Settings > WiFi -
  reaching the upload screen and tapping Upload without ever visiting
  WiFi settings first hits this directly. Fixed by requiring
  `WiFi.status() == WL_CONNECTED` (not just "configured") before the
  Upload button is even enabled, **and** as a second defensive check
  inside the upload function itself in case of a race between the button
  being enabled and the tap landing. Don't remove either check - both are
  load-bearing, confirmed via a real crash and a real fix.
- **The upload is fully blocking**, not polled from `loop()` like WiFi
  connect/scan - a deliberate simplification given uploads are a rare,
  manually-triggered action while the user is already looking at the
  screen. Safe specifically because it's only reachable while
  `recState == REC_IDLE`, so no I2S/DMA timing is at stake.
- Transcription itself is async on Scriberr's side (upload returns a job
  ID immediately; the device doesn't poll for the result - deliberately
  out of scope, matching the "fire-and-forget" decision). Check
  transcripts in Scriberr's own UI.
- **The upload `title` field is prefixed with this device's `CYDEOS-XXXX`
  name** (see "Device identity" below) - e.g. `CYDEOS-795A_rec_....wav` -
  so uploads from multiple CYDEOS devices are distinguishable in
  Scriberr's job list. Confirmed on real hardware with both boards
  uploading to the same instance.

### Device identity: `cydeosDeviceName` (board-agnostic - this code is shared)

A single `"CYDEOS-XXXX"` identifier (derived from the last 2 bytes of the
**Bluetooth** MAC, computed once near the top of `setup()`) is shared by:
- BLE advertising name.
- WiFi hostname (`WiFi.setHostname(cydeosDeviceName)`, called right after
  every `WiFi.mode(WIFI_STA)` - there are two call sites, WiFi-enable and
  the BLE-transfer-session STA-restore path; both need it). Before this
  fix, WiFi hostname fell back to Arduino-ESP32's generic default
  (`<chip>-XXXXXX`, derived from the **WiFi** MAC - a different address
  than the Bluetooth MAC used for the BLE name, so the two would never
  have matched even if both had been set independently).
- The transcription-upload title prefix (see above).

Confirmed on real hardware: router's DHCP client list shows
`CYDEOS-795A`/`CYDEOS-B75D` instead of a generic default, matching each
board's own BLE name exactly.

### Battery status (board-agnostic - this code is shared)

A small battery icon in the notification bar, just left of the clock,
shows one of `BATTERY_NOT_CONNECTED`/`LOW`/`HALF`/`FULL`/`CHARGING` -
both boards have real battery hardware, confirmed via each board's own
vendor schematic. Sampled every 5s via `analogReadMilliVolts()`, which
already applies Arduino-ESP32's ADC calibration curve rather than a raw
`analogRead()` + linear math. `BATTERY_ADC_PIN` and `BATTERY_DIVIDER_RATIO`
are per-board constants in `board_config.h`, each calibrated against a
real multimeter reading at that board's JST connector - **don't reuse one
board's ratio for the other**, they're wired with different resistor
pairs:

| Board | ADC pin | Ratio | Calibration reading |
|---|---|---|---|
| `cyd` (Hosyond E32R35T) | IO34 (vendor-documented `BAT_ADC`) | 1.940 | 3.642V measured vs. 1877.5mV avg raw |
| `cyd_es3c28p` (ES3C28P/CBD) | IO9 (vendor pin table) | 2.011 | 4.1585V measured vs. 2067.9mV avg raw |

Both ratios are close to a nominal 100K/100K (2:1) divider, within normal
resistor tolerance - confirmed via each board's schematic/pin table, not
guessed.

- **Thresholds**: `<1.0V` → `NOT_CONNECTED`, `<3.4V` → `LOW`, `<3.9V` →
  `HALF`, else `FULL` (roughly the standard single-cell Li-ion curve - not
  independently derived per board, since draining either battery to
  confirm each boundary wasn't done).
- **`NOT_CONNECTED` cannot be reliably detected on either board, for two
  different underlying reasons - confirmed on real hardware, not a
  guess on either one.**
  - **ES3C28P/CBD**: unplugging the battery's JST connector (board still
    on USB power) didn't move the ADC reading at all (stayed
    ~4.1-4.19V). `BATTERY_ADC_PIN` there senses a charge-managed
    **system rail** that stays held near battery-float-voltage by the
    onboard charge circuit whenever USB is connected, regardless of
    whether a battery is actually installed.
  - **`cyd`/Hosyond**: despite its divider (R2/R3, confirmed via the
    E32R35T schematic) being wired directly across the raw `BAT+` battery
    line rather than a regulated system rail, unplugging the battery here
    instead made the reading **rise** to ~4.1-4.15V (from a ~3.77V
    baseline) - the opposite symptom, but the same root cause category:
    the TP4054 charge IC's own output regulates toward its ~4.2V
    constant-voltage setpoint once there's no battery load pulling the
    node down to the real cell voltage, so an unloaded charger output
    looks electrically like "battery present and full."
  - Net result on both boards: "no battery, running on charger/USB power"
    and "battery present and full" are electrically indistinguishable
    from a simple ADC threshold. The `<1.0V` `NOT_CONNECTED` threshold is
    a vestigial fallback for a genuinely grounded/shorted pin on either
    board, not a real-world-reachable state during normal use. Don't
    spend more time trying to fix this without adding a hardware signal
    (e.g. a charge-IC status pin) - it's a wiring/circuit-topology
    limitation on both boards, not a firmware bug.
- **The actual portable use case - running on battery alone, no USB -
  was verified on the ES3C28P/CBD and works correctly** (not yet
  re-verified on `cyd` after the original refactor this was found in). With
  the battery topped off and the icon showing `FULL`, USB was unplugged for
  ~20-30s (board stayed powered from the battery) and the icon still
  correctly showed `FULL` throughout; readings resumed normally after
  reconnecting USB.
- **`CHARGING` is inferred from a rising voltage trend, not a dedicated
  charge-status pin** - neither board breaks one out. **Validated (and
  fixed) on real hardware** on the ES3C28P/CBD: the battery ran down
  overnight and was found recharging the next session. v1's logic (raw
  single-sample readings 30s apart vs. a flat 20mV threshold) flickered
  visibly between `LOW` and `CHARGING` every 30-60s - the real charge rate
  measured live was only ~6-12mV/30s (under the 20mV threshold), while
  individual raw samples 5s apart jumped by up to 8mV on their own, so a
  single noisy sample at either end of the 30s comparison could push the
  delta over the threshold in either direction essentially at random.
  Fixed with a rolling average (~30s) to suppress sample noise before
  computing the trend, a threshold tuned down to the actually-observed
  charge rate, and a streak requirement (2 consecutive same-direction
  windows) before flipping `CHARGING` on or off.
  **`CHARGING` is still genuinely best-effort, not just conservatively
  tuned.** Real voltage was observed to plateau for minutes at a time
  while still connected to the charger mid-curve, not just near full -
  likely the charge IC periodically pausing to sense true open-circuit
  voltage (a known technique in cheap linear chargers). During a plateau
  there's no rising signal to detect, so the icon correctly falls back to
  `LOW`/`HALF`/`FULL` instead of `CHARGING`, even though the battery is
  physically on power. A serial trend-debug log line is worth keeping
  wired up (as it was in CYD-Voice-Recorder) so this stays diagnosable —
  don't treat "icon says HALF while charger is plugged in" as a bug report
  without checking those numbers first.
- Icon rendering: a small outlined body + terminal nub, solid-color-filled
  proportional to level (red/yellow/green for low/half/full) or solid
  aqua for charging, empty outline only (no fill) for not-connected -
  deliberately no lightning-bolt glyph or similar fine detail, since the
  icon is only ~16x9px on the ES3C28P/CBD's resolution and a solid,
  distinct fill color per state was judged more legible than fine pixel
  art at that size. Same absolute pixel size on `cyd` (not run through
  `UI_SCALE()`) since that board's notification bar has plenty of room at
  its native 320x480 resolution.

### CYDEOS Companion (BLE + WiFi transfer)

**Protocol note:** what's described here is the v1 protocol as
implemented in CYD-Voice-Recorder (recorder-specific commands only). This
repo's `CYDEOS Spec.md` drafts a v2 that generalizes the same transport
(framed messages over the same Command/Response characteristics) to also
cover app-list/launch/status — not yet merged into
`CYDEOS Companion Spec.md`. The hardware/protocol findings below still
apply regardless of which message types ride on top.

- **The BLE pairing passkey overlay must be scaled per-board** (a
  `UI_TEXT_SIZE_LARGE` per-board macro, wrapped in `UI_SCALE()`) rather
  than hardcoded absolute pixel offsets tuned only against the Hosyond
  screen.
- **Confirmed working end-to-end on real hardware with the real CYDEOS
  Companion Android app** (in CYD-Voice-Recorder): pairing (passkey
  display + phone-side entry), remote start/stop recording (byte-exact WAV
  output, no dropped/sped-up audio), and the WiFi transfer session (SoftAP
  handoff, file listing, and file download all worked in the app).
- **`scripts/ble_test.py` takes an optional `name_substring` argument**
  (`ble_test.py 20 B75D`) to target one board when more than one
  `CYDEOS-*` device is advertising at once — without it, the scanner just
  grabs whichever `CYDEOS-*` device it finds first.
- **Known non-fatal issue: the WiFi transfer session logs WiFi-driver
  errors every time it starts and stops** (`netstack cb reg failed with
  12308`, `wifi: timeout when WiFi un-init`) - confirmed functionally
  harmless across multiple real test runs, but NOT actually fixed despite
  two attempts at a settle-delay fix on both sides of the STA/AP
  transition. One error line was observed printing interleaved
  mid-character with an unrelated `Serial.println()`, proving these are
  async WiFi-driver-task logs. Current best (unconfirmed) explanation:
  WiFi/BLE coexistence contention, since the transition happens while a
  BLE connection is simultaneously active. If this ever becomes a real
  problem, the next thing to try is pausing/ending the BLE connection
  itself around the WiFi mode switch, not further WiFi-side sequencing
  changes.

### UI architecture

**This section describes CYD-Voice-Recorder's single-file
`src/cyd/main.cpp` structure and needs to be re-derived for this repo's
`src/core/` + `src/apps/` split** (see `CYDEOS Spec.md`'s "Native SD app
system" for the app-lifecycle interface this repo is moving to instead).
The underlying UI/input *behavior* below is still correct and should
carry over even though the file structure won't:

- The notification bar (top, always visible) and settings shade (drops
  down from it — brightness slider, PWM-dimmed with a floor so it's never
  fully dark, persisted in NVS) are global, available from any screen.
- Recording itself (the I2S read/SD write) keeps running regardless of
  which app is on screen — only gated on recording state, never on which
  app is active — so it continues in the background if the user switches
  apps. Only the *drawing* is gated on the active app, so other apps'
  screens never get painted over. **In this repo, this remains an
  OS-level special case for the Recorder app specifically — see
  `CYDEOS Spec.md`'s "Execution model," which deliberately does not expose
  general background/multitasking capability to other apps.**
- Touch gestures should share one gesture-mode state machine (e.g.
  `GESTURE_NONE` / `GESTURE_SLIDER` / `GESTURE_BOTTOM_SWIPE`) so a
  touch-down can only ever be interpreted as one gesture at a time.
  Resistive touch has brief signal dropouts even mid-drag that looked like
  a release+re-press; a touch should only be treated as truly released
  after ~80ms of continuously reading "untouched," otherwise a dropout
  during a slider drag could close the shade and immediately register as a
  button press underneath.

`cyd` shows up as a generic CH340 adapter at `/dev/ttyUSB0`;
`cyd_es3c28p` enumerates as native USB CDC at `/dev/ttyACM0` instead (see
"Hard-won ES3C28P findings"). Run `pio device list` if unsure which is
plugged in, and always pass `-e <env>` explicitly on build/upload/test
commands rather than relying on whatever env happens to be default.

## Toolchain

PlatformIO Core lives in a dedicated venv at `~/.platformio-venv` (Ubuntu
25.10 externally-manages system Python, so it isn't installed globally).
`pio` and `platformio` wrapper scripts in `~/.local/bin` (on PATH) call into
that venv, so just run `pio ...` normally. If a fresh machine/clone has
neither: `python3 -m venv ~/.platformio-venv && ~/.platformio-venv/bin/pip
install platformio`, then create the two wrapper scripts (each just
`exec "$HOME/.platformio-venv/bin/<name>" "$@"`) in `~/.local/bin`.

## Common commands

- Build: `pio run -e cyd` or `pio run -e cyd_es3c28p`
- Flash: `pio run -e <env> -t upload`
- Both envs bake the clock in at flash time from the host's clock, as a
  fallback until it can NTP-sync itself (against us.pool.ntp.org, on WiFi
  connect). The system clock always holds true UTC; local time is derived
  via `localtime_r()` with `TZ` hardcoded (a real timezone setting belongs
  in a future Settings > Time screen, per CYD-Voice-Recorder's own
  roadmap). This means the build flag must be **plain UTC epoch**
  (`date +%s`), not a local-time trick value — always flash with:
  `LOCAL_EPOCH=$(date +%s) && PLATFORMIO_BUILD_FLAGS="-DBUILD_LOCAL_EPOCH=${LOCAL_EPOCH}UL" pio run -e cyd -t upload`
  (swap `-e cyd` for `-e cyd_es3c28p` as needed). Skipping this falls back
  to a stale hardcoded default.
- Native unit tests (pure logic, no hardware, fast): `pio test -e native`
  — put these in `test/test_native/`
- On-device unit tests (flashes the board and runs Unity over serial):
  `pio test -e cyd` — put these in `test/test_embedded/`. (CYD-Voice-
  Recorder ran these against its now-removed `esp32dev_oled` prototype
  env; retargeted at `cyd` here since that env no longer exists.)
- Static analysis: `pio check -e <env>`

## BLE testing from this host

`scripts/ble_test.py [scan_timeout_seconds]` (uses `bleak`, installed in the
same `~/.platformio-venv`) validates what's checkable without a phone:
device discoverable as `CYDEOS-XXXX`, GATT service/characteristic UUIDs
match `CYDEOS Companion Spec.md` exactly, and writing to the Command
characteristic without pairing doesn't silently succeed. It deliberately
does **not** attempt full BLE passkey pairing — that needs a registered
BlueZ pairing agent capable of keyboard entry, which isn't set up on this
host. Full pairing + command/status round-trip validation needs the real
CYDEOS Companion Android app.
If a stalled unpaired-write test leaves the device stuck `Connected: yes`
in `bluetoothctl`, `bluetoothctl disconnect <MAC>` clears it (the ESP32
resumes advertising on disconnect).

## Serial monitor / reading device output

`pio device monitor` requires a real interactive TTY and will error out
("requires an interactive terminal on stdin") when run non-interactively,
which is how agent tool calls invoke shell commands. Instead use
`scripts/serial_read.py [seconds] [port] [baud]`, which reads the port
directly via pyserial and prints what it captured. It uses the same
pyserial installed alongside PlatformIO — no separate install needed.

## Hardware permissions

The user's account was added to the `dialout` group to access
`/dev/ttyUSB0` without sudo. If a shell session predates that change (was
started before the user logged out/in), group membership won't have taken
effect yet — check with `id` (look for `dialout` in `groups=`), and if it's
missing, prefix hardware-touching commands with `sg dialout -c "..."` as a
workaround for that session only.
