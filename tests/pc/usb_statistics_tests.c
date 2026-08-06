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

    stats_telemetry_set_frequency_hz(48000);
    stats_telemetry_set_track_levels((S8)-12, (S8)-24);
    stats_telemetry_set_gain_dbfs((S8)-10);
    stats_telemetry_set_equalizer_state((S8)80, 2);

    statistics_test_build_wire_packet(wire, buf, 7);

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
    assert(wire[23] == 0xbb);
    assert((int8_t)wire[24] == -12);
    assert((int8_t)wire[25] == -24);
    assert((int8_t)wire[26] == -10);
    assert((int8_t)wire[27] == 80);
    assert(wire[28] == 0x03);
    assert(wire[29] == 0x00);
    assert(wire[30] == 0x00);
    assert(wire[31] == 0x00);
    assert(wire[36] == 2);
    assert(wire[3] == statistics_test_build_wire_checksum(wire));
    printf("test_statistics_wire_packet_is_little_endian passed\n");
}

int main() {
    test_get_usb_stats();
    test_statistics_report_iteration_swaps_buffers();
    test_statistics_report_iteration_resets_counters();
    test_statistics_report_iteration_sends_usb_data();
    test_statistics_report_iteration_reports_deadline_misses();
    test_statistics_report_iteration_preserves_counters_on_send_failure();
    test_statistics_wire_packet_is_little_endian();
    printf("\nAll USB statistics tests completed!\n");
    return 0;
}
