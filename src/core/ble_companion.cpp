#include "core/ble_companion.h"

#include <NimBLEDevice.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>
#include <esp_mac.h>

#include "boards/board_config.h"
#include "core/display.h"
#include "core/identity.h"
#include "core/ui_widgets.h"

#define BLE_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_COMMAND_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_RESPONSE_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

#define BLE_MSG_STATUS_REQUEST 0x01
#define BLE_MSG_STATUS_RESPONSE 0x02
#define BLE_MSG_CMD_START_RECORDING 0x10
#define BLE_MSG_CMD_STOP_RECORDING 0x11
#define BLE_MSG_CMD_PAUSE_RESUME 0x12
#define BLE_MSG_FILE_LIST_REQUEST 0x20
#define BLE_MSG_FILE_LIST_ENTRY 0x21
#define BLE_MSG_FILE_LIST_END 0x22
#define BLE_MSG_TRANSFER_SESSION_REQUEST 0x30
#define BLE_MSG_TRANSFER_SESSION_RESPONSE 0x31
#define BLE_MSG_TRANSFER_SESSION_END 0x32
#define BLE_MSG_ERROR 0xF0

static BleCompanionHooks hooks;

static NimBLECharacteristic *bleResponseChar = nullptr;
static bool bleClientConnected = false;

static volatile uint8_t blePendingCommand = 0; // 0 = none, else a BLE_MSG_* code
static volatile bool blePasskeyPending = false;
static volatile uint32_t blePasskeyToShow = 0;
static volatile bool bleAuthResultPending = false;
static volatile bool bleAuthSucceeded = false;
bool blePairingUIActive = false;

// ---------------------------------------------------------------------
// Framed message protocol
// ---------------------------------------------------------------------
static void sendBleMessage(uint8_t msgType, const uint8_t *payload, uint16_t len) {
  if (!bleClientConnected || !bleResponseChar) return;
  uint8_t buf[3 + 64];
  if (len > sizeof(buf) - 3) len = sizeof(buf) - 3;
  buf[0] = msgType;
  buf[1] = (uint8_t)(len & 0xFF);
  buf[2] = (uint8_t)((len >> 8) & 0xFF);
  if (payload && len > 0) memcpy(buf + 3, payload, len);
  bleResponseChar->notify(buf, 3 + len);
}

void bleCompanionSendError(uint8_t code) {
  sendBleMessage(BLE_MSG_ERROR, &code, 1);
}

void bleCompanionSendStatus() {
  uint8_t state = hooks.getRecordingState ? hooks.getRecordingState() : 0;
  uint32_t freeMB = 0;
  float freeHours = 0.0f;
  if (hooks.getFreeSpace) hooks.getFreeSpace(&freeMB, &freeHours);
  uint8_t payload[9];
  payload[0] = state;
  memcpy(payload + 1, &freeMB, 4);
  memcpy(payload + 5, &freeHours, 4);
  sendBleMessage(BLE_MSG_STATUS_RESPONSE, payload, 9);
}

void bleCompanionSendFileListEntry(const char *name, uint32_t size, uint32_t duration, uint32_t timestamp) {
  uint8_t nameLen = (uint8_t)strnlen(name, 32);
  uint8_t payload[1 + 32 + 12];
  payload[0] = nameLen;
  memcpy(payload + 1, name, nameLen);
  memcpy(payload + 1 + nameLen, &size, 4);
  memcpy(payload + 1 + nameLen + 4, &duration, 4);
  memcpy(payload + 1 + nameLen + 8, &timestamp, 4);
  sendBleMessage(BLE_MSG_FILE_LIST_ENTRY, payload, 1 + nameLen + 12);
  delay(15); // lets the BLE stack flush between notifies - see CLAUDE.md open item on FILE_LIST throughput/pagination
}

static void handleFileListRequest() {
  if (!hooks.onFileListRequest) {
    bleCompanionSendError(BLE_ERR_NOT_IMPLEMENTED);
    return;
  }
  if (hooks.onFileListRequest()) {
    sendBleMessage(BLE_MSG_FILE_LIST_END, nullptr, 0);
  }
}

