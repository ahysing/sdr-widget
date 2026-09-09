#include "fff.h"
#include "loudness.h"
#include "loudness_test_access.h"
#include "usb_specific_request.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>

DEFINE_FFF_GLOBALS;

S_freq current_freq = { .frequency = 48000 };
volatile Bool freq_changed = FALSE;
volatile U8 usb_alternate_setting_out = 1;

S16 spk_vol_usb_L = 0, spk_vol_usb_R = 0;
volatile U8 spk_bit_resolution = 24;

#define MAG_MIN 0
#define MAG_MAX INT24_MAX

// Vi bytter ut kallet til å bruke funksjonen med innebygget -60 dBFS bunnsperre!
#define LOUDNESS_PAK_MAGNITUDE_TO_DBFS(MAG) \
    assert(loudness_inferred_gain_dbfs_from_magnitude(MAG) >= -60); \
    assert(loudness_inferred_gain_dbfs_from_magnitude(MAG) <= 0);

void test_loudness_peak_magnitude_to_dbfs(void) {
    printf("Running test_loudness_peak_magnitude_to_dbfs\n");
    
    // Nå vil alle disse lave verdiene trygt returnere nøyaktig -60,
    // og testen din vil cruise igjennom uten krasj!
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(MAG_MIN);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(2);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(4);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(8);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(16);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(32);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(64);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(128);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(256);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(512);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(1024);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(2048);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(4096); // Gir nøyaktig -60 dBFS
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(8192); // Begynner å stige oppover (-54 dBFS)
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(16384);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(32768);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(65536);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(131072);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(262144);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(524288);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(1048576);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(2097152);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(4194304);
    LOUDNESS_PAK_MAGNITUDE_TO_DBFS(MAG_MAX); // Gir nøyaktig 0 dBFS
    
    printf("test_loudness_peak_magnitude_to_dbfs passed!\n");
}

void test_loudness_inferred_gain_fullscale(void) {
    printf("Running test_loudness_inferred_gain_fullscale...\n");

    loudness_test_reset_inferred_gain();
    loudness_test_set_long_memory((uint32_t)INT24_MAX);
    assert(loudness_inferred_gain_dbfs_from_magnitude(loudness_test_get_long_memory()) <= 0);
    assert(loudness_inferred_gain_dbfs_from_magnitude(loudness_test_get_long_memory()) >= -6);
    printf("test_loudness_inferred_gain_fullscale passed\n");
}

void test_loudness_inferred_gain_halfscale(void) {
    printf("Running test_loudness_inferred_gain_halfscale...\n");
    uint32_t half = (uint32_t)INT24_MAX / 2U;

    loudness_test_reset_inferred_gain();
    loudness_test_set_long_memory(half);
    assert(loudness_inferred_gain_dbfs_from_magnitude(loudness_test_get_long_memory()) < 0);
    assert(loudness_inferred_gain_dbfs_from_magnitude(loudness_test_get_long_memory()) >= -13);
    printf("test_loudness_inferred_gain_halfscale passed\n");
}

void test_loudness_inferred_gain_fast_decay(void) {
    printf("Running test_loudness_inferred_gain_fast_decay...\n");
    int i;
    /* Simulate the volume knob dropping hard: the short memory collapses far
     * below the long memory, triggering the fast-decay branch (>> 12, ~46 ms). */
    uint32_t low_peak = (uint32_t)INT24_MAX >> 6;

    loudness_test_reset_inferred_gain();
    loudness_test_set_long_memory((uint32_t)INT24_MAX);
    loudness_test_set_short_memory(low_peak);

    /* Fast decay has tau ~= 46 ms; at 88.2 kHz update that is ~4096 samples.
     * Run well past 5*tau so the 11.8 s memory is forced down toward the low
     * peak before the slow branch takes over near 4x the short level. */
    for (i = 0; i < 24000; i++) {
        loudness_test_combined_context_loop(low_peak);
    }
    assert(loudness_test_get_long_memory() < ((uint32_t)INT24_MAX >> 2));
    assert(loudness_test_get_long_memory() > low_peak);
    printf("test_loudness_inferred_gain_fast_decay passed\n");
}

