#include "core/wifi.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstring>

#include "core/display.h"
#include "core/identity.h"
#include "core/clock.h"
#include "core/keyboard.h"
#include "core/ui_widgets.h"

#define SETTINGS_TOGGLE_Y (NOTIF_BAR_H + UI_SCALE(30))
#define SETTINGS_TOGGLE_H UI_SCALE(50)
#define SETTINGS_SCAN_BTN_Y (SETTINGS_TOGGLE_Y + SETTINGS_TOGGLE_H + UI_SCALE(30))
#define SETTINGS_SCAN_BTN_H UI_SCALE(50)
#define SETTINGS_SAVED_BTN_Y (SETTINGS_SCAN_BTN_Y + SETTINGS_SCAN_BTN_H + UI_SCALE(20))
#define SETTINGS_SAVED_BTN_H UI_SCALE(50)
#define SETTINGS_STATUS_Y (SETTINGS_SAVED_BTN_Y + SETTINGS_SAVED_BTN_H + UI_SCALE(30))

#define NET_DETAIL_STATUS_Y (NOTIF_BAR_H + UI_SCALE(50))
#define NET_DETAIL_BTN_H UI_SCALE(50)
#define NET_DETAIL_DISCONNECT_Y (NET_DETAIL_STATUS_Y + UI_SCALE(30))
#define NET_DETAIL_AUTO_Y (NET_DETAIL_DISCONNECT_Y + NET_DETAIL_BTN_H + UI_SCALE(20))
#define NET_DETAIL_FORGET_Y (NET_DETAIL_AUTO_Y + NET_DETAIL_BTN_H + UI_SCALE(20))

#define MAX_SAVED_NETWORKS 5

static Preferences wifiPrefs;
static ExitToSettingsHomeFn onExitToHome = nullptr;

enum WifiScreen { WIFI_MAIN,
                   WIFI_SCAN_RESULTS,
                   WIFI_PASSWORD_ENTRY,
                   WIFI_CONNECTING,
                   WIFI_SAVED_NETWORKS,
                   WIFI_NETWORK_DETAIL };
static WifiScreen wifiScreen = WIFI_MAIN;

static bool wifiEnabled = false;
static int scanResultCount = 0; // -1 while a scan is in progress

static char selectedSSID[33] = "";
static bool selectedIsOpen = false;
static char passwordInput[65] = "";
static bool autoConnectChecked = true;
static bool showPasswordChecked = false;
static int passwordRevealIndex = -1;
static uint32_t connectAttemptStartMs = 0;
static wl_status_t lastKnownWifiStatus = (wl_status_t)-1; // sentinel - forces a redraw on the first check

// A password only gets persisted to NVS once a connection actually
// succeeds (see checkWifiConnectResult()) - never on the attempt itself.
// See CLAUDE.md's "WiFi settings findings".
static bool pendingSaveCredential = false;
static bool pendingAutoConnect = false;

// Background auto-connect (triggered from handleMainTouch, not
// attemptConnect()) doesn't go through WIFI_CONNECTING/
// checkWifiConnectResult() at all, so it needs its own timeout tracking.
static bool autoConnectPending = false;
static char autoConnectSSID[33] = "";
static uint32_t autoConnectStartMs = 0;

struct SavedNetwork {
  char ssid[33];
  char password[65];
  bool autoConnect;
};
static SavedNetwork savedNetworks[MAX_SAVED_NETWORKS];
static int savedNetworkCount = 0;

