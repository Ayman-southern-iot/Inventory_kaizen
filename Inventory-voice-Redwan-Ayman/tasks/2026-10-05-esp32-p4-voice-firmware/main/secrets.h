/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * NVS-backed secrets.
 *
 * Credentials and the API key are provisioned into NVS by tools/provision_nvs.py
 * from a gitignored .env. They are deliberately NOT in Kconfig: sdkconfig.defaults
 * is a committed file, so anything placed there would end up in git history.
 *
 * Nothing in this module ever logs a secret in full.
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>

#define SECRETS_MAX_LEN 128

typedef struct {
    char wifi_ssid[33];                 /* 32 chars + NUL, per 802.11 */
    char wifi_pass[65];                 /* 64 chars + NUL, WPA2 max   */
    char api_base[SECRETS_MAX_LEN];     /* may be empty until the API is known */
    char api_key[SECRETS_MAX_LEN];      /* may be empty until the API is known */
    char voice_tok[SECRETS_MAX_LEN];    /* bearer token for the native /stt API    */
    char voice_tok2[SECRETS_MAX_LEN];   /* token for the OpenAI-compatible /v1 API */
} secrets_t;

/**
 * Load secrets from the NVS namespace written by tools/provision_nvs.py.
 *
 * Wi-Fi SSID and password are required; api_base/api_key may be absent while the
 * inventory API details are still pending. A missing required key returns an error
 * naming which one, rather than proceeding with an empty credential.
 */
esp_err_t secrets_load(secrets_t *out);

/** Log what was loaded, with every secret redacted. Safe to call freely. */
void secrets_log_redacted(const secrets_t *s);
