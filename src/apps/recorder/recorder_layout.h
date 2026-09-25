#pragma once

#include "boards/board_config.h"
#include "core/ui_widgets.h" // NOTIF_BAR_H, ROW_LIST_Y/ROW_H, TEXT_FIELD_*, BACK_BTN_*

// Recorder main screen (record/pause buttons, free-space bar, spectrum).
#define BTN_RADIUS UI_SCALE(60)
#define BTN_ROW_CENTER_Y (NOTIF_BAR_H + UI_SCALE(20) + BTN_RADIUS)
#define BTN_RECORD_CX (SCREEN_W / 4)
#define BTN_PAUSE_CX (SCREEN_W * 3 / 4)

#define FREESPACE_BAR_Y (BTN_ROW_CENTER_Y + BTN_RADIUS + UI_SCALE(40))
#define FREESPACE_BAR_H UI_SCALE(40)

#define SPECTRUM_Y (FREESPACE_BAR_Y + FREESPACE_BAR_H + UI_SCALE(20))

// Bottom corner icon buttons (Recordings folder / Recorder Settings
// hamburger) - anchored flush into the screen's own bottom-left/
// bottom-right corners (no margin), "fenced" in by drawing only the top,
// chamfer, and inner edges (see recorder_ui.cpp) - the outer side edge
// and bottom edge are the screen's own physical edges, so drawing a
// border there would be redundant. RECORDER_ICON_BTN_SIZE must stay in
// sync with core/ui_widgets.h's BOTTOM_EDGE_SWIPE_MARGIN_X, which reserves
// these same corners from starting the swipe-up-to-launcher gesture.
//
// The spectrum area is shortened to end above this row rather than having
// it float on top of the buttons, since the spectrum redraws live during
// recording and would otherwise need to carefully avoid painting over
// them on every single frame.
#define RECORDER_ICON_BTN_SIZE UI_SCALE(44)
#define RECORDER_ICON_BTN_Y (SCREEN_H - RECORDER_ICON_BTN_SIZE)
#define RECORDER_FOLDER_BTN_X 0
#define RECORDER_SETTINGS_BTN_X (SCREEN_W - RECORDER_ICON_BTN_SIZE)

#define SPECTRUM_H (RECORDER_ICON_BTN_Y - UI_SCALE(10) - SPECTRUM_Y)

// Recordings browser - detail screen (name/size/duration/date + upload button).
#define REC_DETAIL_NAME_Y (NOTIF_BAR_H + UI_SCALE(40))
#define REC_DETAIL_SIZE_Y (REC_DETAIL_NAME_Y + UI_SCALE(30))
#define REC_DETAIL_DURATION_Y (REC_DETAIL_SIZE_Y + UI_SCALE(30))
#define REC_DETAIL_DATE_Y (REC_DETAIL_DURATION_Y + UI_SCALE(30))
#define REC_DETAIL_UPLOAD_BTN_Y (REC_DETAIL_DATE_Y + UI_SCALE(40))
#define REC_DETAIL_UPLOAD_BTN_H UI_SCALE(50)
#define REC_DETAIL_STATUS_Y (REC_DETAIL_UPLOAD_BTN_Y + REC_DETAIL_UPLOAD_BTN_H + UI_SCALE(30))

// Transcription (Scriberr) settings screen - three tap-to-edit fields.
#define TRANSCRIPTION_HOST_Y (NOTIF_BAR_H + UI_SCALE(70))
#define TRANSCRIPTION_PORT_Y (TRANSCRIPTION_HOST_Y + TEXT_FIELD_H + UI_SCALE(35))
#define TRANSCRIPTION_APIKEY_Y (TRANSCRIPTION_PORT_Y + TEXT_FIELD_H + UI_SCALE(35))