// ---- Saved network persistence (NVS) ----
static void loadSavedNetworks() {
  // Read-write (not read-only) so the namespace auto-creates on a fresh
  // device instead of logging a scary-looking (but harmless) NOT_FOUND
  // error on first boot.
  wifiPrefs.begin("wifi", false);
  savedNetworkCount = wifiPrefs.getInt("count", 0);
  if (savedNetworkCount > MAX_SAVED_NETWORKS) savedNetworkCount = MAX_SAVED_NETWORKS;
  if (savedNetworkCount < 0) savedNetworkCount = 0;
  for (int i = 0; i < savedNetworkCount; i++) {
    char keySSID[8], keyPass[8], keyAuto[8];
    snprintf(keySSID, sizeof(keySSID), "ssid%d", i);
    snprintf(keyPass, sizeof(keyPass), "pass%d", i);
    snprintf(keyAuto, sizeof(keyAuto), "auto%d", i);
    wifiPrefs.getString(keySSID, savedNetworks[i].ssid, sizeof(savedNetworks[i].ssid));
    wifiPrefs.getString(keyPass, savedNetworks[i].password, sizeof(savedNetworks[i].password));
    savedNetworks[i].autoConnect = wifiPrefs.getBool(keyAuto, false);
  }
  wifiPrefs.end();
  Serial.printf("Loaded %d saved WiFi network(s)\n", savedNetworkCount);
}

static void saveNetworksToNVS() {
  wifiPrefs.begin("wifi", false);
  wifiPrefs.putInt("count", savedNetworkCount);
  for (int i = 0; i < savedNetworkCount; i++) {
    char keySSID[8], keyPass[8], keyAuto[8];
    snprintf(keySSID, sizeof(keySSID), "ssid%d", i);
    snprintf(keyPass, sizeof(keyPass), "pass%d", i);
    snprintf(keyAuto, sizeof(keyAuto), "auto%d", i);
    wifiPrefs.putString(keySSID, savedNetworks[i].ssid);
    wifiPrefs.putString(keyPass, savedNetworks[i].password);
    wifiPrefs.putBool(keyAuto, savedNetworks[i].autoConnect);
  }
  wifiPrefs.end();
}

static int findSavedNetwork(const char *ssid) {
  for (int i = 0; i < savedNetworkCount; i++) {
    if (strcmp(savedNetworks[i].ssid, ssid) == 0) return i;
  }
  return -1;
}

static void saveNetwork(const char *ssid, const char *password, bool autoConnect) {
  int idx = findSavedNetwork(ssid);
  if (idx < 0) {
    idx = (savedNetworkCount < MAX_SAVED_NETWORKS) ? savedNetworkCount++ : 0; // evict oldest slot once full
  }
  strncpy(savedNetworks[idx].ssid, ssid, sizeof(savedNetworks[idx].ssid) - 1);
  savedNetworks[idx].ssid[sizeof(savedNetworks[idx].ssid) - 1] = 0;
  strncpy(savedNetworks[idx].password, password, sizeof(savedNetworks[idx].password) - 1);
  savedNetworks[idx].password[sizeof(savedNetworks[idx].password) - 1] = 0;
  savedNetworks[idx].autoConnect = autoConnect;
  saveNetworksToNVS();
}

static void forgetSavedNetwork(const char *ssid) {
  int idx = findSavedNetwork(ssid);
  if (idx < 0) return;
  for (int i = idx; i < savedNetworkCount - 1; i++) {
    savedNetworks[i] = savedNetworks[i + 1];
  }
  savedNetworkCount--;
  saveNetworksToNVS();
}

// Forward declarations - these screens reference each other in a cycle.
static void drawMain();
static void drawPasswordScreen();
static void drawSavedNetworksScreen();
static void drawNetworkDetailScreen();

