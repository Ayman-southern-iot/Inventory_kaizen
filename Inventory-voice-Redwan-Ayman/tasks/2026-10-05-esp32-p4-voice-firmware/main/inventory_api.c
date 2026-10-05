/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "inventory_api.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "sdkconfig.h"

static const char *TAG = "inv_api";

/* The live /api/v1/catalogue response measured 47.7 KB with 65 products, so a 48 KB
 * cap left only ~345 bytes of headroom -- one new product would have silently
 * truncated it. 128 KB gives real room to grow. */
#define RESP_LIMIT (128 * 1024)
#define RATE_LIMIT_RETRY_MS 2500

#define CACHE_MAX 200

static inventory_item_t *s_cache;
static size_t s_cache_n;
static int64_t s_cache_at_us = -1;

static char s_base_url[160];
static char s_api_key[200];

typedef struct {
    char  *buf;
    size_t len, cap;
    bool   overflow;
} resp_acc_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    resp_acc_t *acc = evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !acc) {
        return ESP_OK;
    }
    if (acc->len + evt->data_len + 1 > acc->cap) {
        size_t want = (acc->len + evt->data_len + 1) * 2;
        if (want > RESP_LIMIT) {
            acc->overflow = true;
            return ESP_OK;
        }
        char *bigger = realloc(acc->buf, want);
        if (!bigger) {
            acc->overflow = true;
            return ESP_OK;
        }
        acc->buf = bigger;
        acc->cap = want;
    }
    memcpy(acc->buf + acc->len, evt->data, evt->data_len);
    acc->len += evt->data_len;
    acc->buf[acc->len] = '\0';
    return ESP_OK;
}

const char *inventory_api_status_text(int s)
{
    switch (s) {
    case 0:   return "no response (connect/TLS failure)";
    case 200: return "ok";
    case 401: return "key missing, invalid, revoked or expired";
    case 403: return "key type not allowed on this endpoint";
    case 404: return "no such endpoint";
    case 429: return "rate limited (120 req/min per key)";
    default:  return (s >= 500) ? "server error" : "unexpected status";
    }
}

esp_err_t inventory_api_init(const char *base_url, const char *api_key)
{
    if (!base_url || !base_url[0]) {
        s_base_url[0] = '\0';
        ESP_LOGW(TAG, "no base URL configured -- API calls will be refused");
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_base_url, sizeof(s_base_url), "%s", base_url);
    size_t n = strlen(s_base_url);
    if (n && s_base_url[n - 1] == '/') {
        s_base_url[n - 1] = '\0';
    }
    snprintf(s_api_key, sizeof(s_api_key), "%s", api_key ? api_key : "");

    ESP_LOGI(TAG, "base URL: %s", s_base_url);
    /* Length and presence only. The key itself is never logged, never put in a URL,
     * and never sent to the dashboard -- per the IMS rules of the road. */
    ESP_LOGI(TAG, "api key: %s (Authorization: Bearer ***)",
             s_api_key[0] ? "<set, hidden>" : "<NOT SET -- expect HTTP 401>");
    if (strncmp(s_base_url, "https://", 8) != 0) {
        ESP_LOGW(TAG, "base URL is not https:// -- the bearer token will cross the "
                      "network in cleartext");
    }
    return ESP_OK;
}

bool inventory_api_is_configured(void)
{
    return s_base_url[0] != '\0';
}

static esp_err_t http_get(const char *path, resp_acc_t *acc, int *status, uint32_t *ms)
{
    char url[320];
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .event_handler = http_event,
        .user_data = acc,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(c, ESP_FAIL, TAG, "http client init failed");

    if (s_api_key[0]) {
        char hdr[232];
        snprintf(hdr, sizeof(hdr), "Bearer %s", s_api_key);
        esp_http_client_set_header(c, "Authorization", hdr);
    }
    esp_http_client_set_header(c, "Accept", "application/json");

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(c);
    uint32_t took = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (status) *status = st;
    if (ms) *ms = took;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET %s failed: %s (%" PRIu32 " ms)", path, esp_err_to_name(err), took);
        return err;
    }
    ESP_LOGI(TAG, "GET %s -> %d (%" PRIu32 " ms, %u bytes)", path, st, took, (unsigned)acc->len);
    if (acc->overflow) {
        ESP_LOGE(TAG, "response exceeded %d bytes and was truncated", RESP_LIMIT);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* ------------------------------ tolerant JSON helpers ----------------------------- */

static void copy_str(const cJSON *o, const char *key, char *dest, size_t cap)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(v) && v->valuestring) {
        snprintf(dest, cap, "%s", v->valuestring);
    }
}

