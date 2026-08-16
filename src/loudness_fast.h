/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_fast.h — FAST biquad loudness filter API
 */

#ifndef LOUDNESS_FAST_H_
#define LOUDNESS_FAST_H_

#include <stdint.h>
#include "compiler.h"

typedef struct {
    int32_t w1;  /* w[n-1] */
    int32_t w2;  /* w[n-2] */
} biquad_state_fast_t;

typedef struct {
    int32_t a1;
    int32_t a2;
    int32_t b0;
    int32_t b1;
    int32_t b2;
} biquad_quotients_fast_t;

int64_t loudness_fast_24bit(int32_t sample);
int32_t biquad_step_fast_32bit(int32_t sample, biquad_state_fast_t* biquad_states,
    const biquad_quotients_fast_t* q);
S32 loudness_filter_16bit_container(S32 sample);
S32 loudness_filter_24bit_container(S32 sample);
void loudness_change_frequency_fast(uint32_t frequency);

#ifdef BUILD_TESTING
void loudness_test_load_active_quotients_fast(int equalizer_step);
#endif

#endif /* LOUDNESS_FAST_H_ */
