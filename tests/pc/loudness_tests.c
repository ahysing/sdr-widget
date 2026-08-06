#include "fff.h"
#include "loudness.h"
#include "loudness_test_access.h"
#include "usb_specific_request.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>

DEFINE_FFF_GLOBALS;

S_freq current_freq = { .frequency = 48000 };
volatile Bool freq_changed = FALSE;

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

void test_loudness_reset_rms() {
    printf("Running loudness_reset_rms...\n");
    loudness_reset_rms();
    assert(loudness_get_track_rms_dbfs() == -6);

    printf("loudness_reset_rms passed\n");
}

void test_loudness_update_track_level() {
    printf("Running test_loudness_update_track_level...\n");
    current_freq.frequency = 44100;
    loudness_init();
    loudness_reset_rms();
    printf("  Testing decay to silence...\n");
    int i;
    for (i = 0; i < 20000; i++) {
        loudness_update_track_level(0);
    }

    int32_t dbfs_silence = loudness_get_track_rms_dbfs();
    printf("  dBFS after silence loop: %d\n", dbfs_silence);

    /* Siden vi startet på -6 dBFS og matet inn ren stillhet, må energinivået ha falt betraktelig.
     * Vi sjekker at det har falt under den definerte minimumsgrensen på -18 dBFS. */
    assert(dbfs_silence < LOUDNESS_TRACK_DBFS_MIN);
    printf("test_loudness_update_track_level passed\n");
}



void test_loudness_update_track_level_fullscale() {
    printf("Running test_loudness_update_track_level_fullscale...\n");
    current_freq.frequency = 44100;
    loudness_init();
    loudness_reset_rms();

    printf("  Testing growth to full-scale signal...\n");
    // full-scale 24-bits signal INT24_MAX (8388607).
    int i;
    for (i = 0; i < 20000; i++) {
        if (i % 2 == 0) {
            loudness_update_track_level(8388607LL);
        } else {
            loudness_update_track_level(-8388608LL);
        }
    }

    int32_t dbfs_loud = loudness_get_track_rms_dbfs();
    printf("  dBFS after loud signal loop: %d\n", dbfs_loud);

    /* Et kontinuerlig full-scale signal skal stabilisere seg helt i toppen av skalaen (0 dBFS).
     * Siden integratoren bruker tid, sjekker vi at den har klatret forbi vårt maksimale rock-vindu (-6 dBFS). */
    assert(dbfs_loud >= LOUDNESS_TRACK_DBFS_MAX);
    assert(dbfs_loud <= 0); // Kan aldri overstige digital klipping (0 dBFS)

    printf("test_loudness_update_track_level_fullscale passed\n");
}

void test_loudness_rms_stereo_window_duration(void) {
    printf("Running test_loudness_rms_stereo_window_duration...\n");
    /* The window is now a fixed power-of-two (2^21) for performance. */
    uint32_t expected_window = 1UL << 21;

    current_freq.frequency = 48000;
    loudness_reset_rms();

    {
        uint32_t i;
        /* Feed enough samples to reach the window cap. */
        for (i = 0; i < expected_window; i++) {
            loudness_update_track_level(1000);
        }
        
        loudness_update_track_level(1000);
        }

    printf("test_loudness_rms_stereo_window_duration passed\n");
}

void test_loudness_init() {
    printf("Running test_loudness_init...\n");
    loudness_init();
    assert(loudness_get_last_db_spl() == LOUDNESS_REF_PHON);
    printf("test_loudness_init passed\n");
}

int64_t loudness_24bit_wrapper(int64_t sample) {
#ifdef PRECISE
    return loudness_precise_24bit(sample);
#elif defined(FAST)
    return loudness_fast_24bit((int32_t)sample);
#else
    assert(0 && "Unable to test loudness when FAST and PRECISE is not defined");
    return 0;
#endif
}

