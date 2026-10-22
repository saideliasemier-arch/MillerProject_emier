#!/usr/bin/env python3
"""Check the GateMate DirtyJTAG USB protocol and basic TAP clocking."""

import sys

import usb.core
import usb.util

VID = 0x2E8A
PID = 0x000A
EP_OUT = 0x01
EP_IN = 0x82
TIMEOUT_MS = 1000


def main() -> int:
    device = usb.core.find(idVendor=VID, idProduct=PID)
    if device is None:
        print(f"DirtyJTAG device {VID:04x}:{PID:04x} not found")
        print("Flash dirtyjtag_gatemate.uf2 and check the USB driver/permissions.")
        return 1

    try:
        device.set_configuration()
        configuration = device.get_active_configuration()
        interface = configuration[(0, 0)]
        out_endpoint = usb.util.find_descriptor(
            interface,
            custom_match=lambda endpoint: (
                usb.util.endpoint_direction(endpoint.bEndpointAddress) == usb.util.ENDPOINT_OUT
                and usb.util.endpoint_type(endpoint.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK
            ),
        )
        in_endpoint = usb.util.find_descriptor(
            interface,
            custom_match=lambda endpoint: (
                usb.util.endpoint_direction(endpoint.bEndpointAddress) == usb.util.ENDPOINT_IN
                and usb.util.endpoint_type(endpoint.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK
            ),
        )
        if out_endpoint is None or in_endpoint is None:
            raise RuntimeError("Vendor interface does not expose bulk IN/OUT endpoints")

        usb.util.claim_interface(device, interface.bInterfaceNumber)

        out_endpoint.write(bytes((0x01, 0x00)), timeout=TIMEOUT_MS)
        info = bytes(in_endpoint.read(64, timeout=TIMEOUT_MS))
        if not info.startswith(b"DJTAG2\n"):
            raise RuntimeError(f"Unexpected CMD_INFO response: {info!r}")
        print(f"USB protocol: {info[:7].decode().strip()} OK")

        out_endpoint.write(bytes((0x02, 0x03, 0xE8, 0x00)), timeout=TIMEOUT_MS)
        out_endpoint.write(bytes((0x04, 0x76, 0x60, 0x00)), timeout=TIMEOUT_MS)
        out_endpoint.write(bytes((0x86, 0x10, 0x05, 0x00)), timeout=TIMEOUT_MS)
        out_endpoint.write(bytes((0x86, 0x00, 0x01, 0x00)), timeout=TIMEOUT_MS)
        tdo = bytes(in_endpoint.read(64, timeout=TIMEOUT_MS))
        if not tdo:
            raise RuntimeError("No TDO readout received after clocking the TAP")

        print("JTAG clock: 1 MHz requested; TAP reset/idle sequence sent")
        print(f"TDO readout: 0x{tdo[0]:02x} (electrical connectivity check only)")
        return 0
    finally:
        usb.util.release_interface(device, 0)
        usb.util.dispose_resources(device)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (usb.core.USBError, RuntimeError) as error:
        print(f"Smoke test failed: {error}", file=sys.stderr)
        raise SystemExit(1)