#ifndef ALIENS_SENTRY_UI_H
#define ALIENS_SENTRY_UI_H

#include <stdbool.h>
#include <stdint.h>

/* Default critical threshold and phase duration. A full cycle is two phases. */
enum { SENTRY_DEFAULT_CRITICAL_THRESHOLD = 10, SENTRY_DEFAULT_CRITICAL_PHASE_MS = 171 };

typedef struct {
    uint16_t rounds_remaining;
    uint8_t temp;
    uint8_t range_m;
    char station;
    bool critical;
    /* Zero-initialized callers retain the original rounds / 15 timing. */
    bool time_override;
    uint16_t time_centiseconds; /* 0..9999, used when time_override is true */
} sentry_state_t;

void sentry_ui_create(void);
void sentry_ui_set_station(char station);
void sentry_ui_set_state(const sentry_state_t *state);
/* Critical at rounds <= threshold (or state.critical). Defaults: 10, 171 ms.
 * phase_ms is the duration of each phase, clamped to at least 1 ms.
 * Call after create, on the LVGL thread. Threshold is clamped to 999. */
void sentry_ui_configure_critical(uint16_t threshold, uint32_t phase_ms);

/* Platform-neutral phase observer, called synchronously on the LVGL thread.
 * Never block/render/re-enter the UI here: queue an audio command instead.
 * dark/compact = active && !inverted; that is the audible phase in the clip.
 * Registration immediately reports current state; ordinary telemetry does not
 * retrigger it. Configuration restarts the phase. Deletion reports inactive
 * and clears registration, so register again after sentry_ui_create(). */
typedef void (*sentry_critical_callback_t)(bool active, bool inverted, void *user_data);
void sentry_ui_set_critical_callback(sentry_critical_callback_t callback, void *user_data);

/* Optional playback clock. Both functions execute on the LVGL thread and must
 * be nonblocking. Requests occur only on entry/exit, configuration or attachment;
 * phase updates NEVER feed back into requests. read returns completed playback
 * state (not generated-ahead state), or false to use the local visual fallback.
 * Pass NULL/NULL to detach. Register again after UI recreation. */
typedef void (*sentry_critical_request_t)(bool active, uint32_t phase_ms, void *user_data);
typedef bool (*sentry_critical_clock_t)(bool *active, bool *inverted, void *user_data);
void sentry_ui_set_critical_clock(sentry_critical_request_t request,
                                  sentry_critical_clock_t read, void *user_data);

#ifdef ALIENS_TEST
#include "lvgl.h"

lv_obj_t *gauge_create(lv_obj_t *parent, const char *title, int32_t x, int32_t y);
#endif

#endif
