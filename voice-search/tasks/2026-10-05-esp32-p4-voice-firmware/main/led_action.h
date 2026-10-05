/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * The action layer: maps recognised speech-command IDs onto the indicator LED.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Command IDs registered with MultiNet. Several spoken phrasings map onto each. */
#define VOICE_CMD_LED_ON   1
#define VOICE_CMD_LED_OFF  2

/** Configure the LED GPIO as an output and drive it off. */
esp_err_t led_action_init(void);

/** Drive the LED on or off, honouring CONFIG_VOICE_LED_ACTIVE_LOW. */
esp_err_t led_action_set(bool on);

/** Current logical state (true = lit). */
bool led_action_get(void);

/**
 * Apply a recognised command.
 *
 * @param command_id  ID from esp_mn_results_t
 * @return true if the ID mapped to an action, false if it was unrecognised
 */
bool led_action_handle_command(int command_id);
