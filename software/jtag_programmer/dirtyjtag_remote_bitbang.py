#!/usr/bin/env python3
"""Expose a DirtyJTAG USB adapter as an OpenOCD remote_bitbang server."""

from __future__ import annotations

import argparse
import socket
import sys
from collections.abc import Iterable

import usb.core
import usb.util

CMD_STOP = 0x00
CMD_INFO = 0x01
CMD_FREQ = 0x02
CMD_CLK = 0x06
CMD_SETSIG = 0x04
CMD_READOUT = 0x80

SIG_TCK = 1 << 1
SIG_TDI = 1 << 2
SIG_TDO = 1 << 3
SIG_TMS = 1 << 4
SIG_TRST = 1 << 5
SIG_SRST = 1 << 6

USB_EP_OUT = 0x01
USB_EP_IN = 0x82
USB_TIMEOUT_MS = 1000
USB_COMMAND_LIMIT = 60


def signal_command(remote_state: int) -> bytes:
    """Translate remote_bitbang TCK/TMS/TDI bits into DirtyJTAG SETSIG."""
    values = 0
    if remote_state & 0x4:
        values |= SIG_TCK
    if remote_state & 0x2:
        values |= SIG_TMS
    if remote_state & 0x1:
        values |= SIG_TDI
    return bytes((CMD_SETSIG, SIG_TCK | SIG_TMS | SIG_TDI, values))


def release_reset_command() -> bytes:
    """Keep the adapter's active-low TRST and SRST lines deasserted."""
    mask = SIG_TRST | SIG_SRST
    return bytes((CMD_SETSIG, mask, mask))


def clock_command(remote_state: int) -> bytes:
    """Clock one JTAG cycle and return the sampled TDO value."""
    signals = 0
    if remote_state & 0x2:
        signals |= SIG_TMS
    if remote_state & 0x1:
        signals |= SIG_TDI
    return bytes((CMD_CLK | CMD_READOUT, signals, 1, CMD_STOP))


class DirtyJtag:
    def __init__(self, vid: int, pid: int, frequency_khz: int) -> None:
        device = usb.core.find(idVendor=vid, idProduct=pid)
        if device is None:
            raise RuntimeError(f"DirtyJTAG device {vid:04x}:{pid:04x} not found")

        self.device = device
        self.interface_number = 0
        self.pending = bytearray()
        self.closed = False
        self.remote_state = 0
        self.suppress_next_rise = False

        device.set_configuration()
        configuration = device.get_active_configuration()
        interface = configuration[(0, 0)]
        self.interface_number = interface.bInterfaceNumber
        self.out_endpoint = usb.util.find_descriptor(
            interface,
            custom_match=lambda endpoint: (
                endpoint.bEndpointAddress == USB_EP_OUT
                and usb.util.endpoint_type(endpoint.bmAttributes)
                == usb.util.ENDPOINT_TYPE_BULK
            ),
        )
        self.in_endpoint = usb.util.find_descriptor(
            interface,
            custom_match=lambda endpoint: (
                endpoint.bEndpointAddress == USB_EP_IN
                and usb.util.endpoint_type(endpoint.bmAttributes)
                == usb.util.ENDPOINT_TYPE_BULK
            ),
        )
        if self.out_endpoint is None or self.in_endpoint is None:
            usb.util.dispose_resources(device)
            raise RuntimeError("DirtyJTAG interface is missing its bulk endpoints")

        usb.util.claim_interface(device, self.interface_number)
        try:
            info = self._request(bytes((CMD_INFO, CMD_STOP)))
            if not info.startswith(b"DJTAG2\n"):
                raise RuntimeError(f"Unexpected DirtyJTAG info response: {info!r}")
            self._write(
                bytes(
                    (
                        CMD_FREQ,
                        (frequency_khz >> 8) & 0xFF,
                        frequency_khz & 0xFF,
                        CMD_STOP,
                    )
                )
            )
            self._write(release_reset_command() + bytes((CMD_STOP,)))
            self.reset_tap()
        except (OSError, usb.core.USBError, RuntimeError):
            self.close()
            raise

    def _write(self, data: bytes) -> None:
        sent = self.out_endpoint.write(data, timeout=USB_TIMEOUT_MS)
        if sent != len(data):
            raise RuntimeError(f"Short DirtyJTAG USB write: {sent}/{len(data)} bytes")

    def _request(self, commands: bytes) -> bytes:
        self._write(commands)
        return bytes(self.in_endpoint.read(64, timeout=USB_TIMEOUT_MS))

    def queue_signal(self, remote_state: int) -> None:
        previous_state = self.remote_state
        self.remote_state = remote_state
        if (
            self.suppress_next_rise
            and remote_state & 0x4
            and not previous_state & 0x4
        ):
            self.suppress_next_rise = False
            return
        self.pending.extend(signal_command(remote_state))
        if len(self.pending) >= USB_COMMAND_LIMIT:
            self.flush()

    def flush(self) -> None:
        if self.pending:
            self._write(bytes(self.pending) + bytes((CMD_STOP,)))
            self.pending.clear()

    def read_tdo(self) -> bool:
        commands = bytes(self.pending) + clock_command(self.remote_state)
        self.pending.clear()
        response = self._request(commands)
        if not response:
            raise RuntimeError("DirtyJTAG returned no TDO sample")
        self.suppress_next_rise = True
        return response[0] != 0

    def release_resets(self) -> None:
        self.flush()
        self._write(release_reset_command() + bytes((CMD_STOP,)))

    def reset_tap(self) -> None:
        self.flush()
        for _ in range(6):
            self.queue_signal(0b010)
            self.queue_signal(0b110)
        self.queue_signal(0)
        self.flush()

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        try:
            self.flush()
        finally:
            try:
                usb.util.release_interface(self.device, self.interface_number)
            finally:
                usb.util.dispose_resources(self.device)


