/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Wi-Fi bring-up for the ESP32-P4-WIFI6-DEV-KIT.
 *
 * The P4 has NO radio of its own. Wi-Fi is provided by the onboard ESP32-C6
 * reached over SDIO via ESP-Hosted, with esp_wifi_remote making the ordinary
 * esp_wifi.h API proxy transparently to the C6.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#include "secrets.h"

/** Bring up netif + event loop + the Wi-Fi station on the C6. Does not scan or connect. */
esp_err_t net_wifi_start(const secrets_t *creds);

/** SoftAP IPv4 address as a string, or NULL if the SoftAP is not running. */
const char *net_wifi_softap_ip(void);

/**
 * Phase 0 diagnostic: run a battery of scans with different parameters and report
 * what each finds.
 *
 * This exists because a plain default scan on this board has twice returned
 * "0 APs found" while the SDIO transport and RPC round-trip were demonstrably
 * healthy -- and while a laptop on the same desk could see several 2.4 GHz
 * networks. That rules out an empty room, so the cause is either scan parameters,
 * country/channel configuration, or the RF path itself. Each variant below
 * isolates one of those.
 *
 * @return ESP_OK if ANY variant found at least one AP, ESP_FAIL otherwise.
 */
esp_err_t net_wifi_scan_diagnostics(void);

/**
 * Associate with the configured access point and wait for an IPv4 lease.
 *
 * Retries with backoff on disconnect. Blocks until an IP arrives or the timeout
 * expires. The password is never logged.
 *
 * @param s          credentials loaded by secrets_load()
 * @param timeout_ms how long to wait for IP_EVENT_STA_GOT_IP
 */
esp_err_t net_wifi_connect(const secrets_t *s, uint32_t timeout_ms);

/** True once an IPv4 address is held. */
bool net_wifi_is_connected(void);

/**
 * Prove the link actually reaches beyond the LAN: resolve a hostname by DNS, then
 * make one plain HTTP GET. Association and a DHCP lease alone do NOT prove this --
 * project 08 held an IP on a switch that could not route anywhere.
 */
esp_err_t net_wifi_check_internet(void);

/**
 * Start a UDP listener on port 9999 that logs every datagram it receives.
 *
 * Diagnostic for a specific, important question: when an external host cannot reach
 * this board's TCP server, is the AP blocking station-to-station traffic, or does
 * ESP-Hosted simply never deliver inbound packets from the C6 to the P4?
 *
 *   receives BROADCAST but not UNICAST -> AP client isolation
 *   receives NEITHER                   -> inbound over ESP-Hosted is broken
 *   receives BOTH                      -> inbound works; the problem is TCP/port-specific
 */
void net_wifi_start_udp_probe(void);
