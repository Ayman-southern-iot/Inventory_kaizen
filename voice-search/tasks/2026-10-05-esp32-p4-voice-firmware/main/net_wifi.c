/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "net_wifi.h"

#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net_wifi";

static esp_netif_t *s_ap_netif;     /* SoftAP netif, when APSTA is enabled */
static char s_ap_ip[16];

esp_err_t net_wifi_start(const secrets_t *creds)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");

    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(sta, ESP_FAIL, TAG, "creating station netif failed");

#if CONFIG_INV_SOFTAP_ENABLE
    /* Both netifs must exist before esp_wifi_start(), so create the AP one here even
     * though it is configured a few lines below. */
    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_ap_netif, ESP_FAIL, TAG, "creating softap netif failed");
#endif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
#if CONFIG_INV_SOFTAP_ENABLE
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set_mode APSTA failed");
#else
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_mode failed");
#endif

    /*
     * Apply credentials BEFORE esp_wifi_start().
     *
     * This ordering is not cosmetic. Calling esp_wifi_set_config() *after* start is
     * legal in plain ESP-IDF, but over ESP-Hosted it timed out every time with
     * DEFAULT_RPC_RSP_TIMEOUT (5 s) -- the C6 slave never answered the request.
     * init -> set_mode -> set_config -> start is the order the hosted slave expects.
     */
    if (creds && creds->wifi_ssid[0]) {
        wifi_config_t cfg = { 0 };
        /* Fixed-size byte arrays, not C strings: an 802.11 SSID is up to 32 bytes
         * with no required NUL. cfg is zero-initialised, so the tail stays 0. */
        memcpy(cfg.sta.ssid, creds->wifi_ssid,
               strnlen(creds->wifi_ssid, sizeof(cfg.sta.ssid)));
        memcpy(cfg.sta.password, creds->wifi_pass,
               strnlen(creds->wifi_pass, sizeof(cfg.sta.password)));
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG,
                            "set_config failed (before start)");
        ESP_LOGI(TAG, "credentials applied for \"%s\"", creds->wifi_ssid);
    }

#if CONFIG_INV_SOFTAP_ENABLE
    {
        wifi_config_t ap = { 0 };
        size_t ssid_len = strnlen(CONFIG_INV_SOFTAP_SSID, sizeof(ap.ap.ssid));
        memcpy(ap.ap.ssid, CONFIG_INV_SOFTAP_SSID, ssid_len);
        ap.ap.ssid_len = ssid_len;
        ap.ap.max_connection = 4;
        ap.ap.channel = 0;              /* 0 = follow the station's channel */
        size_t pw_len = strlen(CONFIG_INV_SOFTAP_PASSWORD);
        if (pw_len >= 8) {
            memcpy(ap.ap.password, CONFIG_INV_SOFTAP_PASSWORD,
                   strnlen(CONFIG_INV_SOFTAP_PASSWORD, sizeof(ap.ap.password)));
            ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
        } else {
            /* WPA2 requires >=8 characters; anything shorter must be open or the AP
             * silently fails to start. */
            ap.ap.authmode = WIFI_AUTH_OPEN;
            if (pw_len) {
                ESP_LOGW(TAG, "softap password shorter than 8 chars -- opening the AP");
            }
        }
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG,
                            "softap set_config failed");
        ESP_LOGI(TAG, "softap configured: \"%s\" (%s)", CONFIG_INV_SOFTAP_SSID,
                 ap.ap.authmode == WIFI_AUTH_OPEN ? "open" : "WPA2");
    }
#endif

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    /* Precautionary settle time: the radio lives on another chip behind an RPC
     * link, so start() returning does not strictly guarantee the PHY is up.
     * MEASURED: this delay is NOT required -- scans found 14-16 APs with it set to
     * 0 ms. Kept short and deliberate rather than removed, but do not credit it
     * with fixing anything. */
    vTaskDelay(pdMS_TO_TICKS(500));

    return ESP_OK;
}

static void log_country(void)
{
    wifi_country_t country = {0};
    if (esp_wifi_get_country(&country) == ESP_OK) {
        /* cc is not guaranteed NUL-terminated */
        ESP_LOGI(TAG, "country: %c%c  channels %d..%d  max_tx_power %d  policy %d",
                 country.cc[0] ? country.cc[0] : '?', country.cc[1] ? country.cc[1] : '?',
                 country.schan, country.schan + country.nchan - 1,
                 country.max_tx_power, country.policy);
    } else {
        ESP_LOGW(TAG, "esp_wifi_get_country() failed -- country config unknown");
    }
}

