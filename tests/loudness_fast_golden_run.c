#include "loudness.h"
#include "loudness_fast.h"
#include "loudness_fast_golden_vectors.h"

int loudness_fast_run_golden_selftest(void)
{
    int i;
    int32_t output;

    loudness_init();
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(LOUDNESS_NEUTRAL_STEP);
    for (i = 0; i < LOUDNESS_GOLDEN_UNITY_CASE_COUNT; i++) {
        loudness_fast_reset_states();
        output = loudness_fast_24bit(loudness_golden_unity_inputs[i]);
        if (output != loudness_golden_unity_outputs[i]) {
            return i + 1;
        }
    }

    loudness_change_frequency_fast(48000);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(0);
    for (i = 0; i < LOUDNESS_GOLDEN_STEP0_CASE_COUNT; i++) {
        output = loudness_fast_24bit(loudness_golden_step0_inputs[i]);
        if (output != loudness_golden_step0_outputs[i]) {
            return 100 + i;
        }
    }

    return 0;
}
