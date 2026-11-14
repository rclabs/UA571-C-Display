#include "audio.h"
#include "sentry_bell.h"
#include "sdkconfig.h"
#include <esp_log.h>

#if CONFIG_ALIENS_AUDIO_ENABLED
#include <inttypes.h>
#include <driver/dac_continuous.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#if !CONFIG_DAC_DMA_AUTO_16BIT_ALIGN
#error "Enable CONFIG_DAC_DMA_AUTO_16BIT_ALIGN for the DAC8 synthesizer"
#endif

static const char *TAG = "sentry_audio";
static QueueHandle_t commands;
static dac_continuous_handle_t dac;
static sentry_bell_t bell;
static sentry_bell_sequence_t sequence;
/* 10 ms full descriptors, at most 80 ms queued. Large writes avoid the old
 * 2 ms write path's repeated descriptor splitting and very short refill margin.
 * Render ahead; the DAC sample clock, NOT task wakeups, determines every gate. */
enum { DMA_SAMPLES = 240, DMA_DESCRIPTORS = 8, CHUNK_SAMPLES = 960, HISTORY = 16 };
typedef struct { bool active; uint32_t phase_ms, generation; } command_t;
typedef struct { uint64_t start, phase_samples; bool active; } segment_t;
static portMUX_TYPE clock_lock = portMUX_INITIALIZER_UNLOCKED;
/* Episode identity prevents acknowledgements leaking into a later alarm. */
static bool requested_active, acknowledge_pending;
static uint32_t request_generation, acknowledge_generation;
static struct {
    bool healthy;
    uint64_t played;
    uint32_t buffers, stops;
    segment_t segments[HISTORY];
    unsigned head, count;
    uint64_t last_read, last_epoch, last_phase, max_poll_gap, missed_phases;
    bool read_before;
} clock_state;

static bool IRAM_ATTR converted(dac_continuous_handle_t handle, const dac_event_data_t *event, void *arg) {
    (void)handle; (void)arg;
    portENTER_CRITICAL_ISR(&clock_lock);
    /* write_bytes is the actual descriptor length, which can be shorter than
     * buf_size. IDF expands each DAC8 sample into a 16-bit DMA word. */
    clock_state.played += event->write_bytes / 2;
    ++clock_state.buffers;
    portEXIT_CRITICAL_ISR(&clock_lock);
    return false;
}

static bool IRAM_ATTR stopped(dac_continuous_handle_t handle, const dac_event_data_t *event, void *arg) {
    (void)handle; (void)event; (void)arg;
    portENTER_CRITICAL_ISR(&clock_lock);
    ++clock_state.stops;
    portEXIT_CRITICAL_ISR(&clock_lock);
    return false;
}

static bool record_segment(uint64_t start, bool active, uint64_t phase_samples) {
    portENTER_CRITICAL(&clock_lock);
    /* Never overwrite a segment still needed to describe queued playback. */
    unsigned next = (clock_state.head + 1) % HISTORY;
    if(clock_state.count == HISTORY && clock_state.segments[(next + 1) % HISTORY].start > clock_state.played) {
        portEXIT_CRITICAL(&clock_lock);
        return false;
    }
    clock_state.head = next;
    clock_state.segments[next] = (segment_t){start, phase_samples, active};
    if(clock_state.count < HISTORY) ++clock_state.count;
    portEXIT_CRITICAL(&clock_lock);
    return true;
}

static void audio_task(void *arg) {
    (void)arg;
    uint8_t samples[CHUNK_SAMPLES];
    uint64_t generated = 0;
    uint32_t generation = 0;
    int64_t last_log = esp_timer_get_time(), max_render_us = 0;
    for(;;) {
        command_t next;
        if(xQueueReceive(commands, &next, 0) == pdTRUE) {
            sentry_bell_sequence_configure(&sequence, next.active, next.phase_ms);
            /* Overwrite may skip an intervening exit: reset mute explicitly. */
            sequence.acknowledged = false;
            generation = next.generation;
            if(!record_segment(generated, next.active, sequence.phase_samples)) {
                ESP_LOGE(TAG, "Playback history overflow; stopping audio");
                break;
            }
        }
        portENTER_CRITICAL(&clock_lock);
        bool acknowledge = acknowledge_pending && acknowledge_generation == generation;
        portEXIT_CRITICAL(&clock_lock);
        if(acknowledge) sentry_bell_sequence_acknowledge(&sequence);
        int64_t begin = esp_timer_get_time();
        sentry_bell_sequence_render(&sequence, &bell, samples, sizeof(samples));
        int64_t render_us = esp_timer_get_time() - begin;
        if(render_us > max_render_us) max_render_us = render_us;
        size_t written = 0;
        esp_err_t err = dac_continuous_write(dac, samples, sizeof(samples), &written, 1000);
        generated += written;
        portENTER_CRITICAL(&clock_lock);
        uint32_t stops = clock_state.stops, buffers = clock_state.buffers;
        uint64_t played = clock_state.played, gap = clock_state.max_poll_gap, missed = clock_state.missed_phases;
        portEXIT_CRITICAL(&clock_lock);
        if(err != ESP_OK || written != sizeof(samples) || stops) {
            ESP_LOGE(TAG, "Stream fault: %s, bytes=%u/%u, DMA stops=%"PRIu32"; visual fallback enabled",
                     esp_err_to_name(err), (unsigned)written, (unsigned)sizeof(samples), stops);
            break;
        }
        if(esp_timer_get_time() - last_log >= 10000000) {
            ESP_LOGI(TAG, "Clock: played=%"PRIu64", buffers=%"PRIu32", queued=%"PRIu64
                     " samples, DMA stops=%"PRIu32", max render=%"PRIi64" us, max UI gap=%"PRIu64
                     " samples, missed visual phases=%"PRIu64,
                     played, buffers, generated - played, stops, max_render_us, gap, missed);
            last_log = esp_timer_get_time();
        }
    }
    portENTER_CRITICAL(&clock_lock);
    clock_state.healthy = false;
    portEXIT_CRITICAL(&clock_lock);
    /* Try to park at midpoint before stopping. No busy loop or LVGL calls. */
    for(unsigned i = 0; i < sizeof(samples); ++i) samples[i] = 128;
    dac_continuous_write(dac, samples, sizeof(samples), NULL, 100);
    vTaskDelay(pdMS_TO_TICKS(100));
    dac_continuous_disable(dac);
    vTaskDelete(NULL);
}

