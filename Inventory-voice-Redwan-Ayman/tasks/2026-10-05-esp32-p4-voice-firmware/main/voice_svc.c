/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "voice_svc.h"

#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "voice_svc";

#define CALL_TIMEOUT_MS   20000   /* guide says 15-20 s per call */
#define MAX_ATTEMPTS      3       /* 1 try + 2 retries, network/5xx only */
#define RETRY_DELAY_MS    600
#define PCM_RESP_LIMIT    (1024 * 1024)  /* 15 s @ 16 kHz 16-bit = 480 KB */

static char s_base[96];
static char s_token[160];
static SemaphoreHandle_t s_lock;   /* the service handles ONE call at a time */

typedef struct {
    char  *buf;
    size_t len, cap;
    bool   overflow;
} acc_t;

static esp_err_t collect(esp_http_client_event_t *evt)
{
    acc_t *a = evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !a) {
        return ESP_OK;
    }
    if (a->len + evt->data_len + 1 > a->cap) {
        size_t want = (a->len + evt->data_len + 1) * 2;
        if (want > PCM_RESP_LIMIT) {
            a->overflow = true;
            return ESP_OK;
        }
        char *b = realloc(a->buf, want);
        if (!b) {
            a->overflow = true;
            return ESP_OK;
        }
        a->buf = b;
        a->cap = want;
    }
    memcpy(a->buf + a->len, evt->data, evt->data_len);
    a->len += evt->data_len;
    a->buf[a->len] = '\0';
    return ESP_OK;
}

const char *voice_svc_status_text(int s)
{
    switch (s) {
    case 0:   return "no response (no route or service down)";
    case 200: return "ok";
    case 400: return "BAD_AUDIO -- odd byte count, not 16-bit PCM (firmware bug)";
    case 401: return "UNAUTHENTICATED -- token missing or wrong";
    case 403: return "FORBIDDEN_NETWORK -- this board's source address is not allowlisted";
    case 413: return "AUDIO_TOO_LONG -- recording exceeded 15 s";
    case 422: return "validation -- empty or over-long text";
    default:  return (s >= 500) ? "server error (retryable)" : "unexpected status";
    }
}

esp_err_t voice_svc_init(const char *base_url, const char *token)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex alloc failed");
    }
    if (!base_url || !base_url[0]) {
        s_base[0] = '\0';
        ESP_LOGW(TAG, "no base URL -- voice calls disabled");
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_base, sizeof(s_base), "%s", base_url);
    size_t n = strlen(s_base);
    if (n && s_base[n - 1] == '/') {
        s_base[n - 1] = '\0';
    }
    snprintf(s_token, sizeof(s_token), "%s", token ? token : "");

    ESP_LOGI(TAG, "voice service: %s", s_base);
    /* Presence and length only; the token itself is never logged. */
    ESP_LOGI(TAG, "token: %s", s_token[0] ? "<set, hidden>" : "<NOT SET -- expect 401>");
    return ESP_OK;
}

bool voice_svc_is_configured(void)
{
    return s_base[0] != '\0';
}

/*
 * One HTTP call, serialised and with bounded retries.
 *
 * body==NULL means GET. Retries happen only for transport errors and 5xx: the guide
 * is explicit that a 4xx is deterministic, so re-sending it wastes a round trip and,
 * on a single-threaded service, blocks somebody else's turn.
 */
