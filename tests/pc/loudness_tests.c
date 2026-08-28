#include "fff.h"
#include "loudness.h"
#include "loudness_test_access.h"
#include "usb_specific_request.h"
#include "loudness_fast.h"
#include "loudness_highres.h"
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
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX);
    printf("test_loudness_init passed\n");
}

int64_t loudness_24bit_wrapper(int64_t sample) {
    return loudness_fast_24bit(0, (int32_t)sample);
}

void test_loudness_24bit_processing() {
    printf("Running test_loudness_24bit_processing...\n");
    loudness_init();
    loudness_set_source_has_volume_control();

    // At 0 dBFS the 95-phon row still runs through the normal biquad.
    loudness_usb_volume_changed(0);
    
    int64_t sample = 1000;
    int64_t result = loudness_24bit_wrapper(sample);
    
    assert(result != 0);

    // Switching to the 55 phon equalizer step must boost low frequencies
    loudness_usb_volume_changed(-25 * 256);
    int differs = 0;
    for (int i = 0; i < 200; i++) {
        int64_t in = (i & 1) ? 100000 : -100000;
        int64_t out = loudness_24bit_wrapper(in);
        if (out != in) differs = 1;
    }
    assert(differs);
    printf("test_loudness_24bit_processing passed\n");
}

