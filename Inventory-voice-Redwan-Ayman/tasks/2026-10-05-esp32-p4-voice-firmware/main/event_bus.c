/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "event_bus.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "event_bus";

#define HISTORY_DEPTH 40
#define EVENT_JSON_MAX 512

static const char *STAGE_NAMES[EV_STAGE_MAX] = {
    [EV_INFO]      = "info",
    [EV_WAKEWORD]  = "wakeword",
    [EV_COMMAND]   = "command",
    [EV_SKU]       = "sku",
    [EV_HTTP_REQ]  = "http_request",
    [EV_HTTP_RESP] = "http_response",
    [EV_PARSE]     = "parse",
    [EV_RESULT]    = "result",
    [EV_ERROR]     = "error",
};

static const char *STATUS_NAMES[] = { "pending", "ok", "fail" };

static event_sink_fn s_sink;
static SemaphoreHandle_t s_lock;

/* Ring buffer of retained events, so a late-joining browser sees recent context. */
static char *s_history[HISTORY_DEPTH];
static size_t s_head;      /* next write slot */
static size_t s_count;     /* how many slots are populated */
static uint32_t s_seq;

const char *event_stage_name(event_stage_t stage)
{
    if (stage < 0 || stage >= EV_STAGE_MAX || !STAGE_NAMES[stage]) {
        return "info";
    }
    return STAGE_NAMES[stage];
}

esp_err_t event_bus_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

void event_bus_set_sink(event_sink_fn sink)
{
    s_sink = sink;
}

/* Store in the ring (taking ownership of a copy) and hand to the sink. */
static void emit(char *json)
{
    if (!json) {
        return;
    }

    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
        free(s_history[s_head]);
        s_history[s_head] = strdup(json);
        s_head = (s_head + 1) % HISTORY_DEPTH;
        if (s_count < HISTORY_DEPTH) {
            s_count++;
        }
        xSemaphoreGive(s_lock);
    }

    /* The sink is called outside the lock: broadcasting can block on sockets, and
     * holding the history mutex across that would stall every publisher. */
    event_sink_fn sink = s_sink;
    if (sink) {
        sink(json);
    }
    free(json);
}

/* Common envelope: seq, uptime, stage, status -- callers add their own fields. */
static cJSON *new_event(event_stage_t stage, event_status_t status)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "seq", ++s_seq);
    cJSON_AddNumberToObject(o, "t_ms", (double)(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(o, "stage", event_stage_name(stage));
    cJSON_AddStringToObject(o, "status",
                            STATUS_NAMES[(status < 3) ? status : EV_STATUS_PENDING]);
    return o;
}

void event_bus_publish(event_stage_t stage, event_status_t status, const char *fmt, ...)
{
    char msg[EVENT_JSON_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* Mirror to the serial log too: the dashboard is convenient but the serial log is
     * the record that survives a browser that was not open at the time. */
    if (status == EV_STATUS_FAIL) {
        ESP_LOGE(TAG, "[%s] %s", event_stage_name(stage), msg);
    } else {
        ESP_LOGI(TAG, "[%s] %s", event_stage_name(stage), msg);
    }

    cJSON *o = new_event(stage, status);
    if (!o) {
        return;
    }
    cJSON_AddStringToObject(o, "message", msg);
    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    emit(json);
}

void event_bus_publish_result(bool ok,
                              const char *sku,
                              const char *name,
                              bool available,
                              int quantity,
                              const char *unit,
                              const char *location,
                              int http_status,
                              unsigned latency_ms,
                              const char *error)
{
    cJSON *o = new_event(EV_RESULT, ok ? EV_STATUS_OK : EV_STATUS_FAIL);
    if (!o) {
        return;
    }
    cJSON_AddBoolToObject(o, "ok", ok);
    cJSON_AddStringToObject(o, "sku", sku ? sku : "");
    cJSON_AddStringToObject(o, "name", name ? name : "");
    cJSON_AddNumberToObject(o, "http_status", http_status);
    cJSON_AddNumberToObject(o, "latency_ms", latency_ms);
    if (ok) {
        cJSON_AddBoolToObject(o, "available", available);
        cJSON_AddNumberToObject(o, "quantity", quantity);
        cJSON_AddStringToObject(o, "unit", unit ? unit : "");
        cJSON_AddStringToObject(o, "location", location ? location : "");
    } else {
        cJSON_AddStringToObject(o, "error", error ? error : "unknown error");
    }

    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);

    if (ok) {
        ESP_LOGI(TAG, "[result] %s: %s qty=%d (HTTP %d, %u ms)", sku ? sku : "?",
                 available ? "available" : "out of stock", quantity, http_status, latency_ms);
    } else {
        ESP_LOGE(TAG, "[result] %s FAILED: %s (HTTP %d)", sku ? sku : "?",
                 error ? error : "?", http_status);
    }
    emit(json);
}

char *event_bus_history_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }

    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
        /* Walk oldest -> newest. */
        size_t start = (s_count == HISTORY_DEPTH) ? s_head : 0;
        for (size_t i = 0; i < s_count; i++) {
            const char *item = s_history[(start + i) % HISTORY_DEPTH];
            if (!item) {
                continue;
            }
            cJSON *parsed = cJSON_Parse(item);
            if (parsed) {
                cJSON_AddItemToArray(arr, parsed);
            }
        }
        xSemaphoreGive(s_lock);
    }

    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}
