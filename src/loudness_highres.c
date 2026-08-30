#include "loudness_highres.h"
#include "loudness.h"
#include "loudness_internal.h"
#include "taskAK5394A.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <math.h>

static biquad_runtime_fast_t staging_hires_halfrate_runtime[LOUDNESS_CHANNELS];
static biquad_runtime_fast_t
    loudness_hires_halfrate_runtime_bank[2][LOUDNESS_CHANNELS];
static volatile uint8_t loudness_highres_active_bank;
static uint32_t loudness_highres_frequency_hz;

static loudness_highres_channel_state_t loudness_highres_ch[2];
static loudness_highres_biquad1_step_stride_fn loudness_highres_biquad1_stride_step =
    NULL;

static biquad_quotients_fast_t loudness_highres_quotients_scaled[
    LOUDNESS_NUM_EQUALIZER_STEPS];

#define LOUDNESS_HIGHRES_DEFINE_BIQUAD1_STEP_RUNTIME_STRIDE(fn_name, shift_plus_one, counter_mask) \
int32_t fn_name(int32_t x_24, biquad_state_fast_t *st,                                      \
    loudness_highres_channel_state_t *ch, const biquad_runtime_fast_t *rt)                  \
{                                                                                             \
    if (ch->sample_counter == 0) {                                                            \
        int32_t y_true = loudness_fast_biquad1_step_runtime(x_24, st, rt);                  \
        ch->y_derivative = (y_true - ch->y_prev_biquad) >> ((shift_plus_one) - 1);           \
        ch->y_current_est = y_true;                                                           \
        ch->y_prev_biquad = y_true;                                                           \
        ch->sample_counter = 1;                                                               \
        return y_true;                                                                        \
    }                                                                                         \
    ch->y_current_est += ch->y_derivative;                                                    \
    ch->sample_counter++;                                                                     \
    ch->sample_counter &= (counter_mask);                                                     \
    return ch->y_current_est;                                                                 \
}

LOUDNESS_HIGHRES_DEFINE_BIQUAD1_STEP_RUNTIME_STRIDE(
    loudness_highres_biquad1_step_runtime_stride4, 3, 0x03)
LOUDNESS_HIGHRES_DEFINE_BIQUAD1_STEP_RUNTIME_STRIDE(
    loudness_highres_biquad1_step_runtime_stride2, 2, 0x01)

static Bool loudness_highres_share_master_row(void)
{
    return TRUE;
}

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
    int32_t s = y_24 >> 8;
    return loudness_highres_s16_to_container(s);
}

static const biquad_runtime_fast_t *loudness_highres_active_runtime(void)
{
    return loudness_hires_halfrate_runtime_bank[loudness_highres_active_bank & 1u];
}

static int32_t loudness_highres_db_spl_x10_for_channel(int channel)
{
    int32_t gain_q8 = (channel == 1)
        ? (int32_t)target_gain_dbfs_right_q8
        : (int32_t)target_gain_dbfs_left_q8;

    return (LOUDNESS_DB_SPL_MAX * 10)
        + loudness_gain_dbfs_q8_to_x10(
            loudness_clamp_gain_dbfs_q8(gain_q8));
}

#define LOUDNESS_Q28_ONE ((int32_t)1 << 28)