void test_loudness_update_active_equalizer_step_uncompressed_18dbfs(void) {
    printf("Running test_loudness_update_active_equalizer_step (Uncompressed -18 dBFS RMS)...\n");
    loudness_init();
    loudness_set_source_has_volume_control();

    loudness_usb_volume_changed(0);
    printf("  [0 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX);

    loudness_usb_volume_changed(-10 * 256);
    printf("  [-10 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX - 10);

    loudness_usb_volume_changed(-20 * 256);
    printf("  [-20 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX - 20);

    loudness_usb_volume_changed(-30 * 256);
    printf("  [-30 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX - 30);

    printf("test_loudness_update_active_equalizer_step_uncompressed_18dbfs passed\n\n");
}

void test_loudness_update_active_equalizer_step_compressed_6dbfs(void) {
    printf("Running test_loudness_update_active_equalizer_step (Max Compressed -6 dBFS RMS)...\n");
    loudness_init();
    loudness_set_source_has_volume_control();

    loudness_usb_volume_changed(0);
    printf("  [0 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX);

    loudness_usb_volume_changed(-10 * 256);
    printf("  [-10 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX - 10);

    loudness_usb_volume_changed(-20 * 256);
    printf("  [-20 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX - 20);

    loudness_usb_volume_changed(-30 * 256);
    printf("  [-30 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX - 30);

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

void test_loudness_apply_noise_shaper(void) {
    /* Siden vi har valgt bort 2nd-order noise shaper, tester vi nå
     * den rene, raske nedskaleringen (DOWNSAMPLE_24BIT) i stedet. */
    printf("Running test_loudness_downsample_24bit...\n");

    int32_t high_res_sample = 0x12345678; // Et full-scale internt 32-bit signal

    // Makroen din gjør et rått skift med >> 8
    int32_t out = DOWNSAMPLE_24BIT(high_res_sample);

    // Verifiser at de laveste 8 bitene (0x78) kastes trygt bort, og at vi sitter igjen med 24-bit lyd
    assert(out == 0x123456);

    printf("test_loudness_downsample_24bit passed\n");
}

void test_saturate_24bit_s64_to_s32() {
    printf("Running test_saturate_24bit_s64_to_s32...\n");
    assert(saturate_24bit_s64_to_s32(8388607LL) == 8388607);
    assert(saturate_24bit_s64_to_s32(8388608LL) == 8388607);
    assert(saturate_24bit_s64_to_s32(-8388608LL) == -8388608);
    assert(saturate_24bit_s64_to_s32(-8388609LL) == -8388608);
    printf("test_saturate_24bit_s64_to_s32 passed\n");
}

void test_saturate_24bit_s32_to_s32(void) {
    printf("Running test_saturate_24bit_s32_to_s32...\n");
    assert(saturate_24bit_s32_to_s32(8388607) == 8388607);
    assert(saturate_24bit_s32_to_s32(8388608) == 8388607);
    assert(saturate_24bit_s32_to_s32(-8388608) == -8388608);
    assert(saturate_24bit_s32_to_s32(-8388609) == -8388608);
    printf("test_saturate_24bit_s32_to_s32 passed\n");
}

void test_saturate_24bit_s32_to_u32(void) {
    printf("Running test_saturate_24bit_s32_to_u32...\n");
    assert(saturate_24bit_s32_to_u32(8388607) == 8388607U);
    assert(saturate_24bit_s32_to_u32(-8388608) == (U32)-8388608);
    printf("test_saturate_24bit_s32_to_u32 passed\n");
}

void test_saturate_16bit_s32_to_s32(void) {
    printf("Running test_saturate_16bit_s32_to_s32...\n");
    assert(saturate_16bit_s32_to_s32(50000) == INT16_MAX);
    assert(saturate_16bit_s32_to_s32(-50000) == INT16_MIN);
    assert(saturate_16bit_s32_to_s32(1000) == 1000);
    printf("test_saturate_16bit_s32_to_s32 passed\n");
}

void test_loudness_get_gain_dbfs() {
    printf("Running test_loudness_get_gain_dbfs...\n");
    loudness_set_source_has_volume_control();
    loudness_usb_volume_changed(0);
    assert(loudness_get_gain_dbfs() == 0);

    loudness_usb_volume_changed(-256 * 10);
    assert(loudness_get_gain_dbfs() == -10);

    loudness_usb_volume_changed(VOL_MIN);
    assert(loudness_get_gain_dbfs() == -60);

    loudness_usb_volume_changed(VOL_MAX);
    assert(loudness_get_gain_dbfs() == 0);
    printf("test_loudness_get_gain_dbfs passed\n");
}



/* Test for Use Case A og B: Verifiser 16-bit CD-audio fortegnsbevaring og prosessering */
void test_loudness_16bit_cd_audio_processing(void) {
    printf("Running test_loudness_16bit_cd_audio_processing...\n");
    loudness_init();

    loudness_usb_volume_changed(-20 * 256);

    int32_t usb_container = ((int32_t)(int16_t)-4000) << 16;

    int32_t filtered_container = loudness_filter_16bit_container(0,usb_container);

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
        (void)loudness_filter_16bit_container(0,usb_container);
    }

    int32_t filtered_container = loudness_filter_16bit_container(0,usb_container);
    int32_t amp = (int16_t)(filtered_container >> 16);

    assert((filtered_container & 0xFFFF) == 0);
    assert(filtered_container == (amp << 16));
    assert(amp >= INT16_MIN && amp <= INT16_MAX);

    /* Do not compare this with a second direct loudness_fast_24bit() call:
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
 * @brief Test for Use Case A: 16-bit Sign Extension Verification
 *
 * When 16-bit signed audio is packed inside an unsigned 32-bit container from USB,
 * negative half-waves have their MSB set to 1. The upsampling macros must perform
 * a proper signed cast first to ensure the upper bits are filled with 1s (sign extension).
 * If this fails, negative numbers turn into massive positive values, creating catastrophic distortion.
 */
void test_loudness_16bit_sign_extension(void) {
    printf("Running test_loudness_16bit_sign_extension...\n");

    /* Simulate a negative 16-bit CD-audio sample (e.g., -4000) packed inside a U32 container */
    uint16_t raw_negative_16bit = (uint16_t)-4000;
    uint32_t usb_container_sample = (uint32_t)raw_negative_16bit;

    /* Verify upsampling macro (32-bit output) */
    int32_t sample_32 = UPSAMPLE_16BIT_32(usb_container_sample);
    /* A negative input MUST remain a properly sign-extended negative input */
    assert(sample_32 == -4000 * 65536LL || sample_32 == (-4000 << 16));
    assert(sample_32 < 0);

    /* Verify 64-bit upsampling macro */
    int64_t sample_64 = UPSAMPLE_16BIT_64(usb_container_sample);
    assert(sample_64 == -4000 * 65536LL || sample_64 == ((int64_t)-4000 << 16));
    assert(sample_64 < 0);

    printf("test_loudness_16bit_sign_extension passed\n\n");
}

/**
 * @brief Test for 24-bit sign extension in UPSAMPLE_24BIT macros
 *
 * 24-bit UAC samples are packed in the lower 24 bits of a 32-bit container.
 * Negative half-waves have bit 23 set but bits 24-31 are zero unless explicitly
 * sign-extended. The upsampling macros must normalize before scaling.
 */
void test_loudness_24bit_sign_extension(void) {
    printf("Running test_loudness_24bit_sign_extension...\n");

    /* -4000 in 24-bit two's complement: lower 24 bits are 0xFFF060 */
    uint32_t usb_container_sample = 0x00FFF060U;

    int32_t sample_32 = UPSAMPLE_24BIT_32(usb_container_sample);
    assert(sample_32 == (-4000 << 8));
    assert(sample_32 < 0);

    int64_t sample_64 = UPSAMPLE_24BIT_64(usb_container_sample);
    assert(sample_64 == ((int64_t)-4000 << 8));
    assert(sample_64 < 0);

    printf("test_loudness_24bit_sign_extension passed\n\n");
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
    
    S32 output = loudness_filter_24bit_container(0,input_negative);

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

    S32 out_pos = loudness_filter_24bit_container(0,max_pos);
    S32 out_neg = loudness_filter_24bit_container(0,max_neg);

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
        S32 out = loudness_filter_24bit_container(0,0);
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
    loudness_usb_volume_changed(0);

    /* The full filter must preserve container alignment and sign. */
    assert(loudness_filter_24bit_container(0,0) == 0);
    {
        int32_t positive = loudness_filter_24bit_container(0,123456 << 8);
        loudness_fast_reset_states();
        int32_t negative = loudness_filter_24bit_container(0,-123456 << 8);
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
    loudness_usb_volume_changed(0);

    int32_t prev = loudness_filter_24bit_container(0,100 << 8);
    int32_t at_zero = loudness_filter_24bit_container(0,0);
    int32_t next = loudness_filter_24bit_container(0,-100 << 8);

    /* Exact-zero input remains container-aligned while state advances. */
    assert((prev & 0xFF) == 0);
    assert((at_zero & 0xFF) == 0);
    assert((next & 0xFF) == 0);

    printf("test_loudness_24bit_container_zero_crossing passed\n\n");
}

void test_loudness_df2_step_transition_no_reset(void) {
    printf("Running test_loudness_df2_step_transition_no_reset...\n");
    int i;
    int32_t sample = 200000;
    int32_t out_before;
    int32_t out_after;
    int32_t spike;

    loudness_init();
    loudness_set_source_has_volume_control();

    loudness_usb_volume_changed(-10 * 256);
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX - 10);

    for (i = 0; i < 256; i++) {
        loudness_fast_24bit(0, sample);
    }
    out_before = (int32_t)loudness_fast_24bit(0, sample);

    loudness_usb_volume_changed(-20 * 256);
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX - 20);

    out_after = (int32_t)loudness_fast_24bit(0, sample);
    spike = out_after - out_before;
    if (spike < 0) {
        spike = -spike;
    }
    assert(spike < (sample >> 2));

    printf("test_loudness_df2_step_transition_no_reset passed\n\n");
}


/**
 * @brief Test for Use Case B: Quantization Noise & Dither Verification
 *
 * Digitally attenuating 16-bit audio by -30 dB strips away bit depth.
 * The 2nd-order noise shaper with TPDF dither must break this signal-error correlation.
 * This test verifies that the error accumulator tracks and alternates the sign of
 * the quantization error, pushing the noise energy effectively out of the audio band.
 */
void test_loudness_dither_and_noise_shaping(void) {
    printf("Running test_loudness_dither_and_noise_shaping...\n");

    int32_t error_accumulator_state = 0;

    /* Simulate a highly vulnerable static low-level signal (truncation boundary stress) */
    int32_t low_level_attenuated_sample = 0x00123456;

    /* First processing step */
    int32_t out1 = loudness_apply_noise_shaper_to_output(low_level_attenuated_sample, &error_accumulator_state);
    int32_t first_error = error_accumulator_state;

    /* The noise shaper MUST capture a non-zero quantization error from the 8-bit truncation */
    assert(first_error != 0);

    /* Second processing step with the exact same static input */
    int32_t out2 = loudness_apply_noise_shaper_to_output(low_level_attenuated_sample, &error_accumulator_state);

    /* A working 2nd-order high-pass noise shaper modulates the error dynamically.
     * The error state MUST change after the second sample due to the feedback coefficients [2, -1]
     * and the injected pseudo-random TPDF dither. */
    assert(error_accumulator_state != first_error);

    printf("test_loudness_dither_and_noise_shaping passed\n\n");
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
    assert(loudness_test_get_equalizer_step(350) == 0);
    assert(loudness_test_get_equalizer_step(355) == 1);
    assert(loudness_test_get_equalizer_step(550) == 40);
    assert(loudness_test_get_equalizer_step(800) == 90);
    assert(loudness_test_get_equalizer_step(950) == 120);
    printf("test_loudness_get_equalizer_step_121_levels passed\n\n");
}

void test_loudness_80_phon_baked_volume_filter(void) {
    printf("Running test_loudness_80_phon_baked_volume_filter...\n");
    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_usb_volume_changed(-15 * 256);
    current_freq.frequency = 48000;
    loudness_change_frequency_fast(48000);
    loudness_fast_reset_states();
    assert(loudness_test_get_equalizer_step(last_db_spl_x10) == 90);

    int64_t sample = 123456;
    int64_t result = loudness_24bit_wrapper(sample);
    assert(result > 0);
    assert(result < sample);
    printf("test_loudness_80_phon_baked_volume_filter passed\n\n");
}

void test_loudness_equalizer_step_half_db_boundaries(void) {
    printf("Running test_loudness_equalizer_step_half_db_boundaries...\n");
    loudness_init();
    last_db_spl = LOUDNESS_REF_PHON;
    last_db_spl_x10 = LOUDNESS_REF_PHON * 10;
    assert(loudness_test_should_change_equalizer_step(800) == FALSE);
    assert(loudness_test_should_change_equalizer_step(805) == TRUE);
    assert(loudness_test_should_change_equalizer_step(795) == TRUE);
    printf("test_loudness_equalizer_step_half_db_boundaries passed\n\n");
}

void test_loudness_bass_boost_default_enabled(void) {
    printf("Running test_loudness_bass_boost_default_enabled...\n");
    loudness_init();
    assert(loudness_bass_boost_is_enabled() == TRUE);
    printf("test_loudness_bass_boost_default_enabled passed\n\n");
}

void test_loudness_bass_boost_mirror_and_gate(void) {
    printf("Running test_loudness_bass_boost_mirror_and_gate...\n");
    const int neutral_step_at_80_phon =
        (LOUDNESS_REF_PHON * 10 - LOUDNESS_MIN_PHON_X10)
        / LOUDNESS_EQUALIZER_STEP_X10;

    loudness_init();
    loudness_set_source_has_volume_control();

    loudness_bass_boost_set(FALSE);
    assert(loudness_bass_boost_is_enabled() == FALSE);

    loudness_usb_volume_changed(-20 * 256);
    loudness_update_active_equalizer_step();
    assert((int)last_db_spl_x10 == LOUDNESS_REF_PHON * 10);
    assert(loudness_test_get_equalizer_step(last_db_spl_x10) == neutral_step_at_80_phon);

    loudness_bass_boost_set(TRUE);
    assert(loudness_bass_boost_is_enabled() == TRUE);
    loudness_usb_volume_changed(-20 * 256);
    loudness_update_active_equalizer_step();
    assert(loudness_test_get_equalizer_step(last_db_spl_x10) < neutral_step_at_80_phon);

    loudness_bass_boost_set(TRUE);
    printf("test_loudness_bass_boost_mirror_and_gate passed\n\n");
}

void test_loudness_bass_boost_facade_ignores_filter_activity(void) {
    printf("Running test_loudness_bass_boost_facade_ignores_filter_activity...\n");
    const int max_phon_step =
        (LOUDNESS_MAX_PHON_X10 - LOUDNESS_MIN_PHON_X10)
        / LOUDNESS_EQUALIZER_STEP_X10;

    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_bass_boost_set(TRUE);

    loudness_usb_volume_changed(0);
    loudness_update_active_equalizer_step();
    assert(loudness_test_get_equalizer_step(last_db_spl_x10) == max_phon_step);
    assert(loudness_bass_boost_is_enabled() == TRUE);

    loudness_bass_boost_set(FALSE);
    assert(loudness_bass_boost_is_enabled() == FALSE);

    loudness_bass_boost_set(TRUE);
    printf("test_loudness_bass_boost_facade_ignores_filter_activity passed\n\n");
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
    { 55, 40, -29.624143, -29.624619 },
    { 57, 44, -28.436874, -28.437249 },
    { 59, 48, -27.253678, -27.254029 },
    { 61, 52, -26.074340, -26.074662 },
    { 63, 56, -24.898314, -24.898581 },
    { 65, 60, -23.725323, -23.725547 },
    { 67, 64, -22.555126, -22.555293 },
    { 69, 68, -21.387410, -21.387553 },
    { 71, 72, -20.221946, -20.222061 },
    { 73, 76, -19.058543, -19.058627 },
    { 75, 80, -17.896947, -17.897044 },
    { 77, 84, -16.737059, -16.737106 },
    { 79, 88, -15.578649, -15.578707 },
};

static double measure_fast_50hz_gain_db(
    uint32_t sample_rate_hz, int equalizer_step)
{
    double input_sum_squares = 0.0;
    double output_sum_squares = 0.0;
    double input_rms;
    double output_rms;
    int i;

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
            loudness_filter_24bit_container(0,input_container);
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
    biquad_state_fast_t ref_final_state;
    biquad_state_fast_t trans_final_state;

    int32_t reference_outputs[LOUDNESS_TRANSITION_TEST_SAMPLES];
    int32_t transition_outputs[LOUDNESS_TRANSITION_TEST_SAMPLES];

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
        loudness_filter_24bit_container(0,input_container);
    }

    loudness_test_get_fast_channel(0, &state_at_switch, NULL);

    loudness_fast_select_equalizer_steps(600, step_to, step_to);

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        int32_t input_container = (int32_t)((uint32_t)input_24 << 8);
        int32_t output_container =
            loudness_filter_24bit_container(0,input_container);
        transition_outputs[i] = output_container >> 8;
    }

    loudness_test_get_fast_channel(0, &trans_final_state, NULL);

    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(step_to);
    loudness_test_set_fast_channel(0, &state_at_switch);

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        int32_t input_container = (int32_t)((uint32_t)input_24 << 8);
        int32_t output_container =
            loudness_filter_24bit_container(0,input_container);
        reference_outputs[i] = output_container >> 8;
    }

    loudness_test_get_fast_channel(0, &ref_final_state, NULL);

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE + 1;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        assert(transition_outputs[i] == reference_outputs[i]);
    }

    assert(trans_final_state.w1 == ref_final_state.w1);
    assert(trans_final_state.w2 == ref_final_state.w2);
}

void test_loudness_all_curve_transitions_glitchfree(void)
{
    size_t num_cases = sizeof(bass_boost_test_cases) /
        sizeof(bass_boost_test_cases[0]);
    size_t c;

    printf("Running test_loudness_all_curve_transitions_glitchfree...\n");
    fflush(stdout);

    for (c = 0; c < num_cases - 1; c++) {
        int step_current = bass_boost_test_cases[c].equalizer_step;
        int step_next = bass_boost_test_cases[c + 1].equalizer_step;

        printf("  Testing transition from step %d to %d...\n",
            step_current, step_next);
        fflush(stdout);

        assert_filter_transition_equivalence(44100, step_current, step_next);
        assert_filter_transition_equivalence(48000, step_current, step_next);
    }

    printf("  All curve transitions verified bit-exact!\n");
    fflush(stdout);
    printf("test_loudness_all_curve_transitions_glitchfree passed\n\n");
}

/*
 * Lock exact integer biquad output for step 0 at 48 kHz (zero initial state,
 * sequential samples on channel 0). Re-capture literals after coefficient or
 * fixed-point kernel changes by running loudness_fast_24bit(0, ...) in a scratch
 * program with the same setup below.
 */
void test_loudness_fast_biquad_exact_samples(void)
{
    int32_t output0;
    int32_t output1;
    int32_t output2;

    printf("Running test_loudness_fast_biquad_exact_samples...\n");

    loudness_init();
    loudness_change_frequency_fast(48000);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(0);

    output0 = loudness_fast_24bit(0, 2097152);
    output1 = loudness_fast_24bit(0, 2097152);
    output2 = loudness_fast_24bit(0, 0);
    printf("  Q4.28 exact outputs: %d, %d, %d\n",
        output0, output1, output2);
    assert(output0 == 2155);
    assert(output1 == 2272);
    assert(output2 == 232);

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

void test_loudness_hires_stride4_counter_phase(void)
{
    S32 packet_L[7];
    S32 packet_R[7];
    int i;

    printf("Running test_loudness_hires_stride4_counter_phase...\n");

    current_freq.frequency = 176400;
    loudness_change_frequency_fast(176400);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(10);

    for (i = 0; i < 7; i++) {
        packet_L[i] = 1000 << 16;
        packet_R[i] = 1000 << 16;
    }

    loudness_filter_16bit_stereo_packet(packet_L, packet_R, 7);
    assert(loudness_highres_test_channel_state(0)->sample_counter == 3);
    assert(loudness_highres_test_channel_state(1)->sample_counter == 3);

    printf("test_loudness_hires_stride4_counter_phase passed\n\n");
}

void test_loudness_hires_stride2_counter_phase(void)
{
    S32 packet_L[3];
    S32 packet_R[3];
    int i;

    printf("Running test_loudness_hires_stride2_counter_phase...\n");

    current_freq.frequency = 88200;
    loudness_change_frequency_fast(88200);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(10);

    for (i = 0; i < 3; i++) {
        packet_L[i] = 1000 << 16;
        packet_R[i] = 1000 << 16;
    }

    loudness_filter_16bit_stereo_packet(packet_L, packet_R, 3);
    assert(loudness_highres_test_channel_state(0)->sample_counter == 1);
    assert(loudness_highres_test_channel_state(1)->sample_counter == 1);

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
    loudness_change_frequency_fast(176400);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(10);

    for (i = 0; i < 49; i++) {
        double phase = 2.0 * SINE_TEST_PI * 50.0 * (double)i / 176400.0;
        int32_t s16 = (int32_t)lrint((32767.0 * 0.25) * sin(phase));

        packet_a[i] = s16 << 16;
        packet_b[i] = s16 << 16;
    }

    loudness_filter_16bit_stereo_packet(packet_a, packet_b, 49);
    loudness_filter_16bit_stereo_packet(packet_b, packet_b, 49);

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

    ref_gain_44100 = measure_fast_50hz_gain_db_stereo_packet(44100, 80);
    ref_gain_48000 = measure_fast_50hz_gain_db_stereo_packet(48000, 80);

    gain_halfrate = measure_fast_50hz_gain_db_stereo_packet(88200, 80);
    printf("  half-rate 88.2 kHz packet gain=%.3f dB, "
        "reference 44.1 kHz gain=%.3f dB\n",
        gain_halfrate, ref_gain_44100);
    fflush(stdout);
    assert(fabs(gain_halfrate - ref_gain_44100) <=
        HIRES_HALF_DELTA_GAIN_TOLERANCE_DB);

    gain_176 = measure_fast_50hz_gain_db_stereo_packet(176400, 80);
    assert(fabs(gain_176 - ref_gain_44100) <=
        HIRES_HALF_DELTA_GAIN_TOLERANCE_DB);

    gain_192 = measure_fast_50hz_gain_db_stereo_packet(192000, 80);
    printf("  192 kHz packet gain=%.3f dB, reference 48 kHz gain=%.3f dB\n",
        gain_192, ref_gain_48000);
    fflush(stdout);
    assert(fabs(gain_192 - ref_gain_48000) <=
        HIRES_HALF_DELTA_GAIN_TOLERANCE_DB);

    printf("test_loudness_hires_halfrate_delta_near_fullrate passed\n\n");
}

#define IDLE_DRAIN_MAX_SAMPLES 96000

static void drain_zeros_24bit_channel(int channel)
{
    int i;
    biquad_state_fast_t st;

    for (i = 0; i < IDLE_DRAIN_MAX_SAMPLES; i++) {
        (void)loudness_filter_24bit_container(channel, 0);
        if (loudness_channel_filter_is_idle(channel)) {
            return;
        }
    }
    loudness_test_get_fast_channel(channel, &st, NULL);
    (void)st;
    assert(false && "24-bit channel did not reach idle");
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
        if (loudness_channel_filter_is_idle(0)
            && loudness_channel_filter_is_idle(1)) {
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
    loudness_usb_volume_changed(0);

    current_freq.frequency = 44100;
    loudness_change_frequency_fast(44100);
    loudness_fast_reset_states();

    assert(loudness_filter_24bit_container(0, 0) == 0);

    assert(loudness_channel_filter_is_idle(0));
    assert(!loudness_filter_is_active());

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
    assert(!loudness_channel_filter_is_idle(0));
    assert(!loudness_channel_filter_is_idle(1));
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
    loudness_change_frequency_fast(88200);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(80);

    for (i = 0; i < 8; i++) {
        packet_L[i] = 0;
        packet_R[i] = 0;
    }
    loudness_filter_16bit_stereo_packet(packet_L, packet_R, 8);

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
    loudness_change_frequency_fast(192000);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(80);

    for (i = 0; i < 16; i++) {
        packet_L[i] = 0;
        packet_R[i] = 0;
    }
    loudness_filter_16bit_stereo_packet(packet_L, packet_R, 16);

    assert(loudness_channel_filter_is_idle(0));
    assert(loudness_channel_filter_is_idle(1));
    assert(!loudness_filter_is_active());

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

    current_freq.frequency = 88200;
    loudness_change_frequency_fast(88200);
    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(10);

    for (i = 0; i < 8; i++) {
        packet_L[i] = (1000 + i * 100) << 16;
        packet_R[i] = 0;
    }

    loudness_test_get_fast_channel(1, &r_state_before, NULL);
    assert(loudness_channel_filter_is_idle(1));

    loudness_filter_16bit_stereo_packet(packet_L, packet_R, 8);
    loudness_test_get_fast_channel(1, &r_state_after, NULL);

    assert(r_state_before.w1 == r_state_after.w1);
    assert(r_state_before.w2 == r_state_after.w2);
    assert(loudness_highres_test_channel_state(1)->sample_counter == 0);
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

    assert(l_state.w1 != 0 || l_state.w2 != 0);
    assert(r_state.w1 == 0 && r_state.w2 == 0);

    printf("test_per_channel_independent_biquad passed\n\n");
}

void test_per_channel_baked_volume_policy(void)
{
    biquad_state_fast_t state;
    biquad_quotients_fast_t left;
    biquad_quotients_fast_t right;
    const uint32_t independent_rates[] = { 44100, 48000, 88200, 96000 };
    const uint32_t shared_rates[] = { 176400, 192000 };
    size_t i;

    printf("Running test_per_channel_baked_volume_policy...\n");

    spk_vol_usb_L = -6 * 256;
    spk_vol_usb_R = -20 * 256;
    for (i = 0; i < sizeof(independent_rates) / sizeof(independent_rates[0]); i++) {
        current_freq.frequency = independent_rates[i];
        loudness_change_frequency_fast(independent_rates[i]);
        loudness_usb_volume_changed(spk_vol_usb_L);
        loudness_usb_volume_changed_right(spk_vol_usb_R);
        loudness_test_get_fast_channel(0, &state, &left);
        loudness_test_get_fast_channel(1, &state, &right);
        assert(left.b0 != right.b0);
    }

    for (i = 0; i < sizeof(shared_rates) / sizeof(shared_rates[0]); i++) {
        current_freq.frequency = shared_rates[i];
        loudness_change_frequency_fast(shared_rates[i]);
        loudness_usb_volume_changed(spk_vol_usb_L);
        loudness_usb_volume_changed_right(spk_vol_usb_R);
        loudness_test_get_fast_channel(0, &state, &left);
        loudness_test_get_fast_channel(1, &state, &right);
        assert(left.a1 == right.a1);
        assert(left.a2 == right.a2);
        assert(left.b0 == right.b0);
        assert(left.b1 == right.b1);
        assert(left.b2 == right.b2);
    }

    printf("test_per_channel_baked_volume_policy passed\n\n");
}

int main() {
    test_loudness_init();
    test_loudness_24bit_processing();
    test_loudness_update_active_equalizer_step_uncompressed_18dbfs();
    test_loudness_update_active_equalizer_step_compressed_6dbfs();
    test_usb_volume_format();
    test_loudness_apply_noise_shaper();
    test_saturate_24bit_s64_to_s32();
    test_saturate_24bit_s32_to_s32();
    test_saturate_24bit_s32_to_u32();
    test_saturate_16bit_s32_to_s32();
    test_loudness_get_gain_dbfs();
    test_loudness_16bit_cd_audio_processing();
    test_loudness_16bit_container_no_int32_overflow();
    test_loudness_intersample_peak_saturation();
    test_digital_volume_mute();

    test_loudness_16bit_sign_extension();
    test_loudness_24bit_sign_extension();
    test_loudness_24bit_container_round_trip();
    test_loudness_24bit_container_zero_crossing();
    test_loudness_df2_step_transition_no_reset();

    test_container_sign_preservation();
    test_full_scale_boundaries();
    test_dc_silence_response();
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
    test_loudness_hires_stride2_counter_phase();
    test_loudness_hires_stride4_counter_phase();
    test_loudness_hires_packet_boundary_continuity();
    test_loudness_hires_halfrate_delta_near_fullrate();
    test_filter_idle_after_zeros_base();
    test_filter_active_during_hires_interp();
    test_filter_idle_after_zeros_stride2();
    test_filter_idle_after_zeros_stride4();
    test_per_channel_zero_bypass();
    test_per_channel_independent_biquad();
    test_per_channel_baked_volume_policy();
    test_loudness_fast_biquad_exact_samples();
    test_loudness_dither_and_noise_shaping();
    test_loudness_get_equalizer_step_121_levels();
    test_loudness_80_phon_baked_volume_filter();
    test_loudness_equalizer_step_half_db_boundaries();
    test_loudness_bass_boost_default_enabled();
    test_loudness_bass_boost_mirror_and_gate();
    test_loudness_bass_boost_facade_ignores_filter_activity();
    printf("\nAll tests completed!\n");
    return 0;
}
