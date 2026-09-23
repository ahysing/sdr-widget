/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_highres.h — delta-interpolated biquad path for high sample rates
 */

#ifndef LOUDNESS_HIGHRES_H_
#define LOUDNESS_HIGHRES_H_

#include "loudness_fast.h"
#include "compiler.h"

typedef struct {
    int32_t y_prev_biquad;
    int32_t y_derivative;
    int32_t y_current_est;
    uint8_t sample_counter;
} loudness_highres_channel_state_t;

typedef int32_t (*loudness_highres_biquad1_step_stride_fn)(int32_t x_24,
    biquad_state_fast_t *st, loudness_highres_channel_state_t *ch,
    const biquad_runtime_fast_t *rt);

Bool loudness_highres_applies(uint32_t sample_rate_hz);

void loudness_highres_current_stereo_db_spl_x10(
    int32_t *db_spl_left_x10, int32_t *db_spl_right_x10);
void loudness_highres_change_frequency(uint32_t frequency);
void loudness_highres_filter_16bit_stereo_packet(
    S32 *sample_L, S32 *sample_R, U16 num_samples);

const biquad_quotients_fast_t *loudness_highres_halfrate_quotients(
    uint32_t sample_rate_hz, int equalizer_step,
    const biquad_quotients_fast_t *quotients_44100hz,
    const biquad_quotients_fast_t *quotients_48000hz);

void loudness_highres_staging_fill_halfrate_independent(
    const biquad_quotients_fast_t *halfrate_src_left,
    const biquad_quotients_fast_t *halfrate_src_right,
    const biquad_runtime_fast_t *main_runtime);
void loudness_highres_staging_fill_halfrate_shared(
    const biquad_quotients_fast_t *halfrate_src,
    const biquad_runtime_fast_t *main_runtime);

void loudness_highres_staging_commit(void);

void loudness_highres_set_stride(uint32_t sample_rate_hz);
void loudness_highres_reset_states(void);
Bool loudness_highres_channel_is_idle(int channel);

void loudness_highres_filter_16bit_stereo_packet_independent(
    S32 *sample_L, S32 *sample_R,
    biquad_state_fast_t *stL, biquad_state_fast_t *stR, U16 num_samples);
void loudness_highres_filter_16bit_stereo_packet_shared(
    S32 *sample_L, S32 *sample_R,
    biquad_state_fast_t *stL, biquad_state_fast_t *stR, U16 num_samples);

#ifdef BUILD_TESTING
void loudness_highres_test_load_active_quotients(int equalizer_step);
void loudness_highres_test_filter_16bit_stereo_packet_fullrate(S32 *sample_L,
    S32 *sample_R, biquad_state_fast_t *stL, biquad_state_fast_t *stR,
    const biquad_runtime_fast_t *runtime, U16 num_samples);
const loudness_highres_channel_state_t *loudness_highres_test_channel_state(int channel);
#endif

#endif /* LOUDNESS_HIGHRES_H_ */
