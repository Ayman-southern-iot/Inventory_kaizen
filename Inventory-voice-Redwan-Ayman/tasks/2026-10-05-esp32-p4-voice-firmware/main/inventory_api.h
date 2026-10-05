/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * IMS inventory API adapter.
 *
 * Targets the real API at https://ims.siot.solutions:
 *
 *   GET /api/v1/products?search=<term>&limit=100
 *       -> { "items": [ {id, code, name, unit, stock:{total,available,inUse}, ...} ],
 *            "page": 1, "limit": 100, "total": N }
 *   GET /api/v1/catalogue        -> every product with stock + shelf labels
 *
 * Auth is ALWAYS `Authorization: Bearer <key>` per the IMS docs.
 *
 * Operational rules from those docs that shape this code:
 *   - 120 requests/minute per key; HTTP 429 means back off and retry.
 *   - There is no push feed. The board is the ONE server that polls; browsers get
 *     pushed to over the dashboard's WebSocket instead of polling IMS themselves.
 *   - The key must never reach a web page, browser JavaScript, or a URL query string.
 *     It lives in NVS and is only ever sent in a request header.
 *
 * Field parsing is deliberately TOLERANT: several shapes are accepted for the same
 * value, because the exact JSON has not yet been observed against the live endpoint.
 * Missing fields degrade to empty/-1 rather than failing the whole response.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INV_ID_LEN        48
#define INV_CODE_LEN      32
#define INV_NAME_LEN      80
#define INV_UNIT_LEN      16
#define INV_LOC_LEN       64
#define INV_MAX_RESULTS   12   /* a voice answer only needs the top few */

typedef struct {
    char id[INV_ID_LEN];
    char code[INV_CODE_LEN];
    char name[INV_NAME_LEN];
    char unit[INV_UNIT_LEN];
    char location[INV_LOC_LEN];
    int  stock_total;      /* -1 if absent */
    int  stock_available;
    int  stock_in_use;
} inventory_item_t;

typedef struct {
    inventory_item_t items[INV_MAX_RESULTS];
    size_t   n;              /* items actually filled                  */
    int      total;          /* server's total match count, -1 unknown */
    int      http_status;
    uint32_t latency_ms;
    bool     rate_limited;   /* true if a 429 was seen */
} inventory_search_t;

/** base_url without a trailing slash, e.g. "https://ims.siot.solutions". */
esp_err_t inventory_api_init(const char *base_url, const char *api_key);
bool inventory_api_is_configured(void);

/**
 * Search products by free-text term, e.g. "arduino".
 *
 * Retries once on HTTP 429 after a short delay, per the documented rate limit.
 * res->http_status and latency are filled even on failure, so the dashboard can show
 * WHY a lookup failed rather than just that it did.
 */
/** Products held in the in-memory cache. 0 until the first refresh succeeds. */
size_t inventory_api_cache_count(void);

/** Seconds since the cache was last refreshed, or -1 if never. */
int inventory_api_cache_age_s(void);

/**
 * Fetch /api/v1/catalogue and replace the in-memory cache.
 *
 * Call at boot and every few minutes, as the IMS guide advises -- not per question.
 * The catalogue is unpaginated (the server refuses above 5000 products rather than
 * truncating), so this is one request regardless of how many searches follow.
 */
esp_err_t inventory_api_refresh(int *http_status, uint32_t *latency_ms);

/**
 * Match a term against the CACHED catalogue. No HTTP, so it is instant and a spoken
 * question can try several candidate words at no cost.
 */
esp_err_t inventory_api_match(const char *term, inventory_search_t *res);

esp_err_t inventory_api_search(const char *term, inventory_search_t *res);

/** Human-readable explanation for an IMS status code, for the dashboard. */
const char *inventory_api_status_text(int http_status);
