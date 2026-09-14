#ifndef LOUDNESS_FIRST_ORDER_H
#define LOUDNESS_FIRST_ORDER_H

#include <stdint.h>
#include "compiler.h"
#include "loudness.h"
#include "loudness_fast.h"

#ifndef LOUDNESS_Q28_ONE
#define LOUDNESS_Q28_ONE ((int32_t)1 << 28)
#endif

typedef struct {
    int32_t w1;  /* canonical DF-II delay state 1, stored with M-bit headroom */
} biquad_first_order_state_t;

typedef struct {
    int32_t a1;
    int32_t b0;
    int32_t b1;
} biquad_first_order_coefficients_t;

extern biquad_first_order_state_t highshelf_states[LOUDNESS_CHANNELS];
extern const biquad_first_order_coefficients_t highshelf_no_volume_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS];
extern const biquad_first_order_coefficients_t highshelf_no_volume_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS];
#ifdef BUILD_TESTING
int32_t loudness_highshelf(int32_t x_n, biquad_first_order_state_t *st, const biquad_first_order_coefficients_t *rt);
#endif
const biquad_first_order_coefficients_t *active_highshelf_LUT_for_frequency(uint32_t frequency);
const biquad_first_order_coefficients_t *loudness_highshelf_active_coefficients(void);
Bool loudness_highshelf_biquad_is_idle(int channel);
void loudness_highshelf_reset_states(void);

#endif /* LOUDNESS_FIRST_ORDER_H */