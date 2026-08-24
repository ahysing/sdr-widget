# Loudness compensation

Digital loudness contour correction in the USB audio path. Implementation: [`src/loudness.c`](../src/loudness.c), [`src/loudness_fast.c`](../src/loudness_fast.c), [`src/loudness_highres.c`](../src/loudness_highres.c), [`src/loudness.h`](../src/loudness.h). Filter design plots: [FILTER.md](FILTER.md). HID debug fields: [USB_STATISTICS.md](USB_STATISTICS.md). RTOS/port roadmap: [LOUDNESS_PORT_PLAN.md](LOUDNESS_PORT_PLAN.md).

## Motivation — why compensation below 80 phon

Human hearing is not equally sensitive at all frequencies. At low listening levels, bass and treble are perceived as quieter relative to midrange than at higher levels. The science is based on listening experiments at different volumes. The findings of these experiements are implemented as a standard **ISO 226 2003**.

The motivation of the loudness control is to serve sound, which is mostly music, with the same percived effect to the listener at low volumes. In the process of making an studio album sound engineers make adjustments for the music to better fit playback (on a stereo, a car radio, or on a mobile device). This process is called mastering. By convention mastering is done between 75 dB and 80 dB volume. These volumes are great for movie theaters, but is often too loud for the casual listening. The loudness control aims to serve the music with the same percived balance between high and low notes at low volumes. It does so by fitting the loudness contours, which is the graphs from the ISO 226, to match the graph for 80 phon. 80 phon is 80 dB voume at 1khz.

See [Filter Analysis](FILTER.md) to see these how this fitting is done with digital filters.

### Digital Loudness

The loudness control predates digital music by many decades. Before the digital age hi-fi stereo systems would include a loudness button. Due to the limitations of the analog technology these hi-fi systems were built on these loudness controls boosted the bass to a fixed level. This works ok for the most part, but it does not take into account that different volumes requires diffent filter adjustments.

In the digital age we can measure the volume of the playback and find the exact filter needed to "lift" the curves exactly to the 80 phon curve.

## Formula

When the estimated listening level is **at or above 80 phon**, step **13** loads **unity biquads** (transparent IIR pass-through). Below 80 phon, frequency-dependent gain restores tonal balance.

Coefficient ROM holds **two** biquad sections per step (low-shelf ~120 Hz, high-shelf ~8 kHz). For AVR32 CPU budget, **one** section runs at runtime (`LOUDNESS_FAST_FILTERS=1`); see [LOUDNESS_DEVICE_DEBUG.md](LOUDNESS_DEVICE_DEBUG.md).

| Section (ROM) | Type | Approx. frequency | Role |
|---------------|------|-------------------|------|
| 0 | Low-shelf | ~120 Hz | Bass boost |
| 1 | High-shelf | ~8 kHz | Treble boost (table present; not stepped at runtime today) |

This is the classic hi-fi “loudness” control, implemented digitally in the device audio task before DAC output.

## Loudness level — how listening level is calculated

### Host volume

USB Audio Class volume sets `spk_vol_usb_L`. Host gain in dBFS:

```
gain_dbfs = (spk_vol_usb_L − VOL_MAX) / 256
```

Each step is 0.5 dB; `VOL_MAX` = 0 dBFS (full scale). Result is non-positive.

When no host volume control is active, gain is inferred from PCM peaks via [`loudness_inferred_gain`](../src/loudness_inferred_gain.c).

### Background evaluation

A FreeRTOS task runs every **~20 ms** and calls [`loudness_update_active_equalizer_step()`](../src/loudness.c), which:

1. Maps host gain to an integer **dB SPL** estimate.
2. Compares it to `last_db_spl` to select an equalizer step and update statistics snapshots.

The per-sample audio path runs the active biquad chain — it does not recompute the level estimate.

## Equalizers

**14** pre-designed coefficient sets: steps **0–12** at **2 phon** spacing (**55, 57, …, 79 phon**), plus step **13** (**80 phon**, unity biquads).

| Step | Phon | Compensation |
|------|------|--------------|
| 0 | 55 | Strongest bass/treble lift |
| 1–11 | 57–77 | Intermediate contours |
| 12 | 79 | Near-reference contour |
| 13 | ≥ 80 | Unity biquads (transparent) |

Step lookup: [`loudness_get_equalizer_step()`](../src/loudness.c). Per-step boundary **hysteresis** (0.5 dB below each band floor) avoids flutter; step **12↔13** switches at **79.5 / 80.0 phon**.

### Volume gain (`gain_dbfs`)

Host USB volume (or inferred gain) maps directly to the listening-level estimate. At full digital volume, `gain_dbfs = 0`. Turning the slider down yields negative values (e.g. −10 dBFS).

### Effective loudness formula (dB SPL)

From [`loudness_calculate_db_spl()`](../src/loudness.c):

```
db_spl = LOUDNESS_DB_SPL_MAX + gain_dbfs
```

Typical result range: **~35–95 dB SPL** (depends on `LOUDNESS_DB_SPL_MAX` and host gain floor).

**Examples:**

| Condition | gain_dbfs | db_spl (approx.) |
|-----------|-----------|------------------|
| Full volume | 0 | ~95 (bypass at 80 phon) |
| Volume −10 dBFS | −10 | ~85 |
| Volume −30 dBFS | −30 | ~65 |

## Transitioning between loudness levels

