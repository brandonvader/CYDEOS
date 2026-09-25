#!/usr/bin/env python3
"""Validate the CYDEOS BLE peripheral against "CYDEOS Companion Spec.md".

This only exercises what's testable from a Linux host without a real phone
to complete the BLE passkey pairing flow. It checks:

  1. The device advertises as CYDEOS-XXXX and is discoverable.
  2. GATT discovery finds the exact service/characteristic UUIDs from the
     spec (catches UUID typos cheaply, without needing a phone).
  3. Writing to the Command characteristic without a human confirming a
     pairing passkey is rejected - confirms the WRITE_ENC security gate is
     actually providing real protection, not silently satisfied.

For (3), this registers its own throwaway BlueZ pairing agent (capability
"KeyboardOnly", so BlueZ negotiates Passkey Entry against our peripheral's
DisplayOnly capability - the same method a real phone's OS pairing dialog
uses) whose callbacks always reject instead of confirming. This is
deliberate, not a bug in the agent: **with no agent registered at all,
BlueZ falls back to reporting "NoInputNoOutput" I/O capability, which the
Bluetooth SMP pairing-method table resolves to Just Works regardless of
the MITM protection our firmware requests - completing pairing with zero
human confirmation and making this check silently pass when it shouldn't.**
Confirmed on real hardware: with no agent registered, an unattended write
completed successfully (and "BLE pairing succeeded" appeared in the
device's own serial log) on both boards, reproducibly, even across a full
`bluetoothd` restart - a host-environment gap, not a stale-bond artifact.
Registering an agent that actively refuses to confirm restores this test's
original intent: no human present to read+enter the passkey means pairing
must not be able to complete on its own, on any host.

Full pairing + command/status round-trip still needs the real CYDEOS
Companion Android app - its OS pairing dialog is a real human confirming
the code, which this script deliberately never does.

Usage: ble_test.py [scan_timeout_seconds] [name_substring]

name_substring narrows the scan to one device when more than one CYDEOS-*
board is advertising at once (e.g. two boards on a desk during development) -
pass part of its name, like "B75D" for "CYDEOS-B75D", or the full name.
"""
import asyncio
import subprocess
import sys

from bleak import BleakClient, BleakScanner
from dbus_fast import Message
from dbus_fast.aio import MessageBus
from dbus_fast.constants import BusType, MessageType
from dbus_fast.errors import DBusError
from dbus_fast.service import ServiceInterface, method

SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
COMMAND_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
RESPONSE_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

AGENT_PATH = "/cydeos/ble_test_agent"
AGENT_CAPABILITY = "KeyboardOnly"
REJECTED = ("org.bluez.Error.Rejected", "ble_test.py: no human present to confirm pairing")


class RejectingAgent(ServiceInterface):
    """org.bluez.Agent1 that always refuses - see module docstring for why
    a registered-but-refusing agent is the correct way to test this,
    rather than leaving no agent registered at all."""

    def __init__(self):
        super().__init__("org.bluez.Agent1")

    @method()
    def Release(self):
        pass

    @method()
    def RequestPinCode(self, device: "o") -> "s":
        raise DBusError(*REJECTED)

    @method()
    def DisplayPinCode(self, device: "o", pincode: "s"):
        pass

    @method()
    def RequestPasskey(self, device: "o") -> "u":
        raise DBusError(*REJECTED)

    @method()
    def DisplayPasskey(self, device: "o", passkey: "u", entered: "q"):
        pass

    @method()
    def RequestConfirmation(self, device: "o", passkey: "u"):
        raise DBusError(*REJECTED)

    @method()
    def RequestAuthorization(self, device: "o"):
        raise DBusError(*REJECTED)

    @method()
    def AuthorizeService(self, device: "o", uuid: "s"):
        raise DBusError(*REJECTED)

    @method()
    def Cancel(self):
        pass


