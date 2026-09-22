//
// Created by AHysing on 6/27/2026.
//
#include "usb_statistics.h"

#ifndef USBSTATISTICS_DISABLE

#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#endif
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#ifdef UNIT_TEST
#include "usb_host_stubs.h"
#else
#include "usb_statistics_descriptors.h"
#include "usb_stats_hid_report_descriptor.h"
#include "usb_drv.h"
#include "usb_standard_request.h"
#include "usb_fifo_hw_lock.h"
#endif
#include "stats_telemetry.h"

static volatile usb_stats_t usb_stats[2] = {
    {0, 0, 0, 0, 0, 0xFFFF, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0xFFFF, 0, 0, 0, 0, 0, 0}
};
static volatile int collect_index = 0;
static U8 statistics_report_seq = 0;
static volatile Bool statistics_runtime_active = TRUE;
#ifdef FREERTOS_USED
static Bool statistics_initialized = FALSE;
#endif

static void statistics_reset_period_counters(volatile usb_stats_t *s)
{
    s->generation = 0;
    s->underruns = 0;
    s->overruns = 0;
    s->fifo_level = 0;
    s->max_fifo = 0;
    s->min_fifo = 0xFFFF;
    s->deadline_misses = 0;
    s->event_count = 0;
    s->last_tag = USB_STATS_TAG_NONE;
    s->last_arg0 = 0;
    s->last_arg1 = 0;
    s->last_arg2 = 0;
}

Bool statistics_runtime_is_active(void)
{
    return statistics_runtime_active;
}

void statistics_runtime_set_active(Bool active)
{
    (void)active;
    if (!statistics_runtime_active) {
        statistics_reset_period_counters(&usb_stats[0]);
        statistics_reset_period_counters(&usb_stats[1]);
    }
    statistics_runtime_active = TRUE;
}

static void statistics_write_le16(U8 *dst, U16 value)
{
    dst[0] = (U8)(value);
    dst[1] = (U8)(value >> 8);
}

static void statistics_write_le32(U8 *dst, U32 value)
{
    dst[0] = (U8)(value);
    dst[1] = (U8)(value >> 8);
    dst[2] = (U8)(value >> 16);
    dst[3] = (U8)(value >> 24);
}

static U8 statistics_wire_checksum(const U8 *wire)
{
    U8 checksum = 0;
    size_t i;

    for (i = 0; i < USB_STATS_PACKET_WIRE_SIZE; i++) {
        if (i == USB_STATS_PACKET_CHECKSUM_OFFSET) {
            continue;
        }
        checksum ^= wire[i];
    }
    return checksum;
}

static void statistics_build_wire_packet(U8 *wire, const volatile usb_stats_t *s,
    const stats_telemetry_snapshot_t *telemetry, U8 report_seq)
{
    wire[0] = USB_STATS_PACKET_HID_ANCHOR;
    wire[1] = USB_STATS_PACKET_VERSION;
    wire[2] = report_seq;
    wire[3] = 0;
    statistics_write_le32(&wire[4], s->overruns);
    statistics_write_le32(&wire[8], s->underruns);
    statistics_write_le16(&wire[12], s->fifo_level);
    statistics_write_le16(&wire[14], s->max_fifo);
    statistics_write_le16(&wire[16], s->min_fifo);
    statistics_write_le32(&wire[18], s->deadline_misses);
    statistics_write_le16(&wire[22], telemetry->frequency_100hz);
    wire[24] = (U8)telemetry->gain_dbfs;
    wire[25] = (U8)telemetry->db_spl;
    statistics_write_le32(&wire[26], s->event_count);
    wire[30] = s->last_tag;
    wire[31] = s->last_arg0;
    wire[32] = s->last_arg1;
    wire[33] = s->last_arg2;
    wire[34] = telemetry->equalizer_step;
    wire[35] = telemetry->source_has_volume_control ? 1u : 0u;
    wire[3] = statistics_wire_checksum(wire);
}

#ifndef UNIT_TEST
static U8 statistics_hid_buffer[USB_STATS_HID_TRANSFER_SIZE];
#define STATISTICS_HID_SEND_WAIT_MS 50
#ifdef FREERTOS_USED
#define STATISTICS_HID_SEND_WAIT_TICKS \
    ((STATISTICS_HID_SEND_WAIT_MS * configTICK_RATE_HZ) / 1000)
#endif

static void statistics_prepare_hid_buffer(const U8 *wire_packet)
{
    memset(statistics_hid_buffer, 0, USB_STATS_HID_TRANSFER_SIZE);
    statistics_hid_buffer[0] = USB_STATS_HID_REPORT_ID;
    memcpy(&statistics_hid_buffer[1], wire_packet, USB_STATS_PACKET_WIRE_SIZE);
}

static void statistics_clear_hid_buffer(void)
{
    memset(statistics_hid_buffer, 0, USB_STATS_HID_TRANSFER_SIZE);
}

static Bool statistics_hid_try_send(void)
{
    usb_fifo_hw_lock_t lock;
    Bool sent = FALSE;

    if (!Is_device_enumerated()) {
        return FALSE;
    }

    usb_fifo_hw_lock(&lock);
    if (Is_usb_in_ready(EP_STATS_HID_TX)) {
        Usb_reset_endpoint_fifo_access(EP_STATS_HID_TX);
        (void)usb_write_ep_txpacket(EP_STATS_HID_TX, statistics_hid_buffer,
            USB_STATS_HID_TRANSFER_SIZE, NULL);
        Usb_ack_in_ready_send(EP_STATS_HID_TX);
        sent = TRUE;
    }
    usb_fifo_hw_unlock(&lock);
    return sent;
}
#endif

