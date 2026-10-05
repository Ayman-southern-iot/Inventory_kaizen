/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Inventory query console -- ESP32-P4-WIFI6-DEV-KIT.
 *
 * Queries the IMS inventory API over HTTPS and shows the whole pipeline live on a
 * board-hosted web dashboard: the process on one side, the fetched result on the
 * other. An external LED on GPIO26 lights when a fetch SUCCEEDS and stays dark when
 * it fails.
 *
 * Voice input (ESP-SR WakeNet + MultiNet, as proven in projects/22-voice-control-led)
 * is the next phase; for now the search term comes from CONFIG_INV_SEARCH_TERM and the
 * query runs on a timer, exercising exactly the code path the voice trigger will use.
 *
 * Startup is a sequence of explicit gates rather than one pass/fail, because each can
 * fail for a different reason and a single verdict would hide which:
 *   1 radio   2 associate+DHCP   3 DNS+HTTP   4 clock   5 TLS
 *   6 API search   7 dashboard serves real bytes
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"

#include "net_wifi.h"
#include "secrets.h"
#include "app_time.h"
#include "inventory_api.h"
#include "event_bus.h"
#include "web_server.h"
#include "led_action.h"
#include "voice_svc.h"
#include "board_audio.h"
#include "query_parse.h"


static const char *TAG = "inv_console";

