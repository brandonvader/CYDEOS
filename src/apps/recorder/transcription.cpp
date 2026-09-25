#include "apps/recorder/transcription.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstring>

#include "apps/recorder/recordings_screen.h"
#include "core/display.h"
#include "core/identity.h"
#include "core/keyboard.h"
#include "core/ui_widgets.h"

#define SCRIBERR_HOST_Y (NOTIF_BAR_H + UI_SCALE(70))
#define SCRIBERR_PORT_Y (SCRIBERR_HOST_Y + TEXT_FIELD_H + UI_SCALE(35))
#define SCRIBERR_APIKEY_Y (SCRIBERR_PORT_Y + TEXT_FIELD_H + UI_SCALE(35))

static Preferences scriberrPrefs;
static char scriberrHost[48] = "";   // IP/hostname only, no port - the on-screen
                                      // keyboard's symbol rows have no ':', so
                                      // host and port are separate fields/NVS keys
                                      // rather than a combined "host:port" string
static char scriberrPort[8] = "";    // numeric string; empty means "use 80"
static char scriberrApiKey[48] = ""; // sent as the X-API-Key header - confirmed against a real instance

static ExitTranscriptionSettingsFn onExitToRecordings = nullptr;

enum TranscriptionScreen { TRANSCRIPTION_SETTINGS,
                            TRANSCRIPTION_FIELD_ENTRY };
static TranscriptionScreen screen = TRANSCRIPTION_SETTINGS;

enum ScriberrField { FIELD_HOST,
                      FIELD_PORT,
                      FIELD_APIKEY };
static ScriberrField editingField = FIELD_HOST;
static bool apiKeyRevealChecked = false;
static int apiKeyRevealIndex = -1;

UploadStatus lastUploadStatus = UPLOAD_NONE;
char lastUploadMessage[64] = "";

static uint16_t getScriberrPort() {
  if (strlen(scriberrPort) == 0) return 80;
  int port = atoi(scriberrPort);
  return (port > 0 && port <= 65535) ? (uint16_t)port : 80;
}

void loadTranscriptionSettings() {
  scriberrPrefs.begin("scriberr", true);
  scriberrPrefs.getString("host", scriberrHost, sizeof(scriberrHost));
  scriberrPrefs.getString("port", scriberrPort, sizeof(scriberrPort));
  scriberrPrefs.getString("apikey", scriberrApiKey, sizeof(scriberrApiKey));
  scriberrPrefs.end();
}

static void saveTranscriptionSettings() {
  scriberrPrefs.begin("scriberr", false);
  scriberrPrefs.putString("host", scriberrHost);
  scriberrPrefs.putString("port", scriberrPort);
  scriberrPrefs.putString("apikey", scriberrApiKey);
  scriberrPrefs.end();
}

bool transcriptionIsConfigured() {
  return strlen(scriberrHost) > 0 && strlen(scriberrApiKey) > 0;
}

