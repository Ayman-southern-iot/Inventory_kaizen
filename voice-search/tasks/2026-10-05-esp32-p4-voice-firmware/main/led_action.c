/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "led_action.h"

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "led_action";

static bool s_led_on;

esp_err_t led_action_init(void)
{
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_VOICE_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "configuring GPIO%d failed", CONFIG_VOICE_LED_GPIO);

    ESP_LOGI(TAG, "LED on GPIO%d (%s), starting off",
             CONFIG_VOICE_LED_GPIO,
#if CONFIG_VOICE_LED_ACTIVE_LOW
             "active-low"
#else
             "active-high"
#endif
            );

    return led_action_set(false);
}

esp_err_t led_action_set(bool on)
{
    int level = on ? 1 : 0;
#if CONFIG_VOICE_LED_ACTIVE_LOW
    level = !level;
#endif
    ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_VOICE_LED_GPIO, level), TAG, "gpio_set_level failed");
    s_led_on = on;
    return ESP_OK;
}

bool led_action_get(void)
{
    return s_led_on;
}

bool led_action_handle_command(int command_id)
{
    switch (command_id) {
    case VOICE_CMD_LED_ON:
        led_action_set(true);
        ESP_LOGI(TAG, ">>> LED ON  (GPIO%d) <<<", CONFIG_VOICE_LED_GPIO);
        return true;

    case VOICE_CMD_LED_OFF:
        led_action_set(false);
        ESP_LOGI(TAG, ">>> LED OFF (GPIO%d) <<<", CONFIG_VOICE_LED_GPIO);
        return true;

    default:
        ESP_LOGW(TAG, "command_id %d has no action bound to it", command_id);
        return false;
    }
}
