# Filter Analysis

The loudness filter compensates ISO 226:2003 loudness contours.

The `create2biquads.py` script optimizes a 2-biquad chain (low-shelf + high-shelf) for **13 contour levels** at **2 phon** spacing: **55, 57, 59, …, 79 phon**. Coefficients are stored in `{ a1, a2, b0, b1, b2 }` order (`a0 = 1`, not stored). A hand-written **80 phon** unity row (step 13) is appended in firmware.

55–79 phon corresponds to the listening range where loudness compensation is most useful; at **≥ 80 phon** the equalizer runs unity biquads.

**Runtime note:** ROM tables include both shelves per step, but AVR32 firmware steps **one** biquad section per sample per channel (`LOUDNESS_FAST_FILTERS=1` in [`src/loudness_fast.c`](../src/loudness_fast.c)). See [LOUDNESS.md](LOUDNESS.md) and [LOUDNESS_DEVICE_DEBUG.md](LOUDNESS_DEVICE_DEBUG.md).

## Results

The ISO226 curves used as reference looks like

![The ISO226](./iso226.png)

The filter looks like

![The filter](./filter.png)

The compensated loudness contours looks like

![loudness contour](./loudnesscontour.png)