// ---- Main WiFi screen ----
static void drawMain() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawBackButton();
  drawCenteredLine("WiFi Settings", NOTIF_BAR_H + 8, COLOR_AQUA);

  tft.fillRoundRect(40, SETTINGS_TOGGLE_Y, SCREEN_W - 80, SETTINGS_TOGGLE_H, 10,
                     wifiEnabled ? lerp565(COLOR_BG, COLOR_AQUA, 0.25f) : lerp565(COLOR_BG, COLOR_GREY_DIM, 0.5f));
  tft.drawRoundRect(40, SETTINGS_TOGGLE_Y, SCREEN_W - 80, SETTINGS_TOGGLE_H, 10, wifiEnabled ? COLOR_AQUA : COLOR_GREY_DIM);
  char toggleLabel[16];
  snprintf(toggleLabel, sizeof(toggleLabel), "WiFi: %s", wifiEnabled ? "ON" : "OFF");
  drawTextIn(toggleLabel, 40, SCREEN_W - 80, SETTINGS_TOGGLE_Y + SETTINGS_TOGGLE_H / 2 - 8, COLOR_WHITE);

  uint16_t scanColor = wifiEnabled ? COLOR_AQUA : COLOR_GREY_DIM;
  tft.fillRoundRect(40, SETTINGS_SCAN_BTN_Y, SCREEN_W - 80, SETTINGS_SCAN_BTN_H, 10, lerp565(COLOR_BG, scanColor, 0.15f));
  tft.drawRoundRect(40, SETTINGS_SCAN_BTN_Y, SCREEN_W - 80, SETTINGS_SCAN_BTN_H, 10, scanColor);
  drawTextIn("Scan for Networks", 40, SCREEN_W - 80, SETTINGS_SCAN_BTN_Y + SETTINGS_SCAN_BTN_H / 2 - 8,
             wifiEnabled ? COLOR_WHITE : COLOR_GREY_DIM);

  // Always tappable regardless of wifiEnabled - viewing/forgetting saved
  // networks or changing auto-connect doesn't need the radio on.
  tft.fillRoundRect(40, SETTINGS_SAVED_BTN_Y, SCREEN_W - 80, SETTINGS_SAVED_BTN_H, 10, lerp565(COLOR_BG, COLOR_AQUA, 0.15f));
  tft.drawRoundRect(40, SETTINGS_SAVED_BTN_Y, SCREEN_W - 80, SETTINGS_SAVED_BTN_H, 10, COLOR_AQUA);
  drawTextIn("Saved Networks", 40, SCREEN_W - 80, SETTINGS_SAVED_BTN_Y + SETTINGS_SAVED_BTN_H / 2 - 8, COLOR_WHITE);

  if (WiFi.status() == WL_CONNECTED) {
    drawCenteredLine("Connected:", SETTINGS_STATUS_Y, COLOR_AQUA);
    drawCenteredLine(WiFi.SSID().c_str(), SETTINGS_STATUS_Y + 26, COLOR_AQUA);
    drawCenteredLine("IP Address:", SETTINGS_STATUS_Y + 60, COLOR_TEXT);
    drawCenteredLine(WiFi.localIP().toString().c_str(), SETTINGS_STATUS_Y + 86, COLOR_TEXT);
  } else if (wifiEnabled) {
    drawCenteredLine("Not connected", SETTINGS_STATUS_Y, COLOR_TEXT);
  }

  // Keep in sync with whatever was just drawn, so checkWifiStatusChanged()
  // doesn't immediately redraw again on the next tick just because it
  // hadn't been told about this draw.
  lastKnownWifiStatus = WiFi.status();
}

static void handleMainTouch(int x, int y) {
  if (handleBackButtonTouch(x, y)) {
    if (onExitToHome) onExitToHome();
    return;
  }
  if (y >= SETTINGS_TOGGLE_Y && y <= SETTINGS_TOGGLE_Y + SETTINGS_TOGGLE_H) {
    wifiEnabled = !wifiEnabled;
    if (wifiEnabled) {
      WiFi.mode(WIFI_STA);
      WiFi.setHostname(cydeosDeviceName); // must come after mode(WIFI_STA), before begin()
      for (int i = 0; i < savedNetworkCount; i++) {
        if (savedNetworks[i].autoConnect) {
          Serial.printf("Auto-connecting to %s\n", savedNetworks[i].ssid);
          WiFi.begin(savedNetworks[i].ssid, savedNetworks[i].password);
          autoConnectPending = true;
          strncpy(autoConnectSSID, savedNetworks[i].ssid, sizeof(autoConnectSSID) - 1);
          autoConnectSSID[sizeof(autoConnectSSID) - 1] = 0;
          autoConnectStartMs = millis();
          break;
        }
      }
    } else {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      autoConnectPending = false;
    }
    drawMain();
    return;
  }
  if (wifiEnabled && y >= SETTINGS_SCAN_BTN_Y && y <= SETTINGS_SCAN_BTN_Y + SETTINGS_SCAN_BTN_H) {
    WiFi.scanNetworks(true); // async - polled in wifiTick() via checkWifiScanComplete()
    scanResultCount = -1;
    wifiScreen = WIFI_SCAN_RESULTS;
    Serial.println("WiFi scan started");
    return;
  }
  if (y >= SETTINGS_SAVED_BTN_Y && y <= SETTINGS_SAVED_BTN_Y + SETTINGS_SAVED_BTN_H) {
    wifiScreen = WIFI_SAVED_NETWORKS;
    drawSavedNetworksScreen();
  }
}

