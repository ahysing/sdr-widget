# Plan: Build Henry Audio USB DAC 128 Mk3 (HA128 / AT32UC3A3128)

The **root `Makefile` remains configured for DA 256 / SPRX** (`FEATURE_PRODUCT_HA256`, `HW_GEN_SPRX`). Mk3 (USB DAC 128) builds use **`scripts/build-ha128.sh`** only — no edits to `Makefile`, `makefile.defs`, or `Release/makefile` for product selection.

## Prerequisites

| Requirement | Notes |
|-------------|--------|
| MSYS2 **UCRT64** | `export MSYSTEM=UCRT64` |
| `make` | e.g. `pacman -S make` |
| Atmel AVR32 toolchain | `/c/Program Files (x86)/Atmel/AVR Tools/AVR Toolchain/bin` |
| `make-widget` | Include `/c/...` toolchain path if needed (MSYS2) |

## Build Mk3 firmware

From repository root:

```bash
bash scripts/build-ha128.sh
```

From a parent directory (e.g. `borgestrand/`):

```bash
bash sdr-widget/scripts/build-ha128.sh
```

**Output:** `Release/widget.elf` linked for **uc3a3128**.

## What the script does

1. Sets `PATH` / `AVR32BIN` for UCRT64.
2. Reads `AUDIO_WIDGET_DEFAULTS` from the root `Makefile` (keeps `BUILD_DATE`, `BUILD_COMMIT`, etc.).
3. Appends Mk3 overrides: `-UFEATURE_PRODUCT_HA256 -UFEATURE_SPDIF_CMD -UHW_GEN_SPRX` and `-DFEATURE_PRODUCT_AB1x -DHW_GEN_AB1X`.
4. **`make -C Release clean`** — required so old SPRX object files are not linked (undefined `wm8804_*` / `pcm5142_*` otherwise).
5. **`make audio-widget`** with `PARTNAME=-mpart=uc3a3128` and the overridden `AUDIO_WIDGET_DEFAULTS`.
6. **Re-link** via `make -n all | sed 's/-mpart=uc3a3256/-mpart=uc3a3128/' | sh` because `Release/makefile` still hard-codes **256** on the link line.

Verify:

```bash
avr32-readelf -A Release/widget.elf | grep -i uc3
```

## Default `make audio-widget` (not Mk3)

Plain `make audio-widget` still builds the **Makefile default** (HA256 / SPRX, compile `-mpart=uc3a3128` from `PARTNAME`, link **uc3a3256** in `Release/makefile`). Use that for DA 256 work, not for Mk3 hardware.

## Flashing Mk3

- **`prog128.bat`** for **AT32UC3A3128**
- Do not use `prog256.bat` on 128 KB parts

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Undefined `wm8804_*`, `pcm5142_*`, `spdif_rx_status` | Run **`build-ha128.sh`** (includes `Release` clean), not a partial rebuild |
| Wrong product after manual `make` | Use the script or replicate its `AUDIO_WIDGET_DEFAULTS` + clean + relink steps |

## Related

- `AW_readme.txt` — Flip / firmware update
- `docs/FIRMWARE_USAGE.md` — Mk3 USB and `henryctl`
