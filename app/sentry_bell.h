#ifndef ALIENS_SENTRY_BELL_H
#define ALIENS_SENTRY_BELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Additive tone synthesis. Output: unsigned DAC8 at the declared sample rate. */
enum { SENTRY_BELL_SAMPLE_RATE = 24000, SENTRY_BELL_PARTIALS = 9, SENTRY_BELL_TABLE_SIZE = 1024 };
typedef struct {
    int16_t sine[SENTRY_BELL_TABLE_SIZE];
    uint32_t phase[SENTRY_BELL_PARTIALS];
    uint32_t step[SENTRY_BELL_PARTIALS];
    float envelope;
    float level;
    bool gate;
} sentry_bell_t;

/* Single-owner API: call only from the audio task (or a desktop test). */
void sentry_bell_init(sentry_bell_t *bell, unsigned volume_percent);
void sentry_bell_set_gate(sentry_bell_t *bell, bool on);
void sentry_bell_render(sentry_bell_t *bell, uint8_t *samples, size_t count);

/* Sample-clocked warning sequencer. Zero-initialize before first use.
 * Configure only on state/config changes,
 * never once per visual frame. Rendering chunks may cross any phase boundary. */
typedef struct {
    bool active;
    bool acknowledged; /* Silence this episode without stopping its clock. */
    uint64_t position;
    uint64_t phase_samples;
} sentry_bell_sequence_t;
void sentry_bell_sequence_configure(sentry_bell_sequence_t *sequence, bool active, uint32_t phase_ms);
void sentry_bell_sequence_acknowledge(sentry_bell_sequence_t *sequence);
void sentry_bell_sequence_render(sentry_bell_sequence_t *sequence, sentry_bell_t *bell,
                                 uint8_t *samples, size_t count);

#endif
