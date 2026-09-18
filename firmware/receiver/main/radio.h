#pragma once

#include "vk_protocol.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*rx_radio_cb_t)(const uint8_t mac[6], const vk_hdr_t *hdr, const uint8_t *payload, void *ctx);

void rx_radio_init(rx_radio_cb_t cb, void *ctx);
bool rx_radio_has_peer(void);
bool rx_radio_is_peer(const uint8_t mac[6]);
void rx_radio_get_peer(uint8_t mac[6]);
void rx_radio_set_peer(const uint8_t mac[6]);
void rx_radio_clear_peer(void);
bool rx_radio_send(vk_pkt_type_t type, const void *payload, uint8_t len);
bool rx_radio_pairing_open(void);
void rx_radio_tick(uint32_t now_ms);
