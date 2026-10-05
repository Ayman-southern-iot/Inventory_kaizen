/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "secrets.h"

#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "secrets";
static const char *NAMESPACE = "inventory";

/* Copy an NVS string into a fixed buffer. Missing keys yield an empty string. */
static esp_err_t get_str(nvs_handle_t h, const char *key, char *dest, size_t cap)
{
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, key, dest, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        dest[0] = '\0';
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (err != ESP_OK) {
        dest[0] = '\0';
    }
    return err;
}

esp_err_t secrets_load(secrets_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS namespace '%s' not found (%s).", NAMESPACE, esp_err_to_name(err));
        ESP_LOGE(TAG, "Provision it first:  python tools/provision_nvs.py --port <port>");
        return err;
    }

    esp_err_t ssid_err = get_str(h, "wifi_ssid", out->wifi_ssid, sizeof(out->wifi_ssid));
    esp_err_t pass_err = get_str(h, "wifi_pass", out->wifi_pass, sizeof(out->wifi_pass));
    /* Optional until the inventory API details are known. */
    get_str(h, "api_base", out->api_base, sizeof(out->api_base));
    get_str(h, "api_key", out->api_key, sizeof(out->api_key));
    get_str(h, "voice_tok", out->voice_tok, sizeof(out->voice_tok));
    get_str(h, "voice_tok2", out->voice_tok2, sizeof(out->voice_tok2));

    nvs_close(h);

    if (ssid_err != ESP_OK || out->wifi_ssid[0] == '\0') {
        ESP_LOGE(TAG, "required key 'wifi_ssid' is missing or empty");
        return ESP_ERR_NOT_FOUND;
    }
    if (pass_err != ESP_OK) {
        /* An open network legitimately has no password, so empty is allowed --
         * but a read error is not the same thing as deliberately empty. */
        ESP_LOGW(TAG, "'wifi_pass' not found; treating the network as open");
    }

    return ESP_OK;
}

/*
 * Reveal NOTHING of a secret -- not even a prefix.
 *
 * The first version of this printed the first four characters ("%.4s***"), which put
 * four characters of the live Wi-Fi password into the serial log on every boot. Four
 * characters is four an attacker does not have to guess, and serial logs get pasted
 * into tickets and chat. Length alone is enough to catch a paste error.
 */
static void log_redacted_field(const char *label, const char *value)
{
    size_t n = strlen(value);
    if (n == 0) {
        ESP_LOGI(TAG, "  %-10s <not set>", label);
    } else {
        ESP_LOGI(TAG, "  %-10s <%u chars, hidden>", label, (unsigned)n);
    }
}

void secrets_log_redacted(const secrets_t *s)
{
    ESP_LOGI(TAG, "loaded from NVS namespace '%s':", NAMESPACE);
    /* The SSID is broadcast over the air by definition, so it is not a secret and
     * printing it in full is what makes "joined the wrong network" diagnosable. */
    ESP_LOGI(TAG, "  %-10s \"%s\"", "ssid", s->wifi_ssid);
    log_redacted_field("password", s->wifi_pass);
    ESP_LOGI(TAG, "  %-10s %s", "api_base", s->api_base[0] ? s->api_base : "<not set>");
    log_redacted_field("api_key", s->api_key);
    log_redacted_field("voice_tok", s->voice_tok);
    log_redacted_field("voice_tok2", s->voice_tok2);
}