static biquad_quotients_fast_t loudness_highres_scale_quotients_fast(
    biquad_quotients_fast_t base, uint32_t n)
{
    if (n <= 1 || n > 4) {
        return base;
    }
    double b0 = (double)base.b0 / (double)LOUDNESS_Q28_ONE;
    double b1 = (double)base.b1 / (double)LOUDNESS_Q28_ONE;
    double b2 = (double)base.b2 / (double)LOUDNESS_Q28_ONE;
    double a1 = (double)base.a1 / (double)LOUDNESS_Q28_ONE;
    double a2 = (double)base.a2 / (double)LOUDNESS_Q28_ONE;
    double cosw = -a1 / (1.0 + a2);
    if (cosw > 1.0) {
        cosw = 1.0;
    } else if (cosw < -1.0) {
        cosw = -1.0;
    }
    double w0 = acos(cosw);
    double k = tan(w0 / (2.0 * n)) / tan(w0 / 2.0);
    double den_s = 1.0 - a1 + a2;
    if (fabs(den_s) < 1e-12) {
        return base;
    }
    double N0 = (b0 + b1 + b2) / den_s;
    double N1 = 2.0 * (b0 - b2) / den_s;
    double N2 = (b0 - b1 + b2) / den_s;
    double D1 = 2.0 * (1.0 - a2) / den_s;
    double D2 = (1.0 + a1 + a2) / den_s;
    double k2 = k * k;
    double N1_new = N1 / k;
    double N2_new = N2 / k2;
    double D1_new = D1 / k;
    double D2_new = D2 / k2;
    double a0_n = D2_new + D1_new + 1.0;
    if (fabs(a0_n) < 1e-12) {
        return base;
    }
    double b0_n = (N2_new + N1_new + N0) / a0_n;
    double b1_n = 2.0 * (N0 - N2_new) / a0_n;
    double b2_n = (N2_new - N1_new + N0) / a0_n;
    double a1_n = 2.0 * (1.0 - D2_new) / a0_n;
    double a2_n = (D2_new - D1_new + 1.0) / a0_n;
    biquad_quotients_fast_t result;
    result.b0 = (int32_t)round(b0_n * (double)LOUDNESS_Q28_ONE);
    result.b1 = (int32_t)round(b1_n * (double)LOUDNESS_Q28_ONE);
    result.b2 = (int32_t)round(b2_n * (double)LOUDNESS_Q28_ONE);
    result.a1 = (int32_t)round(a1_n * (double)LOUDNESS_Q28_ONE);
    result.a2 = (int32_t)round(a2_n * (double)LOUDNESS_Q28_ONE);
    return result;
}

static const biquad_quotients_fast_t *loudness_highres_resolve_scaled_quotient_table(
    uint32_t frequency)
{
    const biquad_quotients_fast_t *base_table;
    uint32_t n = 1;
    int b;

    base_table = loudness_fast_baked_quotient_table_44100hz();
    if (frequency % (uint32_t)FREQ_48 == 0) {
        base_table = loudness_fast_baked_quotient_table_48000hz();
        n = frequency / (uint32_t)FREQ_48;
    } else if (frequency % (uint32_t)FREQ_44 == 0) {
        base_table = loudness_fast_baked_quotient_table_44100hz();
        n = frequency / (uint32_t)FREQ_44;
    }
    if (n > 4) {
        n = 4;
    }
    if (n > 1) {
        for (b = 0; b < LOUDNESS_NUM_EQUALIZER_STEPS; b++) {
            loudness_highres_quotients_scaled[b] =
                loudness_highres_scale_quotients_fast(base_table[b], n);
        }
        base_table = loudness_highres_quotients_scaled;
    }
    return base_table;
}

static void loudness_highres_fill_staging_quotients_fast(
    int equalizer_step_left, int equalizer_step_right)
{
    const biquad_quotients_fast_t *table =
        loudness_fast_active_equalizer_step_table();
    const biquad_quotients_fast_t *hires_src_left;
    const biquad_quotients_fast_t *hires_src_right;
    biquad_runtime_fast_t staging_runtime[LOUDNESS_CHANNELS];

    if (loudness_highres_share_master_row()) {
        equalizer_step_right = equalizer_step_left;
    }

    hires_src_left = loudness_highres_halfrate_quotients(
        loudness_highres_frequency_hz, equalizer_step_left,
        loudness_fast_baked_quotient_table_44100hz(),
        loudness_fast_baked_quotient_table_48000hz());
    if (loudness_highres_share_master_row()) {
        loudness_fast_stage_shared_equalizer_step(table, equalizer_step_left);
        staging_runtime[0] = *loudness_fast_channel_runtime(0);
        loudness_highres_staging_fill_halfrate_shared(hires_src_left,
            staging_runtime);
        return;
    }

    hires_src_right = loudness_highres_halfrate_quotients(
        loudness_highres_frequency_hz, equalizer_step_right,
        loudness_fast_baked_quotient_table_44100hz(),
        loudness_fast_baked_quotient_table_48000hz());
    loudness_fast_stage_equalizer_steps(table, equalizer_step_left,
        equalizer_step_right);
    staging_runtime[0] = *loudness_fast_channel_runtime(0);
    staging_runtime[1] = *loudness_fast_channel_runtime(1);
    loudness_highres_staging_fill_halfrate_independent(
        hires_src_left, hires_src_right, staging_runtime);
}

Bool loudness_highres_applies(uint32_t sample_rate_hz)
{
    return sample_rate_hz == (uint32_t)FREQ_88
        || sample_rate_hz == (uint32_t)FREQ_96
        || sample_rate_hz == (uint32_t)FREQ_176
        || sample_rate_hz == (uint32_t)FREQ_192;
}

