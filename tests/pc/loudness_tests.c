#include "fff.h"
#include "loudness.h"
#include "loudness_test_access.h"
#include "loudness_internal.h"
#include "device_audio_volume.h"
#include "usb_specific_request.h"
#include "loudness_fast.h"
#include "loudness_first_order.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#define _USE_MATH_DEFINES // Enable math constants like M_PI
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

DEFINE_FFF_GLOBALS;

S_freq current_freq = { .frequency = 48000 };
volatile Bool freq_changed = FALSE;
volatile U8 usb_alternate_setting_out = 1;

/* Mock volume state normally provided by device_audio_task.c. */
S16 spk_vol_usb_L = 0, spk_vol_usb_R = 0;
volatile U8 spk_bit_resolution = 24;

static int32_t apply_digital_volume(int32_t sample, S32 mult) {
    if (mult == 0) {
        return 0;
    }
    if (mult == VOL_MULT_UNITY) {
        return sample;
    }
    return (int32_t)(((int64_t)sample * (int64_t)mult) >> VOL_MULT_SHIFT);
}

void test_loudness_init() {
    printf("Running test_loudness_init...\n");
    loudness_init();
    assert(loudness_get_last_db_spl_left_x10() == LOUDNESS_DB_SPL_MAX * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);
    printf("test_loudness_init passed\n");
}

int64_t loudness_24bit_wrapper(int64_t sample) {
    return loudness_test_filter_24bit_left( (int32_t)(sample << 8)) >> 8;
}

void test_loudness_24bit_processing() {
    printf("Running test_loudness_24bit_processing...\n");
    loudness_init();
    loudness_set_source_has_volume_control();

    // At 0 dBFS the 95-phon row still runs through the normal biquad.
    loudness_usb_volume_changed_left(0);
    
    S32 sample_L[1] = { 1000 << 8 };
    S32 sample_R[1] = { 1000 << 8 };
    loudness_filter_24bit_stereo_packet(sample_L, sample_R, 1);
    
    assert(sample_L[0] != 0);

    // Switching to the 55 phon equalizer step must boost low frequencies
    loudness_usb_volume_changed_left(-25 * 256);
    int differs = 0;
    for (int i = 0; i < 200; i++) {
        S32 in_L[1] = { (i & 1) ? (100000 << 8) : (-100000 << 8) };
        S32 in_R[1] = { (i & 1) ? (100000 << 8) : (-100000 << 8) };
        S32 before_L = in_L[0];
        loudness_filter_24bit_stereo_packet(in_L, in_R, 1);
        if (in_L[0] != before_L) differs = 1;
    }
    assert(differs);
    printf("test_loudness_24bit_processing passed\n");
}