// ---- Saved networks list + per-network detail ----
static void drawSavedNetworksScreen() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawBackButton();
  drawCenteredLine("Saved Networks", NOTIF_BAR_H + 8, COLOR_AQUA);

  if (savedNetworkCount == 0) {
    drawCenteredLine("No saved networks", ROW_LIST_Y + 20, COLOR_TEXT);
    return;
  }

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  for (int i = 0; i < savedNetworkCount; i++) {
    bool isCurrent = wifiConnected && strcmp(WiFi.SSID().c_str(), savedNetworks[i].ssid) == 0;
    char label[19];
    strncpy(label, savedNetworks[i].ssid, sizeof(label) - 1);
    label[sizeof(label) - 1] = 0;
    drawListRow(i, label, isCurrent, isCurrent);
    if (savedNetworks[i].autoConnect) {
      int y = ROW_LIST_Y + i * ROW_H;
      tft.setTextColor(COLOR_AQUA_DIM, isCurrent ? lerp565(COLOR_BG, COLOR_AQUA, 0.15f) : COLOR_BG);
      tft.setCursor(SCREEN_W - 90, y + 10);
      tft.print("AUTO");
    }
  }
}

static void handleSavedNetworksTouch(int x, int y) {
  if (handleBackButtonTouch(x, y)) {
    wifiScreen = WIFI_MAIN;
    drawMain();
    return;
  }
  if (savedNetworkCount == 0) return;
  int idx = (y - ROW_LIST_Y) / ROW_H;
  if (idx < 0 || idx >= savedNetworkCount) return;

  strncpy(selectedSSID, savedNetworks[idx].ssid, sizeof(selectedSSID) - 1);
  selectedSSID[sizeof(selectedSSID) - 1] = 0;
  wifiScreen = WIFI_NETWORK_DETAIL;
  drawNetworkDetailScreen();
}