The biquad runs as **canonical Direct Form II** with per-channel `w1`/`w2` delay state (`LOUDNESS_CHANNELS=2`: L=0, R=1). FAST stores states with **M-bit headroom** (scaled canonical DF-II) so internal pole buildup at low frequencies does not overflow `int32_t` states.

**Shared across L/R:** equalizer step, coefficient banks (`biquad_quotients_fast_t`, `biquad_runtime_fast_t`).

**Per channel:** `biquad_state_fast_t` (`w1`, `w2`); at 88.2/96/176.4/192 kHz also `loudness_highres_channel_state_t` (delta-interpolation between anchor biquad steps).

Coefficients commit **immediately** when the background task selects a new step. **`w1`/`w2` are not scaled, reconstructed, or reset** on step change — active quotients publish via a double bank flip (see [`LOUDNESS_DEVICE_DEBUG.md`](LOUDNESS_DEVICE_DEBUG.md) for on-device debugging).

1. **Step change:** when hysteresis boundaries are crossed, [`loudness_fast_select_equalizer_step()`](../src/loudness_fast.c) stages new coefficients and publishes the inactive bank with a single index flip.
2. **Events:** firmware records `USB_STATS_TAG_EQUALIZER_STEP_SWITCH` (prev dB SPL, new dB SPL, step index 0–13).
3. **Step 13:** at ≥ 80 phon the unity biquad row runs through the normal filter chain (not a separate hot-path skip).

Sample-rate changes re-scale coefficient tables via [`loudness_change_frequency_fast()`](../src/loudness_fast.c) (queued from the USB sample-rate handler on firmware). At **88.2, 96, 176.4, and 192 kHz** the highres path ([`src/loudness_highres.c`](../src/loudness_highres.c)) runs one biquad anchor per channel per stride-2 or stride-4 block with linear interpolation between anchors.

PC tests include a **State Transition Equivalence** check: mid-stream coefficient swaps must be bit-exact vs running the target curve from a clean start.

## Idle bypass (steady digital silence)

When the host keeps streaming **sample == 0** with the USB connection open, the firmware skips work that cannot change the output:

| Condition | Action |
|-----------|--------|
| `sample == 0` and `loudness_channel_filter_is_idle(ch)` | Skip filter step for that channel; output stays 0 |
| `sample == 0` but channel **not** idle | Run filter with `x=0` for that channel only (drain `w1`/`w2` and highres interp state) |
| `loudness_filter_is_active()` | Run `spk_vol_mult` gain multiply (when not muted) |
| `!loudness_filter_is_active()` | Skip `spk_vol_mult` multiply |
| `usb_spk_mute != 0` | Force samples to 0 (unchanged; independent of filter idle) |

**Per-channel idle** (`loudness_channel_filter_is_idle(ch)`):

- Biquad: `w1 == 0 && w2 == 0`
- Highres (when rate applies): `y_prev_biquad`, `y_derivative`, `y_current_est` all zero and `sample_counter == 0`

**Filter active** (`loudness_filter_is_active()`): true if **either** channel is non-idle.

Implementation: [`src/loudness_fast.c`](../src/loudness_fast.c), [`src/loudness_highres.c`](../src/loudness_highres.c), volume gating in [`src/uac2_device_audio_task.c`](../src/uac2_device_audio_task.c). The envelope follower is **not** skipped on silent packets (inferred-gain decay still runs).

At non-unity equalizer steps, fixed-point biquad state can settle into a small **limit cycle** (`w1`/`w2` never reach exactly zero). Bypass then does not engage after prior audio until state is reset; output remains correct, only CPU savings are reduced. Unity (step 13) and cold-start silence drain to exact idle reliably.

PC tests: `test_filter_idle_after_zeros_*`, `test_per_channel_zero_bypass`, `test_per_channel_independent_biquad` in `tests/pc/loudness_tests.c`.

## Signal chain summary

Per USB OUT packet in [`src/uac2_device_audio_task.c`](../src/uac2_device_audio_task.c):

1. **~20 ms task** (background): map host gain → integer dB SPL → equalizer step (with hysteresis).
2. **Packet path** (16-bit alt setting): [`loudness_filter_16bit_stereo_packet()`](../src/loudness_fast.c) — per-channel biquad (+ highres stride at ≥ 88.2 kHz).
3. **Per-sample path** (24-bit alt setting): `LOUDNESS_FILTER_24BIT_CONTAINER(ch, sample)` with `ch` 0=L, 1=R.
4. **Mute** (`usb_spk_mute`): force L/R to 0 when set.
5. **Volume** (`spk_vol_mult_*`): gain multiply when filter was active at packet start (`loudness_filter_is_active()`); skipped when fully idle.
6. **DAC** output buffer.

Source modules: [`src/loudness.c`](../src/loudness.c) (level + RTOS deferral), [`src/loudness_fast.c`](../src/loudness_fast.c) (biquad + idle API), [`src/loudness_highres.c`](../src/loudness_highres.c) (stride-2/4 interpolation). See [INSTALLATION.md](INSTALLATION.md) for build flags.

## USB statistics debug fields

The 1 Hz HID statistics packet exposes loudness state for debugging (see [USB_STATISTICS.md](USB_STATISTICS.md)):

| Field | Meaning |
|-------|---------|
| `gain_dbfs` | Host volume dBFS from `loudness_get_gain_dbfs()` |
| `db_spl` | Listening level used for step selection |
| `equalizer_step` | Active contour step (0–13) |
