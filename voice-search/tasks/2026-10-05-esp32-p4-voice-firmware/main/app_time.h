/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Wall-clock time via SNTP.
 *
 * This is not a nicety: TLS certificate validation compares the certificate's
 * validity window against the system clock. This board boots at the epoch (1970)
 * because no RTC battery is fitted by default, so every HTTPS handshake would fail
 * with a "certificate is not yet valid" error until the clock is set.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/** Start SNTP and block until the clock looks sane, or the timeout expires. */
esp_err_t app_time_sync(uint32_t timeout_ms);

/** True once the clock is plausibly correct (i.e. well past the epoch). */
bool app_time_is_valid(void);

/** Current time as an ISO-8601-ish string, for logs and the dashboard. */
void app_time_iso8601(char *out, size_t cap);
