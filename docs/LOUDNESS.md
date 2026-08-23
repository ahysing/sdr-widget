# Loudness compensation

Digital loudness contour correction in the USB audio path. Implementation: [`src/loudness.c`](../src/loudness.c), [`src/loudness.h`](../src/loudness.h). Filter design plots: [FILTER.md](FILTER.md). HID debug fields: [USB_STATISTICS.md](USB_STATISTICS.md).

## Motivation — why compensation below 80 phon

Human hearing is not equally sensitive at all frequencies. At low listening levels, bass and treble are perceived as quieter relative to midrange than at higher levels. The science is based on listening experiments at different volumes. The findings of these experiements are implemented as a standard **ISO 226 2003**.

The motivation of the loudness control is to serve sound, which is mostly music, with the same percived effect to the listener at low volumes. In the process of making an studio album sound engineers make adjustments for the music to better fit playback (on a stereo, a car radio, or on a mobile device). This process is called mastering. By convention mastering is done between 75 dB and 80 dB volume. These volumes are great for movie theaters, but is often too loud for the casual listening. The loudness control aims to serve the music with the same percived balance between high and low notes at low volumes. It does so by fitting the loudness contours, which is the graphs from the ISO 226, to match the graph for 80 phon. 80 phon is 80 dB voume at 1khz.

See [Filter Analysis](FILTER.md) to see these how this fitting is done with digital filters.

### Digital Loudness

The loudness control predates digital music by many decades. Before the digital age hi-fi stereo systems would include a loudness button. Due to the limitations of the analog technology these hi-fi systems were built on these loudness controls boosted the bass to a fixed level. This works ok for the most part, but it does not take into account that different volumes requires diffent filter adjustments.

In the digital age we can measure the volume of the playback and find the exact filter needed to "lift" the curves exactly to the 80 phon curve.

## Formula

When the estimated listening level is **at or above 80 phon**, step **13** loads **unity biquads** (transparent IIR pass-through). Below 80 phon, two IIR biquad sections apply frequency-dependent gain to restore tonal balance:

| Filter | Type | Approx. frequency | Role |
|--------|------|-------------------|------|
| 0 | Low-shelf | ~120 Hz | Bass boost |
| 1 | High-shelf | ~8 kHz | Treble boost |

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

The biquad runs as **canonical Direct Form II** with `w1`/`w2` delay state per shelf section (low + high). FAST stores states with **M-bit headroom** (scaled canonical DF-II) so internal pole buildup at low frequencies does not overflow `int32_t` states.

Coefficients commit **immediately** when the background task selects a new step. **`w1`/`w2` are not scaled, reconstructed, or reset** on step change — active quotients publish via a double bank flip (see [`LOUDNESS_DEVICE_DEBUG.md`](LOUDNESS_DEVICE_DEBUG.md) for on-device debugging).

1. **Step change:** when hysteresis boundaries are crossed, [`loudness_select_equalizer_step()`](../src/loudness.c) stages new coefficients and copies them into `active_quotients` inside a short critical section.
2. **Events:** firmware records `USB_STATS_TAG_EQUALIZER_STEP_SWITCH` (prev dB SPL, new dB SPL, step index 0–13).
3. **Step 13:** at ≥ 80 phon the unity biquad row runs through the normal filter chain (not a separate hot-path skip).

Sample-rate changes re-scale coefficient tables via [`loudness_change_frequency()`](../src/loudness.c) (queued from the USB sample-rate handler).

PC tests include a **State Transition Equivalence** check: mid-stream coefficient swaps must be bit-exact vs running the target curve from a clean start.

## Signal chain summary

1. ~20 ms task: map host gain → integer dB SPL → equalizer step (with hysteresis)
2. Per sample: biquad filter chain (unity at step 13)
3. USB volume lookup table
4. DAC output

The firmware uses the biquad path (2-bit Q29). See [INSTALLATION.md](INSTALLATION.md) for build flags.

## USB statistics debug fields

The 1 Hz HID statistics packet exposes loudness state for debugging (see [USB_STATISTICS.md](USB_STATISTICS.md)):

| Field | Meaning |
|-------|---------|
| `gain_dbfs` | Host volume dBFS from `loudness_get_gain_dbfs()` |
| `db_spl` | Listening level used for step selection |
| `equalizer_step` | Active contour step (0–13) |
