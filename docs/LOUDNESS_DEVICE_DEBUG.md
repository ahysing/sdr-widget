# FAST loudness device debugging

Procedures for diagnosing pitch and glitch artifacts on Henry Audio hardware after the scaled canonical DF-II FAST path. PC unit tests pass; this guide covers on-device A/B tests and telemetry.

## Build variants

| Build | Command | Purpose |
|-------|---------|---------|
| Normal | `make audio-widget` | Production contour |
| Forced unity (A/B) | `make audio-widget LOUDNESS_FORCE_UNITY_STEP=1` | Locks equalizer step 13; isolates contour vs transport |
| No loudness | `make audio-widget LOUDNESS_DISABLE=1` | Bypass filter entirely |

Flash `Release/widget.elf` with your usual AVR32 programming flow.

## Task 2 — Flash forced-unity firmware

```powershell
make audio-widget LOUDNESS_FORCE_UNITY_STEP=1
```

Confirm playback starts. Telemetry must show `equalizer_step: 13` at every volume setting.

## Task 3 — Forced-unity listen + telemetry

1. Start playback at 48 kHz (or 44.1 kHz).
2. In one terminal:

```powershell
python usbstatistics/usbstatistics.py --verbose --deltas
```

3. Set Windows volume to 100, then sweep down to 50 in several steps.
4. Record:
   - Whether pitch still rises in the ~69 slider region
   - Whether glitches coincide with `skip` / `insert` lines in the delta log

## Task 4 — Decision tree

| Observation | Next action |
|-------------|-------------|
| Pitch/glitch **gone** at forced unity | Contour or coeff commit issue — use normal FAST build after fixes in this branch |
| Pitch/glitch **remains** at step 13 | Transport / CPU / `usb_volume_format` — see transport section below |
| Glitches **only when moving** the volume slider | USB volume handler blocking audio (`taskENTER_CRITICAL` during coeff commit) |

## Task 5 — Transport correlation log

Use `--deltas` to print per-report changes:

```powershell
python usbstatistics/usbstatistics.py --deltas
```

Watch for:

- `deadline_misses` — audio task lateness (>100 ms slip threshold)
- `underruns` / `overruns` — FIFO insert/skip (`last_tag` 5 = insert, 4 = skip)
- `equalizer_step` changes — contour band crossings (~80 phon / Windows slider ~69)

Glitch audible at the same time as a skip/insert line strongly implicates transport, not the biquad.

## Task 6 — Atomic coefficient commit (implemented)

Step changes use a **double bank** for active quotients and runtime coefficients: the audio path reads one complete bank per sample while the loudness task publishes the inactive bank with a single index flip. This avoids torn reads during `memcpy` mid-sample. Highres halfrate runtime uses a separate double bank in [`src/loudness_highres.c`](../src/loudness_highres.c).

## Idle bypass (implemented)

When digital silence (`sample == 0`) is streamed with the connection open, per-channel filter steps are skipped if that channel's biquad and highres state are fully idle. Volume multiply is skipped when **both** channels are idle (`loudness_filter_is_active()` false at packet start). Mute handling is unchanged.

