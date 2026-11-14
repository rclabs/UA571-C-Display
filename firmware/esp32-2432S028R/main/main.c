#include <stdint.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "lcd.h"
#include "audio.h"
#include "sdkconfig.h"
#include "sentry_ui.h"
#include "touch.h"

static const char *TAG = "aliens";

static sentry_state_t state = {
    .rounds_remaining = 20,
    .temp = 42,
    .range_m = 100,
    .station = 'A',
};

static void touch_pressed(lv_event_t *event)
{
    (void)event;
    /* Input-device event catches a press anywhere, including child widgets.
     * No LVGL changes or DAC writes here; the audio task handles the release. */
    sentry_audio_acknowledge();
}

static void update_demo_state(void)
{
    if(state.rounds_remaining > 0) {
        state.rounds_remaining--;
    }
    else {
        state.rounds_remaining = 500;
    }

    state.temp = (uint8_t)((state.temp + 3) % 101);
    if(state.rounds_remaining < 40) {
        state.range_m = (uint8_t)(state.rounds_remaining * 2);
    }
    else {
        state.range_m = (uint8_t)((state.range_m + 97) % 101);
    }
}

void app_main(void)
{
    esp_lcd_panel_io_handle_t lcd_io;
    esp_lcd_panel_handle_t lcd_panel;
    esp_lcd_touch_handle_t touch_handle;
    lvgl_port_touch_cfg_t touch_cfg = {0};

    ESP_ERROR_CHECK(lcd_display_brightness_init());
    ESP_ERROR_CHECK(app_lcd_init(&lcd_io, &lcd_panel));

    lv_display_t *display = app_lvgl_init(lcd_io, lcd_panel);
    if(display == NULL) {
        ESP_LOGE(TAG, "app_lvgl_init failed");
        esp_restart();
    }

    ESP_ERROR_CHECK(touch_init(&touch_handle));
    touch_cfg.disp = display;
    touch_cfg.handle = touch_handle;
    lv_indev_t *touch_input = lvgl_port_add_touch(&touch_cfg);
    if(touch_input == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        esp_restart();
    }

    ESP_ERROR_CHECK(lcd_display_brightness_set(25));
    ESP_ERROR_CHECK(lcd_display_rotate(display, LV_DISPLAY_ROTATION_90));

    esp_err_t audio_err = sentry_audio_init();
    if(audio_err != ESP_OK && audio_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Audio unavailable (%s); continuing with visual warning", esp_err_to_name(audio_err));
    }
#if CONFIG_ALIENS_START_IN_CRITICAL
    state.rounds_remaining = SENTRY_DEFAULT_CRITICAL_THRESHOLD;
#endif

    if(lvgl_port_lock(0)) {
        sentry_ui_create();
        lv_indev_add_event_cb(touch_input, touch_pressed, LV_EVENT_PRESSED, NULL);
        if(audio_err == ESP_OK)
            sentry_ui_set_critical_clock(sentry_audio_request, sentry_audio_read_clock, NULL);
        sentry_ui_set_state(&state);
        lvgl_port_unlock();
    }

    while(true) {
        update_demo_state();

        if(lvgl_port_lock(0)) {
            sentry_ui_set_state(&state);
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
