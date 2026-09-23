# Filter Analysis

The loudness filter compensates ISO 226:2003 loudness contours.

The `create1loudnessvolume.py` script optimizes one low-shelf biquad for **121
contour levels** at **0.5 phon** spacing from **35.0 through 95.0 phon**.
Coefficients are stored in `{ a1, a2, b0, b1, b2 }` order (`a0 = 1`, not
stored).

Each row also bakes its corresponding -60..0 dB playback gain into
`b0`/`b1`/`b2`. The 80-phon contour is flat in shape but still contains -15 dB
gain, so it is processed by the normal kernel.

See [LOUDNESS.md](LOUDNESS.md) and
[LOUDNESS_DEVICE_DEBUG.md](LOUDNESS_DEVICE_DEBUG.md).

## Results

The ISO226 curves used as reference looks like

![The ISO226](./iso226.png)

The filter looks like

![The filter](./filter.png)

The compensated loudness contours looks like

![loudness contour](./loudnesscontour.png)
