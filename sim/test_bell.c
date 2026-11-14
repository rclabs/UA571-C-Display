#include <stdio.h>
#include <string.h>
#include "sentry_bell.h"

#define CHECK(c) do { if(!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while(0)
static sentry_bell_t a, b;
static uint8_t whole[4096], chunked[4096];
static uint8_t scheduled[3 * 8208], varied[3 * 8208];

int main(void) {
    sentry_bell_init(&a, 100);
    sentry_bell_render(&a, whole, sizeof(whole));
    for(unsigned i = 0; i < sizeof(whole); ++i) CHECK(whole[i] == 128);
    sentry_bell_set_gate(&a, true);
    sentry_bell_render(&a, whole, sizeof(whole));
    unsigned changes = 0;
    for(unsigned i = 0; i < sizeof(whole); ++i) {
        CHECK(whole[i] > 0 && whole[i] < 255);
        if(whole[i] != 128) ++changes;
    }
    CHECK(changes > sizeof(whole)/2);
    sentry_bell_init(&b, 100);
    sentry_bell_set_gate(&b, true);
    sentry_bell_render(&b, chunked, 19);
    sentry_bell_set_gate(&b, true); /* Already on: no oscillator/envelope restart. */
    sentry_bell_render(&b, chunked+19, sizeof(chunked)-19);
    CHECK(memcmp(whole, chunked, sizeof(whole)) == 0);
    sentry_bell_set_gate(&a, false);
    sentry_bell_render(&a, whole, sizeof(whole));
    for(unsigned i = 100; i < sizeof(whole); ++i) CHECK(whole[i] == 128);
    sentry_bell_init(&a, 0);
    sentry_bell_set_gate(&a, true);
    sentry_bell_render(&a, whole, sizeof(whole));
    for(unsigned i = 0; i < sizeof(whole); ++i) CHECK(whole[i] == 128);
    sentry_bell_init(&a, 15);
    sentry_bell_set_gate(&a, true);
    sentry_bell_render(&a, whole, sizeof(whole));
    for(unsigned i = 0; i < sizeof(whole); ++i) CHECK(whole[i] >= 110 && whole[i] <= 146);
    sentry_bell_sequence_t sequence = {0};
    sentry_bell_sequence_configure(&sequence, true, 171);
    CHECK(sequence.phase_samples == 4104);
    sentry_bell_init(&a, 15);
    sentry_bell_sequence_render(&sequence, &a, scheduled, sizeof(scheduled));
    /* Independent per-sample oracle: boundaries must land at exactly 4104,
     * 8208, ... regardless of render chunks or the cadence of UI callbacks. */
    sentry_bell_init(&b, 15);
    for(size_t i = 0; i < sizeof(varied); ++i) {
        sentry_bell_set_gate(&b, (i / 4104) % 2 == 0);
        sentry_bell_render(&b, varied+i, 1);
    }
    CHECK(memcmp(scheduled, varied, sizeof(varied)) == 0);
    sentry_bell_init(&b, 15);
    sentry_bell_sequence_configure(&sequence, true, 171);
    for(size_t i = 0; i < sizeof(varied);) {
        size_t count = (i * 17 + 83) % 1100 + 1;
        if(count > sizeof(varied)-i) count = sizeof(varied)-i;
        sentry_bell_sequence_render(&sequence, &b, varied+i, count);
        i += count;
    }
    CHECK(memcmp(scheduled, varied, sizeof(varied)) == 0);
    sentry_bell_sequence_configure(&sequence, false, 171);
    sentry_bell_sequence_render(&sequence, &b, whole, sizeof(whole));
    for(unsigned i = 100; i < sizeof(whole); ++i) CHECK(whole[i] == 128);
    /* Acknowledging preserves phase position and silences all later pulses. */
    sentry_bell_sequence_configure(&sequence, true, 171);
    sentry_bell_sequence_render(&sequence, &b, whole, 1000);
    uint64_t position = sequence.position;
    sentry_bell_sequence_acknowledge(&sequence);
    sentry_bell_sequence_acknowledge(&sequence); /* Repeated taps are harmless. */
    CHECK(sequence.active && sequence.acknowledged && sequence.position == position);
    sentry_bell_sequence_render(&sequence, &b, varied, sizeof(varied));
    CHECK(sequence.position == position + sizeof(varied));
    for(unsigned i = 100; i < sizeof(varied); ++i) CHECK(varied[i] == 128);
    sentry_bell_sequence_configure(&sequence, false, 171);
    sentry_bell_sequence_acknowledge(&sequence); /* Tap outside an alarm does nothing. */
    CHECK(!sequence.acknowledged);
    sentry_bell_sequence_configure(&sequence, true, 171);
    sentry_bell_sequence_render(&sequence, &b, whole, sizeof(whole));
    CHECK(!sequence.acknowledged);
    changes = 0;
    for(unsigned i = 0; i < sizeof(whole); ++i) if(whole[i] != 128) ++changes;
    CHECK(changes > sizeof(whole)/2); /* Next episode sounds normally. */
    sentry_bell_sequence_configure(&sequence, true, UINT32_MAX);
    CHECK(sequence.phase_samples == (uint64_t)UINT32_MAX * 24);
    sentry_bell_sequence_configure(&sequence, true, 0);
    CHECK(sequence.phase_samples == 24);
    sentry_bell_init(&b, 15);
    sentry_bell_sequence_render(&sequence, &b, varied, 25);
    CHECK(!b.gate); /* Clamped 1 ms phase: boundary inside this buffer. */
    puts("PASS: synthesis, silence/mute, clipping, release, exact sample boundaries, arbitrary chunks");
    return 0;
}
