/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Client for the speech service on the infra VM (http://10.10.8.51:8099).
 *
 *   POST /stt    raw PCM in  -> {"text": "...", "ms": N}
 *   POST /tts    {"text":..} -> raw PCM out
 *   GET  /health             -> {"ok": true, "model": "...", "voice": "..."}
 *
 * Audio is raw PCM, 16-bit signed little-endian, 16 kHz, mono, with NO container --
 * exactly what esp_codec_dev produces and consumes, so nothing has to be parsed.
 *
 * Operational rules from the service's integration guide, enforced here:
 *   - Every call carries `Authorization: Bearer <token>`.
 *   - ONE request at a time: the service transcribes/synthesises serially because it
 *     shares a host. A mutex serialises callers rather than trusting discipline.
 *   - Retry ONLY on network errors and 5xx, at most twice. A 4xx is deterministic --
 *     retrying it just repeats the same answer.
 *   - /stt returns an EMPTY text when no speech was heard. That is normal, not an
 *     error; the caller should say "I didn't catch that" and stop.
 *   - Recordings must be <= 15 s or the service answers 413 (possibly as a connection
 *     reset mid-upload, because it replies and hangs up early).
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VOICE_PCM_RATE      16000
#define VOICE_MAX_SECONDS   15
#define VOICE_TTS_MAX_CHARS 500
#define VOICE_TEXT_MAX      256

typedef struct {
    char     text[VOICE_TEXT_MAX];  /* empty string == no speech heard */
    int      server_ms;             /* the service's own reported time  */
    int      http_status;
    uint32_t latency_ms;            /* measured on the board           */
} voice_stt_result_t;

esp_err_t voice_svc_init(const char *base_url, const char *token);
bool      voice_svc_is_configured(void);

/** GET /health. Fills model/voice names if the caller provides buffers. */
esp_err_t voice_svc_health(int *http_status, char *model, size_t model_cap,
                          char *voice, size_t voice_cap);

/** POST /stt with raw PCM. ESP_OK even when text is empty (silence is not an error). */
esp_err_t voice_svc_stt(const int16_t *pcm, size_t samples, voice_stt_result_t *out);

/**
 * POST /tts. Allocates the returned PCM; caller frees it.
 * @param pcm_out    receives a malloc'd buffer of 16-bit samples
 * @param bytes_out  its size in bytes
 */
esp_err_t voice_svc_tts(const char *text, int16_t **pcm_out, size_t *bytes_out,
                        int *http_status, uint32_t *latency_ms);

/** Maps a status code to something worth logging or speaking. */
const char *voice_svc_status_text(int http_status);

/**
 * True if a transcript looks like a Whisper hallucination rather than speech.
 *
 * Fed low-SNR audio, Whisper does not return an empty string -- it emits a short
 * phrase repeated many times. Measured on this hardware:
 *     "They grow. They grow. They grow."            x23
 *     "Thank you very much, thank you very much,"   x12
 *     "I am going to do this, I am going to do this" x11
 * Those are indistinguishable from real text to a caller that only checks for empty,
 * so they must be detected and discarded or they will be parsed as a query.
 */
bool voice_svc_looks_hallucinated(const char *text);

/* ---------------- OpenAI-compatible transcription (/v1/audio/transcriptions) --------------
 *
 * The same server also exposes an OpenAI-shaped layer, discovered because its errors use a
 * different envelope than the native /stt API:
 *
 *   /v1/audio/transcriptions -> {"error":{"message":..,"type":..,"code":..}}   OpenAI style
 *   /stt, /tts, /health      -> {"code":..,"message":..}                        native style
 *
 * This variant takes a WAV file as multipart/form-data with a `model` field, i.e. what the
 * documented curl does:
 *
 *   curl -H "Authorization: Bearer <tok>" -F file=@audio.wav -F model=whisper-1 \
 *        http://host:8099/v1/audio/transcriptions
 *
 * It costs more on an embedded client than /stt (multipart envelope + a 44-byte WAV header,
 * and the whole body assembled in RAM) but it is portable to any OpenAI-compatible server.
 */

/**
 * POST a WAV file to /v1/audio/transcriptions as multipart/form-data.
 *
 * @param wav      complete WAV bytes (header included)
 * @param wav_len  length of wav
 * @param token    bearer token to use (this layer may want a different one than /stt)
 * @param model    `model` form field, e.g. "whisper-1"
 * @param out      transcript, status and timing
 */
esp_err_t voice_svc_transcribe_wav(const uint8_t *wav, size_t wav_len,
                                   const char *token, const char *model,
                                   voice_stt_result_t *out);
