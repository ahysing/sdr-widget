#ifndef SDR_WIDGET_USB_FIFO_HW_LOCK_H
#define SDR_WIDGET_USB_FIFO_HW_LOCK_H

#include "compiler.h"

typedef struct {
    Bool irq_were_enabled;
} usb_fifo_hw_lock_t;

void usb_fifo_hw_lock(usb_fifo_hw_lock_t *lock);
void usb_fifo_hw_unlock(usb_fifo_hw_lock_t *lock);

#endif
