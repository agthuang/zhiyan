#pragma once

#include <stdint.h>

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_AUDIO_CONTROL,
    ITF_NUM_AUDIO_STREAMING,
    ITF_NUM_TOTAL
};

enum {
    EPNUM_CDC_NOTIF = 0x81,
    EPNUM_CDC_OUT   = 0x02,
    EPNUM_CDC_IN    = 0x82,
    EPNUM_HID       = 0x83,
    EPNUM_AUDIO     = 0x84,
};

#define USB_VID 0x303A
#define USB_PID 0x1003
