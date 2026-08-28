/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_highres.h — delta-interpolated biquad path for high sample rates
 */

#ifndef LOUDNESS_HIGHRES_H_
#define LOUDNESS_HIGHRES_H_

#include "loudness_fast.h"
#include "compiler.h"

Bool loudness_highres_applies(uint32_t sample_rate_hz);

const biquad_quotients_fast_t *loudness_highres_halfrate_quotients(
    uint32_t sample_rate_hz, int equalizer_step,
    const biquad_quotients_fast_t *quotients_44100hz,
    const biquad_quotients_fast_t *quotients_48000hz);

void loudness_highres_staging_fill_halfrate(
    const biquad_quotients_fast_t *halfrate_src_left,
    const biquad_quotients_fast_t *halfrate_src_right,
    const biquad_runtime_fast_t *main_runtime);

void loudness_highres_staging_commit(void);

void loudness_highres_set_stride(uint32_t sample_rate_hz);
void loudness_highres_reset_states(void);
Bool loudness_highres_channel_is_idle(int channel);

void loudness_highres_filter_16bit_stereo_packet(S32 *sample_L, S32 *sample_R,
    biquad_state_fast_t *stL, biquad_state_fast_t *stR, U16 num_samples);

#ifdef BUILD_TESTING
void loudness_highres_test_filter_16bit_stereo_packet_fullrate(S32 *sample_L,
    S32 *sample_R, biquad_state_fast_t *stL, biquad_state_fast_t *stR,
    const biquad_runtime_fast_t *runtime, U16 num_samples);
const loudness_highres_channel_state_t *loudness_highres_test_channel_state(int channel);
#endif

#endif /* LOUDNESS_HIGHRES_H_ */
