/* Render a WAV from the sample-clocked sequencer + playback-driven UI.
 * No SDL audio device is opened; listen to the resulting file explicitly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "sentry_ui.h"
#include "sentry_bell.h"

static sentry_bell_t bell;
static sentry_bell_sequence_t sequence;
static uint8_t pixels[320 * 240 * 2];
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    (void)area; (void)data;
    lv_display_flush_ready(display);
}
static void request(bool active, uint32_t phase_ms, void *data) {
    (void)data;
    sentry_bell_sequence_configure(&sequence, active, phase_ms);
}
static bool playback(bool *active, bool *inverted, void *data) {
    (void)data;
    *active = sequence.active;
    *inverted = *active && ((sequence.position / sequence.phase_samples) % 2);
    return true;
}
static void le16(FILE *f, unsigned n) { fputc(n & 255, f); fputc((n >> 8) & 255, f); }
static void le32(FILE *f, unsigned n) { le16(f, n); le16(f, n >> 16); }

int main(int argc, char **argv) {
    const char *path = NULL;
    unsigned volume = 15;
    bool stall_ui = false;
    for(int i = 1; i < argc; ++i) {
        if(!strcmp(argv[i], "--stall-ui")) stall_ui = true;
        else if(!strcmp(argv[i], "--wav") && i+1 < argc) path = argv[++i];
        else if(!strcmp(argv[i], "--volume") && i+1 < argc) {
            char *end;
            const char *text = argv[++i];
            long n = strtol(text, &end, 10);
            if(end == text || *end || n < 0 || n > 100) return 2;
            volume = (unsigned)n;
        }
        else { fprintf(stderr, "Usage: %s --wav output.wav [--volume 0..100] [--stall-ui]\n", argv[0]); return 2; }
    }
    if(!path) return 2;
    FILE *file = fopen(path, "wb");
    if(!file) { perror(path); return 1; }
    const unsigned seconds = 3, bytes = seconds * SENTRY_BELL_SAMPLE_RATE * 2;
    fwrite("RIFF", 1, 4, file); le32(file, 36+bytes); fwrite("WAVEfmt ", 1, 8, file);
    le32(file, 16); le16(file, 1); le16(file, 1); le32(file, SENTRY_BELL_SAMPLE_RATE);
    le32(file, SENTRY_BELL_SAMPLE_RATE*2); le16(file, 2); le16(file, 16);
    fwrite("data", 1, 4, file); le32(file, bytes);

    sentry_bell_init(&bell, volume);
    lv_init();
    lv_display_t *display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    sentry_ui_create();
    sentry_ui_set_critical_clock(request, playback, NULL);
    sentry_state_t state = {.rounds_remaining = 500, .station = 'B'};
    uint8_t block[SENTRY_BELL_SAMPLE_RATE/1000];
    for(unsigned ms = 0; ms < seconds*1000; ++ms) {
        if(ms == 250) state.rounds_remaining = SENTRY_DEFAULT_CRITICAL_THRESHOLD;
        if(ms == 2700) state.rounds_remaining = 500;
        /* Simulate a half-second display stall, without starving audio. */
        if(!(stall_ui && ms >= 700 && ms < 1200)) {
            sentry_ui_set_state(&state);
            lv_timer_handler();
        }
        sentry_bell_sequence_render(&sequence, &bell, block, sizeof(block));
        for(unsigned i = 0; i < sizeof(block); ++i)
            le16(file, (uint16_t)((int)block[i]-128)*256u);
        lv_tick_inc(1);
    }
    lv_deinit();
    bool ok = !ferror(file);
    if(fclose(file) != 0) ok = false;
    if(ok) printf("Synthesized preview: %s (24 kHz, sample-clocked %d ms phases, %u%% level)\n", path, SENTRY_DEFAULT_CRITICAL_PHASE_MS, volume);
    return ok ? 0 : 1;
}
