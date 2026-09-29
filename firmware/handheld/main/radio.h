#pragma once

#include "vk_protocol.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*radio_rx_cb_t)(const uint8_t mac[6], const vk_hdr_t *hdr, const uint8_t *payload, void *ctx);

void radio_init(radio_rx_cb_t cb, void *ctx);
/** Enable ESP-NOW connectionless power save (web/NVS eco). */
void radio_set_modem_sleep(bool enable);
/** Hold RF awake for Voice — call on PTT down. */
void radio_modem_boost(void);
/** Release RF hold after Voice up. */
void radio_modem_unboost(void);
bool radio_has_peer(void);
void radio_get_peer(uint8_t mac[6]);
void radio_clear_peer(void);
void radio_set_peer(const uint8_t mac[6]);
bool radio_send(vk_pkt_type_t type, const void *payload, uint8_t len);
bool radio_send_audio(const int16_t *samples, size_t count);
void radio_send_pair_req(void);
bool radio_tx_ready(void);
