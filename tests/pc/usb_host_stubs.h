#ifndef USB_HOST_STUBS_H
#define USB_HOST_STUBS_H

#include "compiler.h"

#define EP_STATS_HID_TX 6
#define EP_STATISTICS EP_STATS_HID_TX
#define USB_STATS_HID_REPORT_ID 1u

Bool Is_device_enumerated(void);
Bool Is_usb_in_ready(U8 ep);
void Usb_reset_endpoint_fifo_access(U8 ep);
void Usb_write_endpoint_data(U8 ep, U8 width, U8 data);
void Usb_send_in(U8 ep);
void print_dbg(const char *str);

#endif