// ---- Settings screen (server host:port + API key, tap either field to
// edit via the shared on-screen keyboard) ----
static void drawSettings() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawBackButton();
  drawCenteredLine("Transcription (Scriberr)", NOTIF_BAR_H + 8, COLOR_AQUA);

  drawCenteredLine("Server IP/Host (tap to edit)", SCRIBERR_HOST_Y - UI_SCALE(20), COLOR_TEXT);
  tft.drawRoundRect(30, SCRIBERR_HOST_Y, SCREEN_W - 60, TEXT_FIELD_H, 6, COLOR_AQUA_DIM);
  tft.fillRect(32, SCRIBERR_HOST_Y + 2, SCREEN_W - 64, TEXT_FIELD_H - 4, COLOR_BG);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.setTextSize(UI_TEXT_SIZE_NORMAL);
  tft.setCursor(40, SCRIBERR_HOST_Y + 10);
  tft.print(strlen(scriberrHost) > 0 ? scriberrHost : "Not set");

  drawCenteredLine("Port (tap to edit, default 80)", SCRIBERR_PORT_Y - UI_SCALE(20), COLOR_TEXT);
  tft.drawRoundRect(30, SCRIBERR_PORT_Y, SCREEN_W - 60, TEXT_FIELD_H, 6, COLOR_AQUA_DIM);
  tft.fillRect(32, SCRIBERR_PORT_Y + 2, SCREEN_W - 64, TEXT_FIELD_H - 4, COLOR_BG);
  tft.setCursor(40, SCRIBERR_PORT_Y + 10);
  tft.print(strlen(scriberrPort) > 0 ? scriberrPort : "80 (default)");

  drawCenteredLine("API Key (tap to edit)", SCRIBERR_APIKEY_Y - UI_SCALE(20), COLOR_TEXT);
  tft.drawRoundRect(30, SCRIBERR_APIKEY_Y, SCREEN_W - 60, TEXT_FIELD_H, 6, COLOR_AQUA_DIM);
  tft.fillRect(32, SCRIBERR_APIKEY_Y + 2, SCREEN_W - 64, TEXT_FIELD_H - 4, COLOR_BG);
  tft.setCursor(40, SCRIBERR_APIKEY_Y + 10);
  tft.print(strlen(scriberrApiKey) > 0 ? "Set" : "Not set");
}

static void drawFieldEntry();

// Shared by all three fields - fills in the keyboard target and switches
// to the keyboard-entry screen. Only the API key gets masking/reveal
// (host and port aren't sensitive).
static void openFieldEntry(ScriberrField field) {
  editingField = field;
  char *target;
  size_t targetSize;
  int *revealIndex = nullptr;
  if (field == FIELD_HOST) {
    target = scriberrHost;
    targetSize = sizeof(scriberrHost);
  } else if (field == FIELD_PORT) {
    target = scriberrPort;
    targetSize = sizeof(scriberrPort);
  } else {
    target = scriberrApiKey;
    targetSize = sizeof(scriberrApiKey);
    apiKeyRevealIndex = -1;
    revealIndex = &apiKeyRevealIndex;
    apiKeyRevealChecked = false;
  }
  openKeyboardFor(target, targetSize, revealIndex, drawFieldEntry, [] {
    saveTranscriptionSettings();
    screen = TRANSCRIPTION_SETTINGS;
    drawSettings();
  });
  screen = TRANSCRIPTION_FIELD_ENTRY;
  drawFieldEntry();
}

static void handleSettingsTouch(int x, int y) {
  if (handleBackButtonTouch(x, y)) {
    if (onExitToRecordings) onExitToRecordings();
    return;
  }
  if (y >= SCRIBERR_HOST_Y && y <= SCRIBERR_HOST_Y + TEXT_FIELD_H) {
    openFieldEntry(FIELD_HOST);
    return;
  }
  if (y >= SCRIBERR_PORT_Y && y <= SCRIBERR_PORT_Y + TEXT_FIELD_H) {
    openFieldEntry(FIELD_PORT);
    return;
  }
  if (y >= SCRIBERR_APIKEY_Y && y <= SCRIBERR_APIKEY_Y + TEXT_FIELD_H) {
    openFieldEntry(FIELD_APIKEY);
  }
}

