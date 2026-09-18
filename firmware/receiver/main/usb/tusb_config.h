#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT 0
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_FREERTOS
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED

#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

#define CFG_TUD_CDC   1
#define CFG_TUD_MSC   0
#define CFG_TUD_HID   1
#define CFG_TUD_MIDI  0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_AUDIO 1

#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_EP_BUFSIZE 64

#define CFG_TUD_HID_EP_BUFSIZE 16

#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE              16000
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN                 TUD_AUDIO_MIC_ONE_CH_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT                 1
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ              64
#define CFG_TUD_AUDIO_ENABLE_EP_IN                    1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX    2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX            1
/* Fixed size: ((16000+999)/1000 + 1) * 2 * 1 = 34 — avoid TUD_AUDIO_EP_SIZE in #if */
#define CFG_TUD_AUDIO_EP_SZ_IN                        34
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX             CFG_TUD_AUDIO_EP_SZ_IN
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ          (4 * CFG_TUD_AUDIO_EP_SZ_IN)

#ifdef __cplusplus
}
#endif

#endif
