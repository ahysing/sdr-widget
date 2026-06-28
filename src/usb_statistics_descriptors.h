//
// Created by AHysing on 6/28/2026.
//

#ifndef SDR_WIDGET_USB_STATISTICS_DESCRIPTORS_H
#define SDR_WIDGET_USB_STATISTICS_DESCRIPTORS_H
#include "usb_task.h"
#define INTERFACE_NB3              4
#define ALTERNATE_NB3              0
#define NB_ENDPOINT3               1

#define INTERFACE_CLASS3           0xFF
#define INTERFACE_SUB_CLASS3       0x00
#define INTERFACE_PROTOCOL3        0x00
#define INTERFACE_INDEX3           0


#define DSC_INTERFACE_STATISTICS   INTERFACE_NB3


// Statistics endpoint
#define ENDPOINT_NB_6              (0x06 | MSK_EP_DIR)

#define EP_ATTRIBUTES_6             TYPE_INTERRUPT

#define EP_SIZE_6_FS                64
#define EP_SIZE_6_HS                64

#define EP_INTERVAL_6_FS            10
#define EP_INTERVAL_6_HS            10
#endif //SDR_WIDGET_USB_STATISTICS_DESCRIPTORS_H
