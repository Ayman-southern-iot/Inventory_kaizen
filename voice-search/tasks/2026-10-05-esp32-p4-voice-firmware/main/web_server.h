/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Dashboard web server.
 *
 * Serves one self-contained page from flash and pushes pipeline events to every
 * connected browser over a WebSocket. The page is embedded (not on a CDN) because the
 * board may be on a network with no route to the internet, and a dashboard that only
 * renders when the internet is up would be useless exactly when it is needed.
 *
 *   GET  /          the dashboard (gzip-encoded)
 *   GET  /ws        WebSocket: live pipeline events
 *   GET  /api/state JSON: retained event history, for the initial render
 */

#pragma once

#include "esp_err.h"

esp_err_t web_server_start(void);
void web_server_stop(void);

/** Number of currently attached WebSocket clients. */
int web_server_client_count(void);

/**
 * Fetch our own pages over 127.0.0.1 and report whether they really serve.
 *
 * This exists because "httpd_start() returned OK" is NOT evidence that a server works
 * -- projects/08-ethernet-web-server shipped marked "done" on exactly that basis and
 * had never served a single byte. A loopback request proves the handlers run, the page
 * is intact in flash, and the response is well-formed, independently of whether any
 * external client can route to the board.
 */
esp_err_t web_server_selftest(void);