// ---------------------------------------------------------------------
// WiFi transfer session - the on-demand SoftAP + HTTP handoff for actual
// file downloads. Only one session at a time; starting it drops the
// device's own STA connection (if any) for the duration, per the
// architecture tradeoff in "CYDEOS Companion Spec.md" - reconnected
// automatically on teardown. The file-serving endpoints themselves are
// generic (anything under /sdcard by filename) - what "files" means to
// list is supplied by the registered hook.
// ---------------------------------------------------------------------
#define BLE_TRANSFER_SESSION_TIMEOUT_MS (5UL * 60UL * 1000UL) // safety net if TRANSFER_SESSION_END never arrives

static WebServer bleTransferServer(80);
static bool bleTransferSessionActive = false;
static uint32_t bleTransferLastActivityMs = 0;
static bool bleTransferHadSTAConnection = false;
static String bleTransferSavedSSID;
static String bleTransferSavedPassword;
static char bleTransferApSsid[24] = "";
static char bleTransferApPassword[11] = "";

static void handleHttpFileList() {
  bleTransferLastActivityMs = millis();
  String json = hooks.httpFileListJson ? hooks.httpFileListJson() : "[]";
  bleTransferServer.send(200, "application/json", json);
}

// WebServer only routes exact paths via .on(), not path parameters, so
// "/files/<filename>" is caught here via onNotFound() instead and parsed
// manually - anything else falls through to a plain 404.
static void handleHttpFileDownloadOrNotFound() {
  bleTransferLastActivityMs = millis();
  String uri = bleTransferServer.uri();
  if (!uri.startsWith("/files/")) {
    bleTransferServer.send(404, "text/plain", "Not found");
    return;
  }
  String filename = uri.substring(7);
  if (filename.length() == 0 || filename.indexOf('/') >= 0 || filename.indexOf("..") >= 0) {
    bleTransferServer.send(400, "text/plain", "Bad filename");
    return;
  }

  char path[64];
  snprintf(path, sizeof(path), "/sdcard/%s", filename.c_str());
  FILE *f = fopen(path, "rb");
  if (!f) {
    bleTransferServer.send(404, "text/plain", "Not found");
    return;
  }
  fseek(f, 0, SEEK_END);
  long fileSize = ftell(f);
  fseek(f, 0, SEEK_SET);

  bleTransferServer.setContentLength(fileSize);
  bleTransferServer.send(200, "audio/wav", "");

  uint8_t buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    bleTransferServer.sendContent((const char *)buf, n);
    bleTransferLastActivityMs = millis();
  }
  fclose(f);
}

static void sendBleTransferSessionResponse() {
  IPAddress ip = WiFi.softAPIP();
  uint8_t payload[1 + 24 + 1 + 10 + 4 + 2];
  int off = 0;
  uint8_t ssidLen = (uint8_t)strlen(bleTransferApSsid);
  payload[off++] = ssidLen;
  memcpy(payload + off, bleTransferApSsid, ssidLen);
  off += ssidLen;
  uint8_t pwLen = (uint8_t)strlen(bleTransferApPassword);
  payload[off++] = pwLen;
  memcpy(payload + off, bleTransferApPassword, pwLen);
  off += pwLen;
  payload[off++] = ip[0];
  payload[off++] = ip[1];
  payload[off++] = ip[2];
  payload[off++] = ip[3];
  uint16_t port = 80;
  payload[off++] = (uint8_t)(port & 0xFF);
  payload[off++] = (uint8_t)((port >> 8) & 0xFF);
  sendBleMessage(BLE_MSG_TRANSFER_SESSION_RESPONSE, payload, off);
}

