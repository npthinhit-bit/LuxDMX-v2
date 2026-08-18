#include "wifi_manager.h"
#include "common.h"
#include "logger.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

static const char* TAG = "captive_portal";
static bool captive_portal_active = false;

// DNS server callback for captive portal
static void dns_server_callback(const char *name, ip_addr_t *addr, void *arg) {
    // Redirect all DNS queries to our SoftAP IP
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);
    addr->u_addr.ip4.addr = ip_info.ip.addr;
    LOG_DEBUG(TAG, "DNS redirect: %s -> %s", name, ip4addr_ntoa(&ip_info.ip));
}

// Start captive portal DNS server
esp_err_t captive_portal_start(void) {
    if (captive_portal_active) {
        return ESP_OK;
    }

    // Create DNS server
    ip_addr_t dnsserver;
    IP_ADDR4(&dnsserver, 127, 0, 0, 1); // Use localhost as DNS server

    // Start DNS server
    dns_init();
    dns_setserver(0, &dnsserver);
    dns_set_callback(dns_server_callback, NULL);

    captive_portal_active = true;
    LOG_INFO(TAG, "Captive portal DNS server started");
    return ESP_OK;
}

// Stop captive portal DNS server
esp_err_t captive_portal_stop(void) {
    if (!captive_portal_active) {
        return ESP_OK;
    }

    // Stop DNS server
    dns_stop();

    captive_portal_active = false;
    LOG_INFO(TAG, "Captive portal DNS server stopped");
    return ESP_OK;
}

// Check if captive portal is active
bool captive_portal_is_active(void) {
    return captive_portal_active;
}