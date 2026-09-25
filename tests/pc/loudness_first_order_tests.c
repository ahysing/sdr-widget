#include "fff.h"
#include "loudness_first_order.h"
#include "loudness.h"
#include "usb_specific_request.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <math.h>

DEFINE_FFF_GLOBALS;

/* Mock hardware / USB state needed by loudness.c and loudness_inferred_gain.c */
S_freq current_freq = { .frequency = 48000 };
volatile Bool freq_changed = FALSE;
volatile U8 usb_alternate_setting_out = 1;
S16 spk_vol_usb_L = 0, spk_vol_usb_R = 0;
volatile U8 spk_bit_resolution = 24;

#define SINE_TEST_SAMPLE_COUNT        48000
#define SINE_TEST_SETTLE_SAMPLES      2000
#define SINE_TEST_AMPLITUDE           2097152
#define SINE_TEST_FREQUENCY_HZ        10000
#define SINE_TEST_GAIN_TOLERANCE_DB   0.05
#define SINE_TEST_PI                  3.14159265358979323846

typedef struct {
    int phon;
    int equalizer_step;
    double expected_gain_44100_db;
    double expected_gain_48000_db;
} lower_tremble_test_case_t;

/* High-precision table generated from create1loudnessvolume.py */
static const lower_tremble_test_case_t lower_tremble_test_case[] = {
    { 55,  40,  0.00000000,  0.00000000 },
    { 57,  44,  0.00000000,  0.00000000 },
    { 59,  48,  0.00000000,  0.00000000 },
    { 61,  52,  0.00000000,  0.00000000 },
    { 63,  56,  0.00000000,  0.00000000 },
    { 65,  60,  0.00000000,  0.00000000 },
    { 67,  64,  0.00000000,  0.00000000 },
    { 69,  68,  0.00000000,  0.00000000 },
    { 71,  72,  0.00000000,  0.00000000 },
    { 73,  76,  0.00000000,  0.00000000 },
    { 75,  80,  0.00000000,  0.00000000 },
    { 77,  84,  0.00000000,  0.00000000 },
    { 80,  90,  0.00000000,  0.00000000 },
    { 85, 100, -0.56384100, -0.56011700 },
    { 90, 110, -1.10320600, -1.09341400 },
};

/* External table declarations from src/loudness_first_order.c */
extern const biquad_first_order_quotients_t highshelf_no_volume_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS];
extern const biquad_first_order_quotients_t highshelf_no_volume_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS];

static const biquad_first_order_quotients_t *get_highshelf_quotients(
    uint32_t sample_rate_hz, int equalizer_step)
{
    assert(equalizer_step >= 0 && equalizer_step < LOUDNESS_NUM_EQUALIZER_STEPS);
    if (sample_rate_hz == 44100) {
        return &highshelf_no_volume_44100hz[equalizer_step];
    } else {
        return &highshelf_no_volume_48000hz[equalizer_step];
    }
}

static double measure_highshelf_gain_db(
    uint32_t sample_rate_hz, int equalizer_step, double test_freq_hz)
{
    double input_sum_squares = 0.0;
    double output_sum_squares = 0.0;
    double input_rms;
    double output_rms;
    int i;
    biquad_first_order_state_t state = { 0 };
    const biquad_first_order_quotients_t *quotients =
        get_highshelf_quotients(sample_rate_hz, equalizer_step);

    for (i = 0; i < SINE_TEST_SAMPLE_COUNT; i++) {
        double phase = 2.0 * SINE_TEST_PI * test_freq_hz *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        int32_t output_24 = loudness_highshelf(input_24, &state, quotients);

        if (i >= SINE_TEST_SETTLE_SAMPLES) {
            input_sum_squares += (double)input_24 * (double)input_24;
            output_sum_squares += (double)output_24 * (double)output_24;
        }
    }

    input_rms = sqrt(input_sum_squares /
        (SINE_TEST_SAMPLE_COUNT - SINE_TEST_SETTLE_SAMPLES));
    output_rms = sqrt(output_sum_squares /
        (SINE_TEST_SAMPLE_COUNT - SINE_TEST_SETTLE_SAMPLES));
    return 20.0 * log10(output_rms / input_rms);
}