static esp_err_t call(const char *path, esp_http_client_method_t method,
                      const char *content_type, const void *body, size_t body_len,
                      acc_t *acc, int *status_out, uint32_t *ms_out)
{
    char url[192];
    snprintf(url, sizeof(url), "%s%s", s_base, path);

    esp_err_t err = ESP_FAIL;
    int status = 0;
    uint32_t took = 0;

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        free(acc->buf);
        memset(acc, 0, sizeof(*acc));

        esp_http_client_config_t cfg = {
            .url = url,
            .method = method,
            .timeout_ms = CALL_TIMEOUT_MS,
            .event_handler = collect,
            .user_data = acc,
        };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) {
            err = ESP_FAIL;
            break;
        }

        if (s_token[0]) {
            char hdr[192];
            snprintf(hdr, sizeof(hdr), "Bearer %s", s_token);
            esp_http_client_set_header(c, "Authorization", hdr);
        }
        if (content_type) {
            esp_http_client_set_header(c, "Content-Type", content_type);
        }
        if (body && body_len) {
            esp_http_client_set_post_field(c, (const char *)body, (int)body_len);
        }

        int64_t t0 = esp_timer_get_time();
        err = esp_http_client_perform(c);
        took = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        status = esp_http_client_get_status_code(c);
        esp_http_client_cleanup(c);

        bool retryable = (err != ESP_OK) || (status >= 500);
        if (!retryable) {
            break;
        }
        if (attempt < MAX_ATTEMPTS) {
            ESP_LOGW(TAG, "%s attempt %d/%d failed (%s, status %d) -- retrying",
                     path, attempt, MAX_ATTEMPTS, esp_err_to_name(err), status);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        }
    }

    if (s_lock) {
        xSemaphoreGive(s_lock);
    }

    if (status_out) *status_out = status;
    if (ms_out) *ms_out = took;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s (%" PRIu32 " ms)", path, esp_err_to_name(err), took);
        return err;
    }
    if (acc->overflow) {
        ESP_LOGE(TAG, "%s response exceeded %d bytes", path, PCM_RESP_LIMIT);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void json_field(const char *body, const char *key, char *dest, size_t cap)
{
    dest[0] = '\0';
    if (!body) {
        return;
    }
    cJSON *o = cJSON_Parse(body);
    if (!o) {
        return;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(v) && v->valuestring) {
        snprintf(dest, cap, "%s", v->valuestring);
    }
    cJSON_Delete(o);
}

esp_err_t voice_svc_health(int *http_status, char *model, size_t model_cap,
                          char *voice, size_t voice_cap)
{
    ESP_RETURN_ON_FALSE(voice_svc_is_configured(), ESP_ERR_INVALID_STATE, TAG,
                        "not configured");
    acc_t acc = {0};
    int status = 0;
    uint32_t ms = 0;
    esp_err_t err = call("/health", HTTP_METHOD_GET, NULL, NULL, 0, &acc, &status, &ms);

    if (err == ESP_OK && status == 200) {
        if (model && model_cap) json_field(acc.buf, "model", model, model_cap);
        if (voice && voice_cap) json_field(acc.buf, "voice", voice, voice_cap);
        ESP_LOGI(TAG, "/health 200 in %" PRIu32 " ms  model=%s voice=%s", ms,
                 (model && model[0]) ? model : "?", (voice && voice[0]) ? voice : "?");
    } else {
        ESP_LOGE(TAG, "/health -> %d (%s)", status, voice_svc_status_text(status));
        if (acc.buf && acc.len) {
            ESP_LOGE(TAG, "  body: %.160s", acc.buf);
        }
    }

    free(acc.buf);
    if (http_status) *http_status = status;
    if (err != ESP_OK) return err;
    return (status == 200) ? ESP_OK : ESP_FAIL;
}

esp_err_t voice_svc_stt(const int16_t *pcm, size_t samples, voice_stt_result_t *out)
{
    ESP_RETURN_ON_FALSE(pcm && samples && out, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(voice_svc_is_configured(), ESP_ERR_INVALID_STATE, TAG,
                        "not configured");
    memset(out, 0, sizeof(*out));

    /* Cap locally rather than letting the service answer 413 -- which it may deliver
     * as a connection reset mid-upload, a far more confusing failure. */
    size_t max_samples = (size_t)VOICE_PCM_RATE * VOICE_MAX_SECONDS;
    if (samples > max_samples) {
        ESP_LOGW(TAG, "recording %u samples exceeds the %d s limit; truncating",
                 (unsigned)samples, VOICE_MAX_SECONDS);
        samples = max_samples;
    }

    acc_t acc = {0};
    esp_err_t err = call("/stt", HTTP_METHOD_POST, "application/octet-stream",
                         pcm, samples * sizeof(int16_t),
                         &acc, &out->http_status, &out->latency_ms);
    if (err != ESP_OK) {
        free(acc.buf);
        return err;
    }
    if (out->http_status != 200) {
        ESP_LOGE(TAG, "/stt -> %d (%s)", out->http_status,
                 voice_svc_status_text(out->http_status));
        free(acc.buf);
        return ESP_FAIL;
    }

    cJSON *o = cJSON_Parse(acc.buf);
    free(acc.buf);
    ESP_RETURN_ON_FALSE(o, ESP_ERR_INVALID_RESPONSE, TAG, "/stt body is not JSON");

    const cJSON *t = cJSON_GetObjectItemCaseSensitive(o, "text");
    if (cJSON_IsString(t) && t->valuestring) {
        snprintf(out->text, sizeof(out->text), "%s", t->valuestring);
    }
    const cJSON *ms = cJSON_GetObjectItemCaseSensitive(o, "ms");
    out->server_ms = cJSON_IsNumber(ms) ? ms->valueint : -1;
    cJSON_Delete(o);

    if (!out->text[0]) {
        /* Documented and expected: no speech heard. Not a failure. */
        ESP_LOGW(TAG, "/stt heard no speech (empty text) after %" PRIu32 " ms",
                 out->latency_ms);
    } else {
        ESP_LOGI(TAG, "/stt (%.1f s audio) -> \"%s\"  [server %d ms, board %" PRIu32 " ms]",
                 (float)samples / VOICE_PCM_RATE, out->text, out->server_ms,
                 out->latency_ms);
    }
    return ESP_OK;
}

esp_err_t voice_svc_tts(const char *text, int16_t **pcm_out, size_t *bytes_out,
                        int *http_status, uint32_t *latency_ms)
{
    ESP_RETURN_ON_FALSE(text && text[0] && pcm_out && bytes_out, ESP_ERR_INVALID_ARG,
                        TAG, "bad args");
    ESP_RETURN_ON_FALSE(voice_svc_is_configured(), ESP_ERR_INVALID_STATE, TAG,
                        "not configured");
    *pcm_out = NULL;
    *bytes_out = 0;

    if (strlen(text) > VOICE_TTS_MAX_CHARS) {
        ESP_LOGE(TAG, "text is %u chars, over the %d limit", (unsigned)strlen(text),
                 VOICE_TTS_MAX_CHARS);
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *req = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(req, ESP_ERR_NO_MEM, TAG, "json alloc failed");
    cJSON_AddStringToObject(req, "text", text);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    ESP_RETURN_ON_FALSE(body, ESP_ERR_NO_MEM, TAG, "json print failed");

    acc_t acc = {0};
    int status = 0;
    uint32_t ms = 0;
    esp_err_t err = call("/tts", HTTP_METHOD_POST, "application/json",
                         body, strlen(body), &acc, &status, &ms);
    free(body);

    if (http_status) *http_status = status;
    if (latency_ms) *latency_ms = ms;

    if (err != ESP_OK) {
        free(acc.buf);
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "/tts -> %d (%s)", status, voice_svc_status_text(status));
        free(acc.buf);
        return ESP_FAIL;
    }
    /* An odd byte count cannot be 16-bit PCM; refuse rather than play noise. */
    if (acc.len < 2 || (acc.len % 2) != 0) {
        ESP_LOGE(TAG, "/tts returned %u bytes -- not whole 16-bit samples",
                 (unsigned)acc.len);
        free(acc.buf);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *pcm_out = (int16_t *)acc.buf;   /* ownership transfers to the caller */
    *bytes_out = acc.len;
    ESP_LOGI(TAG, "/tts -> %u bytes (%.1f s audio) in %" PRIu32 " ms",
             (unsigned)acc.len, (float)(acc.len / 2) / VOICE_PCM_RATE, ms);
    return ESP_OK;
}

/* ------------- OpenAI-compatible multipart transcription ------------- */

#define MP_BOUNDARY "----esp32p4VoiceBoundary7a3f"

/* Captures response headers so we can report exactly what the server sent back --
 * useful while the server's contract is still being pinned down. */
typedef struct {
    acc_t body;
    char  hdrs[512];
    size_t hdr_len;
    int   server_ms;
} mp_acc_t;

static esp_err_t mp_event(esp_http_client_event_t *evt)
{
    mp_acc_t *m = evt->user_data;
    if (!m) {
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        /* The OpenAI layer reports its own processing time in a header, not in the
         * JSON body -- which is why server_ms came back as -1 from the "ms" field. */
        if (evt->header_key && strcasecmp(evt->header_key, "x-processing-time-ms") == 0
            && evt->header_value) {
            m->server_ms = atoi(evt->header_value);
        }
        int n = snprintf(m->hdrs + m->hdr_len, sizeof(m->hdrs) - m->hdr_len,
                         "%s: %s\n", evt->header_key ? evt->header_key : "?",
                         evt->header_value ? evt->header_value : "");
        if (n > 0 && (size_t)n < sizeof(m->hdrs) - m->hdr_len) {
            m->hdr_len += n;
        }
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        esp_http_client_event_t e = *evt;
        e.user_data = &m->body;
        return collect(&e);
    }
    return ESP_OK;
}

esp_err_t voice_svc_transcribe_wav(const uint8_t *wav, size_t wav_len,
                                   const char *token, const char *model,
                                   voice_stt_result_t *out)
{
    ESP_RETURN_ON_FALSE(wav && wav_len && out, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(voice_svc_is_configured(), ESP_ERR_INVALID_STATE, TAG,
                        "not configured");
    memset(out, 0, sizeof(*out));
    if (!model || !model[0]) {
        model = "whisper-1";
    }

    /* Assemble the multipart body. Two parts: the file, then the model field.
     * esp_http_client needs one contiguous buffer, so the whole thing is built in
     * PSRAM -- 72 KB of WAV plus a small envelope is nothing against 32 MB. */
    const char *pre_fmt =
        "--" MP_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    const char *mid_fmt =
        "\r\n--" MP_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
        "%s"
        "\r\n--" MP_BOUNDARY "--\r\n";

    char mid[160];
    int mid_len = snprintf(mid, sizeof(mid), mid_fmt, model);
    size_t pre_len = strlen(pre_fmt);
    size_t total = pre_len + wav_len + (size_t)mid_len;

    char *body = malloc(total);
    ESP_RETURN_ON_FALSE(body, ESP_ERR_NO_MEM, TAG,
                        "could not allocate a %u-byte multipart body", (unsigned)total);
    memcpy(body, pre_fmt, pre_len);
    memcpy(body + pre_len, wav, wav_len);
    memcpy(body + pre_len + wav_len, mid, (size_t)mid_len);

    char url[192];
    snprintf(url, sizeof(url), "%s/v1/audio/transcriptions", s_base);
    ESP_LOGI(TAG, "POST %s  (multipart, %u bytes: %u WAV + %u envelope, model=%s)",
             url, (unsigned)total, (unsigned)wav_len,
             (unsigned)(pre_len + mid_len), model);

    mp_acc_t m = { .server_ms = -1 };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CALL_TIMEOUT_MS,
        .event_handler = mp_event,
        .user_data = &m,
    };

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        if (s_lock) xSemaphoreGive(s_lock);
        free(body);
        return ESP_FAIL;
    }
    if (token && token[0]) {
        char hdr[192];
        snprintf(hdr, sizeof(hdr), "Bearer %s", token);
        esp_http_client_set_header(c, "Authorization", hdr);
    }
    esp_http_client_set_header(c, "Content-Type",
                               "multipart/form-data; boundary=" MP_BOUNDARY);
    esp_http_client_set_post_field(c, body, (int)total);

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(c);
    out->latency_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    out->http_status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
    free(body);

    /* Report the full exchange verbatim: while the server's contract is unsettled,
     * the raw headers and body are the evidence, not a summary of them. */
    ESP_LOGI(TAG, "---- response: HTTP %d in %" PRIu32 " ms ----",
             out->http_status, out->latency_ms);
    if (m.hdr_len) {
        ESP_LOGI(TAG, "---- headers ----\n%s", m.hdrs);
    }
    if (m.body.buf && m.body.len) {
        ESP_LOGI(TAG, "---- body (%u bytes) ----\n%.400s", (unsigned)m.body.len, m.body.buf);
    } else {
        ESP_LOGW(TAG, "---- body: empty ----");
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "transport failed: %s", esp_err_to_name(err));
        free(m.body.buf);
        return err;
    }

    if (out->http_status == 200 && m.body.buf) {
        /* OpenAI shape is {"text": "..."}; keep the native "ms" if it is offered. */
        cJSON *o = cJSON_Parse(m.body.buf);
        if (o) {
            const cJSON *t = cJSON_GetObjectItemCaseSensitive(o, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                snprintf(out->text, sizeof(out->text), "%s", t->valuestring);
            }
            const cJSON *ms = cJSON_GetObjectItemCaseSensitive(o, "ms");
            out->server_ms = cJSON_IsNumber(ms) ? ms->valueint : m.server_ms;
            cJSON_Delete(o);
        }
        if (out->server_ms < 0) {
            out->server_ms = m.server_ms;
        }
        ESP_LOGI(TAG, "transcript: \"%s\"", out->text);
    }

    free(m.body.buf);
    return (out->http_status == 200) ? ESP_OK : ESP_FAIL;
}


/*
 * Detect Whisper's repetition hallucination.
 *
 * Approach: take the first few words as a candidate phrase and count how many times
 * it recurs. A real answer may repeat a word, but it does not repeat the same opening
 * 3-word phrase four times. Also catches the degenerate single-word case.
 */
bool voice_svc_looks_hallucinated(const char *text)
{
    if (!text || !text[0]) {
        return false;
    }
    size_t len = strlen(text);
    if (len < 24) {
        return false;          /* too short to be a loop */
    }

    /* Build a lowercase copy with runs of whitespace/punctuation collapsed to one
     * space, so "They grow. They grow" and "they grow they grow" compare equal. */
    char *norm = malloc(len + 1);
    if (!norm) {
        return false;
    }
    size_t n = 0;
    bool in_space = true;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (isalnum(c)) {
            norm[n++] = (char)tolower(c);
            in_space = false;
        } else if (!in_space) {
            norm[n++] = ' ';
            in_space = true;
        }
    }
    while (n && norm[n - 1] == ' ') {
        n--;
    }
    norm[n] = '\0';

    /* Candidate phrase = first 3 words (or 2 if that is all there is). */
    int spaces = 0;
    size_t cut = n;
    for (size_t i = 0; i < n; i++) {
        if (norm[i] == ' ' && ++spaces == 3) {
            cut = i;
            break;
        }
    }
    if (cut < 4) {
        free(norm);
        return false;
    }

    char phrase[64];
    size_t plen = (cut < sizeof(phrase) - 1) ? cut : sizeof(phrase) - 1;
    memcpy(phrase, norm, plen);
    phrase[plen] = '\0';

    int hits = 0;
    for (const char *p = norm; (p = strstr(p, phrase)) != NULL; p += plen) {
        hits++;
        if (hits >= 4) {
            break;
        }
    }
    free(norm);

    if (hits >= 4) {
        ESP_LOGW(TAG, "transcript looks hallucinated: \"%.40s...\" repeats the opening "
                      "phrase %d+ times -- discarding", text, hits);
        return true;
    }
    return false;
}
