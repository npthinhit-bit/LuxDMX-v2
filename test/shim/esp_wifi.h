/*
 * Native test shim - ESP WiFi
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
} wifi_config_sta_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    int ssid_len;
    int channel;
    int max_connection;
    int authmode;
    int reserved;
} wifi_config_ap_t;

typedef union {
    wifi_config_sta_t sta;
    wifi_config_ap_t ap;
} wifi_config_t;

#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA_WPA2_PSK 3

typedef struct {
    int reason;
} wifi_event_sta_disconnected_t;

typedef struct {
    uint32_t ip;
    uint32_t gw;
    uint32_t netmask;
} esp_netif_ip_info_t;

typedef enum {
    WIFI_EVENT = 0,
    IP_EVENT = 1
} wifi_event_t;

typedef enum {
    WIFI_EVENT_STA_CONNECTED = 0,
    WIFI_EVENT_STA_DISCONNECTED = 1,
    WIFI_EVENT_AP_START = 2,
    WIFI_EVENT_AP_STACONNECTED = 3,
    WIFI_EVENT_AP_STADISCONNECTED = 4,
} wifi_event_id_t;

typedef enum {
    IP_EVENT_STA_GOT_IP = 100,
    IP_EVENT_STA_LOST_IP = 101,
} ip_event_id_t;

/* WiFi init config */
typedef struct {
    int max_scan_results;
    int magic;
} wifi_init_config_t;

#define WIFI_INIT_CONFIG_DEFAULT() { .max_scan_results = 0, .magic = 0 }

typedef struct {
    int8_t rssi;
    uint8_t ssid[32];
    uint8_t authmode;
    uint8_t channel;
} wifi_ap_record_t;

typedef struct {
    int8_t rssi;
    uint8_t ssid[32];
    uint8_t channel;
    uint8_t authmode;
} wifi_scan_record_t;

esp_err_t esp_wifi_init(const void* config);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_get_mode(int* mode);
esp_err_t esp_wifi_set_config(int ifx, const wifi_config_t* config);
esp_err_t esp_wifi_get_config(int ifx, wifi_config_t* config);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_scan_start(const void* config, bool block);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t* num);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t* num, wifi_ap_record_t* records);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t* ap_info);

#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
