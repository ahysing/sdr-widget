# Loudness + volume device debugging

Procedures for validating the single-biquad Q4.28 path on Henry Audio hardware.

## Build variants

| Build | Command | Purpose |
|-------|---------|---------|
| Normal | `make audio-widget` | Baked loudness + volume coefficients |
| No loudness | `make audio-widget LOUDNESS_DISABLE=1` | Transport baseline with standalone volume multiply |

The normal build has no unity shortcut. Every non-idle sample runs through the
biquad because each row includes playback volume in `b0`, `b1`, and `b2`.

## Device test

1. Flash `Release/widget.elf`.
2. Start playback at 44.1, 48, 88.2, 96, 176.4, and 192 kHz.
3. Run:

```powershell
python usbstatistics/usbstatistics.py --verbose --deltas
```

4. Sweep Windows volume from 0 to -60 dB in 0.5 dB increments where practical.
5. Check that `equalizer_step_left/right` move from 120 to 0, audio level changes once
   (no double attenuation), and pitch remains stable.

At 44.1/48/88.2/96 kHz, left and right channels may use independent rows. At
176.4/192 kHz, both channels use one row selected from the average host volume.

## What to monitor

- `skip` / `insert`: direct transport failures.
- FIFO minimum/maximum: stability around the target fill.
- `deadline_misses`: useful only together with transport events.
- `equalizer_step_left/right`: active per-channel rows, or the shared averaged
  row above 96 kHz.
- `gain_dbfs_left/right`: effective per-channel gains, 0..-60 dB.
- `db_spl_left/right`: effective per-channel listening levels.

## Coefficient publication

USB volume changes are deferred to the loudness task. Q4.28 runtime
coefficients publish through an inactive bank followed by one bank-index flip,
so the audio task reads complete coefficient sets.

## Idle bypass

Cold digital silence skips per-channel filter work while state is exactly idle.
After active non-unity filtering, fixed-point state may settle into a limit
cycle rather than exact zero; this affects CPU savings, not output correctness.
Mute remains a separate explicit zero-output path.

## Regression commands

```powershell
run-pc-tests.cmd
make test-avr32
make clean
make audio-widget
```

`make test-avr32` is a compile check. The PC suite covers Q4.28 exact samples,
0.5 dB row mapping, baked gain, channel policy, high-resolution stride paths,
state independence, and telemetry.

## Success criteria

- Volume is applied exactly once.
- No identity/unity shortcut symbols remain.
- Left/right balance works through 96 kHz.
- 176.4/192 kHz use the shared averaged row.
- No pitch wander, new clipping, or coefficient-transition spikes.
- Skip/insert counts and FIFO stability are no worse than the transport baseline.
