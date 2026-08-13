#include "fff.h"
#include "loudness.h"
#include "loudness_test_access.h"
#include "usb_specific_request.h"
#include "usb_statistics.h"
#include "audio_stats_logic.h"
#include "usb_test_mocks.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>

DEFINE_FFF_GLOBALS;

S_freq current_freq = { .frequency = 44100 };
volatile Bool freq_changed = FALSE;

S16 spk_vol_usb_L = 0, spk_vol_usb_R = 0;
volatile U8 spk_bit_resolution = 24;

#ifdef FAST
static int64_t process_sample(int64_t sample) {
    return loudness_fast_24bit((int32_t)sample);
}
#elif defined(PRECISE)
static int64_t process_sample(int64_t sample) {
    return loudness_precise_24bit(sample);
}
#else
#error FAST or PRECISE must be defined
#endif

static void reset_usb_stats_buffers(void) {
    usb_test_mocks_reset();
    statistics_test_reset();
}

static void test_equalizer_step_switch_tagged_events_volume_sweep(void) {
    printf("Running test_equalizer_step_switch_tagged_events_volume_sweep...\n");
    reset_usb_stats_buffers();
    volatile usb_stats_t *stats = get_usb_stats();

    current_freq.frequency = 44100;
    loudness_init();
    root_mean_square = 1099511627776ULL;

    assert(stats->event_count == 0);
    assert(loudness_get_last_db_spl() == 80);

    spk_vol_usb_L = 0;
    loudness_update_active_equalizer_step();
    assert(stats->event_count == 0);

    spk_vol_usb_L = -10 * 256;
    loudness_update_active_equalizer_step();
    assert(loudness_get_last_db_spl() == 71);
    assert(stats->event_count == 1);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg0 == 80);
    assert(stats->last_arg1 == 71);
    assert(stats->last_arg2 == 8);

    spk_vol_usb_L = -20 * 256;
    loudness_update_active_equalizer_step();
    assert(loudness_get_last_db_spl() == 62);
    assert(stats->event_count == 2);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg0 == 71);
    assert(stats->last_arg1 == 62);
    assert(stats->last_arg2 == 3);

    spk_vol_usb_L = -30 * 256;
    loudness_update_active_equalizer_step();
    assert(loudness_get_last_db_spl() == 53);
    assert(stats->event_count == 3);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg0 == 62);
    assert(stats->last_arg1 == 53);
    assert(stats->last_arg2 == 0);

    spk_vol_usb_L = 0;
    loudness_update_active_equalizer_step();
    assert(loudness_get_last_db_spl() == 80);
    assert(stats->event_count == 4);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg1 == 80);
    assert(stats->last_arg2 == 13);

    assert(stats->deadline_misses == 0);
    printf("test_equalizer_step_switch_tagged_events_volume_sweep passed\n");
}

static void test_equalizer_step_switch_stats_report_and_reset(void) {
    printf("Running test_equalizer_step_switch_stats_report_and_reset...\n");
    reset_usb_stats_buffers();
    volatile usb_stats_t *stats = get_usb_stats();

    Is_device_enumerated_fake.return_val = TRUE;
    Is_usb_in_ready_fake.return_val = TRUE;

    stats->event_count = 4;
    stats->last_tag = USB_STATS_TAG_EQUALIZER_STEP_SWITCH;
    stats->last_arg0 = 62;
    stats->last_arg1 = 53;
    stats->last_arg2 = 0;
    stats->deadline_misses = 0;

    statistics_report_iteration();

    assert(Usb_write_endpoint_data_fake.call_count == 1 + (int)USB_STATS_PACKET_WIRE_SIZE);
    assert(stats->event_count == 0);
    assert(stats->last_tag == USB_STATS_TAG_NONE);
    assert(stats->deadline_misses == 0);
    printf("test_equalizer_step_switch_stats_report_and_reset passed\n");
}

static void test_equalizer_step_switch_rapid_sweep_no_deadline_misses(void) {
    printf("Running test_equalizer_step_switch_rapid_sweep_no_deadline_misses...\n");
    reset_usb_stats_buffers();
    volatile usb_stats_t *stats = get_usb_stats();

    current_freq.frequency = 48000;
    loudness_init();
    root_mean_square = 1099511627776ULL;

    const int volumes[] = { 0, -10 * 256, -20 * 256, -30 * 256, -10 * 256, 0 };
    int v;
    for (v = 0; v < 6; v++) {
        spk_vol_usb_L = (S16)volumes[v];
        loudness_update_active_equalizer_step();
        (void)process_sample(1000);
        (void)process_sample(-1000);
    }

    assert(stats->deadline_misses == 0);
    assert(stats->event_count >= 4);
    printf("test_equalizer_step_switch_rapid_sweep_no_deadline_misses passed\n");
}

int main(void) {
#ifdef FAST
    printf("=== loudness_equalizer_step_switch_stats_tests (FAST) ===\n");
#else
    printf("=== loudness_equalizer_step_switch_stats_tests (PRECISE) ===\n");
#endif
    test_equalizer_step_switch_tagged_events_volume_sweep();
    test_equalizer_step_switch_stats_report_and_reset();
    test_equalizer_step_switch_rapid_sweep_no_deadline_misses();
    printf("All loudness_equalizer_step_switch_stats_tests passed.\n");
    return 0;
}