static void drawNetworkDetailScreen() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawBackButton();

  int idx = findSavedNetwork(selectedSSID);
  if (idx < 0) { // forgotten already (e.g. auto-forgotten while this screen wasn't open) - nothing to show
    wifiScreen = WIFI_SAVED_NETWORKS;
    drawSavedNetworksScreen();
    return;
  }

  char label[19];
  strncpy(label, selectedSSID, sizeof(label) - 1);
  label[sizeof(label) - 1] = 0;
  drawCenteredLine(label, NOTIF_BAR_H + 8, COLOR_AQUA);

  bool isConnected = (WiFi.status() == WL_CONNECTED) && strcmp(WiFi.SSID().c_str(), selectedSSID) == 0;
  drawCenteredLine(isConnected ? "Currently connected" : "Not connected", NET_DETAIL_STATUS_Y,
                    isConnected ? COLOR_AQUA : COLOR_TEXT);

  uint16_t discColor = isConnected ? COLOR_RED : COLOR_GREY_DIM;
  tft.fillRoundRect(40, NET_DETAIL_DISCONNECT_Y, SCREEN_W - 80, NET_DETAIL_BTN_H, 10, lerp565(COLOR_BG, discColor, 0.15f));
  tft.drawRoundRect(40, NET_DETAIL_DISCONNECT_Y, SCREEN_W - 80, NET_DETAIL_BTN_H, 10, discColor);
  drawTextIn("Disconnect", 40, SCREEN_W - 80, NET_DETAIL_DISCONNECT_Y + NET_DETAIL_BTN_H / 2 - 8,
             isConnected ? COLOR_WHITE : COLOR_GREY_DIM);

  bool autoOn = savedNetworks[idx].autoConnect;
  tft.fillRoundRect(40, NET_DETAIL_AUTO_Y, SCREEN_W - 80, NET_DETAIL_BTN_H, 10, lerp565(COLOR_BG, COLOR_AQUA, autoOn ? 0.25f : 0.1f));
  tft.drawRoundRect(40, NET_DETAIL_AUTO_Y, SCREEN_W - 80, NET_DETAIL_BTN_H, 10, COLOR_AQUA);
  drawTextIn(autoOn ? "Auto Connect: ON" : "Auto Connect: OFF", 40, SCREEN_W - 80,
             NET_DETAIL_AUTO_Y + NET_DETAIL_BTN_H / 2 - 8, COLOR_WHITE);

  tft.fillRoundRect(40, NET_DETAIL_FORGET_Y, SCREEN_W - 80, NET_DETAIL_BTN_H, 10, lerp565(COLOR_BG, COLOR_RED, 0.15f));
  tft.drawRoundRect(40, NET_DETAIL_FORGET_Y, SCREEN_W - 80, NET_DETAIL_BTN_H, 10, COLOR_RED);
  drawTextIn("Forget This Network", 40, SCREEN_W - 80, NET_DETAIL_FORGET_Y + NET_DETAIL_BTN_H / 2 - 8, COLOR_WHITE);
}

static void handleNetworkDetailTouch(int x, int y) {
  if (handleBackButtonTouch(x, y)) {
    wifiScreen = WIFI_SAVED_NETWORKS;
    drawSavedNetworksScreen();
    return;
  }

  int idx = findSavedNetwork(selectedSSID);
  if (idx < 0) {
    wifiScreen = WIFI_SAVED_NETWORKS;
    drawSavedNetworksScreen();
    return;
  }

  if (y >= NET_DETAIL_DISCONNECT_Y && y <= NET_DETAIL_DISCONNECT_Y + NET_DETAIL_BTN_H) {
    bool isConnected = (WiFi.status() == WL_CONNECTED) && strcmp(WiFi.SSID().c_str(), selectedSSID) == 0;
    if (isConnected) {
      WiFi.disconnect();
      autoConnectPending = false; // this was a deliberate disconnect, not a failure to clean up after
      drawNetworkDetailScreen();
    }
    return;
  }
  if (y >= NET_DETAIL_AUTO_Y && y <= NET_DETAIL_AUTO_Y + NET_DETAIL_BTN_H) {
    bool newAuto = !savedNetworks[idx].autoConnect;
    // Only one network ever actually auto-connects (handleMainTouch's loop
    // stops at the first match) - enforce that here too rather than
    // letting the UI imply multiple networks would.
    if (newAuto) {
      for (int i = 0; i < savedNetworkCount; i++) savedNetworks[i].autoConnect = false;
    }
    savedNetworks[idx].autoConnect = newAuto;
    saveNetworksToNVS();
    drawNetworkDetailScreen();
    return;
  }
  if (y >= NET_DETAIL_FORGET_Y && y <= NET_DETAIL_FORGET_Y + NET_DETAIL_BTN_H) {
    forgetSavedNetwork(selectedSSID);
    wifiScreen = WIFI_SAVED_NETWORKS;
    drawSavedNetworksScreen();
  }
}