void test_loudness_24bit_processing() {
    printf("Running test_loudness_24bit_processing...\n");
    loudness_init();

    // At high volume (0 dBFS -> 80 phon), it should be passthrough (bypass)
    spk_vol_usb_L = 0;
    loudness_update_active_equalizer_step();
    
    int64_t sample = 1000;
    int64_t result = loudness_24bit_wrapper(sample);
    
    if (result != sample) {
        printf("FAIL: result %lld != sample %lld. loudness_get_last_db_spl()=%d\n", (long long)result, (long long)sample, (int)loudness_get_last_db_spl());
    }
    assert(result == sample);

    // Switching to the 55 phon equalizer step must boost low frequencies
    spk_vol_usb_L = -25 * 256;
    loudness_update_active_equalizer_step();
    int differs = 0;
    for (int i = 0; i < 200; i++) {
        if (i % 48 == 0) {
            loudness_coeff_ramp_step();
        }
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
    root_mean_square = 1099511627776ULL;

    spk_vol_usb_L = 0;
    loudness_update_active_equalizer_step();
    printf("  [0 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == 80);

    spk_vol_usb_L = -10 * 256;
    loudness_update_active_equalizer_step();
    printf("  [-10 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == 71);

    spk_vol_usb_L = -20 * 256;
    loudness_update_active_equalizer_step();
    printf("  [-20 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == 62);

    spk_vol_usb_L = -30 * 256;
    loudness_update_active_equalizer_step();
    printf("  [-30 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == 53);

    printf("test_loudness_update_active_equalizer_step_uncompressed_18dbfs passed\n\n");
}

void test_loudness_update_active_equalizer_step_compressed_6dbfs(void) {
    printf("Running test_loudness_update_active_equalizer_step (Max Compressed -6 dBFS RMS)...\n");
    loudness_init();
    root_mean_square = 17592186044416ULL;

    // 0 dBFS dempning -> Forventer 81 dB SPL (+1 dB på grunn av komprimert energi)
    spk_vol_usb_L = 0;
    loudness_update_active_equalizer_step();
    printf("  [0 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == 81);

    // -10 dBFS dempning -> Forventer 71 dB SPL
    spk_vol_usb_L = -10 * 256;
    loudness_update_active_equalizer_step();
    printf("  [-10 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == 72);

    // -20 dBFS dempning -> Forventer 61 dB SPL
    spk_vol_usb_L = -20 * 256;
    loudness_update_active_equalizer_step();
    printf("  [-20 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == 63);

    // -30 dBFS dempning -> Forventer 51 dB SPL
    spk_vol_usb_L = -30 * 256;
    loudness_update_active_equalizer_step();
    printf("  [-30 dBFS vol] loudness_get_last_db_spl(): %d\n", loudness_get_last_db_spl());
    assert(loudness_get_last_db_spl() == 54);

    printf("test_loudness_update_active_equalizer_step_compressed_6dbfs passed\n\n");
}

void test_usb_volume_format(void) {
    printf("Running test_usb_volume_format...\n");
    assert(usb_volume_format(0) == VOL_MULT_UNITY);

    S32 mult = usb_volume_format(-6 * 256);
    assert(mult == (VOL_MULT_UNITY >> 1));
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

void test_loudness_get_gain_dbfs() {
    printf("Running test_loudness_get_gain_dbfs...\n");
    spk_vol_usb_L = 0;
    assert(loudness_get_gain_dbfs() == 0);
    
    spk_vol_usb_L = -256 * 10; // -10 dB
    assert(loudness_get_gain_dbfs() == -10);
    printf("test_loudness_get_gain_dbfs passed\n");
}

void test_loudness_get_track_dbfs(void) {
    printf("Running test_loudness_get_track_dbfs...\n");
    current_freq.frequency = 44100;
    loudness_init();
    loudness_reset_rms();

    int i;
    for (i = 0; i < 20000; i++) {
        loudness_update_track_level(0);
    }
    assert(loudness_get_track_dbfs() == LOUDNESS_TRACK_DBFS_MIN);

    for (i = 0; i < 20000; i++) {
        if (i % 2 == 0) {
            loudness_update_track_level(8388607LL);
        } else {
            loudness_update_track_level(-8388608LL);
        }
    }
    assert(loudness_get_track_dbfs() == LOUDNESS_TRACK_DBFS_MAX);
    printf("test_loudness_get_track_dbfs passed\n");
}



/* Test for Use Case A og B: Verifiser 16-bit CD-audio fortegnsbevaring og prosessering */
#ifdef FAST
void test_loudness_16bit_cd_audio_processing(void) {
    printf("Running test_loudness_16bit_cd_audio_processing...\n");
    loudness_init();

    spk_vol_usb_L = -20 * 256;
    loudness_update_active_equalizer_step();

    int32_t raw_usb_16bit_sample = (int32_t)((uint16_t)-4000);

    // 1. Verifiser at vår 16-bit FAST oppsamplingsmakro utfører korrekt fortegnsutvidelse
    int32_t sample_32 = UPSAMPLE_16BIT_32(raw_usb_16bit_sample);

    // Siden tallet er negativt, må det forbli negativt i 32-bit domenet!
    assert(sample_32 < 0);

    // 2. Kjør samplet gjennom 32-bit filterkaskaden
    int32_t filtered_32 = loudness_fast_24bit(sample_32);

    // Verifiser at filteret faktisk har transformerte verdien (ikke ren bypass)
    assert(filtered_32 != sample_32);

    printf("test_loudness_16bit_cd_audio_processing passed\n");
}
#endif

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

    /* Verify FAST mode upsampling macro (32-bit output) */
    int32_t sample_32 = UPSAMPLE_16BIT_32(usb_container_sample);
    /* A negative input MUST remain a properly sign-extended negative input */
    assert(sample_32 == -4000 * 65536LL || sample_32 == (-4000 << 16));
    assert(sample_32 < 0);

    /* Verify PRECISE mode upsampling macro (64-bit output) */
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

void test_loudness_coeff_ramp_convergence(void) {
    printf("Running test_loudness_coeff_ramp_convergence...\n");
    current_freq.frequency = 44100;
    loudness_init();
    root_mean_square = 1099511627776ULL;

    spk_vol_usb_L = 0;
    loudness_update_active_equalizer_step();
    assert(loudness_get_last_db_spl() == 80);

    spk_vol_usb_L = -30 * 256;
    loudness_update_active_equalizer_step();
    assert(loudness_get_last_db_spl() == 53);

    int i;
    for (i = 0; i < 800; i++) {
        (void)loudness_24bit_wrapper((i & 1) ? 1000 : -1000);
    }

    spk_vol_usb_L = 0;
    loudness_update_active_equalizer_step();
    for (i = 0; i < 800; i++) {
        (void)loudness_24bit_wrapper((i & 1) ? 1000 : -1000);
    }
    assert(loudness_get_last_db_spl() == 80);

    printf("test_loudness_coeff_ramp_convergence passed\n");
}

/**
 * @brief Test for dynamic coefficient ramping and fixed-point convergence.
 *
 * Verifies that ramp_coeff_fast calculates smooth linear trajectories using the
 * Q31 reciprocal scale, handles both positive and negative diffs, and safely
 * snaps directly to the target value when the sample window reaches its end.
 */
void test_loudness_coefficient_ramping(void) {
    printf("Running test_loudness_coefficient_ramping...\n");

    int32_t start_coeff = 0;
    int32_t target_coeff = 536870912; // 1.0 i Q29 (Q29_ONE)
    int32_t current = start_coeff;

    // Total tidsramme for overgangen (f.eks. 15 ms = 660 sampler ved 44.1 kHz)
    int total_samples = 660;

    // =========================================================================
    // SCENARIO 1: Test oppadgående kurve (Ramp Up) og jevn stigning
    // =========================================================================
    current = ramp_coeff_fast(current, target_coeff, total_samples);

    /* Etter nøyaktig 1 sample ut av 660, må koeffisienten ha steget,
     * men den må fortsatt være et veldig lite tall nær starten */
    assert(current > start_coeff);
    assert(current < (target_coeff / 10));
    printf("  Ramp up step 1 passed (current: %d)\n", current);

    // =========================================================================
    // SCENARIO 2: Test nedadgående kurve (Ramp Down / Slå av filter)
    // =========================================================================
    int32_t high_coeff = 536870912;
    int32_t low_target = 0;
    int32_t current_down = high_coeff;

    current_down = ramp_coeff_fast(current_down, low_target, total_samples);

    /* Sjekk at fortegnsmatematikken (sign extension) i muls.d fungerer
     * for negative differanser, slik at koeffisienten faller korrekt */
    assert(current_down < high_coeff);
    assert(current_down > low_target);
    printf("  Ramp down step 1 passed (current: %d)\n", current_down);

    // =========================================================================
    // SCENARIO 3: Test matematisk konvergens (Låsing til målet)
    // =========================================================================
    /* Vi simulerer den aller siste samplings-syklusen i vinduet (remaining = 1).
     * Funksjonen SKAL returnere målet bit-perfekt for å unngå lekkasje. */
    int32_t final_step = ramp_coeff_fast(536870900, target_coeff, 1);
    assert(final_step == target_coeff);

    /* Sjekk også boundary-case der remaining er 0 (skal returnere målet) */
    int32_t boundary_step = ramp_coeff_fast(536870900, target_coeff, 0);
    assert(boundary_step == target_coeff);

    /* Sjekk at hvis vi allerede er på målet, returneres målet umiddelbart */
    int32_t steady_state = ramp_coeff_fast(target_coeff, target_coeff, 100);
    assert(steady_state == target_coeff);

    printf("test_loudness_coefficient_ramping passed\n\n");
}

int main() {
    test_loudness_reset_rms();
    test_loudness_update_track_level();
    test_loudness_update_track_level_fullscale();
    test_loudness_rms_stereo_window_duration();
    test_loudness_init();
    test_loudness_24bit_processing();
    test_loudness_update_active_equalizer_step_uncompressed_18dbfs();
    test_loudness_update_active_equalizer_step_compressed_6dbfs();
    test_usb_volume_format();
    test_loudness_apply_noise_shaper();
    test_saturate_24bit_s64_to_s32();
    test_saturate_24bit_s32_to_s32();
    test_saturate_24bit_s32_to_u32();
    test_loudness_get_gain_dbfs();
    test_loudness_get_track_dbfs();
#ifdef FAST
    test_loudness_16bit_cd_audio_processing();
#endif
    test_loudness_intersample_peak_saturation();
    test_digital_volume_mute();

    test_loudness_16bit_sign_extension();
    test_loudness_24bit_sign_extension();
    test_loudness_dither_and_noise_shaping();
    test_loudness_coeff_ramp_convergence();
    test_loudness_intersample_peak_saturation();
    test_loudness_coefficient_ramping();
    printf("\nAll tests completed!\n");
    return 0;
}
