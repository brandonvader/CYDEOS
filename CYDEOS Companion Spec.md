# CYDEOS Companion — Android App Spec (v1 draft)

> **Carried over from CYD-Voice-Recorder as the v1 baseline.** Everything
> below is the recorder-specific protocol as already implemented and
> tested there. `CYDEOS Spec.md`'s "BLE Companion protocol v2" section
> drafts an extension — new message types (`0x40`-`0x45`) so the phone can
> list/launch/stop apps and see OS-level status, not just control the
> recorder — layered on the exact same framed-message transport described
> below. That v2 section has not been merged into this document yet; treat
> this file as "what's built" and the spec's v2 section as "what's next."

**Status:** The ESP32 side implements this entire spec (pairing,
STATUS/CMD_*, FILE_LIST_*, TRANSFER_SESSION_*) in CYD-Voice-Recorder's
`src/cyd/main.cpp`'s "Bluetooth LE" / "WiFi transfer session" sections —
not yet ported into this repo's `src/core/`. Pairing and the STATUS/CMD_*
control commands have been exercised against real hardware; FILE_LIST_*
and TRANSFER_SESSION_* have also since been confirmed end-to-end against
the real Companion app (see CYD-Voice-Recorder's CLAUDE.md). **Keep the
BLE and WiFi-transfer protocol handling behind a clean abstraction/
repository layer** on the app side regardless - this project has
repeatedly hit non-obvious hardware/driver surprises (see this repo's own
`CLAUDE.md`), so isolate that blast radius.

## What CYDEOS Companion does (v1)

1. Discover and pair with a nearby CYDEOS device (ESP32-based voice
   recorder) over Bluetooth LE, with randomized-passkey security.
2. Remote control: start / pause / stop recording, with live status.
3. Browse the list of recordings on the device (name, date, duration,
   size) over the same BLE connection — no WiFi needed for this.
4. Download a recording: the app requests a transfer session, the device
   spins up a temporary WiFi access point, the app joins it and downloads
   over HTTP, then the device reverts to its normal WiFi connection.

**Out of scope for v1** (explicitly backlogged, don't build yet):
- Uploading recordings to a Scriberr (or other transcription) instance
  from the app. (The ESP32 itself does direct-to-Scriberr uploads over
  its own WiFi connection as a separate, unrelated feature, now scoped
  inside the Recorder app — not the Companion app's job in v1.)
- Deleting recordings from the device.
- Managing/forgetting multiple paired devices beyond basic Android
  Bluetooth pairing UI.
- Firmware update / OTA of any kind.
- App management (list/launch/stop SD apps) — see the v2 draft in
  `CYDEOS Spec.md` instead.

## Why this architecture (context for whoever builds this)

The original idea was a totally generic Bluetooth file browser app talking
to the ESP32. That's not achievable: ESP-IDF's Bluetooth stack has no
OBEX FTP server support (confirmed — the one community attempt at
anything like it only handles push-to-device, not serving files for pull).
A custom app is necessary for file access, not just for the
control/Scriberr features.

True Wi-Fi Direct (P2P) isn't supported on this ESP32 chip either
(long-standing unresolved gap in ESP-IDF). AirDrop and Quick Share are
closed, proprietary protocols with no real interop path.

What *is* real, well-supported ESP32 infrastructure: BLE for a persistent
low-power control channel, and a temporary WiFi access point (SoftAP) for
the rare, deliberate "I want to download a file" action. Espressif's own
coexistence docs rate BLE+SoftAP running together as "supported but
performance unstable" — expected to work, possibly a bit slower during
that window, not a blocker. Running the device's normal WiFi (STA) mode
*at the same time* as BLE+SoftAP is not validated anywhere, so the device
briefly drops its home WiFi connection during a transfer session and
reconnects afterward. The user has explicitly accepted this tradeoff.

Splitting it this way means the BLE connection (control + file browsing)
can stay open continuously in the background without ever touching WiFi
or disrupting the device's regular network connection — only an actual
download triggers the brief handoff.

## Pairing / discovery flow

1. CYDEOS advertises BLE with a device name like `CYDEOS-XXXX` (last 4
   hex digits of its MAC) and the CYDEOS service UUID in the advertisement,
   so the app can filter scan results to just CYDEOS devices.
2. App scans, shows a list of nearby CYDEOS devices, user taps one.
3. App initiates Android's standard Bluetooth bonding flow. This triggers
   the ESP32's Secure Simple Pairing passkey generation: a random 6-digit
   code appears on the CYDEOS screen, and Android's native pairing dialog
   prompts the user to enter it. This is the "randomized PIN" security
   requirement — it's handled by the OS pairing dialog, not custom app UI.
4. Once bonded, the app connects to the GATT service and subscribes to
   notifications on the Response characteristic (see below).
5. Android's Bluetooth stack remembers the bond, so reconnecting to an
   already-paired device in the future doesn't require re-entering the
   passkey.

## BLE protocol

**Service**: CYDEOS Control Service — UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
(reusing the well-known Nordic UART Service UUID range as a starting
point, since it's a proven pattern with lots of ESP32-side prior art;
confirmed no collision via GATT discovery against real hardware).

Two characteristics, matching the Nordic UART Service shape:
- **Command** (`6e400002-...`) — Write, phone → device
- **Response** (`6e400003-...`) — Notify, device → phone

A simple framed message protocol runs on top of these two characteristics
(rather than adding a new characteristic per feature):

```
[1 byte: message type] [2 bytes: payload length, little-endian] [payload]
```

Messages longer than one BLE packet (MTU-dependent, request a larger MTU
at connection time, typically ~247 bytes usable on modern phones) are
split across multiple packets; the receiver buffers until it has
`payload length` bytes matching the header, using the message type as a
simple frame boundary check.

### Message types

| Code | Name | Direction | Payload |
|---|---|---|---|
| `0x01` | STATUS_REQUEST | phone→device | (empty) |
| `0x02` | STATUS_RESPONSE | device→phone | 1 byte state (0=idle,1=recording,2=paused) + 4 bytes free space (MB, uint32 LE) + 4 bytes est. hours remaining (float32 LE) |
| `0x10` | CMD_START_RECORDING | phone→device | (empty) |
| `0x11` | CMD_STOP_RECORDING | phone→device | (empty) |
| `0x12` | CMD_PAUSE_RESUME | phone→device | (empty) — toggles, matching the device's own pause button behavior |
| `0x20` | FILE_LIST_REQUEST | phone→device | (empty) |
| `0x21` | FILE_LIST_ENTRY | device→phone | 1 byte filename length + filename (UTF-8) + 4 bytes file size (bytes, uint32 LE) + 4 bytes duration (seconds, uint32 LE) + 4 bytes unix timestamp (uint32 LE) — sent once per file |
| `0x22` | FILE_LIST_END | device→phone | (empty) — marks end of list |
| `0x30` | TRANSFER_SESSION_REQUEST | phone→device | (empty) — requests a transfer session covering all files, not a specific one; the app picks which file(s) to download over HTTP once connected |
| `0x31` | TRANSFER_SESSION_RESPONSE | device→phone | 1 byte SSID length + SSID + 1 byte password length + password + 4 bytes device IP (raw bytes) + 2 bytes HTTP port (uint16 LE) |
| `0x32` | TRANSFER_SESSION_END | phone→device | (empty) — tells the device the app is done, so it can tear down the AP and reconnect to STA immediately rather than waiting for a timeout |
| `0xF0` | ERROR | device→phone | 1 byte error code + optional message string |

The device should also push an unsolicited `STATUS_RESPONSE` notification
whenever recording state actually changes (e.g. triggered locally via the
device's own touchscreen), not just in response to a request — so the
app's status display stays live without polling.

**Recommendation:** the device should also implement a session timeout on
`TRANSFER_SESSION_RESPONSE` (e.g. auto-tear-down after N minutes of no HTTP
activity) as a safety net in case `TRANSFER_SESSION_END` is never received
(app crash, connection drop, etc.) — don't rely solely on the app sending
it. (Implemented as 5 minutes in CYD-Voice-Recorder.)

See `CYDEOS Spec.md` for the drafted v2 extension (`0x40`-`0x45`:
APP_LIST_REQUEST/ENTRY/END, APP_LAUNCH, APP_STOP, DEVICE_STATUS_RESPONSE).

## WiFi transfer session (HTTP)

Once the app receives `TRANSFER_SESSION_RESPONSE` and joins the temporary
AP (see Android implementation notes below), it talks to a small HTTP
server on the device:

- `GET /files` → JSON array of `{name, size, duration, timestamp}` —
  functionally redundant with the BLE file list, but authoritative at
  transfer time in case anything changed; simplest to just always call
  this fresh rather than trust the earlier BLE snapshot.
- `GET /files/<filename>` → raw file bytes, `Content-Type: audio/wav`

Keep this minimal for v1 — no auth beyond "you're on the temporary AP,
which required a BLE-paired handshake to get the credentials for," no
delete/upload endpoints yet (reserve for future backlog items).

## Android implementation notes (things that will trip you up)

- **Bluetooth permissions**: `BLUETOOTH_SCAN` and `BLUETOOTH_CONNECT` on
  Android 12+ (API 31+); `ACCESS_FINE_LOCATION` is still required for BLE
  scanning on older Android versions. Handle both permission models.
- **Joining the temporary SoftAP programmatically is not the old
  `WifiManager.addNetwork()` flow** — that's deprecated/restricted since
  Android 10. Use `WifiNetworkSpecifier` + `ConnectivityManager
  .requestNetwork()` to connect to a specific local network without it
  becoming the phone's default internet route, and
  `ConnectivityManager.bindProcessToNetwork()` (or per-request `Network`
  binding on the HTTP client) to make sure the file-download HTTP calls
  actually go over that network rather than the phone's normal mobile/WiFi
  internet connection. This is the single most likely source of confusing
  bugs ("it connects but downloads time out") if missed.
- Request a larger BLE MTU (`requestMtu()`) right after connecting, before
  relying on the ~23-byte default — the framed protocol above assumes a
  realistic modern MTU (~247 bytes), not the ancient BLE default.
- Design the BLE command/response handling as a single serialized queue
  (one outstanding request at a time) — Android BLE GATT operations are
  notoriously unreliable if you fire multiple operations concurrently
  without waiting for each callback.

## Suggested app screens

1. **Device scan/pairing** — list of nearby CYDEOS devices, tap to pair.
2. **Control** (main/home screen once connected) — large record/pause/stop
   controls mirroring the device's own UI, live status (state, free
   space/hours remaining).
3. **Recordings** — file list (name, date, duration, size), tap to
   download, progress indicator during transfer.
4. **Device info** — paired device name, connection status, maybe an
   unpair action.
5. **Apps** (future, per v2) — list of installed apps, launch/stop,
   device-wide status distinct from the Recorder-specific one above.

## Open items

- Validate real BLE throughput for `FILE_LIST_ENTRY` with a realistic
  number of recordings (dozens+) — may need pagination if it's slow.
  Current implementation caps a single request at 200 entries
  (`BLE_MAX_FILE_LIST_ENTRIES`) and pauses 15ms between notifications as a
  first-pass throttle, unvalidated at that scale.
- Validate the BLE+SoftAP concurrent-operation behavior in practice, given
  Espressif's own "unstable performance" caveat — worth confirming it's
  merely slower rather than actually dropping the BLE link.
- The transfer-session HTTP server assumes only one client connects
  during a session (the phone that requested it) - no auth beyond "you have
  the per-session credentials," matching the spec's intent, but untested
  against anything trying to interfere (e.g. another device joining the
  same open-ish AP window before the intended client connects).
- The v2 app-management extension (`0x40`-`0x45`) drafted in
  `CYDEOS Spec.md` needs its exact byte layouts worked out and tested
  end-to-end once the native SD app system exists to actually manage.