void loudness_highres_current_stereo_db_spl_x10(
    int32_t *db_spl_left_x10, int32_t *db_spl_right_x10)
{
    int32_t left_x10 = loudness_highres_db_spl_x10_for_channel(0);
    int32_t right_x10 = loudness_highres_db_spl_x10_for_channel(1);

    if (loudness_highres_share_master_row()) {
        int32_t shared = (left_x10 + right_x10) / 2;
        *db_spl_left_x10 = shared;
        *db_spl_right_x10 = shared;
    } else {
        *db_spl_left_x10 = left_x10;
        *db_spl_right_x10 = right_x10;
    }
}

void loudness_highres_change_frequency(uint32_t frequency)
{
    int32_t db_spl_left_x10;
    int32_t db_spl_right_x10;
    int equalizer_step_left;
    int equalizer_step_right;
    const biquad_quotients_fast_t *table;

    if (!loudness_highres_applies(frequency)) {
        return;
    }

    loudness_highres_frequency_hz = frequency;
    loudness_highres_set_stride(frequency);

    loudness_highres_current_stereo_db_spl_x10(
        &db_spl_left_x10, &db_spl_right_x10);
    equalizer_step_left = loudness_get_equalizer_step(db_spl_left_x10);
    equalizer_step_right = loudness_get_equalizer_step(db_spl_right_x10);

    table = loudness_highres_resolve_scaled_quotient_table(frequency);
    loudness_fast_set_active_equalizer_step_table(table);
    loudness_highres_fill_staging_quotients_fast(equalizer_step_left,
        equalizer_step_right);
    loudness_fast_commit_staged_quotients();
    loudness_highres_staging_commit();
    loudness_fast_refresh_idle_cache();
}

void loudness_highres_set_stride(uint32_t sample_rate_hz)
{
    memset(loudness_highres_ch, 0, sizeof(loudness_highres_ch));
    loudness_highres_biquad1_stride_step = (sample_rate_hz >= (uint32_t)FREQ_176)
        ? loudness_highres_biquad1_step_runtime_stride4
        : loudness_highres_biquad1_step_runtime_stride2;
}

void loudness_highres_reset_states(void)
{
    memset(loudness_highres_ch, 0, sizeof(loudness_highres_ch));
}

const biquad_quotients_fast_t *loudness_highres_halfrate_quotients(
    uint32_t sample_rate_hz, int equalizer_step,
    const biquad_quotients_fast_t *quotients_44100hz,
    const biquad_quotients_fast_t *quotients_48000hz)
{
    if (sample_rate_hz == (uint32_t)FREQ_88
        || sample_rate_hz == (uint32_t)FREQ_176) {
        return &quotients_44100hz[equalizer_step];
    }
    if (sample_rate_hz == (uint32_t)FREQ_96
        || sample_rate_hz == (uint32_t)FREQ_192) {
        return &quotients_48000hz[equalizer_step];
    }
    return NULL;
}

void loudness_highres_staging_fill_halfrate_independent(
    const biquad_quotients_fast_t *halfrate_src_left,
    const biquad_quotients_fast_t *halfrate_src_right,
    const biquad_runtime_fast_t *main_runtime)
{
    int channel;
    for (channel = 0; channel < LOUDNESS_CHANNELS; channel++) {
        const biquad_quotients_fast_t *src =
            (channel == 0) ? halfrate_src_left : halfrate_src_right;
        if (src != NULL) {
            loudness_highres_runtime_from_quotients(src,
                &staging_hires_halfrate_runtime[channel]);
        } else {
            staging_hires_halfrate_runtime[channel] = main_runtime[channel];
        }
    }
}

void loudness_highres_staging_fill_halfrate_shared(
    const biquad_quotients_fast_t *halfrate_src,
    const biquad_runtime_fast_t *main_runtime)
{
    if (halfrate_src != NULL) {
        loudness_highres_runtime_from_quotients(halfrate_src,
            &staging_hires_halfrate_runtime[0]);
    } else {
        staging_hires_halfrate_runtime[0] = main_runtime[0];
    }
}

void loudness_highres_staging_commit(void)
{
    uint8_t inactive = (uint8_t)(loudness_highres_active_bank ^ 1u);

    memcpy(loudness_hires_halfrate_runtime_bank[inactive],
        staging_hires_halfrate_runtime, sizeof(staging_hires_halfrate_runtime));
    loudness_highres_active_bank = inactive;
}

Bool loudness_highres_channel_is_idle(int channel)
{
    const loudness_highres_channel_state_t *ch;

    if (channel < 0 || channel > 1) {
        return TRUE;
    }
    ch = &loudness_highres_ch[channel];
    return ch->y_prev_biquad == 0 && ch->y_derivative == 0
        && ch->y_current_est == 0 && ch->sample_counter == 0;
}

