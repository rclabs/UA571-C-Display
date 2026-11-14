#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "sentry_ui.h"
#include "lvgl.h"

static bool quit_requested = false;

static int app_event_watch(void *userdata, SDL_Event *event) {
    (void)userdata;

    if(event->type == SDL_QUIT) {
        quit_requested = true;
    }

    if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_q) {
        quit_requested = true;
    }

    return 0;
}

int main(void) {
    SDL_SetHint(SDL_HINT_APP_NAME, "aliens-gauge");

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        return 1;
    }

    lv_init();

    lv_display_t *display = lv_sdl_window_create(220, 260);
    if(display == NULL) {
        return 1;
    }

    lv_sdl_window_set_title(display, "aliens gauge");
    lv_sdl_window_set_resizeable(display, false);
    SDL_AddEventWatch(app_event_watch, NULL);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *bar = gauge_create(screen, "Temp", 20, 20);
    if(bar == NULL) {
        return 1;
    }

    uint32_t last_tick = SDL_GetTicks();
    uint32_t last_value_update = last_tick;
    int32_t value = 0;
    int32_t direction = 1;

    while(!quit_requested) {
        uint32_t now = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick = now;

        if(now - last_value_update >= 50) {
            value += direction;
            if(value >= 100) {
                value = 100;
                direction = -1;
            }
            else if(value <= 0) {
                value = 0;
                direction = 1;
            }

            lv_bar_set_value(bar, value, LV_ANIM_OFF);
            last_value_update = now;
        }

        uint32_t wait_ms = lv_timer_handler();
        if(wait_ms == LV_NO_TIMER_READY || wait_ms > 5) {
            wait_ms = 5;
        }

        SDL_Delay(wait_ms);
    }

    SDL_DelEventWatch(app_event_watch, NULL);
    lv_sdl_quit();
    lv_deinit();

    return 0;
}