// ---- Scan results panel ----
static void drawScanResultsPanel() {
  tft.fillRoundRect(SCAN_PANEL_X, SCAN_PANEL_Y, SCAN_PANEL_W, SCAN_PANEL_H, 12, COLOR_BG);
  tft.drawRoundRect(SCAN_PANEL_X, SCAN_PANEL_Y, SCAN_PANEL_W, SCAN_PANEL_H, 12, COLOR_AQUA_DIM);

  if (scanResultCount == -1) {
    drawCenteredLine("Scanning...", SCAN_PANEL_Y + SCAN_PANEL_H / 2 - 8, COLOR_TEXT);
    return;
  }
  if (scanResultCount == 0) {
    drawCenteredLine("No networks found", SCAN_PANEL_Y + SCAN_PANEL_H / 2 - 8, COLOR_TEXT);
    return;
  }
  for (int i = 0; i < scanResultCount; i++) {
    int rowY = SCAN_PANEL_Y + 8 + i * SCAN_ROW_H;
    if (i > 0) tft.drawFastHLine(SCAN_PANEL_X + 8, rowY - 4, SCAN_PANEL_W - 16, COLOR_GREY_DIM);

    char label[19];
    strncpy(label, WiFi.SSID(i).c_str(), 18);
    label[18] = 0;
    tft.setTextColor(COLOR_WHITE, COLOR_BG);
    tft.setTextSize(UI_TEXT_SIZE_NORMAL);
    tft.setCursor(SCAN_PANEL_X + 16, rowY + 8);
    tft.print(label);

    drawSignalIcon(SCAN_PANEL_X + SCAN_PANEL_W - 54, rowY + 12, WiFi.RSSI(i));
    if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
      drawLockIcon(SCAN_PANEL_X + SCAN_PANEL_W - 28, rowY + 12);
    }
  }
}

static void attemptConnect(const char *ssid, const char *password, bool saveCredential, bool autoConnect) {
  WiFi.begin(ssid, password);
  connectAttemptStartMs = millis();
  wifiScreen = WIFI_CONNECTING;
  // Saved only once success is confirmed - see checkWifiConnectResult().
  pendingSaveCredential = saveCredential;
  pendingAutoConnect = autoConnect;
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);
  drawCenteredLine("Connecting...", SCREEN_H / 2 - 8, COLOR_AQUA);
  Serial.printf("Connecting to %s\n", ssid);
}

static void onPasswordKeyboardRedraw() { drawPasswordScreen(); }

static void onPasswordKeyboardDone() {
  if (strlen(passwordInput) > 0) {
    attemptConnect(selectedSSID, passwordInput, true, autoConnectChecked);
  }
}

static void handleScanResultsTouch(int x, int y) {
  // scanResultCount == 0 (confirmed empty) backs out on any tap, same as a
  // geometric tap outside the panel - but == -1 (scan still in progress)
  // must NOT, or every impatient tap on the "Scanning..." panel itself
  // cancels navigation back to it before the scan ever gets to complete.
  // See CLAUDE.md.
  if (x < SCAN_PANEL_X || x > SCAN_PANEL_X + SCAN_PANEL_W || scanResultCount == 0) {
    wifiScreen = WIFI_MAIN;
    drawMain();
    return;
  }
  int idx = (y - (SCAN_PANEL_Y + 8)) / SCAN_ROW_H;
  if (idx < 0 || idx >= scanResultCount) return;

  strncpy(selectedSSID, WiFi.SSID(idx).c_str(), sizeof(selectedSSID) - 1);
  selectedSSID[sizeof(selectedSSID) - 1] = 0;
  selectedIsOpen = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);

  int savedIdx = findSavedNetwork(selectedSSID);
  if (savedIdx >= 0) {
    attemptConnect(selectedSSID, savedNetworks[savedIdx].password, false, savedNetworks[savedIdx].autoConnect);
  } else if (selectedIsOpen) {
    attemptConnect(selectedSSID, "", false, false);
  } else {
    passwordInput[0] = 0;
    autoConnectChecked = true;
    showPasswordChecked = false;
    passwordRevealIndex = -1;
    openKeyboardFor(passwordInput, sizeof(passwordInput), &passwordRevealIndex,
                     onPasswordKeyboardRedraw, onPasswordKeyboardDone);
    wifiScreen = WIFI_PASSWORD_ENTRY;
    drawPasswordScreen();
  }
}