static void handleTransferSessionRequest() {
  if (bleTransferSessionActive) {
    sendBleTransferSessionResponse(); // already running - resend current creds (e.g. a retried/lost notification)
    return;
  }
  if (hooks.canStartTransferSession && !hooks.canStartTransferSession()) {
    bleCompanionSendError(BLE_ERR_BUSY_RECORDING);
    return;
  }

  bleTransferHadSTAConnection = (WiFi.status() == WL_CONNECTED);
  if (bleTransferHadSTAConnection) {
    bleTransferSavedSSID = WiFi.SSID();
    bleTransferSavedPassword = WiFi.psk();
  }

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(bleTransferApSsid, sizeof(bleTransferApSsid), "CYDEOS-XFER-%02X%02X", mac[4], mac[5]);

  // Per-session random password (never fixed - recordings/files could be
  // sensitive).
  const char *chars = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"; // no ambiguous 0/O/1/I
  for (int i = 0; i < 10; i++) bleTransferApPassword[i] = chars[random(0, (int)strlen(chars))];
  bleTransferApPassword[10] = '\0';

  // WIFI_OFF-then-settle before the mode switch, same as
  // endTransferSession() below - see CLAUDE.md on why this alone doesn't
  // fully eliminate the driver's "netstack cb reg failed" log noise on
  // real hardware (functionally harmless).
  if (bleTransferHadSTAConnection) WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(bleTransferApSsid, bleTransferApPassword);

  bleTransferServer.on("/files", HTTP_GET, handleHttpFileList);
  bleTransferServer.onNotFound(handleHttpFileDownloadOrNotFound);
  bleTransferServer.begin();

  bleTransferSessionActive = true;
  bleTransferLastActivityMs = millis();
  Serial.printf("BLE transfer session started: AP=%s ip=%s\n", bleTransferApSsid, WiFi.softAPIP().toString().c_str());

  sendBleTransferSessionResponse();
}

static void endTransferSession() {
  if (!bleTransferSessionActive) return;
  bleTransferServer.stop();
  WiFi.softAPdisconnect(true);
  // See CLAUDE.md: this pause-in-WIFI_OFF is the standard fix for
  // "netstack cb reg failed"/"timeout when WiFi un-init" errors on this
  // transition, but confirmed NOT to fully eliminate them here (current
  // best explanation: WiFi/BLE coexistence contention) - functionally
  // harmless, kept as the correct defensive practice regardless.
  WiFi.mode(WIFI_OFF);
  delay(100);
  if (bleTransferHadSTAConnection) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(cydeosDeviceName);
    WiFi.begin(bleTransferSavedSSID.c_str(), bleTransferSavedPassword.c_str());
  }
  bleTransferSessionActive = false;
  Serial.println("BLE transfer session ended");
}

// ---------------------------------------------------------------------
// Command dispatch - runs on the main loop() task (see BleCommandCallbacks
// below for why: NimBLE callbacks run on their own FreeRTOS task and must
// not touch the display/SD directly).
// ---------------------------------------------------------------------
static void handlePendingCommand() {
  uint8_t cmd = blePendingCommand;
  blePendingCommand = 0;
  switch (cmd) {
    case BLE_MSG_STATUS_REQUEST:
      bleCompanionSendStatus();
      break;
    case BLE_MSG_CMD_START_RECORDING:
      if (hooks.onStartRecording) hooks.onStartRecording();
      else bleCompanionSendError(BLE_ERR_NOT_IMPLEMENTED);
      break;
    case BLE_MSG_CMD_STOP_RECORDING:
      if (hooks.onStopRecording) hooks.onStopRecording();
      else bleCompanionSendError(BLE_ERR_NOT_IMPLEMENTED);
      break;
    case BLE_MSG_CMD_PAUSE_RESUME:
      if (hooks.onPauseResume) hooks.onPauseResume();
      else bleCompanionSendError(BLE_ERR_NOT_IMPLEMENTED);
      break;
    case BLE_MSG_FILE_LIST_REQUEST:
      handleFileListRequest();
      break;
    case BLE_MSG_TRANSFER_SESSION_REQUEST:
      handleTransferSessionRequest();
      break;
    case BLE_MSG_TRANSFER_SESSION_END:
      endTransferSession();
      break;
    default:
      break;
  }
}

