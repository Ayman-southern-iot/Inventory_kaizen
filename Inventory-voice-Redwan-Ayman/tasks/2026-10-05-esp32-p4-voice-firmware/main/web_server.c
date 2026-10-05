/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "web_server.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>   /* close() for the socket-close hook */

#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_http_client.h"

#include "event_bus.h"

static const char *TAG = "web";

/* The dashboard, gzipped at build time and embedded. See main/CMakeLists.txt. */
extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]   asm("_binary_index_html_gz_end");

#define MAX_WS_CLIENTS 4

static httpd_handle_t s_server;
static int s_clients[MAX_WS_CLIENTS];
static int s_client_count;

int web_server_client_count(void)
{
    return s_client_count;
}

static void client_add(int fd)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i] == fd) {
            return;
        }
    }
    if (s_client_count < MAX_WS_CLIENTS) {
        s_clients[s_client_count++] = fd;
        ESP_LOGI(TAG, "websocket client fd=%d attached (%d total)", fd, s_client_count);
    } else {
        ESP_LOGW(TAG, "websocket client limit (%d) reached, refusing fd=%d", MAX_WS_CLIENTS, fd);
    }
}

static void client_remove(int fd)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i] == fd) {
            s_clients[i] = s_clients[--s_client_count];
            ESP_LOGI(TAG, "websocket client fd=%d detached (%d left)", fd, s_client_count);
            return;
        }
    }
}

/*
 * Broadcast sink, registered with the event bus.
 *
 * Uses httpd_ws_send_frame_async because publishers call this from arbitrary tasks
 * (the query path), not from inside an HTTP handler. A send failure means that client
 * is gone -- drop it rather than retrying forever.
 */
static void broadcast(const char *json)
{
    if (!s_server || s_client_count == 0 || !json) {
        return;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    /* Iterate backwards: client_remove() compacts the array by moving the last entry
     * into the removed slot, which would skip an element on a forward walk. */
    for (int i = s_client_count - 1; i >= 0; i--) {
        int fd = s_clients[i];
        esp_err_t err = httpd_ws_send_frame_async(s_server, fd, &frame);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "send to fd=%d failed (%s), dropping client",
                     fd, esp_err_to_name(err));
            client_remove(fd);
        }
    }
}

static esp_err_t index_handler(httpd_req_t *req)
{
    const size_t len = index_html_gz_end - index_html_gz_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    /* The page is immutable per build; let the browser cache it. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_gz_start, len);
}

static esp_err_t state_handler(httpd_req_t *req)
{
    char *history = event_bus_history_json();
    httpd_resp_set_type(req, "application/json");
    if (!history) {
        return httpd_resp_sendstr(req, "[]");
    }
    esp_err_t err = httpd_resp_sendstr(req, history);
    free(history);
    return err;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* Handshake. ESP-IDF completes the upgrade for us; just register the socket. */
        client_add(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame = { 0 };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);   /* 0 = just get the length */
    if (err != ESP_OK) {
        return err;
    }

    if (frame.len) {
        uint8_t *buf = calloc(1, frame.len + 1);
        if (!buf) {
            return ESP_ERR_NO_MEM;
        }
        frame.payload = buf;
        err = httpd_ws_recv_frame(req, &frame, frame.len);
        if (err == ESP_OK && frame.type == HTTPD_WS_TYPE_TEXT) {
            /* The dashboard is read-only; inbound frames are only keepalives. */
            ESP_LOGD(TAG, "ws rx: %.*s", (int)frame.len, (char *)buf);
        }
        free(buf);
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        client_remove(httpd_req_to_sockfd(req));
    }
    return err;
}

/* Called by the server when any socket closes, including a browser tab closing. */
static void on_close(httpd_handle_t hd, int sockfd)
{
    client_remove(sockfd);
    close(sockfd);
}

esp_err_t web_server_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 7;
    config.close_fn = on_close;
    /* Needed so the WebSocket URI can share the server with normal GETs. */
    config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd_start failed");

    static const httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = index_handler,
    };
    static const httpd_uri_t uri_state = {
        .uri = "/api/state", .method = HTTP_GET, .handler = state_handler,
    };
    static const httpd_uri_t uri_ws = {
        .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true,
    };

    httpd_register_uri_handler(s_server, &uri_index);
    httpd_register_uri_handler(s_server, &uri_state);
    httpd_register_uri_handler(s_server, &uri_ws);

    event_bus_set_sink(broadcast);

    ESP_LOGI(TAG, "dashboard server up on port %d (page %u bytes gzipped)",
             config.server_port, (unsigned)(index_html_gz_end - index_html_gz_start));
    return ESP_OK;
}

void web_server_stop(void)
{
    if (!s_server) {
        return;
    }
    event_bus_set_sink(NULL);
    httpd_stop(s_server);
    s_server = NULL;
    s_client_count = 0;
}


/* ------------------------------- loopback self-test ------------------------------ */

typedef struct { size_t len; int status; } probe_t;

static esp_err_t probe_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        ((probe_t *)evt->user_data)->len += evt->data_len;
    }
    return ESP_OK;
}

static esp_err_t probe_one(const char *path, size_t *len_out, int *status_out)
{
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1%s", path);

    probe_t p = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .event_handler = probe_event,
        .user_data = &p,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (len_out) *len_out = p.len;
    if (status_out) *status_out = status;
    return err;
}

esp_err_t web_server_selftest(void)
{
    if (!s_server) {
        ESP_LOGE(TAG, "self-test: server not running");
        return ESP_ERR_INVALID_STATE;
    }

    bool all_ok = true;

    /* The page. Note the client does not send Accept-Encoding: gzip, but our handler
     * always sets Content-Encoding: gzip, so the body length should match the stored
     * compressed size exactly -- which also confirms the blob is intact. */
    size_t len = 0;
    int status = 0;
    esp_err_t err = probe_one("/", &len, &status);
    size_t expect = index_html_gz_end - index_html_gz_start;
    if (err == ESP_OK && status == 200 && len == expect) {
        ESP_LOGI(TAG, "self-test GET /          -> 200, %u bytes (matches embedded page)",
                 (unsigned)len);
    } else {
        ESP_LOGE(TAG, "self-test GET /          -> %s, status %d, %u bytes (expected %u)",
                 esp_err_to_name(err), status, (unsigned)len, (unsigned)expect);
        all_ok = false;
    }

    /* The state endpoint, which proves a handler that builds JSON at runtime works. */
    len = 0; status = 0;
    err = probe_one("/api/state", &len, &status);
    if (err == ESP_OK && status == 200 && len >= 2) {
        ESP_LOGI(TAG, "self-test GET /api/state -> 200, %u bytes of JSON", (unsigned)len);
    } else {
        ESP_LOGE(TAG, "self-test GET /api/state -> %s, status %d, %u bytes",
                 esp_err_to_name(err), status, (unsigned)len);
        all_ok = false;
    }

    return all_ok ? ESP_OK : ESP_FAIL;
}
