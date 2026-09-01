#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include "usb_statistics.h"
#include "stats_telemetry.h"
#include "usb_test_mocks.h"

DEFINE_FFF_GLOBALS;

void setup() {
    usb_test_mocks_reset();
    statistics_test_reset();
}

void test_get_usb_stats() {
    printf("Running test_get_usb_stats...\n");
    setup();
    volatile usb_stats_t* stats = get_usb_stats();
    assert(stats == statistics_test_get_buffer(0));

    statistics_test_set_collect_index(1);
    stats = get_usb_stats();
    assert(stats == statistics_test_get_buffer(1));
    printf("test_get_usb_stats passed\n");
}

void test_statistics_report_iteration_swaps_buffers() {
    printf("Running test_statistics_report_iteration_swaps_buffers...\n");
    setup();
    assert(statistics_test_get_collect_index() == 0);

    statistics_report_iteration();
    assert(statistics_test_get_collect_index() == 1);

    statistics_report_iteration();
    assert(statistics_test_get_collect_index() == 0);
    printf("test_statistics_report_iteration_swaps_buffers passed\n");
}

void test_statistics_report_iteration_resets_counters() {
    printf("Running test_statistics_report_iteration_resets_counters...\n");
    setup();

    Is_device_enumerated_fake.return_val = TRUE;
    Is_usb_in_ready_fake.return_val = TRUE;

    volatile usb_stats_t *buf0 = statistics_test_get_buffer(0);
    buf0->overruns = 10;
    buf0->underruns = 5;
    buf0->max_fifo = 100;
    buf0->min_fifo = 10;

    statistics_report_iteration();

    assert(buf0->overruns == 0);
    assert(buf0->underruns == 0);
    assert(buf0->max_fifo == 0);
    assert(buf0->min_fifo == 0xFFFF);
    assert(statistics_test_get_collect_index() == 1);
    printf("test_statistics_report_iteration_resets_counters passed\n");
}

void test_statistics_report_iteration_sends_usb_data() {
    printf("Running test_statistics_report_iteration_sends_usb_data...\n");
    setup();

    Is_device_enumerated_fake.return_val = TRUE;
    Is_usb_in_ready_fake.return_val = TRUE;

    statistics_test_get_buffer(0)->overruns = 123;

    statistics_report_iteration();

    assert(Usb_write_endpoint_data_fake.call_count == 1 + (int)USB_STATS_PACKET_WIRE_SIZE);
    assert(Usb_send_in_fake.call_count == 1);
    assert(Usb_send_in_fake.arg0_val == EP_STATS_HID_TX);
    printf("test_statistics_report_iteration_sends_usb_data passed\n");
}

void test_statistics_report_iteration_reports_deadline_misses() {
    printf("Running test_statistics_report_iteration_reports_deadline_misses...\n");
    setup();

    Is_device_enumerated_fake.return_val = TRUE;
    Is_usb_in_ready_fake.return_val = TRUE;

    statistics_test_get_buffer(0)->deadline_misses = 5;

    statistics_report_iteration();

    assert(statistics_test_get_buffer(0)->deadline_misses == 0);
    printf("test_statistics_report_iteration_reports_deadline_misses passed\n");
}

void test_statistics_report_iteration_preserves_counters_on_send_failure() {
    printf("Running test_statistics_report_iteration_preserves_counters_on_send_failure...\n");
    setup();

    Is_device_enumerated_fake.return_val = TRUE;
    Is_usb_in_ready_fake.return_val = FALSE;

    volatile usb_stats_t *buf0 = statistics_test_get_buffer(0);
    buf0->overruns = 10;
    buf0->underruns = 5;
    buf0->max_fifo = 100;
    buf0->deadline_misses = 3;

    statistics_report_iteration();

    assert(buf0->overruns == 10);
    assert(buf0->underruns == 5);
    assert(buf0->max_fifo == 100);
    assert(buf0->deadline_misses == 3);
    assert(Usb_send_in_fake.call_count == 0);
    printf("test_statistics_report_iteration_preserves_counters_on_send_failure passed\n");
}