static void log_mac(void)
{
    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        ESP_LOGI(TAG, "station MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

/* Run one scan variant and report. Returns the AP count, or -1 on error. */
static int run_scan(const char *label, const wifi_scan_config_t *cfg)
{
    ESP_LOGI(TAG, "--- scan [%s] ---", label);

    esp_err_t err = esp_wifi_scan_start(cfg, true /* block */);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] esp_wifi_scan_start failed: %s", label, esp_err_to_name(err));
        return -1;
    }

    uint16_t count = 0;
    err = esp_wifi_scan_get_ap_num(&count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] scan_get_ap_num failed: %s", label, esp_err_to_name(err));
        return -1;
    }

    if (count == 0) {
        ESP_LOGW(TAG, "[%s] 0 APs", label);
        /* Still clear any buffered records so the next variant starts clean. */
        esp_wifi_clear_ap_list();
        return 0;
    }

    uint16_t to_fetch = count > 20 ? 20 : count;
    wifi_ap_record_t *records = calloc(to_fetch, sizeof(wifi_ap_record_t));
    if (!records) {
        ESP_LOGE(TAG, "[%s] out of memory for %u records", label, to_fetch);
        return (int)count;
    }

    err = esp_wifi_scan_get_ap_records(&to_fetch, records);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] scan_get_ap_records failed: %s", label, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "[%s] *** %u AP(s) ***", label, count);
        for (uint16_t i = 0; i < to_fetch; i++) {
            ESP_LOGI(TAG, "    ssid=\"%s\" rssi=%d ch=%u auth=%d",
                     (const char *)records[i].ssid, records[i].rssi,
                     records[i].primary, records[i].authmode);
        }
    }
    free(records);
    return (int)count;
}

esp_err_t net_wifi_scan_diagnostics(void)
{
    ESP_LOGI(TAG, "================ Wi-Fi scan ================");
    log_mac();
    log_country();

    /*
     * Only the default (NULL) scan config is issued, deliberately.
     *
     * MEASURED on this board with esp_hosted 1.4.7: esp_wifi_scan_start() fails with
     * ESP_FAIL for EVERY non-NULL wifi_scan_config_t -- long active dwell, passive
     * mode, show_hidden, and single-channel were all tried and all failed, each
     * burning the full 5 s DEFAULT_RPC_RSP_TIMEOUT because the C6 slave never
     * answers. So dwell time and channel targeting are simply not available here.
     *
     * Worse, issuing them is not free: six consecutive timed-out RPCs (30 s of them)
     * left the RPC channel unable to complete a subsequent esp_wifi_connect(), which
     * then failed the same way. Avoiding the unsupported calls entirely is both
     * correct and necessary -- do not "just try" them again.
     */
    int count = run_scan("default", NULL);

    ESP_LOGI(TAG, "============================================");
    if (count > 0) {
        ESP_LOGI(TAG, "RESULT: PASS -- %d AP(s) visible.", count);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "RESULT: FAIL -- 0 APs.");
    ESP_LOGE(TAG, "Before suspecting the antenna, check the call order: "
                  "esp_wifi_scan_get_ap_num() MUST precede get_ap_records(), which "
                  "frees the list. That exact bug produced a phantom 'dead radio' in "
                  "projects/09-wifi-ble-companion for a month.");
    return ESP_FAIL;
}

/* ===================== Phase 0b: association + reachability ===================== */

#include "freertos/event_groups.h"
#include "esp_http_client.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <unistd.h>

#define BIT_GOT_IP     BIT0
#define BIT_GAVE_UP    BIT1
#define MAX_RETRIES    8

static EventGroupHandle_t s_net_events;
static int s_retries;
static bool s_connected;
static esp_netif_ip_info_t s_ip;

static const char *disconnect_reason(uint8_t r)
{
    /* The few reasons that actually change what a human should do next. */
    switch (r) {
    case WIFI_REASON_AUTH_FAIL:            return "auth failed (wrong password?)";
    case WIFI_REASON_NO_AP_FOUND:          return "AP not found (wrong SSID, or 5 GHz-only?)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:    return "4-way handshake timeout (wrong password?)";
    case WIFI_REASON_ASSOC_FAIL:           return "association failed";
    case WIFI_REASON_BEACON_TIMEOUT:       return "beacon timeout (out of range?)";
    case WIFI_REASON_CONNECTION_FAIL:      return "connection failed";
    default:                               return "see esp_wifi_types.h";
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        s_connected = false;
        if (s_retries < MAX_RETRIES) {
            s_retries++;
            /* Linear backoff: enough to let an AP recover, short enough to stay usable. */
            vTaskDelay(pdMS_TO_TICKS(500 * s_retries));
            ESP_LOGW(TAG, "disconnected (reason %u: %s) -- retry %d/%d",
                     d->reason, disconnect_reason(d->reason), s_retries, MAX_RETRIES);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "giving up after %d attempts (last reason %u: %s)",
                     MAX_RETRIES, d->reason, disconnect_reason(d->reason));
            xEventGroupSetBits(s_net_events, BIT_GAVE_UP);
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        s_ip = e->ip_info;
        s_connected = true;
        s_retries = 0;
        ESP_LOGI(TAG, "got IP " IPSTR "  mask " IPSTR "  gw " IPSTR,
                 IP2STR(&s_ip.ip), IP2STR(&s_ip.netmask), IP2STR(&s_ip.gw));
        xEventGroupSetBits(s_net_events, BIT_GOT_IP);
    }
}

bool net_wifi_is_connected(void)
{
    return s_connected;
}

