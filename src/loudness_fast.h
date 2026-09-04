/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_fast.h — FAST biquad loudness filter API
 */

#ifndef LOUDNESS_FAST_H_
#define LOUDNESS_FAST_H_

#include <stdint.h>
#include "compiler.h"

#define LOUDNESS_FAST_FILTERS 1
#define LOUDNESS_CHANNELS     2

typedef struct {
    int32_t w1;  /* canonical DF-II delay state 1, stored with M-bit headroom */
    int32_t w2;  /* canonical DF-II delay state 2, stored with M-bit headroom */
} biquad_state_fast_t;

typedef struct {
    int32_t a1;
    int32_t a2;
    int32_t b0;
    int32_t b1;
    int32_t b2;
} biquad_quotients_fast_t;

int32_t loudness_lowshelf(int32_t x_n, biquad_state_fast_t *st, const biquad_quotients_fast_t *q);
int32_t biquad_step_fast_32bit(int32_t sample, biquad_state_fast_t* biquad_states, const biquad_quotients_fast_t* q);
S32 loudness_filter_16bit_container(int channel, S32 sample);
S32 loudness_filter_24bit_container(int channel, S32 sample);
void loudness_filter_16bit_stereo_packet(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples);
void loudness_filter_24bit_stereo_packet(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples);
void loudness_change_frequency_fast(uint32_t frequency);
const biquad_quotients_fast_t *loudness_fast_channel_quotients(int channel);
const biquad_quotients_fast_t *loudness_lowshelf_quotients(int channel);
const biquad_quotients_fast_t *loudness_lowshelf_active_quotients(void);
Bool loudness_channel_biquad_is_idle(int channel);
Bool loudness_channel_filter_is_idle(int channel);
Bool loudness_lowshelf_is_active(void);
Bool loudness_filter_is_active(void);

#ifdef BUILD_TESTING
void loudness_test_load_active_quotients_fast(int equalizer_step);
void loudness_test_get_fast_channel(int channel, biquad_state_fast_t *state, biquad_quotients_fast_t *quotients);
void loudness_test_set_fast_channel(int channel, const biquad_state_fast_t *state);
#endif

#define FMA_24BIT(A, B, C) ((S64)(A) + ((S64)(S32)(B) * (S64)(S32)(C)))
#define FMS_24BIT(A, B, C) ((S64)(A) - ((S64)(S32)(B) * (S64)(S32)(C)))

#endif /* LOUDNESS_FAST_H_ */
