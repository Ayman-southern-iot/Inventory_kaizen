/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "board_audio.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "driver/i2s_std.h"
#include "sdkconfig.h"

static const char *TAG = "board_audio";

/*
 * How many channels the codec is opened with. With 2, a read returns interleaved
 * frames and the microphone channel must be extracted; with 1 the stream is already
 * mono and must be taken as-is. Getting this wrong does not fail loudly -- it
 * produces a half-rate aliased signal that measures as noise.
 */
#define CODEC_CHANNELS CONFIG_INV_MIC_CHANNELS
#define MIC_CHANNEL    0

static esp_codec_dev_handle_t s_mic;

esp_err_t board_audio_init(void)
{
    /* On P4 the BSP's microphone init performs the whole bring-up itself -- I2C to
     * the ES8311 and the I2S bus with this board's real pin mapping. No explicit
     * bsp_audio_init() and no poweramp enable are needed for capture. */
    s_mic = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(s_mic, ESP_FAIL, TAG, "bsp_audio_codec_microphone_init() failed");

    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_gain(s_mic, (float)CONFIG_INV_MIC_GAIN_DB),
                        TAG, "setting mic gain failed");

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = MIC_SAMPLE_RATE,
        .bits_per_sample = MIC_BITS,
        .channel = CODEC_CHANNELS,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_mic, &fs), TAG, "opening mic failed");

    ESP_LOGI(TAG, "microphone up: %d Hz, %d-bit, %d interleaved channels, gain %d dB "
                  "(mono taken from channel %d)",
             MIC_SAMPLE_RATE, MIC_BITS, CODEC_CHANNELS, CONFIG_INV_MIC_GAIN_DB, MIC_CHANNEL);
    return ESP_OK;
}

float board_audio_dbfs(float rms)
{
    if (rms < 1.0f) {
        return -96.0f;
    }
    return 20.0f * log10f(rms / 32768.0f);
}

esp_err_t board_audio_record_mono(int16_t *dest, size_t samples,
                                  float *rms_out, int16_t *peak_out)
{
    ESP_RETURN_ON_FALSE(dest && samples, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(s_mic, ESP_ERR_INVALID_STATE, TAG, "mic not initialised");

    /* Read in chunks rather than one huge call: esp_codec_dev_read blocks until the
     * full request is satisfied, and a chunked loop lets progress be logged and keeps
     * the interleaved scratch buffer small. */
    const size_t chunk_frames = 1600;                 /* 100 ms */
    int16_t *scratch = heap_caps_malloc(chunk_frames * CODEC_CHANNELS * sizeof(int16_t),
                                        MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(scratch, ESP_ERR_NO_MEM, TAG, "scratch alloc failed");

    size_t done = 0;
    double sum_sq = 0.0;
    int16_t peak = 0;

    while (done < samples) {
        size_t want = samples - done;
        if (want > chunk_frames) {
            want = chunk_frames;
        }
        esp_err_t err = esp_codec_dev_read(s_mic, scratch,
                                           want * CODEC_CHANNELS * sizeof(int16_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "mic read failed at sample %u: %s", (unsigned)done,
                     esp_err_to_name(err));
            free(scratch);
            return err;
        }
        /* With 1 channel this is a straight copy; with 2 it deinterleaves. */
        for (size_t i = 0; i < want; i++) {
            int16_t s = scratch[i * CODEC_CHANNELS + MIC_CHANNEL];
            dest[done + i] = s;
            sum_sq += (double)s * (double)s;
            int16_t mag = (s < 0) ? (int16_t)(-(int32_t)s) : s;
            if (mag > peak) {
                peak = mag;
            }
        }
        done += want;
    }
    free(scratch);

    float rms = (samples > 0) ? (float)sqrt(sum_sq / samples) : 0.0f;
    if (rms_out)  *rms_out = rms;
    if (peak_out) *peak_out = peak;

    /* Zero-crossing rate separates speech from noise far better than level does.
     * Speech sits around 500-3000 crossings/s; a half-rate aliased or hiss-dominated
     * signal runs 5000+. Logging it makes "the mic is working" checkable. */
    size_t zc = 0;
    for (size_t i = 1; i < samples; i++) {
        if ((dest[i - 1] < 0) != (dest[i] < 0)) {
            zc++;
        }
    }
    float zcr = (samples > 1) ? (float)zc * MIC_SAMPLE_RATE / (float)(samples - 1) : 0.0f;

    ESP_LOGI(TAG, "captured %.2f s: rms=%.0f (%.1f dBFS) peak=%d zcr=%.0f/s [%s]",
             (float)samples / MIC_SAMPLE_RATE, rms, board_audio_dbfs(rms), peak, zcr,
             (zcr > 4000) ? "NOISE-LIKE" : (zcr > 200) ? "speech-like" : "near-DC");
    return ESP_OK;
}

/* Minimal 44-byte canonical WAV header for mono 16-bit PCM. */
size_t board_audio_write_wav_header(uint8_t *buf, size_t pcm_bytes)
{
    const uint32_t rate = MIC_SAMPLE_RATE;
    const uint16_t channels = 1;
    const uint16_t bits = MIC_BITS;
    const uint32_t byte_rate = rate * channels * (bits / 8);
    const uint16_t block_align = channels * (bits / 8);

    memcpy(buf + 0, "RIFF", 4);
    uint32_t riff_size = 36 + (uint32_t)pcm_bytes;
    memcpy(buf + 4, &riff_size, 4);
    memcpy(buf + 8, "WAVEfmt ", 8);
    uint32_t fmt_size = 16;
    memcpy(buf + 16, &fmt_size, 4);
    uint16_t audio_fmt = 1;                 /* PCM */
    memcpy(buf + 20, &audio_fmt, 2);
    memcpy(buf + 22, &channels, 2);
    memcpy(buf + 24, &rate, 4);
    memcpy(buf + 28, &byte_rate, 4);
    memcpy(buf + 32, &block_align, 2);
    memcpy(buf + 34, &bits, 2);
    memcpy(buf + 36, "data", 4);
    uint32_t data_size = (uint32_t)pcm_bytes;
    memcpy(buf + 40, &data_size, 4);
    return 44;
}
