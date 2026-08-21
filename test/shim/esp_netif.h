/*
 * Native test shim - ESP NetIF
 */
#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

typedef void* esp_netif_t;

esp_err_t esp_netif_init(void);
esp_netif_t esp_netif_create_default_wifi_sta(void);
esp_netif_t esp_netif_create_default_wifi_ap(void);
esp_err_t esp_netif_get_ip_info(const esp_netif_t* netif, esp_netif_ip_info_t* ip_info);

/* Added to align host shim with ESP-IDF v6 types used by wifi_manager.c */
typedef struct {
    esp_netif_ip_info_t ip_info;
} ip_event_got_ip_t;

#define IPSTR "%d.%d.%d.%d"
#define IP2STR(ipaddr) ((uint8_t*)(ipaddr))[0], ((uint8_t*)(ipaddr))[1], ((uint8_t*)(ipaddr))[2], ((uint8_t*)(ipaddr))[3]
