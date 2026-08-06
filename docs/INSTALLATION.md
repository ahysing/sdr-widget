# Installation

## Introduction

This document describes how to compile the SDR-Widget / audiophile-widget firmware and install the resulting `widget.elf` binary onto the device.

The firmware targets the Atmel AT32UC3A3256 microcontroller on the SDR-Widget board. You need the AVR32 toolchain installed and the `AVR32BIN` environment variable pointing to the directory that contains `avr32-gcc`.

On Windows, the top-level `Makefile` also builds `widget-control.exe` (a host-side USB control utility) using MSVC. PC unit tests can be run with `make test`.

## Compilation

The root `Makefile` drives firmware builds. The AVR32 link step is performed in the `Release/` subdirectory and produces `Release/widget.elf`.

### Prerequisites

- AVR32 GCC toolchain (`avr32-gcc`, `avr32-objcopy`, etc.)
- `AVR32BIN` environment variable set to the toolchain `bin` directory
- On Windows: Visual Studio with `vcvars64.bat` (for `widget-control` and unit tests)

### Build targets

| Command | Output | Description |
|---------|--------|-------------|
| `make audio-widget` | `Release/widget.elf` + `widget-control.exe` | Audiophile-widget firmware (DIB board) |
| `make sdr-widget` | `Release/widget.elf` + `widget-control.exe` | SDR-Widget firmware (default board) |
| `make all` | `Release/widget.elf` + `widget-control.exe` | Default build (SDR-Widget board) |
| `make clean` | — | Remove build artifacts |

Both `audio-widget` and `sdr-widget` invoke the same underlying steps:

```
make -C Release all CFLAGS="<board flag> <optimization and loudness flags>"
make widget-control.exe
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
| `LOUDNESS_TYPE` | `FAST` | `FAST` → 32-bit biquad path; `PRECISE` → 64-bit biquad path |
| `LOUDNESS_DISABLE` | `0` | Set to `1` to compile out the loudness filter entirely |

Examples:

```bash
# Default audiophile-widget build (FAST loudness, USB statistics enabled)
make audio-widget

# PRECISE loudness biquad path
make audio-widget LOUDNESS_TYPE=PRECISE

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

PC unit tests verify loudness logic, coefficient ramping, and USB statistics without the AVR32 toolchain:

```bash
make test
```

Some loudness tests are built with `-DUSBSTATISTICS_DISABLE`; the equalizer-step-switch test (`loudness_equalizer_step_switch_stats_tests.exe`) requires USB statistics events enabled.

## Installing to Device

After a successful build, the firmware binary is located at `Release/widget.elf`.

For step-by-step instructions on flashing the firmware to the widget hardware, refer to the section **"Installing new firmware - Windows"** in the project readme:

[Installing new firmware - Windows](https://github.com/borgestrand/sdr-widget/blob/6916f821ee46ba323eb184496960b592501a7974/AW_readme.txt#L509)
