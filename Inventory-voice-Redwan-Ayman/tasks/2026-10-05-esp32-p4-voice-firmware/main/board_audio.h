/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Microphone capture for the ESP32-P4-WIFI6-DEV-KIT.
 *
 * Adapted from projects/22-voice-control-led, where this path was proven on hardware.
 * Two facts from that work shape this file:
 *
 *   1. The ES8311 is a MONO codec but is opened in I2S_SLOT_MODE_STEREO, so every
 *      frame read back holds TWO interleaved channels and only channel 0 carries the
 *      microphone (channel 1 measured flat zero on every sample). Whisper needs mono,
 *      so channel 0 is extracted here.
 *   2. Detection is governed by LEVEL. Speech at -27 dBFS was recognised; everything
 *      at -41 dBFS and quieter never was. Hence the RMS/peak reporting below: if a
 *      transcript is wrong, the first question is always "how loud was it".
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#define MIC_SAMPLE_RATE 16000
#define MIC_BITS        16

/** Bring up the onboard microphone at 16 kHz / 16-bit. */
esp_err_t board_audio_init(void);

/**
 * Record mono audio from the microphone.
 *
 * @param dest     buffer for `samples` mono int16 samples
 * @param samples  how many mono samples to capture (16000 == 1 second)
 * @param rms_out  optional: RMS of the captured audio
 * @param peak_out optional: peak absolute sample
 */
esp_err_t board_audio_record_mono(int16_t *dest, size_t samples,
                                  float *rms_out, int16_t *peak_out);

/** dBFS for a given RMS, for judging whether speech was loud enough. */
float board_audio_dbfs(float rms);

/**
 * Wrap mono PCM in a 44-byte WAV header, in place at the front of `buf`.
 *
 * `/v1/audio/transcriptions` takes a file, so the PCM needs a container. Caller must
 * leave 44 bytes of headroom before the samples.
 */
size_t board_audio_write_wav_header(uint8_t *buf, size_t pcm_bytes);