This reduces CPU load during long idle periods. After non-unity audio, integer biquad state may not return to exact zero (limit cycle); bypass may not engage until reset, but output stays correct. See [LOUDNESS.md](LOUDNESS.md#idle-bypass-steady-digital-silence).

PC regression: `test_filter_idle_after_zeros_*`, `test_per_channel_zero_bypass`, `test_per_channel_independent_biquad` in `make test`.

## Task 7 — Re-test normal FAST

After flashing normal FAST (without `LOUDNESS_FORCE_UNITY_STEP`):

1. Repeat the Task 3 volume sweep with `--deltas`.
2. Listen at reported steps 13, 11, and 3 (`gain_dbfs` roughly 0, −18, −34 in your earlier capture).
3. Compare to forced-unity behavior.

## Task 8 — Fixed-point biquad regression

PC regression: `test_loudness_fast_biquad_exact_samples` in `make test` locks three exact `loudness_fast_24bit()` outputs for step 0 at 48 kHz.

Compile check on AVR32 toolchain:

```powershell
make test-avr32
```

Optional on-device parity: duplicate the same three asserts in firmware boot code if `macs.d` vs software FMA drift must be caught on hardware.

## Task 9 — Step-transition correlation

When contour is enabled, note telemetry when `equalizer_step` changes (e.g. 13→11 near `gain_dbfs: -18`). Glitches aligned with those events point to transition handling; glitches with skip/insert point to transport.

Equalizer switch events appear in statistics when USB statistics events are enabled (not `USBSTATISTICS_DISABLE=1`).

## Task 10 — 1 kHz sine sanity check (optional)

Play a pure 1 kHz sine at volume 100 and ~69:

- At step 13, pitch should stay 1 kHz.
- After contour activates, timbre may change (bass/treble shelves) but **fundamental pitch** should not shift unless skip/insert occurs.

Wideband music sounding “brighter” below 80 phon is expected loudness compensation per [`LOUDNESS.md`](LOUDNESS.md).

## Track C — Transport / timing (Task 4 → transport)

Your forced-unity A/B confirms:

| Observation | Your result | Implication |
|-------------|-------------|-------------|
| Pitch at fixed volume | **Gone** with `LOUDNESS_FORCE_UNITY_STEP=1` | Contour step changes caused the pitch artifact |
| Glitches at fixed volume | **Still present** (~2500 `deadline_misses`/s, skip/insert) | Baseline transport instability — not contour |
| Glitches when moving slider | **Much worse** (+999 misses, INSERT gap 3050+) | USB volume path triggers FIFO shock |

### Track C tasks

| Task | Status | What |
|------|--------|------|
| **C2a** | Done | Defer coeff commit from USB SET_CUR to loudness RTOS task (`LOUDNESS_REQUEST_VOLUME`) |
| **C2b** | Done | Lock-free double-bank publish (no `taskENTER_CRITICAL` on coeff commit) |
| **C2c** | Done | Skip redundant commits when resolved step unchanged (forced unity) |
| **C3** | **You** | Re-flash and re-run forced-unity listen + `--deltas` |

Build and flash:

```powershell
make audio-widget LOUDNESS_FORCE_UNITY_STEP=1
```

C3 success criteria:

- `deadline_misses` delta near **0** at fixed volume and slider
- No INSERT/SKIP lines during steady playback
- Music smooth; no pitch wander when moving slider
- Then flash **normal FAST** (no force unity) and confirm pitch stays fixed while contour timbre changes below 80 phon

## Device findings (2026-03)

| Build | Steady playback | Volume sweep |
|-------|-----------------|--------------|
| FAST, 2 biquads | Glitchy; ~2500 deadline misses/s; skip/insert | Pitch + transport collapse |
| FAST, 1 biquad (`LOUDNESS_FILTERS=1`) | Smooth (stats optional) | Pitch returns with skip/insert |
| `LOUDNESS_DISABLE=1` | **Perfect** (fifo ~1440 stable) | Pitch/glitch from skip/insert only |

Conclusions:

1. **Filter CPU** — two biquad steps per sample per channel at 48 kHz exceeded AVR32 budget; **one** runtime section (`LOUDNESS_FAST_FILTERS=1`) is the pragmatic fix. L and R each hold independent `w1`/`w2` (`LOUDNESS_CHANNELS=2`). **Idle bypass** skips filter and volume multiply when both channels are fully idle on zero input (see [LOUDNESS.md](LOUDNESS.md#idle-bypass-steady-digital-silence)).
2. **Volume slider** — skip/insert and fifo swings to ~3072 happen even with loudness disabled; root cause is USB volume SET_CUR handling + feedback/skip logic, not the biquad.
3. **`deadline_misses` ~9000/s** with `LOUDNESS_DISABLE` and smooth audio means the counter is a weak glitch predictor when skip/insert stay at zero; trust skip/insert and fifo stability over raw deadline counts.

### Volume path optimizations (Track C next)

- **Done:** `usb_volume_format()` only when `spk_vol_usb_*` changes (not every USB OUT packet).
- **Done:** Update `spk_vol_mult_*` immediately on SET_CUR.
- **Next:** Reduce SET_CUR latency (avoid blocking control path during slider bursts); consider volume ramp or deferring non-audio work off the USB request handler.

## Success criteria

- No pitch wander at fixed volume
- Smooth loudness decrease with slider
- Skip/insert not correlated with glitches (or counts greatly reduced)
- Step changes inaudible or below spike test bound (`test_loudness_df2_step_transition_no_reset`)
- Idle bypass: sustained digital null does not increase skip/insert or deadline misses vs active filtering
