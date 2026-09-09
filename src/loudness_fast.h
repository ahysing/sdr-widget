/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_fast.h — FAST biquad loudness filter API
 */

#ifndef LOUDNESS_FAST_H_
#define LOUDNESS_FAST_H_

#include <stdint.h>
#include "compiler.h"

#define LOUDNESS_CHANNELS     2
typedef struct {
    int32_t w1;  /* canonical DF-II delay state 1, stored with M-bit headroom */
} biquad_state_t;

typedef struct {
    int32_t a1;
    int32_t b0;
    int32_t b1;
} biquad_coefficients_t;

void loudness_filter_16bit_stereo_packet(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples);
void loudness_filter_24bit_stereo_packet(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples);
void loudness_process_uac2_stereo_packet(
    S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples,
    Bool is_16bit_container);
void loudness_change_frequency_fast(uint32_t frequency);
const biquad_coefficients_t *loudness_lowshelf_active_coefficients(void);

#ifdef BUILD_TESTING
int32_t loudness_lowshelf(int32_t x_n, biquad_state_t *st, const biquad_coefficients_t *q);

#define LOUDNESS_TEST_IDLE_LOWSHELF_LEFT   (1u << 0)
#define LOUDNESS_TEST_IDLE_LOWSHELF_RIGHT  (1u << 1)
#define LOUDNESS_TEST_IDLE_HIGHSHELF_LEFT  (1u << 2)
#define LOUDNESS_TEST_IDLE_HIGHSHELF_RIGHT (1u << 3)
#define LOUDNESS_TEST_FILTER_IDLE_LEFT \
    (LOUDNESS_TEST_IDLE_LOWSHELF_LEFT | LOUDNESS_TEST_IDLE_HIGHSHELF_LEFT)
#define LOUDNESS_TEST_FILTER_IDLE_RIGHT \
    (LOUDNESS_TEST_IDLE_LOWSHELF_RIGHT | LOUDNESS_TEST_IDLE_HIGHSHELF_RIGHT)
#define LOUDNESS_TEST_FILTER_IDLE_ALL \
    (LOUDNESS_TEST_FILTER_IDLE_LEFT | LOUDNESS_TEST_FILTER_IDLE_RIGHT)

uint8_t loudness_test_get_filter_idle_mask(void);

static inline S32 loudness_test_filter_24bit_left(S32 sample)
{
    S32 packet_L = sample;
    S32 packet_R = 0;
    loudness_filter_24bit_stereo_packet(&packet_L, &packet_R, 1);
    return packet_L;
}

static inline S32 loudness_test_filter_16bit_left(S32 sample)
{
    S32 packet_L = sample;
    S32 packet_R = 0;
    loudness_filter_16bit_stereo_packet(&packet_L, &packet_R, 1);
    return packet_L;
}

static inline Bool loudness_test_left_filter_is_idle(void)
{
    uint8_t mask = loudness_test_get_filter_idle_mask();
    return (mask & LOUDNESS_TEST_FILTER_IDLE_LEFT) == LOUDNESS_TEST_FILTER_IDLE_LEFT;
}

static inline Bool loudness_test_right_filter_is_idle(void)
{
    uint8_t mask = loudness_test_get_filter_idle_mask();
    return (mask & LOUDNESS_TEST_FILTER_IDLE_RIGHT) == LOUDNESS_TEST_FILTER_IDLE_RIGHT;
}

static inline Bool loudness_test_lowshelf_is_active(void)
{
    uint8_t mask = loudness_test_get_filter_idle_mask();
    return (mask & (LOUDNESS_TEST_IDLE_LOWSHELF_LEFT | LOUDNESS_TEST_IDLE_LOWSHELF_RIGHT)) !=
        (LOUDNESS_TEST_IDLE_LOWSHELF_LEFT | LOUDNESS_TEST_IDLE_LOWSHELF_RIGHT);
}

void loudness_test_load_active_coefficients(int equalizer_step);
void loudness_test_load_quotient_table_fast(
    const biquad_coefficients_t *table, int equalizer_step);
void loudness_test_get_fast_channel(int channel, biquad_state_t *state, biquad_coefficients_t *coefficients);
void loudness_test_set_fast_channel(int channel, const biquad_state_t *state);
#endif

#define FMA_24BIT(A, B, C) ((S64)(A) + ((S64)(S32)(B) * (S64)(S32)(C)))
#define FMS_24BIT(A, B, C) ((S64)(A) - ((S64)(S32)(B) * (S64)(S32)(C)))

#endif /* LOUDNESS_FAST_H_ */
