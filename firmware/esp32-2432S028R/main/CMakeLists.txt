idf_component_register(
    SRCS
        main.c
        audio.c
        lcd.c
        touch.c
        ../../../app/sentry_ui.c
        ../../../app/sentry_bell.c
        ../../../src/fonts/font_sentry_digits_28.c
        ../../../src/fonts/font_sentry_caption_17.c
        ../../../src/fonts/font_sentry_critical_14.c
        ../../../src/fonts/font_sentry_station_28.c
    INCLUDE_DIRS
        "."
        "../../../app"
    REQUIRES
        esp_lcd
        esp_timer
        driver
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_spi
        esp_driver_dac
        esp_lvgl_port
        lvgl
        esp_lcd_ili9341
        esp_lcd_touch
        esp_lcd_touch_xpt2046
)

# Generated LVGL font sources should use the component's public lvgl.h.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