/* Accept the first key that is present, so minor naming differences in the live API
 * do not silently produce blank fields. */
static void copy_str_any(const cJSON *o, const char *const *keys, char *dest, size_t cap)
{
    dest[0] = '\0';
    for (int i = 0; keys[i]; i++) {
        copy_str(o, keys[i], dest, cap);
        if (dest[0]) {
            return;
        }
    }
}

static int get_int(const cJSON *o, const char *key, int fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsNumber(v)) {
        return v->valueint;
    }
    return fallback;
}

/* id may come back as a string or a number depending on the backend. */
static void copy_id(const cJSON *o, char *dest, size_t cap)
{
    dest[0] = '\0';
    const char *const keys[] = { "id", "_id", "productId", NULL };
    for (int i = 0; keys[i]; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, keys[i]);
        if (cJSON_IsString(v) && v->valuestring) {
            snprintf(dest, cap, "%s", v->valuestring);
            return;
        }
        if (cJSON_IsNumber(v)) {
            snprintf(dest, cap, "%d", v->valueint);
            return;
        }
    }
}

/*
 * Stock may be nested as {"stock":{"total":..,"available":..,"inUse":..}} (what the
 * IMS docs describe) or flattened. Handle both rather than assuming.
 */
static void parse_stock(const cJSON *item, inventory_item_t *out)
{
    out->stock_total = out->stock_available = out->stock_in_use = -1;

    const cJSON *stock = cJSON_GetObjectItemCaseSensitive(item, "stock");
    const cJSON *src = cJSON_IsObject(stock) ? stock : item;

    /* The catalogue nests {total, available, inUse}; /api/v1/products returns FLAT
     * totalQuantity / totalAvailable / totalInUse instead. Both are real shapes from
     * the live API, so accept either rather than assuming one. */
    out->stock_total     = get_int(src, "total",     get_int(item, "totalQuantity",  out->stock_total));
    out->stock_available = get_int(src, "available", get_int(item, "totalAvailable", out->stock_available));
    out->stock_in_use    = get_int(src, "inUse",
                           get_int(src, "in_use",
                           get_int(item, "totalInUse", out->stock_in_use)));
    if (out->stock_total < 0) {
        out->stock_total = get_int(item, "totalOnHand", get_int(item, "totalOwned", -1));
    }

    /* If only a bare quantity is given, treat it as the total. */
    if (out->stock_total < 0) {
        out->stock_total = get_int(src, "quantity", -1);
    }
    /* A total with no explicit available figure is better shown as available than as
     * unknown -- but never invent a number that contradicts one the server sent. */
    if (out->stock_available < 0 && out->stock_total >= 0 && out->stock_in_use < 0) {
        out->stock_available = out->stock_total;
    }
}

static void parse_item(const cJSON *o, inventory_item_t *out)
{
    memset(out, 0, sizeof(*out));
    static const char *code_keys[] = { "code", "sku", "productCode", NULL };
    static const char *name_keys[] = { "name", "title", "productName", NULL };
    static const char *unit_keys[] = { "unit", "uom", NULL };
    static const char *loc_keys[]  = { "shelf", "shelfLabel", "location", "compartment", NULL };

    copy_id(o, out->id, sizeof(out->id));
    copy_str_any(o, code_keys, out->code, sizeof(out->code));
    copy_str_any(o, name_keys, out->name, sizeof(out->name));
    copy_str_any(o, unit_keys, out->unit, sizeof(out->unit));
    copy_str_any(o, loc_keys, out->location, sizeof(out->location));

    /* The real IMS catalogue carries shelves as locations[] with a readable "label",
     * e.g. "Demo Store / Microcontrollers / S2". Prefer that over any flat field. */
    if (!out->location[0]) {
        const cJSON *locs = cJSON_GetObjectItemCaseSensitive(o, "locations");
        if (cJSON_IsArray(locs) && cJSON_GetArraySize(locs) > 0) {
            const cJSON *first = cJSON_GetArrayItem(locs, 0);
            if (cJSON_IsObject(first)) {
                static const char *lbl[] = { "label", "storageId", "compartment", NULL };
                copy_str_any(first, lbl, out->location, sizeof(out->location));
            }
        }
    }

    parse_stock(o, out);
}

/* The payload may be {items:[...]} or a bare array; tolerate both. */
static const cJSON *find_items_array(const cJSON *root, int *total_out)
{
    *total_out = -1;
    if (cJSON_IsArray(root)) {
        *total_out = cJSON_GetArraySize(root);
        return root;
    }
    if (!cJSON_IsObject(root)) {
        return NULL;
    }
    *total_out = get_int(root, "total", -1);
    static const char *keys[] = { "products", "items", "data", "results", NULL };
    for (int i = 0; keys[i]; i++) {
        const cJSON *a = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
        if (cJSON_IsArray(a)) {
            if (*total_out < 0) {
                *total_out = cJSON_GetArraySize(a);
            }
            return a;
        }
    }
    return NULL;
}