static void checkWifiScanComplete() {
  if (wifiScreen != WIFI_SCAN_RESULTS || scanResultCount != -1) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scanResultCount = (n == WIFI_SCAN_FAILED) ? 0 : (n > MAX_SCAN_RESULTS ? MAX_SCAN_RESULTS : n);
  Serial.printf("WiFi scan complete: %d network(s)\n", scanResultCount);
  drawScanResultsPanel();
}

static void checkWifiConnectResult() {
  if (wifiScreen != WIFI_CONNECTING) return;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected, IP=%s\n", WiFi.localIP().toString().c_str());
    if (pendingSaveCredential) {
      saveNetwork(WiFi.SSID().c_str(), WiFi.psk().c_str(), pendingAutoConnect);
      pendingSaveCredential = false;
    }
    syncClockFromNTP();
    wifiScreen = WIFI_MAIN;
    drawMain();
  } else if (millis() - connectAttemptStartMs > 15000) {
    Serial.println("WiFi connect timed out");
    // pendingSaveCredential is only false here for a reconnect to an
    // already-saved network - drop the stale password rather than let it
    // keep silently failing forever on every future WiFi-enable.
    if (!pendingSaveCredential) {
      int idx = findSavedNetwork(selectedSSID);
      if (idx >= 0) {
        forgetSavedNetwork(selectedSSID);
        Serial.printf("Forgot stale saved network: %s\n", selectedSSID);
      }
    }
    pendingSaveCredential = false;
    wifiScreen = WIFI_MAIN;
    drawMain();
    drawCenteredLine("Connection failed", SETTINGS_STATUS_Y + 30, COLOR_RED);
  }
}

// Auto-connect (triggered from handleMainTouch when WiFi is switched on)
// doesn't go through WIFI_CONNECTING/checkWifiConnectResult() at all - this
// catches the status change live while sitting on the main screen instead.
static void checkWifiStatusChanged() {
  if (wifiScreen != WIFI_MAIN) return;
  wl_status_t status = WiFi.status();
  if (status != lastKnownWifiStatus) {
    if (status == WL_CONNECTED && lastKnownWifiStatus != WL_CONNECTED) {
      syncClockFromNTP(); // covers auto-connect, which skips checkWifiConnectResult() entirely
      autoConnectPending = false;
    }
    drawMain(); // also updates lastKnownWifiStatus
  }

  // Background auto-connect never goes through checkWifiConnectResult(), so
  // give it the same "forget on failure" handling here instead.
  if (autoConnectPending && status != WL_CONNECTED && millis() - autoConnectStartMs > 15000) {
    forgetSavedNetwork(autoConnectSSID);
    Serial.printf("Auto-connect to %s timed out - forgot stale saved network\n", autoConnectSSID);
    autoConnectPending = false;
    drawMain();
  }
}

