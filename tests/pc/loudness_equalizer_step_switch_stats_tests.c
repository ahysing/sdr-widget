#include "fff.h"
#include "loudness.h"
#include "loudness_test_access.h"
#include "usb_specific_request.h"
#include "usb_statistics.h"
#include "stats_telemetry.h"
#include "audio_stats_logic.h"
#include "usb_test_mocks.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>

DEFINE_FFF_GLOBALS;

S_freq current_freq = { .frequency = 44100 };
volatile Bool freq_changed = FALSE;
volatile U8 usb_alternate_setting_out = 1;

S16 spk_vol_usb_L = 0, spk_vol_usb_R = 0;
volatile U8 spk_bit_resolution = 24;

static int64_t process_sample(int64_t sample) {
    return loudness_fast_24bit((int32_t)sample);
}

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
    loudness_set_source_has_volume_control();
    root_mean_square = 1099511627776ULL;

    assert(stats->event_count == 0);
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX);

    loudness_usb_volume_changed(0);
    assert(stats->event_count == 0);

    loudness_usb_volume_changed((79 - LOUDNESS_DB_SPL_MAX) * 256);
    assert(loudness_get_last_db_spl() == 79);
    assert(stats->event_count == 1);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg0 == LOUDNESS_DB_SPL_MAX);
    assert(stats->last_arg1 == 79);
    assert(stats->last_arg2 == 12);

    loudness_usb_volume_changed((75 - LOUDNESS_DB_SPL_MAX) * 256);
    assert(loudness_get_last_db_spl() == 75);
    assert(stats->event_count == 2);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg0 == 79);
    assert(stats->last_arg1 == 75);
    assert(stats->last_arg2 == 10);

    loudness_usb_volume_changed((55 - LOUDNESS_DB_SPL_MAX) * 256);
    assert(loudness_get_last_db_spl() == 55);
    assert(stats->event_count == 3);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg0 == 75);
    assert(stats->last_arg1 == 55);
    assert(stats->last_arg2 == 0);

    loudness_usb_volume_changed(0);
    assert(loudness_get_last_db_spl() == LOUDNESS_DB_SPL_MAX);
    assert(stats->event_count == 4);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg1 == LOUDNESS_DB_SPL_MAX);
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
    loudness_set_source_has_volume_control();
    root_mean_square = 1099511627776ULL;

    const int volumes[] = {
        0,
        (79 - LOUDNESS_DB_SPL_MAX) * 256,
        (75 - LOUDNESS_DB_SPL_MAX) * 256,
        (55 - LOUDNESS_DB_SPL_MAX) * 256,
        (79 - LOUDNESS_DB_SPL_MAX) * 256,
        0
    };
    int v;
    for (v = 0; v < 6; v++) {
        loudness_usb_volume_changed((S16)volumes[v]);
        (void)process_sample(1000);
        (void)process_sample(-1000);
    }

    assert(stats->deadline_misses == 0);
    assert(stats->event_count >= 4);
    printf("test_equalizer_step_switch_rapid_sweep_no_deadline_misses passed\n");
}

static void test_usb_volume_change_updates_telemetry_immediately(void) {
    stats_telemetry_snapshot_t telemetry;

    printf("Running test_usb_volume_change_updates_telemetry_immediately...\n");
    reset_usb_stats_buffers();
    current_freq.frequency = 48000;
    loudness_init();

    loudness_usb_volume_changed((S16)((84 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.source_volume_control == 1);
    assert(telemetry.gain_dbfs == 84 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl == 84);
    assert(telemetry.equalizer_step == 13);
    assert(loudness_get_gain_dbfs() == 84 - LOUDNESS_DB_SPL_MAX);

    /* A gain change inside the same hysteresis band updates reported dB SPL
     * without falsely changing the active equalizer step. */
    loudness_usb_volume_changed((S16)((83 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs == 83 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl == 83);
    assert(telemetry.equalizer_step == 13);

    /* The canonical target is no longer polled from the shared USB variable. */
    spk_vol_usb_L = (S16)((75 - LOUDNESS_DB_SPL_MAX) * 256);
    assert(loudness_get_gain_dbfs() == 83 - LOUDNESS_DB_SPL_MAX);

    loudness_usb_volume_changed((S16)((75 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs == 75 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl == 75);
    assert(telemetry.equalizer_step == 10);
    assert(loudness_get_gain_dbfs() == 75 - LOUDNESS_DB_SPL_MAX);

    /* Values observed in the device log must update both SPL and filter step. */
    loudness_usb_volume_changed((S16)((77 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs == 77 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl == 77);
    assert(telemetry.equalizer_step == 11);

    loudness_usb_volume_changed((S16)((81 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs == 81 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl == 81);
    assert(telemetry.equalizer_step == 13);

    loudness_usb_volume_changed(VOL_MIN);
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs == LOUDNESS_GAIN_DBFS_MIN);
    assert(telemetry.db_spl == LOUDNESS_DB_SPL_MAX + LOUDNESS_GAIN_DBFS_MIN);
    assert(telemetry.equalizer_step == 0);
    printf("test_usb_volume_change_updates_telemetry_immediately passed\n");
}

int main(void) {
    printf("=== loudness_equalizer_step_switch_stats_tests ===\n");
    test_equalizer_step_switch_tagged_events_volume_sweep();
    test_equalizer_step_switch_stats_report_and_reset();
    test_equalizer_step_switch_rapid_sweep_no_deadline_misses();
    test_usb_volume_change_updates_telemetry_immediately();
    printf("All loudness_equalizer_step_switch_stats_tests passed.\n");
    return 0;
}
