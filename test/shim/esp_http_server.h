/*
 * Native test shim - ESP HTTP Server
 * Minimal definitions for host-side testing
 */
#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

typedef void* httpd_handle_t;

typedef struct httpd_req httpd_req_t;

typedef struct httpd_uri {
    const char* uri;
    int method;
    void* handler;
    void* user_ctx;
} httpd_uri_t;

#define HTTP_GET 1
#define HTTP_POST 2
#define HTTP_PUT 3
#define HTTP_DELETE 4

typedef struct {
    int max_uri_handlers;
    int max_open_sockets;
    int recv_wait_timeout;
    int send_wait_timeout;
    void* uri_match_fn;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() { \
    .max_uri_handlers = 16, \
    .max_open_sockets = 7, \
    .recv_wait_timeout = 5, \
    .send_wait_timeout = 5, \
    .uri_match_fn = NULL \
}

typedef struct httpd_req {
    char* uri;
    int content_len;
    int method;
} httpd_req_t;

esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t* config);
esp_err_t httpd_stop(httpd_handle_t handle);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t* uri);
esp_err_t httpd_resp_send(httpd_req_t* req, const char* buffer, int len);
esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type);
esp_err_t httpd_resp_send_500(httpd_req_t* req);
esp_err_t httpd_resp_send_err(httpd_req_t* req, int err_code, const char* msg);
int httpd_req_recv(httpd_req_t* req, char* buf, int len);

#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