// ---- Field entry (shared by all three fields - which one is being
// edited is tracked in editingField) ----
static void drawFieldEntry() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);

  bool isApiKey = (editingField == FIELD_APIKEY);
  const char *label = (editingField == FIELD_HOST)   ? "Scriberr Server IP/Host"
                       : (editingField == FIELD_PORT) ? "Scriberr Port"
                                                        : "Scriberr API Key";
  drawCenteredLine(label, NOTIF_BAR_H + 16, COLOR_TEXT);

  const char *target = (editingField == FIELD_HOST) ? scriberrHost
                        : (editingField == FIELD_PORT) ? scriberrPort
                                                         : scriberrApiKey;

  tft.drawRoundRect(30, TEXT_FIELD_Y, SCREEN_W - 60, TEXT_FIELD_H, 6, COLOR_AQUA_DIM);
  tft.fillRect(32, TEXT_FIELD_Y + 2, SCREEN_W - 64, TEXT_FIELD_H - 4, COLOR_BG);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.setTextSize(UI_TEXT_SIZE_NORMAL);
  tft.setCursor(40, TEXT_FIELD_Y + 10);
  int len = strlen(target);
  for (int i = 0; i < len; i++) {
    bool reveal = !isApiKey || apiKeyRevealChecked || i == apiKeyRevealIndex;
    tft.print(reveal ? target[i] : '*');
  }

  if (isApiKey) {
    // Same Show/Hide-toggle-in-the-field pattern as the WiFi password screen.
    int pwToggleW = UI_SCALE(52);
    int pwToggleX = SCREEN_W - 34 - pwToggleW;
    tft.fillRect(pwToggleX - 4, TEXT_FIELD_Y + 2, pwToggleW + 4, TEXT_FIELD_H - 4, COLOR_BG);
    drawTextIn(apiKeyRevealChecked ? "Hide" : "Show", pwToggleX, pwToggleW, TEXT_FIELD_Y + TEXT_FIELD_H / 2 - 8, COLOR_AQUA);
  }

  drawKeyboard();
}

static void handleFieldEntryTouch(int x, int y) {
  if (editingField == FIELD_APIKEY) {
    int pwToggleW = UI_SCALE(52);
    int pwToggleX = SCREEN_W - 34 - pwToggleW;
    if (y >= TEXT_FIELD_Y && y <= TEXT_FIELD_Y + TEXT_FIELD_H && x >= pwToggleX - 4 && x <= pwToggleX + pwToggleW) {
      apiKeyRevealChecked = !apiKeyRevealChecked;
      drawFieldEntry();
      return;
    }
  }
  if (y >= KB_Y) {
    handleKeyboardTouch(x, y);
  }
}

void transcriptionSettingsInit(ExitTranscriptionSettingsFn onExit) {
  onExitToRecordings = onExit;
}

void enterTranscriptionSettings() {
  screen = TRANSCRIPTION_SETTINGS;
  drawSettings();
}

void transcriptionHandleTouch(int x, int y) {
  if (screen == TRANSCRIPTION_SETTINGS) {
    handleSettingsTouch(x, y);
  } else {
    handleFieldEntryTouch(x, y);
  }
}

// ---------------------------------------------------------------------
// Upload - streams a WAV file straight from SD to Scriberr's
// multipart/form-data upload endpoint over a raw WiFiClient. Confirmed
// against a real Scriberr instance: POST /api/v1/transcription/submit,
// multipart field "audio", X-API-Key header - see CLAUDE.md.
// ---------------------------------------------------------------------
#define SCRIBERR_UPLOAD_BOUNDARY "----CYDBoundary7MA4YWxkTrZu0gW"
#define SCRIBERR_UPLOAD_TIMEOUT_MS 20000

static void setUploadResult(UploadStatus status, const char *message) {
  lastUploadStatus = status;
  strncpy(lastUploadMessage, message, sizeof(lastUploadMessage) - 1);
  lastUploadMessage[sizeof(lastUploadMessage) - 1] = 0;
  redrawRecordingsDetailScreenIfShowing();
}

