#include "tusb.h"

#define USB_VID 0x2e8au
#define USB_PID 0x000au
#define USB_DEVICE_VERSION 0x0100u
#define USB_VENDOR_EP_OUT 0x01u
#define USB_VENDOR_EP_IN 0x82u
#define USB_VENDOR_EP_SIZE 64u

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_DEVICE_VERSION,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x00,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0,
        TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_VENDOR_DESCRIPTOR(0, 0, USB_VENDOR_EP_OUT, USB_VENDOR_EP_IN,
        USB_VENDOR_EP_SIZE)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

static const char *const string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "Miller Project",
    "GateMate DirtyJTAG"
};

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t language_id)
{
    (void)language_id;
    static uint16_t descriptor[32];
    size_t count;

    if (index == 0u) {
        descriptor[1] = 0x0409;
        count = 1u;
    } else {
        if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) {
            return NULL;
        }
        const char *string = string_descriptors[index];
        count = 0u;
        while (string[count] != '\0' && count < 31u) {
            descriptor[1u + count] = (uint8_t)string[count];
            ++count;
        }
    }

    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * count + 2u));
    return descriptor;
}