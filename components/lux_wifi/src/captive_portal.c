#include "captive_portal.h"
#include "logger.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "captive_portal";
static bool portal_active = false;
static int dns_sock = -1;
static TaskHandle_t dns_task_handle = NULL;

static void dns_server_task(void* pvParameters) {
    uint8_t recv_buf[512];
    struct sockaddr_in src_addr;
    socklen_t src_addr_len = sizeof(src_addr);

    LOG_INFO(TAG, "DNS server started on port 53");

    while (portal_active && dns_sock >= 0) {
        int len = recvfrom(dns_sock, recv_buf, sizeof(recv_buf), 0,
                           (struct sockaddr*)&src_addr, &src_addr_len);

        if (len > 0 && len >= (int)sizeof(uint16_t) * 6) {
            uint16_t* hdr = (uint16_t*)recv_buf;

            // Only respond to queries (flags field bit 15 = 0)
            uint16_t flags = ntohs(hdr[1]);
            if ((flags & 0x8000) == 0) {
                // Build response - redirect all queries to AP IP (192.168.4.1)
                uint8_t resp_buf[512];
                memcpy(resp_buf, recv_buf, len);

                // Set response flags: standard response, no error
                uint16_t* resp_hdr = (uint16_t*)resp_buf;
                resp_hdr[1] = htons(0x8180);
                resp_hdr[6] = htons(1); // ancount=1

                // Add answer section - A record for 192.168.4.1
                uint8_t* ptr = resp_buf + len;
                // Point to the question name (assuming 12 bytes header)
                *ptr++ = 0xC0; *ptr++ = 0x0C; // Name pointer to query
                *ptr++ = 0x00; *ptr++ = 0x01; // Type A
                *ptr++ = 0x00; *ptr++ = 0x01; // Class IN
                *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x3C; // TTL 60s
                *ptr++ = 0x00; *ptr++ = 0x04; // Data length 4
                *ptr++ = 192; *ptr++ = 168; *ptr++ = 4; *ptr++ = 1; // 192.168.4.1

                int resp_len = ptr - resp_buf;
                sendto(dns_sock, resp_buf, resp_len, 0,
                       (struct sockaddr*)&src_addr, src_addr_len);
            }
        }
    }

    LOG_INFO(TAG, "DNS server stopped");
    dns_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t captive_portal_start(void) {
    if (portal_active) {
        return ESP_OK;
    }

    // Create DNS UDP socket
    dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (dns_sock < 0) {
        LOG_ERROR(TAG, "Failed to create DNS socket");
        return ESP_FAIL;
    }

    int opt = 1;
    setsockopt(dns_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(dns_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERROR(TAG, "Failed to bind DNS socket");
        close(dns_sock);
        dns_sock = -1;
        return ESP_FAIL;
    }

    // Create DNS server task
    BaseType_t task_created = xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle);
    if (task_created != pdPASS) {
        LOG_ERROR(TAG, "Failed to create DNS task");
        close(dns_sock);
        dns_sock = -1;
        return ESP_ERR_NO_MEM;
    }

    portal_active = true;
    LOG_INFO(TAG, "Captive portal started");
    return ESP_OK;
}

esp_err_t captive_portal_stop(void) {
    if (!portal_active) {
        return ESP_OK;
    }

    portal_active = false;

    // Close DNS socket
    if (dns_sock >= 0) {
        close(dns_sock);
        dns_sock = -1;
    }

    // Wait for DNS task to finish
    if (dns_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        dns_task_handle = NULL;
    }

    LOG_INFO(TAG, "Captive portal stopped");
    return ESP_OK;
}

bool captive_portal_is_active(void) {
    return portal_active;
}