// Draws/clears the pairing-code overlay. Takes over the whole content area
// regardless of the active app, then relies on the caller (the shell) to
// notice blePairingUIActive dropped and redraw whatever app was active.
static void handlePendingPairingUI() {
  if (blePasskeyPending) {
    blePasskeyPending = false;
    blePairingUIActive = true;
    char buf[8];
    snprintf(buf, sizeof(buf), "%06lu", (unsigned long)blePasskeyToShow);
    tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
    drawCenteredLine("Bluetooth Pairing", NOTIF_BAR_H + UI_SCALE(120), COLOR_AQUA);
    drawCenteredLine("Enter this code on your phone:", NOTIF_BAR_H + UI_SCALE(160), COLOR_TEXT);
    tft.setTextColor(COLOR_WHITE, COLOR_BG);
    tft.setTextSize(UI_TEXT_SIZE_LARGE);
    int w = tft.textWidth(buf);
    tft.setCursor((SCREEN_W - w) / 2, NOTIF_BAR_H + UI_SCALE(200));
    tft.print(buf);
  }
  if (bleAuthResultPending) {
    bleAuthResultPending = false;
    blePairingUIActive = false;
    Serial.printf("BLE pairing %s\n", bleAuthSucceeded ? "succeeded" : "failed");
    // Deliberately does not redraw here - the shell's own tick notices
    // blePairingUIActive just dropped and redraws whatever app is active.
  }
}

class BleServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
    bleClientConnected = true;
    Serial.println("BLE: client connected");
  }
  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override {
    bleClientConnected = false;
    Serial.printf("BLE: client disconnected (reason %d)\n", reason);
    NimBLEDevice::startAdvertising(); // resume advertising so the app can reconnect
  }
  uint32_t onPassKeyDisplay() override {
    uint32_t passkey = random(0, 1000000);
    blePasskeyToShow = passkey;
    blePasskeyPending = true;
    return passkey;
  }
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override {
    bleAuthSucceeded = connInfo.isEncrypted();
    bleAuthResultPending = true;
  }
};

class BleCommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo) override {
    std::string value = chr->getValue();
    if (value.length() < 3) return; // malformed - shorter than even an empty-payload header
    // Only empty-payload commands are handled today; add payload
    // reassembly if/when a future message type needs one.
    blePendingCommand = (uint8_t)value[0];
  }
};

void bleCompanionInit(const BleCompanionHooks &h) {
  hooks = h;
}

void bleCompanionSetup() {
  NimBLEDevice::init(cydeosDeviceName);
  NimBLEDevice::setSecurityAuth(true, true, true); // bonding, MITM protection, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY); // we show the passkey; phone's own OS dialog is where it's entered

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new BleServerCallbacks());

  NimBLEService *service = server->createService(BLE_SERVICE_UUID);
  NimBLECharacteristic *commandChar = service->createCharacteristic(
      BLE_CHAR_COMMAND_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);
  commandChar->setCallbacks(new BleCommandCallbacks());

  bleResponseChar = service->createCharacteristic(BLE_CHAR_RESPONSE_UUID, NIMBLE_PROPERTY::NOTIFY);

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  // NimBLE-Arduino 2.x no longer advertises the device name or enables the
  // scan response by default - see CLAUDE.md. Order matters:
  // enableScanResponse() must be called before setName().
  advertising->enableScanResponse(true);
  advertising->setName(cydeosDeviceName);
  NimBLEDevice::startAdvertising();

  Serial.printf("BLE advertising as %s\n", cydeosDeviceName);
}

void bleCompanionTick() {
  if (blePendingCommand != 0) handlePendingCommand();
  if (blePasskeyPending || bleAuthResultPending) handlePendingPairingUI();

  // WebServer is synchronous - handleClient() has to be polled while a
  // transfer session is running.
  if (bleTransferSessionActive) {
    bleTransferServer.handleClient();
    if (millis() - bleTransferLastActivityMs > BLE_TRANSFER_SESSION_TIMEOUT_MS) {
      Serial.println("BLE transfer session timed out - tearing down");
      endTransferSession();
    }
  }
}
