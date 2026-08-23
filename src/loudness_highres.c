#include "loudness_highres.h"
#include "loudness.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>

#define LOUDNESS_HIGHRES_FILTERS LOUDNESS_FAST_FILTERS

/*
 * Max stereo frames in one HS 16-bit USB packet (392 bytes / 8).
 */
#define LOUDNESS_MAX_STEREO_PACKET_SAMPLES  49u

static biquad_runtime_fast_t staging_hires_halfrate_runtime[LOUDNESS_HIGHRES_FILTERS];
static biquad_runtime_fast_t loudness_hires_halfrate_runtime_bank[2][LOUDNESS_HIGHRES_FILTERS];
static volatile uint8_t loudness_highres_active_bank;

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

static const biquad_runtime_fast_t *loudness_highres_active_runtime(void)
{
    return loudness_hires_halfrate_runtime_bank[loudness_highres_active_bank & 1u];
}

Bool loudness_highres_applies(uint32_t sample_rate_hz)
{
    return sample_rate_hz == 88200U || sample_rate_hz == 96000U
        || sample_rate_hz == 176400U || sample_rate_hz == 192000U;
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

/*
 * At 88.2/96/176.4/192 kHz run one biquad on the stereo mid (L+R)/2 and apply
 * the same shelf delta to both channels. Filter only even-index samples; odd
 * samples get the shelf delta linearly interpolated from their neighbors. Coeffs
 * for the biquad come from the native 44.1/48 kHz table (fs/2 shelf).
 */
void loudness_highres_filter_16bit_stereo_packet(S32 *sample_L, S32 *sample_R,
    U16 num_samples, biquad_state_fast_t *st)
{
    int32_t delta_buf[LOUDNESS_MAX_STEREO_PACKET_SAMPLES];
    const biquad_runtime_fast_t *runtime = loudness_highres_active_runtime();
    U16 i;

    for (i = 0; i < num_samples; i += 2u) {
        int32_t sL = loudness_highres_container_to_s16(sample_L[i]);
        int32_t sR = loudness_highres_container_to_s16(sample_R[i]);
        int32_t mid = (sL + sR) >> 1;
        int32_t y_mid;
        int32_t out_mid;
        int32_t delta;

        y_mid = loudness_fast_biquad1_step_runtime(mid << 8, st, runtime);
        out_mid = y_mid >> 8;
        if (out_mid > INT16_MAX) {
            out_mid = INT16_MAX;
        } else if (out_mid < INT16_MIN) {
            out_mid = INT16_MIN;
        }
        delta = out_mid - mid;
        delta_buf[i] = delta;
        sample_L[i] = loudness_highres_s16_to_container(sL + delta);
        sample_R[i] = loudness_highres_s16_to_container(sR + delta);
    }

    for (i = 1; i < num_samples; i += 2u) {
        int32_t sL = loudness_highres_container_to_s16(sample_L[i]);
        int32_t sR = loudness_highres_container_to_s16(sample_R[i]);
        int32_t delta;

        if ((i + 1u) < num_samples) {
            delta = (delta_buf[i - 1u] + delta_buf[i + 1u]) >> 1;
        } else {
            delta = delta_buf[i - 1u];
        }
        sample_L[i] = loudness_highres_s16_to_container(sL + delta);
        sample_R[i] = loudness_highres_s16_to_container(sR + delta);
    }
}

#ifdef BUILD_TESTING
void loudness_highres_test_filter_16bit_stereo_packet_fullrate(S32 *sample_L,
    S32 *sample_R, U16 num_samples, biquad_state_fast_t *st,
    const biquad_runtime_fast_t *runtime)
{
    U16 i;

    for (i = 0; i < num_samples; i++) {
        int32_t sL = loudness_highres_container_to_s16(sample_L[i]);
        int32_t sR = loudness_highres_container_to_s16(sample_R[i]);
        int32_t mid = (sL + sR) >> 1;
        int32_t y_mid = loudness_fast_biquad1_step_runtime(mid << 8, st, runtime);
        int32_t out_mid = y_mid >> 8;
        int32_t delta;

        if (out_mid > INT16_MAX) {
            out_mid = INT16_MAX;
        } else if (out_mid < INT16_MIN) {
            out_mid = INT16_MIN;
        }
        delta = out_mid - mid;
        sample_L[i] = loudness_highres_s16_to_container(sL + delta);
        sample_R[i] = loudness_highres_s16_to_container(sR + delta);
    }
}
#endif