// ---- Password entry screen ----
static void drawPasswordScreen() {
  tft.fillRect(0, NOTIF_BAR_H, SCREEN_W, SCREEN_H - NOTIF_BAR_H, COLOR_BG);

  char ssidLabel[40];
  snprintf(ssidLabel, sizeof(ssidLabel), "Password for %s", selectedSSID);
  drawCenteredLine(ssidLabel, NOTIF_BAR_H + 16, COLOR_TEXT);

  tft.drawRoundRect(30, TEXT_FIELD_Y, SCREEN_W - 60, TEXT_FIELD_H, 6, COLOR_AQUA_DIM);
  tft.fillRect(32, TEXT_FIELD_Y + 2, SCREEN_W - 64, TEXT_FIELD_H - 4, COLOR_BG);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.setTextSize(UI_TEXT_SIZE_NORMAL);
  tft.setCursor(40, TEXT_FIELD_Y + 10);
  int len = strlen(passwordInput);
  for (int i = 0; i < len; i++) {
    bool reveal = showPasswordChecked || (i == passwordRevealIndex);
    tft.print(reveal ? passwordInput[i] : '*');
  }

  int pwToggleW = UI_SCALE(52);
  int pwToggleX = SCREEN_W - 34 - pwToggleW;
  tft.fillRect(pwToggleX - 4, TEXT_FIELD_Y + 2, pwToggleW + 4, TEXT_FIELD_H - 4, COLOR_BG);
  drawTextIn(showPasswordChecked ? "Hide" : "Show", pwToggleX, pwToggleW, TEXT_FIELD_Y + TEXT_FIELD_H / 2 - 8, COLOR_AQUA);

  drawCheckbox(40, PW_CHECKBOX_Y, autoConnectChecked);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(UI_TEXT_SIZE_NORMAL);
  tft.setCursor(40 + PW_CHECKBOX_SIZE + 10, PW_CHECKBOX_Y + 3);
  tft.print("Auto Connect");

  bool canConnect = len > 0;
  uint16_t btnColor = canConnect ? COLOR_AQUA : COLOR_GREY_DIM;
  tft.fillRoundRect(SCREEN_W - 140, PW_CONNECT_BTN_Y, 110, PW_CONNECT_BTN_H, 8, lerp565(COLOR_BG, btnColor, 0.2f));
  tft.drawRoundRect(SCREEN_W - 140, PW_CONNECT_BTN_Y, 110, PW_CONNECT_BTN_H, 8, btnColor);
  drawTextIn("Connect", SCREEN_W - 140, 110, PW_CONNECT_BTN_Y + PW_CONNECT_BTN_H / 2 - 8, canConnect ? COLOR_WHITE : COLOR_GREY_DIM);

  drawKeyboard();
}

static void handlePasswordScreenTouch(int x, int y) {
  int pwToggleW = UI_SCALE(52);
  int pwToggleX = SCREEN_W - 34 - pwToggleW;
  if (y >= TEXT_FIELD_Y && y <= TEXT_FIELD_Y + TEXT_FIELD_H && x >= pwToggleX - 4 && x <= pwToggleX + pwToggleW) {
    showPasswordChecked = !showPasswordChecked;
    drawPasswordScreen();
    return;
  }
  if (y >= PW_CHECKBOX_Y && y <= PW_CHECKBOX_Y + PW_CHECKBOX_SIZE && x >= 40 && x <= 40 + PW_CHECKBOX_SIZE + 150) {
    autoConnectChecked = !autoConnectChecked;
    drawPasswordScreen();
    return;
  }
  if (y >= PW_CONNECT_BTN_Y && y <= PW_CONNECT_BTN_Y + PW_CONNECT_BTN_H && x >= SCREEN_W - 140) {
    onPasswordKeyboardDone();
    return;
  }
  if (y >= KB_Y) {
    handleKeyboardTouch(x, y);
  }
}

void wifiInit(ExitToSettingsHomeFn onExit) {
  onExitToHome = onExit;
  loadSavedNetworks();
}

void wifiEnterSettings() {
  wifiScreen = WIFI_MAIN;
  drawMain();
}

void wifiHandleTouch(int x, int y) {
  switch (wifiScreen) {
    case WIFI_MAIN: handleMainTouch(x, y); break;
    case WIFI_SCAN_RESULTS: handleScanResultsTouch(x, y); break;
    case WIFI_PASSWORD_ENTRY: handlePasswordScreenTouch(x, y); break;
    case WIFI_SAVED_NETWORKS: handleSavedNetworksTouch(x, y); break;
    case WIFI_NETWORK_DETAIL: handleNetworkDetailTouch(x, y); break;
    case WIFI_CONNECTING: break; // no interaction while connecting
  }
}

void wifiTick() {
  checkWifiScanComplete();
  checkWifiConnectResult();
  checkWifiStatusChanged();
}
