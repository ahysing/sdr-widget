#include "usb_fifo_hw_lock.h"

void usb_fifo_hw_lock(usb_fifo_hw_lock_t *lock)
{
    if (lock == NULL) {
        return;
    }
    lock->irq_were_enabled = Is_global_interrupt_enabled();
    if (lock->irq_were_enabled) {
        Disable_global_interrupt();
    }
}

void usb_fifo_hw_unlock(usb_fifo_hw_lock_t *lock)
{
    if (lock == NULL) {
        return;
    }
    if (lock->irq_were_enabled) {
        Enable_global_interrupt();
    }
}
