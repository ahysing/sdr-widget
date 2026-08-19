#include <stdio.h>
#include "loudness.h"
#include "loudness_fast.h"
#include "loudness_fast_golden_vectors.h"
#include "usb_specific_request.h"

S_freq current_freq = { .frequency = 48000 };
volatile Bool freq_changed = FALSE;
volatile U8 usb_alternate_setting_out = 0;

int main(void)
{
    int i;
    int32_t output;

    loudness_init();
    loudness_change_frequency_fast(48000);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(0);

    for (i = 0; i < LOUDNESS_GOLDEN_STEP0_CASE_COUNT; i++) {
        output = loudness_fast_24bit(loudness_golden_step0_inputs[i]);
        printf("%d, %d\n", loudness_golden_step0_inputs[i], output);
    }

    return 0;
}