void uploadRecording(const char *filename) {
  lastUploadStatus = UPLOAD_IN_PROGRESS;
  redrawRecordingsDetailScreenIfShowing(); // shows "Uploading..." before the blocking work below starts

  if (strlen(scriberrHost) == 0) {
    setUploadResult(UPLOAD_FAILED, "No server configured");
    return;
  }
  // Defensive - the caller already checks this before ever invoking this
  // function, but WiFiClient::connect() while WiFi was never turned on
  // crashes with an lwIP assert ("Invalid mbox"), not a clean error
  // return - confirmed on real hardware. Never let this function reach
  // that call without a live connection. See CLAUDE.md.
  if (WiFi.status() != WL_CONNECTED) {
    setUploadResult(UPLOAD_FAILED, "WiFi not connected");
    return;
  }
  const char *host = scriberrHost;
  uint16_t port = getScriberrPort();
  Serial.printf("Uploading %s to %s:%u ...\n", filename, host, port);

  char path[64];
  snprintf(path, sizeof(path), "/sdcard/%s", filename);
  FILE *f = fopen(path, "rb");
  if (!f) {
    setUploadResult(UPLOAD_FAILED, "File not found");
    return;
  }
  fseek(f, 0, SEEK_END);
  long fileSize = ftell(f);
  fseek(f, 0, SEEK_SET);

  // A separate "title" field, not just the "audio" part's filename=
  // attribute - confirmed against a real instance that Scriberr always
  // stores the upload under its own job-UUID path regardless, and only
  // uses this separate, optional field for a human-readable name.
  // Prefixed with this device's CYDEOS-XXXX name so uploads from multiple
  // CYDEOS devices are distinguishable in Scriberr's job list.
  char uploadTitle[64];
  snprintf(uploadTitle, sizeof(uploadTitle), "%s_%s", cydeosDeviceName, filename);

  char preamble[400]; // generous margin over the ~315-byte worst case
  int preambleLen = snprintf(preamble, sizeof(preamble),
                              "--" SCRIBERR_UPLOAD_BOUNDARY "\r\n"
                              "Content-Disposition: form-data; name=\"title\"\r\n\r\n"
                              "%s\r\n"
                              "--" SCRIBERR_UPLOAD_BOUNDARY "\r\n"
                              "Content-Disposition: form-data; name=\"audio\"; filename=\"%s\"\r\n"
                              "Content-Type: audio/wav\r\n\r\n",
                              uploadTitle, filename);
  const char *epilogue = "\r\n--" SCRIBERR_UPLOAD_BOUNDARY "--\r\n";
  int epilogueLen = strlen(epilogue);
  long contentLength = preambleLen + fileSize + epilogueLen;

  WiFiClient client;
  client.setTimeout(SCRIBERR_UPLOAD_TIMEOUT_MS);
  if (!client.connect(host, port)) {
    fclose(f);
    Serial.println("Scriberr upload: connection failed");
    setUploadResult(UPLOAD_FAILED, "Connection failed");
    return;
  }

  client.print("POST /api/v1/transcription/submit HTTP/1.1\r\n");
  client.printf("Host: %s:%u\r\n", host, port);
  client.printf("X-API-Key: %s\r\n", scriberrApiKey);
  client.print("Content-Type: multipart/form-data; boundary=" SCRIBERR_UPLOAD_BOUNDARY "\r\n");
  client.printf("Content-Length: %ld\r\n", contentLength);
  client.print("Connection: close\r\n\r\n");
  client.write((const uint8_t *)preamble, preambleLen);

  uint8_t buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    client.write(buf, n);
  }
  fclose(f);
  client.write((const uint8_t *)epilogue, epilogueLen);

  unsigned long waitStart = millis();
  while (!client.available() && client.connected() && millis() - waitStart < SCRIBERR_UPLOAD_TIMEOUT_MS) {
    delay(10);
  }
  String statusLine = client.readStringUntil('\n');
  statusLine.trim(); // drop the trailing \r from the HTTP line ending
  client.stop();

  Serial.printf("Scriberr upload: HTTP response: %s\n", statusLine.c_str());
  if (statusLine.indexOf("200") > 0) {
    setUploadResult(UPLOAD_SUCCESS, "Uploaded - queued for transcription");
  } else if (statusLine.length() == 0) {
    setUploadResult(UPLOAD_FAILED, "No response from server");
  } else {
    char msg[64];
    snprintf(msg, sizeof(msg), "Upload failed: %s", statusLine.c_str());
    setUploadResult(UPLOAD_FAILED, msg);
  }
}
