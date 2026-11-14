#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sentry_ui.h"
#include "lvgl.h"

enum { CYD_W = 320, CYD_H = 240 };
static const float SIM_SCALE = 2.0f;
static bool quit_requested;
static SDL_Keycode pending_key;
static uint16_t critical_threshold = SENTRY_DEFAULT_CRITICAL_THRESHOLD;
static uint32_t blink_ms = SENTRY_DEFAULT_CRITICAL_PHASE_MS, capture_ms;
static sentry_state_t state = {
    .rounds_remaining = 500, .temp = 48, .range_m = 0, .station = 'A'
};
static unsigned char pixels[CYD_W * CYD_H * 3];
static unsigned char draw_buffer[CYD_W * CYD_H * 3];

static void usage(const char *program) {
    fprintf(stderr,
        "Usage: %s [--capture output.ppm] [--demo] [--lamp-test]\n"
        "  [--rounds 0..999] [--time 0..99.99] [--temp 0..100]\n"
        "  [--range 0..100] [--station A|B] [--critical]\n"
        "  [--critical-threshold 0..999] [--blink-ms 1..60000]\n"
        "  [--capture-ms 0..10000] (advance simulated time before capture)\n"
        "Time defaults to rounds / 15; --time overrides it independently.\n"
        "Keys: q quit; a/b station; space demo; l lamp test;\n"
        "      up/down rounds; left/right time; t/g temp; r/f range;\n"
        "      c force critical; u restore automatic time. Shift = steps of 10.\n",
        program);
}

static bool number(const char *s, double max, double *result) {
    char *end;
    *result = strtod(s, &end);
    return end != s && *end == '\0' && isfinite(*result) && *result >= 0 && *result <= max;
}

/* Capture the actual LVGL software renderer, without SDL or a compositor. */
static void capture_flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    for(int y = area->y1; y <= area->y2; ++y) {
        /* LVGL RGB888 is stored B,G,R. PPM stores R,G,B. */
        for(int x = area->x1; x <= area->x2; ++x) {
            unsigned char *p = &pixels[(y * CYD_W + x) * 3];
            p[0] = data[2]; p[1] = data[1]; p[2] = data[0];
            data += 3;
        }
    }
    lv_display_flush_ready(display);
}

static int capture(const char *path) {
    lv_init();
    lv_display_t *display = lv_display_create(CYD_W, CYD_H);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, capture_flush);
    sentry_ui_create();
    sentry_ui_configure_critical(critical_threshold, blink_ms);
    sentry_ui_set_state(&state);
    for(uint32_t ms = 0; ms < capture_ms; ++ms) {
        lv_tick_inc(1);
        lv_timer_handler();
        /* Match continuous telemetry in the interactive simulator. */
        sentry_ui_set_state(&state);
    }
    lv_refr_now(display);
    FILE *file = fopen(path, "wb");
    if(!file) { perror(path); lv_deinit(); return 1; }
    bool ok = fprintf(file, "P6\n%d %d\n255\n", CYD_W, CYD_H) > 0;
    ok = fwrite(pixels, 1, sizeof(pixels), file) == sizeof(pixels) && ok;
    if(fclose(file) != 0) ok = false;
    lv_deinit();
    return ok ? 0 : 1;
}

static int event_watch(void *userdata, SDL_Event *event) {
    (void)userdata;
    if(event->type == SDL_QUIT) quit_requested = true;
    if(event->type == SDL_KEYDOWN) pending_key = event->key.keysym.sym;
    return 0;
}

static int clamp(int value, int max) { return value < 0 ? 0 : value > max ? max : value; }

