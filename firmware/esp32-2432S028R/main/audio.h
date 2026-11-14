#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

/* Standard CYD: GPIO26/DAC1 -> onboard amp -> P4. Initialization is once-only.
 * Request/read are nonblocking LVGL-thread hooks. Read reports DMA-completed
 * playback, not the generated-ahead cursor; false selects visual fallback. */
esp_err_t sentry_audio_init(void);
void sentry_audio_request(bool active, uint32_t phase_ms, void *user_data);
/* Silence the current requested episode; leaves playback/visual phase intact.
 * LVGL-thread only. A new episode or explicit reconfiguration re-arms sound. */
void sentry_audio_acknowledge(void);
bool sentry_audio_read_clock(bool *active, bool *inverted, void *user_data);
