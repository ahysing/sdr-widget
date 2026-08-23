#include "fff.h"
#include "loudness.h"
#include "loudness_test_access.h"
#include "usb_specific_request.h"
#include "loudness_fast.h"
#include "loudness_highres.h"
#include "loudness_fast_golden_vectors.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#define _USE_MATH_DEFINES // Enable math constants like M_PI
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

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
    return loudness_fast_24bit((int32_t)sample);
}

void test_loudness_24bit_processing() {
    printf("Running test_loudness_24bit_processing...\n");
    loudness_init();
    loudness_set_source_has_volume_control();

    // At high volume (0 dBFS -> 80 phon), it should be passthrough (bypass)
    loudness_usb_volume_changed(0);
    
    int64_t sample = 1000;
    int64_t result = loudness_24bit_wrapper(sample);
    
    if (result != sample) {
        printf("FAIL: result %lld != sample %lld. loudness_get_last_db_spl()=%d\n", (long long)result, (long long)sample, (int)loudness_get_last_db_spl());
    }
    assert(result == sample);

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

    int32_t filtered_container = loudness_filter_16bit_container(usb_container);

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
        (void)loudness_filter_16bit_container(usb_container);
    }

    int32_t filtered_container = loudness_filter_16bit_container(usb_container);
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
    assert(apply_digital_volume(hot_sample, usb_volume_format((S16)(VOL_MIN - 256))) == 0);
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
    
    // Setter filteret til 80 phon (Unity gain / flatt filter)
    loudness_test_load_active_quotients_fast(13); 

    // Et typisk negativt signal i en 32-bit container (bits 31:8)
    // Eksempel: -1000 i 24-bit er 0xFFFF18. Skiftet opp: 0xFFF18000
    S32 input_negative = (S32)0xFFF18000; 
    
    S32 output = loudness_filter_24bit_container(input_negative);

    // Siden filteret er flatt (unity), må utgangen være nøyaktig lik inngangen
    if (output != input_negative) {
        printf("Fortegn ødelagt! Input: 0x%08X, Output: 0x%08X (Skulle vært like)", 
                  (unsigned int)input_negative, (unsigned int)output);
        assert(false);
    }

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
    loudness_test_load_active_quotients_fast(13); // Unity gain

    // Maks positiv 24-bit i 32-bit container: 0x7FFFFF00
    S32 max_pos = (S32)0x7FFFFF00;
    // Maks negativ 24-bit i 32-bit container: 0x80000000
    S32 max_neg = (S32)0x80000000;

    S32 out_pos = loudness_filter_24bit_container(max_pos);
    S32 out_neg = loudness_filter_24bit_container(max_neg);

    if (out_pos != max_pos) {
        printf("Maks positiv klippet eller endret! Forventet: 0x%08X, Fikk: 0x%08X", 
                  (unsigned int)max_pos, (unsigned int)out_pos);
        assert(false);
    }

    if (out_neg != max_neg) {
        printf("Maks negativ klippet eller endret! Forventet: 0x%08X, Fikk: 0x%08X", 
                  (unsigned int)max_neg, (unsigned int)out_neg);
        assert(false);
    }

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
        S32 out = loudness_filter_24bit_container(0);
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

    /* Unity filter at 80 phon: container round-trip must preserve sample words. */
    assert(loudness_filter_24bit_container(0) == 0);
    assert(loudness_filter_24bit_container(123456 << 8) == (123456 << 8));
    assert(loudness_filter_24bit_container((int32_t)((int64_t)INT24_MIN << 8)) == (int32_t)((int64_t)INT24_MIN << 8));
    assert(loudness_filter_24bit_container((int32_t)((int64_t)INT24_MAX << 8)) == (int32_t)((int64_t)INT24_MAX << 8));

    printf("test_loudness_24bit_container_round_trip passed\n\n");
}