/* One query: emit every stage onto the bus, drive the LED from the outcome. */
static void run_query(const char *term)
{
    event_bus_publish(EV_COMMAND, EV_STATUS_OK, "search request: \"%s\"", term);

    if (!net_wifi_is_connected()) {
        event_bus_publish(EV_ERROR, EV_STATUS_FAIL, "network is down");
        event_bus_publish_result(false, term, "", false, 0, "", "", 0, 0, "network down");
        led_action_set(false);
        return;
    }
    if (!inventory_api_is_configured()) {
        event_bus_publish(EV_ERROR, EV_STATUS_FAIL, "no API base URL provisioned");
        event_bus_publish_result(false, term, "", false, 0, "", "", 0, 0,
                                 "API not configured");
        led_action_set(false);
        return;
    }

    /* Must name the endpoint actually called. The adapter fetches the whole catalogue
     * and matches locally (see inventory_api.c for why), so advertising a
     * /products?search= request here would make the process pane lie about what the
     * firmware did -- which is precisely what this panel exists to prevent. */
    event_bus_publish(EV_HTTP_REQ, EV_STATUS_PENDING,
                      "GET /api/v1/catalogue  [Bearer ***]  then match \"%s\" locally", term);

    inventory_search_t res;
    esp_err_t err = inventory_api_search(term, &res);

    if (res.rate_limited) {
        event_bus_publish(EV_HTTP_RESP, EV_STATUS_FAIL,
                          "429 rate limited -- retried once after backoff");
    }

    event_bus_publish(err == ESP_OK ? EV_HTTP_RESP : EV_ERROR,
                      err == ESP_OK ? EV_STATUS_OK : EV_STATUS_FAIL,
                      "HTTP %d (%s) in %" PRIu32 " ms",
                      res.http_status, inventory_api_status_text(res.http_status),
                      res.latency_ms);

    if (err != ESP_OK) {
        char why[112];
        snprintf(why, sizeof(why), "%s - %s", esp_err_to_name(err),
                 inventory_api_status_text(res.http_status));
        event_bus_publish_result(false, term, "", false, 0, "", "",
                                 res.http_status, res.latency_ms, why);
        /* The LED rule is about the FETCH, not about stock. A failed fetch = dark. */
        led_action_set(false);
        return;
    }

    event_bus_publish(EV_PARSE, EV_STATUS_OK, "parsed %u of %d match(es)",
                      (unsigned)res.n, res.total);

    /* Secondary matches go in the process pane so a near-miss is visible rather than
     * silently discarded in favour of the top hit. */
    for (size_t i = 1; i < res.n; i++) {
        const inventory_item_t *p = &res.items[i];
        event_bus_publish(EV_INFO, EV_STATUS_OK, "also matched: %s %s (avail %d)",
                          p->code[0] ? p->code : "-", p->name, p->stock_available);
    }

    const inventory_item_t *top = &res.items[0];
    char name[INV_NAME_LEN + 24];
    if (res.total > (int)res.n) {
        snprintf(name, sizeof(name), "%s  (+%d more)", top->name, res.total - (int)res.n);
    } else {
        snprintf(name, sizeof(name), "%s", top->name);
    }

    event_bus_publish_result(true,
                             top->code[0] ? top->code : top->id,
                             name,
                             top->stock_available > 0,
                             top->stock_available,
                             top->unit,
                             top->location,
                             res.http_status,
                             res.latency_ms,
                             NULL);
    led_action_set(true);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== IMS Inventory Console (ESP32-P4) ===");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erasing; provisioned secrets would be lost");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(event_bus_init());
    ESP_ERROR_CHECK(led_action_init());

    secrets_t secrets;
    if (secrets_load(&secrets) != ESP_OK) {
        ESP_LOGE(TAG, "no credentials. Provision with:");
        ESP_LOGE(TAG, "  python tools/provision_nvs.py --port <port>");
        return;
    }
    secrets_log_redacted(&secrets);

    ESP_ERROR_CHECK(net_wifi_start(&secrets));

    bool scan_ok  = (net_wifi_scan_diagnostics() == ESP_OK);
    bool assoc_ok = scan_ok && (net_wifi_connect(&secrets, 30000) == ESP_OK);
    bool reach_ok = assoc_ok && (net_wifi_check_internet() == ESP_OK);

    /* TLS validates certificates against the clock, and this board boots at the epoch
     * (no RTC battery), so SNTP must land before the first https:// request. */
    bool clock_ok = reach_ok && (app_time_sync(CONFIG_INV_SNTP_TIMEOUT_MS) == ESP_OK);

    bool tls_ok = false;
    if (clock_ok) {
        esp_http_client_config_t t = {
            .url = "https://example.com/", .method = HTTP_METHOD_GET,
            .timeout_ms = 15000, .crt_bundle_attach = esp_crt_bundle_attach,
        };
        esp_http_client_handle_t c = esp_http_client_init(&t);
        if (c) {
            esp_err_t terr = esp_http_client_perform(c);
            int ts = esp_http_client_get_status_code(c);
            esp_http_client_cleanup(c);
            tls_ok = (terr == ESP_OK && ts >= 200 && ts < 400);
            ESP_LOGI(TAG, "HTTPS probe: %s (status %d)", tls_ok ? "ok" : "FAILED", ts);
        }
    }

    bool api_ok = false;
    if (secrets.api_base[0]) {
        api_ok = (inventory_api_init(secrets.api_base, secrets.api_key) == ESP_OK);
    } else {
        ESP_LOGW(TAG, "INVENTORY_BASE_URL not provisioned; set it in .env and re-run "
                      "tools/provision_nvs.py");
    }

    /* First real search, used as the startup gate. */
    bool search_ok = false;
    if (api_ok && reach_ok) {
        /* Cache the catalogue once. Every spoken question then matches locally and
         * instantly -- the IMS guide explicitly asks callers not to fetch per query. */
        int cs = 0;
        uint32_t cms = 0;
        if (inventory_api_refresh(&cs, &cms) == ESP_OK) {
            inventory_search_t probe;
            search_ok = (inventory_api_match(CONFIG_INV_SEARCH_TERM, &probe) == ESP_OK);
            ESP_LOGI(TAG, "cached %u products in %" PRIu32 " ms; \"%s\" matches %d",
                     (unsigned)inventory_api_cache_count(), cms,
                     CONFIG_INV_SEARCH_TERM, probe.total);
        }
    }

    /* --- voice service: authenticated /health, which is Ayman's requested item 1 --- */
    bool voice_ok = false;
    char vmodel[48] = "", vvoice[48] = "";
    int vstatus = 0;
    if (secrets.voice_tok[0]) {
        voice_svc_init(CONFIG_INV_VOICE_SVC_URL, secrets.voice_tok);
        voice_ok = (voice_svc_health(&vstatus, vmodel, sizeof(vmodel),
                                     vvoice, sizeof(vvoice)) == ESP_OK);
    } else {
        ESP_LOGW(TAG, "VOICE_TOKEN not provisioned -- skipping voice service checks");
    }

    /* A /health 200 only proves auth and the route. Synthesising a real sentence
     * proves the service can actually do work and that the PCM we get back is
     * well-formed -- the same distinction as "server started" vs "server served". */
    bool tts_ok = false;
    uint32_t tts_ms = 0;
    size_t tts_bytes = 0;
    if (voice_ok) {
        int16_t *pcm = NULL;
        int st = 0;
        if (voice_svc_tts("Arduino Mega, 35 available, Demo Store shelf S2",
                          &pcm, &tts_bytes, &st, &tts_ms) == ESP_OK) {
            tts_ok = true;
            free(pcm);
        }
    }

    bool web_ok = (web_server_start() == ESP_OK);
    bool web_serves = web_ok && (web_server_selftest() == ESP_OK);

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {0};
    if (sta) {
        esp_netif_get_ip_info(sta, &ip);
    }

    ESP_LOGI(TAG, "==================== GATES ====================");
    ESP_LOGI(TAG, "  1. radio / scan        : %s", scan_ok    ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  2. associate + DHCP    : %s", assoc_ok   ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  3. DNS + HTTP          : %s", reach_ok   ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  4. SNTP clock          : %s", clock_ok   ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  5. HTTPS + cert        : %s", tls_ok     ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  6. IMS search \"%s\"%*s: %s", CONFIG_INV_SEARCH_TERM,
             (int)(10 - strlen(CONFIG_INV_SEARCH_TERM)), "", search_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  7. dashboard serves    : %s", web_serves ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "  8. voice /health       : %s (HTTP %d)%s%s",
             voice_ok ? "PASS" : "FAIL", vstatus,
             vmodel[0] ? "  model=" : "", vmodel[0] ? vmodel : "");
    ESP_LOGI(TAG, "  9. voice /tts round    : %s%s", tts_ok ? "PASS" : "FAIL",
             tts_ok ? "" : " (needs 8 first)");
    if (tts_ok) {
        ESP_LOGI(TAG, "       -> %u bytes of PCM (%.1f s) in %" PRIu32 " ms",
                 (unsigned)tts_bytes, (float)(tts_bytes / 2) / VOICE_PCM_RATE, tts_ms);
    }
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  dashboard (LAN)   : http://" IPSTR "/", IP2STR(&ip.ip));
#if CONFIG_INV_SOFTAP_ENABLE
    const char *ap_ip = net_wifi_softap_ip();
    if (ap_ip) {
        ESP_LOGI(TAG, "  dashboard (SoftAP): http://%s/", ap_ip);
        ESP_LOGI(TAG, "    join Wi-Fi \"%s\" to reach it directly", CONFIG_INV_SOFTAP_SSID);
    }
