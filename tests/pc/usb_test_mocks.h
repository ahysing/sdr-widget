#ifndef USB_TEST_MOCKS_H
#define USB_TEST_MOCKS_H

#include "fff.h"
#include "compiler.h"
#include <stdint.h>

#define configTSK_USB_DAUDIOSTATS_NAME "STATS"
#define configTSK_USB_DAUDIOSTATS_STACK_SIZE 256
#define configTSK_USB_DAUDIOSTATS_PRIORITY 1
#define configTSK_USB_DAUDIOSTATS_PERIOD_MS 1000
#define configTICK_RATE_HZ 1000
#define EP_STATS_HID_TX 6
#define EP_STATISTICS EP_STATS_HID_TX
#define USB_STATS_HID_REPORT_ID 1u
#define USB_STATS_HID_REPORT_SIZE 63u
#define USB_STATS_HID_TRANSFER_SIZE 64u
#define MSK_EP_DIR 0x80

#define FREERTOS_H
#define TASK_H
#define SEMPHR_H
#define _USB_DRV_H_
#define _USB_STANDARD_REQUEST_H_
#define _PRINT_FUNCS_H_
#define SDR_WIDGET_USB_STATISTICS_DESCRIPTORS_H

typedef uint32_t portTickType;

FAKE_VOID_FUNC(vTaskDelayUntil, portTickType*, portTickType);
FAKE_VALUE_FUNC(portTickType, xTaskGetTickCount);
FAKE_VALUE_FUNC(int, xTaskCreate, void*, const char*, uint16_t, void*, int, void**);
FAKE_VALUE_FUNC(Bool, Is_device_enumerated);
FAKE_VALUE_FUNC(Bool, Is_usb_in_ready, U8);
FAKE_VOID_FUNC(Usb_reset_endpoint_fifo_access, U8);
FAKE_VOID_FUNC(Usb_write_endpoint_data, U8, U8, U8);
FAKE_VOID_FUNC(Usb_send_in, U8);
FAKE_VOID_FUNC(print_dbg, const char*);

static inline void usb_test_mocks_reset(void) {
    RESET_FAKE(vTaskDelayUntil);
    RESET_FAKE(xTaskGetTickCount);
    RESET_FAKE(xTaskCreate);
    RESET_FAKE(Is_device_enumerated);
    RESET_FAKE(Is_usb_in_ready);
    RESET_FAKE(Usb_reset_endpoint_fifo_access);
    RESET_FAKE(Usb_write_endpoint_data);
    RESET_FAKE(Usb_send_in);
    RESET_FAKE(print_dbg);
}

#endif
