/* Headless lifecycle/transition regression test for the portable warning. */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "sentry_ui.h"
#include "sentry_layout.h"

static uint8_t buffer[320 * 240 * 3];
enum {
    WARNING_W = (88 * SENTRY_VIEW_W + SENTRY_DESIGN_W / 2) / SENTRY_DESIGN_W,
    WARNING_H = (54 * SENTRY_VIEW_H + SENTRY_DESIGN_H / 2) / SENTRY_DESIGN_H,
};
static uint8_t dark[WARNING_W * WARNING_H * 3], light[WARNING_W * WARNING_H * 3], actual[WARNING_W * WARNING_H * 3];
#define CHECK(condition) do { if(!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while(0)

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    (void)area;
    (void)data;
    lv_display_flush_ready(display);
}

static void snapshot(lv_display_t *display, uint8_t *dest) {
    lv_refr_now(display);
    int x = SENTRY_VIEW_X + sentry_scale_x(2);
    int top = SENTRY_VIEW_Y + sentry_scale_y(116);
    for(int y = 0; y < WARNING_H; ++y)
        memcpy(dest + y * WARNING_W * 3, buffer + ((top + y) * 320 + x) * 3, WARNING_W * 3);
}

static void advance(unsigned ms, const sentry_state_t *state) {
    for(unsigned i = 0; i < ms; ++i) {
        lv_tick_inc(1);
        lv_timer_handler();
        sentry_ui_set_state(state);
    }
}