/* Percent-encode a search term for safe use in a query string. */
static void urlencode(const char *in, char *out, size_t cap)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 4 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else if (c == ' ') {
            out[o++] = '+';
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
}

/*
 * Fetch the whole catalogue and match locally.
 *
 * Why not GET /api/v1/products?search=<term>, which exists? Because the catalogue
 * endpoint is strictly better here and the IMS docs say so ("Best single call",
 * "fetch and cache ... don't call the API once per user action"):
 *
 *   - It carries readable shelf labels (locations[].label, e.g.
 *     "Demo Store / Microcontrollers / S2"). The paged /products response has no
 *     location at all, so a voice answer could not say where the item is.
 *   - It nests stock as {total, available, inUse}, which is what we want to report.
 *   - ONE request answers any search term, so a second spoken query costs no extra
 *     API call and the 120 req/min budget is never a concern.
 *
 * Cost is ~48 KB per fetch, which is fine on a board with 32 MB of PSRAM.
 */
/* Case-insensitive substring test. */
static bool contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !*haystack || !needle || !*needle) {
        return false;
    }
    size_t nl = strlen(needle);
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nl && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nl) {
            return true;
        }
    }
    return false;
}

size_t inventory_api_cache_count(void)
{
    return s_cache_n;
}

int inventory_api_cache_age_s(void)
{
    if (s_cache_at_us < 0) {
        return -1;
    }
    return (int)((esp_timer_get_time() - s_cache_at_us) / 1000000);
}