void test_statistics_wire_packet_is_little_endian() {
    printf("Running test_statistics_wire_packet_is_little_endian...\n");
    setup();

    volatile usb_stats_t *buf = statistics_test_get_buffer(0);
    U8 wire[USB_STATS_PACKET_WIRE_SIZE];

    buf->fifo_level = 671;
    buf->max_fifo = 680;
    buf->min_fifo = 390;
    buf->deadline_misses = 11;
    buf->event_count = 3;

    stats_telemetry_set_frequency_hz(192000);
    stats_telemetry_set_gain_dbfs_stereo((S8)-10, (S8)-20);
    stats_telemetry_set_equalizer_state_stereo(
        (S8)80, 90, (S8)70, 70);
    stats_telemetry_set_source_has_volume_control(1);
    stats_telemetry_set_bass_boost_enabled(1);
    stats_telemetry_set_loudness_enabled(0);
    stats_telemetry_set_sample_bits(24);
    stats_telemetry_set_gain_inferred_dbfs_stereo((S8)-3, (S8)-4);

    statistics_test_build_wire_packet(wire, buf, 7);

    assert(USB_STATS_PACKET_VERSION == 5);
    assert(USB_STATS_PACKET_WIRE_SIZE == 44);
    assert(sizeof(usb_stats_packet_t) == USB_STATS_PACKET_WIRE_SIZE);
    assert(wire[0] == USB_STATS_PACKET_HID_ANCHOR);
    assert(wire[1] == USB_STATS_PACKET_VERSION);
    assert(wire[2] == 7);
    assert(wire[12] == 0x9f);
    assert(wire[13] == 0x02);
    assert(wire[14] == 0xa8);
    assert(wire[15] == 0x02);
    assert(wire[16] == 0x86);
    assert(wire[17] == 0x01);
    assert(wire[18] == 0x0b);
    assert(wire[19] == 0x00);
    assert(wire[20] == 0x00);
    assert(wire[21] == 0x00);
    assert(wire[22] == 0x80);
    assert(wire[23] == 0x07);
    assert((int8_t)wire[24] == -10);
    assert((int8_t)wire[25] == -20);
    assert((int8_t)wire[26] == 80);
    assert((int8_t)wire[27] == 70);
    assert(wire[28] == 0x03);
    assert(wire[29] == 0x00);
    assert(wire[30] == 0x00);
    assert(wire[31] == 0x00);
    assert(wire[36] == 90);
    assert(wire[37] == 70);
    assert(wire[38] == 1);
    assert(wire[USB_STATS_WIRE_OFFSET_BASS_BOOST_ENABLED] == 1);
    assert(wire[USB_STATS_WIRE_OFFSET_LOUDNESS_ENABLED] == 0);
    assert((int8_t)wire[USB_STATS_WIRE_OFFSET_GAIN_INFERRED_LEFT] == -3);
    assert((int8_t)wire[USB_STATS_WIRE_OFFSET_GAIN_INFERRED_RIGHT] == -4);
    assert(wire[USB_STATS_WIRE_OFFSET_SAMPLE_BITS] == 24);
    assert(wire[3] == statistics_test_build_wire_checksum(wire));
    printf("test_statistics_wire_packet_is_little_endian passed\n");
}

void test_statistics_report_swaps_buffers_when_runtime_inactive() {
    printf("Running test_statistics_report_swaps_buffers_when_runtime_inactive...\n");
    setup();

    Is_device_enumerated_fake.return_val = TRUE;
    Is_usb_in_ready_fake.return_val = TRUE;

    statistics_runtime_set_active(FALSE);
    assert(statistics_test_get_collect_index() == 0);

    volatile usb_stats_t *buf0 = statistics_test_get_buffer(0);
    buf0->overruns = 42;
    buf0->deadline_misses = 99;

    statistics_report_iteration();

    assert(statistics_test_get_collect_index() == 1);
    assert(buf0->overruns == 0);
    assert(buf0->deadline_misses == 0);
    assert(statistics_test_get_report_seq() == 1);
    assert(Usb_send_in_fake.call_count == 1);
    printf("test_statistics_report_swaps_buffers_when_runtime_inactive passed\n");
}