#else
    /* Station-only build: the dashboard exists only at the LAN address above. */
    ESP_LOGI(TAG, "  (SoftAP disabled -- station-only build)");
#endif
    ESP_LOGI(TAG, "==============================================");

    event_bus_publish(EV_INFO, EV_STATUS_OK, "console ready; polling \"%s\" every %ds",
                      CONFIG_INV_SEARCH_TERM, CONFIG_INV_POLL_SECONDS);

    /* ---------------- live voice loop ---------------- */
    if (board_audio_init() != ESP_OK) {
        ESP_LOGE(TAG, "microphone init failed -- falling back to timed text queries");
        while (true) {
            run_query(CONFIG_INV_SEARCH_TERM);
            vTaskDelay(pdMS_TO_TICKS(CONFIG_INV_POLL_SECONDS * 1000));
        }
    }

    const size_t rec_samples = (size_t)CONFIG_INV_RECORD_SECONDS * MIC_SAMPLE_RATE;
    const size_t pcm_bytes = rec_samples * sizeof(int16_t);

    /* One buffer holding the 44-byte WAV header followed by the samples, so the whole
     * thing can be POSTed without a second copy. Recording writes past the header. */
    uint8_t *wav = heap_caps_malloc(44 + pcm_bytes, MALLOC_CAP_SPIRAM);
    if (!wav) {
        wav = malloc(44 + pcm_bytes);
    }
    if (!wav) {
        ESP_LOGE(TAG, "could not allocate %u bytes for the recording buffer",
                 (unsigned)(44 + pcm_bytes));
        return;
    }
    int16_t *pcm = (int16_t *)(wav + 44);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "######################################################");
    ESP_LOGI(TAG, "#  SPEAK WHEN YOU SEE 'RECORDING NOW'                #");
    ESP_LOGI(TAG, "#  Hold the board ~10-15 cm away and speak clearly.  #");
    ESP_LOGI(TAG, "#  Try: \"where is arduino\"                          #");
    ESP_LOGI(TAG, "######################################################");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2500));

        /* Refresh between questions, never during one: a stale answer is bad, but a
         * 400 ms stall after the user has finished speaking is worse. */
        if (inventory_api_cache_age_s() > CONFIG_INV_CACHE_MAX_AGE_S) {
            int cs = 0;
            uint32_t cms = 0;
            if (inventory_api_refresh(&cs, &cms) == ESP_OK) {
                event_bus_publish(EV_INFO, EV_STATUS_OK,
                                  "catalogue refreshed: %u products in %" PRIu32 " ms",
                                  (unsigned)inventory_api_cache_count(), cms);
            }
        }

        ESP_LOGI(TAG, "");
        ESP_LOGW(TAG, ">>> RECORDING NOW for %d s -- SPEAK <<<", CONFIG_INV_RECORD_SECONDS);
        event_bus_publish(EV_INFO, EV_STATUS_PENDING, "recording %d s of audio",
                          CONFIG_INV_RECORD_SECONDS);

        float rms = 0;
        int16_t peak = 0;
        if (board_audio_record_mono(pcm, rec_samples, &rms, &peak) != ESP_OK) {
            continue;
        }
        float dbfs = board_audio_dbfs(rms);
        ESP_LOGI(TAG, "<<< recording done: %.1f dBFS, peak %d >>>", dbfs, peak);
        event_bus_publish(EV_INFO, EV_STATUS_OK, "captured %.1f dBFS, peak %d", dbfs, peak);

        /*
         * Energy gate. Measured: every capture below about -35 dBFS produced either a
         * Whisper hallucination or an empty result, and not one produced usable text.
         * Uploading that audio cannot help and can actively mislead, so stop here.
         */
        if (dbfs < (float)CONFIG_INV_MIN_SPEECH_DBFS) {
            ESP_LOGW(TAG, "TOO QUIET (%.1f dBFS, gate %d) -- not sending. Speak louder "
                          "and closer; the one correct transcript so far was -26.5 dBFS.",
                     dbfs, CONFIG_INV_MIN_SPEECH_DBFS);
            event_bus_publish(EV_ERROR, EV_STATUS_FAIL,
                              "too quiet: %.1f dBFS (need > %d)", dbfs,
                              CONFIG_INV_MIN_SPEECH_DBFS);
            continue;
        }

        board_audio_write_wav_header(wav, pcm_bytes);

        /* Mirror the exact bytes we are about to transcribe, so the audio can be heard
         * and measured off-board. Failures are indistinguishable from the serial log
         * alone. Best-effort: a dead sink must never block the query. */
        if (strlen(CONFIG_INV_WAV_SINK_URL) > 0) {
            esp_http_client_config_t scfg = {
                .url = CONFIG_INV_WAV_SINK_URL,
                .method = HTTP_METHOD_POST,
                .timeout_ms = 8000,
            };
            esp_http_client_handle_t sc = esp_http_client_init(&scfg);
            if (sc) {
                esp_http_client_set_header(sc, "Content-Type", "audio/wav");
                esp_http_client_set_post_field(sc, (const char *)wav, (int)(44 + pcm_bytes));
                esp_err_t serr = esp_http_client_perform(sc);
                ESP_LOGI(TAG, "mirrored capture to sink: %s",
                         (serr == ESP_OK) ? "ok" : esp_err_to_name(serr));
                esp_http_client_cleanup(sc);
            }
        }

        voice_stt_result_t r;
        esp_err_t terr = voice_svc_transcribe_wav(wav, 44 + pcm_bytes,
                                                  secrets.voice_tok2[0] ? secrets.voice_tok2
                                                                        : secrets.voice_tok,
                                                  "whisper-1", &r);
        if (terr != ESP_OK) {
            event_bus_publish(EV_ERROR, EV_STATUS_FAIL, "STT failed: HTTP %d",
                              r.http_status);
            continue;
        }
        if (!r.text[0]) {
            ESP_LOGW(TAG, "no speech heard -- say it again, louder");
            event_bus_publish(EV_ERROR, EV_STATUS_FAIL, "no speech heard");
            continue;
        }
        /* A hallucinated loop reads as perfectly ordinary text to anything that only
         * checks for empty, so it has to be rejected explicitly or it gets parsed as
         * a query and searched for. */
        if (voice_svc_looks_hallucinated(r.text)) {
            event_bus_publish(EV_ERROR, EV_STATUS_FAIL,
                              "discarded a hallucinated transcript (%.1f dBFS audio)", dbfs);
            continue;
        }

        ESP_LOGI(TAG, "================================================");
        ESP_LOGI(TAG, "  YOU SAID: \"%s\"", r.text);
        ESP_LOGI(TAG, "  (server %d ms, round trip %" PRIu32 " ms)", r.server_ms, r.latency_ms);
        event_bus_publish(EV_COMMAND, EV_STATUS_OK, "heard: \"%s\"", r.text);

        /* ---- transcript -> search term -> cached catalogue ---- */
        qp_candidates_t cand;
        if (query_parse(r.text, &cand) == 0) {
            ESP_LOGW(TAG, "  no product word in that sentence");
            event_bus_publish(EV_ERROR, EV_STATUS_FAIL, "no product word recognised");
            led_action_set(false);
            continue;
        }

        /* Try each candidate against the CACHE, so several words cost no API calls.
         * Longest first, because the more specific word is the better search term. */
        inventory_search_t hit;
        const char *used = NULL;
        for (size_t i = 0; i < cand.n; i++) {
            if (inventory_api_match(cand.terms[i], &hit) == ESP_OK) {
                used = cand.terms[i];
                break;
            }
            ESP_LOGI(TAG, "  \"%s\" -> no match", cand.terms[i]);
        }

        if (!used) {
            ESP_LOGW(TAG, "  nothing in the catalogue matched any candidate");
            event_bus_publish(EV_ERROR, EV_STATUS_FAIL,
                              "no catalogue match for \"%s\"", r.text);
            led_action_set(false);
            continue;
        }

        event_bus_publish(EV_SKU, EV_STATUS_OK, "matched \"%s\": %d product(s)",
                          used, hit.total);

        const inventory_item_t *top = &hit.items[0];
        ESP_LOGI(TAG, "  ------------------------------------------");
        ESP_LOGI(TAG, "  SEARCH \"%s\" -> %d match(es), cache age %ds",
                 used, hit.total, inventory_api_cache_age_s());
        for (size_t i = 0; i < hit.n; i++) {
            const inventory_item_t *p = &hit.items[i];
            ESP_LOGI(TAG, "    %c %-14s %-34s avail %d/%d %s @ %s",
                     (i == 0) ? '*' : ' ',
                     p->code[0] ? p->code : "-", p->name, p->stock_available,
                     p->stock_total, p->unit, p->location[0] ? p->location : "-");
        }

        /* The sentence we would speak once TTS is available. Built and logged now so
         * the wording can be judged before the audio path exists. */
        char say[VOICE_TTS_MAX_CHARS];
        if (hit.total > 3) {
            snprintf(say, sizeof(say),
                     "That matches %d products. Can you be more specific?", hit.total);
        } else if (top->stock_available <= 0 && top->stock_total > 0) {
            snprintf(say, sizeof(say), "%s is here but all %d are in use.",
                     top->name, top->stock_total);
        } else {
            snprintf(say, sizeof(say), "%s. %d available. %s.", top->name,
                     top->stock_available,
                     top->location[0] ? top->location : "location unknown");
        }
        ESP_LOGI(TAG, "  WOULD SAY: \"%s\"", say);
        ESP_LOGI(TAG, "  ------------------------------------------");

        event_bus_publish_result(true, top->code[0] ? top->code : top->id, top->name,
                                 top->stock_available > 0, top->stock_available,
                                 top->unit, top->location, hit.http_status, r.latency_ms,
                                 NULL);
        led_action_set(true);
    }
}