esp_err_t inventory_api_refresh(int *http_status, uint32_t *latency_ms)
{
    ESP_RETURN_ON_FALSE(inventory_api_is_configured(), ESP_ERR_INVALID_STATE, TAG,
                        "API not configured");

    if (!s_cache) {
        /* PSRAM: ~200 x 252 bytes is nothing against 32 MB, and keeping it out of
         * internal RAM leaves that for TLS and the web server. */
        s_cache = heap_caps_malloc(CACHE_MAX * sizeof(inventory_item_t), MALLOC_CAP_SPIRAM);
        if (!s_cache) {
            s_cache = calloc(CACHE_MAX, sizeof(inventory_item_t));
        }
        ESP_RETURN_ON_FALSE(s_cache, ESP_ERR_NO_MEM, TAG, "cache alloc failed");
    }

    resp_acc_t acc = {0};
    int status = 0;
    uint32_t ms = 0;
    esp_err_t err = http_get("/api/v1/catalogue", &acc, &status, &ms);

    if (status == 429) {
        ESP_LOGW(TAG, "rate limited; retrying refresh once in %d ms", RATE_LIMIT_RETRY_MS);
        free(acc.buf);
        memset(&acc, 0, sizeof(acc));
        vTaskDelay(pdMS_TO_TICKS(RATE_LIMIT_RETRY_MS));
        err = http_get("/api/v1/catalogue", &acc, &status, &ms);
    }

    if (http_status) *http_status = status;
    if (latency_ms) *latency_ms = ms;

    if (err != ESP_OK || status != 200) {
        if (status != 200 && acc.buf) {
            cJSON *e = cJSON_Parse(acc.buf);
            if (e) {
                char code[48] = "", msg[160] = "";
                copy_str(e, "code", code, sizeof(code));
                copy_str(e, "message", msg, sizeof(msg));
                if (code[0] || msg[0]) {
                    ESP_LOGE(TAG, "IMS error %d: %s -- %s", status, code, msg);
                }
                cJSON_Delete(e);
            }
        }
        ESP_LOGE(TAG, "catalogue refresh failed: %s HTTP %d (%s)", esp_err_to_name(err),
                 status, inventory_api_status_text(status));
        free(acc.buf);
        /* Keep any previous cache: stale data beats no data for answering a question. */
        return (err != ESP_OK) ? err : ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(acc.buf);
    free(acc.buf);
    ESP_RETURN_ON_FALSE(root, ESP_ERR_INVALID_RESPONSE, TAG, "catalogue is not valid JSON");

    int ignored = 0;
    const cJSON *arr = find_items_array(root, &ignored);
    if (!arr) {
        ESP_LOGE(TAG, "no products array in the catalogue");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    size_t n = 0;
    const cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsObject(it)) {
            continue;
        }
        if (n >= CACHE_MAX) {
            ESP_LOGW(TAG, "catalogue exceeds the %d-product cache -- ignoring the rest",
                     CACHE_MAX);
            break;
        }
        parse_item(it, &s_cache[n]);
        if (s_cache[n].code[0] || s_cache[n].name[0]) {
            n++;
        }
    }
    cJSON_Delete(root);

    s_cache_n = n;
    s_cache_at_us = esp_timer_get_time();
    ESP_LOGI(TAG, "catalogue cached: %u products (HTTP %d, %" PRIu32 " ms)",
             (unsigned)n, status, ms);
    return (n > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t inventory_api_match(const char *term, inventory_search_t *res)
{
    ESP_RETURN_ON_FALSE(term && term[0] && res, ESP_ERR_INVALID_ARG, TAG, "bad args");
    memset(res, 0, sizeof(*res));
    res->total = 0;
    res->http_status = 200;      /* no request was made; the cache answered */

    if (!s_cache || s_cache_n == 0) {
        ESP_LOGW(TAG, "catalogue cache is empty -- refresh first");
        return ESP_ERR_INVALID_STATE;
    }

    int matched = 0;
    for (size_t i = 0; i < s_cache_n; i++) {
        const inventory_item_t *p = &s_cache[i];
        if (!contains_ci(p->name, term) && !contains_ci(p->code, term)) {
            continue;
        }
        matched++;
        if (res->n < INV_MAX_RESULTS) {
            res->items[res->n++] = *p;
        }
    }
    res->total = matched;
    return (res->n > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t inventory_api_search(const char *term, inventory_search_t *res)
{
    ESP_RETURN_ON_FALSE(term && term[0] && res, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(inventory_api_is_configured(), ESP_ERR_INVALID_STATE, TAG,
                        "API not configured");

    memset(res, 0, sizeof(*res));
    res->total = -1;

    const char *path = "/api/v1/catalogue";
    resp_acc_t acc = {0};
    esp_err_t err = http_get(path, &acc, &res->http_status, &res->latency_ms);

    /* One retry on 429 per the documented 120/min limit. Retrying harder would only
     * make the rate limiting worse. */
    if (res->http_status == 429) {
        res->rate_limited = true;
        ESP_LOGW(TAG, "rate limited; retrying once in %d ms", RATE_LIMIT_RETRY_MS);
        free(acc.buf);
        memset(&acc, 0, sizeof(acc));
        vTaskDelay(pdMS_TO_TICKS(RATE_LIMIT_RETRY_MS));
        err = http_get(path, &acc, &res->http_status, &res->latency_ms);
    }

    if (err != ESP_OK) {
        free(acc.buf);
        return err;
    }
    if (res->http_status != 200) {
        if (acc.buf) {
            cJSON *e = cJSON_Parse(acc.buf);
            if (e) {
                char code[48] = "", msg[160] = "";
                copy_str(e, "code", code, sizeof(code));
                copy_str(e, "message", msg, sizeof(msg));
                if (code[0] || msg[0]) {
                    ESP_LOGE(TAG, "IMS error %d: %s -- %s", res->http_status, code, msg);
                }
                cJSON_Delete(e);
            }
        }
        ESP_LOGE(TAG, "catalogue fetch failed: HTTP %d (%s)", res->http_status,
                 inventory_api_status_text(res->http_status));
        free(acc.buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(acc.buf);
    free(acc.buf);
    ESP_RETURN_ON_FALSE(root, ESP_ERR_INVALID_RESPONSE, TAG, "catalogue is not valid JSON");

    int ignored = 0;
    const cJSON *arr = find_items_array(root, &ignored);
    if (!arr) {
        ESP_LOGE(TAG, "no products array in the catalogue");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    int scanned = 0, matched = 0;
    const cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsObject(it)) {
            continue;
        }
        scanned++;

        inventory_item_t cand;
        parse_item(it, &cand);
        if (!contains_ci(cand.name, term) && !contains_ci(cand.code, term)) {
            continue;
        }
        matched++;
        if (res->n < INV_MAX_RESULTS) {
            res->items[res->n++] = cand;
        }
    }
    cJSON_Delete(root);

    res->total = matched;
    ESP_LOGI(TAG, "catalogue: %d product(s); \"%s\" matched %d, showing %u",
             scanned, term, matched, (unsigned)res->n);
    for (size_t i = 0; i < res->n; i++) {
        const inventory_item_t *p = &res->items[i];
        ESP_LOGI(TAG, "    [%u] %-16s %-32s avail %d/%d %s @ %s", (unsigned)i,
                 p->code[0] ? p->code : "-", p->name[0] ? p->name : "(unnamed)",
                 p->stock_available, p->stock_total,
                 p->unit[0] ? p->unit : "", p->location[0] ? p->location : "-");
    }
    return (res->n > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}