def handle_client(connection: socket.socket, adapter: DirtyJtag) -> None:
    while True:
        data = connection.recv(4096)
        if not data:
            adapter.flush()
            return
        for command in data:
            if ord("0") <= command <= ord("7"):
                adapter.queue_signal(command - ord("0"))
            elif command == ord("R"):
                connection.sendall(b"1" if adapter.read_tdo() else b"0")
            elif ord("r") <= command <= ord("u"):
                adapter.release_resets()
            elif command in (ord("B"), ord("b")):
                continue
            elif command == ord("Q"):
                adapter.flush()
                return
            else:
                raise RuntimeError(
                    f"Unsupported remote_bitbang command: 0x{command:02x}"
                )


def serve(adapter: DirtyJtag, host: str, port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((host, port))
        server.listen(1)
        print(f"DirtyJTAG remote_bitbang listening on {host}:{port}", flush=True)
        while True:
            connection, address = server.accept()
            print(f"OpenOCD connected from {address[0]}:{address[1]}", flush=True)
            with connection:
                adapter.reset_tap()
                handle_client(connection, adapter)
            print("OpenOCD disconnected", flush=True)


def parse_args(arguments: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=0x1209)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=0xC0CA)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=3335)
    parser.add_argument("--frequency-khz", type=int, default=1000)
    args = parser.parse_args(arguments)
    if not 1 <= args.frequency_khz <= 1000:
        parser.error("--frequency-khz must be between 1 and 1000")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    return args


def main(arguments: Iterable[str] | None = None) -> int:
    args = parse_args(arguments)
    adapter: DirtyJtag | None = None
    try:
        adapter = DirtyJtag(args.vid, args.pid, args.frequency_khz)
        print(
            f"DirtyJTAG DJTAG2 ready at {args.vid:04x}:{args.pid:04x}, "
            f"{args.frequency_khz} kHz",
            flush=True,
        )
        serve(adapter, args.host, args.port)
    except (OSError, usb.core.USBError, RuntimeError) as error:
        print(f"DirtyJTAG bridge failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 0
    finally:
        if adapter is not None:
            adapter.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