static void assert_lower_tremble_case(size_t case_index)
{
    const lower_tremble_test_case_t *test_case =
        &lower_tremble_test_case[case_index];
    double gain_44100 = measure_highshelf_gain_db(
        44100, test_case->equalizer_step, SINE_TEST_FREQUENCY_HZ);
    double gain_48000 = measure_highshelf_gain_db(
        48000, test_case->equalizer_step, SINE_TEST_FREQUENCY_HZ);

    printf("  %d phon (step %d): 44.1 kHz %.3f dB, 48 kHz %.3f dB\n",
        test_case->phon, test_case->equalizer_step, gain_44100, gain_48000);
    fflush(stdout);

    assert(fabs(gain_44100 - test_case->expected_gain_44100_db) <
        SINE_TEST_GAIN_TOLERANCE_DB);
    assert(fabs(gain_48000 - test_case->expected_gain_48000_db) <
        SINE_TEST_GAIN_TOLERANCE_DB);
}

#define DEFINE_LOWER_TREMBLE_TEST(PHON, INDEX) \
    void test_lower_tremble_##PHON##phon_magnitude(void) \
    { \
        printf("Running test_lower_tremble_%dphon_magnitude...\n", PHON); \
        assert_lower_tremble_case(INDEX); \
    }

DEFINE_LOWER_TREMBLE_TEST(55, 0)
DEFINE_LOWER_TREMBLE_TEST(57, 1)
DEFINE_LOWER_TREMBLE_TEST(59, 2)
DEFINE_LOWER_TREMBLE_TEST(61, 3)
DEFINE_LOWER_TREMBLE_TEST(63, 4)
DEFINE_LOWER_TREMBLE_TEST(65, 5)
DEFINE_LOWER_TREMBLE_TEST(67, 6)
DEFINE_LOWER_TREMBLE_TEST(69, 7)
DEFINE_LOWER_TREMBLE_TEST(71, 8)
DEFINE_LOWER_TREMBLE_TEST(73, 9)
DEFINE_LOWER_TREMBLE_TEST(75, 10)
DEFINE_LOWER_TREMBLE_TEST(77, 11)
DEFINE_LOWER_TREMBLE_TEST(80, 12)
DEFINE_LOWER_TREMBLE_TEST(85, 13)
DEFINE_LOWER_TREMBLE_TEST(90, 14)

void test_lower_tremble_is_monotonic(void)
{
    double previous_44100 = 1000.0;
    double previous_48000 = 1000.0;
    size_t i;

    printf("Running test_lower_tremble_is_monotonic...\n");
    for (i = 0; i < sizeof(lower_tremble_test_case) /
        sizeof(lower_tremble_test_case[0]); i++) {
        double gain_44100 = measure_highshelf_gain_db(
            44100, lower_tremble_test_case[i].equalizer_step, SINE_TEST_FREQUENCY_HZ);
        double gain_48000 = measure_highshelf_gain_db(
            48000, lower_tremble_test_case[i].equalizer_step, SINE_TEST_FREQUENCY_HZ);

        /* Treble gain should be non-increasing (gain becomes more negative as phon increases) */
        assert(gain_44100 <= previous_44100 + 1e-4);
        assert(gain_48000 <= previous_48000 + 1e-4);
        previous_44100 = gain_44100;
        previous_48000 = gain_48000;
    }
}

int main(void)
{
    test_lower_tremble_55phon_magnitude();
    test_lower_tremble_57phon_magnitude();
    test_lower_tremble_59phon_magnitude();
    test_lower_tremble_61phon_magnitude();
    test_lower_tremble_63phon_magnitude();
    test_lower_tremble_65phon_magnitude();
    test_lower_tremble_67phon_magnitude();
    test_lower_tremble_69phon_magnitude();
    test_lower_tremble_71phon_magnitude();
    test_lower_tremble_73phon_magnitude();
    test_lower_tremble_75phon_magnitude();
    test_lower_tremble_77phon_magnitude();
    test_lower_tremble_80phon_magnitude();
    test_lower_tremble_85phon_magnitude();
    test_lower_tremble_90phon_magnitude();

    test_lower_tremble_is_monotonic();

    printf("All first-order high-shelf tests passed!\n");
    return 0;
}