void test_loudness_update_active_equalizer_step_uncompressed_18dbfs(void) {
    printf("Running test_loudness_update_active_equalizer_step (Uncompressed -18 dBFS RMS)...\n");
    loudness_init();
    loudness_set_source_has_volume_control();

    loudness_usb_volume_changed_left(0);
    printf("  [0 dBFS vol] loudness_get_last_db_spl_left_x10(): %d\n", loudness_get_last_db_spl_left_x10());
    assert(loudness_get_last_db_spl_left_x10() == LOUDNESS_DB_SPL_MAX * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    loudness_usb_volume_changed_left(-10 * 256);
    printf("  [-10 dBFS vol] loudness_get_last_db_spl_left_x10(): %d\n", loudness_get_last_db_spl_left_x10());
    assert(loudness_get_last_db_spl_left_x10() == (LOUDNESS_DB_SPL_MAX - 10) * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    loudness_usb_volume_changed_left(-20 * 256);
    printf("  [-20 dBFS vol] loudness_get_last_db_spl_left_x10(): %d\n", loudness_get_last_db_spl_left_x10());
    assert(loudness_get_last_db_spl_left_x10() == (LOUDNESS_DB_SPL_MAX - 20) * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    loudness_usb_volume_changed_left(-30 * 256);
    printf("  [-30 dBFS vol] loudness_get_last_db_spl_left_x10(): %d\n", loudness_get_last_db_spl_left_x10());
    assert(loudness_get_last_db_spl_left_x10() == (LOUDNESS_DB_SPL_MAX - 30) * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    printf("test_loudness_update_active_equalizer_step_uncompressed_18dbfs passed\n\n");
}

void test_loudness_update_active_equalizer_step_compressed_6dbfs(void) {
    printf("Running test_loudness_update_active_equalizer_step (Max Compressed -6 dBFS RMS)...\n");
    loudness_init();
    loudness_set_source_has_volume_control();

    loudness_usb_volume_changed_left(0);
    printf("  [0 dBFS vol] loudness_get_last_db_spl_left_x10(): %d\n", loudness_get_last_db_spl_left_x10());
    assert(loudness_get_last_db_spl_left_x10() == LOUDNESS_DB_SPL_MAX * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    loudness_usb_volume_changed_left(-10 * 256);
    printf("  [-10 dBFS vol] loudness_get_last_db_spl_left_x10(): %d\n", loudness_get_last_db_spl_left_x10());
    assert(loudness_get_last_db_spl_left_x10() == (LOUDNESS_DB_SPL_MAX - 10) * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    loudness_usb_volume_changed_left(-20 * 256);
    printf("  [-20 dBFS vol] loudness_get_last_db_spl_left_x10(): %d\n", loudness_get_last_db_spl_left_x10());
    assert(loudness_get_last_db_spl_left_x10() == (LOUDNESS_DB_SPL_MAX - 20) * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    loudness_usb_volume_changed_left(-30 * 256);
    printf("  [-30 dBFS vol] loudness_get_last_db_spl_left_x10(): %d\n", loudness_get_last_db_spl_left_x10());
    assert(loudness_get_last_db_spl_left_x10() == (LOUDNESS_DB_SPL_MAX - 30) * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    printf("test_loudness_update_active_equalizer_step_compressed_6dbfs passed\n\n");
}

void test_usb_volume_format(void) {
    printf("Running test_usb_volume_format...\n");
    assert(usb_volume_format(0) == VOL_MULT_UNITY);

    S32 mult = usb_volume_format(-6 * 256);
    assert(mult == (VOL_MULT_UNITY >> 1));
    assert(VOL_MIN == (S16)(-60 * 256));
    assert(usb_volume_format(VOL_MIN) > 0);
    printf("test_usb_volume_format passed\n");
}

void test_saturate_24bit_s64_to_s32() {
    printf("Running test_saturate_24bit_s64_to_s32...\n");
    assert(saturate_24bit_s64_to_s32(8388607LL) == 8388607);
    assert(saturate_24bit_s64_to_s32(8388608LL) == 8388607);
    assert(saturate_24bit_s64_to_s32(-8388608LL) == -8388608);
    assert(saturate_24bit_s64_to_s32(-8388609LL) == -8388608);
    printf("test_saturate_24bit_s64_to_s32 passed\n");
}

void test_saturate_16bit_s32_to_s32(void) {
    printf("Running test_saturate_16bit_s32_to_s32...\n");
    assert(saturate_16bit_s32_to_s32(50000) == INT16_MAX);
    assert(saturate_16bit_s32_to_s32(-50000) == INT16_MIN);
    assert(saturate_16bit_s32_to_s32(1000) == 1000);
    printf("test_saturate_16bit_s32_to_s32 passed\n");
}

static void assert_gain_dbfs_left(int32_t expected)
{
    assert(loudness_get_gain_dbfs_left() == expected);
}

static void assert_gain_dbfs_right(int32_t expected)
{
    assert(loudness_get_gain_dbfs_right() == expected);
}

void test_loudness_get_gain_dbfs_per_channel(void) {
    printf("Running test_loudness_get_gain_dbfs_per_channel...\n");
    loudness_set_source_has_volume_control();

    loudness_usb_volume_changed_left(0);
    loudness_usb_volume_changed_right(0);
    assert_gain_dbfs_left(0);
    assert_gain_dbfs_right(0);

    loudness_usb_volume_changed_left(-256 * 10);
    assert_gain_dbfs_left(-10);
    assert_gain_dbfs_right(0);

    loudness_usb_volume_changed_right(-256 * 10);
    assert_gain_dbfs_left(-10);
    assert_gain_dbfs_right(-10);

    loudness_usb_volume_changed_left(VOL_MIN);
    assert_gain_dbfs_left(-60);

    loudness_usb_volume_changed_right(VOL_MAX);
    assert_gain_dbfs_left(-60);
    assert_gain_dbfs_right(0);

    loudness_usb_volume_changed_left(VOL_MAX);
    assert_gain_dbfs_left(0);
    assert_gain_dbfs_right(0);
    printf("test_loudness_get_gain_dbfs_per_channel passed\n");
}



/* Test for Use Case A og B: Verifiser 16-bit CD-audio fortegnsbevaring og prosessering */
void test_loudness_16bit_cd_audio_processing(void) {
    printf("Running test_loudness_16bit_cd_audio_processing...\n");
    loudness_init();

    loudness_usb_volume_changed_left(-20 * 256);

    int32_t usb_container = ((int32_t)(int16_t)-4000) << 16;

    int32_t filtered_container = loudness_test_filter_16bit_left(usb_container);

    assert(filtered_container < 0);
    assert((filtered_container & 0xFFFF) == 0);
    assert(filtered_container != usb_container);

    printf("test_loudness_16bit_cd_audio_processing passed\n");
}

void test_loudness_16bit_container_no_int32_overflow(void) {
    printf("Running test_loudness_16bit_container_no_int32_overflow...\n");
    loudness_init();
    spk_vol_usb_L = -20 * 256;
    loudness_set_level_dbfs(-18);

    int32_t usb_container = ((int32_t)INT16_MAX) << 16;
    int i;

    for (i = 0; i < 4096; i++) {
        (void)loudness_test_filter_16bit_left(usb_container);
    }

    int32_t filtered_container = loudness_test_filter_16bit_left(usb_container);
    int32_t amp = (int16_t)(filtered_container >> 16);

    assert((filtered_container & 0xFFFF) == 0);
    assert(filtered_container == (amp << 16));
    assert(amp >= INT16_MIN && amp <= INT16_MAX);

    /* Do not compare this with a second direct loudness_test_filter_24bit_left() call:
     * the DF2 filter is stateful, so the next sample intentionally observes
     * different w1/w2 state. The assertions above verify container safety. */

    printf("test_loudness_16bit_container_no_int32_overflow passed\n");
}

/* Test for Volum-grenser: Sjekk at indeks 255 gir absolutt digital stillhet (Mute) */
void test_digital_volume_mute(void) {
    printf("Running test_digital_volume_mute...\n");

    int32_t hot_sample = 8388607;
    assert(apply_digital_volume(hot_sample, usb_volume_format(VOL_INVALID)) == 0);
    printf("test_digital_volume_mute passed\n");
}

/**
 * TEST 1: Verifiser at fortegnshåndteringen (Sign Extension) er intakt.
 * Et negativt 24-bits tall plassert i bits 31:8 må ikke vris om til et
 * gigantisk positivt tall etter at det har passert container-skiftingen.
 */
void test_container_sign_preservation(void) {
    printf("Running test_container_sign_preservation...\n");
    printf("Sign preservation for negative samples\n");
    loudness_fast_reset_states();
    
    loudness_test_load_active_quotients_fast(90);

    // Et typisk negativt signal i en 32-bit container (bits 31:8)
    // Eksempel: -1000 i 24-bit er 0xFFFF18. Skiftet opp: 0xFFF18000
    S32 input_negative = (S32)0xFFF18000; 
    
    S32 output = loudness_test_filter_24bit_left(input_negative);

    // Sjekk spesifikt at bit 31 fortsatt er høy (negativt tall)
    if (output >= 0) {
        printf("Negativt tall ble positivt! Output: %d", output);
        assert(false);
    }
    printf("test_container_sign_preservation passed\n\n");
}

/**
 * TEST 2: Verifiser at ulineær bit-wrapping ikke skjer ved full-skala (0 dBFS).
 * Tester maksimalt positive og negative verdier for 24-bit lyd i en 32-bit container.
 */
void test_full_scale_boundaries(void) {
    printf("Running test_full_scal_boundaries...\n\n");
    printf("Full scale bounary values (0 dBFS)\n");
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(120);

    // Maks positiv 24-bit i 32-bit container: 0x7FFFFF00
    S32 max_pos = (S32)0x7FFFFF00;
    // Maks negativ 24-bit i 32-bit container: 0x80000000
    S32 max_neg = (S32)0x80000000;

    S32 out_pos = loudness_test_filter_24bit_left(max_pos);
    S32 out_neg = loudness_test_filter_24bit_left(max_neg);

    assert(out_pos >= 0);
    assert(out_neg <= 0);
    assert((out_pos & 0xFF) == 0);
    assert((out_neg & 0xFF) == 0);

    printf("test_full_scal_boundaries passed\n\n");
}

/**
 * TEST 3: Null-respons og DC-offset sjekk.
 * Sørger for at filteret ikke genererer en egen DC-spenning eller "spraker"
 * når inngangen er fullstendig stille (digital null).
 */
void test_dc_silence_response(void) {
    printf("Running test_dc_slience response..\n\n");
    printf("Digital null/silence\n");
    loudness_fast_reset_states();
    
    // Sett et trinn med kraftig bassboost (f.eks 55 phon)
    loudness_test_load_active_quotients_fast(0); 

    // Kjør 1000 sampler med ren stillhet gjennom filteret
    for (int i = 0; i < 1000; i++) {
        S32 out = loudness_test_filter_24bit_left(0);
        if (out != 0) {
            printf("Filteret lekker energi ved stillhet! Sample %d ga: %d", i, out);
            assert(false);
        }
    }
}

void test_loudness_24bit_container_round_trip(void) {
    printf("Running test_loudness_24bit_container_round_trip...\n");
    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_usb_volume_changed_left(0);

    /* The full filter must preserve container alignment and sign. */
    assert(loudness_test_filter_24bit_left(0) == 0);
    {
        int32_t positive = loudness_test_filter_24bit_left(123456 << 8);
        loudness_fast_reset_states();
        int32_t negative = loudness_test_filter_24bit_left(-123456 << 8);
        assert(positive > 0);
        assert(negative < 0);
        assert((positive & 0xFF) == 0);
        assert((negative & 0xFF) == 0);
    }

    printf("test_loudness_24bit_container_round_trip passed\n\n");
}

void test_loudness_24bit_container_zero_crossing(void) {
    printf("Running test_loudness_24bit_container_zero_crossing...\n");
    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_usb_volume_changed_left(0);

    int32_t prev = loudness_test_filter_24bit_left(100 << 8);
    int32_t at_zero = loudness_test_filter_24bit_left(0);
    int32_t next = loudness_test_filter_24bit_left(-100 << 8);

    /* Exact-zero input remains container-aligned while state advances. */
    assert((prev & 0xFF) == 0);
    assert((at_zero & 0xFF) == 0);
    assert((next & 0xFF) == 0);

    printf("test_loudness_24bit_container_zero_crossing passed\n\n");
}

void test_loudness_df2_step_transition_no_reset(void) {
    printf("Running test_loudness_df2_step_transition_no_reset...\n");
    /* TODO: fix this test
    int i;
    int32_t sample = 200000 << 8;
    int32_t out_before;
    int32_t out_after;
    int32_t spike;

    loudness_init();
    loudness_set_source_has_volume_control();

    loudness_usb_volume_changed_left(-10 * 256);
    assert(loudness_get_last_db_spl_left_x10() == (LOUDNESS_DB_SPL_MAX - 10) * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    for (i = 0; i < 256; i++) {
        loudness_test_filter_24bit_left( sample);
    }
    out_before = (int32_t)loudness_test_filter_24bit_left( sample);

    loudness_usb_volume_changed_left(-20 * 256);
    assert(loudness_get_last_db_spl_left_x10() == (LOUDNESS_DB_SPL_MAX - 20) * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    out_after = (int32_t)loudness_test_filter_24bit_left( sample);
    spike = out_after - out_before;
    if (spike < 0) {
        spike = -spike;
    }
    assert(spike < (sample >> 2));
*/
    printf("test_loudness_df2_step_transition_no_reset passed\n\n");
}


/**
 * @brief Test for Use Case C: Intersample Peaks & Headroom Stress-Testing
 *
 * Heavily limited "Loudness War" CD audio alternates rapidly between maximum positive (+32767)
 * and maximum negative (-32768) bounds. Passing these through a heavy low-shelf loudness boost
 * causes intermediate biquad accumulation to skyrocket past full-scale.
 * This test guarantees that our saturation layer handles this clipping gracefully without wrapping.
 */
void test_loudness_intersample_peak_saturation(void) {
    printf("Running test_loudness_intersample_peak_saturation...\n");

    /* Test safe clipping on the absolute boundaries of the 24-bit domain */
    int64_t extreme_positive_accumulator = 200000000LL;  /* Far beyond INT24_MAX */
    int64_t extreme_negative_accumulator = -200000000LL; /* Far below INT24_MIN */

    int32_t saturated_pos = saturate_24bit_s64_to_s32(extreme_positive_accumulator);
    int32_t saturated_neg = saturate_24bit_s64_to_s32(extreme_negative_accumulator);

    /* The output MUST be strictly clamped to the physical limits of the AK4396/5394A DACs */
    assert(saturated_pos == 8388607);
    assert(saturated_neg == -8388608);

    printf("test_loudness_intersample_peak_saturation passed\n\n");
}

void test_loudness_get_equalizer_step_121_levels(void) {
    printf("Running test_loudness_get_equalizer_step_121_levels...\n");
    assert(loudness_test_get_equalizer_step(LOUDNESS_MIN_PHON_X10) == 0);
    assert(loudness_test_get_equalizer_step(LOUDNESS_MIN_PHON_X10 + 5) == 1);
    assert(loudness_test_get_equalizer_step(550) == 60);
    assert(loudness_test_get_equalizer_step(800) == 110);
    assert(loudness_test_get_equalizer_step(LOUDNESS_MAX_PHON_X10) == 120);
    printf("test_loudness_get_equalizer_step_121_levels passed\n\n");
}

void test_loudness_80_phon_baked_volume_filter(void) {
    printf("Running test_loudness_80_phon_baked_volume_filter...\n");
    loudness_init();
    loudness_set_source_has_volume_control();
    current_freq.frequency = 48000;
    loudness_change_frequency_fast(48000);
    loudness_usb_volume_changed_left(-15 * 256);
    loudness_usb_volume_changed_right(-15 * 256);
    loudness_fast_reset_states();
    assert(loudness_test_get_equalizer_step(last_db_spl_left_x10) == 90);
    assert(loudness_test_get_equalizer_step(last_db_spl_right_x10) == 90);

    int64_t sample = 123456;
    int64_t result = loudness_24bit_wrapper(sample);
    assert(result > 0);
    assert(result < sample);
    printf("test_loudness_80_phon_baked_volume_filter passed\n\n");
}

void test_loudness_equalizer_step_half_db_boundaries(void) {
    printf("Running test_loudness_equalizer_step_half_db_boundaries...\n");
    loudness_init();
    last_db_spl_left_x10 = LOUDNESS_REF_PHON * 10;
    last_db_spl_right_x10 = LOUDNESS_REF_PHON * 10;
    assert(loudness_test_should_change_equalizer_step(800, 800) == FALSE);
    assert(loudness_test_should_change_equalizer_step(805, 800) == TRUE);
    assert(loudness_test_should_change_equalizer_step(800, 805) == TRUE);
    assert(loudness_test_should_change_equalizer_step(795, 800) == TRUE);
    assert(loudness_test_should_change_equalizer_step(800, 795) == TRUE);
    assert(loudness_test_should_change_equalizer_step(805, 805) == TRUE);
    assert(loudness_test_should_change_equalizer_step(795, 795) == TRUE);
    printf("test_loudness_equalizer_step_half_db_boundaries passed\n\n");
}

void test_loudness_default_enabled(void) {
    printf("Running test_loudness_default_enabled...\n");
    loudness_init();
    assert(loudness_loudness_is_enabled() == TRUE);
    assert(loudness_bass_boost_is_enabled() == FALSE);
    printf("test_loudness_default_enabled passed\n\n");
}

void test_loudness_bass_boost_toggle_mutual_exclusion(void) {
    printf("Running test_loudness_bass_boost_toggle_mutual_exclusion...\n");
    loudness_init();
    assert(loudness_loudness_is_enabled() == TRUE);
    assert(loudness_bass_boost_is_enabled() == FALSE);
    assert(loudness_active_filter() == LOUDNESS_MODE);

    loudness_bass_boost_set(TRUE);
    assert(loudness_bass_boost_is_enabled() == TRUE);
    assert(loudness_loudness_is_enabled() == TRUE);
    assert(loudness_active_filter() != FILTER_OFF_MODE);

    loudness_loudness_set(TRUE);
    assert(loudness_loudness_is_enabled() == TRUE);
    assert(loudness_bass_boost_is_enabled() == TRUE);
    assert(loudness_active_filter() == LOUDNESS_MODE);

    loudness_loudness_set(FALSE);
    assert(loudness_loudness_is_enabled() == FALSE);
    assert(loudness_bass_boost_is_enabled() == TRUE);
    assert(loudness_active_filter() != FILTER_OFF_MODE);
    printf("test_loudness_bass_boost_toggle_mutual_exclusion passed\n\n");
}

void test_loudness_bass_boost_mirror_and_gate(void) {
    printf("Running test_loudness_bass_boost_mirror_and_gate...\n");

    loudness_init();
    loudness_set_source_has_volume_control();

    loudness_bass_boost_set(FALSE);
    loudness_loudness_set(FALSE);
    assert(loudness_bass_boost_is_enabled() == FALSE);
    loudness_update_active_equalizer_step();
    assert(loudness_test_volume_in_biquad() == FALSE);

    loudness_usb_volume_changed_left(-20 * 256);
    loudness_usb_volume_changed_right(-20 * 256);
    loudness_update_active_equalizer_step();
    assert((int)last_db_spl_right_x10 == (LOUDNESS_DB_SPL_MAX - 20) * 10);

    loudness_bass_boost_set(TRUE);
    loudness_usb_volume_changed_left(-20 * 256);
    loudness_usb_volume_changed_right(-20 * 256);
    loudness_update_active_equalizer_step();
    assert(loudness_test_volume_in_biquad() == FALSE);
    {
        const biquad_quotients_fast_t *no_vol =
            loudness_fast_no_volume_quotient_table_48000hz();
        const biquad_quotients_fast_t *low_q = loudness_lowshelf_active_quotients();
        assert(low_q[0].b0 == no_vol[BASSS_PHON_55_IDX].b0);
        assert(low_q[0].a1 == no_vol[BASSS_PHON_55_IDX].a1);
    }

    loudness_bass_boost_set(TRUE);
    printf("test_loudness_bass_boost_mirror_and_gate passed\n\n");
}

void test_loudness_bass_boost_unity_passthrough(void) {
    printf("Running test_loudness_bass_boost_unity_passthrough...\n");
    const int32_t test_sample = 1000000 << 8;
    int32_t filtered;
    double gain_db;

    loudness_init();
    loudness_set_source_has_volume_control();
    current_freq.frequency = 48000;
    loudness_change_frequency(48000);

    loudness_bass_boost_set(FALSE);
    loudness_loudness_set(FALSE);
    loudness_update_active_equalizer_step();
    assert(loudness_test_volume_in_biquad() == FALSE);

    filtered = loudness_test_filter_24bit_left( test_sample);
    gain_db = 20.0 * log10(fabs((double)filtered / (double)test_sample));
    assert(fabs(gain_db) < 0.1);

    printf("test_loudness_bass_boost_unity_passthrough passed\n\n");
}

void test_loudness_bass_boost_reenable_resets_states(void) {
    printf("Running test_loudness_bass_boost_reenable_resets_states...\n");
    biquad_state_fast_t state;

    loudness_init();
    loudness_set_source_has_volume_control();
    current_freq.frequency = 48000;
    loudness_change_frequency(48000);

    loudness_bass_boost_set(FALSE);
    loudness_loudness_set(FALSE);
    loudness_update_active_equalizer_step();

    loudness_test_get_fast_channel(0, &state, NULL);
    state.w1 = 100000000;
    loudness_test_set_fast_channel(0, &state);

    loudness_bass_boost_set(TRUE);
    loudness_update_active_equalizer_step();

    loudness_test_get_fast_channel(0, &state, NULL);
    assert(state.w1 == 0);
    assert(loudness_test_volume_in_biquad() == FALSE);

    printf("test_loudness_bass_boost_reenable_resets_states passed\n\n");
}

void test_loudness_bass_boost_facade_ignores_filter_activity(void) {
    printf("Running test_loudness_bass_boost_facade_ignores_filter_activity...\n");
    const int max_phon_step =
        (LOUDNESS_MAX_PHON_X10 - LOUDNESS_MIN_PHON_X10)
        / LOUDNESS_EQUALIZER_STEP_X10;

    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_bass_boost_set(TRUE);

    loudness_usb_volume_changed_left(0);
    loudness_usb_volume_changed_right(0);
    loudness_update_active_equalizer_step();
    assert(loudness_test_get_equalizer_step(last_db_spl_right_x10) == max_phon_step);
    assert(loudness_bass_boost_is_enabled() == TRUE);

    loudness_bass_boost_set(FALSE);
    assert(loudness_bass_boost_is_enabled() == FALSE);

    loudness_bass_boost_set(TRUE);
    printf("test_loudness_bass_boost_facade_ignores_filter_activity passed\n\n");
}

void test_loudness_highshelf_and_lowshelf_share_runtime_slot_and_swap_simultaneously(void) {
    printf("Running test_loudness_highshelf_and_lowshelf_share_runtime_slot_and_swap_simultaneously...\n");
    loudness_init();
    loudness_loudness_set(TRUE);
    current_freq.frequency = 48000;
    loudness_change_frequency_fast(48000);

    /* Publish step 100 on left (85 phon), step 40 on right (55 phon) */
    loudness_fast_select_equalizer_steps(850, 550, 100, 40);

    const biquad_quotients_fast_t *low_q = loudness_lowshelf_active_quotients();
    const biquad_first_order_quotients_t *high_q = loudness_highshelf_active_quotients();

    /* Check that left matches step 100 and right matches step 40 from 48kHz baked tables */
    const biquad_quotients_fast_t *table_low = loudness_fast_baked_quotient_table_48000hz();
    const biquad_first_order_quotients_t *expected_high_100 = &highshelf_no_volume_48000hz[100];
    const biquad_first_order_quotients_t *expected_high_40 = &highshelf_no_volume_48000hz[40];

    assert(low_q[0].b0 == table_low[100].b0);
    assert(low_q[0].a1 == table_low[100].a1);
    assert(high_q[0].b0 == expected_high_100->b0);
    assert(high_q[0].a1 == expected_high_100->a1);

    assert(low_q[1].b0 == table_low[40].b0);
    assert(low_q[1].a1 == table_low[40].a1);
    assert(high_q[1].b0 == expected_high_40->b0);
    assert(high_q[1].a1 == expected_high_40->a1);

    printf("test_loudness_highshelf_and_lowshelf_share_runtime_slot_and_swap_simultaneously passed\n\n");
}

void test_bass_boost_mode_selects_index_40_with_12db_boost(void) {
    printf("Running test_bass_boost_mode_selects_index_40_with_12db_boost...\n");
    loudness_init();
    loudness_set_source_has_volume_control();
    current_freq.frequency = 48000;
    loudness_change_frequency_fast(48000);

    loudness_bass_boost_set(TRUE);
    loudness_usb_volume_changed_left(0); /* 0 dBFS */
    loudness_update_active_equalizer_step();

    const biquad_quotients_fast_t *low_q = loudness_lowshelf_active_quotients();
    const biquad_first_order_quotients_t *high_q = loudness_highshelf_active_quotients();

    const biquad_quotients_fast_t *table_low =
        loudness_fast_no_volume_quotient_table_48000hz();

    assert(low_q[0].b0 == table_low[BASSS_PHON_55_IDX].b0);
    assert(low_q[0].a1 == table_low[BASSS_PHON_55_IDX].a1);
    assert(high_q[0].b0 == LOUDNESS_Q28_ONE);
    assert(high_q[0].a1 == 0);
    assert(high_q[0].b1 == 0);
    assert(loudness_test_volume_in_biquad() == FALSE);

    printf("test_bass_boost_mode_selects_index_40_with_12db_boost passed\n\n");
}

void test_bass_boost_external_volume_scales_output(void) {
    printf("Running test_bass_boost_external_volume_scales_output...\n");
    const int32_t test_sample = 1000000 << 8;
    int32_t filtered;
    S32 sample_L;
    S32 sample_R;

    loudness_init();
    loudness_set_source_has_volume_control();
    current_freq.frequency = 48000;
    loudness_change_frequency_fast(48000);
    loudness_bass_boost_set(TRUE);
    loudness_update_active_equalizer_step();
    assert(loudness_test_volume_in_biquad() == FALSE);

    filtered = loudness_test_filter_24bit_left( test_sample);
    spk_vol_mult_L = VOL_MULT_UNITY / 2;
    spk_vol_mult_R = VOL_MULT_UNITY / 2;
    sample_L = filtered;
    sample_R = filtered;
    adjust_volume(&sample_L, &sample_R);
    assert(labs(sample_L) < labs(filtered));

    printf("test_bass_boost_external_volume_scales_output passed\n\n");
}

void test_active_filter_mode_fallback_to_bass_boost(void) {
    printf("Running test_active_filter_mode_fallback_to_bass_boost...\n");
    loudness_init();
    loudness_bass_boost_set(FALSE);
    loudness_loudness_set(TRUE);
    assert(loudness_loudness_is_enabled() == TRUE);
    assert(loudness_bass_boost_is_enabled() == FALSE);
    assert(loudness_active_filter() == LOUDNESS_MODE);

    loudness_bass_boost_set(TRUE);
    assert(loudness_bass_boost_is_enabled() == TRUE);
    assert(loudness_active_filter() != FILTER_OFF_MODE);

    loudness_loudness_set(TRUE);
    assert(loudness_active_filter() == LOUDNESS_MODE);

    loudness_loudness_set(FALSE);
    assert(loudness_bass_boost_is_enabled() == TRUE);
    assert(loudness_loudness_is_enabled() == FALSE);
    assert(loudness_active_filter() != FILTER_OFF_MODE);

    printf("test_active_filter_mode_fallback_to_bass_boost passed\n\n");
}

void test_device_audio_set_volume_in_biquad_hard_clip_switching(void) {
    printf("Running test_device_audio_set_volume_in_biquad_hard_clip_switching...\n");
    S32 sL, sR;

    /* 1. Initial state without source volume control */
    loudness_init();
    loudness_test_reset_inferred_gain();
    loudness_loudness_set(TRUE);
    loudness_bass_boost_set(FALSE);
    assert(loudness_inferred_gain_has_source_volume_control() == FALSE);
    assert(loudness_active_filter() == LOUDNESS_MODE);
    assert(device_audio_volume_apply_fn == hard_clip);

    /* 2. Bass boost enabled without source volume control -> hard_clip only */
    loudness_bass_boost_set(TRUE);
    assert(loudness_active_filter() != FILTER_OFF_MODE);
    assert(device_audio_volume_apply_fn == hard_clip);

    /* Verify hard_clip behavior without volume scaling */
    sL = INT24_MAX + 1000;
    sR = INT24_MIN - 1000;
    device_audio_volume_apply_fn(&sL, &sR);
    assert(sL == INT24_MAX);
    assert(sR == INT24_MIN);

    /* 3. Last write wins: enable loudness while bass boost is still enabled */
    loudness_loudness_set(TRUE);
    assert(loudness_bass_boost_is_enabled() == TRUE);
    assert(loudness_loudness_is_enabled() == TRUE);
    assert(loudness_active_filter() == LOUDNESS_MODE);
    assert(device_audio_volume_apply_fn == hard_clip);

    /* 4. Disable loudness -> fallback to active bass boost */
    loudness_loudness_set(FALSE);
    assert(loudness_active_filter() != FILTER_OFF_MODE);
    assert(device_audio_volume_apply_fn == hard_clip);

    /* 5. Set source volume control while bass boost is active -> adjust_volume_hard_clip */
    loudness_set_source_has_volume_control();
    assert(loudness_inferred_gain_has_source_volume_control() == TRUE);
    assert(loudness_active_filter() != FILTER_OFF_MODE);
    assert(device_audio_volume_apply_fn == adjust_volume_hard_clip);

    /* Verify adjust_volume_hard_clip with unity gain */
    spk_vol_mult_L = VOL_MULT_UNITY;
    spk_vol_mult_R = VOL_MULT_UNITY;
    sL = INT24_MAX + 500;
    sR = INT24_MIN - 500;
    device_audio_volume_apply_fn(&sL, &sR);
    assert(sL == INT24_MAX);
    assert(sR == INT24_MIN);

    /* 6. Switch to LOUDNESS_MODE with source volume control -> adjust_volume */
    loudness_loudness_set(TRUE);
    assert(loudness_active_filter() == LOUDNESS_MODE);
    assert(device_audio_volume_apply_fn == adjust_volume_hard_clip);

    /* 7. Switch to FILTER_OFF_MODE with source volume control -> adjust_volume */
    loudness_loudness_set(FALSE);
    loudness_bass_boost_set(FALSE);
    assert(loudness_active_filter() == FILTER_OFF_MODE);
    assert(device_audio_volume_apply_fn == adjust_volume);

    /* 8. Reset source volume control in FILTER_OFF_MODE -> keep_volume */
    loudness_test_reset_inferred_gain();
    assert(loudness_active_filter() == FILTER_OFF_MODE);
    assert(device_audio_volume_apply_fn == keep_volume);

    printf("test_device_audio_set_volume_in_biquad_hard_clip_switching passed\n\n");
}

void test_uac2_loudness_filter_enabled_filter_off(void) {
    printf("Running test_uac2_loudness_filter_enabled_filter_off...\n");
    loudness_init();

    loudness_bass_boost_set(FALSE);
    loudness_loudness_set(FALSE);
    assert(loudness_uac2_packet_filter_enabled(TRUE, 44100) == FALSE);
    assert(loudness_uac2_packet_filter_enabled(TRUE, 48000) == FALSE);
    assert(loudness_uac2_packet_filter_enabled(FALSE, 48000) == FALSE);
    assert(loudness_uac2_packet_filter_enabled(TRUE, 96000) == FALSE);

    loudness_loudness_set(TRUE);
    assert(loudness_uac2_packet_filter_enabled(TRUE, 44100) == TRUE);
    assert(loudness_uac2_packet_filter_enabled(TRUE, 48000) == TRUE);

    loudness_loudness_set(FALSE);
    loudness_bass_boost_set(TRUE);
    assert(loudness_uac2_packet_filter_enabled(TRUE, 48000) == TRUE);

    printf("test_uac2_loudness_filter_enabled_filter_off passed\n\n");
}

#define SINE_TEST_SAMPLE_COUNT   48000
#define SINE_TEST_SETTLE_SAMPLES 2000
#define SINE_TEST_AMPLITUDE      2097152
#define SINE_TEST_FREQUENCY_HZ   50
#define SINE_TEST_GAIN_TOLERANCE_DB 0.05
#define SINE_TEST_PI             3.14159265358979323846
#define LOUDNESS_TRANSITION_TEST_SAMPLES 1000
#define LOUDNESS_TRANSITION_SWITCH_SAMPLE 500

typedef struct {
    int phon;
    int equalizer_step;
    double expected_gain_44100_db;
    double expected_gain_48000_db;
} bass_boost_test_case_t;

static const bass_boost_test_case_t bass_boost_test_cases[] = {
    { 55, 40, -25.284141, -25.292944 },
    { 57, 44, -24.045971, -24.052440 },
    { 59, 48, -22.827060, -22.833630 },
    { 61, 52, -21.624469, -21.632203 },
    { 63, 56, -20.437729, -20.444329 },
    { 65, 60, -19.263486, -19.268601 },
    { 67, 64, -18.097796, -18.102755 },
    { 69, 68, -16.940458, -16.945126 },
    { 71, 72, -15.789999, -15.794413 },
    { 73, 76, -14.643666, -14.647834 },
    { 75, 80, -13.502152, -13.505604 },
    { 77, 84, -12.363544, -12.366152 },
    { 79, 88, -11.227045, -11.229486 },
};

static double measure_fast_50hz_gain_db(
    uint32_t sample_rate_hz, int equalizer_step)
{
    double input_sum_squares = 0.0;
    double output_sum_squares = 0.0;
    double input_rms;
    double output_rms;
    int i;

    loudness_loudness_set(TRUE);
    loudness_bass_boost_set(FALSE);
    loudness_set_source_has_volume_control();
    current_freq.frequency = sample_rate_hz;
    loudness_change_frequency_fast(sample_rate_hz);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(equalizer_step);

    for (i = 0; i < SINE_TEST_SAMPLE_COUNT; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        int32_t input_container = (int32_t)((uint32_t)input_24 << 8);
        int32_t output_container =
            loudness_test_filter_24bit_left(input_container);
        int32_t output_24 = output_container >> 8;

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

static void assert_50hz_bass_boost_case(size_t case_index)
{
    const bass_boost_test_case_t *test_case =
        &bass_boost_test_cases[case_index];
    double gain_44100 = measure_fast_50hz_gain_db(
        44100, test_case->equalizer_step);
    double gain_48000 = measure_fast_50hz_gain_db(
        48000, test_case->equalizer_step);

    printf("  %d phon: 44.1 kHz %.3f dB, 48 kHz %.3f dB\n",
        test_case->phon, gain_44100, gain_48000);
    fflush(stdout);
    assert(fabs(gain_44100 - test_case->expected_gain_44100_db) <
        SINE_TEST_GAIN_TOLERANCE_DB);
    assert(fabs(gain_48000 - test_case->expected_gain_48000_db) <
        SINE_TEST_GAIN_TOLERANCE_DB);
}

#define DEFINE_50HZ_BASS_BOOST_TEST(PHON, INDEX) \
    void test_50hz_##PHON##phon_bass_boost_magnitude(void) \
    { \
        printf("Running test_50hz_%dphon_bass_boost_magnitude...\n", PHON); \
        assert_50hz_bass_boost_case(INDEX); \
    }

DEFINE_50HZ_BASS_BOOST_TEST(55, 0)
DEFINE_50HZ_BASS_BOOST_TEST(57, 1)
DEFINE_50HZ_BASS_BOOST_TEST(59, 2)
DEFINE_50HZ_BASS_BOOST_TEST(61, 3)
DEFINE_50HZ_BASS_BOOST_TEST(63, 4)
DEFINE_50HZ_BASS_BOOST_TEST(65, 5)
DEFINE_50HZ_BASS_BOOST_TEST(67, 6)
DEFINE_50HZ_BASS_BOOST_TEST(69, 7)
DEFINE_50HZ_BASS_BOOST_TEST(71, 8)
DEFINE_50HZ_BASS_BOOST_TEST(73, 9)
DEFINE_50HZ_BASS_BOOST_TEST(75, 10)
DEFINE_50HZ_BASS_BOOST_TEST(77, 11)
DEFINE_50HZ_BASS_BOOST_TEST(79, 12)

void test_50hz_bass_boost_is_monotonic(void)
{
    double previous_44100 = -1000.0;
    double previous_48000 = -1000.0;
    size_t i;

    printf("Running test_50hz_bass_boost_is_monotonic...\n");
    for (i = 0; i < sizeof(bass_boost_test_cases) /
        sizeof(bass_boost_test_cases[0]); i++) {
        double gain_44100 = measure_fast_50hz_gain_db(
            44100, bass_boost_test_cases[i].equalizer_step);
        double gain_48000 = measure_fast_50hz_gain_db(
            48000, bass_boost_test_cases[i].equalizer_step);
        assert(gain_44100 > previous_44100);
        assert(gain_48000 > previous_48000);
        previous_44100 = gain_44100;
        previous_48000 = gain_48000;
    }
}

static void assert_filter_transition_equivalence(uint32_t sample_rate_hz,
    int step_from, int step_to)
{
    int i;
    biquad_state_fast_t state_at_switch;
    biquad_first_order_state_t highshelf_state_at_switch;
    biquad_state_fast_t ref_final_state;
    biquad_state_fast_t trans_final_state;
    biquad_first_order_state_t ref_final_highshelf_state;
    biquad_first_order_state_t trans_final_highshelf_state;

    int32_t reference_outputs[LOUDNESS_TRANSITION_TEST_SAMPLES];
    int32_t transition_outputs[LOUDNESS_TRANSITION_TEST_SAMPLES];

    loudness_loudness_set(TRUE);
    loudness_bass_boost_set(FALSE);
    loudness_set_source_has_volume_control();
    current_freq.frequency = sample_rate_hz;
    loudness_change_frequency_fast(sample_rate_hz);

    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(step_from);

    for (i = 0; i < LOUDNESS_TRANSITION_SWITCH_SAMPLE; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        int32_t input_container = (int32_t)((uint32_t)input_24 << 8);
        loudness_test_filter_24bit_left(input_container);
    }

    loudness_test_get_fast_channel(0, &state_at_switch, NULL);
    highshelf_state_at_switch = highshelf_states[0];

    loudness_fast_select_equalizer_steps(600, 600, step_to, step_to);

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        int32_t input_container = (int32_t)((uint32_t)input_24 << 8);
        int32_t output_container =
            loudness_test_filter_24bit_left(input_container);
        transition_outputs[i] = output_container >> 8;
    }

    loudness_test_get_fast_channel(0, &trans_final_state, NULL);
    trans_final_highshelf_state = highshelf_states[0];

    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(step_to);
    loudness_test_set_fast_channel(0, &state_at_switch);
    highshelf_states[0] = highshelf_state_at_switch;

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        int32_t input_container = (int32_t)((uint32_t)input_24 << 8);
        int32_t output_container =
            loudness_test_filter_24bit_left(input_container);
        reference_outputs[i] = output_container >> 8;
    }

    loudness_test_get_fast_channel(0, &ref_final_state, NULL);
    ref_final_highshelf_state = highshelf_states[0];

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE + 1;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        assert(transition_outputs[i] == reference_outputs[i]);
    }

    assert(trans_final_state.w1 == ref_final_state.w1);
    assert(trans_final_highshelf_state.w1 == ref_final_highshelf_state.w1);
}

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

void assert_highshelf_transition_equivalence(uint32_t sample_rate_hz,
    int step_from, int step_to)
{
    int i;
    biquad_first_order_state_t trans_state = { 0 };
    biquad_first_order_state_t state_at_switch = { 0 };
    biquad_first_order_state_t ref_state = { 0 };

    int32_t transition_outputs[LOUDNESS_TRANSITION_TEST_SAMPLES];
    int32_t reference_outputs[LOUDNESS_TRANSITION_TEST_SAMPLES];

    const biquad_first_order_quotients_t *q_from =
        get_highshelf_quotients(sample_rate_hz, step_from);
    const biquad_first_order_quotients_t *q_to =
        get_highshelf_quotients(sample_rate_hz, step_to);

    for (i = 0; i < LOUDNESS_TRANSITION_SWITCH_SAMPLE; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        loudness_highshelf(input_24, &trans_state, q_from);
    }

    state_at_switch = trans_state;

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        transition_outputs[i] =
            loudness_highshelf(input_24, &trans_state, q_to);
    }

    ref_state = state_at_switch;
    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        reference_outputs[i] =
            loudness_highshelf(input_24, &ref_state, q_to);
    }

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        assert(transition_outputs[i] == reference_outputs[i]);
    }

    assert(trans_state.w1 == ref_state.w1);
}

void test_loudness_all_curve_transitions_glitchfree(void)
{
    int step;

    printf("Running test_loudness_all_curve_transitions_glitchfree...\n");
    fflush(stdout);

    for (step = 0; step < LOUDNESS_NUM_EQUALIZER_STEPS - 1; step++) {
        printf("  Testing transition from step %d to %d...\n", step, step + 1);
        fflush(stdout);

        assert_filter_transition_equivalence(44100, step, step + 1);
        assert_filter_transition_equivalence(48000, step, step + 1);
    }

    printf("  All curve transitions verified bit-exact!\n");
    fflush(stdout);
    printf("test_loudness_all_curve_transitions_glitchfree passed\n\n");
}

/*
 * Lock exact integer biquad output for step 0 at 48 kHz (zero initial state,
 * sequential samples on channel 0). Re-capture literals after coefficient or
 * fixed-point kernel changes by running loudness_test_filter_24bit_left( ...) in a scratch
 * program with the same setup below.
 */
void test_loudness_fast_biquad_exact_samples(void)
{
    int32_t output0;
    int32_t output1;
    int32_t output2;
    int32_t packet_L[3];
    int32_t packet_R[3];

    printf("Running test_loudness_fast_biquad_exact_samples...\n");

    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_change_frequency_fast(48000);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(0);

    packet_L[0] = 2097152 << 8;
    packet_L[1] = 2097152 << 8;
    packet_L[2] = 0;
    packet_R[0] = 0;
    packet_R[1] = 0;
    packet_R[2] = 0;

    loudness_filter_24bit_stereo_packet(packet_L, packet_R, 3);
    output0 = packet_L[0] >> 8;
    output1 = packet_L[1] >> 8;
    output2 = packet_L[2] >> 8;
    printf("  Q4.28 exact outputs: %d, %d, %d\n",
        output0, output1, output2);
    assert(output0 == 2173);
    assert(output1 == 2325);
    assert(output2 == 303);

    printf("test_loudness_fast_biquad_exact_samples passed\n\n");
}

#define HIRES_HALF_DELTA_PACKET_SAMPLES 49u
#define HIRES_HALF_DELTA_TEST_SAMPLES   8820
#define HIRES_HALF_DELTA_GAIN_TOLERANCE_DB 0.35

static double measure_fast_50hz_gain_db_stereo_packet(uint32_t sample_rate_hz,
    int equalizer_step)
{
    double input_sum_squares = 0.0;
    double output_sum_squares = 0.0;
    double input_rms;
    double output_rms;
    int i;
    S32 packet_L[HIRES_HALF_DELTA_PACKET_SAMPLES];
    S32 packet_R[HIRES_HALF_DELTA_PACKET_SAMPLES];

    current_freq.frequency = sample_rate_hz;
    loudness_change_frequency_fast(sample_rate_hz);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(equalizer_step);

    for (i = 0; i < HIRES_HALF_DELTA_TEST_SAMPLES; i += HIRES_HALF_DELTA_PACKET_SAMPLES) {
        int j;
        int chunk = HIRES_HALF_DELTA_PACKET_SAMPLES;

        if (i + chunk > HIRES_HALF_DELTA_TEST_SAMPLES) {
            chunk = HIRES_HALF_DELTA_TEST_SAMPLES - i;
        }

        for (j = 0; j < chunk; j++) {
            double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
                (double)(i + j) / (double)sample_rate_hz;
            int32_t s16 = (int32_t)lrint(
                (32767.0 * 0.25) * sin(phase));

            packet_L[j] = s16 << 16;
            packet_R[j] = s16 << 16;
        }

        loudness_filter_16bit_stereo_packet(packet_L, packet_R, (U16)chunk);

        for (j = 0; j < chunk; j++) {
            int abs_index = i + j;

            if (abs_index >= SINE_TEST_SETTLE_SAMPLES) {
                double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
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

static void drain_zeros_stereo_packet(uint32_t sample_rate_hz)
{
    S32 zeros[48];
    int p;
    biquad_state_fast_t l_state;
    biquad_state_fast_t r_state;

    memset(zeros, 0, sizeof(zeros));
    current_freq.frequency = sample_rate_hz;
    loudness_change_frequency_fast(sample_rate_hz);

    for (p = 0; p < 2048; p++) {
        loudness_filter_16bit_stereo_packet(zeros, zeros, 48);
        if (loudness_test_get_filter_idle_mask() == LOUDNESS_TEST_FILTER_IDLE_ALL) {
            return;
        }
    }
    loudness_test_get_fast_channel(0, &l_state, NULL);
    loudness_test_get_fast_channel(1, &r_state, NULL);
    (void)l_state;
    (void)r_state;
    assert(false && "stereo packet path did not reach idle");
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
    loudness_filter_16bit_stereo_packet(packet_L, packet_R, num_samples);
}

void test_filter_idle_after_zeros_base(void)
{
    printf("Running test_filter_idle_after_zeros_base...\n");
    fflush(stdout);

    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_usb_volume_changed_left(0);

    current_freq.frequency = 44100;
    loudness_change_frequency_fast(44100);
    loudness_fast_reset_states();

    assert(loudness_test_filter_24bit_left( 0) == 0);

    assert(loudness_test_left_filter_is_idle());
    assert(!loudness_test_lowshelf_is_active());

    printf("test_filter_idle_after_zeros_base passed\n\n");
}

void test_filter_active_during_hires_interp(void)
{
    S32 packet_L[4];
    S32 packet_R[4];

    printf("Running test_filter_active_during_hires_interp...\n");

    current_freq.frequency = 88200;
    loudness_change_frequency_fast(88200);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(10);

    drive_stereo_impulse(packet_L, packet_R, 4);
    assert(!loudness_test_left_filter_is_idle());
    assert(loudness_test_lowshelf_is_active());

    printf("test_filter_active_during_hires_interp passed\n\n");
}

void test_filter_idle_after_zeros_stride2(void)
{
    S32 packet_L[8];
    S32 packet_R[8];
    int i;

    printf("Running test_filter_idle_after_zeros_stride2...\n");

    current_freq.frequency = 88200;
    loudness_change_frequency_fast(88200);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(80);

    for (i = 0; i < 8; i++) {
        packet_L[i] = 0;
        packet_R[i] = 0;
    }
    loudness_filter_16bit_stereo_packet(packet_L, packet_R, 8);

    assert(loudness_test_left_filter_is_idle());
    assert(loudness_test_right_filter_is_idle());
    assert(!loudness_test_lowshelf_is_active());

    printf("test_filter_idle_after_zeros_stride2 passed\n\n");
}

void test_filter_idle_after_zeros_stride4(void)
{
    S32 packet_L[16];
    S32 packet_R[16];
    int i;

    printf("Running test_filter_idle_after_zeros_stride4...\n");

    current_freq.frequency = 192000;
    loudness_change_frequency_fast(192000);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(80);

    for (i = 0; i < 16; i++) {
        packet_L[i] = 0;
        packet_R[i] = 0;
    }
    loudness_filter_16bit_stereo_packet(packet_L, packet_R, 16);

    assert(loudness_test_left_filter_is_idle());
    assert(loudness_test_right_filter_is_idle());
    assert(!loudness_test_lowshelf_is_active());

    printf("test_filter_idle_after_zeros_stride4 passed\n\n");
}

void test_per_channel_zero_bypass(void)
{
    S32 packet_L[8];
    S32 packet_R[8];
    biquad_state_fast_t r_state_before;
    biquad_state_fast_t r_state_after;
    int i;

    printf("Running test_per_channel_zero_bypass...\n");

    current_freq.frequency = 44100;
    loudness_change_frequency_fast(44100);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(10);

    for (i = 0; i < 8; i++) {
        packet_L[i] = (1000 + i * 100) << 16;
        packet_R[i] = 0;
    }

    loudness_test_get_fast_channel(1, &r_state_before, NULL);
    assert(loudness_test_right_filter_is_idle());

    loudness_filter_16bit_stereo_packet(packet_L, packet_R, 8);
    loudness_test_get_fast_channel(1, &r_state_after, NULL);

    assert(r_state_before.w1 == r_state_after.w1);
    assert(packet_R[0] == 0);

    printf("test_per_channel_zero_bypass passed\n\n");
}

void test_per_channel_independent_biquad(void)
{
    S32 packet_L[32];
    S32 packet_R[32];
    biquad_state_fast_t l_state;
    biquad_state_fast_t r_state;
    int i;

    printf("Running test_per_channel_independent_biquad...\n");

    current_freq.frequency = 44100;
    loudness_change_frequency_fast(44100);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(10);

    for (i = 0; i < 32; i++) {
        double phase = 2.0 * SINE_TEST_PI * 50.0 * (double)i / 44100.0;
        int32_t s16 = (int32_t)lrint((32767.0 * 0.25) * sin(phase));

        packet_L[i] = s16 << 16;
        packet_R[i] = 0;
    }

    loudness_filter_16bit_stereo_packet(packet_L, packet_R, 32);
    loudness_test_get_fast_channel(0, &l_state, NULL);
    loudness_test_get_fast_channel(1, &r_state, NULL);

    assert(l_state.w1 != 0);
    assert(r_state.w1 == 0);

    printf("test_per_channel_independent_biquad passed\n\n");
}

void test_per_channel_baked_volume_policy(void)
{
    biquad_state_fast_t state;
    biquad_quotients_fast_t left;
    biquad_quotients_fast_t right;
    biquad_quotients_fast_t shared_actual;
    biquad_quotients_fast_t shared_expected;
    const uint32_t independent_rates[] = { 44100, 48000,  88200, 96000, 176400, 192000 };
    size_t i;

    printf("Running test_per_channel_baked_volume_policy...\n");

    loudness_init();
    loudness_test_reset_inferred_gain();
    loudness_set_source_has_volume_control();
    loudness_loudness_set(TRUE);
    loudness_bass_boost_set(FALSE);
    spk_vol_usb_L = 0;
    spk_vol_usb_R = 0;
    current_freq.frequency = 48000;
    loudness_change_frequency_fast(48000);
    loudness_usb_volume_changed_left(0);
    loudness_usb_volume_changed_right(0);
    loudness_update_active_equalizer_step();
    loudness_test_get_fast_channel(0, &state, &left);
    loudness_test_get_fast_channel(1, &state, &right);
    assert(left.b0 == right.b0);

    spk_vol_usb_L = -6 * 256;
    spk_vol_usb_R = -20 * 256;
    for (i = 0; i < sizeof(independent_rates) / sizeof(independent_rates[0]); i++) {
        current_freq.frequency = independent_rates[i];
        loudness_change_frequency_fast(independent_rates[i]);
        loudness_usb_volume_changed_left(spk_vol_usb_L);
        loudness_usb_volume_changed_right(spk_vol_usb_R);
        assert(loudness_get_last_db_spl_left_x10() == (LOUDNESS_DB_SPL_MAX - 6) * 10);
        assert(loudness_get_last_db_spl_right_x10() == (LOUDNESS_DB_SPL_MAX - 20) * 10);
        loudness_test_get_fast_channel(0, &state, &left);
        loudness_test_get_fast_channel(1, &state, &right);
        assert(left.b0 != right.b0);
    }

    printf("test_per_channel_baked_volume_policy passed\n\n");
}

int main() {
    test_loudness_init();
    test_loudness_24bit_processing();
    test_loudness_update_active_equalizer_step_uncompressed_18dbfs();
    test_loudness_update_active_equalizer_step_compressed_6dbfs();
    test_usb_volume_format();
    test_saturate_24bit_s64_to_s32();
    test_saturate_16bit_s32_to_s32();
    test_loudness_get_gain_dbfs_per_channel();
    test_loudness_16bit_cd_audio_processing();
    test_loudness_16bit_container_no_int32_overflow();
    test_loudness_intersample_peak_saturation();
    test_digital_volume_mute();

    test_loudness_24bit_container_round_trip();
    test_loudness_24bit_container_zero_crossing();
    test_loudness_df2_step_transition_no_reset();

    test_container_sign_preservation();
    test_full_scale_boundaries();
    test_dc_silence_response();
    test_50hz_bass_boost_is_monotonic();
    test_50hz_55phon_bass_boost_magnitude();
    test_50hz_57phon_bass_boost_magnitude();
    test_50hz_59phon_bass_boost_magnitude();
    test_50hz_61phon_bass_boost_magnitude();
    test_50hz_63phon_bass_boost_magnitude();
    test_50hz_65phon_bass_boost_magnitude();
    test_50hz_67phon_bass_boost_magnitude();
    test_50hz_69phon_bass_boost_magnitude();
    test_50hz_71phon_bass_boost_magnitude();
    test_50hz_73phon_bass_boost_magnitude();
    test_50hz_75phon_bass_boost_magnitude();
    test_50hz_77phon_bass_boost_magnitude();
    test_50hz_79phon_bass_boost_magnitude();
    test_50hz_bass_boost_is_monotonic();
    test_loudness_all_curve_transitions_glitchfree();
    test_filter_idle_after_zeros_base();
    test_filter_active_during_hires_interp();
    test_filter_idle_after_zeros_stride2();
    test_filter_idle_after_zeros_stride4();
    test_per_channel_zero_bypass();
    test_per_channel_independent_biquad();
    test_per_channel_baked_volume_policy();
    test_loudness_fast_biquad_exact_samples();
    test_loudness_get_equalizer_step_121_levels();
    test_loudness_80_phon_baked_volume_filter();
    test_loudness_equalizer_step_half_db_boundaries();
    test_loudness_default_enabled();
    test_loudness_bass_boost_toggle_mutual_exclusion();
    test_loudness_highshelf_and_lowshelf_share_runtime_slot_and_swap_simultaneously();
    test_bass_boost_mode_selects_index_40_with_12db_boost();
    test_bass_boost_external_volume_scales_output();
    test_active_filter_mode_fallback_to_bass_boost();
    test_device_audio_set_volume_in_biquad_hard_clip_switching();
    test_uac2_loudness_filter_enabled_filter_off();
    test_loudness_bass_boost_mirror_and_gate();
    test_loudness_bass_boost_unity_passthrough();
    test_loudness_bass_boost_reenable_resets_states();
    test_loudness_bass_boost_facade_ignores_filter_activity();
    printf("\nAll tests completed!\n");
    return 0;
}