/*
 * At 88.2/96 kHz (stride 2) and 176.4/192 kHz (stride 4), run one biquad per
 * channel with delta-interpolated output between anchor samples.
 */
static void loudness_highres_filter_16bit_stereo_packet_with_runtime(
    S32 *sample_L, S32 *sample_R,
    biquad_state_fast_t *stL, biquad_state_fast_t *stR,
    const biquad_runtime_fast_t *runtime_left,
    const biquad_runtime_fast_t *runtime_right, U16 num_samples)
{
    loudness_highres_biquad1_step_stride_fn step = loudness_highres_biquad1_stride_step;
    Bool idle_L = loudness_channel_filter_idle_cached(0);
    int i;

    for (i = 0; i < num_samples; i++) {
        int32_t xL = loudness_highres_s16_to_24bit(sample_L[i]);
        int32_t xR = loudness_highres_s16_to_24bit(sample_R[i]);
        int32_t yL;
        int32_t yR;

        if (sample_L[i] == 0 && idle_L) {
            yL = 0;
        } else {
            yL = step(xL, stL, &loudness_highres_ch[0], runtime_left);
            idle_L = loudness_channel_biquad_is_idle(0)
                && loudness_highres_channel_is_idle(0);
        }
        
        if (sample_R[i] == 0 && loudness_channel_filter_is_idle(1)) {
            yR = 0;
        } else {
            yR = step(xR, stR, &loudness_highres_ch[1], runtime_right);
        }
        sample_L[i] = loudness_highres_y24_to_container(yL);
        sample_R[i] = loudness_highres_y24_to_container(yR);
    }
}

void loudness_highres_filter_16bit_stereo_packet_independent(
    S32 *sample_L, S32 *sample_R,
    biquad_state_fast_t *stL, biquad_state_fast_t *stR, U16 num_samples)
{
    const biquad_runtime_fast_t *runtime = loudness_highres_active_runtime();
    loudness_highres_filter_16bit_stereo_packet_with_runtime(
        sample_L, sample_R, stL, stR, &runtime[0], &runtime[1], num_samples);
}

void loudness_highres_filter_16bit_stereo_packet_shared(
    S32 *sample_L, S32 *sample_R,
    biquad_state_fast_t *stL, biquad_state_fast_t *stR, U16 num_samples)
{
    const biquad_runtime_fast_t *runtime = loudness_highres_active_runtime();
    loudness_highres_filter_16bit_stereo_packet_with_runtime(
        sample_L, sample_R, stL, stR, &runtime[0], &runtime[0], num_samples);
}

void loudness_highres_filter_16bit_stereo_packet(
    S32 *sample_L, S32 *sample_R, U16 num_samples)
{
    biquad_state_fast_t *stL = loudness_fast_biquad_state(0);
    biquad_state_fast_t *stR = loudness_fast_biquad_state(1);

    if (loudness_highres_share_master_row()) {
        loudness_highres_filter_16bit_stereo_packet_shared(
            sample_L, sample_R, stL, stR, num_samples);
    } else {
        loudness_highres_filter_16bit_stereo_packet_independent(
            sample_L, sample_R, stL, stR, num_samples);
    }
    loudness_fast_refresh_idle_cache();
}

#ifdef BUILD_TESTING
void loudness_highres_test_load_active_quotients(int equalizer_step)
{
    const biquad_quotients_fast_t *table =
        loudness_highres_resolve_scaled_quotient_table(loudness_highres_frequency_hz);
    loudness_fast_set_active_equalizer_step_table(table);
    loudness_highres_fill_staging_quotients_fast(equalizer_step, equalizer_step);
    loudness_fast_commit_staged_quotients();
    loudness_highres_staging_commit();
}

void loudness_highres_test_filter_16bit_stereo_packet_fullrate(S32 *sample_L,
    S32 *sample_R, biquad_state_fast_t *stL, biquad_state_fast_t *stR,
    const biquad_runtime_fast_t *runtime, U16 num_samples)
{
    int i;
    for (i = 0; i < num_samples; i++) {
        int32_t xL = loudness_highres_s16_to_24bit(sample_L[i]);
        int32_t xR = loudness_highres_s16_to_24bit(sample_R[i]);
        int32_t yL = loudness_fast_biquad1_step_runtime(xL, stL, &runtime[0]);
        int32_t yR = loudness_fast_biquad1_step_runtime(xR, stR, &runtime[1]);

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
