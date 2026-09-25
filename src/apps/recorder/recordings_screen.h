#pragma once

// Recordings browser - list + per-recording detail/upload screens, plus
// the entry point into transcription settings, all as tabs of the
// Recorder app (see CYDEOS Spec.md's "Built-in apps" for why this and
// transcription upload are folded into one app rather than separate
// launcher-level apps).
typedef void (*ExitRecordingsFn)();
void recordingsScreenInit(ExitRecordingsFn onExitToRecorderMain);
void enterRecordingsScreen(); // draws the list
void recordingsHandleTouch(int x, int y);

// Called by transcription.cpp to refresh the detail screen's status line
// at each upload-progress transition - a no-op if that screen isn't
// actually showing right now (mirrors the original's setUploadResult()
// check).
void redrawRecordingsDetailScreenIfShowing();