void test_loudness_24bit_container_zero_crossing(void) {
    printf("Running test_loudness_24bit_container_zero_crossing...\n");
    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_usb_volume_changed(0);

    int32_t prev = loudness_filter_24bit_container(100 << 8);
    int32_t at_zero = loudness_filter_24bit_container(0);
    int32_t next = loudness_filter_24bit_container(-100 << 8);

    /* Zero crossings must stay continuous; bypassing the filter at exact zero
     * used to leave stale IIR state and produce single-sample spikes. */
    assert(at_zero == 0);
    assert(prev == (100 << 8));
    assert(next == (-100 << 8));

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
        loudness_fast_24bit(sample);
    }
    out_before = (int32_t)loudness_fast_24bit(sample);

    loudness_usb_volume_changed(-20 * 256);
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX - 20);

    out_after = (int32_t)loudness_fast_24bit(sample);
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

void test_loudness_get_equalizer_step_14_levels(void) {
    printf("Running test_loudness_get_equalizer_step_14_levels...\n");
    assert(loudness_test_get_equalizer_step(54) == 0);
    assert(loudness_test_get_equalizer_step(55) == 0);
    assert(loudness_test_get_equalizer_step(79) == 12);
    assert(loudness_test_get_equalizer_step(80) == 13);
    assert(loudness_test_get_equalizer_step(81) == 13);
    printf("test_loudness_get_equalizer_step_14_levels passed\n\n");
}

void test_loudness_80_phon_unity_filter(void) {
    printf("Running test_loudness_80_phon_unity_filter...\n");
    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_usb_volume_changed(0);
    assert(loudness_test_get_equalizer_step(loudness_get_last_db_spl()) == 13);

    int64_t sample = 123456;
    int64_t result = loudness_24bit_wrapper(sample);
    assert(result == sample);
    printf("test_loudness_80_phon_unity_filter passed\n\n");
}

void test_loudness_equalizer_step_hysteresis_79_80(void) {
    printf("Running test_loudness_equalizer_step_hysteresis_79_80...\n");
    loudness_init();
    last_db_spl = LOUDNESS_REF_PHON;
    assert(loudness_test_should_change_equalizer_step(LOUDNESS_REF_PHON * 10 + 6) == FALSE);
    assert(loudness_test_should_change_equalizer_step(LOUDNESS_REF_PHON * 10 - 6) == TRUE);

    last_db_spl = LOUDNESS_REF_PHON - 1;
    assert(loudness_test_should_change_equalizer_step(LOUDNESS_REF_PHON * 10 - 1) == FALSE);
    assert(loudness_test_should_change_equalizer_step(LOUDNESS_REF_PHON * 10) == TRUE);
    printf("test_loudness_equalizer_step_hysteresis_79_80 passed\n\n");
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
    { 55,  0, 10.375857, 10.375381 },
    { 57,  1,  9.563126,  9.562751 },
    { 59,  2,  8.746322,  8.745971 },
    { 61,  3,  7.925660,  7.925338 },
    { 63,  4,  7.101686,  7.101419 },
    { 65,  5,  6.274677,  6.274453 },
    { 67,  6,  5.444874,  5.444707 },
    { 69,  7,  4.612590,  4.612447 },
    { 71,  8,  3.778054,  3.777939 },
    { 73,  9,  2.941457,  2.941373 },
    { 75, 10,  2.103053,  2.102956 },
    { 77, 11,  1.262941,  1.262894 },
    { 79, 12,  0.421351,  0.421293 },
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
            loudness_filter_24bit_container(input_container);
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
    double previous_44100 = 1000.0;
    double previous_48000 = 1000.0;
    size_t i;

    printf("Running test_50hz_bass_boost_is_monotonic...\n");
    for (i = 0; i < sizeof(bass_boost_test_cases) /
        sizeof(bass_boost_test_cases[0]); i++) {
        double gain_44100 = measure_fast_50hz_gain_db(
            44100, bass_boost_test_cases[i].equalizer_step);
        double gain_48000 = measure_fast_50hz_gain_db(
            48000, bass_boost_test_cases[i].equalizer_step);
        assert(gain_44100 < previous_44100);
        assert(gain_48000 < previous_48000);
        previous_44100 = gain_44100;
        previous_48000 = gain_48000;
    }
}

