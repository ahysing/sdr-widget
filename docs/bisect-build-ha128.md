# Mk3 builds while bisecting (3d023f89 … main)

## Final result (2026-09-29)

**Root cause:** default `AUDIO_WIDGET_FEATURE_FLAGS` omitted **`-DFEATURE_ALT2_16BIT`**. Hosts often select UAC2 **ALT2 (16-bit)**; without the flag, firmware discards USB OUT (`num_samples: 0`) → silence while volume/EQ telemetry can still move.

**Fix on `main`:** `-DFEATURE_ALT2_16BIT` in root `Makefile` (see `3e3ddeb4` and following commits). Linear history after M5 (`c66d226a`): `3e3ddeb4` → USB stats / simplify / bass-boost / MSYS2 refactor (`931c1d79`).

**Mk3 front LED (AB1.x):** **green** = legacy **UAC1** (`16d0:075c`, smooth on old releases); **red** = **UAC2** (`16d0:075d`, current `feature_image_uac2_audio` default). Stutter on UAC2 was largely **per-sample loudness in the USB OUT path** — fixed by one filter pass per packet in `uac2_device_audio_task.c`.

**Build at tip:**

```bash
export MSYSTEM=UCRT64
./scripts/build-ha128.sh
```

(`build-ha128.sh` sets UCRT64-friendly `PATH`, then runs **`make audio-widget-mk3`**: clean `Release/`, compile with **`FEATURE_PRODUCT_AB1x`** / **`HW_GEN_AB1X`**, relink for `uc3a3128`. Same as `make audio-widget-mk3` from the repo root.)

**HID stats:** ~1 report/s (`configTSK_USB_DAUDIOSTATS_PERIOD_MS`). Run `python usbstatistics/usbstatistics.py --verbose --delta` while Spotify is playing; use `--debug` if packets are rejected. `if=3` / `usage_page=0xff00` is normal on UAC2 + `FEATURE_CFG_INTERFACE`.

After hardware checks, publish with `git push --force-with-lease origin main` (history was rebased; local `main` replaces the old four commits on origin).

## CFLAGS / `make test` crash

If you see:

```text
CFLAGS="Running test_calculate_gap_same_buffer...
unexpected EOF while looking for matching `"'
```

something invoked **`make test`** (PC unit tests) while **`CFLAGS`** was already set for firmware. Mk3 bisect builds must **not** run the test suite as part of the firmware recipe. Use **`./scripts/build-ha128.sh`** or **`make audio-widget-mk3`** only. To inspect Mk3 compile tokens without building: **`make print-audio-widget-mk3-defaults`**.

## Carry these across `git checkout` (uncommitted is fine)

| Item | Purpose |
|------|---------|
| `scripts/build-ha128.sh` | UCRT64 `PATH` + **`make audio-widget-mk3`** (Mk3 flags, clean + relink) |
| `makefile.targets` | **Link-only** override for `widget.elf` (`--gc-sections`, `LDFLAGS_APP_OPTIMIZATIONS`); do **not** use the old `src/Mobo_config.o` special rule with bare `CFLAGS` |
| `LOUDNESS_PROCESS_UAC2_STEREO_PACKET` in `src/loudness.h` | Required when bisecting **74d50f2a**, **3915292e**, **c66d226a** (M3–M5): macro wires UAC2 stereo OUT through loudness in one pass per packet. On current `main` it is already in the tree; when checking out older commits, copy the macro definition from tip `loudness.h` if link fails with an undefined reference |

Optional: root `Makefile` targets **`print-audio-widget-defaults`** / **`print-audio-widget-mk3-defaults`** echo the firmware `CFLAGS` prefix (Mk3 uses the latter).

## Commands

```bash
export MSYSTEM=UCRT64
./scripts/build-ha128.sh
make audio-widget-mk3 LOUDNESS_DISABLE=1   # Phase 1 discriminator (or pass via make)
```

## Phase 1 result (recorded)

`LOUDNESS_DISABLE=1` still **no sound** on `f706680d` and `19a749d7` → continue ladder from **3d023f89** / bootstrap / **49e4045c** (not only M2–M5 loudness path).

## Hardware ladder (2026-09-29)

| Commit | Sound |
|--------|-------|
| `3d023f89` | OK |
| `74d50f2a` (M3) | OK |
| `78644772` / `b149801a` (M2), loudness on or `LOUDNESS_DISABLE=1` | **no** |

**First bad milestone in the ladder: M2 (`78644772`)** — looked like UAC2/loudness; **Mk3 default build** was the real issue (missing ALT2). Re-test M2+ after `3e3ddeb4` on `main` if you want to confirm playback through the full commit chain.

See also `docs/NO_SOUND_FIX_HYPOTHESIS.md` (untracked notes).
