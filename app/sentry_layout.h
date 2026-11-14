#ifndef ALIENS_SENTRY_LAYOUT_H
#define ALIENS_SENTRY_LAYOUT_H

/* Physical CYD safe area, mapped from a 320x240 design coordinate system.
 * Fill the panel with a 2 px border clearance. Independent font height keeps
 * glyph proportions unchanged when the layout grows. */
enum {
    SENTRY_DESIGN_W = 320,
    SENTRY_DESIGN_H = 240,
    SENTRY_VIEW_X = 2,
    SENTRY_VIEW_Y = 2,
    SENTRY_VIEW_W = 316,
    SENTRY_VIEW_H = 236,
    SENTRY_FONT_H = 204,
};

static inline int sentry_scale_x(int value) {
    return (value * SENTRY_VIEW_W + SENTRY_DESIGN_W / 2) / SENTRY_DESIGN_W;
}
static inline int sentry_scale_y(int value) {
    return (value * SENTRY_VIEW_H + SENTRY_DESIGN_H / 2) / SENTRY_DESIGN_H;
}
#endif
