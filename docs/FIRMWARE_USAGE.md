# Firmware usage

This document describes how to control loudness and bass boost on SDR-Widget /
audiophile-widget firmware from the host.

**Host tools:** [`henryctl`](../henryctl.c) exposes bass boost and loudness
preferences (`--bassboost`, `--loudness`). [`widget-control`](../widget-control.c) is the
original SDR-Widget features utility (NVRAM features `-g`/`-s`, etc.) and does
**not** include those loudness controls.

## Dual UAC controls and last-filter-wins

The UAC2 Feature Unit exposes **two independent preference flags**:

| Control | UAC CS | Preference flag | `GET_CUR` semantics |
|---------|--------|-----------------|---------------------|
| Bass Boost | `0x09` | `loudness_bass_boost_enabled` | **Both can be 1** |
| Loudness | `0x0A` | `loudness_loudness_enabled` | **Both can be 1** |

Firmware applies **exactly one active DSP mode** (`last_filter_enabled`):

- Enabling either control makes that mode active (last write wins).
- Disabling the **active** control falls back to the other preference if still on,
  otherwise `FILTER_OFF_MODE`.
- Disabling a **non-active** preference only clears the flag.

HID statistics bytes `bass_boost_enabled` / `loudness_enabled` (wire offsets 39
and 42) report the **active mode**, not the raw preference flags — they are
mutually exclusive in telemetry. See [USB_STATISTICS.md](USB_STATISTICS.md) and
[LOUDNESS.md](LOUDNESS.md) for the full filter-mode matrix.

To change firmware state from the host, use `henryctl` (see below) or a host that
issues UAC2 Feature Unit `SET_CUR` for CS `0x09` / `0x0A` — which the in-box
Windows UAC2 driver does not expose in its UI today.

## Loudness CLI (`henryctl`)

`henryctl` is a host-side command-line utility built from
[`henryctl.c`](../henryctl.c). It talks to the device over USB using libusb.

Build it with the root `Makefile` (`make henryctl` or `make all`). On Windows
the output is `henryctl.exe`; on Linux it is `henryctl`.

