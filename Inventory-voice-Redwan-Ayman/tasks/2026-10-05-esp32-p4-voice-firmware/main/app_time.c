/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "app_time.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_netif.h"

static const char *TAG = "app_time";

/* 2024-01-01. Anything earlier means SNTP has not landed yet; comparing against a
 * fixed sane floor is more reliable than trusting a sync callback that can fire
 * before the clock is actually stepped. */
#define SANE_EPOCH 1704067200

bool app_time_is_valid(void)
{
    time_t now = 0;
    time(&now);
    return now > SANE_EPOCH;
}

void app_time_iso8601(char *out, size_t cap)
{
    time_t now = 0;
    struct tm tm_info;
    time(&now);
    gmtime_r(&now, &tm_info);
    strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}

esp_err_t app_time_sync(uint32_t timeout_ms)
{
    if (app_time_is_valid()) {
        char buf[32];
        app_time_iso8601(buf, sizeof(buf));
        ESP_LOGI(TAG, "clock already set: %s", buf);
        return ESP_OK;
    }

    /*
     * num_of_servers must equal the NUMBER OF STRINGS in the list -- it is the array
     * length, not a slot count. index_of_first_server is a separate lwIP *slot*
     * offset, so setting it to 1 leaves slot 0 free for whatever DHCP offers while our
     * one static server lands in slot 1. From esp_netif_sntp.c:
     *
     *     for (int i = 0; i < num_of_servers; ++i)
     *         sntp_setservername(i + index_of_first_server, servers[i]);
     *
     * Passing num_of_servers=2 with a single-entry list therefore reads servers[1] out
     * of bounds and panics with a Load access fault on a garbage pointer -- which is
     * exactly what happened here before this was corrected.
     *
     * server_from_dhcp additionally requires CONFIG_LWIP_DHCP_GET_NTP_SRV=y; without
     * it, esp_netif_sntp_init() returns ESP_ERR_INVALID_ARG and logs why.
     */
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        1, ESP_SNTP_SERVER_LIST("pool.ntp.org"));
    cfg.start = true;
    cfg.server_from_dhcp = true;
    cfg.index_of_first_server = 1;
    cfg.renew_servers_after_new_IP = true;
    cfg.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
    cfg.sync_cb = NULL;

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_sntp_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "waiting up to %" PRIu32 " ms for SNTP...", timeout_ms);
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP did not sync (%s) -- HTTPS will fail certificate "
                      "validation until the clock is set", esp_err_to_name(err));
        return err;
    }

    char buf[32];
    app_time_iso8601(buf, sizeof(buf));
    ESP_LOGI(TAG, "clock set via SNTP: %s (UTC)", buf);
    return app_time_is_valid() ? ESP_OK : ESP_FAIL;
}
