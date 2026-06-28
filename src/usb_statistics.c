//
// Created by AHysing on 6/27/2026.
//
#include "usb_statistics.h"

//!
//! @brief This function initializes the USB audio class 1 and 2 statistics
//!
//! This function enables the statistics collected in USB Audio Class 1 and USB Audio Class 2
//! to be sent over USB.
//!

#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#endif
#include <string.h>
#include <stdio.h>
#include "usb_statistics_descriptors.h"

U8 statistics_buffer[32];
xQueueHandle queue_handle;

static usb_stats_t usb_stats = {
    0,        // generation
    0,        // underruns
    0,        // overruns
    // 0,     // feedback_changes
    0,        // fifo_level
    0,        // max_fifo
    0,        // min_fifo
    0,        // deadline_misses
};

void statistics_init()
{
    queue_handle = xQueueCreate(2, sizeof(usb_stats_t));
    if (queue_handle != NULL)
    {
#ifdef FREERTOS_USED
        xTaskCreate(statistics_task,
            configTSK_USB_DAUDIOSTATS_NAME,
            configTSK_USB_DAUDIOSTATS_STACK_SIZE,
            NULL,
            configTSK_USB_DAUDIOSTATS_PRIORITY,
            NULL);
#endif
    }
}

void statistics_task(void *pvParameters)
{
    usb_stats_packet_t packet = {
        1,
        0,
        0,
        0,
        0,
        0,
        0,
    };

    portTickType lastWakeTime = xTaskGetTickCount();
    while (TRUE)
    {
        usb_stats_t stats = snapshot();
        packet.overruns = stats.overruns;
        packet.overruns = stats.underruns;
        packet.fifo_level = stats.fifo_level;
        packet.max_fifo = stats.max_fifo;
        packet.min_fifo = stats.min_fifo;
        packet.deadline_misses = stats.deadline_misses;
        if (xQueueSend(queue_handle, (void *)&packet, portMAX_DELAY) != pdPASS)
        {
            fprintf(stderr, "Failed to send USB statistics packet to queue\n");
        }

        vTaskDelayUntil(&lastWakeTime, configTSK_USB_DAUDIOSTATS_PERIOD_MS * configTICK_RATE_HZ / 1000);
    }
}

usb_stats_t* get_usb_stats()
{
    return &usb_stats;
}

usb_stats_t snapshot()
{
    usb_stats_t snapshot;
    U32 g1 = 0;
    U32 g2 = 0;
    do
    {
        g1 = usb_stats.generation;
        memcpy(&snapshot, &usb_stats, sizeof(snapshot));
        g2 = usb_stats.generation;
    } while (g1 != g2 && (g1 & 1));
    return snapshot;
}