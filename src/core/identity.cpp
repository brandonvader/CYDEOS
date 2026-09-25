#include "core/identity.h"

#include <Arduino.h>
#include <esp_mac.h>

char cydeosDeviceName[20] = "";

void computeCydeosDeviceName() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(cydeosDeviceName, sizeof(cydeosDeviceName), "CYDEOS-%02X%02X", mac[4], mac[5]);
}
