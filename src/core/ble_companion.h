#pragma once

#include <Arduino.h>
#include <cstdint>

// CYDEOS Companion - persistent BLE control/status channel for the phone
// app. See "CYDEOS Companion Spec.md" for the full protocol (message
// types, byte formats, pairing flow). This module owns the transport
// (pairing, GATT, the framed message protocol, the pairing-passkey
// overlay, and the SoftAP+HTTP transfer-session mechanics) - it does NOT
// know what "recording" means. Recording control and the recordings file
// list are supplied by whichever app registers hooks via
// bleCompanionInit(); with no hooks registered (the state M0 leaves this
// in, since the Recorder app doesn't exist until M1), every command
// replies BLE_ERR_NOT_IMPLEMENTED, matching the spec's own "unsupported
// message types get a graceful ERROR reply" contract.
//
// This split is deliberate groundwork for the spec's drafted BLE
// Companion protocol v2 (app list/launch/status, not just recorder
// control) - recorder-specific commands are already "just the first app's
// hooks", not something wired directly into the transport.

#define BLE_ERR_NOT_IMPLEMENTED 0x01
#define BLE_ERR_BUSY_RECORDING 0x02
#define BLE_ERR_SD_UNAVAILABLE 0x03

// recState mirror for STATUS_RESPONSE's 1-byte state field: 0=idle,
// 1=recording, 2=paused. An app with no notion of "recording" (any future
// non-recorder app) should just always report 0.
typedef uint8_t (*BleGetRecordingStateFn)();
typedef void (*BleGetFreeSpaceFn)(uint32_t *freeMB, float *freeHoursOut);
typedef void (*BleCommandFn)(); // CMD_START/STOP/PAUSE_RESUME - hook does the state change, its own UI update, and calls bleCompanionSendStatus() itself (same pattern the recorder's own touch handlers use)

// Called once per matching file during FILE_LIST_REQUEST handling - the
// hook enumerates whatever "files" mean for it and calls
// bleCompanionSendFileListEntry() per entry. Return false if the hook
// already sent its own error (e.g. BLE_ERR_BUSY_RECORDING) instead of a
// list - the core skips sending FILE_LIST_END in that case.
typedef bool (*BleFileListFn)();

// Returning false here rejects TRANSFER_SESSION_REQUEST with
// BLE_ERR_BUSY_RECORDING before the SoftAP handoff even starts.
typedef bool (*BleCanStartTransferSessionFn)();

// Returns a JSON array string for the transfer session's GET /files - the
// same metadata FILE_LIST_ENTRY sends over BLE, authoritative at transfer
// time. Defaults to "[]" if no hook is registered.
typedef String (*BleHttpFileListJsonFn)();

struct BleCompanionHooks {
  BleGetRecordingStateFn getRecordingState = nullptr;
  BleGetFreeSpaceFn getFreeSpace = nullptr;
  BleCommandFn onStartRecording = nullptr;
  BleCommandFn onStopRecording = nullptr;
  BleCommandFn onPauseResume = nullptr;
  BleFileListFn onFileListRequest = nullptr;
  BleCanStartTransferSessionFn canStartTransferSession = nullptr;
  BleHttpFileListJsonFn httpFileListJson = nullptr;
};

void bleCompanionInit(const BleCompanionHooks &hooks);
void bleCompanionSetup(); // brings up BLE advertising - call once from setup()
void bleCompanionTick();  // call every loop() iteration

// Called by an app's FILE_LIST hook, once per matching entry.
void bleCompanionSendFileListEntry(const char *name, uint32_t size, uint32_t duration, uint32_t timestamp);
void bleCompanionSendError(uint8_t code);

// Unsolicited status push - call whenever recording state actually changes
// (see the spec: the phone's status display should stay live without
// polling), not just in response to a STATUS_REQUEST.
void bleCompanionSendStatus();

// True while the pairing-code overlay is on screen - other periodic
// redraws (e.g. a recorder app's live spectrum) should skip drawing over
// it. Mirrors the original blePairingUIActive flag.
extern bool blePairingUIActive;
