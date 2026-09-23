#ifndef LOUDNESS_FIRST_ORDER_H
#define LOUDNESS_FIRST_ORDER_H

#include <stdint.h>

typedef struct {
    int32_t w1;  /* canonical DF-II delay state 1, stored with M-bit headroom */
} biquad_first_order_state_t;

typedef struct {
    int32_t a1;
    int32_t b0;
    int32_t b1;
} biquad_first_order_quotients_t;

#endif