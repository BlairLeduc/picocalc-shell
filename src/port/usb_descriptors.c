/* usb_descriptors.c — what usbdisk tells the computer it is
 * (design.md §4.2).
 *
 * Adapted from UF2 Loader's ui/lib/usb_msc/usb_descriptors.c, itself
 * TinyUSB's msc example (MIT, Ha Thach; THIRD-PARTY.md): the same vendor
 * and product numbers and strings, so the computer sees the card the
 * same way from the shell as from the loader's menu. Only the RP2350's
 * full-speed descriptor is kept, and the serial number is the flash's
 * unique id through the SDK rather than TinyUSB's board layer. */

#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

/* TinyUSB's own product number scheme: 0x4000 | MSC << 1. */
#define USB_VID 0xCafe
#define USB_PID (0x4000 | (CFG_TUD_MSC << 1))

static const tusb_desc_device_t s_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&s_device;
}

enum { ITF_NUM_MSC, ITF_NUM_TOTAL };

#define EPNUM_MSC_OUT    0x01
#define EPNUM_MSC_IN     0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const uint8_t s_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return s_config;
}

enum { STRID_LANGID, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL };

static const char *const s_strings[] = {
    [STRID_MANUFACTURER] = "ClockworkPi",
    [STRID_PRODUCT]      = "PicoCalc",
};

static uint16_t s_desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t n;
    if (index == STRID_LANGID) {
        s_desc_str[1] = 0x0409;   /* English */
        n = 1;
    } else {
        char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        const char *s;
        if (index == STRID_SERIAL) {
            pico_get_unique_board_id_string(serial, sizeof serial);
            s = serial;
        } else if (index < sizeof s_strings / sizeof s_strings[0]) {
            s = s_strings[index];
        } else {
            return NULL;
        }
        n = strlen(s);
        if (n > 32) n = 32;
        for (size_t i = 0; i < n; i++) s_desc_str[1 + i] = (uint8_t)s[i];
    }
    s_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return s_desc_str;
}
