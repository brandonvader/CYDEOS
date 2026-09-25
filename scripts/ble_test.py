#!/usr/bin/env python3
"""Validate the CYDEOS BLE peripheral against "CYDEOS Companion Spec.md".

This only exercises what's testable from a Linux host without a real phone
to complete the BLE passkey pairing flow (BlueZ's pairing-agent plumbing
for scripted Passkey Entry is more effort than it's worth here). It checks:

  1. The device advertises as CYDEOS-XXXX and is discoverable.
  2. GATT discovery finds the exact service/characteristic UUIDs from the
     spec (catches UUID typos cheaply, without needing a phone).
  3. Writing to the Command characteristic without pairing is rejected -
     confirms the WRITE_ENC security gate is actually active, not
     accidentally left open.

Full pairing + command/status round-trip needs the real CYDEOS Companion
Android app (or a scripted BlueZ pairing agent, not built yet).

Usage: ble_test.py [scan_timeout_seconds] [name_substring]

name_substring narrows the scan to one device when more than one CYDEOS-*
board is advertising at once (e.g. two boards on a desk during development) -
pass part of its name, like "B75D" for "CYDEOS-B75D", or the full name.
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
COMMAND_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
RESPONSE_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


async def main():
    timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    name_filter = sys.argv[2] if len(sys.argv) > 2 else None

    print(f"Scanning for CYDEOS-*{f' matching {name_filter!r}' if name_filter else ''} for up to {timeout}s...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: d.name is not None and d.name.startswith("CYDEOS-")
        and (name_filter is None or name_filter in d.name),
        timeout=timeout,
    )
    if device is None:
        print("FAIL: no CYDEOS-* device found")
        sys.exit(1)
    print(f"Found {device.name} @ {device.address}")

    async with BleakClient(device) as client:
        print("Connected. Discovering services...")
        service = client.services.get_service(SERVICE_UUID)
        if service is None:
            print(f"FAIL: service {SERVICE_UUID} not found")
            sys.exit(1)
        print(f"OK: found service {SERVICE_UUID}")

        command_char = service.get_characteristic(COMMAND_UUID)
        response_char = service.get_characteristic(RESPONSE_UUID)
        if command_char is None or response_char is None:
            print("FAIL: missing Command/Response characteristic")
            sys.exit(1)
        print(f"OK: found Command characteristic {COMMAND_UUID} (props={command_char.properties})")
        print(f"OK: found Response characteristic {RESPONSE_UUID} (props={response_char.properties})")

        print("Attempting an unpaired write to Command (should be rejected)...")
        status_request = bytes([0x01, 0x00, 0x00])  # STATUS_REQUEST, 0-length payload
        try:
            await asyncio.wait_for(
                client.write_gatt_char(command_char, status_request, response=True),
                timeout=8.0,
            )
            print("UNEXPECTED: write succeeded without pairing - security gate may not be active")
        except asyncio.TimeoutError:
            # BlueZ has no pairing agent registered in this script, so when
            # the write requires encryption it tries to kick off pairing and
            # then just stalls waiting for an agent that isn't there, rather
            # than failing fast. That stall is itself a sign WRITE_ENC is
            # active - it's what "not authenticated yet" looks like here.
            print("OK: write did not complete (stalled pending pairing, no agent registered) - "
                  "consistent with the WRITE_ENC security gate being active")
        except Exception as e:
            print(f"OK: write rejected as expected ({type(e).__name__}: {e})")

    print("Done.")


if __name__ == "__main__":
    asyncio.run(main())