void test_statistics_idle_wire_packet_fields() {
    printf("Running test_statistics_idle_wire_packet_fields...\n");
    setup();

    stats_telemetry_set_frequency_hz(96000);

    volatile usb_stats_t idle = {
        0, 0, 0, 0, 0, 0xFFFF, 0, 0, USB_STATS_TAG_NONE, 0, 0, 0
    };
    U8 wire[USB_STATS_PACKET_WIRE_SIZE];

    statistics_test_build_wire_packet(wire, &idle, 1);

    assert(wire[4] == 0);
    assert(wire[5] == 0);
    assert(wire[6] == 0);
    assert(wire[7] == 0);
    assert(wire[8] == 0);
    assert(wire[9] == 0);
    assert(wire[10] == 0);
    assert(wire[11] == 0);
    assert(wire[12] == 0);
    assert(wire[13] == 0);
    assert(wire[14] == 0);
    assert(wire[15] == 0);
    assert(wire[16] == 0xFF);
    assert(wire[17] == 0xFF);
    assert(wire[18] == 0);
    assert(wire[19] == 0);
    assert(wire[20] == 0);
    assert(wire[21] == 0);
    assert(wire[22] == 0xC0);
    assert(wire[23] == 0x03);
    assert(wire[32] == USB_STATS_TAG_NONE);
    assert(wire[USB_STATS_WIRE_OFFSET_BASS_BOOST_ENABLED] == 1);
    assert(wire[3] == statistics_test_build_wire_checksum(wire));
    printf("test_statistics_idle_wire_packet_fields passed\n");
}

void test_statistics_report_always_swaps_buffers() {
    printf("Running test_statistics_report_always_swaps_buffers...\n");
    setup();

    Is_device_enumerated_fake.return_val = TRUE;
    Is_usb_in_ready_fake.return_val = TRUE;

    statistics_runtime_set_active(FALSE);
    statistics_report_iteration();
    assert(statistics_test_get_collect_index() == 1);

    statistics_runtime_set_active(TRUE);
    statistics_report_iteration();
    assert(statistics_test_get_collect_index() == 0);

    statistics_report_iteration();
    assert(statistics_test_get_collect_index() == 1);
    printf("test_statistics_report_always_swaps_buffers passed\n");
}

static Bool test_transport_stats_rate(uint32_t frequency_hz) {
    return (frequency_hz == 44100U || frequency_hz == 48000U ||
        frequency_hz == 88200U || frequency_hz == 96000U ||
        frequency_hz == 176400U || frequency_hz == 192000U);
}

void test_transport_stats_rates_include_hires() {
    printf("Running test_transport_stats_rates_include_hires...\n");

    assert(test_transport_stats_rate(44100U));
    assert(test_transport_stats_rate(48000U));
    assert(test_transport_stats_rate(88200U));
    assert(test_transport_stats_rate(96000U));
    assert(test_transport_stats_rate(176400U));
    assert(test_transport_stats_rate(192000U));
    printf("test_transport_stats_rates_include_hires passed\n");
}

void test_statistics_full_mode_reports_live_fifo_at_88200() {
    printf("Running test_statistics_full_mode_reports_live_fifo_at_88200...\n");
    setup();

    Is_device_enumerated_fake.return_val = TRUE;
    Is_usb_in_ready_fake.return_val = TRUE;
    stats_telemetry_set_frequency_hz(88200);
    statistics_runtime_set_active(TRUE);

    volatile usb_stats_t *buf = statistics_test_get_buffer(0);
    buf->fifo_level = 1492;
    buf->max_fifo = 1495;
    buf->min_fifo = 1488;

    statistics_report_iteration();

    assert(statistics_test_get_report_seq() == 1);
    assert(buf->fifo_level == 0);
    assert(buf->max_fifo == 0);
    assert(buf->min_fifo == 0xFFFF);
    printf("test_statistics_full_mode_reports_live_fifo_at_88200 passed\n");
}

int main() {
    test_get_usb_stats();
    test_statistics_report_iteration_swaps_buffers();
    test_statistics_report_iteration_resets_counters();
    test_statistics_report_iteration_sends_usb_data();
    test_statistics_report_iteration_reports_deadline_misses();
    test_statistics_report_iteration_preserves_counters_on_send_failure();
    test_statistics_wire_packet_is_little_endian();
    test_statistics_report_swaps_buffers_when_runtime_inactive();
    test_statistics_idle_wire_packet_fields();
    test_statistics_report_always_swaps_buffers();
    test_transport_stats_rates_include_hires();
    test_statistics_full_mode_reports_live_fifo_at_88200();
    printf("\nAll USB statistics tests completed!\n");
    return 0;
}
