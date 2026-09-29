# Mk3 builds while bisecting (3d023f89 … 19a749d7)

## CFLAGS / `make test` crash

If you see:

```text
CFLAGS="Running test_calculate_gap_same_buffer...
unexpected EOF while looking for matching `"'
```

`scripts/build-ha128.sh` was calling **`make test`** (PC unit tests). Use the fixed script that calls **`print-audio-widget-defaults`** only.

## Carry these files across `git checkout` (uncommitted is fine)

| File | Purpose |
|------|---------|
| `scripts/build-ha128.sh` | Safe defaults echo; `LOUDNESS_DISABLE` / `USBSTATISTICS_DISABLE`; auto link-fix |
| `scripts/patch_uac2_link.py` | Adds `LOUDNESS_PROCESS_UAC2_STEREO_PACKET` for **74d50f2a**, **3915292e**, **c66d226a** |
| `makefile.targets` | **Link-only** override (`widget.elf`); do **not** use the old `src/Mobo_config.o` special rule with bare `CFLAGS` |

Optional: `Makefile` hunk for `print-audio-widget-defaults` — not required if `build-ha128.sh` uses `--eval` fallback.

## Commands

```bash
export MSYSTEM=UCRT64
./scripts/build-ha128.sh
LOUDNESS_DISABLE=1 ./scripts/build-ha128.sh   # Phase 1 discriminator
```

## Phase 1 result (recorded)

`LOUDNESS_DISABLE=1` still **no sound** on `f706680d` and `19a749d7` → continue ladder from **3d023f89** / bootstrap / **49e4045c** (not only M2–M5 loudness path).

## Hardware ladder (2026-09-29)

| Commit | Sound |
|--------|-------|
| `3d023f89` | OK |
| `74d50f2a` (M3) | OK |
| `78644772` / `b149801a` (M2), loudness on or `LOUDNESS_DISABLE=1` | **no** |

**First bad milestone: M2 (`78644772`)** — diff M3 vs M2 for playback (UAC2 loudness hook / volume-in-EQ), not M5 ramp until M2 is understood.
