/* tusb_config.h — TinyUSB for usbdisk: a device with one mass-storage
 * interface (design.md §4.2).
 *
 * The settings are UF2 Loader's ui/lib/usb_msc/tusb_config.h (MIT, from
 * TinyUSB's examples; THIRD-PARTY.md), with its comments and unused
 * options dropped. */
#ifndef SHELL_TUSB_CONFIG_H
#define SHELL_TUSB_CONFIG_H

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

/* The SDK sets some of these itself (CFG_TUSB_OS is its own); its
 * values stand. */
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif
#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_DEFAULT_SPEED)
#endif
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif
#define CFG_TUD_ENABLED       1
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

#define CFG_TUD_CDC           0
#define CFG_TUD_MSC           1
#define CFG_TUD_HID           0
#define CFG_TUD_MIDI          0
#define CFG_TUD_VENDOR        0

/* The piece of a SCSI transfer usbdisk moves to or from the card at
 * once (config.h). */
#include "config.h"
#define CFG_TUD_MSC_EP_BUFSIZE SHELL_USB_BUF

#endif /* SHELL_TUSB_CONFIG_H */