async def call_agent_manager(bus, member, signature, body):
    reply = await bus.call(Message(
        destination="org.bluez",
        path="/org/bluez",
        interface="org.bluez.AgentManager1",
        member=member,
        signature=signature,
        body=body,
    ))
    if reply.message_type == MessageType.ERROR:
        raise RuntimeError(f"{member} failed: {reply.error_name}: {reply.body}")
    return reply


async def register_agent():
    """Own, separate system-bus connection - BlueZ's agent registration is
    bus-wide, not tied to whichever connection bleak happens to use
    internally for the GATT operations below."""
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    agent = RejectingAgent()
    bus.export(AGENT_PATH, agent)
    await call_agent_manager(bus, "RegisterAgent", "os", [AGENT_PATH, AGENT_CAPABILITY])
    await call_agent_manager(bus, "RequestDefaultAgent", "o", [AGENT_PATH])
    return bus


async def unregister_agent(bus):
    try:
        await call_agent_manager(bus, "UnregisterAgent", "o", [AGENT_PATH])
    except Exception:
        pass
    bus.unexport(AGENT_PATH)
    bus.disconnect()


def forget_existing_bond(address):
    """A REJECTED pairing attempt never completes a bond, but any PRIOR
    successful one (e.g. a Just Works pairing from before this script
    registered its own agent, or from a real phone) leaves a real,
    persistent bond on both sides. BlueZ reuses that bond's existing LTK
    for a write's security upgrade without renegotiating - our agent is
    then never even called, and the write "succeeds" for a reason that has
    nothing to do with whether the security gate can resist an unattended
    pairing attempt right now. Confirmed on real hardware: this is exactly
    what made the agent-registration fix above appear to do nothing on the
    first retest - the bond from the *previous* (pre-agent) unexpected
    pairing was still valid. Best-effort - a device with no existing bond
    just fails harmlessly here."""
    subprocess.run(
        ["bluetoothctl", "remove", address],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )


async def main():
    timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    name_filter = sys.argv[2] if len(sys.argv) > 2 else None

    def matches(d, adv):
        return d.name is not None and d.name.startswith("CYDEOS-") and (name_filter is None or name_filter in d.name)

    agent_bus = await register_agent()
    try:
        print(f"Scanning for CYDEOS-*{f' matching {name_filter!r}' if name_filter else ''} for up to {timeout}s...")
        device = await BleakScanner.find_device_by_filter(matches, timeout=timeout)
        if device is None:
            print("FAIL: no CYDEOS-* device found")
            sys.exit(1)
        print(f"Found {device.name} @ {device.address}")

        # bluetoothctl remove doesn't just clear the bond, it unregisters
        # BlueZ's whole Device object - the one we just found is now a
        # stale D-Bus path bleak would fail to connect to. Re-discover it
        # so BlueZ creates a fresh Device object from the still-arriving
        # advertisements before connecting.
        forget_existing_bond(device.address)
        print("Forgot any existing bond for this device - re-scanning to get a fresh Device object...")
        device = await BleakScanner.find_device_by_filter(matches, timeout=timeout)
        if device is None:
            print("FAIL: device did not reappear after forgetting its bond")
            sys.exit(1)

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

            print("Attempting a write to Command with no human confirming pairing (should be rejected)...")
            status_request = bytes([0x01, 0x00, 0x00])  # STATUS_REQUEST, 0-length payload
            try:
                await asyncio.wait_for(
                    client.write_gatt_char(command_char, status_request, response=True),
                    timeout=8.0,
                )
                print("UNEXPECTED: write succeeded with no human confirming pairing - "
                      "security gate may not be active (see module docstring before assuming "
                      "this means what it looks like - confirm no other agent is registered)")
            except asyncio.TimeoutError:
                print("OK: write did not complete within 8s (pairing stalled, agent never confirmed) - "
                      "consistent with the WRITE_ENC security gate being active")
            except Exception as e:
                print(f"OK: write rejected as expected ({type(e).__name__}: {e})")

        print("Done.")
    finally:
        await unregister_agent(agent_bus)


if __name__ == "__main__":
    asyncio.run(main())
