/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_precise.h — PRECISE biquad loudness filter API
 */

#ifndef LOUDNESS_PRECISE_H_
#define LOUDNESS_PRECISE_H_

#include <stdint.h>
#include "compiler.h"

typedef struct {
    int64_t w1;  /* w[n-1] */
    int64_t w2;  /* w[n-2] */
} biquad_state_precise_t;

typedef struct {
    int64_t a1;
    int64_t a2;
    int64_t b0;
    int64_t b1;
    int64_t b2;
} biquad_quotients_precise_t;

int64_t loudness_precise_24bit(int64_t sample);
int64_t biquad_step_precise_24bit(int64_t sample, biquad_state_precise_t* biquad_states,
    const biquad_quotients_precise_t* q);
void loudness_change_frequency_precise(uint32_t frequency);
Bool loudness_filter_is_active();

#endif /* LOUDNESS_PRECISE_H_ */
