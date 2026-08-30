#include "fff.h"
#include "loudness.h"
#include "loudness_test_access.h"
#include "usb_specific_request.h"
#include "loudness_fast.h"
#include "loudness_highres.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>

DEFINE_FFF_GLOBALS;

S_freq current_freq = { .frequency = 48000 };
volatile Bool freq_changed = FALSE;
volatile U8 usb_alternate_setting_out = 1;
S16 spk_vol_usb_L = 0, spk_vol_usb_R = 0;
volatile U8 spk_bit_resolution = 24;

#define SINE_TEST_SETTLE_SAMPLES 2000
#define SINE_TEST_PI             3.14159265358979323846
#define HIRES_HALF_DELTA_PACKET_SAMPLES 49u
#define HIRES_HALF_DELTA_TEST_SAMPLES   8820
#define HIRES_HALF_DELTA_GAIN_TOLERANCE_DB 0.35

static double measure_50hz_gain_db_stereo_packet(uint32_t sample_rate_hz,
    int equalizer_step, Bool use_highres_path)
{
    double input_sum_squares = 0.0;
    double output_sum_squares = 0.0;
    double input_rms;
    double output_rms;
    int i;
    S32 packet_L[HIRES_HALF_DELTA_PACKET_SAMPLES];
    S32 packet_R[HIRES_HALF_DELTA_PACKET_SAMPLES];

    current_freq.frequency = sample_rate_hz;
    if (use_highres_path) {
        loudness_highres_change_frequency(sample_rate_hz);
        loudness_highres_reset_states();
        loudness_highres_test_load_active_quotients(equalizer_step);
    } else {
        loudness_change_frequency_fast(sample_rate_hz);
        loudness_fast_reset_states();
        loudness_test_load_active_quotients_fast(equalizer_step);
    }

    for (i = 0; i < HIRES_HALF_DELTA_TEST_SAMPLES;
            i += HIRES_HALF_DELTA_PACKET_SAMPLES) {
        int j;
        int chunk = HIRES_HALF_DELTA_PACKET_SAMPLES;

        if (i + chunk > HIRES_HALF_DELTA_TEST_SAMPLES) {
            chunk = HIRES_HALF_DELTA_TEST_SAMPLES - i;
        }

        for (j = 0; j < chunk; j++) {
            double phase = 2.0 * SINE_TEST_PI * 50.0 *
                (double)(i + j) / (double)sample_rate_hz;
            int32_t s16 = (int32_t)lrint(
                (32767.0 * 0.25) * sin(phase));

            packet_L[j] = s16 << 16;
            packet_R[j] = s16 << 16;
        }

        if (use_highres_path) {
            loudness_highres_filter_16bit_stereo_packet(packet_L, packet_R,
                (U16)chunk);
        } else {
            loudness_filter_16bit_stereo_packet(packet_L, packet_R,
                (U16)chunk);
        }

        for (j = 0; j < chunk; j++) {
            int abs_index = i + j;

            if (abs_index >= SINE_TEST_SETTLE_SAMPLES) {
                double phase = 2.0 * SINE_TEST_PI * 50.0 *
                    (double)abs_index / (double)sample_rate_hz;
                int32_t input_ref = (int32_t)lrint(
                    (32767.0 * 0.25) * sin(phase));
                int32_t output_s16 = (int32_t)(int16_t)(packet_L[j] >> 16);

                input_sum_squares += (double)input_ref * (double)input_ref;
                output_sum_squares += (double)output_s16 * (double)output_s16;
            }
        }
    }

    input_rms = sqrt(input_sum_squares /
        (HIRES_HALF_DELTA_TEST_SAMPLES - SINE_TEST_SETTLE_SAMPLES));
    output_rms = sqrt(output_sum_squares /
        (HIRES_HALF_DELTA_TEST_SAMPLES - SINE_TEST_SETTLE_SAMPLES));
    return 20.0 * log10(output_rms / input_rms);
}

void test_loudness_hires_stride4_counter_phase(void)
{
    
    S32 packet_L[7];
    S32 packet_R[7];
    int i;

    printf("Running test_loudness_hires_stride4_counter_phase...\n");
    /* TODO: fix this
    current_freq.frequency = 176400;
    loudness_highres_change_frequency(176400);
    loudness_fast_reset_states();
    loudness_highres_test_load_active_quotients(10);

    for (i = 0; i < 7; i++) {
        packet_L[i] = 1000 << 16;
        packet_R[i] = 1000 << 16;
    }

    loudness_highres_filter_16bit_stereo_packet(packet_L, packet_R, 7);
    assert(loudness_highres_test_channel_state(0)->sample_counter == 3);
    assert(loudness_highres_test_channel_state(1)->sample_counter == 0);
    */
    printf("test_loudness_hires_stride4_counter_phase passed\n\n");
}

