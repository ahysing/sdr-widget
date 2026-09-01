#include "fff.h"
#include "loudness.h"
#include "loudness_test_access.h"
#include "loudness_internal.h"
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
    return loudness_filter_24bit_container(0, (int32_t)sample);
}

static void reset_usb_stats_buffers(void) {
    usb_test_mocks_reset();
    statistics_test_reset();
}

static void loudness_usb_volume_changed_stereo(S16 volume_q8)
{
    loudness_usb_volume_changed_left(volume_q8);
    loudness_usb_volume_changed_right(volume_q8);
}

static void test_equalizer_step_switch_tagged_events_volume_sweep(void) {
    printf("Running test_equalizer_step_switch_tagged_events_volume_sweep...\n");
    reset_usb_stats_buffers();
    volatile usb_stats_t *stats = get_usb_stats();

    current_freq.frequency = 44100;
    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_usb_volume_changed_stereo(0);
    reset_usb_stats_buffers();

    assert(stats->event_count == 0);
    assert(loudness_get_last_db_spl_left_x10() == LOUDNESS_DB_SPL_MAX * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);

    loudness_usb_volume_changed_stereo((S16)((79 - LOUDNESS_DB_SPL_MAX) * 256));
    assert(loudness_get_last_db_spl_left_x10() == 790);
    assert(loudness_get_last_db_spl_right_x10() == 790);
    assert(stats->event_count == 1);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg0 == LOUDNESS_DB_SPL_MAX);
    assert(stats->last_arg1 == 79);
    assert(stats->last_arg2 == 88);

    loudness_usb_volume_changed_stereo((S16)((75 - LOUDNESS_DB_SPL_MAX) * 256));
    assert(loudness_get_last_db_spl_left_x10() == 750);
    assert(loudness_get_last_db_spl_right_x10() == 750);
    assert(stats->event_count == 2);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg0 == 79);
    assert(stats->last_arg1 == 75);
    assert(stats->last_arg2 == 80);

    loudness_usb_volume_changed_stereo((S16)((55 - LOUDNESS_DB_SPL_MAX) * 256));
    assert(loudness_get_last_db_spl_left_x10() == 550);
    assert(loudness_get_last_db_spl_right_x10() == 550);
    assert(stats->event_count == 3);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg0 == 75);
    assert(stats->last_arg1 == 55);
    assert(stats->last_arg2 == 40);

    loudness_usb_volume_changed_stereo(0);
    assert(loudness_get_last_db_spl_left_x10() == LOUDNESS_DB_SPL_MAX * 10);
    assert(loudness_get_last_db_spl_right_x10() == LOUDNESS_DB_SPL_MAX * 10);
    assert(stats->event_count == 4);
    assert(stats->last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats->last_arg1 == LOUDNESS_DB_SPL_MAX);
    assert(stats->last_arg2 == 120);

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
        loudness_usb_volume_changed_stereo((S16)volumes[v]);
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

    loudness_usb_volume_changed_left((S16)((84 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.source_has_volume_control == 1);
    assert(telemetry.gain_dbfs_left == 84 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl_left == 84);
    assert(telemetry.equalizer_step_left == 98);

    /* Every 0.5 dB has a dedicated loudness+volume row. */
    loudness_usb_volume_changed_left((S16)((83 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs_left == 83 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl_left == 83);
    assert(telemetry.equalizer_step_left == 96);

    /* The canonical target is no longer polled from the shared USB variable. */
    spk_vol_usb_L = (S16)((75 - LOUDNESS_DB_SPL_MAX) * 256);

    loudness_usb_volume_changed_left((S16)((75 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs_left == 75 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl_left == 75);
    assert(telemetry.equalizer_step_left == 80);

    /* Values observed in the device log must update both SPL and filter step. */
    loudness_usb_volume_changed_left((S16)((77 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs_left == 77 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl_left == 77);
    assert(telemetry.equalizer_step_left == 84);

    loudness_usb_volume_changed_left((S16)((81 - LOUDNESS_DB_SPL_MAX) * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs_left == 81 - LOUDNESS_DB_SPL_MAX);
    assert(telemetry.db_spl_left == 81);
    assert(telemetry.equalizer_step_left == 92);

    loudness_usb_volume_changed_left(VOL_MIN);
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs_left == LOUDNESS_GAIN_DBFS_MIN);
    assert(telemetry.db_spl_left == LOUDNESS_DB_SPL_MAX + LOUDNESS_GAIN_DBFS_MIN);
    assert(telemetry.equalizer_step_left == 0);
    printf("test_usb_volume_change_updates_telemetry_immediately passed\n");
}

static void test_stereo_telemetry_policies(void)
{
    stats_telemetry_snapshot_t telemetry;

    printf("Running test_stereo_telemetry_policies...\n");
    reset_usb_stats_buffers();
    current_freq.frequency = 48000;
    loudness_init();

    loudness_usb_volume_changed_left((S16)(-6 * 256));
    loudness_usb_volume_changed_right((S16)(-20 * 256));
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs_left == -6);
    assert(telemetry.gain_dbfs_right == -20);
    assert(telemetry.db_spl_left == 89);
    assert(telemetry.db_spl_right == 75);
    assert(telemetry.equalizer_step_left == 108);
    assert(telemetry.equalizer_step_right == 80);

    current_freq.frequency = 192000;
    loudness_change_frequency_fast(192000);
    loudness_update_active_equalizer_step();
    telemetry = stats_telemetry_read_best_effort();
    /* Base-rate loudness telemetry is unchanged when frequency is highres. */
    assert(telemetry.gain_dbfs_left == -6);
    assert(telemetry.gain_dbfs_right == -20);
    assert(telemetry.db_spl_left == 89);
    assert(telemetry.db_spl_right == 75);
    assert(telemetry.equalizer_step_left == 108);
    assert(telemetry.equalizer_step_right == 80);

    loudness_usb_volume_changed_left(VOL_MIN);
    loudness_usb_volume_changed_right(VOL_MIN);
    current_freq.frequency = 48000;
    loudness_change_frequency_fast(48000);
    loudness_update_active_equalizer_step();
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.gain_dbfs_left == LOUDNESS_GAIN_DBFS_MIN);
    assert(telemetry.gain_dbfs_right == LOUDNESS_GAIN_DBFS_MIN);
    assert(telemetry.db_spl_left == LOUDNESS_MIN_PHON_X10 / 10);
    assert(telemetry.db_spl_right == LOUDNESS_MIN_PHON_X10 / 10);
    assert(telemetry.equalizer_step_left == 0);
    assert(telemetry.equalizer_step_right == 0);
    printf("test_stereo_telemetry_policies passed\n");
}

static void test_boot_telemetry_reports_loudness_enabled(void) {
    stats_telemetry_snapshot_t telemetry;

    printf("Running test_boot_telemetry_reports_loudness_enabled...\n");
    reset_usb_stats_buffers();
    current_freq.frequency = 48000;
    loudness_init();
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.loudness_enabled == 1);
    assert(telemetry.bass_boost_enabled == 0);
    printf("test_boot_telemetry_reports_loudness_enabled passed\n");
}

static void test_bass_boost_telemetry_reports_equalizer_step_40(void) {
    stats_telemetry_snapshot_t telemetry;

    printf("Running test_bass_boost_telemetry_reports_equalizer_step_40...\n");
    reset_usb_stats_buffers();
    current_freq.frequency = 48000;
    loudness_init();
    loudness_set_source_has_volume_control();
    loudness_bass_boost_set(TRUE);
    loudness_usb_volume_changed_stereo(0);
    loudness_update_active_equalizer_step();
    telemetry = stats_telemetry_read_best_effort();
    assert(telemetry.equalizer_step_left == BASSS_PHON_55_IDX);
    assert(telemetry.equalizer_step_right == BASSS_PHON_55_IDX);
    assert(telemetry.bass_boost_enabled == 1);
    assert(telemetry.loudness_enabled == 0);
    printf("test_bass_boost_telemetry_reports_equalizer_step_40 passed\n");
}

int main(void) {
    printf("=== loudness_equalizer_step_switch_stats_tests ===\n");
    test_equalizer_step_switch_tagged_events_volume_sweep();
    test_equalizer_step_switch_stats_report_and_reset();
    test_equalizer_step_switch_rapid_sweep_no_deadline_misses();
    test_usb_volume_change_updates_telemetry_immediately();
    test_stereo_telemetry_policies();
    test_boot_telemetry_reports_loudness_enabled();
    test_bass_boost_telemetry_reports_equalizer_step_40();
    printf("All loudness_equalizer_step_switch_stats_tests passed.\n");
    return 0;
}
