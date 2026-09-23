#include "loudness_first_order.h"
#include "loudness_fast.h"

/* Coefficients and the DF-II accumulator use Q4.28 throughout. */
#define LOUDNESS_DF2_Q28_SHIFT          28
#define LOUDNESS_DF2_Q28_ROUND          (1LL << (LOUDNESS_DF2_Q28_SHIFT - 1))
#define LOUDNESS_Q28_ONE                ((int32_t)1 << LOUDNESS_DF2_Q28_SHIFT)


/*
 * Canonical DF-II accumulates pole gain before zeros attenuate. At 50 Hz the
 * internal delay line can grow ~5000x larger than x[n] (~13 bits), overflowing
 * int32_t w1/w2 even when input and output stay in range.
 *
 * Fix: store w1/w2 right-shifted by M bits (w' = w >> M). Stored history
 * products are lifted by M before the Q4.28 pole and zero sums.
 */
#define LOUDNESS_DF2_STATE_HEADROOM_M   13
#define LOUDNESS_EQUALIZER_STEP_UNSET   UINT8_MAX


int32_t loudness_fast_biquad_first_order(int32_t x_n,
    biquad_first_order_state_t *st, const biquad_first_order_quotients_t *rt)
{
    int64_t acc;
    int64_t fb;
    int64_t w_unscaled;
    int32_t w0;
    int32_t y_n;

    fb = FMA_24BIT(0, rt->a1, st->w1);
    acc = ((int64_t)x_n << LOUDNESS_DF2_Q28_SHIFT)
        - (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    w_unscaled = acc >> LOUDNESS_DF2_Q28_SHIFT;

    fb = FMA_24BIT(0, rt->b1, st->w1);
    acc = (int64_t)rt->b0 * w_unscaled
        + (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    y_n = saturate_24bit_s64_to_s32(
        (acc + LOUDNESS_DF2_Q28_ROUND) >> LOUDNESS_DF2_Q28_SHIFT);

    w0 = loudness_saturate_s64_to_s32(w_unscaled >> LOUDNESS_DF2_STATE_HEADROOM_M);
    st->w1 = w0;
    return y_n;
}