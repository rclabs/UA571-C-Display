#include <stdbool.h>
#include <stdint.h>
#include "sentry_ui.h"
#include "sentry_layout.h"
#include "lvgl.h"

LV_FONT_DECLARE(font_sentry_digits_28);
LV_FONT_DECLARE(font_sentry_caption_17);
LV_FONT_DECLARE(font_sentry_critical_14);
LV_FONT_DECLARE(font_sentry_station_28);

enum { SCREEN_W = 320, SCREEN_H = 240, CRT = 0xb4ad16 };
static struct {
    lv_obj_t *left_station, *right_station, *rounds, *time, *critical;
    lv_obj_t *temp_bar, *range_bar;
    lv_obj_t *critical_frame, *critical_fill, *critical_text;
    lv_timer_t *critical_timer;
    bool critical_active, critical_inverted, critical_requested;
    uint32_t critical_phase_ms, critical_epoch;
    sentry_critical_request_t clock_request;
    sentry_critical_clock_t clock_read;
    void *clock_user_data;
    uint16_t critical_threshold;
    sentry_critical_callback_t critical_callback;
    void *critical_user_data;
    sentry_state_t state;
} ui;

/* Shared layout geometry is mapped to physical CYD pixels. */
static lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h, int border) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, sentry_scale_x(x), sentry_scale_y(y));
    lv_obj_set_size(obj, sentry_scale_x(w), sentry_scale_y(h));
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(CRT), 0);
    lv_obj_set_style_border_width(obj, border, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(CRT), 0);
    return obj;
}

static void caption(lv_obj_t *parent, const char *text, int x, int y, int w) {
    lv_obj_t *obj = label(parent, text, &font_sentry_caption_17);
    /* Keep horizontal font advances: shrink height, not text readability. */
    int available = SENTRY_VIEW_W - sentry_scale_x(x) - 2;
    lv_obj_set_width(obj, w < available ? w : available);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(obj, sentry_scale_x(x), sentry_scale_y(y));
}

static lv_obj_t *readout(lv_obj_t *parent, int x, int y, int w, int h, const lv_font_t *font) {
    lv_obj_t *box = plain(parent, x, y, w, h, 1);
    lv_obj_t *text = label(box, "", font);
    lv_obj_align(text, LV_ALIGN_CENTER, 0, -2);
    return text;
}

static lv_obj_t *badge(lv_obj_t *parent, int x) {
    lv_obj_t *obj = plain(parent, x, 8, 32, 32, 1);
    /* A real circle on the LCD, independent of the design's aspect correction. */
    lv_obj_set_size(obj, 32, 32);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *text = label(obj, "A", &font_sentry_station_28);
    lv_obj_center(text);
    return text;
}

static void triangle_draw(lv_event_t *event) {
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = lv_color_hex(CRT);
    dsc.opa = LV_OPA_COVER;
    dsc.p[0].x = area.x1; dsc.p[0].y = area.y1;
    dsc.p[1].x = area.x2; dsc.p[1].y = (area.y1 + area.y2) / 2;
    dsc.p[2].x = area.x1; dsc.p[2].y = area.y2;
    lv_draw_triangle(lv_event_get_layer(event), &dsc);
}

static void gauge_draw(lv_event_t *event) {
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(event), &a);
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(CRT);
    dsc.opa = LV_OPA_COVER;
    dsc.width = 1;
    dsc.p1.x = dsc.p2.x = a.x2;
    dsc.p1.y = a.y1; dsc.p2.y = a.y2;
    lv_draw_line(lv_event_get_layer(event), &dsc);
    for(int i = 0; i <= 12; ++i) {
        int width = sentry_scale_x(i == 12 ? 21 : (i == 0 || i == 6 ? 8 : (i % 2 == 0 ? 6 : 3)));
        dsc.p1.x = a.x2 - width; dsc.p2.x = a.x2;
        dsc.p1.y = dsc.p2.y = a.y1 + (lv_area_get_height(&a) - 1) * i / 12;
        lv_draw_line(lv_event_get_layer(event), &dsc);
    }
}