int main(int argc, char **argv) {
    bool demo = false, lamp = false;
    const char *output = NULL;
    for(int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if(!strcmp(arg, "--help") || !strcmp(arg, "-h")) { usage(argv[0]); return 0; }
        if(!strcmp(arg, "--demo")) { demo = true; continue; }
        if(!strcmp(arg, "--critical")) { state.critical = true; continue; }
        if(!strcmp(arg, "--lamp-test")) { lamp = true; continue; }
        if(i + 1 >= argc) { usage(argv[0]); return 2; }
        const char *value = argv[++i];
        double n;
        if(!strcmp(arg, "--capture")) output = value;
        else if(!strcmp(arg, "--station") && strlen(value) == 1 && (*value == 'A' || *value == 'B')) state.station = *value;
        else if(!strcmp(arg, "--rounds") && number(value, 999, &n) && floor(n) == n) state.rounds_remaining = (uint16_t)n;
        else if(!strcmp(arg, "--time") && number(value, 99.99, &n)) { state.time_override = true; state.time_centiseconds = (uint16_t)lround(n * 100); }
        else if(!strcmp(arg, "--temp") && number(value, 100, &n) && floor(n) == n) state.temp = (uint8_t)n;
        else if(!strcmp(arg, "--range") && number(value, 100, &n) && floor(n) == n) state.range_m = (uint8_t)n;
        else if(!strcmp(arg, "--critical-threshold") && number(value, 999, &n) && floor(n) == n) critical_threshold = (uint16_t)n;
        else if(!strcmp(arg, "--blink-ms") && number(value, 60000, &n) && n >= 1 && floor(n) == n) blink_ms = (uint32_t)n;
        else if(!strcmp(arg, "--capture-ms") && number(value, 10000, &n) && floor(n) == n) capture_ms = (uint32_t)n;
        else { fprintf(stderr, "Invalid option/value: %s %s\n", arg, value); return 2; }
    }
    sentry_state_t saved = state;
    if(lamp) state = (sentry_state_t){ .rounds_remaining = 888, .temp = 100, .range_m = 100, .station = 'B', .critical = true, .time_override = true, .time_centiseconds = 8888 };
    if(output) return capture(output);

    SDL_SetHint(SDL_HINT_APP_NAME, "aliens");
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    lv_init();
    lv_display_t *display = lv_sdl_window_create(CYD_W, CYD_H);
    if(!display) return 1;
    lv_sdl_window_set_zoom(display, SIM_SCALE);
    lv_sdl_window_set_title(display, "aliens");
    SDL_Renderer *renderer = lv_sdl_window_get_renderer(display);
    if(renderer) SDL_SetWindowResizable(SDL_RenderGetWindow(renderer), SDL_FALSE);
    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();
    SDL_AddEventWatch(event_watch, NULL);
    sentry_ui_create();
    sentry_ui_configure_critical(critical_threshold, blink_ms);
    sentry_ui_set_state(&state);
    uint32_t last_tick = SDL_GetTicks(), last_update = last_tick;
    while(!quit_requested) {
        uint32_t now = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick = now;
        lv_timer_handler();
        SDL_Keycode key = pending_key;
        pending_key = 0;
        int step = SDL_GetModState() & KMOD_SHIFT ? 10 : 1;
        if(key == SDLK_q) quit_requested = true;
        if(key == SDLK_SPACE) { demo = !demo; last_update = now; }
        if(key == SDLK_l) {
            lamp = !lamp;
            if(lamp) { saved = state; state = (sentry_state_t){ .rounds_remaining = 888, .temp = 100, .range_m = 100, .station = 'B', .critical = true, .time_override = true, .time_centiseconds = 8888 }; }
            else state = saved;
        }
        if(key == SDLK_a || key == SDLK_b) state.station = key == SDLK_a ? 'A' : 'B';
        if(key == SDLK_UP || key == SDLK_DOWN) state.rounds_remaining = clamp(state.rounds_remaining + (key == SDLK_UP ? step : -step), 999);
        if(key == SDLK_LEFT || key == SDLK_RIGHT) {
            if(!state.time_override) state.time_centiseconds = (state.rounds_remaining * 100 + 7) / 15;
            state.time_override = true;
            state.time_centiseconds = clamp(state.time_centiseconds + (key == SDLK_RIGHT ? step : -step), 9999);
        }
        if(key == SDLK_u) state.time_override = false;
        if(key == SDLK_t || key == SDLK_g) state.temp = clamp(state.temp + (key == SDLK_t ? step : -step), 100);
        if(key == SDLK_r || key == SDLK_f) state.range_m = clamp(state.range_m + (key == SDLK_r ? step : -step), 100);
        if(key == SDLK_c) state.critical = !state.critical;
        if(demo && !lamp && now - last_update >= 250) {
            state.rounds_remaining = state.rounds_remaining ? state.rounds_remaining - 1 : 500;
            state.temp = (state.temp + 3) % 101;
            state.range_m = (state.range_m + 97) % 101;
            last_update = now;
        }
        sentry_ui_set_state(&state);
        SDL_Delay(5);
    }
    SDL_DelEventWatch(event_watch, NULL);
    lv_sdl_quit();
    lv_deinit();
    return 0;
}