esp_err_t net_wifi_connect(const secrets_t *s, uint32_t timeout_ms)
{
    if (!s_net_events) {
        s_net_events = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(s_net_events, ESP_ERR_NO_MEM, TAG, "event group alloc failed");
    }
    xEventGroupClearBits(s_net_events, BIT_GOT_IP | BIT_GAVE_UP);
    s_retries = 0;

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL),
                        TAG, "registering WIFI_EVENT handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL),
                        TAG, "registering IP_EVENT handler failed");

    /* Credentials were already applied in net_wifi_start(), before esp_wifi_start()
     * -- see the comment there for why that ordering is required over ESP-Hosted. */
    ESP_LOGI(TAG, "associating with \"%s\"...", s->wifi_ssid);

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(s_net_events, BIT_GOT_IP | BIT_GAVE_UP,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_GOT_IP) {
        return ESP_OK;
    }
    if (bits & BIT_GAVE_UP) {
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "timed out after %" PRIu32 " ms with no IP", timeout_ms);
    return ESP_ERR_TIMEOUT;
}

esp_err_t net_wifi_check_internet(void)
{
    ESP_LOGI(TAG, "--- reachability check ---");

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_dns_info_t dns = {0};
    if (sta && esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
        ESP_LOGI(TAG, "DNS server: " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
    } else {
        ESP_LOGW(TAG, "no DNS server configured -- name resolution will fail");
    }

    /* 1. DNS. Separated from the HTTP step so a resolver failure is distinguishable
     *    from a routing or server failure. */
    const char *host = "example.com";
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, "80", &hints, &res);
    if (gai != 0 || !res) {
        ESP_LOGE(TAG, "DNS resolution of %s FAILED (getaddrinfo=%d)", host, gai);
        return ESP_FAIL;
    }
    struct in_addr a = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "DNS ok: %s -> %s", host, inet_ntoa(a));
    freeaddrinfo(res);

    /* 2. One real HTTP round-trip. Plain HTTP on purpose: TLS needs a correct clock
     *    (SNTP, Phase 2), and this check is about routing, not certificates. */
    esp_http_client_config_t cfg = {
        .url = "http://example.com/",
        .timeout_ms = 10000,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client, ESP_FAIL, TAG, "http client init failed");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int64_t len = esp_http_client_get_content_length(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "HTTP ok: status %d, %lld bytes", status, len);

    /*
     * Also probe the internal infrastructure subnet. The voice pipeline needs a VM on
     * 10.10.8.x for Whisper STT, and the board sits on 192.168.20.x -- a different
     * subnet reached through the gateway. Internet access does NOT imply that
     * cross-subnet route works, so check it explicitly rather than discovering it
     * later as a mysterious timeout.
     */
    {
        esp_http_client_config_t icfg = {
            .url = CONFIG_INV_VOICE_SVC_URL "/health",
            .timeout_ms = 6000,
            .method = HTTP_METHOD_GET,
        };
        esp_http_client_handle_t ic = esp_http_client_init(&icfg);
        if (ic) {
            esp_err_t ierr = esp_http_client_perform(ic);
            int istatus = esp_http_client_get_status_code(ic);
            esp_http_client_cleanup(ic);
            if (ierr == ESP_OK && istatus == 200) {
                ESP_LOGI(TAG, "voice service REACHABLE at %s -- /health returned %d",
                         CONFIG_INV_VOICE_SVC_URL, istatus);
            } else {
                ESP_LOGW(TAG, "voice service NOT reachable at %s (%s, status %d). "
                              "403 = this board's IP is not on the service allowlist; "
                              "a timeout = no route across subnets.",
                         CONFIG_INV_VOICE_SVC_URL, esp_err_to_name(ierr), istatus);
            }
        }
    }

    return (status >= 200 && status < 400) ? ESP_OK : ESP_FAIL;
}


/* ------------------------- inbound-delivery diagnostic ------------------------- */

static void udp_probe_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "udp probe: socket() failed, errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(9999),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "udp probe: bind() failed, errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "udp probe: listening on 0.0.0.0:9999 -- send unicast AND broadcast");

    char buf[128];
    while (true) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&src, &slen);
        if (n < 0) {
            ESP_LOGW(TAG, "udp probe: recvfrom errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        buf[n] = '\0';
        ESP_LOGW(TAG, "udp probe: *** RECEIVED %d bytes from %s: \"%s\" ***",
                 n, inet_ntoa(src.sin_addr), buf);
    }
}

void net_wifi_start_udp_probe(void)
{
    xTaskCreate(udp_probe_task, "udp_probe", 4096, NULL, 4, NULL);
}


const char *net_wifi_softap_ip(void)
{
#if CONFIG_INV_SOFTAP_ENABLE
    if (!s_ap_netif) {
        return NULL;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_ap_netif, &info) != ESP_OK) {
        return NULL;
    }
    snprintf(s_ap_ip, sizeof(s_ap_ip), IPSTR, IP2STR(&info.ip));
    return s_ap_ip;
#else
    return NULL;
#endif
}
