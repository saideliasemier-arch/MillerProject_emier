# GateMate DirtyJTAG Bring-up

## Board pins

The project schematics map the RP2040 signals as follows:

| Signal | GPIO |
| --- | ---: |
| TDI | 16 |
| TDO | 17 |
| TCK | 18 |
| TMS | 19 |
| SRST | 20 |
| TRST | 21 |
| LED | 25 |

This mapping is present in both the Olimex GateMateA1-EVB Rev C reference
schematic and the MillerV1 schematic.

## Build and flash the custom firmware

Install the Raspberry Pi Pico SDK, CMake, Ninja or Make, and the Arm GNU
Toolchain. Set `PICO_SDK_PATH` to the Pico SDK directory.

From the repository root, build with:

```powershell
cmake -S software/jtag_programmer -B software/jtag_programmer/build `
  -DPICO_SDK_PATH="$env:PICO_SDK_PATH"
cmake --build software/jtag_programmer/build
```

The UF2 is written to
`software/jtag_programmer/build/dirtyjtag_gatemate.uf2`. Hold BOOTSEL while
connecting the Olimex board, then copy that UF2 to its `RPI-RP2` drive. The
firmware enumerates as USB VID:PID `2e8a:000a`.

## USB and JTAG smoke test

Install PyUSB on the host with `py -m pip install pyusb` (Windows) or
`python3 -m pip install pyusb` (Linux). On Windows, install a WinUSB-compatible
driver for the vendor-specific interface if PyUSB cannot open the device. On
Linux, install the appropriate udev permission rule or run the test with the
required device permissions.

With the custom firmware running and the board connected, run:

```powershell
py software/jtag_programmer/smoke_test.py
```

The script checks USB enumeration, the DirtyJTAG2 info response, sends a 1 MHz
clock setting, releases the active-low resets, resets the FPGA TAP, and checks
that a TDO response arrives. This is a bridge/wiring smoke test; a TDO value of
`0x00` alone is not evidence of a working RISC-V debug module.

## OpenOCD FPGA TAP scan

The OSS CAD Suite OpenOCD build does not include a native DirtyJTAG driver.
`dirtyjtag_remote_bitbang.py` bridges the DirtyJTAG2 USB protocol to OpenOCD's
built-in `remote_bitbang` adapter. It listens only on localhost and defaults to
the original Olimex DirtyJTAG VID:PID (`1209:c0ca`). The JTAG frequency
defaults to 1 MHz; pass `--vid 0x2e8a --pid 0x000a` to use the custom RP2040
firmware built in this directory.

In one PowerShell terminal, add the OSS CAD Suite's bundled libusb DLL to the
search path and start the bridge:

```powershell
$ossCadSuite = "C:\path\to\oss-cad-suite"
$env:PATH = "$ossCadSuite\lib;$env:PATH"
py .\software\jtag_programmer\dirtyjtag_remote_bitbang.py
```

In another PowerShell terminal, add the OSS CAD Suite DLL and executable
directories to the search path and start OpenOCD:

```powershell
$ossCadSuite = "C:\path\to\oss-cad-suite"
$env:PATH = "$ossCadSuite\bin;$ossCadSuite\lib;$env:PATH"
& "$ossCadSuite\bin\openocd.exe" `
  -f .\software\jtag_programmer\dirtyjtag_remote_bitbang.cfg
```

### Repeated scan result (2026-10-22)

Using the custom firmware at VID:PID `2e8a:000a`, the OpenOCD scan was run
twice consecutively through the bridge. Both runs found the expected GateMate
IDCODE and reported an IR length of 6:

| Run | IDCODE | Expected ID | IR length |
| --- | --- | --- | ---: |
| 1 | `0x20000001` | `0x20000001` | 6 |
| 2 | `0x20000001` | `0x20000001` | 6 |

This verifies the physical GateMate configuration TAP through the host-side
`remote_bitbang` bridge. It does not verify a native OpenOCD DirtyJTAG driver
or the SoC's RISC-V debug interface.

The configuration scans the physical GateMate configuration TAP only. This
bridge is a host-side compatibility layer, not a native OpenOCD DirtyJTAG
driver. `dirtyjtag_gatemate.cfg` is a template for a native driver build; the
commonly used driver fork hard-codes VID:PID `1209:c0ca`, so it needs a source
change to support the custom RP2040 firmware's `2e8a:000a` identity.

## RISC-V debug RTL status

The SoC RTL now connects a soft JTAG TAP, the RISC-V DTM/DMI transport, and the
Debug Module to Ibex. The soft TAP uses the regular GateMate I/O pads connected
to the RP2040: TCK=`IO_WA_A5`, TDI=`IO_WA_A4`, TMS=`IO_WA_B4`, and
TDO=`IO_WA_B3`. The TAP exposes the standard 5-bit `IDCODE`, `DTMCS`, and
`DMI` instructions. The Debug Module is mapped at `0x1a110000`; its request
drives Ibex's debug request input, and it is also connected as a system-bus
host.

Yosys synthesis and nextpnr place-and-route completed for the integrated design;
the generated bitstream is `software/RIGSoC/Impl/impl.bit`. This is not yet
hardware validation. The previous OpenOCD scan result `0x20000001` identifies
the GateMate configuration TAP; it does not identify or validate the new soft
DMI TAP. The bitstream must be programmed before testing its DTM IDCODE, DMI
register access, CPU halt/resume, or GDB connection. The existing
`dirtyjtag_remote_bitbang.cfg` remains the configuration-TAP scan setup and
is not a RISC-V target configuration.