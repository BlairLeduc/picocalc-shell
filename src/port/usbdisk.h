/* usbdisk.h — the card over USB mass storage (design.md §4.2).
 *
 * As the loader's menu does it (ui/lib/usb_msc/usb_msc.c): TinyUSB's MSC
 * device, each SCSI read or write one sd.c block at a time. The core has
 * unmounted FatFs first, so the computer is the card's only user. */
#ifndef SHELL_USBDISK_H
#define SHELL_USBDISK_H

#include <stdbool.h>

/* Offer the card until the computer ejects it or `stop` says a key
 * stopped it; `stop` is called between USB tasks, and keeps the
 * keyboard, and so the southbridge's I2C, polled. NULL, or why not. */
const char *usbdisk_run(bool (*stop)(void *ctx), void *ctx);

#endif /* SHELL_USBDISK_H */
