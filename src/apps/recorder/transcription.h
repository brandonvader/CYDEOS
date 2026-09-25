#pragma once

// Transcription upload - manual, per-recording upload from the Recorder
// app's Recordings screen to a self-hosted transcription service.
// Scriberr is the only backend implemented; per CYDEOS Spec.md this
// config is deliberately scoped inside the Recorder app (not OS-level
// Settings) so adding a second backend later is a change contained
// entirely to this module.

void loadTranscriptionSettings(); // call once at boot
bool transcriptionIsConfigured();  // host + API key both set

// Settings screen (host/port/API key, each tap-to-edit via the shared
// on-screen keyboard) + its own field-entry sub-screen, managed
// internally - the caller only ever sees "the transcription settings
// screen" as one opaque state, same pattern as core/wifi.h's WiFi
// settings screen. onExit is called when the top-level settings screen's
// back button is tapped.
typedef void (*ExitTranscriptionSettingsFn)();
void transcriptionSettingsInit(ExitTranscriptionSettingsFn onExit);
void enterTranscriptionSettings();
void transcriptionHandleTouch(int x, int y);

// Upload trigger + status, used by the Recordings detail screen.
enum UploadStatus { UPLOAD_NONE,
                     UPLOAD_IN_PROGRESS,
                     UPLOAD_SUCCESS,
                     UPLOAD_FAILED };
extern UploadStatus lastUploadStatus;
extern char lastUploadMessage[64];

// Fully blocking - see CLAUDE.md: a deliberate v1 simplification, safe
// specifically because it's only reachable while recState == REC_IDLE, so
// no I2S/DMA timing is at stake. Redraws the Recordings detail screen
// itself at each status transition (drawRecordingsDetailScreen(), in
// recordings_screen.h) - it's the only screen this is ever called from.
void uploadRecording(const char *filename);
