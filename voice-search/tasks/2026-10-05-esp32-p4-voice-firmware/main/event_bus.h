/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Pipeline event bus.
 *
 * Every stage of a query publishes a structured event here, and the bus fans it out
 * to whatever is listening (the dashboard's WebSocket, and later the LED). There is
 * deliberately ONE event path: the dashboard renders what the firmware actually did
 * rather than a parallel narrative that can drift out of sync with it.
 *
 * A short ring buffer is kept so a browser that connects mid-query still sees the
 * recent history instead of an empty pane.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    EV_INFO = 0,      /* general progress                     */
    EV_WAKEWORD,      /* wake word detected                   */
    EV_COMMAND,       /* a phrase was recognised              */
    EV_SKU,           /* phrase resolved to a product SKU     */
    EV_HTTP_REQ,      /* about to call the API                */
    EV_HTTP_RESP,     /* status + latency came back            */
    EV_PARSE,         /* JSON parsed                          */
    EV_RESULT,        /* final answer for the dashboard       */
    EV_ERROR,         /* anything failed                      */
    EV_STAGE_MAX
} event_stage_t;

typedef enum {
    EV_STATUS_PENDING = 0,
    EV_STATUS_OK,
    EV_STATUS_FAIL,
} event_status_t;

/** Sink signature: receives a complete JSON object as a NUL-terminated string. */
typedef void (*event_sink_fn)(const char *json);

esp_err_t event_bus_init(void);

/** Register the broadcast sink (the web server does this). One sink is enough here. */
void event_bus_set_sink(event_sink_fn sink);

/** Publish a progress/status event. printf-style message. */
void event_bus_publish(event_stage_t stage, event_status_t status, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * Publish the final structured result, which the dashboard's right-hand pane renders.
 * Passing ok=false emits an error card instead.
 */
void event_bus_publish_result(bool ok,
                              const char *sku,
                              const char *name,
                              bool available,
                              int quantity,
                              const char *unit,
                              const char *location,
                              int http_status,
                              unsigned latency_ms,
                              const char *error);

/**
 * Serialise the retained ring buffer as a JSON array, for a newly connected client.
 * Returns a malloc'd string the caller must free, or NULL.
 */
char *event_bus_history_json(void);

/** Human-readable stage name, also used as the JSON "stage" value. */
const char *event_stage_name(event_stage_t stage);