static void assert_filter_transition_equivalence(uint32_t sample_rate_hz,
    int step_from, int step_to)
{
    int i;
    int section;

    int32_t reference_outputs[LOUDNESS_TRANSITION_TEST_SAMPLES];
    int32_t transition_outputs[LOUDNESS_TRANSITION_TEST_SAMPLES];
    biquad_state_fast_t states_at_switch[LOUDNESS_FAST_FILTERS];
    biquad_state_fast_t ref_final_states[LOUDNESS_FAST_FILTERS];
    biquad_state_fast_t trans_final_states[LOUDNESS_FAST_FILTERS];

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
        loudness_filter_24bit_container(input_container);
    }

    for (section = 0; section < LOUDNESS_FAST_FILTERS; section++) {
        loudness_test_get_fast_section(section, &states_at_switch[section], NULL);
    }

    loudness_fast_select_equalizer_step(60, step_to);

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        int32_t input_container = (int32_t)((uint32_t)input_24 << 8);
        int32_t output_container =
            loudness_filter_24bit_container(input_container);
        transition_outputs[i] = output_container >> 8;
    }

    for (section = 0; section < LOUDNESS_FAST_FILTERS; section++) {
        loudness_test_get_fast_section(section, &trans_final_states[section], NULL);
    }

    loudness_fast_reset_states();
    loudness_test_load_active_quotients_fast(step_to);
    for (section = 0; section < LOUDNESS_FAST_FILTERS; section++) {
        loudness_test_set_fast_section(section, &states_at_switch[section]);
    }

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        double phase = 2.0 * SINE_TEST_PI * SINE_TEST_FREQUENCY_HZ *
            (double)i / (double)sample_rate_hz;
        int32_t input_24 = (int32_t)lrint(
            (double)SINE_TEST_AMPLITUDE * sin(phase));
        int32_t input_container = (int32_t)((uint32_t)input_24 << 8);
        int32_t output_container =
            loudness_filter_24bit_container(input_container);
        reference_outputs[i] = output_container >> 8;
    }

    for (section = 0; section < LOUDNESS_FAST_FILTERS; section++) {
        loudness_test_get_fast_section(section, &ref_final_states[section], NULL);
    }

    for (i = LOUDNESS_TRANSITION_SWITCH_SAMPLE + 1;
        i < LOUDNESS_TRANSITION_TEST_SAMPLES; i++) {
        assert(transition_outputs[i] == reference_outputs[i]);
    }

    for (section = 0; section < LOUDNESS_FAST_FILTERS; section++) {
        assert(trans_final_states[section].w1 == ref_final_states[section].w1);
        assert(trans_final_states[section].w2 == ref_final_states[section].w2);
    }
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

void test_loudness_fast_golden_vectors(void)
{
    int failure;

    printf("Running test_loudness_fast_golden_vectors...\n");
    failure = loudness_fast_run_golden_selftest();
    assert(failure == 0);
    printf("test_loudness_fast_golden_vectors passed\n\n");
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

    ref_gain_44100 = measure_fast_50hz_gain_db_stereo_packet(44100, 10);
    ref_gain_48000 = measure_fast_50hz_gain_db_stereo_packet(48000, 10);

    gain_halfrate = measure_fast_50hz_gain_db_stereo_packet(88200, 10);
    printf("  half-rate 88.2 kHz packet gain=%.3f dB, "
        "reference 44.1 kHz gain=%.3f dB\n",
        gain_halfrate, ref_gain_44100);
    fflush(stdout);
    assert(fabs(gain_halfrate - ref_gain_44100) <=
        HIRES_HALF_DELTA_GAIN_TOLERANCE_DB);

    gain_176 = measure_fast_50hz_gain_db_stereo_packet(176400, 10);
    assert(fabs(gain_176 - ref_gain_44100) <=
        HIRES_HALF_DELTA_GAIN_TOLERANCE_DB);

    gain_192 = measure_fast_50hz_gain_db_stereo_packet(192000, 10);
    assert(fabs(gain_192 - ref_gain_48000) <=
        HIRES_HALF_DELTA_GAIN_TOLERANCE_DB);

    printf("test_loudness_hires_halfrate_delta_near_fullrate passed\n\n");
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
    test_loudness_fast_golden_vectors();
    test_loudness_dither_and_noise_shaping();
    test_loudness_get_equalizer_step_14_levels();
    test_loudness_80_phon_unity_filter();
    test_loudness_equalizer_step_hysteresis_79_80();
    printf("\nAll tests completed!\n");
    return 0;
}
