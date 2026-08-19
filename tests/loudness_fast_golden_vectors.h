#ifndef LOUDNESS_FAST_GOLDEN_VECTORS_H_
#define LOUDNESS_FAST_GOLDEN_VECTORS_H_

#include <stdint.h>

#define LOUDNESS_GOLDEN_UNITY_CASE_COUNT 3
#define LOUDNESS_GOLDEN_STEP0_CASE_COUNT 3

static const int32_t loudness_golden_unity_inputs[LOUDNESS_GOLDEN_UNITY_CASE_COUNT] = {
    0,
    123456,
    (int32_t)-8388608,
};

static const int32_t loudness_golden_unity_outputs[LOUDNESS_GOLDEN_UNITY_CASE_COUNT] = {
    0,
    123456,
    (int32_t)-8388608,
};

/* 48 kHz step 0 (55 phon), zero initial state, sequential inputs.
 * Runtime uses one biquad (low shelf); high shelf is not stepped on AVR32. */
static const int32_t loudness_golden_step0_inputs[LOUDNESS_GOLDEN_STEP0_CASE_COUNT] = {
    2097152,
    2097152,
    0,
};

static const int32_t loudness_golden_step0_outputs[LOUDNESS_GOLDEN_STEP0_CASE_COUNT] = {
    2131101,
    2198525,
    133869,
};

#endif /* LOUDNESS_FAST_GOLDEN_VECTORS_H_ */
