#pragma once

// "CYDEOS-XXXX", derived once from the Bluetooth MAC's last 2 bytes -
// shared by BLE advertising and the WiFi hostname so the same device shows
// up under the same identifier whether you're looking at it over Bluetooth
// or in your router's client list. Deliberately keyed off the Bluetooth MAC
// specifically (not the WiFi MAC, a different address on the same chip) so
// it exactly matches what's shown for BLE pairing - also used as an
// upload-title prefix by any app that uploads recordings, so uploads from
// multiple CYDEOS devices stay distinguishable at the destination.
extern char cydeosDeviceName[20];

// Must run before anything that reads cydeosDeviceName (WiFi hostname, BLE
// advertising name, any upload title prefix).
void computeCydeosDeviceName();
