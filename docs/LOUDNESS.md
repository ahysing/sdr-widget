# Loudness compensation and playback volume

The USB audio path combines equal-loudness correction and playback volume in
one low-shelf biquad. Implementation:

- [`src/loudness.c`](../src/loudness.c): level mapping and deferred updates
- [`src/loudness_fast.c`](../src/loudness_fast.c): Q4.28 DF-II kernel and tables
- [`src/loudness_highres.c`](../src/loudness_highres.c): stride-2/4 interpolation
- [`loudnesscontour/create1loudnessvolume.py`](../loudnesscontour/create1loudnessvolume.py):
  coefficient generator

## Coefficient rows

Each base sample rate has 121 flat `biquad_quotients_fast_t` rows:

| Row | Phon | Baked volume |
|-----|------|--------------|
| 0 | 35.0 | -60.0 dB |
| 1 | 35.5 | -59.5 dB |
| 90 | 80.0 | -15.0 dB |
| 120 | 95.0 | 0.0 dB |

The 80-phon row has a flat loudness shape, but it is not an identity transfer:
its numerator contains -15 dB playback gain. Consequently there is no unity
shortcut; every non-idle sample runs through the normal biquad kernel.

The generator optimizes one low shelf against ISO 226 with 80 phon as the
reference. It multiplies `b0`, `b1`, and `b2` by
`10 ** (volume_db / 20)` before Q4.28 quantization. Poles `a1` and `a2` are not
volume-scaled.

## Row selection

USB volume is signed Q8.8 dB and is guaranteed to be in -60..0 dB. Listening
level uses 0.1 dB units:

```c
db_spl_x10 = 950 + gain_dbfs_x10;
row = (db_spl_x10 - 350) / 5;
```

The HID `equalizer_step` field reports the left/master row in the range 0..120.
Human-readable `db_spl` telemetry remains rounded to integer dB.

## Fixed-point kernel

Coefficients use Q4.28. Samples remain signed 24-bit integers. The canonical
DF-II accumulator remains aligned at bit 29 and stored delay states retain
13-bit headroom:

```c
PRODUCT_ALIGN_SHIFT = 13 + 29 - 28; /* 14 */
B0_EXTRA_SHIFT = 29 - 28;           /* 1 */
```

This separation is required for PC and AVR32 `macs.d` paths to remain
mathematically equivalent.

## Stereo and high-resolution policy

- 44.1/48/88.2/96 kHz: independent left/right coefficient rows and state.
- 176.4/192 kHz: both channels use the left/master row to minimize work; state
  and interpolation history remain per channel.
- 88.2/96 kHz run stride 2; 176.4/192 kHz run stride 4.

Hosts should send equal channel volume at 176.4/192 kHz.

## Filter modes and volume routing

Loudness processing combines up to two biquads (low-shelf then high-shelf) with
an optional external volume multiply (`adjust_volume` via `spk_vol_mult_*`). Coefficients
come from
[`loudnesscontour/create1loudnessvolume/create1loudnessvolume.py`](../loudnesscontour/create1loudnessvolume/create1loudnessvolume.py).

**Bass boost** applies a fixed 55-phon low-shelf contour (step 40) and bypasses
treble shaping. **Loudness** tracks listening level: the low-shelf row follows USB
volume (gain baked into coefficients when the host provides volume control), and the
high-shelf row follows the same phon step. **Inferred gain** selects the phon row
from the envelope follower when the host does not expose volume, using no-volume
low-shelf tables. **Filter off** skips both biquads and restores normal USB volume
scaling.

| Mode | Low-shelf | High-shelf | Volume stage |
|------|-----------|------------|--------------|
| **Bass boost** | `lowshelf_no_volume_*`, fixed step 40 (55 phon) | Identity (bypass) | External `adjust_volume` |
| **Loudness** (host volume) | `lowshelf_and_volume_*`, step from USB volume → phon | `highshelf_no_volume_*`, same phon | Baked in low-shelf (`keep_volume`) |
| **Inferred gain loudness** | `lowshelf_no_volume_*`, step from inferred gain | `highshelf_no_volume_*`, same phon | No multiply — level from PCM + step |
| **Filter off** | Skipped (`uac2` packet gate) | Skipped | External `adjust_volume` |

Baked-volume low-shelf numerators scale `b0`/`b1`/`b2` by `10^(volume_db/20)` at
table generation time while poles stay fixed — see `second_order_baked_coefficients`
in the generator script. External volume multiplies PCM after the biquad chain in
[`src/device_audio_task.c`](../src/device_audio_task.c).

## USB audio signal chain

1. Read USB samples.
2. Update inferred level only when no host volume control is available.
3. When active (not `FILTER_OFF_MODE` at 44.1/48 kHz), run low-shelf then
   high-shelf biquads:
   - ALT2: stereo 16-bit packet path
   - ALT1: per-channel 24-bit container path
4. Apply `device_audio_volume_apply_fn` (`keep_volume` or `adjust_volume`).
5. Apply explicit mute if requested.
6. Write samples to the DAC buffer.

With `LOUDNESS_MODE`, playback gain is baked into the low-shelf row and
`keep_volume` is a no-op. Bass boost and filter-off use `adjust_volume`.

## LOUDNESS_DISABLE USB facade

`LOUDNESS_DISABLE=1` removes runtime DSP but keeps Bass Boost and Loudness in USB
descriptors. `GET_CUR` returns on; `SET_CUR` is ignored. This avoids Windows
descriptor-cache churn between firmware builds. See [INSTALLATION.md](INSTALLATION.md).

## State and idle bypass

Coefficient changes publish through double banks without resetting per-channel
`w1`/`w2`. Cold zero input skips a channel whose biquad and high-resolution
state are already zero. After active filtering, fixed-point limit cycles may
prevent exact idle; output remains correct but CPU savings may be reduced.

Mute is independent of idle detection and always forces both output channels to
zero.

## Verification

`run-pc-tests.cmd` covers:

- 121-row half-dB mapping and telemetry
- Q4.28 exact integer outputs
- baked-volume magnitude
- coefficient transition equivalence
- independent channel behavior through 96 kHz
- shared master-row policy at 176.4/192 kHz
- high-resolution stride continuity
- saturation, sign, state, and idle behavior

See [LOUDNESS_DEVICE_DEBUG.md](LOUDNESS_DEVICE_DEBUG.md) for device validation.