static bool labels_fit(lv_obj_t *obj) {
    if(lv_obj_check_type(obj, &lv_label_class)) {
        const char *text = lv_label_get_text(obj);
        const lv_font_t *font = lv_obj_get_style_text_font(obj, 0);
        lv_point_t size;
        lv_text_get_size(&size, text, font, 0, 0, 10000, LV_TEXT_FLAG_NONE);
        if(size.x > lv_obj_get_width(obj) || lv_obj_get_height(obj) > font->line_height) {
            fprintf(stderr, "Wrapped/clipped label: %s\n", text);
            return false;
        }
        for(const char *p = text; *p; ++p) {
            lv_font_glyph_dsc_t glyph;
            if(!lv_font_get_glyph_dsc(font, &glyph, (unsigned char)*p, 0) || glyph.is_placeholder) {
                fprintf(stderr, "Missing glyph in: %s\n", text);
                return false;
            }
        }
    }
    for(uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if(!labels_fit(lv_obj_get_child(obj, i))) return false;
    return true;
}

static unsigned phase_events;
static bool observed_active, observed_inverted;
static void observe_phase(bool active, bool inverted, void *user_data) {
    if(user_data != &phase_events) return;
    ++phase_events;
    observed_active = active;
    observed_inverted = inverted;
}

static unsigned clock_requests;
static bool requested_active, played_active, played_inverted;
static uint32_t requested_period;
static void request_clock(bool active, uint32_t phase_ms, void *data) {
    (void)data;
    ++clock_requests;
    requested_active = active;
    requested_period = phase_ms;
}
static bool read_clock(bool *active, bool *inverted, void *data) {
    (void)data;
    *active = played_active;
    *inverted = played_inverted;
    return true;
}

int main(void) {
    lv_init();
    lv_display_t *display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    sentry_ui_create();
    sentry_ui_configure_critical(50, 100);
    sentry_ui_set_critical_callback(observe_phase, &phase_events);
    CHECK(phase_events == 1 && !observed_active);
    sentry_state_t state = {.rounds_remaining = 50, .station = 'A'};
    sentry_ui_set_state(&state);
    snapshot(display, dark);
    CHECK(phase_events == 2 && observed_active && !observed_inverted);
    CHECK(labels_fit(lv_screen_active()));
    lv_obj_t *root = lv_obj_get_child(lv_screen_active(), 0);
    lv_obj_t *header = lv_obj_get_child(root, 0);
    lv_area_t head;
    lv_obj_get_coords(header, &head);
    for(int i = 0; i < 2; ++i) {
        lv_obj_t *station = lv_obj_get_child(header, i);
        lv_area_t bounds;
        lv_obj_get_coords(station, &bounds);
        CHECK(lv_obj_get_width(station) == 32 && lv_obj_get_height(station) == 32);
        CHECK(bounds.x1 >= head.x1 + 5 && bounds.x2 <= head.x2 - 5);
        CHECK(i == 0 ? bounds.x1 == head.x1 + 5 : bounds.x2 == head.x2 - 5);
        CHECK(bounds.y1 > head.y1 && bounds.y2 < head.y2);
    }
    advance(100, &state);
    snapshot(display, light);
    CHECK(phase_events == 3 && observed_active && observed_inverted);
    CHECK(memcmp(dark, light, sizeof(dark)) != 0);
    state.rounds_remaining = 51;
    sentry_ui_set_state(&state);
    advance(300, &state);
    CHECK(phase_events == 4 && !observed_active);
    snapshot(display, actual);
    for(unsigned i = 0; i < sizeof(actual); ++i) CHECK(actual[i] == 0);
    state.rounds_remaining = 49;
    sentry_ui_set_state(&state);
    snapshot(display, actual);
    CHECK(memcmp(actual, dark, sizeof(dark)) == 0);
    advance(99, &state);
    CHECK(phase_events == 5 && observed_active && !observed_inverted);
    snapshot(display, actual);
    CHECK(memcmp(actual, dark, sizeof(dark)) == 0);
    advance(1, &state);
    CHECK(phase_events == 6 && observed_inverted);
    /* Restarting the phase must notify audio even when still critical. */
    sentry_ui_configure_critical(50, 100);
    CHECK(phase_events == 7 && observed_active && !observed_inverted);
    advance(100, &state);
    CHECK(phase_events == 8 && observed_inverted);
    snapshot(display, actual);
    CHECK(memcmp(actual, light, sizeof(light)) == 0);
    /* Destroying/recreating the UI stops audio and clears the observer. */
    sentry_ui_create();
    CHECK(phase_events == 9 && !observed_active);
    sentry_ui_configure_critical(50, 100);
    sentry_ui_set_state(&state);
    advance(100, &state);
    snapshot(display, actual);
    CHECK(memcmp(actual, light, sizeof(light)) == 0);
    sentry_ui_configure_critical(0, 100);
    CHECK(phase_events == 9); /* No stale observer after screen recreation. */
    snapshot(display, actual);
    for(unsigned i = 0; i < sizeof(actual); ++i) CHECK(actual[i] == 0);
    sentry_ui_set_critical_callback(observe_phase, &phase_events);
    CHECK(phase_events == 10 && !observed_active);
    state.critical = true; /* Forced warning above the configured threshold. */
    sentry_ui_set_state(&state);
    CHECK(phase_events == 11 && observed_active && !observed_inverted);
    sentry_ui_set_critical_callback(NULL, NULL);
    CHECK(phase_events == 12 && !observed_active);
    advance(100, &state);
    CHECK(phase_events == 12);
    sentry_ui_create();
    sentry_ui_set_critical_clock(request_clock, read_clock, NULL);
    sentry_ui_set_critical_callback(observe_phase, &phase_events);
    CHECK(clock_requests == 1 && !requested_active);
    state.critical = true;
    sentry_ui_set_state(&state);
    CHECK(clock_requests == 2 && requested_active && requested_period == 171);
    CHECK(!observed_active); /* Generated/requested is NOT played yet. */
    played_active = true;
    advance(5, &state);
    CHECK(observed_active && !observed_inverted);
    advance(600, &state);
    CHECK(!observed_inverted && clock_requests == 2); /* No UI-owned oscillator. */
    played_inverted = true;
    lv_tick_inc(500); /* Stalled LVGL catches up to the playback phase. */
    lv_timer_handler();
    CHECK(observed_inverted && clock_requests == 2);
    state.critical = false;
    state.rounds_remaining = 500;
    sentry_ui_set_state(&state);
    CHECK(clock_requests == 3 && !requested_active);
    CHECK(observed_active); /* Exit follows queued playback too. */
    played_active = false;
    advance(5, &state);
    CHECK(!observed_active);
    sentry_ui_configure_critical(999, 37);
    CHECK(clock_requests == 4 && requested_active && requested_period == 37);
    sentry_ui_create();
    CHECK(clock_requests == 5 && !requested_active); /* Destruction stops audio. */
    advance(200, &state);
    CHECK(clock_requests == 5); /* No stale clock registration. */
    CHECK(SENTRY_DEFAULT_CRITICAL_THRESHOLD < 999);
    state.critical = false;
    state.rounds_remaining = SENTRY_DEFAULT_CRITICAL_THRESHOLD + 1;
    sentry_ui_set_state(&state);
    snapshot(display, actual);
    for(unsigned i = 0; i < sizeof(actual); ++i) CHECK(actual[i] == 0);
    state.rounds_remaining = SENTRY_DEFAULT_CRITICAL_THRESHOLD;
    sentry_ui_set_state(&state);
    snapshot(display, actual);
    CHECK(memcmp(actual, dark, sizeof(actual)) == 0);
    lv_deinit();
    puts("PASS: glyph/label fit, critical lifecycle, playback-clock following, delayed UI, observer cleanup");
    return 0;
}
