#include "loudness_highres.h"
#include "loudness.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>

#define LOUDNESS_HIGHRES_FILTERS LOUDNESS_FAST_FILTERS

static biquad_runtime_fast_t staging_hires_halfrate_runtime[LOUDNESS_HIGHRES_FILTERS];
static biquad_runtime_fast_t loudness_hires_halfrate_runtime_bank[2][LOUDNESS_HIGHRES_FILTERS];
static volatile uint8_t loudness_highres_active_bank;

static loudness_highres_channel_state_t loudness_highres_ch[2];
static loudness_fast_biquad1_step_runtime_stride_fn loudness_fast_biquad1_stride_step =
    loudness_fast_biquad1_step_runtime_stride2;

static void loudness_highres_runtime_from_quotients(const biquad_quotients_fast_t *src,
    biquad_runtime_fast_t *dst)
{
    dst->b0 = src->b0;
    dst->b1 = src->b1;
    dst->b2 = src->b2;
    dst->a1 = src->a1;
    dst->a2 = src->a2;
}

static inline int32_t loudness_highres_s16_to_container(int32_t s)
{
    if (s > INT16_MAX) {
        s = INT16_MAX;
    } else if (s < INT16_MIN) {
        s = INT16_MIN;
    }
    return s << 16;
}

static inline int32_t loudness_highres_container_to_s16(int32_t container)
{
    return (int32_t)(int16_t)(container >> 16);
}

static inline int32_t loudness_highres_s16_to_24bit(int32_t container)
{
    return loudness_highres_container_to_s16(container) << 8;
}

static inline int32_t loudness_highres_y24_to_container(int32_t y_24)
{
    int32_t s;

    s = y_24 >> 8;
    return loudness_highres_s16_to_container(s);
}

static const biquad_runtime_fast_t *loudness_highres_active_runtime(void)
{
    return loudness_hires_halfrate_runtime_bank[loudness_highres_active_bank & 1u];
}

Bool loudness_highres_applies(uint32_t sample_rate_hz)
{
    return sample_rate_hz == 88200U || sample_rate_hz == 96000U
        || sample_rate_hz == 176400U || sample_rate_hz == 192000U;
}

void loudness_highres_set_stride(uint32_t sample_rate_hz)
{
    if (!loudness_highres_applies(sample_rate_hz)) {
        return;
    }

    memset(loudness_highres_ch, 0, sizeof(loudness_highres_ch));
    loudness_fast_biquad1_stride_step = (sample_rate_hz >= 176400U)
        ? loudness_fast_biquad1_step_runtime_stride4
        : loudness_fast_biquad1_step_runtime_stride2;
}

void loudness_highres_reset_states(void)
{
    memset(loudness_highres_ch, 0, sizeof(loudness_highres_ch));
}

const biquad_quotients_fast_t *loudness_highres_halfrate_quotients(
    uint32_t sample_rate_hz, int equalizer_step,
    const biquad_quotients_fast_t (*quotients_44100hz)[LOUDNESS_HIGHRES_QUOTIENT_SECTIONS],
    const biquad_quotients_fast_t (*quotients_48000hz)[LOUDNESS_HIGHRES_QUOTIENT_SECTIONS])
{
    if (sample_rate_hz == 88200U || sample_rate_hz == 176400U) {
        return &quotients_44100hz[equalizer_step][0];
    }
    if (sample_rate_hz == 96000U || sample_rate_hz == 192000U) {
        return &quotients_48000hz[equalizer_step][0];
    }
    return NULL;
}

void loudness_highres_staging_fill_halfrate(
    const biquad_quotients_fast_t *halfrate_src,
    const biquad_runtime_fast_t *main_runtime)
{
    int i;

    for (i = 0; i < LOUDNESS_HIGHRES_FILTERS; i++) {
        if (halfrate_src != NULL) {
            loudness_highres_runtime_from_quotients(halfrate_src,
                &staging_hires_halfrate_runtime[i]);
        } else {
            staging_hires_halfrate_runtime[i] = main_runtime[i];
        }
    }
}

void loudness_highres_staging_commit(void)
{
    uint8_t inactive = (uint8_t)(loudness_highres_active_bank ^ 1u);

    memcpy(loudness_hires_halfrate_runtime_bank[inactive],
        staging_hires_halfrate_runtime, sizeof(staging_hires_halfrate_runtime));
    loudness_highres_active_bank = inactive;
}

void loudness_highres_unity_advance_stereo_packet(S32 *sample_L, S32 *sample_R,
    U16 num_samples, biquad_state_fast_t *st)
{
    U16 i;

    for (i = 0; i < num_samples; i++) {
        int32_t xL = loudness_highres_s16_to_24bit(sample_L[i]);
        int32_t xR = loudness_highres_s16_to_24bit(sample_R[i]);

        loudness_fast_biquad1_unity_advance_state_24bit(xL, st);
        loudness_fast_biquad1_unity_advance_state_24bit(xR, st);
    }
}

/*
 * At 88.2/96 kHz (stride 2) and 176.4/192 kHz (stride 4), run one shared biquad
 * with per-channel delta-interpolated output between anchor samples.
 */
void loudness_highres_filter_16bit_stereo_packet(S32 *sample_L, S32 *sample_R,
    U16 num_samples, biquad_state_fast_t *st)
{
    const biquad_runtime_fast_t *runtime = loudness_highres_active_runtime();
    loudness_fast_biquad1_step_runtime_stride_fn step = loudness_fast_biquad1_stride_step;
    U16 i;

    for (i = 0; i < num_samples; i++) {
        int32_t xL = loudness_highres_s16_to_24bit(sample_L[i]);
        int32_t xR = loudness_highres_s16_to_24bit(sample_R[i]);
        int32_t yL = step(xL, st, &loudness_highres_ch[0], runtime);
        int32_t yR = step(xR, st, &loudness_highres_ch[1], runtime);

        sample_L[i] = loudness_highres_y24_to_container(yL);
        sample_R[i] = loudness_highres_y24_to_container(yR);
    }
}

#ifdef BUILD_TESTING
void loudness_highres_test_filter_16bit_stereo_packet_fullrate(S32 *sample_L,
    S32 *sample_R, U16 num_samples, biquad_state_fast_t *st,
    const biquad_runtime_fast_t *runtime)
{
    U16 i;

    for (i = 0; i < num_samples; i++) {
        int32_t xL = loudness_highres_s16_to_24bit(sample_L[i]);
        int32_t xR = loudness_highres_s16_to_24bit(sample_R[i]);
        int32_t yL = loudness_fast_biquad1_step_runtime(xL, st, runtime);
        int32_t yR = loudness_fast_biquad1_step_runtime(xR, st, runtime);

        sample_L[i] = loudness_highres_y24_to_container(yL);
        sample_R[i] = loudness_highres_y24_to_container(yR);
    }
}

const loudness_highres_channel_state_t *loudness_highres_test_channel_state(int channel)
{
    if (channel < 0 || channel > 1) {
        return NULL;
    }
    return &loudness_highres_ch[channel];
}
#endif
