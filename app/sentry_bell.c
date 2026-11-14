#include "sentry_bell.h"
#include <math.h>
#include <string.h>

/* Additive warning tone with two harmonic families and fixed relative weights.
 * All partials are below Nyquist, unlike an aliased raw square/saw oscillator. */
static const float frequency[SENTRY_BELL_PARTIALS] = {
    1324, 2648, 3972, 5296, 6620, 1687, 3374, 5061, 6748
};
static const int weight[SENTRY_BELL_PARTIALS] = {45, 100, 80, 45, 35, 85, 85, 35, 40};
enum { WEIGHT_SUM = 550, ATTACK_SAMPLES = 72, RELEASE_SAMPLES = 96 };

void sentry_bell_init(sentry_bell_t *bell, unsigned volume_percent) {
    memset(bell, 0, sizeof(*bell));
    if(volume_percent > 100) volume_percent = 100;
    bell->level = (float)volume_percent / 100.0f;
    for(unsigned i = 0; i < SENTRY_BELL_TABLE_SIZE; ++i)
        bell->sine[i] = (int16_t)lroundf(32767.0f * sinf(6.28318530718f * i / SENTRY_BELL_TABLE_SIZE));
    for(unsigned i = 0; i < SENTRY_BELL_PARTIALS; ++i)
        bell->step[i] = (uint32_t)((double)frequency[i] * 4294967296.0 / SENTRY_BELL_SAMPLE_RATE);
}

void sentry_bell_set_gate(sentry_bell_t *bell, bool on) {
    if(on && !bell->gate) {
        /* Identical attack per strike, with a smooth envelope rather than clicks. */
        if(bell->envelope == 0) memset(bell->phase, 0, sizeof(bell->phase));
    }
    bell->gate = on;
}

void sentry_bell_sequence_configure(sentry_bell_sequence_t *sequence, bool active, uint32_t phase_ms) {
    if(!active || !sequence->active) sequence->acknowledged = false;
    sequence->active = active;
    sequence->position = 0;
    sequence->phase_samples = (uint64_t)(phase_ms ? phase_ms : 1) * SENTRY_BELL_SAMPLE_RATE / 1000;
}

void sentry_bell_sequence_acknowledge(sentry_bell_sequence_t *sequence) {
    if(sequence->active) sequence->acknowledged = true;
}

void sentry_bell_sequence_render(sentry_bell_sequence_t *sequence, sentry_bell_t *bell,
                                 uint8_t *samples, size_t count) {
    while(count) {
        bool dark = (sequence->position / sequence->phase_samples) % 2 == 0;
        sentry_bell_set_gate(bell, sequence->active && !sequence->acknowledged && dark);
        uint64_t remaining = sequence->phase_samples - sequence->position % sequence->phase_samples;
        size_t chunk = count;
        if(sequence->active && remaining < chunk) chunk = (size_t)remaining;
        sentry_bell_render(bell, samples, chunk);
        sequence->position += chunk;
        samples += chunk;
        count -= chunk;
    }
}

void sentry_bell_render(sentry_bell_t *bell, uint8_t *samples, size_t count) {
    for(size_t n = 0; n < count; ++n) {
        if(bell->gate) {
            bell->envelope += 1.0f / ATTACK_SAMPLES;
            if(bell->envelope > 1) bell->envelope = 1;
        }
        else {
            bell->envelope -= 1.0f / RELEASE_SAMPLES;
            if(bell->envelope < 0) bell->envelope = 0;
        }
        if(bell->envelope == 0 || bell->level == 0) {
            samples[n] = 128; /* Midscale silence: never leave DC at a waveform peak. */
            continue;
        }
        int32_t sum = 0;
        for(unsigned i = 0; i < SENTRY_BELL_PARTIALS; ++i) {
            uint32_t phase = bell->phase[i];
            unsigned index = phase >> 22;
            unsigned next = (index + 1) & (SENTRY_BELL_TABLE_SIZE - 1);
            float fraction = (float)(phase & 0x3fffff) / 4194304.0f;
            float sine = bell->sine[index] + fraction * (bell->sine[next] - bell->sine[index]);
            sum += (int32_t)(sine * weight[i]);
            bell->phase[i] += bell->step[i];
        }
        /* Smoothstep softens both ends of the short attack/release ramps. */
        float e = bell->envelope;
        e = e * e * (3.0f - 2.0f * e);
        int value = 128 + (int)lroundf(sum * (120.0f / (32767.0f * WEIGHT_SUM)) * bell->level * e);
        if(value < 0) value = 0;
        if(value > 255) value = 255;
        samples[n] = (uint8_t)value;
    }
}