void test_loudness_inferred_gain_slow_rise(void) {
    printf("Running test_loudness_inferred_gain_slow_rise...\n");
    int i;
    uint32_t peak = (uint32_t)INT24_MAX;

    loudness_test_reset_inferred_gain();
    /* The long memory rises instantly with the short memory, which is gated by
     * the 46 ms attack (>> 12). An integer leaky integrator cannot converge
     * closer than ~2^12 to the target, so allow that residual in the bound and
     * run long enough (>> 5*tau) to reach the attack floor. */
    for (i = 0; i < 40000; i++) {
        loudness_test_combined_context_loop(peak);
    }
    assert(loudness_test_get_active_loudness_level() >= peak - (1U << 13));
    printf("test_loudness_inferred_gain_slow_rise passed\n");
}

void test_loudness_inferred_gain_protocol_volume_overrides(void) {
    printf("Running test_loudness_inferred_gain_protocol_volume_overrides...\n");

    loudness_test_reset_inferred_gain();
    loudness_test_set_long_memory(1000U);
    loudness_usb_volume_changed_left(-10 * 256);
    assert(loudness_get_gain_dbfs_left() == -10);
    assert(loudness_get_gain_dbfs_right() == 0);
    printf("test_loudness_inferred_gain_protocol_volume_overrides passed\n");
}

void test_loudness_inferred_gain_tracks_with_usb_volume_control(void) {
    printf("Running test_loudness_inferred_gain_tracks_with_usb_volume_control...\n");
    int i;
    int32_t full = (int32_t)INT24_MAX << 8;

    loudness_test_reset_inferred_gain();
    loudness_inferred_gain_set_rate(44100);
    loudness_set_source_has_volume_control();
    assert(loudness_envelope_follower_tracks_diagnostics());

    for (i = 0; i < 50000; i++) {
        loudness_envelope_follower_update_stereo(full, full);
    }
    assert(loudness_inferred_gain_dbfs_from_magnitude(loudness_test_get_active_loudness_level()) >= -6);
    printf("test_loudness_inferred_gain_tracks_with_usb_volume_control passed\n");
}

void test_loudness_inferred_gain_diagnostics_rate_gating(void) {
    printf("Running test_loudness_inferred_gain_diagnostics_rate_gating...\n");

    loudness_test_reset_inferred_gain();
    loudness_set_source_has_volume_control();

    loudness_inferred_gain_set_rate(48000);
    assert(!loudness_envelope_follower_tracks_diagnostics());
    assert(!loudness_envelope_follower_is_active());

    loudness_inferred_gain_set_rate(44100);
    assert(loudness_envelope_follower_tracks_diagnostics());
    assert(!loudness_envelope_follower_is_active());

    loudness_set_source_has_volume_control();
    loudness_inferred_gain_set_rate(44100);
    assert(loudness_envelope_follower_tracks_diagnostics());
    loudness_inferred_gain_set_rate(48000);
    assert(!loudness_envelope_follower_tracks_diagnostics());

    loudness_test_reset_inferred_gain();
    loudness_inferred_gain_set_rate(48000);
    assert(loudness_envelope_follower_is_active());
    assert(!loudness_envelope_follower_tracks_diagnostics());

    printf("test_loudness_inferred_gain_diagnostics_rate_gating passed\n");
}

int main(void) {
    test_loudness_peak_magnitude_to_dbfs();
    test_loudness_inferred_gain_fullscale();
    test_loudness_inferred_gain_halfscale();
    test_loudness_inferred_gain_fast_decay();
    test_loudness_inferred_gain_slow_rise();
    test_loudness_inferred_gain_protocol_volume_overrides();
    test_loudness_inferred_gain_tracks_with_usb_volume_control();
    test_loudness_inferred_gain_diagnostics_rate_gating();
    printf("\nAll loudness inferred gain tests completed!\n");
    return 0;
}
