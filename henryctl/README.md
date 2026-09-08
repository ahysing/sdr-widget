# `henryctl` — Loudness & Bass Boost Host CLI

`henryctl` is a standalone host command-line utility for Henry Audio / SDR-Widget devices. It communicates with the firmware over USB using `libusb` to query features or toggle the hardware/firmware bass boost and loudness filter settings.

## Features

- **Static Linking:** Builds with static `libusb` on Windows (MSYS2 and PowerShell) so the resulting `henryctl.exe` can be distributed to other Windows 11 systems without installing separate DLLs.
- **Multiplatform:** Builds on MSYS2 (UCRT64 GCC), Linux (GCC), and Windows PowerShell (MSVC `cl`).

## Prerequisites

### 1. Windows (MSYS2 / UCRT64)
In the MSYS2 UCRT64 terminal:
```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make mingw-w64-ucrt-x86_64-libusb
```

### 2. Windows (PowerShell / MSVC)
In PowerShell:
```powershell
vcpkg install libusb:x64-windows-static
```
Ensure `cl.exe` (MSVC) is in PATH (e.g. by loading `vcvars64.ps1` or from the Visual Studio Developer Command Prompt).

### 3. Linux (Debian / Ubuntu / Fedora / Arch)
```bash
# Debian / Ubuntu:
sudo apt install libusb-1.0-0-dev build-essential

# Fedora:
sudo dnf install libusb1-devel gcc make

# Arch Linux:
sudo pacman -S libusb gcc make
```

## Building

### From `henryctl/` directory

#### MSYS2 (Bash):
```bash
make henryctl.exe
# or
make
```

#### PowerShell (Windows):
```powershell
.\build.ps1
# or with make:
make henryctl.exe
```

#### Linux (Bash):
```bash
make
```

### From Repository Root

```bash
make henryctl
# or
make henryctl.exe
```

## Usage

```bash
# Show help
./henryctl.exe --help

# Enable bass boost (verbose)
./henryctl.exe -v --bassboost 1

# Disable bass boost
./henryctl.exe --bassboost 0

# Enable loudness filter
./henryctl.exe --loudness 1

# Disable loudness filter
./henryctl.exe --loudness 0
```