void statistics_init()
{
#ifdef FREERTOS_USED
    if (statistics_initialized) {
        return;
    }
    statistics_initialized = TRUE;
#endif
    stats_telemetry_init();
#ifdef FREERTOS_USED
    xTaskCreate(statistics_task,
        configTSK_USB_DAUDIOSTATS_NAME,
        configTSK_USB_DAUDIOSTATS_STACK_SIZE,
        NULL,
        configTSK_USB_DAUDIOSTATS_PRIORITY,
        NULL);
#endif
}

#ifdef FREERTOS_USED
void statistics_task(void *pvParameters)
{
    portTickType lastWakeTime = xTaskGetTickCount();
    (void)pvParameters;
    while (TRUE)
    {
        vTaskDelayUntil(&lastWakeTime, configTSK_USB_DAUDIOSTATS_PERIOD_MS * configTICK_RATE_HZ / 1000);
        statistics_report_iteration();
    }
}
#endif

static Bool statistics_try_send_packet(const U8 *wire_packet, U8 report_seq,
    volatile usb_stats_t *reset_buf)
{
    if (!Is_device_enumerated()) {
        return FALSE;
    }

#ifndef UNIT_TEST
    statistics_prepare_hid_buffer(wire_packet);
    {
        Bool sent = FALSE;
#ifdef FREERTOS_USED
        portTickType waited_ticks = 0;
        while (!sent && waited_ticks < STATISTICS_HID_SEND_WAIT_TICKS) {
            sent = statistics_hid_try_send();
            if (!sent) {
                vTaskDelay(1);
                waited_ticks++;
            }
        }
#else
        sent = statistics_hid_try_send();
#endif
        if (sent) {
            statistics_report_seq = report_seq;
            statistics_clear_hid_buffer();
            if (reset_buf != NULL) {
                statistics_reset_period_counters(reset_buf);
            }
        }
        return sent;
    }
#else
    {
        Bool sent = FALSE;
        if (Is_usb_in_ready(EP_STATS_HID_TX))
        {
            int i;
            Usb_reset_endpoint_fifo_access(EP_STATS_HID_TX);
            Usb_write_endpoint_data(EP_STATS_HID_TX, 8, USB_STATS_HID_REPORT_ID);
            for (i = 0; i < (int)USB_STATS_PACKET_WIRE_SIZE; i++)
            {
                Usb_write_endpoint_data(EP_STATS_HID_TX, 8, wire_packet[i]);
            }
            statistics_report_seq = report_seq;
            Usb_send_in(EP_STATS_HID_TX);
            sent = TRUE;
        }
        if (sent && reset_buf != NULL) {
            statistics_reset_period_counters(reset_buf);
        }
        return sent;
    }
#endif
}

void statistics_report_iteration()
{
    U8 wire_packet[USB_STATS_PACKET_WIRE_SIZE];
    stats_telemetry_snapshot_t telemetry;
    U8 report_seq;

    telemetry = stats_telemetry_read_best_effort();
    report_seq = (U8)(statistics_report_seq + 1u);

    {
        int report_index;
        volatile usb_stats_t *s;

        report_index = collect_index;
        collect_index = 1 - collect_index;
        s = &usb_stats[report_index];
        statistics_build_wire_packet(wire_packet, s, &telemetry, report_seq);
        statistics_try_send_packet(wire_packet, report_seq, s);
    }
}

volatile usb_stats_t* get_usb_stats()
{
    return &usb_stats[collect_index];
}

#ifdef UNIT_TEST
void statistics_test_reset(void) {
    int i;
    collect_index = 0;
    statistics_report_seq = 0;
    statistics_runtime_active = TRUE;
    stats_telemetry_test_reset();
    for (i = 0; i < 2; i++) {
        usb_stats[i].generation = 0;
        usb_stats[i].overruns = 0;
        usb_stats[i].underruns = 0;
        usb_stats[i].fifo_level = 0;
        usb_stats[i].max_fifo = 0;
        usb_stats[i].min_fifo = 0xFFFF;
        usb_stats[i].deadline_misses = 0;
        usb_stats[i].event_count = 0;
        usb_stats[i].last_tag = USB_STATS_TAG_NONE;
        usb_stats[i].last_arg0 = 0;
        usb_stats[i].last_arg1 = 0;
        usb_stats[i].last_arg2 = 0;
    }
}

void statistics_test_set_collect_index(int index) {
    collect_index = index;
}

volatile usb_stats_t* statistics_test_get_buffer(int index) {
    return &usb_stats[index & 1];
}

int statistics_test_get_collect_index(void) {
    return collect_index;
}

U8 statistics_test_get_report_seq(void) {
    return statistics_report_seq;
}

U8 statistics_test_build_wire_checksum(const U8 *wire)
{
    return statistics_wire_checksum(wire);
}

void statistics_test_build_wire_packet(U8 *wire, const volatile usb_stats_t *s, U8 report_seq)
{
    stats_telemetry_snapshot_t telemetry = stats_telemetry_read_best_effort();
    statistics_build_wire_packet(wire, s, &telemetry, report_seq);
}
#endif

#endif // USBSTATISTICS_DISABLE