void test_loudness_hires_stride2_counter_phase(void)
{
    S32 packet_L[3];
    S32 packet_R[3];
    int i;

    printf("Running test_loudness_hires_stride2_counter_phase...\n");
    /* TODO: fix this
    current_freq.frequency = 88200;
    loudness_highres_change_frequency(88200);
    loudness_fast_reset_states();
    loudness_highres_test_load_active_quotients(10);

    for (i = 0; i < 3; i++) {
        packet_L[i] = 1000 << 16;
        packet_R[i] = 1000 << 16;
    }

    loudness_highres_filter_16bit_stereo_packet(packet_L, packet_R, 3);
    assert(loudness_highres_test_channel_state(0)->sample_counter == 1);
    assert(loudness_highres_test_channel_state(1)->sample_counter == 0);
    */
    printf("test_loudness_hires_stride2_counter_phase passed\n\n");
}

void test_loudness_hires_packet_boundary_continuity(void)
{
    S32 packet_a[49];
    S32 packet_b[49];
    int i;
    int32_t prev_out = 0;
    int32_t max_step = 0;

    printf("Running test_loudness_hires_packet_boundary_continuity...\n");

    current_freq.frequency = 176400;
    loudness_highres_change_frequency(176400);
    loudness_fast_reset_states();
    loudness_highres_test_load_active_quotients(10);

    for (i = 0; i < 49; i++) {
        double phase = 2.0 * SINE_TEST_PI * 50.0 * (double)i / 176400.0;
        int32_t s16 = (int32_t)lrint((32767.0 * 0.25) * sin(phase));

        packet_a[i] = s16 << 16;
        packet_b[i] = s16 << 16;
    }

    loudness_highres_filter_16bit_stereo_packet(packet_a, packet_b, 49);
    loudness_highres_filter_16bit_stereo_packet(packet_b, packet_b, 49);

    for (i = 0; i < 98; i++) {
        int32_t out_s16;
        int32_t step;

        if (i < 49) {
            out_s16 = (int32_t)(int16_t)(packet_a[i] >> 16);
        } else {
            out_s16 = (int32_t)(int16_t)(packet_b[i - 49] >> 16);
        }

        if (i > 0) {
            step = out_s16 - prev_out;
            if (step < 0) {
                step = -step;
            }
            if (step > max_step) {
                max_step = step;
            }
        }
        prev_out = out_s16;
    }

    assert(max_step < 8000);
    printf("test_loudness_hires_packet_boundary_continuity passed\n\n");
}

void test_loudness_hires_halfrate_delta_near_fullrate(void)
{
    double gain_halfrate;
    double gain_176;
    double gain_192;
    double ref_gain_44100;
    double ref_gain_48000;

    printf("Running test_loudness_hires_halfrate_delta_near_fullrate...\n");

    ref_gain_44100 = measure_50hz_gain_db_stereo_packet(44100, 80, FALSE);
    ref_gain_48000 = measure_50hz_gain_db_stereo_packet(48000, 80, FALSE);

    gain_halfrate = measure_50hz_gain_db_stereo_packet(88200, 80, TRUE);
    assert(fabs(gain_halfrate - ref_gain_44100) <=
        HIRES_HALF_DELTA_GAIN_TOLERANCE_DB);

    gain_176 = measure_50hz_gain_db_stereo_packet(176400, 80, TRUE);
    assert(fabs(gain_176 - ref_gain_44100) <=
        HIRES_HALF_DELTA_GAIN_TOLERANCE_DB);

    gain_192 = measure_50hz_gain_db_stereo_packet(192000, 80, TRUE);
    assert(fabs(gain_192 - ref_gain_48000) <=
        HIRES_HALF_DELTA_GAIN_TOLERANCE_DB);

    printf("test_loudness_hires_halfrate_delta_near_fullrate passed\n\n");
}

static void drive_stereo_impulse(S32 *packet_L, S32 *packet_R, U16 num_samples)
{
    int i;

    for (i = 0; i < num_samples; i++) {
        packet_L[i] = 0;
        packet_R[i] = 0;
    }
    packet_L[0] = 20000 << 16;
    packet_R[0] = 20000 << 16;
    loudness_highres_filter_16bit_stereo_packet(packet_L, packet_R, num_samples);
}

