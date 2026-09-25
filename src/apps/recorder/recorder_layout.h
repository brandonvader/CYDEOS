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
#define SPECTRUM_H (SCREEN_H - SPECTRUM_Y - UI_SCALE(20))

// Recordings browser - detail screen (name/size/duration/date + upload button).
#define REC_DETAIL_NAME_Y (NOTIF_BAR_H + UI_SCALE(40))
#define REC_DETAIL_SIZE_Y (REC_DETAIL_NAME_Y + UI_SCALE(30))
#define REC_DETAIL_DURATION_Y (REC_DETAIL_SIZE_Y + UI_SCALE(30))
#define REC_DETAIL_DATE_Y (REC_DETAIL_DURATION_Y + UI_SCALE(30))
#define REC_DETAIL_UPLOAD_BTN_Y (REC_DETAIL_DATE_Y + UI_SCALE(40))
#define REC_DETAIL_UPLOAD_BTN_H UI_SCALE(50)
#define REC_DETAIL_STATUS_Y (REC_DETAIL_UPLOAD_BTN_Y + REC_DETAIL_UPLOAD_BTN_H + UI_SCALE(30))

// Recordings list screen - small top-right link into transcription
// settings, same row as the screen's own back button (top-left).
#define RECORDINGS_SETTINGS_LINK_Y (NOTIF_BAR_H + UI_SCALE(6))
#define RECORDINGS_SETTINGS_LINK_W UI_SCALE(140)
#define RECORDINGS_SETTINGS_LINK_H UI_SCALE(26)

// Recorder main screen - small top-left link into the Recordings browser
// (there's no back button competing for that spot on this screen).
#define RECORDER_RECORDINGS_LINK_X UI_SCALE(8)
#define RECORDER_RECORDINGS_LINK_Y (NOTIF_BAR_H + UI_SCALE(6))
#define RECORDER_RECORDINGS_LINK_W UI_SCALE(110)
#define RECORDER_RECORDINGS_LINK_H UI_SCALE(26)

// Transcription (Scriberr) settings screen - three tap-to-edit fields.
#define TRANSCRIPTION_HOST_Y (NOTIF_BAR_H + UI_SCALE(70))
#define TRANSCRIPTION_PORT_Y (TRANSCRIPTION_HOST_Y + TEXT_FIELD_H + UI_SCALE(35))
#define TRANSCRIPTION_APIKEY_Y (TRANSCRIPTION_PORT_Y + TEXT_FIELD_H + UI_SCALE(35))
