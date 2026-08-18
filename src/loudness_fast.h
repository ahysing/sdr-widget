/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_fast.h — FAST biquad loudness filter API
 */

#ifndef LOUDNESS_FAST_H_
#define LOUDNESS_FAST_H_

#include <stdint.h>
#include "compiler.h"

typedef struct {
    int32_t w1;  /* transposed DF-II delay state 1 */
    int32_t w2;  /* transposed DF-II delay state 2 */
} biquad_state_fast_t;

typedef struct {
    int32_t a1;
    int32_t a2;
    int32_t b0;
    int32_t b1;
    int32_t b2;
} biquad_quotients_fast_t;

int32_t loudness_fast_24bit(int32_t sample);
int32_t biquad_step_fast_32bit(int32_t sample, biquad_state_fast_t* biquad_states,
    const biquad_quotients_fast_t* q);
S32 loudness_filter_16bit_container(S32 sample);
S32 loudness_filter_24bit_container(S32 sample);
void loudness_change_frequency_fast(uint32_t frequency);

#ifdef BUILD_TESTING
void loudness_test_load_active_quotients_fast(int equalizer_step);
void loudness_test_get_fast_section(int section,
    biquad_state_fast_t *state, biquad_quotients_fast_t *quotients,
    int32_t input_history[2], int32_t output_history[2]);
#endif

#endif /* LOUDNESS_FAST_H_ */