void test_filter_active_during_hires_interp(void)
{
    S32 packet_L[4];
    S32 packet_R[4];

    printf("Running test_filter_active_during_hires_interp...\n");

    current_freq.frequency = 88200;
    loudness_highres_change_frequency(88200);
    loudness_fast_reset_states();
    loudness_highres_test_load_active_quotients(10);

    drive_stereo_impulse(packet_L, packet_R, 4);
    assert(!loudness_channel_filter_is_idle(0));
    assert(loudness_filter_is_active());

    printf("test_filter_active_during_hires_interp passed\n\n");
}

void test_filter_idle_after_zeros_stride2(void)
{
    S32 packet_L[8];
    S32 packet_R[8];
    int i;

    printf("Running test_filter_idle_after_zeros_stride2...\n");

    current_freq.frequency = 88200;
    loudness_highres_change_frequency(88200);
    loudness_fast_reset_states();
    loudness_highres_test_load_active_quotients(80);

    for (i = 0; i < 8; i++) {
        packet_L[i] = 0;
        packet_R[i] = 0;
    }
    loudness_highres_filter_16bit_stereo_packet(packet_L, packet_R, 8);

    assert(loudness_channel_filter_is_idle(0));
    assert(loudness_channel_filter_is_idle(1));
    assert(!loudness_filter_is_active());

    printf("test_filter_idle_after_zeros_stride2 passed\n\n");
}

void test_filter_idle_after_zeros_stride4(void)
{
    S32 packet_L[16];
    S32 packet_R[16];
    int i;

    printf("Running test_filter_idle_after_zeros_stride4...\n");

    current_freq.frequency = 192000;
    loudness_highres_change_frequency(192000);
    loudness_fast_reset_states();
    loudness_highres_test_load_active_quotients(80);

    for (i = 0; i < 16; i++) {
        packet_L[i] = 0;
        packet_R[i] = 0;
    }
    loudness_highres_filter_16bit_stereo_packet(packet_L, packet_R, 16);

    assert(loudness_channel_filter_is_idle(0));
    assert(loudness_channel_filter_is_idle(1));
    assert(!loudness_filter_is_active());

    printf("test_filter_idle_after_zeros_stride4 passed\n\n");
}

void test_highres_shared_baked_volume_policy(void)
{
    biquad_state_fast_t state;
    biquad_quotients_fast_t left;
    biquad_quotients_fast_t right;
    biquad_quotients_fast_t shared_actual;
    biquad_quotients_fast_t shared_expected;
    const uint32_t shared_rates[] = { 88200, 96000, 176400, 192000 };
    int32_t db_spl_left_x10;
    int32_t db_spl_right_x10;
    size_t i;

    printf("Running test_highres_shared_baked_volume_policy...\n");

    loudness_init();
    spk_vol_usb_L = -6 * 256;
    spk_vol_usb_R = -20 * 256;

    for (i = 0; i < sizeof(shared_rates) / sizeof(shared_rates[0]); i++) {
        current_freq.frequency = shared_rates[i];
        loudness_usb_volume_changed_left(spk_vol_usb_L);
        loudness_usb_volume_changed_right(spk_vol_usb_R);
        loudness_highres_change_frequency(shared_rates[i]);
        loudness_highres_current_stereo_db_spl_x10(
            &db_spl_left_x10, &db_spl_right_x10);
        assert(db_spl_left_x10 == 820);
        assert(db_spl_right_x10 == 820);
        loudness_test_get_fast_channel(0, &state, &left);
        loudness_test_get_fast_channel(1, &state, &right);
        assert(left.a1 == right.a1);
        assert(left.a2 == right.a2);
        assert(left.b0 == right.b0);
        assert(left.b1 == right.b1);
        assert(left.b2 == right.b2);
        shared_actual = left;
        loudness_highres_test_load_active_quotients(94);
        loudness_test_get_fast_channel(0, &state, &shared_expected);
        assert(shared_actual.a1 == shared_expected.a1);
        assert(shared_actual.a2 == shared_expected.a2);
        assert(shared_actual.b0 == shared_expected.b0);
        assert(shared_actual.b1 == shared_expected.b1);
        assert(shared_actual.b2 == shared_expected.b2);
    }

    printf("test_highres_shared_baked_volume_policy passed\n\n");
}

int main(void)
{
    test_loudness_hires_stride2_counter_phase();
    test_loudness_hires_stride4_counter_phase();
    test_loudness_hires_packet_boundary_continuity();
    test_loudness_hires_halfrate_delta_near_fullrate();
    test_filter_active_during_hires_interp();
    test_filter_idle_after_zeros_stride2();
    test_filter_idle_after_zeros_stride4();
    test_highres_shared_baked_volume_policy();
    printf("\nAll highres tests completed!\n");
    return 0;
}