esp_err_t sentry_audio_init(void) {
    if(commands) return ESP_ERR_INVALID_STATE;
    commands = xQueueCreate(1, sizeof(command_t));
    if(!commands) return ESP_ERR_NO_MEM;
    const dac_continuous_config_t config = {
        /* CH0/GPIO25 belongs to touch. Enable ONLY CH1/GPIO26. */
        .chan_mask = DAC_CHANNEL_MASK_CH1,
        .desc_num = DMA_DESCRIPTORS,
        .buf_size = DMA_SAMPLES * 2,
        .freq_hz = SENTRY_BELL_SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    esp_err_t err = dac_continuous_new_channels(&config, &dac);
    if(err != ESP_OK) goto fail_queue;
    const dac_event_callbacks_t callbacks = {.on_convert_done = converted, .on_stop = stopped};
    err = dac_continuous_register_event_callback(dac, &callbacks, NULL);
    if(err != ESP_OK) goto fail_dac;
    err = dac_continuous_enable(dac);
    if(err != ESP_OK) goto fail_dac;
    sentry_bell_init(&bell, CONFIG_ALIENS_AUDIO_VOLUME);
    sentry_bell_sequence_configure(&sequence, false, 1);
    clock_state.segments[0] = (segment_t){0, sequence.phase_samples, false};
    clock_state.count = 1;
    clock_state.healthy = true;
    if(xTaskCreate(audio_task, "sentry_audio", 6144, NULL, 5, NULL) != pdPASS) {
        clock_state.healthy = false;
        err = ESP_ERR_NO_MEM;
        dac_continuous_disable(dac);
        goto fail_dac;
    }
    ESP_LOGI(TAG, "Sample-clocked warning: GPIO26, %d Hz, volume %d%%", SENTRY_BELL_SAMPLE_RATE, CONFIG_ALIENS_AUDIO_VOLUME);
    return ESP_OK;
fail_dac:
    dac_continuous_del_channels(dac);
    dac = NULL;
fail_queue:
    vQueueDelete(commands);
    commands = NULL;
    return err;
}

void sentry_audio_request(bool active, uint32_t phase_ms, void *user_data) {
    (void)user_data;
    if(commands) {
        portENTER_CRITICAL(&clock_lock);
        requested_active = active;
        ++request_generation;
        acknowledge_pending = false;
        const command_t command = {active, phase_ms, request_generation};
        portEXIT_CRITICAL(&clock_lock);
        xQueueOverwrite(commands, &command);
    }
}

void sentry_audio_acknowledge(void) {
    portENTER_CRITICAL(&clock_lock);
    if(clock_state.healthy && requested_active) {
        acknowledge_generation = request_generation;
        acknowledge_pending = true;
    }
    portEXIT_CRITICAL(&clock_lock);
}

bool sentry_audio_read_clock(bool *active, bool *inverted, void *user_data) {
    (void)user_data;
    portENTER_CRITICAL(&clock_lock);
    bool valid = clock_state.healthy;
    uint64_t played = clock_state.played;
    unsigned oldest = (clock_state.head + HISTORY + 1 - clock_state.count) % HISTORY;
    segment_t segment = clock_state.segments[oldest];
    for(unsigned i = 0; i < clock_state.count; ++i) {
        segment_t candidate = clock_state.segments[(oldest + i) % HISTORY];
        if(candidate.start > played) break;
        segment = candidate;
    }
    uint64_t phase = segment.phase_samples ? (played - segment.start) / segment.phase_samples : 0;
    *active = valid && clock_state.buffers && segment.active;
    *inverted = *active && (phase % 2);
    if(clock_state.read_before) {
        uint64_t gap = played - clock_state.last_read;
        if(gap > clock_state.max_poll_gap) clock_state.max_poll_gap = gap;
        if(*active && segment.start == clock_state.last_epoch && phase > clock_state.last_phase + 1)
            clock_state.missed_phases += phase - clock_state.last_phase - 1;
    }
    clock_state.last_read = played;
    clock_state.last_epoch = segment.start;
    clock_state.last_phase = phase;
    clock_state.read_before = true;
    portEXIT_CRITICAL(&clock_lock);
    return valid;
}
#else
esp_err_t sentry_audio_init(void) { return ESP_ERR_NOT_SUPPORTED; }
void sentry_audio_acknowledge(void) {}
void sentry_audio_request(bool active, uint32_t phase_ms, void *user_data) {
    (void)active; (void)phase_ms; (void)user_data;
}
bool sentry_audio_read_clock(bool *active, bool *inverted, void *user_data) {
    (void)active; (void)inverted; (void)user_data;
    return false;
}
#endif