static lv_obj_t *make_gauge(lv_obj_t *parent, const char *title, int x, int y, int h) {
    caption(parent, title, x - 6, y - 23, 36);
    lv_obj_t *track = plain(parent, x, y, 24, h, 0);
    lv_obj_add_event_cb(track, gauge_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_t *bar = lv_bar_create(track);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, sentry_scale_x(3), 0);
    lv_obj_set_size(bar, sentry_scale_x(10), sentry_scale_y(h));
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(CRT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    return bar;
}

#ifdef ALIENS_TEST
lv_obj_t *gauge_create(lv_obj_t *parent, const char *title, int32_t x, int32_t y) {
    return make_gauge(parent, title, x, y, 148);
}
#endif

static void critical_paint(void) {
    lv_obj_set_style_opa(ui.critical, ui.critical_active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    /* The bright phase expands the frame; the lettering stays anchored. */
    lv_obj_set_pos(ui.critical_frame, sentry_scale_x(ui.critical_inverted ? 0 : 5), sentry_scale_y(ui.critical_inverted ? 0 : 7));
    lv_obj_set_size(ui.critical_frame, sentry_scale_x(ui.critical_inverted ? 88 : 78), sentry_scale_y(ui.critical_inverted ? 54 : 40));
    lv_obj_set_style_bg_color(ui.critical_fill,
        ui.critical_inverted ? lv_color_hex(CRT) : lv_color_black(), 0);
    lv_obj_set_style_text_color(ui.critical_text,
        ui.critical_inverted ? lv_color_black() : lv_color_hex(CRT), 0);
}

static void critical_notify(void) {
    if(ui.critical_callback)
        ui.critical_callback(ui.critical_active, ui.critical_inverted, ui.critical_user_data);
}

void sentry_ui_set_critical_callback(sentry_critical_callback_t callback, void *user_data) {
    if(ui.critical_callback)
        ui.critical_callback(false, false, ui.critical_user_data);
    ui.critical_callback = callback;
    ui.critical_user_data = user_data;
    critical_notify();
}

static bool critical_refresh(void) {
    bool active = ui.critical_requested;
    bool inverted = active && ((lv_tick_elaps(ui.critical_epoch) / ui.critical_phase_ms) % 2);
    if(ui.clock_read) {
        bool played_active, played_inverted;
        if(ui.clock_read(&played_active, &played_inverted, ui.clock_user_data)) {
            active = played_active;
            inverted = active && played_inverted;
        }
    }
    bool changed = active != ui.critical_active || inverted != ui.critical_inverted;
    ui.critical_active = active;
    ui.critical_inverted = inverted;
    if(changed) {
        critical_paint();
        critical_notify();
    }
    return changed;
}

static void critical_tick(lv_timer_t *timer) {
    critical_refresh();
    /* Local fallback also uses an absolute epoch: late service does not move
     * subsequent deadlines. External playback is polled, never advanced here. */
    if(!ui.clock_read)
        lv_timer_set_period(timer, ui.critical_phase_ms - lv_tick_elaps(ui.critical_epoch) % ui.critical_phase_ms);
}

static void critical_restart(void) {
    ui.critical_epoch = lv_tick_get();
    if(ui.clock_request)
        ui.clock_request(ui.critical_requested, ui.critical_phase_ms, ui.clock_user_data);
    lv_timer_set_period(ui.critical_timer, ui.clock_read ? 5 : ui.critical_phase_ms);
    lv_timer_reset(ui.critical_timer);
    if(ui.critical_requested || ui.clock_read) lv_timer_resume(ui.critical_timer);
    else lv_timer_pause(ui.critical_timer);
}

void sentry_ui_set_critical_clock(sentry_critical_request_t request,
                                  sentry_critical_clock_t read, void *user_data) {
    if(ui.clock_request) ui.clock_request(false, ui.critical_phase_ms, ui.clock_user_data);
    ui.clock_request = request && read ? request : NULL;
    ui.clock_read = request && read ? read : NULL;
    ui.clock_user_data = user_data;
    critical_restart();
    critical_refresh();
}

static void critical_update(void) {
    bool active = ui.state.critical || ui.state.rounds_remaining <= ui.critical_threshold;
    if(active != ui.critical_requested) {
        ui.critical_requested = active;
        critical_restart();
    }
    critical_refresh();
}

static void critical_delete(lv_event_t *event) {
    (void)event;
    if(ui.clock_request) ui.clock_request(false, ui.critical_phase_ms, ui.clock_user_data);
    ui.clock_request = NULL;
    ui.clock_read = NULL;
    ui.clock_user_data = NULL;
    ui.critical_requested = false;
    ui.critical_active = false;
    ui.critical_inverted = false;
    critical_notify();
    ui.critical_callback = NULL;
    ui.critical_user_data = NULL;
    if(ui.critical_timer) {
        lv_timer_delete(ui.critical_timer);
        ui.critical_timer = NULL;
    }
}

void sentry_ui_configure_critical(uint16_t threshold, uint32_t phase_ms) {
    ui.critical_threshold = threshold > 999 ? 999 : threshold;
    ui.critical_phase_ms = phase_ms ? phase_ms : 1;
    ui.critical_requested = ui.state.critical || ui.state.rounds_remaining <= ui.critical_threshold;
    critical_restart();
    if(!critical_refresh()) critical_notify();
}

static void update(void) {
    const sentry_state_t *s = &ui.state;
    char station[2] = {s->station, 0};
    lv_label_set_text(ui.left_station, station);
    lv_label_set_text(ui.right_station, station);
    lv_label_set_text_fmt(ui.rounds, "%03u", (unsigned)s->rounds_remaining);
    /* LVGL's built-in printf need not support floats (it rendered "f" before). */
    unsigned cs = s->time_override ? s->time_centiseconds : (s->rounds_remaining * 100u + 7u) / 15u;
    /* A blank tens cell keeps the decimal/fraction anchored below 10 seconds. */
    lv_label_set_text_fmt(ui.time, "%2u.%02u", cs / 100, cs % 100);
    lv_bar_set_value(ui.temp_bar, s->temp, LV_ANIM_OFF);
    lv_bar_set_value(ui.range_bar, s->range_m, LV_ANIM_OFF);
    critical_update();
}

void sentry_ui_set_station(char station) {
    if(station != 'A' && station != 'B') return;
    ui.state.station = station;
    update();
}

void sentry_ui_set_state(const sentry_state_t *state) {
    if(!state) return;
    char previous = ui.state.station;
    ui.state = *state;
    if(ui.state.station != 'A' && ui.state.station != 'B') ui.state.station = previous;
    if(ui.state.rounds_remaining > 999) ui.state.rounds_remaining = 999;
    if(ui.state.time_centiseconds > 9999) ui.state.time_centiseconds = 9999;
    if(ui.state.temp > 100) ui.state.temp = 100;
    if(ui.state.range_m > 100) ui.state.range_m = 100;
    update();
}

void sentry_ui_create(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_t *root = plain(screen, 0, 0, SCREEN_W, SCREEN_H, 0);
    lv_obj_set_pos(root, SENTRY_VIEW_X, SENTRY_VIEW_Y);
    lv_obj_t *header = plain(root, 0, 0, 320, 51, 1);
    ui.left_station = badge(header, 4);
    ui.right_station = badge(header, 282);
    caption(header, "UA 571-C", 34, 7, 244);
    caption(header, "REMOTE SENTRY WEAPON SYSTEM", 34, 24, 244);
    /* Single lower border. */
    plain(root, 0, 239, 320, 1, 1);

    caption(root, "Rounds", 3, 81, 74);
    caption(root, "Remaining", 3, 98, 82);
    caption(root, "TIME AT 100%", 3, 175, 96);
    caption(root, "(secs)", 3, 199, 94);
    ui.rounds = readout(root, 117, 79, 74, 54, &font_sentry_digits_28);
    ui.time = readout(root, 106, 169, 96, 55, &font_sentry_digits_28);
    lv_obj_t *pointer = plain(root, 104, 98, 13, 20, 0);
    lv_obj_add_event_cb(pointer, triangle_draw, LV_EVENT_DRAW_MAIN, NULL);
    ui.critical = plain(root, 2, 116, 88, 54, 0);
    ui.critical_frame = plain(ui.critical, 5, 7, 78, 40, 5);
    /* Sibling fill/text stay fixed while the outer frame expands/contracts. */
    ui.critical_fill = plain(ui.critical, 10, 14, 68, 24, 0);
    ui.critical_text = label(ui.critical_fill, "CRITICAL", &font_sentry_critical_14);
    lv_obj_center(ui.critical_text);
    ui.critical_active = false;
    ui.critical_inverted = false;
    ui.critical_threshold = SENTRY_DEFAULT_CRITICAL_THRESHOLD;
    ui.critical_phase_ms = SENTRY_DEFAULT_CRITICAL_PHASE_MS;
    ui.critical_epoch = lv_tick_get();
    ui.critical_requested = false;
    ui.critical_timer = lv_timer_create(critical_tick, SENTRY_DEFAULT_CRITICAL_PHASE_MS, NULL);
    lv_timer_pause(ui.critical_timer);
    critical_paint();
    lv_obj_add_event_cb(ui.critical, critical_delete, LV_EVENT_DELETE, NULL);
    ui.temp_bar = make_gauge(root, "Temp", 233, 81, 145);
    ui.range_bar = make_gauge(root, "R(M)", 290, 81, 145);
    /* Full side rules were missing before; draw last so they remain continuous. */
    plain(root, 0, 0, 1, 240, 1);
    plain(root, 319, 0, 1, 240, 1);
    ui.state = (sentry_state_t){ .rounds_remaining = 500, .temp = 48, .range_m = 0, .station = 'A' };
    update();
}