Rebuilding `henryctl.exe` alone is **not** enough: the device must be running
firmware that includes the DG8SAQ config interface and vendor command `0x72`
(see [Troubleshooting](#troubleshooting-henryctl-on-windows)).

### Prerequisites

- The device must be connected and visible to libusb.
- **Flash firmware** built with `AUDIO_WIDGET_DEFAULTS` from the root `Makefile`
  (includes `-DFEATURE_CFG_INTERFACE` and the `DG8SAQ_SET_BASS_BOOST` `0x72`
  handler). Rebuilding only the host tool does not change the USB descriptors on
  the device.
- Firmware must expose the **DG8SAQ config interface** (`FEATURE_CFG_INTERFACE`):
  a zero-endpoint interface (class `0x00`) used by `henryctl` on Windows. Without
  it, `usbaudio.sys` owns the audio interfaces and libusb cannot send vendor control
  requests.
- On Linux, run with `sudo` or configure udev rules so your user can access the
  device.
- Use `-u serialId` when more than one compatible device is connected.
- **Windows:** install **WinUSB on USB interface 0 only** with Zadig so libusb can
  claim the DG8SAQ config interface. See [Windows: Zadig setup](#windows-zadig-setup-for-henryctl).

### Windows: Zadig setup for `henryctl`

On Windows, `henryctl` uses libusb to claim **USB interface 0** (the DG8SAQ config
interface: class `0x00`, zero endpoints). The in-box composite driver (`usbccgp.sys`)
does not allow libusb to claim that interface until you assign **WinUSB** to it.
**Do not** replace the driver on the whole composite device or on the audio playback
interface — that breaks playback.

Statistics HID (`usbstatistics.py`) and Windows audio (`usbaudio.sys`) are
unaffected; only interface 0 needs WinUSB.

#### One-time setup

1. Download and run [Zadig](https://zadig.akeo.ie/) (no install required).
2. Menu **Options → List All Devices** (enable).
3. In the device dropdown, find your DAC's **interface 0** entry. For Henry Audio
   USB DAC 128 Mk3 (`VID_16D0` / `PID_075F`) this is often named like:
   - `USB Composite Device` with **MI_00**, or
   - A child device with **USB ID 16D0 075F** and no audio class.

   **Select only interface 0** (`MI_00`). Verify in the USB ID / interface fields
   that this is not the audio function (`MI_01` / `MI_02`) or the top-level
   composite parent.

4. Set the target driver to **WinUSB** (right-hand dropdown).
5. Click **Replace Driver** (or **Install Driver**).
6. Unplug and replug the DAC (or reboot if Windows keeps the old binding).

#### Verify

```bash
./henryctl.exe -v --bassboost 1
```

Expected output:

```
henryctl: claimed config interface 0
henryctl: bass boost set via vendor request 0x72
Bass boost enabled
```

Confirm the HID statistics field updates:

```bash
./henryctl.exe --bassboost 0
python usbstatistics/usbstatistics.py   # bass_boost=0
./henryctl.exe --bassboost 1
python usbstatistics/usbstatistics.py   # bass_boost=1
```

#### Reverting Zadig

To restore the default Windows driver on interface 0: Device Manager → find the
`MI_00` child → **Properties → Driver → Uninstall device** (check *Delete the
driver software* if offered) → reconnect the DAC. You only need WinUSB while
using `henryctl`; audio playback does not require it.

#### If claim still fails

See [Troubleshooting](#troubleshooting-henryctl-on-windows) (USBView, interface
table, and optional [UsbDk](https://github.com/VirtualUSBDK/UsbDk)).

### Common options

| Option | Description |
|--------|-------------|
| `-a` | List connected devices and serial IDs |
| `-d` | Print default feature values |
| `-g` | Read feature values from NVRAM |
| `-m` | Read feature values from RAM |
| `-l` | List available feature names and values |
| `-s` | Write feature values to NVRAM |
| `-r` | Reboot the widget |
| `-u serialId` | Select device by USB serial string |
| `-v` | Verbose output (lists USB interfaces when config interface is missing) |
| `--bassboost 0\|1` | Disable or enable the **firmware** bass boost flag |

### Bass boost from the command line

The loudness filter can be gated by the firmware bass boost preference.

```bash
# Enable loudness contour selection (bass boost on)
sudo ./henryctl --bassboost 1

# Disable loudness contour selection (flat passthrough)
sudo ./henryctl --bassboost 0

# With a specific device
sudo ./henryctl -u 201901030VBSB --bassboost 1
```

`--bassboost` works in this order:

1. **Claim the DG8SAQ config interface** (class `0x00`, zero endpoints).
2. **Vendor request** `DG8SAQ_SET_BASS_BOOST` (`0x72`), 1 byte (`0` = off, `1` = on).
   This is the path that works on Windows.
3. If `0x72` is not supported, fall back to **UAC2 `SET_CUR`** for Feature Unit
   Bass Boost control (CS `0x09`). This may work on Linux; on Windows libusb
   usually cannot claim the audio interface, so this fallback fails with
   `Invalid parameter`.

Both successful paths call `loudness_bass_boost_set()` in
[`src/loudness.c`](../src/loudness.c), which updates `bass_boost_enabled` in
the HID statistics stream (protocol version 5). See
[USB_STATISTICS.md](USB_STATISTICS.md).

When the firmware flag is off, the DAC keeps a flat transfer function regardless
of volume. When on, loudness contour selection follows USB volume (or PCM-inferred
gain when the host does not send volume).

### Troubleshooting `henryctl` on Windows

#### Reading your `-v` output

`henryctl -v` prints the active USB configuration. For current Henry Audio /
AB-1.x firmware with `FEATURE_CFG_INTERFACE`, expect:

| Interface | Class | SubClass | Protocol | Endpoints | Role |
|-----------|-------|----------|----------|-----------|------|
| 0 | `0x00` | `0x00` | `0x00` | 0 | **DG8SAQ config** (for `henryctl` / vendor `0x72`) |
| 1 | `0x01` | `0x01` | `0x20` | 0 | UAC2 Audio Control |
| 2 | `0x01` | `0x02` | `0x20` | 0 / 2 / 2 | UAC2 Audio Streaming (alt 0, 1, 2) |
| 3 | `0x03` | `0x00` | `0x00` | 1 | Statistics HID |

If you see **interface 0** with `class=0x00` and `endpoints=0`, the firmware descriptor
is correct. If claim fails, `henryctl` reports *config interface 0 found but claim
failed* — install WinUSB via Zadig (see above).

Typical failure modes:

```
config interface 0 found but claim failed: Access denied ...
vendor bass boost failed: ... (could not claim config interface 0)
bass boost SET_CUR failed: Invalid parameter.
```

| Symptom | Likely cause |
|---------|----------------|
| No `class=0x00` interface in `-v` list | Firmware not flashed with `FEATURE_CFG_INTERFACE` |
| Interface 0 listed, **claim failed** | Windows driver owns the interface; use Zadig WinUSB on `MI_00` |
| Claim OK, vendor `0x72` fails | Firmware missing `DG8SAQ_SET_BASS_BOOST` handler — rebuild and flash |
| `SET_CUR` → `Invalid parameter` | Expected on Windows: `usbaudio.sys` holds interface 1 |

#### Debugging with USBView

Use **USBView** (from the Windows SDK: `usbview.exe`, often under
`C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\`) or any tree viewer that
shows raw descriptors.

1. **Connect the DAC** and run USBView as Administrator.
2. Expand the host controller → root hub → find **Henry Audio USB DAC 128 Mk 3**
   (or `VID_16D0` / `PID_075F`).
3. Select the **composite parent** device (not only a child `MI_xx` node).

**Check the device descriptor**

- `idVendor` = `16D0`, `idProduct` = `075F`
- `bcdUSB` = `0x0200` (High-Speed)
- Device is a **USB Composite Device** (`usbccgp.sys`)

**Check configuration 1**

- `bNumInterfaces` should be **4** (config + audio control + audio streaming + stats HID).
- Compare the interface list with `henryctl -v` — they must match.

**Interface 0 (DG8SAQ config)** — look for:

```
bInterfaceNumber:       0
bInterfaceClass:        0x00  (per interface specific)
bInterfaceSubClass:     0x00
bInterfaceProtocol:     0x00
bNumEndpoints:          0
```

This is the interface `henryctl` must **claim** before vendor requests work on Windows.

**Interface 1 (Audio Control)**

```
bInterfaceClass:        0x01  (Audio)
bInterfaceSubClass:     0x01  (Audio Control)
bInterfaceProtocol:     0x20  (UAC2)
```

Inside the Audio Control class-specific descriptors, find the **Feature Unit**
(`bUnitID` = `0x14`). Its `bmaControls` master channel should include **Bass Boost**
(bits 20–21 set = read/write). That advertises the control to hosts; Windows'
in-box driver still does not send `SET_CUR` for it (see below).

**Interface 3 (Statistics HID)**

```
bInterfaceClass:        0x03  (HID)
bNumEndpoints:          1  (interrupt IN, typically `0x86`)
```

Used by `usbstatistics.py`; independent of `henryctl`.

**Correlate with Device Manager**

Under **Universal Serial Bus devices** (or **Sound, video and game controllers**):

| USBView interface | Typical Device Manager child |
|-------------------|------------------------------|
| 0 (config) | `USB Composite Device` child, often `MI_00` |
| 1–2 (audio) | `Henry Audio USB DAC 128 Mk 3` under Sound, or `MI_01` |
| 3 (stats HID) | HID-compliant device, often `MI_03` |

Open **Properties → Driver** on each child. Note which driver owns interface 0:

- `usbccgp.sys` — composite parent; libusb claim on interface 0 often fails with
  *Access denied*.
- `WINUSB` / `WinUSB` — libusb can claim (after Zadig, see above).
- `usbaudio.sys` — audio only; must **not** be replaced on the whole composite.

**Fix: WinUSB on interface 0 only (Zadig)** — step-by-step instructions are in
[Windows: Zadig setup](#windows-zadig-setup-for-henryctl). Short version: Zadig →
**List All Devices** → select **MI_00** / interface 0 only → **WinUSB** →
reconnect → run `henryctl -v --bassboost 1`.

**Optional:** [UsbDk](https://github.com/VirtualUSBDK/UsbDk) can allow libusb access
without replacing the audio driver; support depends on your libusb build.

**Confirm success**

```bash
./henryctl.exe -v --bassboost 0
python usbstatistics/usbstatistics.py   # bass_boost=0
./henryctl.exe -v --bassboost 1
python usbstatistics/usbstatistics.py   # bass_boost=1
```

**Sanity check:** change the Windows **volume** slider while running
`usbstatistics.py`. If `gain_dbfs_*` updates but `bass_boost` never changes when
you toggle Speaker Properties, that confirms volume `SET_CUR` reaches the device
but Windows Enhancements do not drive the firmware bass boost flag.

#### Quick checklist

| Step | Action |
|------|--------|
| 1 | Build **firmware** from this repo (`make`), flash, reconnect. |
| 2 | `henryctl -v` shows interface 0 `class=0x00 endpoints=0`. |
| 3 | USBView: `bNumInterfaces` = 4, Feature Unit `0x14` has Bass Boost in `bmaControls`. |
| 4 | [Zadig WinUSB on interface 0 only](#windows-zadig-setup-for-henryctl). |
| 5 | If vendor fails after claim: flash firmware with `DG8SAQ_SET_BASS_BOOST` (`0x72`). |
| 6 | Confirm with `usbstatistics.py` → `bass_boost=0` / `1`. |

## Using Windows sound controls

Windows can show a **Bass Boost** option under Speaker Properties, but that UI
controls **host-side** processing, not the firmware loudness gate.

### What Windows actually does

Microsoft's in-box USB Audio 2.0 class driver documents support for Feature Unit
**Mute**, **Volume**, and **Automatic Gain** `SET_CUR` requests. It does **not**
document Bass Boost (CS `0x09`) `SET_CUR` to the device, even when the firmware
descriptor advertises that control.

The [enable-bass-boost](https://github.com/Falcosc/enable-bass-boost) PowerShell
script makes the Enhancements tab appear by writing **registry keys** that attach
Windows Audio Processing Objects (`PreMixEffectClsid`, `PostMixEffectClsid`,
`UserInterfaceClsid`). Bass boost is applied in the **Windows audio engine** (EQ /
dynamics on the host). No USB control transfer is sent to the DAC.

After running that script (if you use it):

1. Open **Settings → System → Sound** (or classic **Speaker Properties**).
2. Select the playback device.
3. Open **Enhancements** and toggle **Bass Boost**.
4. Click **Apply**.

That changes Windows' mix of the stream. It does **not** call
`loudness_bass_boost_set()` on the widget. Expect `bass_boost_enabled` in HID
stats to stay at its default (`1`) unless you use `henryctl --bassboost` or
another tool that hits vendor `0x72` or UAC2 Bass Boost `SET_CUR`.

### Prerequisite: enable Bass Boost in Windows 11 UI

Microsoft's generic USB Audio 2.0 driver does **not** ship with a Bass Boost
enhancement tab for arbitrary devices. To expose the tab, some users run the
[enable-bass-boost](https://github.com/Falcosc/enable-bass-boost) script. That
is optional and independent of the firmware loudness contour gate.

### Relationship to firmware loudness

| Control | Affects firmware loudness filter? | Visible in `bass_boost_enabled`? |
|---------|-----------------------------------|----------------------------------|
| `henryctl --bassboost` | Yes | Yes |
| UAC2 Bass Boost `SET_CUR` (CS `0x09`) from a capable host | Yes | Yes |
| Windows Enhancements → Bass Boost (incl. Falcosc registry patch) | No (host DSP only) | No |

Firmware loudness contour boost at low listening levels requires the **firmware
flag** to be on **and** volume turned down. Windows Enhancements bass boost is a
separate EQ effect in the host pipeline.

See [LOUDNESS.md](LOUDNESS.md) for filter behaviour and coefficient tables.

## USB statistics

For runtime telemetry (gain, SPL estimates, equalizer step, FIFO health, and
`bass_boost_enabled`), use the HID statistics stream documented in
[USB_STATISTICS.md](USB_STATISTICS.md).
