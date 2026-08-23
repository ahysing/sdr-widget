/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_highres.h — half-rate delta interpolation for high sample rates
 */

#ifndef LOUDNESS_HIGHRES_H_
#define LOUDNESS_HIGHRES_H_

#include "loudness_fast.h"
#include "compiler.h"

#define LOUDNESS_HIGHRES_QUOTIENT_SECTIONS 2

Bool loudness_highres_applies(uint32_t sample_rate_hz);

const biquad_quotients_fast_t *loudness_highres_halfrate_quotients(
    uint32_t sample_rate_hz, int equalizer_step,
    const biquad_quotients_fast_t (*quotients_44100hz)[LOUDNESS_HIGHRES_QUOTIENT_SECTIONS],
    const biquad_quotients_fast_t (*quotients_48000hz)[LOUDNESS_HIGHRES_QUOTIENT_SECTIONS]);

void loudness_highres_staging_fill_halfrate(
    const biquad_quotients_fast_t *halfrate_src,
    const biquad_runtime_fast_t *main_runtime);

void loudness_highres_staging_commit(void);

void loudness_highres_filter_16bit_stereo_packet(S32 *sample_L, S32 *sample_R,
    U16 num_samples, biquad_state_fast_t *st);

#ifdef BUILD_TESTING
void loudness_highres_test_filter_16bit_stereo_packet_fullrate(S32 *sample_L,
    S32 *sample_R, U16 num_samples, biquad_state_fast_t *st,
    const biquad_runtime_fast_t *runtime);
#endif

#endif /* LOUDNESS_HIGHRES_H_ */
