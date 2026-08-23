/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_fast.h — FAST biquad loudness filter API
 */

#ifndef LOUDNESS_FAST_H_
#define LOUDNESS_FAST_H_

#include <stdint.h>
#include "compiler.h"

#define LOUDNESS_FAST_FILTERS 1

typedef struct {
    int32_t w1;  /* canonical DF-II delay state 1, stored with M-bit headroom */
    int32_t w2;  /* canonical DF-II delay state 2, stored with M-bit headroom */
} biquad_state_fast_t;

typedef struct {
    int32_t y_prev_biquad;
    int32_t y_derivative;
    int32_t y_current_est;
    uint8_t sample_counter;
} loudness_highres_channel_state_t;

typedef struct {
    int32_t a1;
    int32_t a2;
    int32_t b0;
    int32_t b1;
    int32_t b2;
} biquad_quotients_fast_t;

/*
 * Hot-path coefficients: same Q29 b0/a1/a2 as quotients; b1/b2/a1/a2 products
 * use stored headroom states w' so only two <<M shifts remain (pole sum, zero sum).
 */
typedef struct {
    int32_t b0;
    int32_t b1;
    int32_t b2;
    int32_t a1;
    int32_t a2;
} biquad_runtime_fast_t;

typedef int32_t (*loudness_fast_biquad1_step_runtime_stride_fn)(int32_t x_24,
    biquad_state_fast_t *st, loudness_highres_channel_state_t *ch,
    const biquad_runtime_fast_t *rt);

int32_t loudness_fast_biquad1_step_runtime(int32_t x_n, biquad_state_fast_t *st,
    const biquad_runtime_fast_t *rt);
int32_t loudness_fast_biquad1_step_runtime_stride2(int32_t x_24,
    biquad_state_fast_t *st, loudness_highres_channel_state_t *ch,
    const biquad_runtime_fast_t *rt);
int32_t loudness_fast_biquad1_step_runtime_stride4(int32_t x_24,
    biquad_state_fast_t *st, loudness_highres_channel_state_t *ch,
    const biquad_runtime_fast_t *rt);
void loudness_fast_biquad1_unity_advance_state_24bit(int32_t x_n,
    biquad_state_fast_t *st);
int32_t loudness_fast_24bit(int32_t sample);
int32_t biquad_step_fast_32bit(int32_t sample, biquad_state_fast_t* biquad_states,
    const biquad_quotients_fast_t* q);
S32 loudness_filter_16bit_container(S32 sample);
S32 loudness_filter_24bit_container(S32 sample);
void loudness_filter_16bit_stereo_packet(S32 *sample_L, S32 *sample_R, U16 num_samples);
Bool loudness_fast_is_unity_step(void);
void loudness_change_frequency_fast(uint32_t frequency);
Bool loudness_filter_is_active();

#ifdef BUILD_TESTING
void loudness_test_filter_16bit_stereo_packet_hires_fullrate(S32 *sample_L,
    S32 *sample_R, U16 num_samples);
void loudness_test_load_active_quotients_fast(int equalizer_step);
void loudness_test_get_fast_section(int section,
    biquad_state_fast_t *state, biquad_quotients_fast_t *quotients);
void loudness_test_set_fast_section(int section,
    const biquad_state_fast_t *state);
int loudness_fast_run_golden_selftest(void);
#endif

#endif /* LOUDNESS_FAST_H_ */
