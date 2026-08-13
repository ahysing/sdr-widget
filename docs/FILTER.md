# Filter Analysis

In the loudness filter a compensates of the ISO226 2003 loudness curves

The `create2biquads.py` script optimizes a 2-biquad chain (low-shelf + high-shelf) for **13 contour levels** at **2 phon** spacing: **55, 57, 59, …, 79 phon**. Coefficients are stored in `{ a1, a2, b0, b1, b2 }` order (`a0 = 1`, not stored). A hand-written **80 phon** unity row (step 13) is appended in firmware.

55–79 phon corresponds to the listening range where loudness compensation is most useful; at **≥ 80 phon** the equalizer runs unity biquads.

## Results

The ISO226 curves used as reference looks like

![The ISO226](./iso226.png)

The filter looks like

![The filter](./filter.png)

The compensated loudness contours looks like

![loudness contour](./loudnesscontour.png)
