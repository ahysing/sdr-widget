# Installation

## Introduction

This document describes how to compile the SDR-Widget / audiophile-widget firmware and install the resulting `widget.elf` binary onto the device.

The firmware targets the Atmel AT32UC3A3256 microcontroller on the SDR-Widget board. You need the AVR32 toolchain installed and the `AVR32BIN` environment variable pointing to the directory that contains `avr32-gcc`.

On Windows, the top-level `Makefile` also builds host-side USB utilities:
`widget-control.exe` (SDR-Widget features API) and `henryctl.exe` (loudness bass
boost). Use MSVC + vcpkg, or MSYS2/UCRT64 + pacman libusb — see
[Building host tools](#building-host-tools) below. PC unit tests can be run with
`make test`.

## Building host tools

### `widget-control`

`widget-control` is the original SDR-Widget host utility for the DG8SAQ features
API (NVRAM `-g`/`-s`, device list `-a`, reboot `-r`, etc.). It does **not**
include `--bassboost`.

```bash
make widget-control
```

The output is `widget-control.exe` on Windows.

### `henryctl`

`henryctl` controls the firmware loudness bass boost flag (`--bassboost 0|1`).
See [FIRMWARE_USAGE.md](FIRMWARE_USAGE.md) for behaviour, Zadig setup, and
troubleshooting.

```bash
make henryctl
```

The output is `henryctl.exe` on Windows.

Both tools default to `AUDIO_WIDGET_DEFAULTS` — the same feature defines as
`make audio-widget` (Henry Audio USB DAC 128 / Mk II / Mk III, QNKTC AB-1.x).
To match a different firmware profile, override `WIDGET_DEFAULTS` (see root
`Makefile` for `AUDIO_WIDGET_DEFAULTS` and `SDR_WIDGET_DEFAULTS`).

### MSYS2 (UCRT64) — recommended on Windows

Install dependencies once:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make mingw-w64-ucrt-x86_64-libusb
```

From the repo root in the **UCRT64** shell:

```bash
make henryctl widget-control
./henryctl.exe -v --bassboost 1
```

Use `make henryctl` (not a bare `cl`/`gcc` line). The Makefile picks MinGW `gcc`
and libusb from the UCRT64 prefix automatically.

On Windows, `henryctl` needs a one-time **Zadig** step (WinUSB on USB interface 0
only) so libusb can claim the DG8SAQ config interface. Audio playback and
`usbstatistics.py` are unchanged. See
[FIRMWARE_USAGE.md — Zadig setup](FIRMWARE_USAGE.md#windows-zadig-setup-for-henryctl).

### MSVC (PowerShell / Developer Command Prompt)

1. Install [libusb via vcpkg](https://vcpkg.io/):

   ```powershell
   vcpkg install libusb:x64-windows
   ```

2. Load the Visual Studio environment and build:

   ```powershell
   .\vcvars64.ps1
   make henryctl widget-control VCPKG_DIR=C:\path\to\vcpkg
   ```

   Default `VCPKG_DIR` is `C:/Users/AHysing/code/vcpkg` if unset.

If you see `Cannot open include file: 'libusb-1.0/libusb.h'`, libusb is not installed in `VCPKG_DIR/installed/x64-windows`, or `VCPKG_DIR` points at the wrong tree.

### Linux

```bash
sudo apt install libusb-1.0-0-dev   # Debian/Ubuntu
make henryctl widget-control
```

## Compilation

The root `Makefile` drives firmware builds. The AVR32 link step is performed in the `Release/` subdirectory and produces `Release/widget.elf`.

### Prerequisites

- AVR32 GCC toolchain (`avr32-gcc`, `avr32-objcopy`, etc.)
- `AVR32BIN` environment variable set to the toolchain `bin` directory
- On Windows: Visual Studio with `vcvars64.bat` (for `henryctl`, `widget-control`, and unit tests)

### Build targets

| Command | Output | Description |
|---------|--------|-------------|
| `make audio-widget` | `Release/widget.elf` + `widget-control.exe` + `henryctl.exe` | Audiophile-widget firmware (DIB board) |
| `make sdr-widget` | `Release/widget.elf` + `widget-control.exe` + `henryctl.exe` | SDR-Widget firmware (default board) |
| `make all` | `Release/widget.elf` + `widget-control.exe` + `henryctl.exe` | Default build (SDR-Widget board) |
| `make clean` | — | Remove build artifacts |

Both `audio-widget` and `sdr-widget` invoke the same underlying steps:

```
make -C Release all CFLAGS="<board flag> <optimization and loudness flags>"
make henryctl widget-control.exe
```

The Release subdirectory compiles all source files listed in `Release/src/subdir.mk` (including `src/loudness.c` and `src/usb_statistics.c`) and links them:

```
avr32-gcc -nostartfiles -Wl,--gc-sections -Wl,-e,_trampoline -mpart=uc3a3256 \
  -Wl,--gc-sections --rodata-writable -Wl,--direct-data \
  -o widget.elf $(OBJS) $(LIBS)
```

### Loudness filter options

| Variable | Default | Effect |
|----------|---------|--------|
| `LOUDNESS_DISABLE` | `0` | Set to `1` to compile out the loudness filter entirely |

Examples:

```bash
# Default audiophile-widget build (FAST loudness, USB statistics enabled)
make audio-widget

# Build without loudness filter
make audio-widget LOUDNESS_DISABLE=1
```

### USB statistics

USB statistics (`src/usb_statistics.c`) collects audio buffer overruns, FIFO depth, deadline misses, and tagged events. Metrics are exposed on a **stats-only HID interface** (vendor usage page `0xFF00`, interface 2, endpoint `0x86`). Windows loads the standard HID driver — **no Zadig required**.

Host tool:

```powershell
cd usbstatistics
python -m venv venv
.\venv\Scripts\activate
pip install -r requirements.txt
python .\usbstatistics.py --list
python .\usbstatistics.py
```

Build options:

| Flag | Effect |
|------|--------|
| default | USB statistics HID + loudness filter events (when loudness enabled) |
| `LOUDNESS_DISABLE=1` | No loudness filter; buffer stats and sample-rate change events still reported |
| `USBSTATISTICS_DISABLE=1` | Omits the entire statistics HID interface and task |

```bash
make audio-widget
make audio-widget LOUDNESS_DISABLE=1
make audio-widget USBSTATISTICS_DISABLE=1
```

### Unit tests (host-side)

PC unit tests verify loudness logic, 14-step equalizer selection, and USB statistics without the AVR32 toolchain:

```bash
make test
```

Some loudness tests are built with `-DUSBSTATISTICS_DISABLE`; the equalizer-step-switch test (`loudness_equalizer_step_switch_stats_tests.exe`) requires USB statistics events enabled.

`loudness_tests.exe` also covers per-channel biquad state, highres stride-2/4 paths, idle bypass, and exact fixed-point biquad samples. See [LOUDNESS.md](LOUDNESS.md) for architecture.

## Installing to Device

After a successful build, the firmware binary is located at `Release/widget.elf`.

For step-by-step instructions on flashing the firmware to the widget hardware, refer to the section **"Installing new firmware - Windows"** in the project readme:

[Installing new firmware - Windows](https://github.com/borgestrand/sdr-widget/blob/6916f821ee46ba323eb184496960b592501a7974/AW_readme.txt#L509)
