/*
 * Native test shim - Web routes
 */
#pragma once

#include "httpd.h"

esp_err_t web_routes_register(httpd_handle_t server);
